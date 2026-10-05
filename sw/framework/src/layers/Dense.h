#pragma once

#include "../Mac.h"
#include "../Quant.h"
#include "Layer.h"

namespace ML {

class DenseLayer : public Layer {
   public:
    DenseLayer(const LayerParams inParams,
               const LayerParams outParams,
               const LayerParams weightParams,
               const LayerParams biasParams,
               bool useReLU = true,
               const QuantParams quant = QuantParams())
        : Layer(inParams, outParams, LayerType::DENSE),
          weightParam(weightParams),
          weightData(weightParams),
          biasParam(biasParams),
          biasData(biasParams),
          useReLU(useReLU),
          quant(quant) {}

    const LayerParams& getWeightParams() const { return weightParam; }
    const LayerParams& getBiasParams() const { return biasParam; }

    const LayerData& getWeightData() const { return weightData; }
    const LayerData& getBiasData() const { return biasData; }
    const QuantParams& getQuantParams() const { return quant; }

    virtual void allocLayer() override {
        Layer::allocLayer();
        weightData.loadData();
        biasData.loadData();
    }

    virtual void freeLayer() override {
        Layer::freeLayer();
        weightData.freeData();
        biasData.freeData();
    }

    virtual void computeNaive(const LayerData& dataIn) const override {
        if (quant.enabled) {
            computeQuantized(dataIn);
            return;
        }

        const std::size_t inputSize =
            dataIn.getParams().flat_count();

        const std::size_t outputSize =
            getOutputParams().flat_count();

        for (std::size_t out = 0; out < outputSize; out++) {

            fp32 sum = getBiasData().get<fp32>(out);

            for (std::size_t in = 0; in < inputSize; in++) {

                std::size_t weightIdx =
                    in * outputSize + out;

                sum +=
                    dataIn.get<fp32>(in) *
                    getWeightData().get<fp32>(weightIdx);
            }

            if (useReLU && sum < 0) {
                sum = 0;
            }

            getOutputData().get<fp32>(out) = sum;
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

    // Lab 5: quantized dense layer with the multiply-accumulates on the MAC unit. As in
    // ConvolutionalLayer::computeAccelerated, the zero point is moved out of the MAC:
    //     sum((in - inZero) * w) = sum(in * w) - inZero * sum(w)
    virtual void computeAccelerated(const LayerData& dataIn) const override {
        if (!quant.enabled) {  // the fp32 model has no MAC implementation
            computeNaive(dataIn);
            return;
        }

        const std::size_t inputSize = dataIn.getParams().flat_count();
        const std::size_t outputSize = getOutputParams().flat_count();

        const i8* in = (const i8*)dataIn.raw();
        const i8* weights = (const i8*)getWeightData().raw();
        const i32* biases = (const i32*)getBiasData().raw();
        const fp32 accScale = quant.inScale * quant.wScale;

        std::vector<i8> row(inputSize);  // one output's weights, contiguous = one MAC group
        for (std::size_t out = 0; out < outputSize; out++) {
            i32 weightSum = 0;
            for (std::size_t i = 0; i < inputSize; i++) {
                row[i] = weights[i * outputSize + out];
                weightSum += row[i];
            }

            i32 acc = macGroup(row.data(), in, inputSize) + biases[out] - quant.inZero * weightSum;

            fp32 y = acc / accScale;
            if (useReLU && y < 0) y = 0;

            if (quant.outScale == 0) {
                getOutputData().get<fp32>(out) = y;  // last layer: fp32 logits for softmax
            } else {
                getOutputData().get<i8>(out) = quantize(y, quant.outScale, quant.outZero);
            }
        }
    }

   private:
    // Lab 4: same integer MAC / dequantize / ReLU / requantize flow as ConvolutionalLayer::computeQuantized
    void computeQuantized(const LayerData& dataIn) const {
        const std::size_t inputSize = dataIn.getParams().flat_count();
        const std::size_t outputSize = getOutputParams().flat_count();

        const i8* in = (const i8*)dataIn.raw();
        const i8* weights = (const i8*)getWeightData().raw();
        const i32* biases = (const i32*)getBiasData().raw();
        const fp32 accScale = quant.inScale * quant.wScale;

        for (std::size_t out = 0; out < outputSize; out++) {
            i32 acc = biases[out];
            for (std::size_t i = 0; i < inputSize; i++) {
                acc += (in[i] - quant.inZero) * weights[i * outputSize + out];
            }

            fp32 y = acc / accScale;
            if (useReLU && y < 0) y = 0;

            if (quant.outScale == 0) {
                getOutputData().get<fp32>(out) = y;  // last layer: fp32 logits for softmax
            } else {
                getOutputData().get<i8>(out) = quantize(y, quant.outScale, quant.outZero);
            }
        }
    }

    LayerParams weightParam;
    LayerData weightData;

    LayerParams biasParam;
    LayerData biasData;

    bool useReLU;
    QuantParams quant;
};

} // namespace ML