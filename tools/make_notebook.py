# Builds util/lab5_06.ipynb and runs its cells once as a check: python3 tools/make_notebook.py $PWD/util/lab5_06.ipynb
import nbformat as nbf, sys, os
md, code = nbf.v4.new_markdown_cell, nbf.v4.new_code_cell
cells = [
md("# CprE 487 Lab 5 (Team 06): power, performance and area of the MAC units\n\n"
   "Reads the saved Vivado reports and board logs for the six fixed-width MACs and the variable-precision MAC. "
   "Section 4 uses fixed-width results; Section 5 compares the variable design against them. "
   "A report that has not been generated yet shows up as NaN."),
code('''import re
from pathlib import Path
import pandas as pd

HW = Path("../hw") if Path("../hw").exists() else Path("hw")
F_SYS = 100e6      # PL clock of the block design (FCLK0)
T_CONSTR = 5e-9    # clock period the standalone MAC is constrained to (zedboard.xdc)'''),
md("## MAC operations in one inference\n\nEach conv/dense output value is one MAC group: `kernel x kernel x Cin` multiply-accumulates, then one result."),
code('''# name, number of outputs (groups), MACs per output
LAYERS = [
    ("conv1", 60 * 60 * 32, 5 * 5 * 3),
    ("conv2", 56 * 56 * 32, 5 * 5 * 32),
    ("conv3", 26 * 26 * 64, 3 * 3 * 32),
    ("conv4", 24 * 24 * 64, 3 * 3 * 64),
    ("conv5", 10 * 10 * 64, 3 * 3 * 64),
    ("conv6", 8 * 8 * 128, 3 * 3 * 64),
    ("dense1", 256, 2048),
    ("dense2", 200, 256),
]
layers = pd.DataFrame(LAYERS, columns=["layer", "groups", "macs_per_group"])
layers["macs"] = layers.groups * layers.macs_per_group
layers["share_%"] = (100 * layers.macs / layers.macs.sum()).round(2)
OPS, GROUPS = int(layers.macs.sum()), int(layers.groups.sum())
assert OPS == 131_595_776  # same count the framework prints (./build/ml)
print(f"{OPS:,} MACs in {GROUPS:,} groups")
layers'''),
md("## Area, timing and power from the reports"),
code('''def grab(path, pattern):
    m = re.search(pattern, path.read_text()) if path.exists() else None
    return float(m.group(1)) if m else float("nan")

rows = []
for mac in ("staged", "piped"):
    for bits in (8, 4, 2):
        d = HW / f"{mac}_mac" / "vivado"
        util, timing, power = (d / f"{n}_{bits}bit.rpt" for n in ("utilization", "timing_summary", "power"))
        rows.append(dict(
            mac=mac, bits=bits,
            LUT=grab(util, r"\\| Slice LUTs\\s+\\|\\s+(\\d+)"),
            FF=grab(util, r"\\| Slice Registers\\s+\\|\\s+(\\d+)"),
            DSP=grab(util, r"\\| DSPs\\s+\\|\\s+(\\d+)"),
            WNS_ns=grab(timing, r"WNS\\(ns\\).*\\n.*\\n\\s+(-?[\\d.]+)"),
            P_total_W=grab(power, r"Total On-Chip Power \\(W\\)\\s+\\|\\s+([\\d.]+)"),
            P_dynamic_W=grab(power, r"Dynamic \\(W\\)\\s+\\|\\s+([\\d.]+)"),
            P_static_W=grab(power, r"Device Static \\(W\\)\\s+\\|\\s+([\\d.]+)"),
        ))
ppa = pd.DataFrame(rows)
ppa["Fmax_MHz"] = (1e-6 / (T_CONSTR - ppa.WNS_ns * 1e-9)).round(1)  # fastest clock that still meets setup
ppa'''),
md("## Throughput and latency (4.1)\n\n"
   "`T` = clock period, `N` = MACs in one group, no back-pressure.\n\n"
   "| | latency of a group | time until the next group can start | throughput |\n|---|---|---|---|\n"
   "| staged | `N * T` (the sum is valid on the edge that takes the last pair) | `(N + 1) * T` (one `SEND_RESULT` cycle) | `N / ((N + 1) * T)` |\n"
   "| piped | `(N + 3) * T` (input, multiply, add, output registers) | `N * T` (groups follow back to back) | `1 / T` |"),
code('''def latency_s(mac, n, f):       # first pair in -> result valid
    return (n if mac == "staged" else n + 3) / f

def throughput(mac, n, f):      # MACs per second, groups of n sent continuously
    return f * n / (n + 1) if mac == "staged" else f

rows = []
for mac in ("staged", "piped"):
    for n in (1, 5, 100, 800):  # 800 = one conv2 output
        rows.append(dict(mac=mac, N=n, latency_ns=latency_s(mac, n, F_SYS) * 1e9, MACs_per_s_M=throughput(mac, n, F_SYS) / 1e6))
pd.DataFrame(rows).round(2)  # at the 100 MHz system clock; replace F_SYS with Fmax for the standalone limit'''),
md("## Energy of one inference (4.2)\n\n"
   "Ideal case from the handout: the MAC runs at full throughput and data movement costs nothing, so "
   "`time = cycles / f` and `energy = power * time`, with `f` = the 200 MHz constraint clock of the power reports. Staged needs one extra cycle per group; piped only the 3-cycle pipeline fill."),
code('''def inference(row, f=1 / T_CONSTR, pes=1):  # same clock the power reports were estimated at (200 MHz)
    cycles = OPS + GROUPS if row.mac == "staged" else OPS + 3
    t = cycles / f / pes                                   # work split evenly over the PEs
    power = row.P_static_W + pes * row.P_dynamic_W         # static power is paid once, dynamic per PE
    return pd.Series(dict(time_s=t, power_W=power, energy_J=power * t, inferences_per_s=1 / t))

one = pd.concat([ppa[["mac", "bits"]], ppa.apply(inference, axis=1)], axis=1)
one'''),
md("## More than one PE (4.3)\n\nThroughput and area scale with the number of PEs, dynamic power too; static power does not, so energy per inference falls slightly."),
code('''rows = []
for _, r in ppa.iterrows():
    for pes in (1, 3, 5, 100):
        rows.append(dict(mac=r.mac, bits=r.bits, PEs=pes, LUT=r.LUT * pes, FF=r.FF * pes, DSP=r.DSP * pes, **inference(r, pes=pes)))
pd.DataFrame(rows)'''),
md("## MAC inside the full system (PS7 + AXI FIFO + MAC, 100 MHz)\n\n"
   "From the reports saved next to each XSA in `hw/xsa`. A block Vivado lists below 1 mW shows as NaN."),
code(r'''def row_of(path, name):   # numeric cells of the first table row that starts with this instance name
    m = re.search(r"^\|\s+" + name + r"\s+\|(.*)$", path.read_text(), re.M) if path.exists() else None
    return [float(c) for c in m.group(1).split("|") if c.strip().replace(".", "").isdigit()] if m else []

rows = []
for mac in ("staged", "piped"):
    for bits in (8, 4, 2):
        base = HW / "xsa" / f"{mac}_mac_{bits}bit_system_"
        util, timing, power = (Path(str(base) + n + ".rpt") for n in ("utilization", "timing", "power"))
        u, fifo = row_of(util, f"{mac}_mac_0"), row_of(util, "axi_fifo_mm_s_0")
        rows.append(dict(
            mac=mac, bits=bits,
            MAC_LUT=u[0] if u else float("nan"), MAC_FF=u[4] if u else float("nan"), MAC_DSP=u[7] if u else float("nan"),
            FIFO_LUT=fifo[0] if fifo else float("nan"), FIFO_FF=fifo[4] if fifo else float("nan"),
            WNS_ns=grab(timing, r"WNS\(ns\).*\n.*\n\s+(-?[\d.]+)"),
            P_total_W=grab(power, r"Total On-Chip Power \(W\)\s+\|\s+([\d.]+)"),
            P_PS7_W=(row_of(power, "processing_system7_0") or [float("nan")])[0],
            P_FIFO_W=(row_of(power, "axi_fifo_mm_s_0") or [float("nan")])[0],
            P_MAC_W=(row_of(power, f"{mac}_mac_0") or [float("nan")])[0],
        ))
system = pd.DataFrame(rows)
system'''),
md("## Measured on the board (4.5)\n\n"
   "One inference of image_0 on the ZedBoard, from the framework output saved in `util/board_logs` "
   "(`Layer N ...: MATCH, ..., software X ms, MAC unit Y ms`). `software` is the same quantized model run on the ARM core alone."),
code(r"""LINE = re.compile(r"Layer (\d+) (\w+): (\w+), MAC ops (\d+), packets (\d+), software ([\d.]+) ms, MAC unit ([\d.]+) ms")
rows = []
for mac in ("staged", "piped"):
    for bits in (8, 4, 2):
        log = Path("board_logs" if Path("board_logs").exists() else "util/board_logs") / f"{mac}{bits}.txt"
        for m in LINE.finditer(log.read_text(errors="ignore") if log.exists() else ""):
            rows.append(dict(mac=mac, bits=bits, layer=int(m[1]), type=m[2], match=m[3] == "MATCH",
                             macs=int(m[4]), software_ms=float(m[6]), mac_unit_ms=float(m[7])))
layers_hw = pd.DataFrame(rows)
board = layers_hw.groupby(["mac", "bits"], sort=False).agg(
    all_match=("match", "all"), software_ms=("software_ms", "sum"), mac_unit_ms=("mac_unit_ms", "sum")).reset_index()
board["MACs_per_s_M"] = (OPS / board.mac_unit_ms / 1e3).round(2)
board["slowdown_vs_software"] = (board.mac_unit_ms / board.software_ms).round(1)
board["ideal_ms_100MHz"] = [round(1e3 * (OPS + (GROUPS if m == "staged" else 3)) / F_SYS) for m in board.mac]
board"""),
md("Per-layer profile of the 8-bit staged run: the time follows the number of MACs, about 0.22 us per MAC in every layer."),
code("""p = layers_hw[(layers_hw.mac == "staged") & (layers_hw.bits == 8) & (layers_hw.macs > 0)].copy()
p["us_per_MAC"] = (1e3 * p.mac_unit_ms / p.macs).round(3)
p["share_%"] = (100 * p.mac_unit_ms / p.mac_unit_ms.sum()).round(1)
p[["layer", "type", "macs", "software_ms", "mac_unit_ms", "us_per_MAC", "share_%"]]"""),
md("## Variable-precision design and layer choices (5.1, 5.3, 5.4)\n\n"
   "The MAC uses spatial accumulation: each 32-bit data word carries 16 weight bits and 16 activation bits. "
   "A packet header selects 8, 4, or 2 bits at runtime. The implemented MAC has registered brick products "
   "and an adder tree; the board self-test covers every operand pair at all three widths."),
code('''qvar = Path("../sw/framework/data/model/qvar/quant_params.txt") if Path("../sw").exists() else Path("sw/framework/data/model/qvar/quant_params.txt")
selected_bits = [int(line.split()[-1]) for line in qvar.read_text().splitlines() if line.strip()]
assert len(selected_bits) == len(layers)
variable_layers = layers[["layer", "groups", "macs_per_group", "macs"]].copy()
variable_layers["bits"] = selected_bits
variable_layers["pairs_per_word"] = 16 // variable_layers.bits
variable_layers["data_words"] = variable_layers.groups * ((variable_layers.macs_per_group + variable_layers.pairs_per_word - 1) // variable_layers.pairs_per_word)
variable_layers["words_at_8_bits"] = variable_layers.groups * ((variable_layers.macs_per_group + 1) // 2)
assert variable_layers.macs.sum() == OPS
assert variable_layers.groups.sum() == GROUPS
variable_layers'''),
md("## Variable MAC area, timing and power (5.5)\n\n"
   "Standalone reports use a 200 MHz constraint. Full-system reports include the PS7 and FIFO and use the block-design clock. "
   "Vivado power values are estimates, not measured board power."),
code('''vd = HW / "variable_mac" / "vivado"
vx = HW / "xsa"
variable_ppa = pd.Series(dict(
    LUT=grab(vd / "utilization.rpt", r"\\| Slice LUTs\\s+\\|\\s+(\\d+)"),
    FF=grab(vd / "utilization.rpt", r"\\| Slice Registers\\s+\\|\\s+(\\d+)"),
    DSP=grab(vd / "utilization.rpt", r"\\| DSPs\\s+\\|\\s+(\\d+)"),
    WNS_ns=grab(vd / "timing_summary.rpt", r"WNS\\(ns\\).*\\n.*\\n\\s+(-?[\\d.]+)"),
    P_total_W=grab(vd / "power.rpt", r"Total On-Chip Power \\(W\\)\\s+\\|\\s+([\\d.]+)"),
    P_dynamic_W=grab(vd / "power.rpt", r"Dynamic \\(W\\)\\s+\\|\\s+([\\d.]+)"),
    P_static_W=grab(vd / "power.rpt", r"Device Static \\(W\\)\\s+\\|\\s+([\\d.]+)"),
))
variable_ppa["Fmax_MHz"] = 1e-6 / (T_CONSTR - variable_ppa.WNS_ns * 1e-9)
vu = row_of(vx / "variable_mac_system_utilization.rpt", "variable_mac_0")
variable_system = pd.Series(dict(
    MAC_LUT=vu[0] if vu else float("nan"), MAC_FF=vu[4] if vu else float("nan"),
    MAC_DSP=vu[7] if vu else float("nan"),
    WNS_ns=grab(vx / "variable_mac_system_timing.rpt", r"WNS\\(ns\\).*\\n.*\\n\\s+(-?[\\d.]+)"),
    P_total_W=grab(vx / "variable_mac_system_power.rpt", r"Total On-Chip Power \\(W\\)\\s+\\|\\s+([\\d.]+)"),
    P_PS7_W=(row_of(vx / "variable_mac_system_power.rpt", "processing_system7_0") or [float("nan")])[0],
    P_FIFO_W=(row_of(vx / "variable_mac_system_power.rpt", "axi_fifo_mm_s_0") or [float("nan")])[0],
    P_MAC_W=(row_of(vx / "variable_mac_system_power.rpt", "variable_mac_0") or [float("nan")])[0],
))
print("Standalone MAC:", variable_ppa.round(3))
print("Full system:", variable_system.round(3))'''),
md("## Variable MAC board result and comparison (5.5)\n\n"
   "One image-0 inference is compared with the quantized software model layer by layer. "
   "The board time is measured; system energy below multiplies that time by Vivado's estimated power."),
code(r'''VAR_LINE = re.compile(r"Layer (\d+) (\w+): (\w+), MAC ops (\d+), words (\d+), packets (\d+), software ([\d.]+) ms, MAC unit ([\d.]+) ms")
var_log = (Path("board_logs") if Path("board_logs").exists() else Path("util/board_logs")) / "variable.txt"
var_text = var_log.read_text(errors="ignore")
var_rows = [dict(layer=int(m[1]), type=m[2], match=m[3] == "MATCH", macs=int(m[4]),
                 words=int(m[5]), packets=int(m[6]), software_ms=float(m[7]), mac_unit_ms=float(m[8]))
            for m in VAR_LINE.finditer(var_text)]
variable_board_layers = pd.DataFrame(var_rows)
assert len(variable_board_layers) == 13 and variable_board_layers["match"].all()
v_ops = int(variable_board_layers.macs.sum())
v_words = int(variable_board_layers.words.sum())
v_packets = int(variable_board_layers.packets.sum())
v_sw_ms = variable_board_layers.software_ms.sum()
v_mac_ms = variable_board_layers.mac_unit_ms.sum()
assert (v_ops, v_words, v_packets) == (OPS, 38_244_352, GROUPS)
assert v_words == int(variable_layers.data_words.sum())
assert "ALL LAYERS MATCH" in var_text and "runTests() COMPLETE" in var_text
baseline = board[(board.mac == "staged") & (board.bits == 8)].iloc[0]
comparison = pd.Series(dict(
    fixed_8_board_ms=baseline.mac_unit_ms, variable_board_ms=v_mac_ms,
    measured_speedup=baseline.mac_unit_ms / v_mac_ms,
    fixed_8_data_words=OPS, variable_data_words=v_words,
    data_word_fraction=v_words / OPS, packets=v_packets,
    variable_software_ms=v_sw_ms, variable_vs_ARM=v_mac_ms / v_sw_ms,
    fixed_8_est_board_energy_J=system[(system.mac == "staged") & (system.bits == 8)].iloc[0].P_total_W * baseline.mac_unit_ms / 1000,
    variable_est_board_energy_J=variable_system.P_total_W * v_mac_ms / 1000,
))
print(comparison.round(3))
variable_board_layers'''),
md("### Side-by-side comparison with every static MAC (5.5)\n\n"
   "The fixed-width board runs use the Lab 4 quantized files. The variable run uses the calibrated "
   "mixed-width files. Energy here is measured board time multiplied by estimated system power; "
   "it is an estimate, not a direct energy measurement."),
code('''comparison_rows = []
for _, r in ppa.iterrows():
    b = board[(board.mac == r.mac) & (board.bits == r.bits)].iloc[0]
    s = system[(system.mac == r.mac) & (system.bits == r.bits)].iloc[0]
    comparison_rows.append(dict(
        design=f"{r.mac} {int(r.bits)}-bit", LUT=r.LUT, FF=r.FF, DSP=r.DSP,
        Fmax_MHz=r.Fmax_MHz, standalone_power_W=r.P_total_W,
        board_time_ms=b.mac_unit_ms, system_power_W=s.P_total_W,
        est_system_energy_J=s.P_total_W * b.mac_unit_ms / 1000,
    ))
comparison_rows.append(dict(
    design="variable mixed", LUT=variable_ppa.LUT, FF=variable_ppa.FF, DSP=variable_ppa.DSP,
    Fmax_MHz=variable_ppa.Fmax_MHz, standalone_power_W=variable_ppa.P_total_W,
    board_time_ms=v_mac_ms, system_power_W=variable_system.P_total_W,
    est_system_energy_J=variable_system.P_total_W * v_mac_ms / 1000,
))
all_designs = pd.DataFrame(comparison_rows)
all_designs.round(3)'''),
md("## Idealized compute-only estimate (5.5)\n\n"
   "With continuous input and no back-pressure, each packet has one header plus its data words. "
   "The registered pipeline adds about seven fill cycles to the entire stream. The estimate uses the "
   "200 MHz standalone constraint and omits CPU/FIFO transfer delays, just like Section 4.2."),
code('''variable_cycles = v_words + v_packets + 7
variable_ideal_s = variable_cycles * T_CONSTR
variable_ideal_energy_mJ = 1000 * variable_ppa.P_total_W * variable_ideal_s
fixed8_ideal = one[(one.mac == "staged") & (one.bits == 8)].iloc[0]
ideal_table = pd.DataFrame([
    dict(design="fixed staged 8-bit", cycles=OPS + GROUPS, ideal_time_s=fixed8_ideal.time_s,
         standalone_power_W=fixed8_ideal.power_W, ideal_energy_mJ=1000 * fixed8_ideal.energy_J),
    dict(design="variable mixed bits", cycles=variable_cycles, ideal_time_s=variable_ideal_s,
         standalone_power_W=variable_ppa.P_total_W, ideal_energy_mJ=variable_ideal_energy_mJ),
])
ideal_table.round(3)'''),
md("## Quantization comparison and interpretation (5.3, 5.5)\n\n"
   "`util/mixed_precision_study.py` calibrates on validation images 800–999 and evaluates on images 0–799. "
   "The saved JSON holds results reproduced with that script: all-8/4/2-bit and mixed precision use the same "
   "calibrated quantization. Lab 4's fixed-width board runs use a different scale/zero-point rule. "
   "The mixed model's 1,000-image accuracy includes the 200 calibration images and should be labeled separately."),
code('''import json
accuracy_path = Path("mixed_precision_results_800.json") if Path("mixed_precision_results_800.json").exists() else Path("util/mixed_precision_results_800.json")
accuracy_data = json.loads(accuracy_path.read_text())
accuracy_table = pd.DataFrame([
    dict(quantization="calibrated", design=f"uniform {b}-bit", top1_800=accuracy_data[str(b)][0], top10_800=accuracy_data[str(b)][1])
    for b in (8, 4, 2)
] + [dict(quantization="calibrated", design="mixed 8/4/8/4/4/4/4/8",
          top1_800=accuracy_data["mixed_8_4_8_4_4_4_4_8"][0],
          top10_800=accuracy_data["mixed_8_4_8_4_4_4_4_8"][1])])
accuracy_table'''),
md("**Main driver and limit:** Packing 2 or 4 operand pairs into one FIFO data word reduces the number of "
   "ARM-to-FIFO writes and improves board time. The header costs one word per packet, and the ARM still "
   "writes each remaining word through memory-mapped I/O. The extra brick logic and pipeline registers "
   "raise MAC area and power. System power changes little because the PS7 dominates; estimated board "
   "energy falls mainly because measured execution time falls. The 2-bit mode works in hardware but "
   "is not selected for any layer because its quantization error hurts accuracy."),
]
nb = nbf.v4.new_notebook(cells=cells, metadata={"kernelspec": {"display_name": "Python 3", "language": "python", "name": "python3"}})
out = sys.argv[1]; nbf.write(nb, out)
os.chdir(os.path.dirname(out)); g = {}
pd = __import__("pandas"); pd.set_option("display.width", 200)
for c in cells:
    if c.cell_type == "code":
        *body, last = c.source.split("\n")
        exec("\n".join(body), g)
        try: print(eval(last, g), "\n")
        except SyntaxError: exec(last, g)
