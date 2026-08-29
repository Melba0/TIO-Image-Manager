#define NOMINMAX
#include "ExtensionManager.h"
#include "engine/OnnxInference.h"

#include <windows.h>
#include <gdiplus.h>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace Gdiplus;

namespace {

std::wstring toWide(const std::string& s) {
    if (s.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring ws(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &ws[0], size);
    return ws;
}

}  // namespace

ExtensionManager::ExtensionManager(const std::vector<std::string>& extension_dirs,
                                   const std::string& photo_dir,
                                   ModelRegistry& registry,
                                   ObjectIdGenerator& id_gen)
    : extension_dirs_(extension_dirs), photo_dir_(photo_dir),
      registry_(registry), id_gen_(id_gen) {}

void ExtensionManager::scan() {
    packs_.clear();
    pack_by_name_.clear();

    std::unordered_set<std::string> seen;

    for (const auto& dir : extension_dirs_) {
        if (!fs::exists(dir)) continue;

        for (const auto& entry : fs::directory_iterator(dir)) {
            if (!entry.is_directory()) continue;
            fs::path cfg = entry.path() / "config.json";
            if (!fs::exists(cfg)) continue;

            try {
                std::ifstream f(cfg);
                auto j = json::parse(f);

                ExtensionPack pack;
                pack.name = j.value("name", entry.path().filename().string());
                if (seen.count(pack.name)) continue;  // first directory wins
                pack.parent_class = j.value("parent_class", "");
                for (const auto& c : j.value("children", json::array())) {
                    pack.children.push_back(c);
                }
                pack.model_path = j.value("model_path", "");
                pack.input_size = j.value("input_size", 224);
                pack.conf_threshold = j.value("conf_threshold", 0.3f);
                pack.crop_padding = j.value("crop_padding", 0.1f);
                pack.is_classifier = j.value("is_classifier", false);

                if (pack.model_path.empty()) continue;

                seen.insert(pack.name);
                packs_.push_back(std::move(pack));
            } catch (const std::exception& e) {
                std::cerr << "[Ext] Failed to parse " << cfg.string() << ": " << e.what() << std::endl;
            }
        }
    }

    // Build the name index AFTER the vector is complete so pointers stay stable.
    for (auto& p : packs_) {
        pack_by_name_[p.name] = &p;
    }
}

void ExtensionManager::setActiveExtensions(const std::vector<std::string>& names) {
    active_.clear();
    for (const auto& n : names) {
        if (pack_by_name_.count(n)) active_.insert(n);
    }
}

bool ExtensionManager::isActive(const std::string& name) const {
    return active_.count(name) != 0;
}

void ExtensionManager::enableExtension(const std::string& name) {
    if (pack_by_name_.count(name)) active_.insert(name);
}

void ExtensionManager::disableExtension(const std::string& name) {
    active_.erase(name);
}

const ExtensionPack* ExtensionManager::getExtension(const std::string& name) const {
    auto it = pack_by_name_.find(name);
    return it != pack_by_name_.end() ? it->second : nullptr;
}

std::shared_ptr<OnnxInference> ExtensionManager::loadModel(const std::string& ext_name) {
    auto it = models_.find(ext_name);
    if (it != models_.end()) return it->second;

    const ExtensionPack* pack = getExtension(ext_name);
    if (!pack) return nullptr;

    // Resolve model path relative to the project root (parent of the first
    // extension directory, e.g. <project>/models/extensions -> <project>).
    std::string model_path = pack->model_path;
    if (!fs::path(model_path).is_absolute() && !extension_dirs_.empty()) {
        auto base = fs::path(extension_dirs_[0]).parent_path();
        model_path = (base / model_path).string();
    }

    auto module = std::make_shared<OnnxInference>(pack->input_size, 4);
    if (!module->loadModel(model_path)) return nullptr;

    models_[ext_name] = module;
    return module;
}

std::vector<float> ExtensionManager::cropRegion(const std::string& img_path, const DetectedObject& obj,
                                                int target, double padding,
                                                double& cx, double& cy, double& cw, double& ch) {
    Bitmap bmp(toWide(img_path).c_str());
    if (bmp.GetLastStatus() != Ok) return {};
    int img_w = bmp.GetWidth();
    int img_h = bmp.GetHeight();
    if (img_w <= 0 || img_h <= 0) return {};

    double pw = obj.w * img_w, ph = obj.h * img_h;
    double px = obj.x * img_w, py = obj.y * img_h;

    double pad = 1.0 + 2.0 * padding;
    cw = pw * pad;
    ch = ph * pad;
    cx = px - cw / 2;
    cy = py - ch / 2;

    double x1 = std::max(0.0, cx);
    double y1 = std::max(0.0, cy);
    double x2 = std::min((double)img_w, cx + cw);
    double y2 = std::min((double)img_h, cy + ch);
    if (x2 <= x1 || y2 <= y1) return {};

    cx = x1; cy = y1; cw = x2 - x1; ch = y2 - y1;

    Bitmap canvas(target, target, PixelFormat24bppRGB);
    Graphics g(&canvas);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.Clear(Color(114, 114, 114));
    Rect dest(0, 0, target, target);
    g.DrawImage(&bmp, dest, (int)cx, (int)cy, (int)cw, (int)ch, UnitPixel);

    Rect rect(0, 0, target, target);
    BitmapData data;
    if (canvas.LockBits(&rect, ImageLockModeRead, PixelFormat24bppRGB, &data) != Ok) return {};

    std::vector<float> tensor((size_t)3 * target * target);
    float* ptr = tensor.data();
    const unsigned char* src = (const unsigned char*)data.Scan0;
    int stride = data.Stride;

    for (int y = 0; y < target; ++y) {
        const unsigned char* row = src + (size_t)y * stride;
        for (int x = 0; x < target; ++x) {
            unsigned char b = row[x * 3 + 0];
            unsigned char g = row[x * 3 + 1];
            unsigned char r = row[x * 3 + 2];
            ptr[0 * target * target + y * target + x] = r / 255.0f;
            ptr[1 * target * target + y * target + x] = g / 255.0f;
            ptr[2 * target * target + y * target + x] = b / 255.0f;
        }
    }

    canvas.UnlockBits(&data);
    return tensor;
}

std::vector<DetectedObject> ExtensionManager::runClassifier(
    const ExtensionPack& pack, const DetectedObject& parent,
    const InferenceTensor& preds, double cx, double cy, double cw, double ch, int img_w, int img_h) {
    std::vector<DetectedObject> out;

    // Accept [1, nc] or [nc] logits.
    int64_t nc = 0;
    const float* p = nullptr;
    if (preds.dim() == 2 && preds.shape[0] == 1 && preds.shape[1] >= 1) {
        nc = preds.shape[1];
        p = preds.data.data();
    } else if (preds.dim() == 1 && preds.shape[0] >= 1) {
        nc = preds.shape[0];
        p = preds.data.data();
    }
    if (nc == 0 || nc != (int64_t)pack.children.size()) return out;

    float mx = p[0];
    for (int64_t i = 1; i < nc; ++i) mx = std::max(mx, p[i]);

    double sum = 0;
    std::vector<double> probs((size_t)nc);
    for (int64_t i = 0; i < nc; ++i) {
        probs[(size_t)i] = std::exp((double)p[i] - mx);
        sum += probs[(size_t)i];
    }
    int best = 0;
    for (int64_t i = 1; i < nc; ++i) {
        if (probs[(size_t)i] > probs[(size_t)best]) best = (int)i;
    }
    double conf = probs[(size_t)best] / sum;
    if (conf < pack.conf_threshold) return out;

    DetectedObject o;
    o.image_path = parent.image_path;
    o.class_name = pack.children[best];
    o.x = (cx + cw / 2) / img_w;
    o.y = (cy + ch / 2) / img_h;
    o.w = cw / img_w;
    o.h = ch / img_h;
    o.area = o.w * o.h;
    o.confidence = conf;
    o.score = (parent.score > 0 ? parent.score : (float)parent.confidence) * 0.9f;
    o.super_class = parent.class_name;
    o.is_fallback = false;
    o.parent_id = parent.obj_id;
    o.obj_id = id_gen_.next();
    o.img_id = parent.img_id;
    out.push_back(o);
    return out;
}

std::vector<DetectedObject> ExtensionManager::runDetector(
    const ExtensionPack& pack, const DetectedObject& parent,
    const InferenceTensor& preds, double cx, double cy, double cw, double ch, int img_w, int img_h) {
    std::vector<DetectedObject> out;

    // Detection-format output: [1, 4+nc, N] rows [x1, y1, x2, y2, cls...] in crop pixels.
    if (preds.dim() != 3 || preds.shape[0] != 1) return out;
    int64_t rows = preds.shape[1];
    int64_t n = preds.shape[2];
    int64_t nc = rows - 4;
    if (nc <= 0 || nc != (int64_t)pack.children.size()) return out;

    const float* p = preds.data.data();
    for (int64_t j = 0; j < n; ++j) {
        float x1 = p[0 * n + j];
        float y1 = p[1 * n + j];
        float x2 = p[2 * n + j];
        float y2 = p[3 * n + j];

        float best_conf = 0.0f;
        int best_cls = -1;
        for (int64_t c = 0; c < nc; ++c) {
            float s = p[(4 + c) * n + j];
            if (s > best_conf) {
                best_conf = s;
                best_cls = (int)c;
            }
        }
        if (best_conf < pack.conf_threshold) continue;
        if (x2 <= x1 || y2 <= y1) continue;

        // box is in crop pixels (0..input_size) -> map to image pixels
        double iw = cw, ih = ch;
        double ix1 = cx + x1 / (double)pack.input_size * iw;
        double iy1 = cy + y1 / (double)pack.input_size * ih;
        double ix2 = cx + x2 / (double)pack.input_size * iw;
        double iy2 = cy + y2 / (double)pack.input_size * ih;

        ix1 = std::max(0.0, ix1);
        iy1 = std::max(0.0, iy1);
        ix2 = std::min((double)img_w, ix2);
        iy2 = std::min((double)img_h, iy2);
        if (ix2 <= ix1 || iy2 <= iy1) continue;

        DetectedObject o;
        o.image_path = parent.image_path;
        o.class_name = pack.children[best_cls];
        o.x = (ix1 + ix2) / 2.0 / img_w;
        o.y = (iy1 + iy2) / 2.0 / img_h;
        o.w = (ix2 - ix1) / img_w;
        o.h = (iy2 - iy1) / img_h;
        o.area = o.w * o.h;
        o.confidence = best_conf;
        o.score = (parent.score > 0 ? parent.score : (float)parent.confidence) * 0.9f;
        o.super_class = parent.class_name;
        o.is_fallback = false;
        o.parent_id = parent.obj_id;
        o.obj_id = id_gen_.next();
        o.img_id = parent.img_id;
        out.push_back(o);
    }
    return out;
}

std::vector<DetectedObject> ExtensionManager::expand(const std::vector<DetectedObject>& parents,
                                                     const std::string& ext_name) {
    std::vector<DetectedObject> result;

    const ExtensionPack* pack = getExtension(ext_name);
    if (!pack) {
        throw std::runtime_error("Unknown extension: " + ext_name);
    }
    if (!isActive(ext_name)) {
        throw std::runtime_error("Extension \"" + ext_name + "\" is not active in registry.");
    }
    // Linkage: the extension's parent_class must exist in the base model's classes.json.
    if (!pack->parent_class.empty() && !registry_.hasClass(pack->parent_class)) {
        throw std::runtime_error("Parent class '" + pack->parent_class +
                                 "' not found in base model classes.");
    }
    auto model = loadModel(ext_name);
    if (!model) {
        throw std::runtime_error("Extension \"" + ext_name + "\" model could not be loaded.");
    }

    for (const auto& parent : parents) {
        // Only expand objects belonging to the pack's parent class.
        if (parent.class_name != pack->parent_class && parent.super_class != pack->parent_class) {
            continue;
        }

        std::string img_path = (fs::path(photo_dir_) / parent.image_path).string();
        double cx = 0, cy = 0, cw = 0, ch = 0;
        std::vector<float> input = cropRegion(img_path, parent, pack->input_size, pack->crop_padding,
                                              cx, cy, cw, ch);
        if (input.empty()) continue;

        Bitmap bmp(toWide(img_path).c_str());
        int img_w = bmp.GetWidth();
        int img_h = bmp.GetHeight();

        InferenceTensor preds = model->run(input.data(), input.size());
        if (!preds.defined()) continue;

        std::vector<DetectedObject> expanded;
        bool classifier = pack->is_classifier || preds.dim() == 1 ||
                          (preds.dim() == 2 && preds.shape[0] == 1);
        if (classifier) {
            expanded = runClassifier(*pack, parent, preds, cx, cy, cw, ch, img_w, img_h);
        } else {
            expanded = runDetector(*pack, parent, preds, cx, cy, cw, ch, img_w, img_h);
        }
        result.insert(result.end(), expanded.begin(), expanded.end());
    }

    return result;
}
