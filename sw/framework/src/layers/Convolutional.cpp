#include "Convolutional.h"

#include <iostream>

#include "../Mac.h"
#include "../Types.h"
#include "../Utils.h"
#include "Layer.h"

namespace ML {
// --- Begin Student Code ---

// Compute the convultion for the layer data
void ConvolutionalLayer::computeNaive(const LayerData& dataIn) const {
    if (quant.enabled) {
        computeQuantized(dataIn);
        return;
    }

    // TODO: Your Code Here...
    // The following line is an example of copying a single 32-bit floating point integer from the input layer data to the output layer data
    /*getOutputData().get<fp32>(0) = dataIn.get<fp32>(0);*/
    const auto& inDims = dataIn.getParams().dims;
    const auto& outDims = getOutputParams().dims;
    const auto& weightDims = getWeightParams().dims;

    const std::size_t inW = inDims[1];
    const std::size_t inC = inDims[2];

    const std::size_t outH = outDims[0];
    const std::size_t outW = outDims[1];
    const std::size_t outC = outDims[2];

    const std::size_t kH = weightDims[0];
    const std::size_t kW = weightDims[1];

    for (std::size_t oy = 0; oy < outH; oy++) {
        for (std::size_t ox = 0; ox < outW; ox++) {
            for (std::size_t oc = 0; oc < outC; oc++) {

                fp32 sum = getBiasData().get<fp32>(oc);

                for (std::size_t ky = 0; ky < kH; ky++) {
                    for (std::size_t kx = 0; kx < kW; kx++) {
                        for (std::size_t ic = 0; ic < inC; ic++) {

                            std::size_t inputIdx =
                                (((oy + ky) * inW) + (ox + kx)) * inC + ic;

                            std::size_t weightIdx =
                                (((ky * kW) + kx) * inC + ic) * outC + oc;

                            sum +=
                                dataIn.get<fp32>(inputIdx) *
                                getWeightData().get<fp32>(weightIdx);
                        }
                    }
                }

                // ReLU
                if (sum < 0) {
                    sum = 0;
                }

                std::size_t outputIdx =
                    ((oy * outW) + ox) * outC + oc;

                getOutputData().get<fp32>(outputIdx) = sum;
            }
        }
    }

}

// Lab 4: integer convolution
//   acc (int32) = bias + sum((in - inZero) * weight)        int8 x int8 MACs, int32 accumulate
//   y   (fp32)  = acc / (inScale * wScale)                  dequantize
//   ReLU on y, then requantize for the next layer (or keep fp32 if outScale == 0)
void ConvolutionalLayer::computeQuantized(const LayerData& dataIn) const {
    const auto& inDims = dataIn.getParams().dims;
    const auto& outDims = getOutputParams().dims;
    const auto& weightDims = getWeightParams().dims;

    const std::size_t inW = inDims[1];
    const std::size_t inC = inDims[2];
    const std::size_t outH = outDims[0];
    const std::size_t outW = outDims[1];
    const std::size_t outC = outDims[2];
    const std::size_t kH = weightDims[0];
    const std::size_t kW = weightDims[1];

    // The first layer receives the fp32 image: quantize it once with this layer's input parameters
    std::vector<i8> quantizedImage;
    const i8* in = (const i8*)dataIn.raw();
    if (dataIn.getParams().elementSize == sizeof(fp32)) {
        const std::size_t count = dataIn.getParams().flat_count();
        quantizedImage.resize(count);
        for (std::size_t i = 0; i < count; i++) {
            quantizedImage[i] = quantize(dataIn.get<fp32>(i), quant.inScale, quant.inZero);
        }
        in = quantizedImage.data();
    }

    const i8* weights = (const i8*)getWeightData().raw();
    const i32* biases = (const i32*)getBiasData().raw();
    const fp32 accScale = quant.inScale * quant.wScale;

    for (std::size_t oy = 0; oy < outH; oy++) {
        for (std::size_t ox = 0; ox < outW; ox++) {
            for (std::size_t oc = 0; oc < outC; oc++) {
                i32 acc = biases[oc];

                for (std::size_t ky = 0; ky < kH; ky++) {
                    for (std::size_t kx = 0; kx < kW; kx++) {
                        for (std::size_t ic = 0; ic < inC; ic++) {
                            std::size_t inputIdx = (((oy + ky) * inW) + (ox + kx)) * inC + ic;
                            std::size_t weightIdx = (((ky * kW) + kx) * inC + ic) * outC + oc;
                            acc += (in[inputIdx] - quant.inZero) * weights[weightIdx];
                        }
                    }
                }

                // Dequantize, then ReLU in the real domain (so the zero point is handled correctly)
                fp32 y = acc / accScale;
                if (y < 0) y = 0;

                std::size_t outputIdx = ((oy * outW) + ox) * outC + oc;
                if (quant.outScale == 0) {
                    getOutputData().get<fp32>(outputIdx) = y;
                } else {
                    getOutputData().get<i8>(outputIdx) = quantize(y, quant.outScale, quant.outZero);
                }
            }
        }
    }
}

// Lab 5: same quantized convolution as computeQuantized, but every multiply-accumulate runs on the MAC unit.
//
// The MAC takes signed QUANT_BITS-bit operands, and (in - inZero) does not fit (conv1: up to 226 at 8 bit),
// so the zero point is moved out of the MAC:
//     sum((in - inZero) * w) = sum(in * w) - inZero * sum(w)
// sum(in * w) is one MAC group per output value; bias - inZero * sum(w) is a per-channel constant added here.
void ConvolutionalLayer::computeAccelerated(const LayerData& dataIn) const {
    if (!quant.enabled) {  // the fp32 model has no MAC implementation
        computeNaive(dataIn);
        return;
    }

    const auto& inDims = dataIn.getParams().dims;
    const auto& outDims = getOutputParams().dims;
    const auto& weightDims = getWeightParams().dims;

    const std::size_t inW = inDims[1];
    const std::size_t inC = inDims[2];
    const std::size_t outH = outDims[0];
    const std::size_t outW = outDims[1];
    const std::size_t outC = outDims[2];
    const std::size_t kH = weightDims[0];
    const std::size_t kW = weightDims[1];
    const std::size_t groupSize = kH * kW * inC;  // MACs per output value

    // The first layer receives the fp32 image: quantize it once with this layer's input parameters
    std::vector<i8> quantizedImage;
    const i8* in = (const i8*)dataIn.raw();
    if (dataIn.getParams().elementSize == sizeof(fp32)) {
        const std::size_t count = dataIn.getParams().flat_count();
        quantizedImage.resize(count);
        for (std::size_t i = 0; i < count; i++) {
            quantizedImage[i] = quantize(dataIn.get<fp32>(i), quant.inScale, quant.inZero);
        }
        in = quantizedImage.data();
    }

    const i8* weights = (const i8*)getWeightData().raw();
    const i32* biases = (const i32*)getBiasData().raw();
    const fp32 accScale = quant.inScale * quant.wScale;

    // Regroup the weights so each output channel's kernel is contiguous (one MAC group), in the same
    // (ky, kx, ic) order as the input patch, and fold the zero-point correction into the bias
    std::vector<i8> kernels(outC * groupSize);
    std::vector<i32> offsets(outC);
    for (std::size_t oc = 0; oc < outC; oc++) {
        i32 weightSum = 0;
        for (std::size_t k = 0; k < groupSize; k++) {
            kernels[oc * groupSize + k] = weights[k * outC + oc];
            weightSum += weights[k * outC + oc];
        }
        offsets[oc] = biases[oc] - quant.inZero * weightSum;
    }

    std::vector<i8> patch(groupSize);
    for (std::size_t oy = 0; oy < outH; oy++) {
        for (std::size_t ox = 0; ox < outW; ox++) {
            // Gather the input window once; it is shared by every output channel
            for (std::size_t ky = 0; ky < kH; ky++) {
                for (std::size_t kx = 0; kx < kW; kx++) {
                    for (std::size_t ic = 0; ic < inC; ic++) {
                        patch[((ky * kW) + kx) * inC + ic] = in[(((oy + ky) * inW) + (ox + kx)) * inC + ic];
                    }
                }
            }

            for (std::size_t oc = 0; oc < outC; oc++) {
                i32 acc = macGroup(&kernels[oc * groupSize], patch.data(), groupSize) + offsets[oc];

                fp32 y = acc / accScale;
                if (y < 0) y = 0;

                std::size_t outputIdx = ((oy * outW) + ox) * outC + oc;
                if (quant.outScale == 0) {
                    getOutputData().get<fp32>(outputIdx) = y;
                } else {
                    getOutputData().get<i8>(outputIdx) = quantize(y, quant.outScale, quant.outZero);
                }
            }
        }
    }
}

// Compute the convolution using threads
void ConvolutionalLayer::computeThreaded(const LayerData& dataIn) const {
    // TODO: Your Code Here...
}

// Compute the convolution using a tiled approach
void ConvolutionalLayer::computeTiled(const LayerData& dataIn) const {
    // TODO: Your Code Here...
}

// Compute the convolution using SIMD
void ConvolutionalLayer::computeSIMD(const LayerData& dataIn) const {
    // TODO: Your Code Here...
}
}  // namespace ML
