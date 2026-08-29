#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include "Types.h"
#include "ModelRegistry.h"
#include "InferenceBackend.h"

class OnnxInference;

// Configuration of a single extension pack (extensions/<name>/config.json).
struct ExtensionPack {
    std::string name;
    std::string parent_class;                  // expand objects whose class matches this
    std::vector<std::string> children;         // classes the extension model can output
    std::string model_path;                    // path to the .onnx (relative to project root)
    int input_size = 224;                      // crop side length fed to the model
    float conf_threshold = 0.3f;               // min confidence for a child detection
    float crop_padding = 0.1f;                 // fraction of the parent bbox to pad
    bool is_classifier = false;                // false -> detection (4+nc rows per anchor)
};

// Scans extensions/ for packs, lazily loads their .onnx models and performs the
// `>>` expansion (crop parent regions -> run the extension model -> new objects).
class ExtensionManager {
public:
    // extension_dirs: one or more directories scanned for pack subfolders
    // (e.g. <project>/models/extensions and <project>/extensions).
    ExtensionManager(const std::vector<std::string>& extension_dirs, const std::string& photo_dir,
                     ModelRegistry& registry, ObjectIdGenerator& id_gen);

    // Discover every <dir>/*/config.json across the configured directories.
    void scan();

    const ExtensionPack* getExtension(const std::string& name) const;
    bool hasExtension(const std::string& name) const { return getExtension(name) != nullptr; }
    const std::vector<ExtensionPack>& extensions() const { return packs_; }

    // Active extensions (from registry.json).  Only these may be used with `>>`.
    void setActiveExtensions(const std::vector<std::string>& names);
    bool isActive(const std::string& name) const;
    void enableExtension(const std::string& name);
    void disableExtension(const std::string& name);
    const std::unordered_set<std::string>& activeExtensions() const { return active_; }

    // Lazily load (and cache) the ONNX model of an extension pack.
    std::shared_ptr<OnnxInference> loadModel(const std::string& ext_name);

    // Expand each object whose class/super-class equals the pack's parent_class:
    // crop the region, run the extension model, map results back to image
    // coordinates and return them as new DetectedObjects (parent_id set).
    // Throws std::runtime_error if the extension is not registered/active.
    std::vector<DetectedObject> expand(const std::vector<DetectedObject>& parents,
                                       const std::string& ext_name);

private:
    std::vector<std::string> extension_dirs_;
    std::string photo_dir_;
    ModelRegistry& registry_;
    ObjectIdGenerator& id_gen_;

    std::vector<ExtensionPack> packs_;
    std::unordered_map<std::string, const ExtensionPack*> pack_by_name_;
    std::unordered_map<std::string, std::shared_ptr<OnnxInference>> models_;
    std::unordered_set<std::string> active_;

    // Crop the parent object's (padded) region into a `target x target` CHW
    // float buffer (values 0..1) and return the crop rectangle in image
    // pixels via cx/cy/cw/ch.  Empty vector on failure.
    std::vector<float> cropRegion(const std::string& img_path, const DetectedObject& obj,
                                  int target, double padding,
                                  double& cx, double& cy, double& cw, double& ch);

    // Detection-format output: [1, 4+nc, N] rows [x1, y1, x2, y2, cls...] in crop pixels.
    std::vector<DetectedObject> runDetector(const ExtensionPack& pack, const DetectedObject& parent,
                                            const InferenceTensor& preds, double cx, double cy, double cw, double ch,
                                            int img_w, int img_h);

    // Classification-format output: [1, nc] (or [nc]) softmax/logits over child classes.
    std::vector<DetectedObject> runClassifier(const ExtensionPack& pack, const DetectedObject& parent,
                                              const InferenceTensor& preds, double cx, double cy, double cw, double ch,
                                              int img_w, int img_h);
};
