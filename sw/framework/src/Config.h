#pragma once

// Disable all timers
// #define DISABLE_TIMING

// Lab 4: bit width of weights/activations. 32 = original fp32 model, 8/4/2 = quantized model.
// Override with `make QUANT_BITS=8` (run `make clean` first when switching).
#ifndef QUANT_BITS
#define QUANT_BITS 8
#endif

// Lab 5 Section 5: `make QUANT_VARIABLE=1` builds the variable-precision model. Every layer reads its own
// operand width from data/model/qvar/quant_params.txt and the MACs go to the variable-precision MAC unit.
// #define QUANT_VARIABLE

namespace ML {
namespace Config {
constexpr bool ENABLE_SIMD = false;
constexpr bool FANCY_LOGGING = true;

// Floating Point Compare Epsilon
constexpr float EPSILON = 0.001;
} // namespace Config
} // namespace ML::Config