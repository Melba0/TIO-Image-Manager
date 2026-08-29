#include "ModelRegistry.h"
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace fs = std::filesystem;
using json = nlohmann::json;

ModelRegistry::ModelRegistry(const std::string& models_dir, const std::string& cache_root)
    : models_dir_(models_dir), cache_root_(cache_root) {}

bool ModelRegistry::scanBaseModels() {
    base_models_.clear();
    base_by_name_.clear();

    fs::path base_dir = fs::path(models_dir_) / "base";
    if (!fs::exists(base_dir)) return false;

    for (const auto& entry : fs::directory_iterator(base_dir)) {
        if (!entry.is_directory()) continue;
        fs::path meta = entry.path() / "meta.json";
        fs::path model_onnx = entry.path() / "model.onnx";
        fs::path cls = entry.path() / "classes.json";
        if (!fs::exists(meta) || !fs::exists(model_onnx)) continue;

        try {
            std::ifstream f(meta);
            auto j = json::parse(f);

            ModelInfo info;
            info.name = entry.path().filename().string();  // package id == folder name
            info.type = j.value("type", "detector");
            info.input_size = j.value("input_size", 640);
            info.classes = j.value("classes", 80);
            info.path = model_onnx.string();

            // classes.json: output classes + parent chain (replaces the global isa_map)
            if (fs::exists(cls)) {
                auto cj = json::parse(std::ifstream(cls));
                for (const auto& c : cj.value("classes", json::array())) {
                    ClassDef cd;
                    cd.name = c.value("name", "");
                    if (c.contains("parent") && c["parent"].is_string()) {
                        cd.parent = c["parent"].get<std::string>();
                    }
                    // enforced: every class has a parent (default "root")
                    if (cd.parent.empty()) cd.parent = "root";
                    if (!cd.name.empty()) info.class_defs.push_back(std::move(cd));
                }
            }

            base_models_.push_back(std::move(info));
        } catch (const std::exception& e) {
            std::cerr << "[Registry] Failed to parse " << meta.string() << ": " << e.what() << std::endl;
        }
    }

    // Index AFTER the vector is complete so pointers stay stable.
    for (auto& m : base_models_) {
        base_by_name_[m.name] = &m;
    }
    return !base_models_.empty();
}

bool ModelRegistry::readRegistry() {
    fs::path reg = fs::path(models_dir_) / "registry.json";
    if (!fs::exists(reg)) return false;
    try {
        std::ifstream f(reg);
        auto j = json::parse(f);
        active_base_ = j.value("active_base", "");
        active_extensions_.clear();
        for (const auto& e : j.value("active_extensions", json::array())) {
            active_extensions_.push_back(e);
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Registry] Failed to read " << reg.string() << ": " << e.what() << std::endl;
        return false;
    }
}

bool ModelRegistry::scan() {
    bool ok_models = scanBaseModels();
    bool ok_reg = readRegistry();
    if (!active_base_.empty() && base_by_name_.count(active_base_) == 0) {
        std::cerr << "[Registry] active_base '" << active_base_ << "' not found in models/base/"
                  << " falling back to '" << (base_models_.empty() ? "" : base_models_[0].name) << "'" << std::endl;
        if (!base_models_.empty()) active_base_ = base_models_[0].name;
    }
    return ok_models && ok_reg;
}

bool ModelRegistry::reload() {
    return readRegistry();
}

const ModelInfo* ModelRegistry::getActiveBaseModel() const {
    auto it = base_by_name_.find(active_base_);
    return it != base_by_name_.end() ? it->second : nullptr;
}

std::vector<std::string> ModelRegistry::getActiveExtensions() const {
    return active_extensions_;
}

std::vector<ModelInfo> ModelRegistry::listAllBaseModels() const {
    return base_models_;
}

bool ModelRegistry::hasBaseModel(const std::string& name) const {
    return base_by_name_.count(name) != 0;
}

std::string ModelRegistry::getBaseCacheDir() const {
    return (fs::path(cache_root_) / active_base_).string();
}

bool ModelRegistry::setActiveBase(const std::string& name) {
    if (base_by_name_.count(name) == 0) return false;
    active_base_ = name;
    return true;
}

// ---- class hierarchy of the ACTIVE base model ----

std::vector<std::string> ModelRegistry::getOutputClassNames() const {
    std::vector<std::string> names;
    const ModelInfo* m = getActiveBaseModel();
    if (!m) return names;
    int n = std::min<int>(m->classes, (int)m->class_defs.size());
    for (int i = 0; i < n; ++i) {
        names.push_back(m->class_defs[i].name);
    }
    return names;
}

std::string ModelRegistry::getParent(const std::string& class_name) const {
    const ModelInfo* m = getActiveBaseModel();
    if (!m) return "root";
    for (const auto& cd : m->class_defs) {
        if (cd.name == class_name) return cd.parent;
    }
    return "root";
}

bool ModelRegistry::isChildOf(const std::string& child, const std::string& parent) const {
    if (child == parent) return true;
    if (parent == "root") return true;  // root is the universal ancestor
    const ModelInfo* m = getActiveBaseModel();
    if (!m) return false;

    std::string cur = child;
    std::unordered_set<std::string> seen;
    while (true) {
        if (seen.count(cur)) return false;  // cycle guard
        seen.insert(cur);
        std::string p = "root";
        for (const auto& cd : m->class_defs) {
            if (cd.name == cur) { p = cd.parent; break; }
        }
        if (p == parent) return true;
        if (p == cur || p == "root") return false;
        cur = p;
    }
}

bool ModelRegistry::hasClass(const std::string& name) const {
    const ModelInfo* m = getActiveBaseModel();
    if (!m) return false;
    for (const auto& cd : m->class_defs) {
        if (cd.name == name) return true;
        if (cd.parent == name) return true;
    }
    return false;
}