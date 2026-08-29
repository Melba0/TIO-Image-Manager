# tio — Image Retrieval DSL Tool / 图像检索 DSL 工具

> Natural-language image retrieval: **describe → DSL → ranked results**.
> 自然语言图片检索：**输入一句话 → 翻译成 DSL → 引擎模糊检索并排序返回**。

[![Language: C++17](https://img.shields.io/badge/language-C%2B%2B17-blue.svg)](https://isocpp.org/)
[![GUI: Qt 6](https://img.shields.io/badge/GUI-Qt%206-green.svg)](https://www.qt.io/)
[![Engine: ONNX Runtime](https://img.shields.io/badge/inference-ONNX%20Runtime-orange.svg)](https://github.com/microsoft/onnxruntime)
[![Platform: Windows](https://img.shields.io/badge/platform-Windows-0078d6.svg)](https://github.com/microsoft/onnxruntime)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPLv3-blue.svg)](LICENSE)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](https://github.com/)

A desktop image-retrieval tool made of two parts:

- **GUI** (`gui/`, Qt 6): a natural-language box in, a ranked thumbnail grid out. It calls an
  LLM to translate your sentence into DSL, shows results, and manages the library.
- **Engine** (`dsl/`, C++17): a hand-written DSL interpreter that parses/evaluates queries,
  runs YOLOv8m inference through **ONNX Runtime (CPU)**, and serves an incremental cache.

---

## 功能特性 / Features

| 特性 | Feature |
|------|---------|
| 自然语言检索（GUI + LLM 翻译） | Natural-language search: describe an image and the GUI translates it to DSL via an LLM |
| 手写递归下降 DSL 解析器 | Hand-written recursive-descent parser (no generator), UTF-8 identifiers support 中文类名 |
| YOLOv8m 目标检测（ONNX Runtime, CPU） | YOLOv8m object detection through ONNX Runtime (CPU-only, no GPU/LibTorch) |
| 新式筛选语法 `$ : (条件)` | Modern filter syntax `$ : (condition)` plus `any(...)` / `all(...)` object-level conditions |
| 集合与上溯：`%` / `^` / `\| & -` | Object extraction `%`, image lift `^`, set union/intersection/difference |
| 继承计数 `cnt(fruit)` | Inheritance-aware counting: `cnt(fruit)` also counts `apple`/`banana` subclasses |
| 置信度降级 | Confidence fallback: low-confidence detections are folded into their parent class |
| 增量缓存 | Incremental cache (`cache/<model>/cache_index.json`): only re-infers added/modified files |
| 图像属性：曝光 / 清晰度 / EXIF / 用户标记 | Image attrs: exposure, blur, lightweight built-in EXIF reader, editable user tags |
| 直方图宏 | 32-bin hue histograms: `obj_hist` / `img_hist` / `hist_sim` / `hist_value` |
| 标签预筛选（GUI 对话框 + `--tag-filter`） | Tag pre-filter pipeline: restrict `$` to images matching key-value tags (persisted) |
| 资产管理（多选删除 / DSL `del`） | Asset management: multi-select delete in the grid, `del` statement in DSL |
| 扩展包 `>>` | Extension packs: crop detected objects and run a fine-grained ONNX model |
| 统一宏系统 | Unified macro table: math/geometry/relationship/atmosphere macros + user macros |
| 中英文 GUI + 深浅主题 | Bilingual GUI (中文/EN) with dark & light themes |

---

## 项目简介 / Overview

**tio** is a semantic image-retrieval tool. Instead of keyword matching, you describe what you
want — *“a cat to the left of a dog”* — and the engine ranks images by a fuzzy DSL evaluation.

**tio** 是一款语义图片检索工具。你不需要输入关键词，而是用一句话描述想要的画面
（例如"一只猫在狗左边"），引擎会基于 DSL 模糊求值对图库图片进行排序。

```
一句话描述 ──► tio.exe (Qt GUI) ──LLM──► DSL 代码 ──QProcess──► dsl.exe (C++ 引擎)
                                                                    │
                                    ┌───────────────────────────────┤
                                    ▼                               ▼
                    cache/<model>/cache_index.json     models/base/yolov8m/model.onnx
                    (增量缓存 / incremental cache)      + models/registry.json
```

- The GUI talks to the engine through `QProcess` in `--json` mode: DSL goes in on **stdin**,
  results come back as **JSON on stdout**.
- The engine loads/incrementally updates its cache on startup, then evaluates the DSL.

---

## 目录结构 / Repository Layout

```
tio/
├── photo/                  # 图库图片 .jpg/.png（示例 COCO 图片）
│                           # sample gallery (add your own images)
├── dsl/                    # 引擎 / the C++ engine
│   ├── CMakeLists.txt      # MSVC + Ninja + ONNX Runtime
│   ├── cmake/              # Findonnxruntime.cmake
│   ├── include/            # Types / InferenceBackend / ModelRegistry ...
│   ├── src/
│   │   ├── main.cpp        # 入口 + CLI + --json 模式 + REPL
│   │   ├── parser/         # Lexer / Parser / AST（手写递归下降）
│   │   ├── executor/       # Evaluator / Context / BuiltinMacros
│   │   ├── cache/          # CacheManager（增量）+ CacheIndex + YoloInference(ONNX)
│   │   ├── engine/         # OnnxInference（ONNX Runtime 后端）
│   │   └── utils/          # filesystem_utils / exif_reader
│   ├── models/
│   │   ├── registry.json   # active_base / active_extensions
│   │   └── base/yolov8m/   # {model.onnx, meta.json, classes.json}
│   ├── cache/              # 运行时生成 / generated at runtime
│   ├── config/             # config.json（阈值）+ settings.ini（GUI 设置，勿提交）
│   ├── docs/               # 中文文档 / Chinese docs
│   └── export_*.py ...     # 模型导出 / Python model-export tools
└── gui/                    # Qt 6 桌面端 / the Qt GUI
    └── build/              # 产物 tio.exe + 伴生 dsl.exe（POST_BUILD 自动部署）
```

---

## 环境依赖 / Requirements

| 依赖 | 说明 | Requirement |
|------|------|-------------|
| C++17 编译器 | MSVC 19.5x+（Visual Studio 2022） | Windows toolchain |
| CMake ≥ 3.18 | 配合 Ninja | with Ninja or VS generator |
| ONNX Runtime | CPU 版 `onnxruntime-win-x64-<ver>.zip` | [onnxruntime releases](https://github.com/microsoft/onnxruntime/releases) |
| nlohmann/json | 头文件库 | header-only; set `-DNLOHMANN_INCLUDE_DIR` |
| GDI+ / Windowscodecs | Windows 系统库，图片解码 | system libs (image decoding) |
| Qt 6（GUI） | 6.10.x MinGW，`windeployqt` 部署 | [Qt](https://www.qt.io/download-open-source) |
| Python 3.10+（可选） | 仅用于模型导出 `.pt → .onnx` | only for model export |

---

## 构建 / Build

### 引擎 / Engine

```powershell
# 在 vcvars64 开发者环境中执行 / from a vcvars64 developer prompt:
cd dsl
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
      -DONNXRUNTIME_ROOT="D:/path/to/onnxruntime-win-x64-1.29.0" `
      -DNLOHMANN_INCLUDE_DIR="D:/path/to/your-nlohmann-json-include"
cmake --build build
# onnxruntime.dll 由 POST_BUILD 自动复制到 build/ 下
# (onnxruntime.dll is copied next to the exe by POST_BUILD)
```

### GUI（Qt 桌面端 / Desktop client）

```powershell
cmake -S gui -B gui/build -G Ninja -DCMAKE_PREFIX_PATH=D:/Qt/6.10.1/mingw_64
cmake --build gui/build
# POST_BUILD 自动完成：复制引擎 dsl.exe + onnxruntime.dll、windeployqt 部署 Qt 运行时
# (POST_BUILD copies dsl.exe + onnxruntime.dll and runs windeployqt)
```

运行 / run: double-click `gui/build/tio.exe`.

---

## 模型导出（.pt → .onnx）/ Model Export

The engine only loads `.onnx` models. Export a YOLOv8 checkpoint with the bundled tool
(or the official `yolo export` CLI):

```powershell
python export_yolov8.py yolov8m.pt models/base/yolov8m/model.onnx
# 等价 / equivalent:  yolo export model=yolov8m.pt format=onnx opset=12 imgsz=640
```

A registered base model needs `model.onnx` + `meta.json` + `classes.json` under
`models/base/<name>/` (see [python_tools.md](dsl/docs/python_tools.md) and
[base_model_pack_format.md](dsl/docs/base_model_pack_format.md)).

---

## 快速开始 / Quick Start

### 命令行 / CLI

```powershell
build\dsl.exe                      # 交互式 REPL / interactive REPL
build\dsl.exe --list-models        # 查看注册的模型 / list registered models
build\dsl.exe --base yolov8m       # 临时切换基座模型 / override the active base
build\dsl.exe --json --photo .\photo --tag-filter "city=sh" < query.dsl
```

### REPL 示例（新语法）/ REPL examples (modern syntax)

```dsl
dsl> $ : (any(class == "person"))              # 所有含人的图片 / images with a person
dsl> % $ : (any(class == "person"))            # 提取所有 person 对象 / extract person objects
dsl> $ : (cnt(fruit) > 2)                      # 水果（含子类）> 2 的图片 / >2 fruit (incl. subclasses)
dsl> $ : (img_warmth() > 0.7 && any(class == "cat"))   # 暖色且含猫 / warm and has a cat
dsl> people = % $ : (any(class == "person"))
dsl> parts = people >> person_parts_v1         # 扩展细化 / fine-grained refinement
dsl> ^ parts                                   # 上溯到图片 / lift back to images
dsl> /reload                                   # 热重载 registry.json / hot-reload config
```

> 旧量词语法 `$ any (cond)` 仍向后兼容；新代码一律推荐 `$ : (cond)` + `any()`/`all()`。

---

## 标签预筛选与资产管理 / Tag Pre-Filter & Asset Management

**Tag pre-filter** restricts the whole query to images whose `user_tags` match all given
key→value conditions (values are OR-ed). In the GUI, press **🏷️ 标签筛选** to build the
conditions in a dialog; the filter is persisted across restarts.

```powershell
# 命令行等价 / CLI equivalent:
dsl.exe --json --photo .\photo --tag-filter "city=sh|bj" --tag-filter "level=3"
dsl.exe --json --photo .\photo --tag-filter "location="    # 任意值的 location / any value
```

An active filter matching nothing yields **zero** results (it never silently falls back to
the whole library).

**Asset management**:
- In the grid, multi-select thumbnails (Ctrl/Shift) and press **🗑 删除选中** to delete files
  + remove their cache entries.
- In DSL, `del <path>` / `del <image-set expression>` / `del <variable>` deletes images.

---

## DSL 速查 / DSL Cheat Sheet

| 语法 | 含义 | Meaning |
|------|------|---------|
| `$` | 全量图库 / the whole library | all images (or current image inside a quantifier) |
| `$ : (cond)` | 筛选满足条件的图片 | filter images where `cond` holds |
| `any(cond)` / `all(cond)` | 存在 / 全部满足 | existential / universal object condition |
| `%` | 提取对象 | ImageSet → ObjectSet |
| `^` | 上溯图片 | ObjectSet → ImageSet (dedup) |
| `\| & -` | 并 / 交 / 差集 | union / intersection / difference |
| `>> pack` | 扩展细化 | run an extension model on matching objects |
| `cnt(cls)` | 继承计数 | count objects of `cls` incl. subclasses |
| `macro f(x)=expr` | 定义宏 | define a user macro |

Object properties: `class` `x` `y` `w` `h` `area` `confidence` `super_class` `original_class`.
Attribute macros: `big` `small` `left` `right` `top` `bottom` `square`, `left_of` `above` `inside`,
`warm` `cool` `bright` `dark` `smooth` `rough`; math `max/min/abs/sqrt/pow/log/exp`.

Image macros: `img_warmth()` `img_bright()` `img_color()` `img_blur()` `img_over()` `img_under()`
`img_exp_good()` `img_camera()` `img_iso()` `img_shutter()` `img_aperture()` `img_fl()` `img_date()`
`img_tag(k)` `img_has_tag(k)` `img_tag_equals(k,v)` `obj_hist(o)` `img_hist()` `hist_sim(A,B)`
`hist_value(o,i)` `img_hist_value(i)` `img_hist_val(i)` `stof(s)` `str_contains(s,sub)`.

---

## 文档 / Documentation

Primary documentation is in Chinese under [`dsl/docs/`](dsl/docs/):

| 文档 | 说明 |
|------|------|
| [dsl_reference.md](dsl/docs/dsl_reference.md) | DSL 语言完整参考 / full language reference |
| [usage_tutorial.md](dsl/docs/usage_tutorial.md) | 使用教程 / usage tutorial |
| [architecture.md](dsl/docs/architecture.md) | 架构说明 / architecture |
| [base_model_pack_format.md](dsl/docs/base_model_pack_format.md) | 基座模型包格式 / base model pack format |
| [extension_pack_format.md](dsl/docs/extension_pack_format.md) | 扩展包格式 / extension pack format |
| [python_tools.md](dsl/docs/python_tools.md) | Python 模型导出工具 / model-export tools |

---

## 路线图 / Roadmap

- [ ] Extension-pack demos + docs for `>>` refinement
- [ ] Linux / macOS engine builds (ONNX Runtime is cross-platform)
- [ ] Open-vocabulary base models (YOLO-World style, dynamic classes)
- [ ] GPU inference option

---

## 许可证 / License

This project is licensed under the **GNU General Public License v3.0** — see [LICENSE](LICENSE).
本项目采用 **GPL-3.0** 协议发布，详见 [LICENSE](LICENSE)。
