#pragma once

#include "GameDataTracker.h"
#include "GameEvents.h"
#include <memory>
#include <onnxruntime/onnxruntime_cxx_api.h>
#include <string>
#include <vector>

struct InferenceInput {
    std::vector<float> inputs;
    PredictionReliability reliability;
};

class InferenceEngine {
private:
    bool initialized;

    // ONNX Runtime Resources
    Ort::Env env;
    std::unique_ptr<Ort::Session> session;
    Ort::AllocatorWithDefaultOptions allocator;
    Ort::MemoryInfo cpu_memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    // Model Info
    std::vector<std::string> input_node_names;
    std::vector<std::string> output_node_names;
    std::vector<const char*> input_node_names_ptr;
    std::vector<const char*> output_node_names_ptr;

    // Augmentation Masks
    std::vector<float> mask_flip_x;
    std::vector<float> mask_flip_y;
    std::vector<float> mask_flip_xy;

    void InitializeInternal(const std::string& model_path);
    void InitializeMasks();

    const std::vector<float> InferRaw(std::vector<float> input);

public:
    bool Initialize(const std::string& model_path);
    void Deinitialize();

    std::optional<InferenceInput> GetInferenceInput(ServerWrapper server, const GameDataTracker& gameDataTracker, double currentTimeMs, bool logInputs = false);
    std::optional<Prediction> Predict(InferenceInput input, Augmentation augmentation);

    static std::optional<int> GetBigBoostIndex(Vector location);
};