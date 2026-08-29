# 图像检索 DSL 解释器（Image Retrieval DSL Interpreter）

基于 C++17 的图片检索 DSL 解释器，内置 YOLOv8m 目标检测（ONNX Runtime / CPU）与智能增量缓存。
用户通过类自然语言的查询语句（例如 `$ : (any(class == "cat"))`）从图库中检索图片、提取对象、
执行继承查询与扩展包细化。

## 功能特性

- **DSL 查询语言**：筛选 `$ : (条件)`、对象级条件 `any()` / `all()`、对象提取 `%`、图片上溯 `^`、
  集合运算 `| & -`、算术/比较/逻辑运算、`cnt()` 继承计数、`>>` 扩展包细化、`del` 删除语句。
- **统一宏系统**：内置宏（数学 `max/min/abs/sqrt/pow/log/exp`、颜色 `color/cct/warmth/...`、
  几何 `big/left/square`、关系 `inside/left_of`、氛围 `warm/bright/smooth/rough`）与用户宏
  （`macro 名(参数)=表达式`）共用同一张宏表。
- **图像属性**：每张图在缓存预处理阶段计算 32 维色调直方图（`obj_hist`/`img_hist`/`hist_sim`）、
  曝光评分（`img_over`/`img_under`/`img_exp_good`）、清晰度（`img_blur`/`obj_blur`）、
  内置轻量 EXIF 解析（相机/ISO/快门/光圈/焦距/日期）与可编辑的用户标记 `user_tags`。
- **标签预筛选**：`--tag-filter key=v1|v2` 在求值前把 `$` 限制到标签匹配的图片（多条件 AND、
  值 OR）；匹配 0 张时返回空结果而非回退全库。GUI 通过「🏷️ 标签筛选」对话框配置并持久化。
- **资产管理**：`del <路径|表达式|变量>` 删除图片文件并同步更新缓存索引；GUI 支持多选删除。
- **智能缓存（增量更新）**：检测结果序列化为 `cache/<model>/cache_index.json`（版本 1.1）。
  每次启动按文件修改时间/大小对比图库，**只对新增/修改的图片重新推理**，删除的图片自动剔除；
  无变化时直接加载索引（秒级启动）。旧版 `metadata.json` 自动迁移。
- **模型注册表（Model Registry）**：通过 `models/registry.json` 动态切换基座 YOLO 模型，无需重新编译。
- **置信度降级**：低置信度检测框按 `classes.json` 中的继承关系自动归入父类（如 `apple -> fruit`）。
- **扩展包（Extension Pack）**：对检测到的对象进行区域裁剪并运行专用 ONNX 模型（检测器/分类器），
  实现"细粒度"二次分析（如 `person` → 身体部位）。
- **ONNX Runtime 推理后端**：基座模型与扩展模型统一由 `InferenceBackend` 抽象接口驱动（当前实现
  `OnnxInference`），CPU 推理，无 LibTorch/GPU 依赖。
- **多语言类别名**：类别名支持 UTF-8（含中文），例如 `cnt(fruit)`、`any flower`。

## 目录结构

```
tio/
├── photo/                        # 待检索图片（.jpg / .png）
└── dsl/                          # 项目根目录
    ├── CMakeLists.txt            # 构建脚本（MSVC + Ninja + ONNX Runtime）
    ├── cmake/                    # Findonnxruntime.cmake 等 CMake 模块
    ├── include/                  # 公共头文件（Types / InferenceBackend / ModelRegistry）
    ├── src/
    │   ├── main.cpp              # 入口：CLI 解析、--json 模式、REPL、/reload
    │   ├── parser/               # Lexer / Parser / AST（手写递归下降）
    │   ├── executor/             # Evaluator / Context / BuiltinMacros
    │   ├── cache/                # CacheManager(增量) + CacheIndex + YoloInference(ONNX)
    │   ├── engine/               # OnnxInference（ONNX Runtime 后端）
    │   └── utils/                # filesystem_utils / exif_reader
    ├── models/
    │   ├── registry.json         # 切换开关（active_base / active_extensions）
    │   ├── base/yolov8m/         # {model.onnx, meta.json, classes.json}
    │   └── extensions/           # 扩展包（可选，见 extension_pack_format.md）
    ├── config/
    │   ├── config.json           # 置信度阈值
    │   └── settings.ini          # GUI 设置（语言 / LLM / 图库 / 标签筛选）
    ├── cache/                    # 运行时自动生成（按模型分子目录）
    ├── docs/                     # 本文档
    └── export_*.py / make_*.py   # Python 模型与配置生成工具
```

## 环境依赖

| 依赖 | 说明 |
|------|------|
| C++ 编译器 | MSVC 19.5x+（Visual Studio 2022 工具链） |
| CMake ≥ 3.18 | 配合 Ninja 或 Visual Studio 生成器 |
| ONNX Runtime | 从 [onnxruntime releases](https://github.com/microsoft/onnxruntime/releases) 下载 `onnxruntime-win-x64-<ver>.zip`（CPU 版），解压后设置 `-DONNXRUNTIME_ROOT=<解压目录>` 或 `CMAKE_PREFIX_PATH` |
| nlohmann/json | 头文件库（`include/` 下需能找到 `nlohmann/json.hpp`，可用 `-DNLOHMANN_INCLUDE_DIR` 指定） |
| GDI+ / Windowscodecs | Windows 系统库，用于图片解码 |

## 构建

```powershell
# 进入项目目录
cd dsl

# 使用 Visual Studio 开发者命令行（含 vcvars64）后执行：
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
      -DONNXRUNTIME_ROOT="D:/path/to/onnxruntime-win-x64-1.29.0" `
      -DNLOHMANN_INCLUDE_DIR="D:/path/to/your-nlohmann-json-include"
cmake --build build

# onnxruntime.dll 由 POST_BUILD 自动复制到 build/ 下
```

## 模型导出（`.pt` → `.onnx`）

推理后端只加载 `.onnx` 模型。基座模型与扩展模型分别用以下命令导出：

```powershell
# 基座 YOLO（输出 (1, 4+nc, N)，框为 letterbox 后输入像素坐标）
python export_yolov8.py yolov8m.pt models/base/yolov8m/model.onnx
# 或官方 CLI：yolo export model=yolov8m.pt format=onnx opset=12 imgsz=640

# 扩展分类器/检测器
python make_ext_model.py 3 models/extensions/demo_v1/model.pt
python export_ext_onnx.py models/extensions/demo_v1/model.pt models/extensions/demo_v1/model.onnx 224
```

导出后把 ONNX 文件放到对应 `model.onnx`，把扩展包 `config.json` 的 `model_path` 改为指向 `.onnx`。
详见 [python_tools.md](python_tools.md)。

## 快速开始

```powershell
# 交互式 REPL（默认加载 models/registry.json 指定的基座模型）
build\dsl.exe

# 执行 DSL 脚本
build\dsl.exe query.dsl

# 查看注册的模型
build\dsl.exe --list-models

# 临时切换基座模型（优先于配置文件）
build\dsl.exe --base yolov8m

# --json 模式（GUI 使用）：DSL 从 stdin 读入，结果以 JSON 输出
build\dsl.exe --json --photo .\photo < query.dsl
```

REPL 示例（新语法：`$ : (条件)` 筛选，`any()`/`all()` 为对象级条件函数）：

```dsl
dsl> $ : (any(class == "person"))                       # 所有含人的图片
dsl> % $ : (any(class == "person"))                     # 所有 person 对象
dsl> $ : (cnt(fruit) > 2)                               # 水果（含 apple/banana 子类）超过 2 的图片
dsl> $ : (img_warmth() > 0.7 && any(class == "cat"))    # 暖色且含猫
dsl> $ : (any(hist_value(obj, 0) + hist_value(obj, 31) > 0.3))  # 红色占比高的图片
dsl> people = % $ : (any(class == "person"))
dsl> parts = people >> person_parts_v1                  # 对 person 做身体部位细化（需扩展包）
dsl> ^ parts                                            # 上溯回这些部位所在的图片
dsl> del people                                         # 删除 people 对应图片（文件 + 缓存）
dsl> /reload                                            # 热重载 models/registry.json（可切换模型）
```

详细内容请参阅：

- [DSL 语言参考](dsl_reference.md)
- [使用教程](usage_tutorial.md)
- [架构说明](architecture.md)
- [主模型包内部 JSON 格式](base_model_pack_format.md)
- [扩展包内部 JSON 格式](extension_pack_format.md)
- [Python 工具脚本说明](python_tools.md)
- [项目交接文档（团队接手指南）](handover.md)
