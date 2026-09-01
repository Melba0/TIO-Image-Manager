# Image Retrieval DSL Interpreter

[**English**](README.md) ・ [**中文**](README_zh.md)

A C++17 image-retrieval DSL interpreter with built-in YOLOv8m (Open Images V7, 601 classes)
object detection (ONNX Runtime / CPU) and a smart incremental cache. Users retrieve images from
the library, extract objects, run inheritance-aware counts and extension-pack refinement through
natural-language-like queries such as `$ : (any(class == "cat"))`.

## Features

- **DSL query language**: filter `$ : (condition)`, object-level conditions `any()` / `all()`,
  object extraction `%`, image lift `^`, set operations `| & -`, arithmetic/comparison/logic,
  inheritance-aware count `cnt()`, extension-pack refinement `>>`, `del` delete statements.
- **Unified macro system**: built-in macros (math `max/min/abs/sqrt/pow/log/exp`, color
  `color/cct/warmth/...`, geometry `big/left/square`, relationships `inside/left_of`, atmosphere
  `warm/bright/smooth/rough`) and user macros (`macro name(args) = expr`) share one macro table.
- **Image attributes**: every image gets a 32-bin hue histogram (`obj_hist`/`img_hist`/
  `hist_sim`), exposure scores (`img_over`/`img_under`/`img_exp_good`), sharpness
  (`img_blur`/`obj_blur`), a lightweight built-in EXIF reader (camera/ISO/shutter/aperture/
  focal length/date) and editable user tags `user_tags` during the cache pre-processing phase.
- **Scene recognition (Places365)**: a 365-dim scene probability vector is computed per image
  during pre-processing with Places365 (ONNX Runtime / CPU, pure C++); DSL macros
  `img_scene("beach")` / `img_scene_top()` / `img_is_indoor()` search by scene (graceful
  degradation when the model is missing).
- **Clustering (V2)**: embedding extension packs (e.g. face recognition) automatically extract
  an embedding per matching object and run DBSCAN clustering during cache build; results
  (`cluster_ids` + per-image `cluster_groups`) persist into `cache_index.json` and drive the
  GUI's grouped sidebar view (`cluster_id()` / `cluster_sim()` DSL macros).
- **Tag pre-filter**: `--tag-filter key=v1|v2` restricts `$` to matching images before
  evaluation (AND across conditions, OR across values); matching 0 images yields an empty result
  instead of silently falling back to the whole library. The GUI configures it via the
  🏷️ Tag Filter dialog and persists it.
- **Asset management**: `del <path|expression|variable>` deletes image files and updates the
  cache index; the GUI supports multi-select deletion.
- **Smart cache (incremental update)**: detection results are serialized to
  `cache/<model>/cache_index.json` (version 1.2). On startup the library is compared by file
  mtime/size and **only added/modified images are re-inferred**; deleted images are pruned; if
  nothing changed the index is loaded directly (second-level startup). Legacy `metadata.json`
  is auto-migrated.
- **Model Registry**: switch base YOLO models dynamically via `models/registry.json` without
  recompiling.
- **Inheritance queries (is-a)**: `classes.json` defines the full inheritance chain;
  `cnt(fruit)` counts `fruit` and its subclasses (`apple`/`banana`…). The OIV7 parent classes
  (`fruit`/`food`/`animal`/`vehicle`…) are themselves output classes.
- **Configurable inference thresholds**: `base_conf_threshold` / `iou_threshold` /
  `fallback_threshold` live in the `[inference]` section of `config/settings.ini` and can be
  adjusted in the GUI settings page.
- **Extension packs**: crop detected object regions and run a dedicated ONNX model
  (detector/classifier/embedding) for fine-grained secondary analysis (e.g.
  `person` → body parts).
- **ONNX Runtime inference backend**: base and extension models are driven by the
  `InferenceBackend` abstraction (current implementation `OnnxInference`), CPU inference, no
  LibTorch/GPU dependency.
- **Multilingual class names**: class names support UTF-8 (including Chinese), e.g.
  `cnt(fruit)`, `any flower`.

## Directory Layout

```
tio/
├── photo/                        # images to search (.jpg / .png)
└── dsl/                          # project root
    ├── CMakeLists.txt            # build script (MSVC + Ninja + ONNX Runtime)
    ├── cmake/                    # Findonnxruntime.cmake and other CMake modules
    ├── include/                  # public headers (Types / InferenceBackend / ModelRegistry)
    ├── src/
    │   ├── main.cpp              # entry: CLI parsing, --json mode, REPL, /reload
    │   ├── parser/               # Lexer / Parser / AST (hand-written recursive descent)
    │   ├── executor/             # Evaluator / Context / BuiltinMacros
    │   ├── cache/                # CacheManager (incremental) + CacheIndex + YoloInference (ONNX)
    │   ├── scene/                # SceneInference (Places365, ONNX)
    │   ├── cluster/              # Clustering (DBSCAN over embeddings)
    │   ├── engine/               # OnnxInference (ONNX Runtime backend)
    │   └── utils/                # filesystem_utils / exif_reader
    ├── models/
    │   ├── registry.json         # switches (active_base / active_extensions)
    │   ├── base/yolov8m-oiv7/    # {model.onnx, meta.json, classes.json}
    │   ├── extensions/           # extension packs (see extension_pack_format.md)
    │   └── scene/                # Places365 (.onnx + categories + meta.json)
    ├── config/
    │   └── settings.ini          # GUI settings + [inference] thresholds
    ├── cache/                    # generated at runtime (subdir per model)
    └── docs/                     # this documentation
```

## Requirements

| Dependency | Notes |
|------------|-------|
| C++ compiler | MSVC 19.5x+ (Visual Studio 2022 toolchain) |
| CMake ≥ 3.18 | with Ninja or the Visual Studio generator |
| ONNX Runtime | download `onnxruntime-win-x64-<ver>.zip` (CPU) from [onnxruntime releases](https://github.com/microsoft/onnxruntime/releases), extract, then pass `-DONNXRUNTIME_ROOT=<extracted>` or `CMAKE_PREFIX_PATH` |
| nlohmann/json | header-only (must find `nlohmann/json.hpp` under `include/`; use `-DNLOHMANN_INCLUDE_DIR`) |
| GDI+ / Windowscodecs | Windows system libraries, used for image decoding |

## Build

```powershell
# from a Visual Studio developer prompt (vcvars64):
cd dsl

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
      -DONNXRUNTIME_ROOT="D:/path/to/onnxruntime-win-x64-1.29.0" `
      -DNLOHMANN_INCLUDE_DIR="D:/path/to/your-nlohmann-json-include"
cmake --build build

# onnxruntime.dll is copied next to the exe by POST_BUILD
```

## Model Export (`.pt` → `.onnx`)

The inference backend only loads `.onnx` models:

```powershell
# base YOLO (output (1, 4+nc, N), boxes in letterboxed input-pixel coordinates)
yolo export model=yolov8m-oiv7.pt format=onnx opset=12 imgsz=640
```

After export, place the ONNX file as `model.onnx` and maintain `meta.json` and `classes.json`
(see [base_model_pack_format.md](base_model_pack_format.md)).

## Quick Start

```powershell
# interactive REPL (loads the base model from models/registry.json by default)
build\dsl.exe

# run a DSL script
build\dsl.exe query.dsl

# list registered models
build\dsl.exe --list-models

# temporarily override the base model (takes precedence over the config file)
build\dsl.exe --base yolov8m-oiv7

# --json mode (used by the GUI): DSL is read from stdin, results printed as JSON
build\dsl.exe --json --photo .\photo < query.dsl
```

REPL examples (modern syntax: `$ : (condition)` filter, `any()`/`all()` object-level conditions):

```dsl
dsl> $ : (any(class == "person"))                       # all images with a person
dsl> % $ : (any(class == "person"))                     # all person objects
dsl> $ : (cnt(fruit) > 2)                               # images with >2 fruit (incl. apple/banana subclasses)
dsl> $ : (img_warmth() > 0.7 && any(class == "cat"))    # warm and has a cat
dsl> $ : (any(hist_value(obj, 0) + hist_value(obj, 31) > 0.3))  # images with a large red share
dsl> people = % $ : (any(class == "person"))
dsl> parts = people >> person_parts_v1                  # body-part refinement (needs the pack)
dsl> ^ parts                                            # lift back to the containing images
dsl> del people                                         # delete the images of people (file + cache)
dsl> /reload                                            # hot-reload models/registry.json
```

See also:

- [DSL language reference](dsl_reference.md)
- [Usage tutorial](usage_tutorial.md)
- [Architecture](architecture.md)
- [Base model pack JSON format](base_model_pack_format.md)
- [Extension pack JSON format](extension_pack_format.md)
- [Project handover (team guide)](handover.md)
