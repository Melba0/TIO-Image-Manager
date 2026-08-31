#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "YoloInference.h"
#include "Types.h"
#include "ModelRegistry.h"
#include "CacheIndex.h"
#include "../scene/SceneInference.h"

class ExtensionManager;

struct ImageCacheEntry {
    std::string path;
    std::vector<DetectedObject> objects;  // objects live in the cache WITHOUT image_path filled
    int64_t last_modified = 0;
    ImageAttrs img_attrs;                 // whole-image color attributes
};

struct PhotoCache {
    std::vector<ImageCacheEntry> images;
    std::string photo_dir;
    // Virtual albums: collection name -> image relative paths (logical grouping).
    std::map<std::string, std::vector<std::string>> collections;
};

class CacheManager {
public:
    // photo_dirs: one or more image directories to index (merged).  With a single
    // directory, cache paths are plain filenames; with several, they are
    // prefixed by each directory's basename (e.g. "photo/000000000009.jpg").
    CacheManager(const std::vector<std::string>& photo_dirs, const std::string& cache_root,
                 ModelRegistry& registry,
                 ObjectIdGenerator& id_gen,
                 float fallback_threshold = 0.0f, float base_conf_threshold = 0.25f,
                 float iou_threshold = 0.45f,
                 const std::string& scene_model_path = "",
                 const std::string& scene_labels_path = "");

    // Rebuild (if needed) then make sure the cache for the ACTIVE base model is loaded.
    bool ensureCacheReady();

    // Incremental update: load the existing cache_index.json (if any), diff the
    // gallery against it and re-run inference only for added / modified files.
    // Deleted files are dropped from the index.  Returns false on hard failure.
    bool loadOrBuildCache();

    // Called on model switch: drop the loaded model + cache so the next
    // ensureCacheReady() rebuilds from the new active base model.
    void invalidate();

    // Wire the extension manager so clustering packs can extract embeddings
    // during cache inference and drive the clustering pass after a rebuild.
    void setExtensionManager(ExtensionManager* ext) { ext_mgr_ = ext; }

    const PhotoCache& getPhotoCache() const { return cache_data_; }
    bool isCacheValid() const { return cache_valid_; }

    // Get objects for a specific image by its relative path
    const std::vector<DetectedObject>* getObjectsForImage(const std::string& rel_path) const;

    // ---- asset management ----
    // Delete the given images (by relative path): removes the file from disk and
    // the entry from the cache index, then re-saves.  Returns the paths that
    // were actually removed from the index.
    std::vector<std::string> removeImages(const std::vector<std::string>& relPaths);

    // Pre-filter pipeline: return the set of image ids whose user_tags match ALL
    // the given TagFilters (values are OR-ed; an empty values list matches any
    // value for that key).  Empty result when filters is empty.
    std::unordered_set<std::string> applyTagFilters(const std::vector<TagFilter>& filters) const;

    // ---- virtual album (collection) management ----
    // Add an image (relative path) to a collection, creating it if needed.
    void addToCollection(const std::string& name, const std::string& rel_path);
    // Remove an image from a collection (the collection is kept, even if empty).
    void removeFromCollection(const std::string& name, const std::string& rel_path);
    // Remove a whole collection.
    void deleteCollection(const std::string& name);
    // Rename an image's cache path (used when a file is renamed on disk):
    // updates the entry key and every collection that referenced the old path.
    bool renameImage(const std::string& old_rel, const std::string& new_rel);

    // ---- clustering (V2) ----
    // Re-run the clustering pass for every active clustering extension pack:
    // gather all cached embeddings, assign deterministic cluster ids, and
    // rebuild each image's cluster_groups.  Called after a cache build/update.
    void runClustering();

private:
    // Load cache_index.json for the active model.  Falls back to migrating the
    // legacy metadata.json format.  Returns false when no usable index exists.
    bool loadIndex();
    bool saveIndex();

    // Current gallery state (relative path, mtime, size) for change detection.
    std::vector<CacheEntry> scanPhotoDirs() const;

    // Diff the gallery against index_ and re-infer only the changed images.
    bool applyIncrementalUpdate();

    // Full inference over the whole gallery (used when there is no index).
    bool buildIndexFromScratch();

    // Run YOLO + image attrs for one image and store the results into e.
    bool inferEntry(const std::string& full_path, CacheEntry& e);

    // Rebuild the in-memory PhotoCache from index_ (sorted by path).
    void rebuildPhotoCache();

    // Read the legacy <cache>/metadata.json format into index_ (returns false on failure).
    bool migrateLegacyMetadata(const std::string& legacy_path);

    // Confidence fallback: degrade low-confidence detections to their is-a parent.
    DetectedObject applyFallback(const DetectedObject& det) const;

    void refreshCachePaths();

    std::vector<std::string> photo_dirs_;
    std::string cache_root_;
    ModelRegistry& registry_;
    std::string cache_dir_;
    std::string cache_file_;
    ObjectIdGenerator& id_gen_;
    float fallback_threshold_;
    float base_conf_threshold_;
    float iou_threshold_;

    // Prefix helper for cache image paths.
    std::string relImagePath(const std::string& dir, const std::string& file) const;

    CacheIndex index_;
    PhotoCache cache_data_;
    bool cache_valid_ = false;
    std::unique_ptr<YoloInference> yolo_;
    std::unique_ptr<SceneInference> scene_;

    std::string scene_model_path_;
    std::string scene_labels_path_;
    bool scene_tried_ = false;

    std::unordered_map<std::string, size_t> image_index_;

    ExtensionManager* ext_mgr_ = nullptr;   // clustering packs (may be null)
};
