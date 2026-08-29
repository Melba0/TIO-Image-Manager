#define NOMINMAX
#include "OnnxInference.h"

#include <windows.h>
#include <cstring>
#include <iostream>

namespace {

// ONNX Runtime's model path parameter is ORTCHAR_T* (wchar_t on Windows).
std::wstring toWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

}  // namespace

OnnxInference::OnnxInference(int input_size, int intra_op_threads)
    : env_(ORT_LOGGING_LEVEL_WARNING, "dsl_onnx"),
      session_(nullptr),
      input_size_(input_size),
      intra_op_threads_(intra_op_threads) {}

OnnxInference::~OnnxInference() = default;

bool OnnxInference::loadModel(const std::string& model_path) {
    session_.reset();
    input_names_.clear();
    output_names_.clear();
    input_name_ptrs_.clear();
    output_name_ptrs_.clear();

    try {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(intra_op_threads_);
        opts.SetInterOpNumThreads(1);
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        // Optional CUDA EP (requires an onnxruntime GPU package):
        //   #include <cuda_provider_factory.h>
        //   OrtSessionOptionsAppendExecutionProvider_CUDA(opts, 0);

        session_ = std::make_unique<Ort::Session>(env_, toWide(model_path).c_str(), opts);

        Ort::AllocatorWithDefaultOptions alloc;
        size_t n_in = session_->GetInputCount();
        for (size_t i = 0; i < n_in; ++i) {
            auto name = session_->GetInputNameAllocated(i, alloc);
            input_names_.emplace_back(name.get());
        }
        size_t n_out = session_->GetOutputCount();
        for (size_t i = 0; i < n_out; ++i) {
            auto name = session_->GetOutputNameAllocated(i, alloc);
            output_names_.emplace_back(name.get());
        }
        for (auto& s : input_names_) input_name_ptrs_.push_back(s.c_str());
        for (auto& s : output_names_) output_name_ptrs_.push_back(s.c_str());

        return true;
    } catch (const Ort::Exception& e) {
        std::cerr << "[Onnx] Failed to load model: " << model_path << "\n"
                  << "[Onnx] " << e.what() << "\n"
                  << "[Onnx] Hint: the engine now loads .onnx models. Export your model, e.g.\n"
                  << "[Onnx]   yolo export model=model.pt format=onnx opset=12 imgsz=640\n"
                  << "[Onnx] and point meta.json / config.json at the resulting .onnx file."
                  << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[Onnx] Failed to load model: " << model_path << "\n"
                  << "[Onnx] " << e.what() << std::endl;
    }
    return false;
}

InferenceTensor OnnxInference::run(const float* chw, size_t numel) {
    InferenceTensor out;
    if (!session_ || !chw) return out;

    try {
        const std::vector<int64_t> shape = {1, 3, input_size_, input_size_};
        Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);
        // CreateTensor needs a mutable pointer; we never write to the input.
        Ort::Value input = Ort::Value::CreateTensor<float>(
            mem_info, const_cast<float*>(chw), numel, shape.data(), shape.size());

        auto outputs = session_->Run(Ort::RunOptions{nullptr},
                                     input_name_ptrs_.data(), &input, 1,
                                     output_name_ptrs_.data(), output_name_ptrs_.size());
        if (outputs.empty()) {
            std::cerr << "[Onnx] Run returned no outputs." << std::endl;
            return out;
        }

        const Ort::Value& t = outputs[0];
        auto info = t.GetTensorTypeAndShapeInfo();
        out.shape = info.GetShape();
        int64_t count = info.GetElementCount();
        out.data.resize((size_t)count);
        if (count > 0) {
            std::memcpy(out.data.data(), t.GetTensorData<float>(), (size_t)count * sizeof(float));
        }
    } catch (const Ort::Exception& e) {
        std::cerr << "[Onnx] Inference error: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[Onnx] Inference error: " << e.what() << std::endl;
    }
    return out;
}
