#pragma once
#include "InferenceBackend.h"
#include <onnxruntime_cxx_api.h>
#include <memory>
#include <vector>

// ONNX Runtime backend.
//
// Thread-safety: a single Ort::Session is shared by the cache builder and
// extension expansion.  ONNX Runtime sessions are thread-safe for concurrent
// Run() calls, so no extra locking is required.
class OnnxInference : public InferenceBackend {
public:
    explicit OnnxInference(int input_size = 640, int intra_op_threads = 4);
    ~OnnxInference() override;

    bool loadModel(const std::string& model_path) override;
    bool valid() const override { return session_ != nullptr; }
    int inputSize() const override { return input_size_; }

    InferenceTensor run(const float* chw, size_t numel) override;

private:
    Ort::Env env_;
    std::unique_ptr<Ort::Session> session_;
    int input_size_;
    int intra_op_threads_;

    std::vector<std::string> input_names_;
    std::vector<std::string> output_names_;
    std::vector<const char*> input_name_ptrs_;
    std::vector<const char*> output_name_ptrs_;
};
