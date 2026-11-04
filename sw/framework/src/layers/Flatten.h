#pragma once

#include "Layer.h"

namespace ML {

class FlattenLayer : public Layer {
   public:
    FlattenLayer(const LayerParams inParams,
                 const LayerParams outParams)
        : Layer(inParams, outParams, LayerType::FLATTEN) {}

    virtual void computeNaive(const LayerData& dataIn) const override {
        // Same layout for fp32 and int8
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

    virtual void computeAccelerated(const LayerData& dataIn) const override {
        computeNaive(dataIn);
    }
};

} // namespace ML