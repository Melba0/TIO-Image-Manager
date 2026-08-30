#define NOMINMAX
#include "SceneInference.h"

#include <windows.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <iostream>
#include <cctype>

using namespace Gdiplus;

namespace {

std::wstring toWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// Normalize a label for comparison: lowercase + " " <-> "-" <-> "_".
std::string normLabel(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == ' ' || c == '-' || c == '_') {
            out.push_back('_');
        } else {
            out.push_back((char)std::tolower((unsigned char)c));
        }
    }
    return out;
}

}  // namespace

SceneInference::SceneInference()
    : env_(ORT_LOGGING_LEVEL_WARNING, "places365") {}

SceneInference::~SceneInference() = default;

bool SceneInference::loadModel(const std::string& model_path, const std::string& labels_path) {
    session_.reset();
    labels_.clear();
    input_names_.clear();
    output_names_.clear();
    input_name_ptrs_.clear();
    output_name_ptrs_.clear();

    // ---- labels ----
    std::ifstream lf(labels_path);
    if (!lf) {
        std::cerr << "[Scene] Cannot open labels file: " << labels_path << std::endl;
        return false;
    }
    std::string line;
    while (std::getline(lf, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        // Places365 lines look like "/b/beach 48" or "/f/forest/broadleaf 150".
        // Keep the base scene name (last "/" segment), drop the trailing index.
        std::string name = line;
        // trailing numeric index
        size_t sp = name.find_last_of(" \t");
        if (sp != std::string::npos) {
            bool all_digits = true;
            for (size_t k = sp + 1; k < name.size(); ++k) {
                if (!std::isdigit((unsigned char)name[k])) { all_digits = false; break; }
            }
            if (all_digits) name = name.substr(0, sp);
        }
        // path prefix: keep the segment after the last '/'
        size_t sl = name.find_last_of('/');
        if (sl != std::string::npos) name = name.substr(sl + 1);
        if (!name.empty()) labels_.push_back(name);
    }
    if ((int)labels_.size() != kNumClasses) {
        std::cerr << "[Scene] Expected " << kNumClasses << " scene labels, got "
                  << labels_.size() << " in " << labels_path << std::endl;
        labels_.clear();
        return false;
    }

    // ---- model ----
    try {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(4);
        opts.SetInterOpNumThreads(1);
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
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

        std::cout << "[Scene] Loaded Places365 model: " << model_path
                  << " (" << kNumClasses << " scenes, input "
                  << input_size_ << "x" << input_size_ << ")" << std::endl;
        return true;
    } catch (const Ort::Exception& e) {
        std::cerr << "[Scene] Failed to load model: " << model_path << "\n"
                  << "[Scene] " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[Scene] Failed to load model: " << model_path << "\n"
                  << "[Scene] " << e.what() << std::endl;
    }
    session_.reset();
    return false;
}

// ---- preprocessing: decode -> 224x224 -> RGB CHW, (x/255 - mean)/std ----
std::vector<float> SceneInference::preprocess(const std::string& image_path) {
    const int S = input_size_;
    Bitmap bmp(toWide(image_path).c_str());
    if (bmp.GetLastStatus() != Ok) return {};

    int ow = bmp.GetWidth();
    int oh = bmp.GetHeight();
    if (ow <= 0 || oh <= 0) return {};

    // Resize directly to 224x224 (matches the Places365 ImageNet-style input).
    Bitmap canvas(S, S, PixelFormat24bppRGB);
    Graphics g(&canvas);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.Clear(Color(0, 0, 0));
    Rect dest(0, 0, S, S);
    g.DrawImage(&bmp, dest, 0, 0, ow, oh, UnitPixel);

    Rect rect(0, 0, S, S);
    BitmapData data;
    if (canvas.LockBits(&rect, ImageLockModeRead, PixelFormat24bppRGB, &data) != Ok) return {};

    std::vector<float> tensor((size_t)3 * S * S);
    float* ptr = tensor.data();
    const unsigned char* src = (const unsigned char*)data.Scan0;
    int stride = data.Stride;

    // ImageNet stats used by Places365 models.
    constexpr float kMean[3] = {0.485f, 0.456f, 0.406f};
    constexpr float kStd[3]  = {0.229f, 0.224f, 0.225f};

    for (int y = 0; y < S; ++y) {
        const unsigned char* row = src + (size_t)y * stride;
        for (int x = 0; x < S; ++x) {
            // GDI+ 24bpp memory layout is BGR.
            unsigned char b = row[x * 3 + 0];
            unsigned char g = row[x * 3 + 1];
            unsigned char r = row[x * 3 + 2];
            ptr[0 * S * S + y * S + x] = (r / 255.0f - kMean[0]) / kStd[0];
            ptr[1 * S * S + y * S + x] = (g / 255.0f - kMean[1]) / kStd[1];
            ptr[2 * S * S + y * S + x] = (b / 255.0f - kMean[2]) / kStd[2];
        }
    }

    canvas.UnlockBits(&data);
    return tensor;
}

std::vector<float> SceneInference::runLogits(const float* chw, size_t numel) {
    std::vector<float> logits;
    if (!session_ || !chw) return logits;

    try {
        const std::vector<int64_t> shape = {1, 3, input_size_, input_size_};
        Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);
        Ort::Value input = Ort::Value::CreateTensor<float>(
            mem_info, const_cast<float*>(chw), numel, shape.data(), shape.size());

        auto outputs = session_->Run(Ort::RunOptions{nullptr},
                                     input_name_ptrs_.data(), &input, 1,
                                     output_name_ptrs_.data(), output_name_ptrs_.size());
        if (outputs.empty()) return logits;

        const Ort::Value& t = outputs[0];
        auto info = t.GetTensorTypeAndShapeInfo();
        int64_t count = info.GetElementCount();
        const float* d = t.GetTensorData<float>();
        if (!d || count <= 0) return logits;
        logits.assign(d, d + count);
    } catch (const Ort::Exception& e) {
        std::cerr << "[Scene] Inference error: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[Scene] Inference error: " << e.what() << std::endl;
    }
    return logits;
}

std::array<float, 365> SceneInference::softmax(const std::vector<float>& logits) {
    std::array<float, 365> probs{};
    if ((int)logits.size() < kNumClasses) return probs;

    float maxv = *std::max_element(logits.begin(), logits.begin() + kNumClasses);
    double sum = 0.0;
    float e[kNumClasses];
    for (int i = 0; i < kNumClasses; ++i) {
        e[i] = std::exp(logits[i] - maxv);
        sum += e[i];
    }
    for (int i = 0; i < kNumClasses; ++i) probs[i] = (float)(e[i] / sum);
    return probs;
}

std::array<float, 365> SceneInference::getSceneVector(const std::string& image_path) {
    std::array<float, 365> vec{};
    if (!valid()) return vec;
    auto chw = preprocess(image_path);
    if (chw.empty()) return vec;
    auto logits = runLogits(chw.data(), chw.size());
    return softmax(logits);
}

std::string SceneInference::getDominantScene(const std::string& image_path) {
    if (!valid()) return "";
    auto vec = getSceneVector(image_path);
    int best = 0;
    for (int i = 1; i < kNumClasses; ++i) {
        if (vec[i] > vec[best]) best = i;
    }
    return labels_[best];
}

std::vector<SceneResult> SceneInference::inferTopK(const std::string& image_path, int k) {
    std::vector<SceneResult> results;
    if (!valid()) return results;
    auto vec = getSceneVector(image_path);

    std::vector<int> idx(kNumClasses);
    for (int i = 0; i < kNumClasses; ++i) idx[i] = i;
    std::partial_sort(idx.begin(), idx.begin() + std::min(k, kNumClasses), idx.end(),
                      [&](int a, int b) { return vec[a] > vec[b]; });

    int n = std::min(k, kNumClasses);
    results.reserve(n);
    for (int i = 0; i < n; ++i) {
        results.push_back({labels_[idx[i]], vec[idx[i]]});
    }
    return results;
}

int SceneInference::labelIndex(const std::string& name) const {
    std::string n = normLabel(name);
    for (int i = 0; i < (int)labels_.size(); ++i) {
        if (normLabel(labels_[i]) == n) return i;
    }
    return -1;
}
