# 项目交接文档（团队接手指南）

[**中文**](handover_zh.md) ・ [**English**](handover.md)

> 本文档汇总本项目的当前架构、已完成的工作、构建/运行方式与已知注意事项，
> 供后续开发团队快速接手。最后更新：2026-09。

## 1. 项目概述

**图像检索 DSL 工具（tio）**：基于自然语言描述检索图片的桌面应用。用户输入一句话
（如"一只猫在狗左边"），LLM 将其翻译为 DSL 查询，DSL 解释器对图库执行模糊检索并返回
排序图片。

- **GUI**：Qt 6（`tio/gui`，产物 `tio.exe`），负责输入、调用 LLM、显示结果、设置页。
- **引擎**：C++17 DSL 解释器（`tio/dsl`，产物 `dsl.exe`），负责解析/求值 DSL、加载 ONNX
  模型推理、缓存管理、扩展包细化。
- **推理后端**：ONNX Runtime（CPU），已完全移除 LibTorch。
- **模型**：基座 `yolov8m-oiv7.onnx`（Open Images V7，601 类，内置 fruit/food/animal/vehicle 等父类）；
  Places365 场景识别（ResNet18/50 → ONNX，365 类，用户提供模型文件）；
  `face_recognition_v1` 聚类扩展包（MobileFaceNet → 128 维 embedding → DBSCAN 人物聚类）。
- **数据**：图库在 `tio/photo`（128 张图片）。

## 2. 架构总览

```
用户输入 ──► tio.exe (Qt GUI) ──LLM──► DSL 代码 ──QProcess──► dsl.exe (引擎)
                                                                     │
                                        ┌────────────────────────────┤
                                        ▼                            ▼
                                cache/<model>/cache_index.json   models/base/*/model.onnx
                                 (增量缓存, 检测结果)            + models/registry.json
```

- GUI 通过 `QProcess` 以 `--json` 模式启动引擎，DSL 从 stdin 传入，结果以 JSON 从 stdout 返回。
- 引擎启动时先 `ensureCacheReady()`：加载/增量更新缓存（见 §6），并对激活的聚类包执行
  embedding 提取 + 聚类。
- 查询求值时，`%` 提取对象、`^` 上溯图片、`>>` 做扩展包细化、`cnt()` 继承计数。

## 3. 目录结构

```
tio/
├── photo/                        # 图库图片（.jpg/.png）
├── dsl/                          # 引擎项目
│   ├── CMakeLists.txt            # MSVC + Ninja + ONNX Runtime
│   ├── cmake/Findonnxruntime.cmake
│   ├── include/                  # Types / InferenceBackend / ModelRegistry / ExtensionManager
│   ├── src/
│   │   ├── main.cpp              # 入口 + --json REPL/文件模式
│   │   ├── cache/                # CacheManager(增量) + CacheIndex + YoloInference(ONNX)
│   │   ├── scene/                # SceneInference（Places365，ONNX，纯 C++/GDI+）
│   │   ├── cluster/              # Clustering（基于 embedding 的 DBSCAN）
│   │   ├── engine/               # OnnxInference（ONNX Runtime 后端）
│   │   ├── executor/             # Evaluator / Context / BuiltinMacros
│   │   ├── parser/               # Lexer / Parser / AST（手写递归下降，无 Lark）
│   │   └── utils/                # filesystem_utils（mtime/size）+ exif_reader（内置 EXIF）
│   ├── models/
│   │   ├── registry.json         # active_base / active_extensions
│   │   ├── base/yolov8m-oiv7/    # {model.onnx, meta.json, classes.json}
│   │   ├── extensions/           # 扩展包（face_recognition_v1，见 §8）
│   │   └── scene/                # Places365（用户提供 .onnx + categories，见其 README）
│   ├── cache/                    # 运行时生成：<model>/cache_index.json
│   ├── config/                   # settings.ini（GUI 设置 + 推理阈值）+ cluster_name_mappings.json
│   ├── docs/                     # 本目录（EN / 中文）
│   └── gui/                      # Qt 项目（产物 gui/build/tio.exe + 伴生 dsl.exe）
```

## 4. 已完成工作清单

### 4.1 GUI 三项缺陷修复
1. **扩展包/模型包不显示**：设置页面板构建时先 `scan()`，`showEvent` 时重新扫描，
   并连接管理器的 `modelsChanged`/`packsChanged` 信号自动刷新。
2. **设置页点击空白后左侧由白变黑**：`currentItemChanged` 收到 null 时恢复上次选中行；
   新增 `#navList` 选中态 QSS（白字蓝底）。
3. **中英文切换无效**：`LanguageManager` 的翻译器成员改为真正的 `ZhTranslator`
   （之前是空基类 `QTranslator`），并在构建设置页前先初始化语言管理器；设置页面板支持重译。

### 4.2 推理后端迁移：LibTorch → ONNX Runtime
- 新增 `include/InferenceBackend.h`（抽象接口）+ `src/engine/OnnxInference.{h,cpp}`。
- `YoloInference` 重构为 ONNX（letterbox → 推理 → xyxy 解码 → NMS）。
- `ExtensionManager` 改用 `OnnxInference` 加载扩展模型（分类器 `[1,nc]` / 检测器 `[1,4+nc,N]`
  / 嵌入 `[1,D]`）。
- `ModelRegistry` 只注册 `model.onnx`；CMake 链接 `onnxruntime::onnxruntime`，删除全部 LibTorch 依赖。
- 已下载并部署 **ONNX Runtime 1.29.0**。

### 4.3 模型切换为 yolov8m-oiv7（Open Images V7）
- `yolov8m-oiv7.pt` → `models/base/yolov8m-oiv7/model.onnx`（输出 `(1,605,8400)`，601 类）。
- 旧的 COCO-80 `yolov8m` 模型及 `make_classes.py`/`make_ext_model.py`/`export_ext_onnx.py` 已移除。
- `classes.json` 的 601 个输出类别按模型输出下标排序，父类链来自 Open Images 官方层级，
  统一小写；父类（fruit/food/animal/vehicle…）本身就是输出类别。
- `registry.json`：`active_base = "yolov8m-oiv7"`。
- 推理阈值统一存于 `config/settings.ini` 的 `[inference]`：`base_conf_threshold`(0.25)、
  `iou_threshold`(0.45)、`fallback_threshold`(0，禁用置信度降级——OIV7 直接输出父类)。
  引擎只读取 settings.ini（`config/config.json` 已移除）。

### 4.4 DSL 语法修正（重要）
新语法（唯一推荐写法）：
```
筛选图片：   $ : (条件)
存在判断：   any(条件)    # 当前图片存在满足条件的对象
全称判断：   all(条件)    # 当前图片所有对象都满足
```
- Parser：新增 `:`（Colon）token，`imgs : (condition)` → `FilterExpr`；
  `any(...)`/`all(...)` 解析为 `AnyAllExpr`（函数式）。
- Evaluator：`evalFilter`（逐图求值条件，score>0 保留）、`evalAnyAll`（any=max，all=min）。
- LLM 提示词已全部改写为新格式。
- 旧量词语法 `$ any (cond)` 仍向后兼容（结果一致），但新代码一律使用新语法。
- 测试文件：`dsl/test.dsl`、`test_spec.dsl` 已按新语法重写。

### 4.5 增量缓存（替代全量重建）
- 缓存文件：`cache/<model>/cache_index.json`（内联 JSON，含相对路径 → `{mtime, size, img_id, objects, img_attrs}`）。
- 启动时对比 `mtime`/`size`：**只对新增/修改的图片重新推理**，删除的剔除；无变化直接加载索引。
- 旧版 `metadata.json` 自动迁移（`CacheIndex::loadLegacyFromFile`）；索引损坏时先迁移，否则全量重建。
- 修改的图片保留原 `img_id`；对象 id 全局唯一单调（共享 `ObjectIdGenerator`）。

### 4.6 直方图宏（把 32 维色调直方图暴露给 DSL）
| 宏 | 说明 |
|----|------|
| `obj_hist(obj)` | 对象区域的 32 维直方图（裸类名→该类最佳对象；无对象→全零） |
| `img_hist()` | 当前图片全局 32 维直方图 |
| `hist_sim(A, B)` | 两个直方图余弦相似度（0~1） |
| `hist_value(obj, idx)` | 对象直方图第 `idx` 个 bin（0~31，越界报错） |
| `img_hist_value(idx)` | 图片全局直方图第 `idx` 个 bin |

- 新增 `HistVec = std::array<float,32>` 类型；`Value` 新增 `HIST_VEC`。
- 颜色↔bin 参考：红 0,31 / 橙 1,2 / 黄 3-5 / 绿 9-11 / 青 14,15 / 蓝 19-21 / 紫 24-26 / 粉 27-29。
- LLM 提示词已含上述说明。

### 4.7 图像质量 / EXIF / 用户标记（ImageAttrs 扩展）
预处理时对每张图计算（存入 `cache_index.json` 的 `img_attrs`）：
- **曝光**：64 维亮度直方图 `luma_hist`、`overexposure_score`（V>240 比例）、
  `underexposure_score`（V<30 比例）、`exposure_goodness = 1-(over+under)/2`。
- **清晰度**：全局/物体区域 Laplacian 方差归一化 `global_blur_score`、`local_blur_score`（`raw/(raw+3000)`，0~1，越高越清晰）。
- **EXIF**：内置轻量 JPEG EXIF 解析器（`src/utils/exif_reader.{h,cpp}`，无需 exiv2 依赖），
  读取 Make/Model/ISO/快门/光圈/焦距/日期；无 EXIF 时返回空/-1。
- **用户标记**：`user_tags`（键值对），GUI 详情面板编辑，存于缓存索引。

DSL 宏：
| 宏 | 说明 |
|----|------|
| `img_over()` / `img_under()` / `img_exp_good()` | 过曝/欠曝/曝光质量（0~1） |
| `img_hist_val(idx)` | 亮度直方图 bin（0~63） |
| `img_blur()` / `img_blurry()` / `obj_blur(obj)` | 全局清晰度 / 1-清晰度 / 物体局部清晰度 |
| `img_camera()` / `img_iso()` / `img_shutter()` / `img_aperture()` / `img_fl()` / `img_date()` | EXIF（无则空/-1） |
| `img_tag(key)` / `img_has_tag(key)` / `img_tag_equals(k,v)` | 用户标记查询 |
| `stof(s)` / `str_contains(s, sub)` | 字符串→数值 / 包含判断（替代 `contains` 运算符） |

GUI：双击结果打开图片详情对话框（元数据 + 标记编辑，直接写回 `cache_index.json`）。
标签预筛选由搜索栏的 **🏷️ 标签筛选** 对话框完成（见 §4.8），旧版"标记过滤器"下拉已移除。

> 备注：任务建议用 exiv2，此处改为内置轻量 EXIF 解析器以省去外部依赖；如需替换为
> exiv2，只需重写 `readExifFromJpeg`。缓存版本已升到 `1.1`（旧缓存自动全量重建）。

### 4.8 标签预筛选与资产管理（asset management）
- **标签预筛选管线**：GUI「🏷️ 标签筛选」对话框把 `(key, values)` 条件集合传到引擎
  `--tag-filter key=v1|v2`（多条件 AND，值 OR）。引擎端 `CacheManager::applyTagFilters`
  求出匹配图片集，`Context::setPrefilteredIds` 让 `$` 只遍历该集合。
- **筛选持久化**：`MainWindow::saveTagFilters/loadSavedTagFilters` 将条件存到
  `config/settings.ini` 的 `[filter] tag_filters`（格式 `key=v1|v2`）。对话框重开、DSL 重执行、
  应用重启后筛选均保留；对话框构造时用已有条件回填行。
- **空匹配语义**：`Context` 新增 `prefilter_active_` 标志——激活的筛选匹配 0 张时返回**空结果**，
  而不是回退到全库（此前 `getAllImagePaths` 以"非空"判断，导致筛选"看起来消失"）。
- **空值语义**：`--tag-filter key=`（无值）表示"key 任意值"；`main.cpp` 解析时不再把空串
  推入 values。
- **资产管理**：GUI 结果网格 `ExtendedSelection` 多选 +「🗑 删除选中」（确认后删除文件并
  调 `removeImagesFromCache`）；DSL 顶层 `del <路径|表达式|变量>`（`Parser` 增加 `Del` 关键字
  与 `DelStmt`，`Evaluator::evalDel` 经 `Context::setDeleteImagesCallback` 回调
  `CacheManager::removeImages` 删除文件并重写索引）。GUI 检测到 DSL 含 `del` 会先弹确认框。
- **对话框防呆**：TagFilterDialog 全部按钮 `setDefault(false)`/`setAutoDefault(false)`，
  避免回车同时触发默认按钮（曾导致"回车误删当前行 / 误新增空行"）。

### 4.9 Places365 场景识别（SceneInference）
- **模型**：`models/scene/places365_googlenet.onnx`（**GoogLeNet**，由官方
  `googlenet_places365.caffemodel` + deploy prototxt 转换而来）+ `categories_places365.txt`
  （官方 365 行）。备选 PyTorch ResNet18/50：用 `torch.onnx` 导出。详见 `models/scene/README_zh.md`。
- **实现**：`src/scene/SceneInference.{h,cpp}`，纯 C++ + ONNX Runtime + GDI+（沿用现有
  图像栈，不引入 OpenCV）。预处理：缩放 224×224 → RGB → `(x/255-mean)/std`
  （ImageNet 统计）→ 推理 → softmax(365)。单张推理约 30ms（CPU，GoogLeNet）。
- **缓存**：`ImageAttrs` 新增 `scene_vector`（`std::array<float,365>`）、`dominant_scene`、
  `indoor_score`（前 205 类概率和）；`CacheIndex` 版本升到 `1.2`（旧缓存自动重建）。
- **集成点**：`CacheManager::inferEntry` 在 YOLO 检测**之后、独立**地跑场景推理
  （同一张原始图），懒加载模型；缺失时打日志并降级（宏返回 0.0 / ""）。
- **DSL 宏**：`img_scene("name")` / `img_scene_top()` / `img_scene_vec()` / `img_is_indoor()`
  （`SceneFn` 枚举 + `evalSceneMacro`）。类别名从 `/b/beach 48` 解析为 `beach`；
  大小写与 `-`/`_`/空格 不敏感。
- **GUI**：图片详情对话框展示 `dominant_scene`、室内概率与 Top-5 场景。

### 4.10 聚类扩展包（V2）
- **模型**：`mobilefacenet.pt`（PyTorch，foamliu/MobileFaceNet）已转换为
  `models/extensions/face_recognition_v1/model.onnx` —— 128 维输出、输入 112×112、
  ImageNet 归一化，与 PyTorch 输出逐位验证（max diff ~3e-6）。
- **配置**：`capabilities.can_cluster` + `cluster_name`/`cluster_threshold`、`input_normalize`、
  `gui.{group_label,show_in_sidebar,icon}`（见 [extension_pack_format.md](extension_pack_format_zh.md)）。
- **管线**：`CacheManager::inferEntry` 对匹配 `parent_class` 的对象提取 L2 归一化 embedding；
  构建完成后 `runClustering` 运行 DBSCAN（minPts=1，余弦阈值，按 `(路径, obj_id)` 确定性排序）；
  聚类 id 形如 `face_cluster_person_001`，持久化进 `cache_index.json`
  （`embeddings`/`cluster_ids`/`cluster_groups`）。
- **DSL 宏**：`cluster_id(obj, "face_cluster")`、`cluster_sim(a, b, "face_cluster")`
  （`ClusterFn` + `evalClusterMacro`）。
- **GUI**：左侧边栏显示 👤 人物分支，每个聚类为一个条目（映射名或原始 id + 照片数）；
  单击显示该组照片，双击重命名（持久化到 `config/cluster_name_mappings.json`）。

## 5. 构建与运行

### 依赖
- **引擎**：MSVC（VS2022 工具链，实测 14.51）、CMake ≥3.18 + Ninja、
  ONNX Runtime（`onnxruntime-win-x64-1.29.0`）、nlohmann/json。
- **GUI**：Qt 6.10.1（MinGW 1310），构建产物需 `windeployqt` 部署 Qt 运行时。
- **Python**（仅模型导出）：PyTorch + onnx。

### 引擎构建
```powershell
# 需在 vcvars64 环境中执行
call "D:\Visual Studio\VC\Auxiliary\Build\vcvars64.bat"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
      -DONNXRUNTIME_ROOT="D:\Visual Studio Data\Modules\onnxruntime-1.29.0\onnxruntime-win-x64-1.29.0" `
      -DNLOHMANN_INCLUDE_DIR="D:\projects\anaconda\Library\include"
cmake --build build
```

### GUI 构建
```powershell
cmake -S gui -B gui/build -G Ninja -DCMAKE_PREFIX_PATH=D:/Qt/6.10.1/mingw_64
cmake --build gui/build
# POST_BUILD 会自动：复制引擎 dsl.exe + onnxruntime.dll、windeployqt 部署 Qt
```

### 运行
- 双击 `gui/build/tio.exe`（引擎由设置页 `engine/path` 或默认 `dsl/build/dsl.exe` 定位）。
- 命令行引擎：`dsl\build\dsl.exe --list-models` / `dsl.exe test.dsl` /
  `dsl.exe --json --photo <图库> <script.dsl>`。

## 6. 缓存机制说明
- 首次启动（无索引）：全量推理并写 `cache_index.json`。
- 之后启动：扫描图库（stat 每个文件），与索引对比 `mtime`/`size`，仅推理差异图片。
- 状态提示（stderr，GUI 日志面板可见）：
  `[Cache] Incremental update: inferring N image(s) (M cached).` 等。
- 清空 `cache/<model>/` 目录可强制全量重建；GUI 设置页"重新索引"即删除该目录。

## 7. DSL 速查（新语法）
```
$ : (any(class == "cat"))                        # 含猫的图片
$ : (cnt(person) > 2)                            # 人数 > 2 的图片
$ : (img_warmth() > 0.7 && any(class == "cat"))  # 暖色且含猫
% $ : (any(class == "person"))                   # 提取 person 对象
parts = people >> person_parts_v1                # 扩展细化（需扩展包存在）
^ parts                                          # 上溯到图片
$ : (any(hist_value(obj, 0) + hist_value(obj, 31) > 0.3))   # 红色占比高
```
更多见 `docs/dsl_reference_zh.md`（仍含旧语法说明，新代码以本文档 §4.4 为准）。

## 8. 已知问题与注意事项
1. **扩展包**：目前仅内置 `face_recognition_v1`（聚类包）。`>>` 细化需要检测/分类类扩展包；
   示例中的 `person_parts_v1`/`botany_v1` 仅为示意。新增方式：
   ```powershell
   # 把扩展模型导出为 .onnx，然后
   # 手写 models/extensions/<name>/config.json，并把 name 加入 registry.json 的 active_extensions
   ```
2. **Windows 长路径**：onnx 等 Python 包安装若报"文件名或扩展名太长"，
   需开启 `LongPathsEnabled` 或用解压方式安装。
3. **PowerShell 写测试文件**：`Set-Content -Encoding UTF8` 会写入 BOM，
   污染 DSL 首个标识符（报"未定义变量"）。用 `-Encoding Ascii` 或无 BOM 写入。
4. **LLM 配置**：`config/settings.ini` 中的 `api_key` 为占位值，需配置真实 Key 才能用
   "翻译为 DSL"功能；否则可手动在 DSL 编辑区输入代码点"执行检索"。
5. **模型导出**：引擎只认 `.onnx`。用 `yolo export model=model.pt format=onnx imgsz=640`。
6. **性能**：CPU 推理约 0.5s/图；129 张全量建缓存约 45s，增量启动（无变化）为秒级
   （仅加载索引，不加载 100MB 模型）。

## 9. 相关文档索引
- [README](README_zh.md) — 总览与快速开始
- [主模型包内部 JSON 格式](base_model_pack_format_zh.md)
- [扩展包内部 JSON 格式](extension_pack_format_zh.md)
- [DSL 语言参考](dsl_reference_zh.md)（含旧语法）
- [使用教程](usage_tutorial_zh.md)
- [架构说明](architecture_zh.md)
