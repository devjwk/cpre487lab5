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

// Fixed width: word = (weight << QUANT_BITS) | activation, one pair per word.
// QUANT_VARIABLE: header word [1:0] = 8/4/2 bit, then each word holds 16 bits of weights (upper) and
// 16 bits of activations (lower) in 16 / bits fields. Off the board a bit-accurate model is used.

#ifdef QUANT_VARIABLE
// Header + data words must fit in the 2048-word FIFO
constexpr std::size_t MAC_MAX_WORDS = 1024;
#else
#ifndef MAC_MAX_GROUP
#define MAC_MAX_GROUP 256
#endif
constexpr std::size_t MAC_MAX_WORDS = MAC_MAX_GROUP;
constexpr int MAC_BITS = (QUANT_BITS == 32) ? 8 : QUANT_BITS;
#endif

struct MacStats {
    ui64 ops = 0;
    ui64 words = 0;
    ui64 packets = 0;
};
inline MacStats& macStats() {
    static MacStats stats;
    return stats;
}

inline i32 macField(ui32 word, int pos, int bits) {
    const i32 signBit = 1 << (bits - 1);
    return (static_cast<i32>((word >> pos) & ((1u << bits) - 1)) ^ signBit) - signBit;
}

#ifdef ZEDBOARD

inline i32 macSend(const ui32* words, std::size_t n) {
    for (std::size_t i = 0; i < n; i++) {
        Xil_Out32(XPAR_AXI_FIFO_0_BASEADDR + XLLF_TDFD_OFFSET, words[i]);
    }
    Xil_Out32(XPAR_AXI_FIFO_0_BASEADDR + XLLF_TLF_OFFSET, n * 4);

    while (Xil_In32(XPAR_AXI_FIFO_0_BASEADDR + XLLF_RDFO_OFFSET) == 0) {}
    ui32 data = 0;
    while (true) {
        ui32 length = Xil_In32(XPAR_AXI_FIFO_0_BASEADDR + XLLF_RLF_OFFSET);
        for (ui32 i = 0; i < (length & 0x7FFFFFFFUL); i += 4) {
            data = Xil_In32(XPAR_AXI_FIFO_0_BASEADDR + XLLF_RDFD_OFFSET);
        }
        if (!(length & (1u << 31))) break;
    }
    return static_cast<i32>(data);
}

#else

inline i32 macSend(const ui32* words, std::size_t n) {
    i32 acc = 0;
#ifdef QUANT_VARIABLE
    const int bits = 8 >> (words[0] & 3);
    for (std::size_t i = 1; i < n; i++) {
        for (int pos = 0; pos < 16; pos += bits) acc += macField(words[i], 16 + pos, bits) * macField(words[i], pos, bits);
    }
#else
    for (std::size_t i = 0; i < n; i++) acc += macField(words[i], MAC_BITS, MAC_BITS) * macField(words[i], 0, MAC_BITS);
#endif
    return acc;
}

#endif

// sum(weights[i] * activations[i]) on the MAC unit
inline i32 macGroup(const i8* weights, const i8* activations, std::size_t n, int bits) {
    static ui32 words[MAC_MAX_WORDS + 1];  // board stack is small
    i32 acc = 0;
#ifdef QUANT_VARIABLE
    const std::size_t perWord = 16 / bits;
    const ui32 mask = (1u << bits) - 1;
    words[0] = (bits == 8) ? 0 : (bits == 4) ? 1 : 2;
    for (std::size_t done = 0; done < n;) {
        std::size_t count = 0;
        for (; count < MAC_MAX_WORDS && done < n; count++) {
            ui32 word = 0;
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
    (void)bits;
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

#ifdef QUANT_VARIABLE
constexpr int MAC_WIDTHS[] = {8, 4, 2};
#else
constexpr int MAC_WIDTHS[] = {MAC_BITS};
#endif

// Every operand pair in every field position, and one long group; returns the mismatches
inline std::size_t macSelfTest(int bits) {
    const int lo = -(1 << (bits - 1)), hi = (1 << (bits - 1)) - 1;
#ifdef QUANT_VARIABLE
    const int slots = 16 / bits;
#else
    const int slots = 1;
#endif
    std::size_t bad = 0;
    i8 w[8], a[8];
    static i8 allW[1 << 16], allA[1 << 16];
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
