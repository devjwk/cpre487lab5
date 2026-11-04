#pragma once

// Disable all timers
// #define DISABLE_TIMING

// 32 = fp32, 8/4/2 = quantized (make clean before switching)
#ifndef QUANT_BITS
#define QUANT_BITS 8
#endif

// make QUANT_VARIABLE=1: per-layer widths from data/model/qvar
// #define QUANT_VARIABLE

namespace ML {
namespace Config {
constexpr bool ENABLE_SIMD = false;
constexpr bool FANCY_LOGGING = true;

// Floating Point Compare Epsilon
constexpr float EPSILON = 0.001;
} // namespace Config
} // namespace ML::Config