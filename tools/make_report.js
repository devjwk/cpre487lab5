// Builds the Word report: (npm install docx) then node tools/make_report.js lab5_report_06.docx
const fs = require("fs");
const { Document, Packer, Paragraph, TextRun, Table, TableRow, TableCell, WidthType, ShadingType, BorderStyle,
        HeadingLevel, LevelFormat, AlignmentType } = require("docx");

const W = 9360;  // text width of US Letter with 1" margins
const FONT = "Calibri";
// `code` spans are written with backticks in the strings below
const runs = (s, base = {}) => s.split("`").map((t, i) => new TextRun({ text: t, ...base, ...(i % 2 ? { font: "Consolas", size: 20 } : {}) }));
const P = (s, o = {}) => new Paragraph({ spacing: { after: 140 }, children: runs(s), ...o });
// Lab 1 style: a bold question followed by the answer in the same paragraph
const Q = (q, a) => new Paragraph({ spacing: { after: 140 }, children: [new TextRun({ text: q + " ", bold: true }), ...runs(a)] });
const H1 = s => new Paragraph({ heading: HeadingLevel.HEADING_1, spacing: { before: 320, after: 140 }, children: [new TextRun(s)] });
const H2 = s => new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 220, after: 100 }, children: [new TextRun(s)] });
const B = s => new Paragraph({ numbering: { reference: "b", level: 0 }, spacing: { after: 60 }, children: runs(s) });
const CAP = (n, s) => new Paragraph({ spacing: { before: 80, after: 200 }, children: [new TextRun({ text: `Table ${n}. `, bold: true }), ...runs(s)] });
const border = { style: BorderStyle.SINGLE, size: 4, color: "D9D6CE" };
const borders = { top: border, bottom: border, left: border, right: border };
function T(widths, rows) {
  const scale = W / widths.reduce((a, b) => a + b, 0);
  const cw = widths.map(w => Math.round(w * scale)); cw[cw.length - 1] += W - cw.reduce((a, b) => a + b, 0);
  return new Table({ width: { size: W, type: WidthType.DXA }, columnWidths: cw,
    rows: rows.map((r, ri) => new TableRow({ tableHeader: ri === 0, children: r.map((c, ci) => new TableCell({
      width: { size: cw[ci], type: WidthType.DXA }, borders,
      margins: { top: 60, bottom: 60, left: 100, right: 100 },
      shading: ri === 0 ? { type: ShadingType.CLEAR, fill: "F3F1EA", color: "auto" } : undefined,
      children: [new Paragraph({ children: runs(String(c), { size: 20, bold: ri === 0 }) })] })) })) });
}

const body = [
  new Paragraph({ heading: HeadingLevel.TITLE, spacing: { after: 80 }, children: [new TextRun("CprE 487/587 Lab 5 Report (Group 06)")] }),
  P("Oct 8, 2026 · Jongwoo Kim · Zach Dixon", { spacing: { after: 240 } }),

  H1("1. Six MAC units (3.3)"),
  P("We made 4-bit and 2-bit versions of the staged and pipelined MAC, so we have six MACs with the 8-bit ones from Lab 3. Both MACs already had a `C_DATA_WIDTH` generic, so all versions use the same VHDL with a different generic value."),
  T([15, 8, 8, 8, 8, 13, 13, 15, 14], [
    ["MAC", "Bits", "LUT", "FF", "DSP", "WNS (ns)", "Fmax (MHz)", "Dynamic power (W)", "Static power (W)"],
    ["Staged", 8, 118, 41, 0, "0.834", "240.0", "0.025", "0.105"],
    ["Staged", 4, 76, 41, 0, "1.100", "256.4", "0.023", "0.105"],
    ["Staged", 2, 53, 41, 0, "1.283", "269.0", "0.023", "0.105"],
    ["Pipelined", 8, 41, 138, 1, "0.511", "222.8", "0.016", "0.105"],
    ["Pipelined", 4, 49, 122, 1, "0.578", "226.1", "0.015", "0.105"],
    ["Pipelined", 2, 56, 114, 1, "0.721", "233.7", "0.015", "0.105"]]),
  CAP(1, "Each MAC built alone at a 200 MHz constraint. Fmax = 1 / (5 ns - WNS)."),
  P("The staged MAC gets smaller with fewer bits (118 to 53 LUTs). The pipelined MAC does not, because its multiplier sits in one DSP block at every width."),
  P("The six XSA files are in `hw/xsa`, next to the timing, utilization and power reports of each full system. The reports of each MAC built alone are in `hw/staged_mac/vivado` and `hw/piped_mac/vivado`."),

  H1("2. Full inference on the MAC units (3.4)"),
  P("`computeAccelerated` sends every multiply-accumulate of the convolution and dense layers to the MAC unit. We ran test image 0 through each of the six MACs on the ZedBoard and compared every layer output with the software quantized result, byte by byte. All 13 layers match for all six MACs."),
  T([18, 10, 22, 26, 24], [
    ["MAC", "Bits", "All 13 layers match", "Software on the ARM core (ms)", "Through the MAC unit (ms)"],
    ["Staged", 8, "Yes", "1,353", "29,073"],
    ["Staged", 4, "Yes", "1,353", "29,080"],
    ["Staged", 2, "Yes", "1,348", "28,303"],
    ["Pipelined", 8, "Yes", "1,353", "29,074"],
    ["Pipelined", 4, "Yes", "1,353", "29,080"],
    ["Pipelined", 2, "Yes", "1,348", "28,303"]]),
  CAP(2, "One inference of image 0 on the ZedBoard. The logs are in `util/board_logs`."),

  H1("3. Power, performance and area (4)"),
  H2("Throughput and latency (4.1)"),
  P("`T` is the clock period and `N` is the number of MACs in one group (one conv or dense output). We assume no back-pressure."),
  T([20, 40, 40], [
    ["MAC", "Minimum latency of one group", "Maximum throughput"],
    ["Staged", "`N * T`", "`N / ((N + 1) * T)`"],
    ["Pipelined", "`(N + 3) * T`", "`1 / T`"]]),
  P("The staged MAC needs one more cycle after each group to send the result. The pipelined MAC has three more register stages, but groups can follow each other with no gap.", { spacing: { before: 140, after: 140 } }),
  T([25, 15, 30, 30], [
    ["MAC", "N", "Latency (ns)", "Throughput (M MACs/s)"],
    ["Staged", 1, 10, "50.0"], ["Staged", 5, 50, "83.3"], ["Staged", 100, "1,000", "99.0"],
    ["Pipelined", 1, 40, 100], ["Pipelined", 5, 80, 100], ["Pipelined", 100, "1,030", 100]]),
  CAP(3, "Example values at T = 10 ns (the 100 MHz system clock)."),

  H2("Power, area and energy of one inference (4.2)"),
  P("The peak power and the area of each MAC are in Table 1. One inference of the Toy Model needs 131,595,776 MACs in 310,728 groups. For the energy we use the 200 MHz clock, because the power reports were made at that clock."),
  P("Staged, 8-bit:"),
  B("Cycles = 131,595,776 MACs + 310,728 groups = 131,906,504"),
  B("Time = 131,906,504 / 200 MHz = 0.660 s"),
  B("Power = 0.105 W static + 0.025 W dynamic = 0.130 W"),
  B("Energy = 0.130 W x 0.660 s = 85.7 mJ"),
  new Paragraph({ spacing: { after: 100 }, children: [] }),
  T([25, 25, 50], [
    ["MAC", "Time (s)", "Energy at 8 / 4 / 2 bits (mJ)"],
    ["Staged", "0.660", "85.7 / 84.4 / 84.4"],
    ["Pipelined", "0.658", "79.6 / 79.0 / 79.0"]]),
  CAP(4, "Estimated energy of one inference. The pipelined MAC needs 131,595,779 cycles (the MACs plus 3 cycles to fill the pipeline)."),

  H2("More than one PE (4.3)"),
  Q("How do power, energy, area and performance scale?", "With `P` MAC units in parallel, throughput and area grow by `P` and the time for one inference drops by `P`. Dynamic power also grows by `P`, but static power is paid only once. So the dynamic energy per inference stays the same and the static energy gets smaller."),
  T([15, 20, 22, 22, 21], [
    ["PEs", "LUT", "Time (s)", "Power (W)", "Energy (mJ)"],
    [1, 118, "0.660", "0.130", "85.7"], [3, 354, "0.220", "0.180", "39.6"],
    [5, 590, "0.132", "0.230", "30.3"], [100, "11,800", "0.0066", "2.605", "17.2"]]),
  CAP(5, "The staged 8-bit MAC with more PEs. The energy cannot go below the dynamic part, 0.025 W x 0.660 s = 16.5 mJ."),

  H2("Do these values make sense? (4.4)"),
  Q("Do they over or underestimate?", "They are far too optimistic for our board. The estimated time is 1.32 s at 100 MHz and we measured 29.07 s, which is 22 times longer. One MAC takes about 0.22 us on the board, or 22 clock cycles instead of 1."),
  Q("What are we not taking into account?", ""),
  B("Data movement. The ARM core writes one word at a time to the FIFO through memory-mapped I/O and waits for each result."),
  B("The rest of the system. The full system uses 1.683 W, and 1.529 W of that is the ARM processor. The MAC uses 2 mW or less. One real inference costs about 1.683 W x 29.07 s = 48.9 J, not 85.7 mJ."),
  B("The FIFO. It needs 764 LUTs and 704 flip-flops, more than six times the staged 8-bit MAC."),
  B("The power reports are estimates without real signal activity."),

  H2("Execution time of all implementations (4.5)"),
  P("The measured times are in Table 2. The staged and pipelined MACs differ by 1 ms at 8 bits, and the 8-bit and 4-bit runs are the same. The quantized model on the ARM core alone takes 1.35 s, so going through the MAC is 21.5 times slower."),
  Q("Why are the 2-bit runs 2.7% faster?", "This does not come from the MAC. The staged and pipelined 2-bit runs take exactly the same time, and the gain is the same in every layer, about 6 ns per word. One word takes 21.5 clock cycles in the 8-bit and 4-bit builds and 20.9 in the 2-bit build. We compared the compiled code of the 4-bit and 2-bit builds: the loop that writes to the FIFO has the same number of instructions and only the constants differ. The 8-bit build has one instruction fewer and is not faster. So the amount of code is not the reason. We think it comes from how the timing of the ARM core lines up with the 100 MHz clock each time a word crosses to the FIFO, but we did not test this."),
  T([14, 20, 16, 17, 15, 18], [
    ["Layer", "MACs", "Software (ms)", "MAC unit (ms)", "us per MAC", "Share of MAC time"],
    ["conv1", "8,640,000", 119, "1,973", "0.228", "6.8%"],
    ["conv2", "80,281,600", 674, "17,660", "0.220", "60.7%"],
    ["conv3", "12,460,032", 102, "2,762", "0.222", "9.5%"],
    ["conv4", "21,233,664", 287, "4,682", "0.220", "16.1%"],
    ["conv5", "3,686,400", 49, 813, "0.221", "2.8%"],
    ["conv6", "4,718,592", 109, "1,045", "0.221", "3.6%"],
    ["dense1", "524,288", 13, 127, "0.242", "0.4%"],
    ["dense2", "51,200", 0, 11, "0.215", "0.0%"]]),
  CAP(6, "Per-layer times for the staged 8-bit MAC. Max pooling, flatten and softmax take less than 1 ms."),
  Q("Why are the speeds the same?", "The time per MAC is the same in every layer, so the run time only follows the number of MACs. The time goes to moving words between the ARM core and the FIFO, and every version moves the same number of words."),

  H2("Effect of a variable-precision MAC (4.6)"),
  B("Area: a variable MAC must still hold the largest width, so it will not be smaller than the 8-bit MAC."),
  B("Power and energy: the MAC is 2 mW of 1.68 W, so lower precision cannot change the system energy in a way we could measure. Energy only goes down if the run time goes down."),
  B("Performance: in our system the time depends on the number of FIFO words. Lower precision helps only if more pairs fit in one 32-bit word (2 pairs at 8 bits, 4 at 4 bits, 8 at 2 bits)."),
  B("Accuracy: in Lab 4 the top-1 accuracy was 23.8% at 8 bits, 6.6% at 4 bits and 0.3% at 2 bits, so only some layers can use a lower precision."),

  H1("4. Variable-precision MAC (5)"),
  P("Not done yet. This section will hold the chosen method (5.1), the quantization level of each layer and the reason (5.3), the implementation (5.4), and the comparison with the 8, 4 and 2-bit results above (5.5)."),

  H1("5. Demo spreadsheet (6)"),
  P("Not filled in yet."),
];

const doc = new Document({
  styles: {
    default: { document: { run: { font: FONT, size: 22 } } },
    paragraphStyles: [
      { id: "Title", name: "Title", basedOn: "Normal", next: "Normal", run: { font: FONT, size: 44 } },
      { id: "Heading1", name: "Heading 1", basedOn: "Normal", next: "Normal", quickFormat: true, run: { font: FONT, size: 30 }, paragraph: { outlineLevel: 0 } },
      { id: "Heading2", name: "Heading 2", basedOn: "Normal", next: "Normal", quickFormat: true, run: { font: FONT, size: 25 }, paragraph: { outlineLevel: 1 } },
    ] },
  numbering: { config: [{ reference: "b", levels: [{ level: 0, format: LevelFormat.BULLET, text: "•", alignment: AlignmentType.LEFT,
    style: { paragraph: { indent: { left: 540, hanging: 270 } } } }] }] },
  sections: [{ properties: { page: { size: { width: 12240, height: 15840 }, margin: { top: 1440, bottom: 1440, left: 1440, right: 1440 } } }, children: body }],
});
Packer.toBuffer(doc).then(b => fs.writeFileSync(process.argv[2], b));
