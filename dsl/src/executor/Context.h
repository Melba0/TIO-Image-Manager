#pragma once
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include "../cache/CacheManager.h"
#include "../parser/AST.h"
#include "ModelRegistry.h"
#include "ExtensionManager.h"

// An in-memory value produced while evaluating the DSL.
struct Value {
    enum Type { IMAGE_SET, OBJECT_SET, OBJECT, ATTR, HIST_VEC, NUM, SCORE, BOOL, STRING, NONE };
    Type type = NONE;

    std::unordered_set<std::string> image_set;
    std::unordered_map<std::string, float> image_scores;  // per-image fuzzy score
    std::vector<DetectedObject> object_set;
    DetectedObject object;   // OBJECT
    Attr attr;               // ATTR
    HistVec hist_val{};      // HIST_VEC (32-bin hue histogram)
    double num_val = 0;
    float score_val = 0;     // SCORE (fuzzy probability 0..1)
    bool bool_val = false;
    std::string str_val;

    static Value makeImageSet(const std::unordered_set<std::string>& s) {
        Value v; v.type = IMAGE_SET; v.image_set = s;
        for (const auto& p : s) v.image_scores[p] = 1.0f;
        return v;
    }
    static Value makeImageSet(const std::unordered_map<std::string, float>& scores) {
        Value v; v.type = IMAGE_SET;
        for (const auto& kv : scores) {
            v.image_set.insert(kv.first);
            v.image_scores[kv.first] = kv.second;
        }
        return v;
    }
    static Value makeObjectSet(const std::vector<DetectedObject>& s) {
        Value v; v.type = OBJECT_SET; v.object_set = s; return v;
    }
    static Value makeObject(const DetectedObject& o) {
        Value v; v.type = OBJECT; v.object = o; return v;
    }
    static Value makeAttr(const Attr& a) {
        Value v; v.type = ATTR; v.attr = a; return v;
    }
    static Value makeHistVec(const HistVec& h) {
        Value v; v.type = HIST_VEC; v.hist_val = h; return v;
    }
    static Value makeNum(double n) {
        Value v; v.type = NUM; v.num_val = n; return v;
    }
    static Value makeScore(float s) {
        Value v; v.type = SCORE; v.score_val = s; return v;
    }
    static Value makeBool(bool b) {
        Value v; v.type = BOOL; v.bool_val = b; return v;
    }
    static Value makeString(const std::string& s) {
        Value v; v.type = STRING; v.str_val = s; return v;
    }
    static Value makeNone() {
        return Value{};
    }

    bool isTruthy() const {
        if (type == BOOL) return bool_val;
        if (type == SCORE) return score_val >= 0.5f;
        if (type == NUM) return num_val != 0;
        if (type == IMAGE_SET) return !image_set.empty();
        if (type == OBJECT_SET) return !object_set.empty();
        if (type == OBJECT) return true;
        return false;
    }
};

class Context {
public:
    Context(const PhotoCache& cache, ModelRegistry* registry, ExtensionManager* ext);

    const PhotoCache& getCache() const { return cache_; }
    const std::vector<DetectedObject>* getObjects(const std::string& image_path) const;
    const ImageAttrs* getImageAttrs(const std::string& image_path) const;

    ModelRegistry* getRegistry() const { return registry_; }
    ExtensionManager* getExtensionManager() const { return ext_; }

    // Variable management
    bool hasVariable(const std::string& name) const;
    Value getVariable(const std::string& name) const;
    void setVariable(const std::string& name, const Value& value);

    // ---- Macro table (built-in + user macros share the same table) ----
    void registerMacro(const std::string& name, MacroDef def);
    const MacroDef* getMacro(const std::string& name) const;

    // ---- Macro parameter scope stack (bindings are shadowed innermost-first) ----
    void pushMacroScope();
    void popMacroScope();
    void bindMacroParam(const std::string& name, const Value& value);
    const Value* findMacroParam(const std::string& name) const;

    // Current iteration context
    void setCurrentImage(const std::string& path) { current_image_ = path; }
    const std::string& getCurrentImage() const { return current_image_; }
    void clearCurrentImage() { current_image_.clear(); }

    void setCurrentObject(const DetectedObject* obj) { current_object_ = obj; }
    const DetectedObject* getCurrentObject() const { return current_object_; }
    void clearCurrentObject() { current_object_ = nullptr; }

    // The object collection of the current iteration (used by cnt() and `obj any`).
    void setCurrentObjects(const std::vector<DetectedObject>* objs) { current_objects_ = objs; }
    const std::vector<DetectedObject>* getCurrentObjects() const { return current_objects_; }
    void clearCurrentObjects() { current_objects_ = nullptr; }

    // Get all image paths
    std::unordered_set<std::string> getAllImagePaths() const;

    // ---- pre-filter pipeline (applied before the DSL runs) ----
    // When active, `$` (and therefore every image-set iteration) only covers
    // the pre-filtered image ids instead of the whole gallery.  An active
    // filter that matches nothing yields an empty set (NOT the whole gallery).
    void setPrefilteredIds(std::unordered_set<std::string> ids) {
        prefiltered_ids_ = std::move(ids);
        prefilter_active_ = true;
    }
    void clearPrefilter() {
        prefiltered_ids_.clear();
        prefilter_active_ = false;
    }
    bool prefilterActive() const { return prefilter_active_; }
    const std::unordered_set<std::string>& prefilteredIds() const { return prefiltered_ids_; }

    // ---- deletion callback (wired by the engine to CacheManager) ----
    using DeleteImagesFn = std::function<std::vector<std::string>(const std::vector<std::string>&)>;
    void setDeleteImagesCallback(DeleteImagesFn fn) { delete_cb_ = std::move(fn); }
    const DeleteImagesFn& deleteImagesCallback() const { return delete_cb_; }

private:
    const PhotoCache& cache_;
    ModelRegistry* registry_ = nullptr;
    ExtensionManager* ext_ = nullptr;
    std::unordered_map<std::string, Value> variables_;
    std::unordered_map<std::string, MacroDef> macros_;
    std::vector<std::unordered_map<std::string, Value>> macro_scopes_;
    std::string current_image_;
    const DetectedObject* current_object_ = nullptr;
    const std::vector<DetectedObject>* current_objects_ = nullptr;
    std::unordered_set<std::string> prefiltered_ids_;
    bool prefilter_active_ = false;
    DeleteImagesFn delete_cb_;
};