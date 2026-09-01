# Architecture

[**English**](architecture.md) ・ [**中文**](architecture_zh.md)

This document describes the interpreter's module decomposition, data flow and key data
structures, to help understand the code organization and how to extend it.

## 1. Module Overview

```
src/
├── main.cpp                       # entry: CLI parsing, dependency wiring, REPL, /reload, --json mode
├── parser/
│   ├── AST.h                      # AST node definitions (Expr / Quantifier / Filter / Expand / Cnt / Del / ClusterFn ...)
│   ├── Lexer.h / Lexer.cpp        # lexical analysis (UTF-8 identifiers, `:`, `>>`, `del` keyword)
│   └── Parser.h / Parser.cpp      # recursive-descent parser
├── executor/
│   ├── Context.h / Context.cpp    # evaluation context: variables, iteration context, tag pre-filter, delete callback
│   └── Evaluator.h / Evaluator.cpp# AST execution engine (filter, quantifiers, sets, cnt, >>, del, cluster macros)
├── cache/
│   ├── CacheIndex.h/.cpp          # cache index (cache_index.json, version 1.2)
│   ├── CacheManager.h/.cpp        # incremental cache build/load/invalidate, confidence fallback, removeImages, applyTagFilters, runClustering
│   └── YoloInference.h/.cpp       # YOLO ONNX inference + letterbox preprocessing + postprocessing
├── scene/
│   └── SceneInference.h/.cpp      # Places365 scene recognition (ONNX)
├── cluster/
│   └── Clustering.h/.cpp          # DBSCAN clustering over embeddings
├── engine/
│   └── OnnxInference.h/.cpp       # ONNX Runtime backend (InferenceBackend implementation)
├── utils/
│   ├── filesystem_utils.h/.cpp    # filesystem & timestamp helpers
│   └── exif_reader.h/.cpp         # lightweight built-in JPEG EXIF parsing (no exiv2)
├── BuiltinMacros.cpp              # built-in macro registration (math/color/histogram/exposure/EXIF/tags/scene/cluster...)
├── ExtensionManager.cpp           # extension pack scanning, lazy model loading, >> expansion, embedding extraction
├── ModelRegistry.cpp              # model registry (models/ scanning + registry.json)
└── (include/)  public headers
```

## 2. Dependencies

```
main
  ├── ModelRegistry        （reads models/registry.json + models/base/*/meta.json）
  ├── IsaManager           （reads config/isa_map.json）
  ├── ExtensionManager     （scans models/extensions, extensions; lazy-loads extension models）
  ├── CacheManager         （depends on ModelRegistry + ObjectIdGenerator）
  └── Context / Evaluator  （depend on CacheManager's PhotoCache + ExtensionManager）

evaluation chain: Lexer → Parser → AST → Evaluator
inference chain:  YoloInference (base) / ExtensionManager (extension) → DetectedObject → Cache / Value
backend:          OnnxInference (ONNX Runtime CPU, drives base and extension models uniformly)
```

## 3. Key Data Structures

### DetectedObject (include/Types.h)

```cpp
struct DetectedObject {
    std::string image_path;       // image path relative to photo/
    std::string class_name;       // current class name (may have been fallback-rewritten)
    double x, y, w, h, area;      // normalized box coordinates and area
    double confidence;            // detection confidence
    Attr attr;                    // region HSV mean/std, color temp, dominant color, 32-bin histogram, LBP roughness, sharpness
    std::string original_class;   // class before fallback (non-empty when is_fallback)
    std::string super_class;      // isa parent
    bool is_fallback;             // whether it was fallback-rewritten
    int parent_id;                // parent object id (>> output links to parent)
    int obj_id;                   // globally unique object id
    int img_id;                   // image id
    std::map<std::string, std::vector<float>> embeddings;   // embedding_name → vector (clustering packs, V2)
    std::map<std::string, std::string> cluster_ids;         // cluster_name → cluster id (V2)
};
```

`ObjectIdGenerator` is a monotonically increasing global id generator shared by
CacheManager (cache build) and ExtensionManager (extension inference) to keep ids globally unique.

### ImageAttrs (include/Types.h)

Computed for every image during cache pre-processing and persisted with the index:

| Group | Fields |
|-------|--------|
| Color | `color_temperature` `avg_hue` `avg_saturation` `avg_value` `dominant_color` `global_hue_hist[32]` |
| Exposure | `luma_hist[64]` `overexposure_score` `underexposure_score` `exposure_goodness` |
| Sharpness | `global_blur_score` `global_blur_raw` |
| EXIF | `camera_make` `camera_model` `iso` `shutter_speed` `aperture` `focal_length` `datetime_original` `width` `height` |
| User tags | `user_tags` (key→value, edited in the GUI detail panel) |
| Scene | `scene_vector[365]` `dominant_scene` `indoor_score` (Places365) |
| Clustering | `cluster_groups` (key=cluster_name → list of cluster ids in this image, V2) |

### TagFilter (include/Types.h)

```cpp
struct TagFilter {
    std::string key;                 // tag name
    std::vector<std::string> values; // allowed values (OR); empty = any value for this key
};
```

### Value (executor/Context.h)

Evaluation result type: `IMAGE_SET / OBJECT_SET / OBJECT / ATTR / HIST_VEC / NUM / BOOL / STRING / NONE`.

## 4. Data Flow

### Query execution (read path)

```
DSL text
  → Lexer (token stream)
  → Parser (AST)
  → Evaluator
      ├─ $          → ImageSet (whole library; only matching images when tag pre-filter is active)
      ├─ $ : (cond) → evaluate the condition per image, keep score>0 (FilterExpr)
      ├─ any/all    → iterate the set, bind current_object / current_objects, evaluate the condition
      ├─ %          → expand each image's objects into an ObjectSet
      ├─ cnt(cls)   → count based on current_objects + isa_map
      ├─ >>         → call ExtensionManager.expand (crop + extension model inference)
      ├─ ^          → extract image paths from an ObjectSet, dedup
      ├─ collection(name) → album ImageSet (from the cache index `collections` field)
      ├─ cluster_id/cluster_sim → read clustering ids (V2)
      └─ del        → call Context::deleteImagesCallback (→ CacheManager.removeImages)
  → printValue / JSON
```

### Cache build (write path)

```
startup → ModelRegistry.getActiveBaseModel()
  → CacheManager.loadOrBuildCache()
      ├─ loadIndex()                read cache/<model>/cache_index.json
      │   └─ (legacy metadata.json auto-migrated)
      ├─ applyIncrementalUpdate()   compare mtime/size, only re-infer added/modified images, prune deleted ones
      │   └─ YoloInference.detect(img) (GDI+ decode → letterbox → ONNX → decode)
      │       + SceneInference (Places365) + exif_reader (EXIF) + image attributes (histogram/exposure/sharpness)
      │       + embedding extraction (active clustering packs, V2)
      └─ if index missing/corrupted → buildIndexFromScratch() full rebuild

post-build → CacheManager.runClustering()  (for each active pack with can_cluster:
                                             collect embeddings → DBSCAN → write cluster_ids/cluster_groups → saveIndex)
```

### Tag pre-filter (write/read path)

```
--tag-filter key=v1|v2 (AND across conditions, OR across values)
  → CacheManager.applyTagFilters()
  → Context.setPrefilteredIds() (marks the filter active even for an empty set)
  → $ only traverses matching images (0 matches → empty result)
```

### Extension refinement (`>>` path)

```
ObjectSet
  → ExtensionManager.expand(parents, ext_name)
      → filter objects whose class_name/super_class == pack.parent_class
      → cropRegion: crop by object box + crop_padding, resize to input_size
      → loadModel: lazily load the extension .onnx
      → runClassifier / runDetector (output [1,n] or [1,4+n,anchors])
      → map result coordinates back to the original image, build DetectedObject (parent_id = parent object id)
```

### Clustering pipeline (V2)

```
CacheManager::inferEntry
  → for each object matching parent_class → ExtensionManager.extractEmbedding
      (crop + input_normalize ("imagenet" per-channel) → ONNX → L2-normalize → store in object.embeddings[embedding_name])

CacheManager::runClustering (after full/incremental build)
  → collect embeddings for the pack's embedding_name across the library, sort by (path, obj_id)
  → DBSCAN (minPts=1, cosine threshold cluster_threshold)
  → cluster id format: <cluster_name>_<parent_class>_<NNN>  (e.g. face_cluster_person_001)
  → write object.cluster_ids[cluster_name] + rebuild per-image cluster_groups[cluster_name]
  → saveIndex()
```

## 5. Model Switching & Cache Isolation

- `models/registry.json` `active_base` decides the current model;
- `CacheManager` cache directory = `cache/<active_base>/`;
- after `/reload` or restart, if `active_base` changed the old cache is ignored and rebuilt on the next query;
- `--base <name>` overrides the config file for the current run.

## 6. Extension Points

| Extension point | Current | Future direction |
|-----------------|---------|------------------|
| Base model | YOLO detector (fixed output `[1, 4+nc, anchors]`) | open-vocabulary models (YOLO-World style, dynamic classes) |
| Input size | read from meta.json, drives preprocessing/decode | — |
| Inference backend | ONNX Runtime (CPU) | GPU (CUDA) EP |
| Extension model output | classifier (`[1,n]`), detector (`[1,4+n,anchors]`), embedding (`[1,D]`) | more formats via `runClassifier` / `runDetector` |
| Class inheritance | single/multi-level in `classes.json` (child→parent→grandparent) | — |
| Object id | global monotonic counter, shared across cache & extensions | — |

## 7. Build Notes

- Language standard: C++17;
- Inference backend: ONNX Runtime (`onnxruntime-win-x64-<ver>.zip`, wired via `find_package(onnxruntime)`);
- Image decoding uses Windows GDI+ / Windowscodecs (`gdiplus.lib` / `windowscodecs`) instead of OpenCV;
- EXIF parsing is a built-in implementation (`src/utils/exif_reader.{h,cpp}`), no exiv2;
- nlohmann/json is found via CMake (use `-DNLOHMANN_INCLUDE_DIR` to point at the include dir).
