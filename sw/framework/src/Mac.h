#pragma once

#include <cstddef>

#include "Config.h"
#include "Types.h"

#ifdef ZEDBOARD
#include "xil_io.h"
#include "xllfifo_hw.h"
#include "xparameters.h"
#endif

namespace ML {

// Lab 5: interface to the hardware MAC unit. A packet of 32-bit AXI-Stream words goes out through the
// AXI-Stream FIFO; TLAST on the last word makes the MAC return the 32-bit accumulated sum and clear itself.
//
// Fixed-width MAC (Lab 3 staged_mac / piped_mac with C_DATA_WIDTH = QUANT_BITS), one pair per word:
//     word = (weight << QUANT_BITS) | activation                        (8 bit: same packing as Lab 3 send_mac)
//
// Variable-precision MAC (Section 5, spatial accumulation, build with QUANT_VARIABLE):
//     first word of a packet = header, bits [1:0] select the operand width: 0 = 8 bit, 1 = 4 bit, 2 = 2 bit
//     data word = 16 bits of weights (upper half) and 16 bits of activations (lower half), cut into
//                 16 / bits fields each; field i of the weights is multiplied by field i of the activations
//                 and all the products of a word are added in the same cycle (2, 4 or 8 MACs per word).
//
// Everywhere except on the ZedBoard a bit-accurate software model of the same words is used, so a value that
// does not fit in its field gives the same wrong answer here as it would on the hardware.

#ifdef QUANT_VARIABLE
// Most data words in one packet; with the header it must fit in the FIFO transmit depth (2048 words)
constexpr std::size_t MAC_MAX_WORDS = 1024;
#else
// Most pairs (= words) sent in one packet. A longer group is split and the partial sums are added in software.
#ifndef MAC_MAX_GROUP
#define MAC_MAX_GROUP 256
#endif
constexpr std::size_t MAC_MAX_WORDS = MAC_MAX_GROUP;
// Bit width of one MAC operand; the fp32 build never calls the MAC, the 8 only keeps the shifts below valid
constexpr int MAC_BITS = (QUANT_BITS == 32) ? 8 : QUANT_BITS;
#endif

// Counters for the evaluation (MAC operations, FIFO data words and packets of the current run)
struct MacStats {
    ui64 ops = 0;
    ui64 words = 0;
    ui64 packets = 0;
};
inline MacStats& macStats() {
    static MacStats stats;
    return stats;
}

// Signed value of the `bits`-bit field of `word` that starts at bit `pos`
inline i32 macField(ui32 word, int pos, int bits) {
    const i32 signBit = 1 << (bits - 1);
    return (static_cast<i32>((word >> pos) & ((1u << bits) - 1)) ^ signBit) - signBit;
}

#ifdef ZEDBOARD

// Send one packet to the MAC and wait for its result (Lab 3 send_mac / recieve_mac)
inline i32 macSend(const ui32* words, std::size_t n) {
    for (std::size_t i = 0; i < n; i++) {
        Xil_Out32(XPAR_AXI_FIFO_0_BASEADDR + XLLF_TDFD_OFFSET, words[i]);
    }
    Xil_Out32(XPAR_AXI_FIFO_0_BASEADDR + XLLF_TLF_OFFSET, n * 4);  // packet length in bytes, starts the transfer

    while (Xil_In32(XPAR_AXI_FIFO_0_BASEADDR + XLLF_RDFO_OFFSET) == 0) {}  // wait for the result word
    ui32 data = 0;
    while (true) {
        ui32 length = Xil_In32(XPAR_AXI_FIFO_0_BASEADDR + XLLF_RLF_OFFSET);
        for (ui32 i = 0; i < (length & 0x7FFFFFFFUL); i += 4) {
            data = Xil_In32(XPAR_AXI_FIFO_0_BASEADDR + XLLF_RDFD_OFFSET);
        }
        if (!(length & (1u << 31))) break;  // bit 31 set = partial packet, keep reading
    }
    return static_cast<i32>(data);
}

#else

// Software model of the MAC unit this build talks to
inline i32 macSend(const ui32* words, std::size_t n) {
    i32 acc = 0;
#ifdef QUANT_VARIABLE
    const int bits = 8 >> (words[0] & 3);  // header
    for (std::size_t i = 1; i < n; i++) {
        for (int pos = 0; pos < 16; pos += bits) acc += macField(words[i], 16 + pos, bits) * macField(words[i], pos, bits);
    }
#else
    for (std::size_t i = 0; i < n; i++) acc += macField(words[i], MAC_BITS, MAC_BITS) * macField(words[i], 0, MAC_BITS);
#endif
    return acc;
}

#endif

// sum(weights[i] * activations[i]) for i < n, computed on the MAC unit with `bits`-bit operands
inline i32 macGroup(const i8* weights, const i8* activations, std::size_t n, int bits) {
    static ui32 words[MAC_MAX_WORDS + 1];  // static: the board's stack is small
    i32 acc = 0;
#ifdef QUANT_VARIABLE
    const std::size_t perWord = 16 / bits;  // pairs in one data word
    const ui32 mask = (1u << bits) - 1;
    words[0] = (bits == 8) ? 0 : (bits == 4) ? 1 : 2;
    for (std::size_t done = 0; done < n;) {
        std::size_t count = 0;  // data words in this packet
        for (; count < MAC_MAX_WORDS && done < n; count++) {
            ui32 word = 0;  // unused fields of the last word stay 0 and add nothing
            for (std::size_t f = 0; f < perWord && done < n; f++, done++) {
                word |= (static_cast<ui32>(weights[done]) & mask) << (16 + f * bits);
                word |= (static_cast<ui32>(activations[done]) & mask) << (f * bits);
            }
            words[1 + count] = word;
        }
        acc += macSend(words, count + 1);
        macStats().words += count;
        macStats().packets++;
    }
#else
    (void)bits;  // the fixed-width MAC is built for MAC_BITS
    const ui32 mask = (1u << MAC_BITS) - 1;
    for (std::size_t done = 0; done < n; done += MAC_MAX_WORDS) {
        const std::size_t count = (n - done < MAC_MAX_WORDS) ? n - done : MAC_MAX_WORDS;
        for (std::size_t i = 0; i < count; i++) {
            words[i] = ((static_cast<ui32>(weights[done + i]) & mask) << MAC_BITS) | (static_cast<ui32>(activations[done + i]) & mask);
        }
        acc += macSend(words, count);
        macStats().words += count;
        macStats().packets++;
    }
#endif
    macStats().ops += n;
    return acc;
}

// Every operand width this build's MAC unit supports
#ifdef QUANT_VARIABLE
constexpr int MAC_WIDTHS[] = {8, 4, 2};
#else
constexpr int MAC_WIDTHS[] = {MAC_BITS};
#endif

// Check the MAC unit against plain integer arithmetic for every possible operand pair, each pair alone in
// every field position of a word, and for one long group that uses every pair once. Returns the mismatches.
inline std::size_t macSelfTest(int bits) {
    const int lo = -(1 << (bits - 1)), hi = (1 << (bits - 1)) - 1;
#ifdef QUANT_VARIABLE
    const int slots = 16 / bits;
#else
    const int slots = 1;
#endif
    std::size_t bad = 0;
    i8 w[8], a[8];
    static i8 allW[1 << 16], allA[1 << 16];  // 8 bit: 65536 pairs (static: the board's stack is small)
    std::size_t count = 0;
    i32 expected = 0;
    for (int x = lo; x <= hi; x++) {
        for (int y = lo; y <= hi; y++) {
            for (int s = 0; s < slots; s++) {
                for (int k = 0; k < slots; k++) w[k] = a[k] = 0;
                w[s] = (i8)x;
                a[s] = (i8)y;
                if (macGroup(w, a, s + 1, bits) != x * y) bad++;
            }
            allW[count] = (i8)x;
            allA[count] = (i8)y;
            count++;
            expected += x * y;
        }
    }
    if (macGroup(allW, allA, count, bits) != expected) bad++;
    return bad;
}

}  // namespace ML
