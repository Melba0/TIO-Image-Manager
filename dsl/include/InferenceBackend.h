#pragma once
#include <string>
#include <vector>
#include <cstdint>

// ---------------------------------------------------------------------------
// Inference backend abstraction.
//
// The engine never talks to a concrete runtime (LibTorch / ONNX Runtime /
// TensorRT / ...) directly; it only goes through InferenceBackend.  This lets
// the inference stack be swapped without touching the DSL engine, cache
// manager or extension machinery.
//
// Contract:
//   * loadModel() is called before any run().  On failure it prints a hint to
//     stderr (including how to obtain the ONNX file) and returns false.
//   * run() takes a CHW float tensor (values 0..1) shaped
//     (1, 3, inputSize(), inputSize()) and returns the raw model output.
// ---------------------------------------------------------------------------

// Raw inference output: row-major float tensor + its shape.
struct InferenceTensor {
    std::vector<int64_t> shape;
    std::vector<float> data;

    bool defined() const { return !data.empty(); }
    int64_t numel() const { return (int64_t)data.size(); }
    int64_t dim() const { return (int64_t)shape.size(); }
};

class InferenceBackend {
public:
    virtual ~InferenceBackend() = default;

    // Load the model file.  Returns false on failure (an error + a model
    // export hint is printed to stderr).
    virtual bool loadModel(const std::string& model_path) = 0;

    virtual bool valid() const = 0;
    virtual int inputSize() const = 0;

    // Run inference on a (1,3,S,S) CHW float tensor with `numel` elements.
    // The returned tensor is owned by the caller (contiguous, CPU).
    virtual InferenceTensor run(const float* chw, size_t numel) = 0;
};
