#include "Context.h"
#include <stdexcept>

Context::Context(const PhotoCache& cache, ModelRegistry* registry, ExtensionManager* ext)
    : cache_(cache), registry_(registry), ext_(ext) {}

const std::vector<DetectedObject>* Context::getObjects(const std::string& image_path) const {
    for (const auto& img : cache_.images) {
        if (img.path == image_path) {
            return &img.objects;
        }
    }
    return nullptr;
}

const ImageAttrs* Context::getImageAttrs(const std::string& image_path) const {
    for (const auto& img : cache_.images) {
        if (img.path == image_path) {
            return &img.img_attrs;
        }
    }
    return nullptr;
}

bool Context::hasVariable(const std::string& name) const {
    return variables_.find(name) != variables_.end();
}

Value Context::getVariable(const std::string& name) const {
    auto it = variables_.find(name);
    if (it != variables_.end()) {
        return it->second;
    }
    throw std::runtime_error("Undefined variable: " + name);
}

void Context::setVariable(const std::string& name, const Value& value) {
    if (variables_.find(name) != variables_.end()) {
        throw std::runtime_error("Variable already defined (immutable): " + name);
    }
    variables_[name] = value;
}

std::unordered_set<std::string> Context::getAllImagePaths() const {
    // Pre-filtered pipeline: when a tag filter is active, `$` only covers the
    // matching images (an active filter matching nothing yields the empty set).
    if (prefilter_active_) return prefiltered_ids_;
    std::unordered_set<std::string> paths;
    for (const auto& img : cache_.images) {
        paths.insert(img.path);
    }
    return paths;
}

std::unordered_set<std::string> Context::getCollection(const std::string& name) const {
    auto it = cache_.collections.find(name);
    if (it == cache_.collections.end()) return {};
    return std::unordered_set<std::string>(it->second.begin(), it->second.end());
}

void Context::registerMacro(const std::string& name, MacroDef def) {
    macros_[name] = std::move(def);
}

const MacroDef* Context::getMacro(const std::string& name) const {
    auto it = macros_.find(name);
    return it != macros_.end() ? &it->second : nullptr;
}

void Context::pushMacroScope() {
    macro_scopes_.emplace_back();
}

void Context::popMacroScope() {
    if (!macro_scopes_.empty()) macro_scopes_.pop_back();
}

void Context::bindMacroParam(const std::string& name, const Value& value) {
    macro_scopes_.back()[name] = value;
}

const Value* Context::findMacroParam(const std::string& name) const {
    for (auto it = macro_scopes_.rbegin(); it != macro_scopes_.rend(); ++it) {
        auto f = it->find(name);
        if (f != it->end()) return &f->second;
    }
    return nullptr;
}