<div align="center">

<img src="assets/banner.svg" alt="HARDWARE-ACCELERATED CNN INFERENCE — Quantized layers in software, their arithmetic on the FPGA" width="100%">

![Software](https://img.shields.io/badge/Software-C%2B%2B-C2410C?style=flat-square&labelColor=431407)
![RTL](https://img.shields.io/badge/RTL-VHDL-7C2D12?style=flat-square&labelColor=431407)
![Board](https://img.shields.io/badge/Board-ZedBoard-EA580C?style=flat-square&labelColor=431407)
![Stage](https://img.shields.io/badge/Stage-In%20progress-D97706?style=flat-square&labelColor=431407)

Iowa State University · CprE 487/587 · Lab 5 · Team 06

[Overview](#overview) · [Accelerated layer](#accelerated-layer) · [Where this lab fits](#where-this-lab-fits) · [Team and credits](#team-and-credits) · [Limitations](#limitations-and-next-steps)

</div>

---

> **Where it stands — In progress (checked October 8, 2026)**  
> The accelerated software path is written and matches the quantized path at 8, 4 and 2 bits, using a software model of the MAC.  
> Not done yet: the 4-bit and 2-bit MAC units, a run on the board, the PPA analysis and the variable-precision MAC.

<img src="assets/at_a_glance.svg" alt="At a glance: the accelerated path matches the quantized path at all 3 bit widths in the software model; one inference is 131.6 million multiply-accumulates, 61% of them in conv2; 2 of the 6 required MAC units exist; one image is estimated to take about 29 seconds on the board at the Lab 3 rate" width="100%">

| | |
|---|---|
| Period | October 2026 (in progress) |
| Team | 2 — Zach Dixon, Jongwoo Kim |
| Stack | C++, VHDL, Vivado/Vitis 2020.1, ZedBoard (Zynq-7000) |
| Earlier labs | [Lab 4 — quantization](https://github.com/devjwk/cpre487lab4), [Lab 3 — MAC units](https://github.com/devjwk/cpre487lab3), [Lab 2 — C++ framework](https://github.com/devjwk/cpre487lab2) |

## Overview

Labs 1–4 produced a TinyImageNet CNN, a C++ implementation of it, two hardware MAC units, and an 8/4/2-bit quantized version of the model. This lab puts them together.

- **Problem:** the quantized model still runs every multiply-add on the CPU. Conv2 alone is 80.3 M of the model's 131.6 M MACs per inference, so that is where hardware should help most.
- **Approach:** add an accelerated inference path in which conv and dense layers send (weight, activation) pairs to the MAC unit over an AXI-Stream FIFO, while max-pool, flatten and softmax stay in software.
- **Then:** measure power, performance and area (PPA), and design a variable-precision MAC.

## Where this lab fits

<img src="assets/lab_flow.svg" alt="Lab 1 · Train in TensorFlow → Lab 2 · C++ framework → Lab 3 · MAC units → Lab 4 · Quantization → Lab 5 · Hardware integration" width="100%">

## Accelerated layer

<img src="assets/accel_path.svg" alt="Accelerated layer: a conv or dense layer in C++ packs weight and activation pairs and sends them through an AXI-Stream FIFO to the MAC unit on the FPGA, which returns an int32 sum; software then adds the bias and zero-point term and dequantizes, applies ReLU and requantizes" width="100%">


## Team and credits

Lab 5 is the work of Team 06: Zach Dixon and Jongwoo Kim. It is still in progress, so this lists what exists in the repository today.

| | Zach Dixon | Jongwoo Kim |
|---|---|---|
| Lab 5 software in this repository | | Repository setup from the Lab 3 hardware and the Lab 4 framework; `computeAccelerated` and the MAC driver (`sw/framework/src/Mac.h`); moving the input zero point out of the hardware MAC |
| Lab 3 hardware this lab reuses (`hw/`) | A `staged_mac` design, the ILA debug setup and the first board test program | The 8-case board test with timing, and the re-runs of simulation and synthesis |
| Lab 4 framework this lab extends | Report co-author | Quantized inference path and its scripts; report co-author |
| Still open | 4-bit and 2-bit MAC units, board run, PPA, variable-precision MAC, report: not assigned here | |

The zero-point change: Lab 4 accumulates `(input − zero_point) × weight`, but that difference can exceed the int8 range the MAC accepts. The hardware now receives `input × weight`, and software folds the `−zero_point × Σweights` correction into the bias for each output channel.

The table only lists what the commit history and the reports show. Both of us can edit this repository, so please correct or extend it.

## What I learned (Jongwoo)

**Technical**
- How a number-format assumption in software (signed 8-bit operands) becomes a hard constraint once the arithmetic moves to hardware.
- Driving an AXI-Stream FIFO from bare-metal C++ through memory-mapped registers.
- Keeping one code base that builds for both the lab PC and the ZedBoard.

**Teamwork**
- Splitting work along the hardware/software boundary and agreeing on the packet format first.
- Coordinating design choices with other teams and TAs so variable-precision approaches do not overlap.

## Resources used

- Sze, Chen, Yang and Emer, *Efficient Processing of Deep Neural Networks* (ch. 7 on reduced precision)
- Course DNN framework template and the Lab 5 handout (`CprE487_587_Lab5.pdf`)
- Xilinx Vivado and Vitis 2020.1, AXI4-Stream FIFO IP

## Results so far

**Accelerated path vs. quantized path** (October 8, 2026, software model of the MAC, built at each bit width)

| Bit width | MAC operations per image | Packets | Layers matching the quantized path |
|---|---|---|---|
| 8 | 131,595,776 | 759,752 | all |
| 4 | 131,595,776 | 759,752 | all |
| 2 | 131,595,776 | 759,752 | all |

On 200 validation images at 8 bits, the MAC path and the plain quantized path give the same accuracy: 20.0% top-1 and 58.5% top-10.

**MAC units carried over from Lab 3** (8-bit; reports in `hw/*/vivado/`)

| MAC unit | LUTs | Registers | DSPs | Setup slack (WNS) |
|---|---|---|---|---|
| `staged_mac` (FSM) | 118 | 41 | 0 | +0.834 ns |
| `piped_mac` (pipelined) | 41 | 138 | 1 | +0.511 ns |

## Progress against the handout

| Handout item | Status |
|---|---|
| 3.1, 3.2 New inference type; `computeAccelerated` in every layer | done |
| 3.3 4-bit and 2-bit versions of both MAC units (6 in total), with XSA files and reports | not started; only the two 8-bit units exist |
| 3.4 Full inference through the MAC units, matching the quantized results | software model: done · on the board: not run |
| 4 Power, performance and area for all MAC units | not started |
| 5 Variable-precision MAC | not started |
| 6 Demo spreadsheet | not started |
| 8 Report and submission archive | not started |

## Limitations and next steps

- Whole-model inference through the MAC unit matches the quantized results in the software model, but has not been run on the board.
- Data is sent one word at a time through memory-mapped I/O. In Lab 3 this reached about 4.5 M MACs/s, slower than the board's CPU, so a streaming (DMA) path is needed for a real speed-up.
- Both timing reports flag pulse-width violations on the test clock constraint, which need to be resolved.
- Still to do: 4-bit and 2-bit MAC variants, PPA and energy-per-inference analysis, and the variable-precision MAC.

## Repository layout

<details>
<summary>Folders and files</summary>

```
hw/staged_mac, hw/piped_mac   MAC units (VHDL, testbenches, Vivado scripts and reports)
sw/framework                  C++ inference framework with the accelerated path
util/                         notebook and helper scripts
```

</details>
