#pragma once
// Stand-in for the Xilinx headers so the ZedBoard code path of sw/framework/src/Mac.h can run on a PC.
// It imitates the registers of the AXI-Stream FIFO that Mac.h uses and writes every packet to a vector
// file for the VHDL testbench: "<number of words> <word 0> ... <word n-1> <expected result>", all in hex.
#include <cstdint>
#include <cstdio>
#include <vector>

#define XPAR_AXI_FIFO_0_BASEADDR 0u
#define XLLF_TDFD_OFFSET 0x10u
#define XLLF_TLF_OFFSET 0x14u
#define XLLF_RDFO_OFFSET 0x1cu
#define XLLF_RDFD_OFFSET 0x20u
#define XLLF_RLF_OFFSET 0x24u

struct FakeFifo {
    std::vector<uint32_t> tx;
    uint32_t result = 0;
    bool pending = false;
    FILE* out = nullptr;
    unsigned long packets = 0;
};
inline FakeFifo& fakeFifo() {
    static FakeFifo f;
    return f;
}

// What the variable-precision MAC must return for a packet (header word, then data words)
inline int32_t fakeMacResult(const std::vector<uint32_t>& words) {
    const int bits = 8 >> (words[0] & 3);
    int64_t acc = 0;
    for (size_t i = 1; i < words.size(); i++) {
        for (int pos = 0; pos < 16; pos += bits) {
            const int32_t sign = 1 << (bits - 1), mask = (1 << bits) - 1;
            const int32_t w = (((words[i] >> (16 + pos)) & mask) ^ sign) - sign;
            const int32_t a = (((words[i] >> pos) & mask) ^ sign) - sign;
            acc += w * a;
        }
    }
    return (int32_t)acc;
}

inline void Xil_Out32(uint32_t addr, uint32_t value) {
    FakeFifo& f = fakeFifo();
    if (addr == XLLF_TDFD_OFFSET) {
        f.tx.push_back(value);
    } else if (addr == XLLF_TLF_OFFSET) {  // packet length in bytes: the packet goes out
        f.result = (uint32_t)fakeMacResult(f.tx);
        if (f.out) {
            fprintf(f.out, "%08zx", f.tx.size());
            for (uint32_t w : f.tx) fprintf(f.out, " %08x", w);
            fprintf(f.out, " %08x\n", f.result);
        }
        f.packets++;
        f.tx.clear();
        f.pending = true;
    }
}

inline uint32_t Xil_In32(uint32_t addr) {
    FakeFifo& f = fakeFifo();
    if (addr == XLLF_RDFO_OFFSET) return f.pending ? 1 : 0;
    if (addr == XLLF_RLF_OFFSET) return 4;
    if (addr == XLLF_RDFD_OFFSET) { f.pending = false; return f.result; }
    return 0;
}
