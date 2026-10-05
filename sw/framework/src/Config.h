#pragma once

// Disable all timers
// #define DISABLE_TIMING

// Lab 4: bit width of weights/activations. 32 = original fp32 model, 8/4/2 = quantized model.
// Override with `make QUANT_BITS=8` (run `make clean` first when switching).
#ifndef QUANT_BITS
#define QUANT_BITS 8
#endif

namespace ML {
namespace Config {
constexpr bool ENABLE_SIMD = false;
constexpr bool FANCY_LOGGING = true;

// Floating Point Compare Epsilon
constexpr float EPSILON = 0.001;
} // namespace Config
} // namespace ML::Config