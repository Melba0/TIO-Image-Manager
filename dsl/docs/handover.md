# Project Handover (Team Guide)

[**English**](handover.md) ・ [**中文**](handover_zh.md)

> This document summarizes the project's current architecture, completed work, build/run
> procedures and known caveats, so an incoming team can get up to speed quickly.
> Last updated: 2026-09.

## 1. Project Overview

**Image-retrieval DSL tool (tio)**: a desktop application that retrieves images from a
natural-language description. The user types a sentence (e.g. *"a cat to the left of a dog"*), an
LLM translates it into a DSL query, and the DSL interpreter performs fuzzy retrieval over the
library, returning ranked images.

- **GUI**: Qt 6 (`tio/gui`, artifact `tio.exe`). Handles input, LLM calls, result display and the
  settings page.
- **Engine**: C++17 DSL interpreter (`tio/dsl`, artifact `dsl.exe`). Parses/evaluates DSL, loads
  ONNX models for inference, manages the cache, and runs extension-pack refinement.
- **Inference backend**: ONNX Runtime (CPU); LibTorch has been fully removed.
- **Models**: base `yolov8m-oiv7.onnx` (Open Images V7, 601 classes, with built-in
  `fruit`/`food`/`animal`/`vehicle` parent classes); Places365 scene recognition (ResNet18/50 →
  ONNX, 365 classes, user-provided model files); `face_recognition_v1` clustering extension pack
  (MobileFaceNet → 128-dim embedding → DBSCAN people clustering).
- **Data**: gallery in `tio/photo` (128 images).

## 2. Architecture Overview

```
user input ──► tio.exe (Qt GUI) ──LLM──► DSL code ──QProcess──► dsl.exe (engine)
                                                                     │
                                        ┌────────────────────────────┤
                                        ▼                            ▼
                                cache/<model>/cache_index.json   models/base/*/model.onnx
                                 (incremental cache, detections)  + models/registry.json
```

- The GUI launches the engine via `QProcess` in `--json` mode; DSL goes in on stdin, results come
  back as JSON on stdout.
- On startup the engine calls `ensureCacheReady()`: load/incrementally update the cache (§6), and
  run embedding extraction + clustering for active clustering packs.
- During evaluation: `%` extracts objects, `^` lifts images, `>>` does extension refinement,
  `cnt()` counts with inheritance.

## 3. Directory Layout

```
tio/
├── photo/                        # gallery images (.jpg/.png)
├── dsl/                          # engine project
│   ├── CMakeLists.txt            # MSVC + Ninja + ONNX Runtime
│   ├── cmake/Findonnxruntime.cmake
│   ├── include/                  # Types / InferenceBackend / ModelRegistry / ExtensionManager
│   ├── src/
│   │   ├── main.cpp              # entry + --json REPL/file modes
│   │   ├── cache/                # CacheManager (incremental) + CacheIndex + YoloInference (ONNX)
│   │   ├── scene/                # SceneInference (Places365, ONNX, pure C++/GDI+)
│   │   ├── cluster/              # Clustering (DBSCAN over embeddings)
│   │   ├── engine/               # OnnxInference (ONNX Runtime backend)
│   │   ├── executor/             # Evaluator / Context / BuiltinMacros
│   │   ├── parser/               # Lexer / Parser / AST (hand-written recursive descent, no Lark)
│   │   └── utils/                # filesystem_utils (mtime/size) + exif_reader (built-in EXIF)
│   ├── models/
│   │   ├── registry.json         # active_base / active_extensions
│   │   ├── base/yolov8m-oiv7/    # {model.onnx, meta.json, classes.json}
│   │   ├── extensions/           # extension packs (face_recognition_v1, see §8)
│   │   └── scene/                # Places365 (user-provided .onnx + categories, see its README)
│   ├── cache/                    # generated at runtime: <model>/cache_index.json
│   ├── config/                   # settings.ini (GUI settings + inference thresholds) + cluster_name_mappings.json
│   ├── docs/                     # this directory (EN / 中文)
│   └── gui/                      # Qt project (artifact gui/build/tio.exe + bundled dsl.exe)
```

## 4. Completed Work

### 4.1 GUI fixes
1. **Extension/model packs not displayed**: the settings panel rescans on `showEvent` and
   re-scans when managers emit `modelsChanged`/`packsChanged`.
2. **Settings page flickering white→black**: `currentItemChanged` restores the last selected row
   on null; added `#navList` selected-state QSS (white text on blue).
3. **Bilingual switching was no-op**: `LanguageManager`'s translator is now a real `ZhTranslator`
   (was an empty base `QTranslator`); language manager is initialized before building the settings
   page, and the settings panel supports re-translation.

### 4.2 Inference backend migration: LibTorch → ONNX Runtime
- Added `include/InferenceBackend.h` (abstract interface) + `src/engine/OnnxInference.{h,cpp}`.
- `YoloInference` refactored to ONNX (letterbox → inference → xyxy decode → NMS).
- `ExtensionManager` loads extension models via `OnnxInference` (classifier `[1,nc]` / detector
  `[1,4+nc,N]` / embedding `[1,D]`).
- `ModelRegistry` registers only `model.onnx`; CMake links `onnxruntime::onnxruntime`; all
  LibTorch deps removed.
- ONNX Runtime **1.29.0** deployed.

### 4.3 Base model switched to yolov8m-oiv7 (Open Images V7)
- `yolov8m-oiv7.pt` → `models/base/yolov8m-oiv7/model.onnx` (output `(1,605,8400)`, 601 classes).
- Old COCO-80 `yolov8m` and the `make_classes.py`/`make_ext_model.py`/`export_ext_onnx.py`
  helpers were removed.
- `classes.json` lists the 601 output classes in output-index order; parent chains come from the
  official Open Images hierarchy, all lowercased; parents (`fruit`/`food`/`animal`/`vehicle`…) are
  themselves output classes.
- `registry.json`: `active_base = "yolov8m-oiv7"`.
- Inference thresholds live in `config/settings.ini` `[inference]`: `base_conf_threshold` (0.25),
  `iou_threshold` (0.45), `fallback_threshold` (0, disables confidence fallback — OIV7 outputs
  parents directly). The engine reads only settings.ini (`config/config.json` removed).

### 4.4 DSL syntax fix (important)
Recommended syntax (the only form to use going forward):
```
filter images:  $ : (condition)
existential:    any(condition)    # the current image has an object satisfying the condition
universal:      all(condition)    # all objects in the current image satisfy the condition
```
- Parser: added `:` (Colon) token; `imgs : (condition)` → `FilterExpr`;
  `any(...)`/`all(...)` parse as `AnyAllExpr` (functional form).
- Evaluator: `evalFilter` (per-image condition, score>0 keeps), `evalAnyAll` (any=max, all=min).
- LLM prompts rewritten to the new format.
- The legacy quantifier syntax `$ any (cond)` is still backward compatible (identical results),
  but new code uses the new syntax.
- Test files: `dsl/test.dsl`, `test_spec.dsl` rewritten.

### 4.5 Incremental cache (replaces full rebuilds)
- Cache file: `cache/<model>/cache_index.json` (inline JSON: relative path → `{mtime, size,
  img_id, objects, img_attrs}`).
- On startup compare `mtime`/`size`: **only added/modified images are re-inferred**; deleted ones
  are pruned; if unchanged, the index is loaded directly.
- Legacy `metadata.json` auto-migrates (`CacheIndex::loadLegacyFromFile`); on corrupted index it
  migrates first, otherwise fully rebuilds.
- Modified images keep their `img_id`; object ids are globally unique and monotonic (shared
  `ObjectIdGenerator`).

### 4.6 Histogram macros (expose the 32-bin hue histogram to DSL)
| Macro | Description |
|-------|-------------|
| `obj_hist(obj)` | 32-bin histogram of the object region (bare class name → best object of that class; no object → all zero) |
| `img_hist()` | whole-image 32-bin histogram |
| `hist_sim(A, B)` | cosine similarity of two histograms (0–1) |
| `hist_value(obj, idx)` | bin `idx` of the object histogram (0–31, out of range is an error) |
| `img_hist_value(idx)` | bin `idx` of the whole-image histogram |

- New `HistVec = std::array<float,32>` type; `Value` gained `HIST_VEC`.
- Color↔bin reference: red 0,31 / orange 1,2 / yellow 3-5 / green 9-11 / cyan 14,15 / blue
  19-21 / purple 24-26 / pink 27-29.
- LLM prompts already include this.

### 4.7 Image quality / EXIF / user tags (ImageAttrs extension)
Computed per image during pre-processing (stored in `img_attrs` of `cache_index.json`):
- **Exposure**: 64-bin luma histogram `luma_hist`, `overexposure_score` (V>240 share),
  `underexposure_score` (V<30 share), `exposure_goodness = 1-(over+under)/2`.
- **Sharpness**: normalized Laplacian variance `global_blur_score`, `local_blur_score`
  (`raw/(raw+3000)`, 0–1, higher = sharper).
- **EXIF**: built-in lightweight JPEG EXIF parser (`src/utils/exif_reader.{h,cpp}`, no exiv2),
  reads Make/Model/ISO/shutter/aperture/focal length/date; empty/-1 when absent.
- **User tags**: `user_tags` (key→value), edited in the GUI detail panel, stored in the cache
  index.

DSL macros:
| Macro | Description |
|-------|-------------|
| `img_over()` / `img_under()` / `img_exp_good()` | over/under-exposure and exposure quality (0–1) |
| `img_hist_val(idx)` | luma histogram bin (0–63) |
| `img_blur()` / `img_blurry()` / `obj_blur(obj)` | global sharpness / 1−sharpness / object-region sharpness |
| `img_camera()` / `img_iso()` / `img_shutter()` / `img_aperture()` / `img_fl()` / `img_date()` | EXIF (empty/-1 when absent) |
| `img_tag(key)` / `img_has_tag(key)` / `img_tag_equals(k,v)` | user-tag queries |
| `stof(s)` / `str_contains(s, sub)` | string→number / containment (replaces the `contains` operator) |

GUI: double-click a result opens the image-detail dialog (metadata + tag editing, written back to
`cache_index.json`). Tag pre-filtering is done via the 🏷️ Tag Filter dialog in the search bar
(§4.8); the old "tag filter" dropdown is removed.

> Note: the task suggested exiv2; a built-in lightweight EXIF parser was used instead to avoid an
> external dependency. To switch to exiv2, just rewrite `readExifFromJpeg`. Cache version bumped
> to `1.1` (old caches are fully rebuilt).

### 4.8 Tag pre-filter & asset management
- **Tag pre-filter pipeline**: the GUI 🏷️ Tag Filter dialog sends the `(key, values)` condition
  set to the engine as `--tag-filter key=v1|v2` (AND across conditions, OR across values). Engine
  `CacheManager::applyTagFilters` computes the matching image set and `Context::setPrefilteredIds`
  makes `$` traverse only that set.
- **Persistence**: `MainWindow::saveTagFilters/loadSavedTagFilters` stores the conditions in
  `config/settings.ini` `[filter] tag_filters` (`key=v1|v2`). Conditions survive dialog reopen,
  DSL re-execution and app restart; the dialog pre-fills rows from the saved conditions.
- **Empty-match semantics**: `Context` gained `prefilter_active_` — an active filter matching 0
  images returns an **empty result** instead of falling back to the whole library (previously
  `getAllImagePaths` used a "non-empty" check, making the filter seem to disappear).
- **Empty-value semantics**: `--tag-filter key=` (no value) means "any value for this key";
  `main.cpp` no longer pushes empty strings into values.
- **Asset management**: GUI result grid `ExtendedSelection` multi-select + 🗑 Delete Selected
  (confirmation dialog, deletes files and calls `removeImagesFromCache`); DSL top-level
  `del <path|expression|variable>` (Parser adds the `Del` keyword and `DelStmt`,
  `Evaluator::evalDel` calls `Context::setDeleteImagesCallback` → `CacheManager::removeImages`).
  The GUI shows a confirmation dialog when it detects `del` in the DSL.
- **Dialog hardening**: all TagFilterDialog buttons have `setDefault(false)`/
  `setAutoDefault(false)` to avoid Enter triggering the default button (previously Enter deleted
  the current row / added an empty row).

### 4.9 Places365 scene recognition (SceneInference)
- **Model**: `models/scene/places365_googlenet.onnx` (**GoogLeNet**, converted from the official
  `googlenet_places365.caffemodel` + deploy prototxt) + `categories_places365.txt` (official 365
  lines). Alternate PyTorch ResNet18/50: export with `torch.onnx`. See `models/scene/README.md`.
- **Implementation**: `src/scene/SceneInference.{h,cpp}`, pure C++ + ONNX Runtime + GDI+ (reuses
  the existing image stack, no OpenCV). Preprocessing: resize 224×224 → RGB → `(x/255-mean)/std`
  (ImageNet stats) → inference → softmax(365). ~30 ms/image (CPU, GoogLeNet).
- **Cache**: `ImageAttrs` gained `scene_vector` (`std::array<float,365>`), `dominant_scene`,
  `indoor_score` (sum of the first 205 classes); `CacheIndex` version bumped to `1.2` (old caches
  rebuilt).
- **Integration**: `CacheManager::inferEntry` runs scene inference **after YOLO detection,
  independently** (same original image), lazy-loading the model; logs and degrades gracefully when
  missing (macros return 0.0 / "").
- **DSL macros**: `img_scene("name")` / `img_scene_top()` / `img_scene_vec()` / `img_is_indoor()`
  (`SceneFn` enum + `evalSceneMacro`). Class names are parsed from `/b/beach 48` → `beach`;
  case and `-`/`_`/space insensitive.
- **GUI**: image detail dialog shows `dominant_scene`, indoor probability and Top-5 scenes.

### 4.10 Clustering extension packs (V2)
- **Model**: `mobilefacenet.pt` (PyTorch, foamliu/MobileFaceNet) converted to
  `models/extensions/face_recognition_v1/model.onnx` — 128-dim output, input 112×112, ImageNet
  normalization, verified bit-comparable with the PyTorch output (max diff ~3e-6).
- **Config**: `capabilities.can_cluster` + `cluster_name`/`cluster_threshold`,
  `input_normalize`, `gui.{group_label,show_in_sidebar,icon}` (see
  [extension_pack_format.md](extension_pack_format.md)).
- **Pipeline**: `CacheManager::inferEntry` extracts L2-normalized embeddings for objects matching
  `parent_class`; after build, `runClustering` runs DBSCAN (minPts=1, cosine threshold) with
  deterministic `(path, obj_id)` ordering; cluster ids look like `face_cluster_person_001`,
  persisted in `cache_index.json` (`embeddings`/`cluster_ids`/`cluster_groups`).
- **DSL macros**: `cluster_id(obj, "face_cluster")`, `cluster_sim(a, b, "face_cluster")`
  (`ClusterFn` + `evalClusterMacro`).
- **GUI**: left sidebar shows a 👤 People branch with per-cluster items (mapped name or raw id +
  photo count); click shows the group's photos, double-click renames (persisted to
  `config/cluster_name_mappings.json`).

## 5. Build & Run

### Dependencies
- **Engine**: MSVC (VS2022 toolchain, tested 14.51), CMake ≥3.18 + Ninja,
  ONNX Runtime (`onnxruntime-win-x64-1.29.0`), nlohmann/json.
- **GUI**: Qt 6.10.1 (MinGW 1310); artifacts need `windeployqt` deployment of the Qt runtime.
- **Python** (model export only): PyTorch + onnx.

### Engine build
```powershell
# must run in a vcvars64 environment
call "D:\Visual Studio\VC\Auxiliary\Build\vcvars64.bat"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
      -DONNXRUNTIME_ROOT="D:\Visual Studio Data\Modules\onnxruntime-1.29.0\onnxruntime-win-x64-1.29.0" `
      -DNLOHMANN_INCLUDE_DIR="D:\projects\anaconda\Library\include"
cmake --build build
```

### GUI build
```powershell
cmake -S gui -B gui/build -G Ninja -DCMAKE_PREFIX_PATH=D:/Qt/6.10.1/mingw_64
cmake --build gui/build
# POST_BUILD copies dsl.exe + onnxruntime.dll and runs windeployqt
```

### Run
- Double-click `gui/build/tio.exe` (the engine is located via the settings `engine/path` or the
  default `dsl/build/dsl.exe`).
- CLI engine: `dsl\build\dsl.exe --list-models` / `dsl.exe test.dsl` /
  `dsl.exe --json --photo <gallery> <script.dsl>`.

## 6. Cache Mechanics
- First start (no index): full inference, write `cache_index.json`.
- Later starts: scan the gallery (stat each file), compare `mtime`/`size` with the index, infer
  only the differences.
- Status messages (stderr, visible in the GUI log panel):
  `[Cache] Incremental update: inferring N image(s) (M cached).` etc.
- Clearing `cache/<model>/` forces a full rebuild; the GUI settings "re-index" button deletes that
  directory.

## 7. DSL Cheat Sheet (modern syntax)
```
$ : (any(class == "cat"))                        # images with a cat
$ : (cnt(person) > 2)                            # images with >2 people
$ : (img_warmth() > 0.7 && any(class == "cat"))  # warm and has a cat
% $ : (any(class == "person"))                   # extract person objects
parts = people >> person_parts_v1                # extension refinement (needs the pack)
^ parts                                          # lift to images
$ : (any(hist_value(obj, 0) + hist_value(obj, 31) > 0.3))   # large red share
```
More in `docs/dsl_reference.md` (still documents the legacy syntax; use §4.4 for new code).

## 8. Known Issues & Caveats
1. **Extension packs**: only `face_recognition_v1` (a clustering pack) is bundled. `>>`
   refinement needs a detection/classification pack; `person_parts_v1`/`botany_v1` in examples
   are illustrative only. To add one:
   ```powershell
   # export an extension model to .onnx, then
   # hand-write models/extensions/<name>/config.json and add <name> to registry.json active_extensions
   ```
2. **Windows long paths**: if Python package installs fail with "file name or extension too long",
   enable `LongPathsEnabled` or install from an archive.
3. **PowerShell test files**: `Set-Content -Encoding UTF8` writes a BOM that pollutes the first
   DSL identifier (reports "undefined variable"). Use `-Encoding Ascii` or BOM-less UTF-8.
4. **LLM config**: `api_key` in `config/settings.ini` is a placeholder; you need a real key for
   "Translate to DSL". Otherwise type DSL manually in the editor and hit "Search".
5. **Model export**: the engine only accepts `.onnx`. Use `yolo export model=model.pt format=onnx
   imgsz=640`.
6. **Performance**: CPU inference ~0.5s/image; a full build over 129 images takes ~45s; an
   incremental start (no changes) is sub-second (loads only the index, not the ~100MB model).

## 9. Related Docs
- [README](README.md) — overview & quick start
- [Base model pack JSON format](base_model_pack_format.md)
- [Extension pack JSON format](extension_pack_format.md)
- [DSL language reference](dsl_reference.md) (legacy syntax included)
- [Usage tutorial](usage_tutorial.md)
- [Architecture](architecture.md)
