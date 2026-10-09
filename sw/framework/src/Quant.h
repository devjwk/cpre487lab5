#pragma once

#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "Config.h"
#include "Types.h"
#include "Utils.h"

namespace ML {

// Per-layer quantization parameters (Lab 4 Section 4)
//   float = (int - zero) / scale
//   in*  : how this layer's input activations are quantized
//   wScale: weight scale (weight zero point is 0); bias scale = inScale * wScale
//   out* : how to requantize the output for the next layer; outScale == 0 means output dequantized fp32
struct QuantParams {
    bool enabled = false;
    fp32 inScale = 1;
    i32 inZero = 0;
    fp32 wScale = 1;
    fp32 outScale = 0;
    i32 outZero = 0;
    // Lab 5 Section 5: operand width of this layer's MACs (its weights and input activations) and of the
    // next layer's input. A fixed-width model uses QUANT_BITS everywhere.
    int bits = QUANT_BITS;
    int outBits = QUANT_BITS;
};

// Round-to-nearest-even (matches numpy's round used to export weights) and clamp to the quantized range
inline i8 quantize(fp32 x, fp32 scale, i32 zero, int bits) {
    const i32 qmax = (1 << (bits - 1)) - 1, qmin = -qmax - 1;  // signed range of `bits` bits (8 bit: -128..127)
    i32 q = (i32)std::nearbyint(x * scale) + zero;
    if (q > qmax) q = qmax;
    if (q < qmin) q = qmin;
    return (i8)q;
}

inline fp32 dequantize(i32 q, fp32 scale, i32 zero) { return (q - zero) / scale; }

// Read "<layer> <inScale> <inZero> <wScale> [bits]" lines (one per conv/dense layer, in model order) and chain
// each layer's output parameters to the next layer's input parameters. The last layer outputs fp32.
// The variable-precision model (Section 5) gives every layer its own bits; without it, QUANT_BITS is used.
inline std::vector<QuantParams> loadQuantParams(const Path& file) {
#ifdef ZEDBOARD
    // No std::ifstream on the board: read the whole text file from the SD card, then parse it the same way
    FIL fil;
    if (f_open(&fil, file.c_str(), FA_OPEN_EXISTING | FA_READ) != FR_OK) throw std::runtime_error("Failed to open quantization params: " + file);
    std::string text(f_size(&fil), '\0');
    UINT bytesRead = 0;
    const FRESULT res = f_read(&fil, &text[0], text.size(), &bytesRead);
    f_close(&fil);
    if (res != FR_OK || bytesRead != text.size()) throw std::runtime_error("Failed to read quantization params: " + file);
    std::istringstream in(text);
#else
    std::ifstream in(file);
    if (!in.is_open()) throw std::runtime_error("Failed to open quantization params: " + file);
#endif

    std::vector<QuantParams> params;
    std::string name;
    QuantParams q;
    q.enabled = true;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        if (!(fields >> name >> q.inScale >> q.inZero >> q.wScale)) continue;
        if (!(fields >> q.bits)) q.bits = QUANT_BITS;
        params.push_back(q);
    }

    for (std::size_t i = 0; i + 1 < params.size(); i++) {
        params[i].outScale = params[i + 1].inScale;
        params[i].outZero = params[i + 1].inZero;
        params[i].outBits = params[i + 1].bits;
    }
    return params;
}

}  // namespace ML
