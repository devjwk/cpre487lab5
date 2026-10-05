#pragma once

#include <cmath>

#include "Layer.h"

namespace ML {

class SoftmaxLayer : public Layer {
   public:
    SoftmaxLayer(const LayerParams inParams,
                 const LayerParams outParams)
        : Layer(inParams, outParams, LayerType::SOFTMAX) {}

    virtual void computeNaive(const LayerData& dataIn) const override {

        const std::size_t inputSize =
            dataIn.getParams().flat_count();

        // Find the largest input value for numerical stability
        fp32 maxVal = dataIn.get<fp32>(0);

        for (std::size_t i = 1; i < inputSize; i++) {
            fp32 value = dataIn.get<fp32>(i);

            if (value > maxVal) {
                maxVal = value;
            }
        }

        // Compute denominator:
        // sum(exp(g_i - maxVal))
        fp32 expSum = 0;

        for (std::size_t i = 0; i < inputSize; i++) {
            expSum += std::exp(
                dataIn.get<fp32>(i) - maxVal
            );
        }

        // Compute each softmax output
        for (std::size_t i = 0; i < inputSize; i++) {

            fp32 numerator = std::exp(
                dataIn.get<fp32>(i) - maxVal
            );

            getOutputData().get<fp32>(i) =
                numerator / expSum;
        }
    }

    virtual void computeThreaded(const LayerData& dataIn) const override {
        computeNaive(dataIn);
    }

    virtual void computeTiled(const LayerData& dataIn) const override {
        computeNaive(dataIn);
    }

    virtual void computeSIMD(const LayerData& dataIn) const override {
        computeNaive(dataIn);
    }

    // Lab 5: no multiply-accumulate in this layer, so nothing to offload to the MAC unit
    virtual void computeAccelerated(const LayerData& dataIn) const override {
        computeNaive(dataIn);
    }
};

} // namespace ML