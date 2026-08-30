#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "Types.h"

// One image's cache entry: detection results + the file metadata used to
// detect changes (mtime + size).
struct CacheEntry {
    std::string path;      // relative to the gallery root (e.g. "img.jpg" or "dir/img.jpg")
    int64_t mtime = 0;     // last write time, Unix seconds
    int64_t size = 0;      // file size in bytes (0 = unknown, e.g. migrated entries)
    int img_id = -1;       // per-image id (kept stable across incremental updates)
    std::vector<DetectedObject> objects;  // objects stored WITHOUT image_path
    ImageAttrs img_attrs;  // whole-image color attributes
};

// The full cache index for one base model, serialized to
// <cache_root>/<model>/cache_index.json.
struct CacheIndex {
    std::string version = "1.2";   // 1.1: exposure/blur/EXIF/user_tags; 1.2: Places365 scene_vector
    std::string model_name;
    std::vector<std::string> photo_dirs;
    int next_obj_id = 0;   // next globally-unique object id
    int next_img_id = 0;   // next image id
    std::unordered_map<std::string, CacheEntry> entries;  // key: relative path

    // Load from a cache_index.json file.  Returns false when the file is
    // missing or cannot be parsed (caller falls back to a full rebuild).
    bool loadFromFile(const std::string& path);

    // Migrate the legacy <cache>/metadata.json format (pre-incremental builds)
    // into this index.  Returns false when the file is missing or invalid.
    bool loadLegacyFromFile(const std::string& path);

    // Serialize to a cache_index.json file (creates parent dirs).  Returns
    // false on I/O failure.
    bool saveToFile(const std::string& path) const;

    // Recompute next_obj_id / next_img_id from the current entries (called
    // after load / incremental edits).
    void refreshCounters();
};
