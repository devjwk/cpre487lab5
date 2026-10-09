# Builds util/lab5_06.ipynb and runs its cells once as a check: python3 tools/make_notebook.py $PWD/util/lab5_06.ipynb
import nbformat as nbf, sys, os
md, code = nbf.v4.new_markdown_cell, nbf.v4.new_code_cell
cells = [
md("# CprE 487 Lab 5 (Team 06): power, performance and area of the MAC units\n\n"
   "Reads the Vivado reports saved in `hw/<staged|piped>_mac/vivado/*_<N>bit.rpt` and works out the Section 4 numbers. "
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
