#pragma once
#include <onnxruntime_cxx_api.h>
#include <vector>
#include <string>
#include <array>
#include <memory>

namespace Gdiplus { class Bitmap; }

// Top-k scene prediction result.
struct SceneResult {
    std::string scene_name;   // scene label (from categories_places365.txt)
    float score;              // softmax probability (0~1)
};

// Places365 scene recognition (ResNet18/50 exported to ONNX).
//
// The model takes a 224x224 RGB image (ImageNet-style normalization) and
// outputs 365 logits (one per Places365 scene).  We softmax them and expose
// top-k / full-vector / dominant-scene accessors.
//
// Uses GDI+ for decoding/preprocessing (the engine's existing image stack) —
// no OpenCV dependency.  Every public method is a no-op / returns empty data
// when the model failed to load, so callers can degrade gracefully.
class SceneInference {
public:
    SceneInference();
    ~SceneInference();

    // Load the ONNX model + the 365-line labels file.  Returns false on
    // failure (an error is printed to stderr).
    bool loadModel(const std::string& model_path, const std::string& labels_path);
    bool valid() const { return session_ != nullptr && labels_.size() == kNumClasses; }

    // Return the top-K scene results for an image file (best first).
    std::vector<SceneResult> inferTopK(const std::string& image_path, int k = 5);
    // Return the full 365-dim softmax probability vector (zeros if unavailable).
    std::array<float, 365> getSceneVector(const std::string& image_path);
    // Return the highest-probability scene name ("" if unavailable).
    std::string getDominantScene(const std::string& image_path);

    // Index of a scene name (case-insensitive; "-" is normalized to "_" and
    // vice versa); -1 when not found.
    int labelIndex(const std::string& name) const;
    const std::vector<std::string>& labels() const { return labels_; }
    static constexpr int kNumClasses = 365;

private:
    // Decode + resize to 224x224 + normalize -> CHW float buffer (1,3,224,224).
    // Returns empty on failure.
    std::vector<float> preprocess(const std::string& image_path);
    // Raw model forward pass on a CHW buffer; returns the 365 logits (or empty).
    std::vector<float> runLogits(const float* chw, size_t numel);
    std::array<float, 365> softmax(const std::vector<float>& logits);

    Ort::Env env_;
    std::unique_ptr<Ort::Session> session_;
    std::vector<std::string> input_names_;
    std::vector<std::string> output_names_;
    std::vector<const char*> input_name_ptrs_;
    std::vector<const char*> output_name_ptrs_;
    std::vector<std::string> labels_;
    const int input_size_ = 224;
};
