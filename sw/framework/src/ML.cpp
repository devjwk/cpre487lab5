#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "Config.h"
#include "Mac.h"
#include "Model.h"
#include "Types.h"
#include "Utils.h"
#include "layers/Convolutional.h"
#include "layers/Dense.h"
#include "layers/Layer.h"
#include "layers/MaxPooling.h"
#include "layers/Softmax.h"
#include "layers/Flatten.h"
#include "layers/Softmax.h"


#ifdef ZEDBOARD
#include <file_transfer/file_transfer.h>
#endif

namespace ML {

// Build our ML toy model
// Lab 4: QUANT_BITS (Config.h) selects the fp32 model (32) or the quantized model (8/4/2).
// Quantized model: int8 activations between layers, int8 weights, int32 biases from <modelPath>/q<bits>/.
// The model input (image) and the last Dense output / Softmax stay fp32.
Model buildToyModel(const Path modelPath) {
    Model model;
    logInfo("--- Building Toy Model ---");

#if QUANT_BITS == 32
    const std::size_t A = sizeof(fp32), W = sizeof(fp32), B = sizeof(fp32);  // activation, weight, bias sizes
    const Path dir = modelPath;
    const std::vector<QuantParams> q(8);  // disabled: original fp32 layers
#else
    const std::size_t A = sizeof(i8), W = sizeof(i8), B = sizeof(i32);
    const Path dir = modelPath / ("q" + std::to_string(QUANT_BITS));
    const std::vector<QuantParams> q = loadQuantParams(dir / "quant_params.txt");
    if (q.size() != 8) throw std::runtime_error("Expected 8 conv/dense layers in quant_params.txt");
    logInfo("Quantized model: " + std::to_string(QUANT_BITS) + " bit, loading " + dir);
#endif
    const std::size_t F = sizeof(fp32);

    // --- Conv 1: L1 --- 64x64x3 -> 60x60x32 (input image is fp32, quantized inside the layer)
    model.addLayer<ConvolutionalLayer>(
        LayerParams{F, {64, 64, 3}},                                      // Input Data
        LayerParams{A, {60, 60, 32}},                                     // Output Data
        LayerParams{W, {5, 5, 3, 32}, dir / "conv1_weights.bin"},         // Weights
        LayerParams{B, {32}, dir / "conv1_biases.bin"},                   // Bias
        q[0]);

    // --- Conv 2: L2 --- 60x60x32 -> 56x56x32
    model.addLayer<ConvolutionalLayer>(
        LayerParams{A, {60, 60, 32}},
        LayerParams{A, {56, 56, 32}},
        LayerParams{W, {5, 5, 32, 32}, dir / "conv2_weights.bin"},
        LayerParams{B, {32}, dir / "conv2_biases.bin"},
        q[1]);

    // --- MPL 1: L3 --- 56x56x32 -> 28x28x32
    model.addLayer<MaxPoolingLayer>(LayerParams{A, {56, 56, 32}}, LayerParams{A, {28, 28, 32}});

    // --- Conv 3: L4 --- 28x28x32 -> 26x26x64
    model.addLayer<ConvolutionalLayer>(
        LayerParams{A, {28, 28, 32}},
        LayerParams{A, {26, 26, 64}},
        LayerParams{W, {3, 3, 32, 64}, dir / "conv3_weights.bin"},
        LayerParams{B, {64}, dir / "conv3_biases.bin"},
        q[2]);

    // --- Conv 4: L5 --- 26x26x64 -> 24x24x64
    model.addLayer<ConvolutionalLayer>(
        LayerParams{A, {26, 26, 64}},
        LayerParams{A, {24, 24, 64}},
        LayerParams{W, {3, 3, 64, 64}, dir / "conv4_weights.bin"},
        LayerParams{B, {64}, dir / "conv4_biases.bin"},
        q[3]);

    // --- MPL 2: L6 --- 24x24x64 -> 12x12x64
    model.addLayer<MaxPoolingLayer>(LayerParams{A, {24, 24, 64}}, LayerParams{A, {12, 12, 64}});

    // --- Conv 5: L7 --- 12x12x64 -> 10x10x64
    model.addLayer<ConvolutionalLayer>(
        LayerParams{A, {12, 12, 64}},
        LayerParams{A, {10, 10, 64}},
        LayerParams{W, {3, 3, 64, 64}, dir / "conv5_weights.bin"},
        LayerParams{B, {64}, dir / "conv5_biases.bin"},
        q[4]);

    // --- Conv 6: L8 --- 10x10x64 -> 8x8x128
    model.addLayer<ConvolutionalLayer>(
        LayerParams{A, {10, 10, 64}},
        LayerParams{A, {8, 8, 128}},
        LayerParams{W, {3, 3, 64, 128}, dir / "conv6_weights.bin"},
        LayerParams{B, {128}, dir / "conv6_biases.bin"},
        q[5]);

    // --- MPL 3: L9 --- 8x8x128 -> 4x4x128
    model.addLayer<MaxPoolingLayer>(LayerParams{A, {8, 8, 128}}, LayerParams{A, {4, 4, 128}});

    // --- Flatten 1: L10 --- 4x4x128 -> 2048
    model.addLayer<FlattenLayer>(LayerParams{A, {4, 4, 128}}, LayerParams{A, {2048}});

    // --- Dense 1: L11 --- 2048 -> 256 (ReLU)
    model.addLayer<DenseLayer>(
        LayerParams{A, {2048}},
        LayerParams{A, {256}},
        LayerParams{W, {2048, 256}, dir / "dense1_weights.bin"},
        LayerParams{B, {256}, dir / "dense1_biases.bin"},
        true, q[6]);

    // --- Dense 2: L12 --- 256 -> 200 (no ReLU, outputs dequantized fp32 for softmax)
    model.addLayer<DenseLayer>(
        LayerParams{A, {256}},
        LayerParams{F, {200}},
        LayerParams{W, {256, 200}, dir / "dense2_weights.bin"},
        LayerParams{B, {200}, dir / "dense2_biases.bin"},
        false, q[7]);

    // --- Softmax 1: L13 --- 200 -> 200 (unchanged fp32)
    model.addLayer<SoftmaxLayer>(LayerParams{F, {200}}, LayerParams{F, {200}});

    return model;
}

void runBasicTest(const Model& model, const Path& basePath) {
    logInfo("--- Running Basic Test ---");

    // Load an image
    LayerData img = {{sizeof(fp32), {64, 64, 3}, "./data/image_0.bin"}};
    img.loadData();

    // Compare images
    std::cout << "Comparing image 0 to itself (max error): " << img.compare<fp32>(img) << std::endl
              << "Comparing image 0 to itself (T/F within epsilon " << ML::Config::EPSILON << "): " << std::boolalpha
              << img.compareWithin<fp32>(img, ML::Config::EPSILON) << std::endl;

    // Test again with a modified copy
    std::cout << "\nChange a value by 0.1 and compare again" << std::endl;
    
    LayerData imgCopy = img;
    imgCopy.get<fp32>(0) += 0.1;

    // Compare images
    img.compareWithinPrint<fp32>(imgCopy);

    // Test again with a modified copy
    log("Change a value by 0.1 and compare again...");
    imgCopy.get<fp32>(0) += 0.1;

    // Compare Images
    img.compareWithinPrint<fp32>(imgCopy);
}

void runLayerTest(const std::size_t layerNum, const Model& model, const Path& basePath) {
    // Load an image
    logInfo(std::string("--- Running Layer Test ") + std::to_string(layerNum) + "---");

    // Construct a LayerData object from a LayerParams one
    // LayerData img(model[layerNum].getInputParams(), test_image_files[layerNum].first);
    dimVec inDims = {2048};
    LayerData img({sizeof(fp32), inDims, basePath /"image_0_data" /"layer_9_output.bin"});
    img.loadData();

    Timer timer("Layer Inference");

    // Run inference on the model
    timer.start();
    const LayerData& output = model.inferenceLayer(img, layerNum, Layer::InfType::NAIVE);
    timer.stop();

    // Compare the output
    // Construct a LayerData object from a LayerParams one
    LayerData expected(output.getParams(), basePath / "image_0_data" / "layer_10_output.bin");
    expected.loadData();
    output.compareWithinPrint<fp32>(expected);
}

void runInferenceTest(const Model& model, const Path& basePath) {
    // Load an image
    logInfo("--- Running Inference Test ---");

    // Construct a LayerData object from a LayerParams one
    LayerData img(model[0].getInputParams(), basePath / "image_0.bin");
    img.loadData();

    Timer timer("Full Inference");

    // Run inference on the model
    timer.start();
    const LayerData& output = model.inference(img, Layer::InfType::NAIVE);
    timer.stop();

    // Compare the output
    // Construct a LayerData object from a LayerParams one
    LayerData expected(model.getOutputLayer().getOutputParams(), basePath / "image_0_data" / "layer_11_output.bin");
    expected.loadData();
    output.compareWithinPrint<fp32>(expected);
}

// get an enum to assign to a layer
std::string layerName(Layer:: LayerType t){
    switch(t){
        case Layer::LayerType::CONVOLUTIONAL:   return "Convolutional";
        case Layer::LayerType::DENSE:           return "Dense";
        case Layer::LayerType::SOFTMAX:         return "SoftMax";
        case Layer::LayerType::MAX_POOLING:     return "MaxPooling";
        case Layer::LayerType::FLATTEN:         return "Flatten";
        default:                                return "Error";
    }

}

void timingTest(const Model& model, const Path& basePath){

    logInfo("Starting Inference Tests");

    LayerData img(model[0].getInputParams(), basePath / "image_0.bin");
    img.loadData();

    const std::size_t numLayers = model.getNumLayers();
    std::vector<fp32> layerTimes(numLayers);

    const LayerData* currentLayer = &img;
    for(std::size_t i = 0; i < numLayers; i++){
        Timer timer("Layer_" + std::to_string(i) + "_" + layerName(model[i].getLType()));
        
        timer.start();
        const LayerData& out = model.inferenceLayer(*currentLayer, i, Layer::InfType::NAIVE);
        timer.stop();

        layerTimes[i] = timer.milliseconds;
        currentLayer = &out;
    }

    fp32 total = 0;
    for(auto ms : layerTimes) total += ms;

    logInfo("Layer Timing Breakdown");

    for(std::size_t i = 0; i < numLayers; i++){
        float percent;
        if(total > 0){
            percent = 100.0 * layerTimes[i] / total;
        }else{
            percent = 0;
        }
        logInfo(layerName(model[i].getLType()) + " Layer (" + std::to_string(i) + std::to_string(layerTimes[i]) + " ms) " + std::to_string(percent) + "% of total time");

    }
    logInfo("Total inference time: " + std::to_string(total) + "ms");

    LayerData expected(model.getOutputLayer().getOutputParams(), basePath / "image_0_data" / "layer_11_output.bin");
    expected.loadData();
    currentLayer->compareWithinPrint<fp32>(expected);

    logInfo("Completed Layer timings");


}

// Lab 4: run image_0 layer by layer, dequantize every int8 output and compare it with the fp32 reference
// output of the same layer (Figure 1 style). Then check that the top-1 class matches fp32 for all test images.
void runQuantCompare(const Model& model, const Path& basePath) {
    logInfo("--- Quantized vs fp32 reference (image_0, per layer) ---");
    const std::size_t numLayers = model.getNumLayers();

    LayerData img(model[0].getInputParams(), basePath / "image_0.bin");
    img.loadData();

    const LayerData* cur = &img;
    QuantParams act;  // how the current activation is quantized (MaxPool/Flatten keep the previous one)
    for (std::size_t i = 0; i < numLayers; i++) {
        const LayerData& out = model.inferenceLayer(*cur, i, Layer::InfType::NAIVE);
        cur = &out;
        if (auto conv = dynamic_cast<const ConvolutionalLayer*>(&model[i])) act = conv->getQuantParams();
        if (auto dense = dynamic_cast<const DenseLayer*>(&model[i])) act = dense->getQuantParams();

        // Keras fuses softmax into the last Dense, so its reference file belongs to our Softmax layer
        if (i == numLayers - 2) continue;
        const std::size_t ref = (i == numLayers - 1) ? i - 1 : i;

        const dimVec& dims = out.getParams().dims;
        LayerData expected({sizeof(fp32), dims, basePath / "image_0_data" / ("layer_" + std::to_string(ref) + "_output.bin")});
        expected.loadData();

        LayerData deq({sizeof(fp32), dims});
        deq.allocData();
        const std::size_t count = out.getParams().flat_count();
        for (std::size_t k = 0; k < count; k++) {
            deq.get<fp32>(k) = (out.getParams().elementSize == sizeof(i8))
                                   ? dequantize(out.get<i8>(k), act.outScale, act.outZero)
                                   : out.get<fp32>(k);
        }

        // Save the dequantized output for the Section 5 visualization (util/plot_channel.py)
        std::ofstream dump(Path(basePath / "model" / ("q" + std::to_string(QUANT_BITS))) / ("image_0_layer_" + std::to_string(i) + "_deq.bin"),
                           std::ios::binary);
        dump.write((const char*)deq.raw(), deq.getParams().byte_size());

        std::cout << "Layer " << i << " " << layerName(model[i].getLType()) << ": max error " << deq.maxDiff<fp32>(expected)
                  << ", cosine similarity " << deq.compare<fp32>(expected) << std::endl;

        // Print a few values like the lab manual's Figure 1
        if (i == 0) {
            for (std::size_t k = 0, shown = 0; k < count && shown < 5; k++) {
                if (expected.get<fp32>(k) == 0) continue;
                std::cout << "  DE Quantized value: " << deq.get<fp32>(k) << "\n  Original value:     " << expected.get<fp32>(k) << std::endl;
                shown++;
            }
        }
    }

    logInfo("--- Top-1 class: quantized vs fp32 reference ---");
    for (int n = 0; n < 3; n++) {
        const std::string name = "image_" + std::to_string(n);
        LayerData input(model[0].getInputParams(), basePath / (name + ".bin"));
        input.loadData();
        const LayerData& output = model.inference(input, Layer::InfType::NAIVE);

        LayerData expected(model.getOutputLayer().getOutputParams(), basePath / (name + "_data") / "layer_11_output.bin");
        expected.loadData();

        std::size_t top = 0, refTop = 0;
        for (std::size_t k = 1; k < 200; k++) {
            if (output.get<fp32>(k) > output.get<fp32>(top)) top = k;
            if (expected.get<fp32>(k) > expected.get<fp32>(refTop)) refTop = k;
        }
        std::cout << name << ": quantized top-1 = " << top << " (p=" << output.get<fp32>(top) << "), fp32 top-1 = " << refTop
                  << " (p=" << expected.get<fp32>(refTop) << ")" << (top == refTop ? "  MATCH" : "  MISMATCH") << std::endl;
    }
}

// Lab 5: run image_0 with the software quantized path (NAIVE) and with the MAC unit (ACCELERATED) and check
// that every layer output is bit-identical. Also reports how many MAC operations each layer needs.
void runAcceleratedCheck(const Model& model, const Path& basePath) {
    logInfo("--- MAC unit (ACCELERATED) vs software quantized (NAIVE), image_0 ---");
    const std::size_t numLayers = model.getNumLayers();

    LayerData img(model[0].getInputParams(), basePath / "image_0.bin");
    img.loadData();

    // Reference: software path, keep a copy of every layer output
    std::vector<LayerData> reference;
    const LayerData* cur = &img;
    std::vector<fp32> naiveMs(numLayers);
    for (std::size_t i = 0; i < numLayers; i++) {
        Timer timer("Layer_" + std::to_string(i) + "_naive");
        timer.start();
        cur = &model.inferenceLayer(*cur, i, Layer::InfType::NAIVE);
        timer.stop();
        naiveMs[i] = timer.milliseconds;
        reference.push_back(*cur);
    }

    macStats() = MacStats();
    bool allMatch = true;
    cur = &img;
    for (std::size_t i = 0; i < numLayers; i++) {
        const MacStats before = macStats();
        Timer timer("Layer_" + std::to_string(i) + "_mac");
        timer.start();
        cur = &model.inferenceLayer(*cur, i, Layer::InfType::ACCELERATED);
        timer.stop();
        const bool match = std::memcmp(cur->raw(), reference[i].raw(), cur->getParams().byte_size()) == 0;
        allMatch = allMatch && match;
        std::cout << "Layer " << i << " " << layerName(model[i].getLType()) << ": " << (match ? "MATCH" : "MISMATCH")
                  << ", MAC ops " << (macStats().ops - before.ops) << ", packets " << (macStats().packets - before.packets)
                  << ", software " << naiveMs[i] << " ms, MAC unit " << timer.milliseconds << " ms" << std::endl;
    }
    std::cout << "Total: " << macStats().ops << " MAC ops in " << macStats().packets << " packets ("
              << MAC_BITS << "-bit operands, up to " << MAC_MAX_GROUP << " pairs per packet) -> "
              << (allMatch ? "ALL LAYERS MATCH" : "MISMATCH FOUND") << std::endl;
}

#ifndef ZEDBOARD
// Lab 4 Sections 5.1 / 6: top-1 / top-10 accuracy and latency over the exported validation images
// (data/val/val_images_u8.bin: N x 64x64x3 uint8, val_labels_i32.bin: N int32). Run with `./build/ml val [N]`.
void runValidation(std::size_t numImages, Layer::InfType infType = Layer::InfType::NAIVE) {
    Path basePath("data");
    Model model = buildToyModel(basePath / "model");
    model.allocLayers();

    std::ifstream imageFile(basePath / "val" / "val_images_u8.bin", std::ios::binary);
    std::ifstream labelFile(basePath / "val" / "val_labels_i32.bin", std::ios::binary);
    if (!imageFile.is_open() || !labelFile.is_open()) throw std::runtime_error("Missing data/val files (run util/export_val.py)");

    const std::size_t imageSize = 64 * 64 * 3;
    std::vector<ui8> pixels(imageSize);
    LayerData input(model[0].getInputParams());
    input.allocData();

    std::size_t top1 = 0, top10 = 0, done = 0;
    double totalMs = 0, minMs = 1e30, maxMs = 0;
    for (; done < numImages; done++) {
        i32 label;
        if (!imageFile.read((char*)pixels.data(), imageSize) || !labelFile.read((char*)&label, sizeof(label))) break;
        for (std::size_t k = 0; k < imageSize; k++) input.get<fp32>(k) = pixels[k] / 255.0f;  // same normalization as Lab 1

        auto start = std::chrono::steady_clock::now();
        const LayerData& output = model.inference(input, infType);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        totalMs += ms;
        if (ms < minMs) minMs = ms;
        if (ms > maxMs) maxMs = ms;

        // Rank of the true class; ties go to the lower index (same as argmax), so a constant output is not "correct"
        const fp32 p = output.get<fp32>(label);
        std::size_t rank = 0;
        for (i32 k = 0; k < 200; k++) {
            fp32 v = output.get<fp32>(k);
            if (v > p || (v == p && k < label)) rank++;
        }
        if (rank == 0) top1++;
        if (rank < 10) top10++;
    }

    std::cout << "\n===== Validation (" << QUANT_BITS << " bit, " << done << " images, "
              << (infType == Layer::InfType::ACCELERATED ? "MAC unit" : "software") << ") =====\n"
              << "Top-1 accuracy:  " << 100.0 * top1 / done << "%\n"
              << "Top-10 accuracy: " << 100.0 * top10 / done << "%\n"
              << "Latency per image: avg " << totalMs / done << " ms, min " << minMs << " ms, max " << maxMs << " ms\n";
    model.freeLayers();
}
#endif

void runTests() {
    // Base input data path (determined from current directory of where you are running the command)
    Path basePath("data");  // May need to be altered for zedboards loading from SD Cards

    // Build the model and allocate the buffers
    Model model = buildToyModel(basePath / "model");
    model.allocLayers();

    // Run some framework tests as an example of loading data
    runBasicTest(model, basePath);

#if QUANT_BITS == 32
    // Run a layer inference test (feeds an fp32 reference activation, so only valid for the fp32 model)
    runLayerTest(10, model, basePath);
#else
    runQuantCompare(model, basePath);
    runAcceleratedCheck(model, basePath);
#endif

    // Run an end-to-end inference test
    runInferenceTest(model, basePath);

    // Run an timing Test
    timingTest(model, basePath);

    // Clean up
    model.freeLayers();
    std::cout << "\n\n----- ML::runTests() COMPLETE -----\n";
}

} // namespace ML

#ifdef ZEDBOARD
extern "C"
int main() {
    try {
        static FATFS fatfs;
        if (f_mount(&fatfs, "/", 1) != FR_OK) {
            throw std::runtime_error("Failed to mount SD card. Is it plugged in?");
        }
        ML::runTests();
    } catch (const std::exception& e) {
        std::cerr << "\n\n----- EXCEPTION THROWN -----\n" << e.what() << '\n';
    }
    std::cout << "\n\n----- STARTING FILE TRANSFER SERVER -----\n";
    FileServer::start_file_transfer_server();
}
#else
int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "val") {
        // ./build/ml val [N] [mac]   -> "mac" runs the multiply-accumulates on the MAC unit
        const bool mac = argc > 3 && std::string(argv[3]) == "mac";
        ML::runValidation(argc > 2 ? std::atoi(argv[2]) : 1000, mac ? ML::Layer::InfType::ACCELERATED : ML::Layer::InfType::NAIVE);
    } else {
        ML::runTests();
    }
}
#endif