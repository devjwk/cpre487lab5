<div align="center">

# HARDWARE-ACCELERATED CNN INFERENCE

### Quantized layers in software, their arithmetic on the FPGA

**C++ · VHDL · ZedBoard**

![Software](https://img.shields.io/badge/Software-C%2B%2B-6366F1?style=flat-square)
![RTL](https://img.shields.io/badge/RTL-VHDL-0F172A?style=flat-square)
![Board](https://img.shields.io/badge/Board-ZedBoard-0891B2?style=flat-square)
![Stage](https://img.shields.io/badge/Stage-In%20progress-F59E0B?style=flat-square)

Iowa State University · CprE 487/587 · Lab 5 · Team 06

[Overview](#overview) · [Accelerated layer](#accelerated-layer) · [Where this lab fits](#where-this-lab-fits) · [My role](#my-role) · [Limitations](#limitations-and-next-steps)

</div>

---

> **Where it stands — In progress**  
> The accelerated software path and both MAC units are in place.  
> A full-model run on the board has not been verified yet.

| `staged_mac` | `piped_mac` | MACs per inference | In conv2 alone |
| :---: | :---: | :---: | :---: |
| **118 LUT · 0 DSP** | **41 LUT · 1 DSP** | **131.6 M** | **80.3 M** |
| | |
|---|---|
| Period | October 2026 (in progress) |
| Team | 2 — Zach Dixon, Jongwoo Kim |
| My role | Software integration (`computeAccelerated`) |
| Stack | C++, VHDL, Vivado/Vitis 2020.1, ZedBoard (Zynq-7000) |
| Earlier labs | [Lab 4 — quantization](https://github.com/devjwk/cpre487lab4), [Lab 3 — MAC units](https://github.com/devjwk/487lab3), [Lab 2 — C++ framework](https://github.com/devjwk/487lab2) |

## Overview

Labs 1–4 produced a TinyImageNet CNN, a C++ implementation of it, two hardware MAC units, and an 8/4/2-bit quantized version of the model. This lab puts them together.

- **Problem:** the quantized model still runs every multiply-add on the CPU. Conv2 alone is 80.3 M of the model's 131.6 M MACs per inference, so that is where hardware should help most.
- **Approach:** add an accelerated inference path in which conv and dense layers send (weight, activation) pairs to the MAC unit over an AXI-Stream FIFO, while max-pool, flatten and softmax stay in software.
- **Then:** measure power, performance and area (PPA), and design a variable-precision MAC.

## Where this lab fits

```mermaid
flowchart LR
    L1["Lab 1 · Train in TensorFlow"] --> L2["Lab 2 · C++ framework"] --> L3["Lab 3 · MAC units"] --> L4["Lab 4 · Quantization"] --> L5["Lab 5 · Hardware integration"]
    style L5 fill:#6366F1,color:#ffffff,stroke:#4338CA
```

## Accelerated layer

```mermaid
flowchart LR
    L["Conv or dense layer · C++"] --> K["Pack weight and activation pairs"]
    K --> F["AXI-Stream FIFO"]
    F --> M["MAC unit · FPGA"]
    M --> R["int32 sum"]
    R --> B["Add bias and zero-point correction"]
    B --> Q["Dequantize · ReLU · requantize"]
```

Max-pool, flatten and softmax stay in software.

## My role

- Set up the repository from our Lab 3 hardware and Lab 4 framework.
- Wrote `computeAccelerated` for the convolutional layer and the MAC driver in `sw/framework/src/Mac.h`.
- Moved the input zero-point out of the hardware MAC. Lab 4 accumulates `(input − zero_point) × weight`, but that difference can exceed the int8 range the MAC accepts. The hardware now receives `input × weight`, and software folds the `−zero_point × Σweights` correction into the bias for each output channel.

Zach wrote most of the Lab 3 MAC VHDL that this lab reuses.

## What I learned

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

| MAC unit | LUTs | Registers | DSPs | Setup slack (WNS) |
|---|---|---|---|---|
| `staged_mac` (FSM) | 118 | 41 | 0 | +0.834 ns |
| `piped_mac` (pipelined) | 41 | 138 | 1 | +0.511 ns |

Reports are in `hw/*/vivado/`. The accelerated software path is written; a full-model run on the board has not been verified yet.

## Limitations and next steps

- Whole-model inference through the MAC unit still has to be checked against the Lab 4 quantized results.
- Data is sent one word at a time through memory-mapped I/O. In Lab 3 this reached about 4.5 M MACs/s, slower than the board's CPU, so a streaming (DMA) path is needed for a real speed-up.
- Both timing reports flag pulse-width violations on the test clock constraint, which need to be resolved.
- Still to do: 4-bit and 2-bit MAC variants, PPA and energy-per-inference analysis, and the variable-precision MAC.

## Repository layout

```
hw/staged_mac, hw/piped_mac   MAC units (VHDL, testbenches, Vivado scripts and reports)
sw/framework                  C++ inference framework with the accelerated path
util/                         notebook and helper scripts
```
