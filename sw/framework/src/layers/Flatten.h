#pragma once

#include "Layer.h"

namespace ML {

class FlattenLayer : public Layer {
   public:
    FlattenLayer(const LayerParams inParams,
                 const LayerParams outParams)
        : Layer(inParams, outParams, LayerType::FLATTEN) {}

    virtual void computeNaive(const LayerData& dataIn) const override {
        // Same memory layout, so a byte copy works for both fp32 and int8 data
        std::memcpy(getOutputData().raw(), dataIn.raw(), dataIn.getParams().byte_size());
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