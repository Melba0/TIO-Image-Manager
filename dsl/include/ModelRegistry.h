#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

// A class in a model package's classes.json. Every class must have a parent
// (missing parents default to "root").
struct ClassDef {
    std::string name;
    std::string parent;
};

// Information about one registered base detection model.
struct ModelInfo {
    std::string name;                 // folder name (the package id)
    std::string path;                 // absolute path to model.onnx
    std::string type;                 // "detector" (open-vocabulary models are a future extension)
    int input_size = 640;
    int classes = 80;                 // number of output classes (from meta.json)
    std::vector<ClassDef> class_defs; // all classes (output classes + parent entries)
};

// Model registry: discovers base models under models/base/*/meta.json and reads
// models/registry.json to decide which base model and extension packs are active.
// Each model package also carries classes.json which defines the model's output
// classes and their is-a parent chain (replaces the global isa_map).
class ModelRegistry {
public:
    ModelRegistry(const std::string& models_dir, const std::string& cache_root);

    // Scan base models + read registry.json. Returns false on failure.
    bool scan();

    // Re-read only registry.json (active_base / active_extensions).
    bool reload();

    const ModelInfo* getActiveBaseModel() const;
    const std::string& getActiveBaseName() const { return active_base_; }
    std::vector<std::string> getActiveExtensions() const;

    std::vector<ModelInfo> listAllBaseModels() const;
    bool hasBaseModel(const std::string& name) const;

    // Absolute cache directory for the active base model: <cache_root>/<folder>.
    std::string getBaseCacheDir() const;

    // Override the active base (used by the --base CLI switch).
    bool setActiveBase(const std::string& name);

    // ---- class hierarchy of the ACTIVE base model ----
    // Output class names by output index (indices [0, classes)).
    std::vector<std::string> getOutputClassNames() const;

    // Parent of a class ("root" when not defined).
    std::string getParent(const std::string& class_name) const;

    // True if `child` is `parent` itself or a transitive descendant.
    bool isChildOf(const std::string& child, const std::string& parent) const;

    // True if the name is a known class or a known parent in the active model.
    bool hasClass(const std::string& name) const;

private:
    bool readRegistry();
    bool scanBaseModels();

    std::string models_dir_;
    std::string cache_root_;
    std::vector<ModelInfo> base_models_;
    std::unordered_map<std::string, const ModelInfo*> base_by_name_;
    std::string active_base_;
    std::vector<std::string> active_extensions_;
};
