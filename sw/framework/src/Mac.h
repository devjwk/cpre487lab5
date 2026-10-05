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

// Lab 5: interface to the hardware MAC unit (Lab 3 staged_mac / piped_mac with C_DATA_WIDTH = QUANT_BITS).
//
// One AXI-Stream word carries a weight/activation pair, each a signed QUANT_BITS-bit value:
//     TDATA = (weight << QUANT_BITS) | activation        (8 bit: same packing as Lab 3 send_mac)
// TLAST on the last word of a packet makes the MAC output the 32-bit accumulated sum and clear itself.
//
// On the ZedBoard the words go through the AXI-Stream FIFO. Everywhere else a bit-accurate software model of
// the MAC is used (it packs and unpacks the same words), so a value that does not fit in QUANT_BITS gives
// the same wrong answer here as it would on the hardware.

// Most pairs sent in one packet. A longer group is split and the partial sums are added in software.
// Must not exceed the AXI-Stream FIFO transmit depth of the hardware design (calibrate to the XSA).
#ifndef MAC_MAX_GROUP
#define MAC_MAX_GROUP 256
#endif

// Bit width of one MAC operand; the fp32 build never calls the MAC, the 8 only keeps the shifts below valid
constexpr int MAC_BITS = (QUANT_BITS == 32) ? 8 : QUANT_BITS;

// Counters for the Section 4 evaluation (MAC operations and packets of the current run)
struct MacStats {
    ui64 ops = 0;
    ui64 packets = 0;
};
inline MacStats& macStats() {
    static MacStats stats;
    return stats;
}

inline ui32 macPack(i8 weight, i8 activation) {
    const ui32 mask = (1u << MAC_BITS) - 1;
    return ((static_cast<ui32>(weight) & mask) << MAC_BITS) | (static_cast<ui32>(activation) & mask);
}

#ifdef ZEDBOARD

// Send one packet of pairs to the MAC and wait for its result (Lab 3 send_mac / recieve_mac)
inline i32 macPacket(const i8* weights, const i8* activations, std::size_t n) {
    for (std::size_t i = 0; i < n; i++) {
        Xil_Out32(XPAR_AXI_FIFO_0_BASEADDR + XLLF_TDFD_OFFSET, macPack(weights[i], activations[i]));
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

// Software model of the MAC: unpack each word as two signed MAC_BITS-bit fields, multiply, accumulate in 32 bits
inline i32 macPacket(const i8* weights, const i8* activations, std::size_t n) {
    const i32 signBit = 1 << (MAC_BITS - 1);
    const ui32 mask = (1u << MAC_BITS) - 1;
    i32 acc = 0;
    for (std::size_t i = 0; i < n; i++) {
        const ui32 word = macPack(weights[i], activations[i]);
        const i32 w = (static_cast<i32>((word >> MAC_BITS) & mask) ^ signBit) - signBit;  // sign-extend
        const i32 a = (static_cast<i32>(word & mask) ^ signBit) - signBit;
        acc += w * a;
    }
    return acc;
}

#endif

// sum(weights[i] * activations[i]) for i < n, computed on the MAC unit
inline i32 macGroup(const i8* weights, const i8* activations, std::size_t n) {
    i32 acc = 0;
    for (std::size_t done = 0; done < n; done += MAC_MAX_GROUP) {
        const std::size_t len = (n - done < MAC_MAX_GROUP) ? n - done : MAC_MAX_GROUP;
        acc += macPacket(weights + done, activations + done, len);
        macStats().packets++;
    }
    macStats().ops += n;
    return acc;
}

}  // namespace ML
