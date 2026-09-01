# tio — 图像检索 DSL 工具

[**中文**](README_zh.md) ・ [**English**](README.md)

> 自然语言图片检索：**输入一句话 → 翻译成 DSL → 引擎模糊检索并排序返回**。

[![Language: C++17](https://img.shields.io/badge/language-C%2B%2B17-blue.svg)](https://isocpp.org/)
[![GUI: Qt 6](https://img.shields.io/badge/GUI-Qt%206-green.svg)](https://www.qt.io/)
[![Engine: ONNX Runtime](https://img.shields.io/badge/inference-ONNX%20Runtime-orange.svg)](https://github.com/microsoft/onnxruntime)
[![Platform: Windows](https://img.shields.io/badge/platform-Windows-0078d6.svg)](https://github.com/microsoft/onnxruntime)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPLv3-blue.svg)](LICENSE)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg)](https://github.com/)

一款由两部分构成的桌面图片检索工具：

- **GUI**（`gui/`，Qt 6）：输入自然语言，输出按相关度排序的缩略图网格。调用 LLM 把句子
  翻译成 DSL、展示结果并管理图库。
- **引擎**（`dsl/`，C++17）：手写 DSL 解释器，负责解析/求值查询，通过 **ONNX Runtime (CPU)**
  运行 YOLOv8m 推理，并提供增量缓存。

---

## 截图

### 主窗口

![主窗口](docs/images/main_window.png)

### 自然语言搜索

![自然语言搜索](docs/images/nl_search.png)

### 排序结果网格

![排序结果](docs/images/results_grid.png)

### 图片详情对话框

![图片详情对话框](docs/images/detail_dialog.png)

### 标签预筛选对话框

![标签预筛选对话框](docs/images/tag_filter.png)

### 设置页

![设置页](docs/images/settings.png)

### REPL / 命令行

![REPL / CLI](docs/images/repl.png)

---

## 功能特性

| 特性 | 说明 |
|------|------|
| 自然语言检索 | 描述一张图片，GUI 通过 LLM 将其翻译为 DSL |
| 手写递归下降 DSL 解析器 | 无生成器；UTF-8 标识符支持中文类名 |
| YOLOv8m（Open Images V7，601 类）检测 | ONNX Runtime（仅 CPU），含 `fruit`/`food`/`animal` 父类 |
| 新式筛选语法 `$ : (条件)` | 以及 `any(...)` / `all(...)` 对象级条件 |
| 集合与上溯：`%` / `^` / `\| & -` | 对象提取、图片上溯、并/交/差集 |
| 继承计数 `cnt(fruit)` | 同时统计 `apple`/`banana` 子类 |
| 可调推理阈值 | base-conf / IoU / fallback，GUI 设置页可调 |
| 增量缓存 | `cache/<model>/cache_index.json`：只对新增/修改的图片重新推理 |
| 图像属性 | 曝光、清晰度、内置轻量 EXIF 读取、可编辑用户标记 |
| Places365 场景识别 | 纯 C++/ONNX，CPU；通过 `img_scene("beach")` / `img_scene_top()` / `img_is_indoor()` 查询 |
| 直方图宏 | 32 维色调直方图：`obj_hist` / `img_hist` / `hist_sim` / `hist_value` |
| 标签预筛选（GUI 对话框 + `--tag-filter`） | 把 `$` 限制到匹配 key→value 标签的图片（持久化） |
| 资产管理 | 网格多选删除、DSL `del` 语句 |
| 扩展包 `>>` | 裁剪检测对象并运行细粒度 ONNX 模型 |
| 聚类扩展包（V2） | 嵌入模型自动对对象聚类（DBSCAN）；侧栏人物视图 + 重命名 |
| 统一宏系统 | 数学/几何/关系/氛围宏 + 用户宏 |
| 中英文 GUI + 深浅主题 | 中文/EN，支持暗色与亮色主题 |

---

## 项目简介

**tio** 是一款语义图片检索工具。你不需要输入关键词，而是用一句话描述想要的画面
（例如"一只猫在狗左边"），引擎会基于 DSL 模糊求值对图库图片进行排序。

```
一句话描述 ──► tio.exe (Qt GUI) ──LLM──► DSL 代码 ──QProcess──► dsl.exe (C++ 引擎)
                                                                    │
                                    ┌───────────────────────────────┤
                                    ▼                               ▼
                    cache/<model>/cache_index.json     models/base/yolov8m-oiv7/model.onnx
                    (增量缓存 / incremental cache)      + models/registry.json
```

- GUI 通过 `QProcess` 以 `--json` 模式与引擎通信：DSL 从 **stdin** 输入，结果以 **JSON** 从 stdout 返回。
- 引擎启动时加载/增量更新缓存，然后求值 DSL。

![架构图](docs/images/architecture.png)

> **截图内容：** 上述数据流示意图：用户 → GUI → LLM → DSL → 引擎 → 缓存（`cache_index.json`）
> 与模型（ONNX + registry）。

---

## 目录结构

```
tio/
├── photo/                  # 图库图片 .jpg/.png（示例图库，可自行添加）
├── dsl/                    # 引擎 / the C++ engine
│   ├── CMakeLists.txt      # MSVC + Ninja + ONNX Runtime
│   ├── cmake/              # Findonnxruntime.cmake
│   ├── include/            # Types / InferenceBackend / ModelRegistry ...
│   ├── src/
│   │   ├── main.cpp        # 入口 + CLI + --json 模式 + REPL
│   │   ├── parser/         # Lexer / Parser / AST（手写递归下降）
│   │   ├── executor/       # Evaluator / Context / BuiltinMacros
│   │   ├── cache/          # CacheManager（增量）+ CacheIndex + YoloInference(ONNX)
│   │   ├── scene/          # SceneInference（Places365 场景识别，ONNX）
│   │   ├── cluster/        # Clustering（基于 embedding 的 DBSCAN）
│   │   ├── engine/         # OnnxInference（ONNX Runtime 后端）
│   │   └── utils/          # filesystem_utils / exif_reader
│   ├── models/
│   │   ├── registry.json   # active_base / active_extensions
│   │   ├── base/yolov8m-oiv7/   # {model.onnx, meta.json, classes.json}
│   │   ├── extensions/          # 扩展包（见 extension_pack_format）
│   │   └── scene/          # Places365（.onnx + categories + meta.json）
│   ├── cache/              # 运行时生成
│   ├── config/             # settings.ini（GUI 设置 + 推理阈值）
│   ├── docs/               # 文档（EN / 中文）
│   └── gui/                # Qt 6 桌面端
        └── build/          # tio.exe + 伴生 dsl.exe（POST_BUILD 自动部署）
```

---

## 环境依赖

| 依赖 | 说明 |
|------|------|
| C++17 编译器 | MSVC 19.5x+（Visual Studio 2022） |
| CMake ≥ 3.18 | 配合 Ninja 或 VS 生成器 |
| ONNX Runtime | CPU 版 `onnxruntime-win-x64-<ver>.zip`，见 [onnxruntime releases](https://github.com/microsoft/onnxruntime/releases) |
| nlohmann/json | 头文件库；设置 `-DNLOHMANN_INCLUDE_DIR` |
| GDI+ / Windowscodecs | Windows 系统库（图片解码） |
| Qt 6（GUI） | 6.10.x MinGW，`windeployqt` 部署 |
| Python 3.10+（可选） | 仅用于模型导出 `.pt → .onnx` |

---

## 构建

### 引擎

```powershell
# 在 vcvars64 开发者环境中执行：
cd dsl
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
      -DONNXRUNTIME_ROOT="D:/path/to/onnxruntime-win-x64-1.29.0" `
      -DNLOHMANN_INCLUDE_DIR="D:/path/to/your-nlohmann-json-include"
cmake --build build
# onnxruntime.dll 由 POST_BUILD 自动复制到 build/ 下
```

### GUI（Qt 桌面端）

```powershell
cmake -S gui -B gui/build -G Ninja -DCMAKE_PREFIX_PATH=D:/Qt/6.10.1/mingw_64
cmake --build gui/build
# POST_BUILD 自动完成：复制 dsl.exe + onnxruntime.dll、运行 windeployqt
```

运行：双击 `gui/build/tio.exe`。

![主窗口](docs/images/main_window.png)

> **截图内容：** 从 `gui/build/tio.exe` 启动后的应用窗口。

---

## 模型导出（.pt → .onnx）

引擎只加载 `.onnx` 模型。用官方 `yolo export` CLI 导出 YOLOv8 检查点：

```powershell
yolo export model=yolov8m-oiv7.pt format=onnx opset=12 imgsz=640
```

注册的基座模型需要在 `models/base/<名字>/` 下包含 `model.onnx` + `meta.json` +
`classes.json`（见 [base_model_pack_format.md](dsl/docs/base_model_pack_format_zh.md)）。

---

## 快速开始

### 命令行

```powershell
build\dsl.exe                      # 交互式 REPL
build\dsl.exe --list-models        # 查看注册的模型
build\dsl.exe --base yolov8m-oiv7  # 临时切换基座模型
build\dsl.exe --json --photo .\photo --tag-filter "city=sh" < query.dsl
```

### REPL 示例（新语法）

```dsl
dsl> $ : (any(class == "person"))              # 所有含人的图片
dsl> % $ : (any(class == "person"))            # 提取 person 对象
dsl> $ : (cnt(fruit) > 2)                      # 水果（含子类）> 2
dsl> $ : (img_warmth() > 0.7 && any(class == "cat"))   # 暖色且含猫
dsl> people = % $ : (any(class == "person"))
dsl> parts = people >> person_parts_v1         # 扩展细化
dsl> ^ parts                                   # 上溯到图片
dsl> /reload                                   # 热重载 registry.json
```

> 旧量词语法 `$ any (cond)` 仍向后兼容；新代码一律使用 `$ : (cond)` + `any()`/`all()`。

![REPL / CLI](docs/images/repl.png)

> **截图内容：** 运行上述示例查询的 REPL 会话。

---

## 标签预筛选与资产管理

**标签预筛选** 把整个查询限制到 `user_tags` 匹配全部给定 key→value 条件（值之间 OR）的图片。
GUI 中点击 **🏷️ 标签筛选** 在对话框中构建条件；筛选跨重启持久化。

```powershell
# 命令行等价：
dsl.exe --json --photo .\photo --tag-filter "city=sh|bj" --tag-filter "level=3"
dsl.exe --json --photo .\photo --tag-filter "location="    # location 任意值
```

激活的筛选匹配 0 张时返回**空结果**（不会悄悄回退到全库）。

**资产管理**：
- 在网格中多选缩略图（Ctrl/Shift）后按 **🗑 删除选中**，删除文件并同步清理缓存条目。
- DSL 中 `del <路径>` / `del <图片集合表达式>` / `del <变量>` 删除图片。

![标签预筛选对话框](docs/images/tag_filter.png)

> **截图内容：** 标签筛选对话框，含 `city=sh`、`level=3` 等条件。

---

## DSL 速查

| 语法 | 含义 |
|------|------|
| `$` | 全量图库（量词内部为"当前图片"） |
| `$ : (cond)` | 筛选满足条件的图片 |
| `any(cond)` / `all(cond)` | 存在 / 全部满足 |
| `%` | 提取对象：ImageSet → ObjectSet |
| `^` | 上溯图片：ObjectSet → ImageSet（去重） |
| `\| & -` | 并 / 交 / 差集 |
| `>> pack` | 对匹配对象运行扩展模型 |
| `cnt(cls)` | 继承计数（含子类） |
| `macro f(x)=expr` | 定义用户宏 |
| `collection("名称")` | 虚拟相簿（GUI 管理）作为 ImageSet |
| `cluster_id(obj, "c")` / `cluster_sim(a, b, "c")` | 聚类宏（V2） |

对象属性：`class` `x` `y` `w` `h` `area` `confidence` `super_class` `original_class`。
属性宏：`big` `small` `left` `right` `top` `bottom` `square`、`left_of` `above` `inside`、
`warm` `cool` `bright` `dark` `smooth` `rough`；数学 `max/min/abs/sqrt/pow/log/exp`。

图片宏：`img_warmth()` `img_bright()` `img_color()` `img_blur()` `img_over()` `img_under()`
`img_exp_good()` `img_camera()` `img_iso()` `img_shutter()` `img_aperture()` `img_fl()`
`img_date()` `img_tag(k)` `img_has_tag(k)` `img_tag_equals(k,v)` `img_scene(name)`
`img_scene_top()` `img_is_indoor()` `obj_hist(o)` `img_hist()` `hist_sim(A,B)` `hist_value(o,i)`
`img_hist_value(i)` `stof(s)` `str_contains(s,sub)`。

---

## 文档

文档为中英双语，每页开头都有语言切换链接。

| 文档 | 说明 |
|------|------|
| [dsl_reference.md](dsl/docs/dsl_reference_zh.md) | DSL 语言完整参考 |
| [usage_tutorial.md](dsl/docs/usage_tutorial_zh.md) | 使用教程 |
| [architecture.md](dsl/docs/architecture_zh.md) | 架构说明 |
| [base_model_pack_format.md](dsl/docs/base_model_pack_format_zh.md) | 基座模型包格式 |
| [extension_pack_format.md](dsl/docs/extension_pack_format_zh.md) | 扩展包格式（含聚类 V2） |
| [handover.md](dsl/docs/handover_zh.md) | 项目交接 / 已知问题 |

---

## 路线图

- [ ] 扩展包演示与 `>>` 细化文档
- [ ] Linux / macOS 引擎构建（ONNX Runtime 跨平台）
- [ ] 开放词汇基座模型（YOLO-World 风格，动态类别）
- [ ] GPU 推理选项

---

## 许可证

本项目采用 **GPL-3.0** 协议发布，详见 [LICENSE](LICENSE)。
