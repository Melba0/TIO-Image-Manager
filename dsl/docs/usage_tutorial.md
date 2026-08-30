# 使用教程

本教程介绍解释器各功能模块的实际用法：首次运行、增量缓存、模型注册表、继承查询、
标签预筛选、资产管理，以及桌面 GUI 的配合使用。

## 1. 首次运行与增量缓存

程序启动时会扫描 `tio/photo/` 下的图片，并依据当前激活的基座模型运行 YOLO 推理。

```powershell
cd dsl
build\dsl.exe
```

启动日志示例：

```
[Main] active base model: yolov8m-oiv7 (C:\...\dsl\models\base\yolov8m-oiv7\model.onnx)
[Main] photo dir: C:\...\tio\photo
[Main] thresholds: base_conf=0.25 iou=0.45 fallback=0
[Cache] Loaded 128 images from cache index.
[Cache] Cache is up to date (128 images).
```

- **首次运行**（无缓存索引）：对每张图执行 YOLO 推理（CPU 下约数分钟），
  结果写入 `cache/yolov8m-oiv7/cache_index.json`。
- **再次运行**：扫描图库并对比 `mtime`/`size`，**只对新增/修改的图片重新推理**，
  删除的图片自动剔除；无变化时直接加载索引（秒级，不加载模型）。
- **缓存失效**：以下情况会触发重建：
  - 图片文件新增 / 删除 / 修改；
  - 缓存格式版本变化；
  - **切换了基座模型**（缓存按模型分子目录）。

### 缓存内容（cache_index.json，版本 1.1）

```json
{
  "version": "1.1",
  "model_name": "yolov8m-oiv7",
  "next_obj_id": 225,
  "next_img_id": 128,
  "entries": {
    "000000000049.jpg": {
      "mtime": 1661439700,
      "size": 158392,
      "img_id": 6,
      "objects": [ { "class": "person", "x": 0.32, "y": 0.64, "w": 0.22, "h": 0.27,
                     "area": 0.062, "confidence": 0.361, "original_class": "",
                     "super_class": "person", "is_fallback": false, "parent_id": -1,
                     "obj_id": 0, "img_id": 6 } ],
      "img_attrs": { "color_temperature": 5200, "avg_hue": 28.1, "avg_saturation": 0.41,
                     "avg_value": 0.62, "dominant_color": "orange",
                     "global_hue_hist": [ ... ], "luma_hist": [ ... ],
                     "overexposure_score": 0.02, "underexposure_score": 0.05,
                     "exposure_goodness": 0.965, "global_blur_score": 0.71,
                     "global_blur_raw": 8123.4, "camera_make": "...", "camera_model": "...",
                     "iso": 100, "shutter_speed": 0.008, "aperture": 2.8, "focal_length": 50,
                     "datetime_original": "...", "width": 640, "height": 480,
                     "user_tags": { "city": "sh", "level": "3" } }
    }
  }
}
```

> 用户标记 `user_tags` 由 GUI 详情面板编辑并写回该文件；标签预筛选正是基于它工作的。

## 2. 模型注册表（Model Registry）

### 目录约定

```
models/
├── registry.json          # 切换开关
└── base/
    ├── yolov8m-oiv7/      # 一个模型一个子目录
    │   ├── model.onnx     # ONNX 模型（引擎只加载 .onnx）
    │   ├── meta.json      # { "name", "type", "input_size", "classes" }
    │   └── classes.json   # 输出类别 + 父类继承链
```

`meta.json` 示例：

```json
{ "name": "yolov8m-oiv7", "type": "detector", "input_size": 640, "classes": 601 }
```

`registry.json` 示例：

```json
{ "active_base": "yolov8m-oiv7", "active_extensions": [] }
```

### 切换模型（改配置后重启）

编辑 `models/registry.json`，把 `active_base` 改为其它模型名，重启程序（或 REPL 中 `/reload`）。
程序检测到 `cache/<新模型>/` 不存在（或旧缓存属于其它模型），自动用新模型重建缓存。

### 命令行临时切换

```powershell
build\dsl.exe --base yolov8m-oiv7   # 本次运行使用 yolov8m-oiv7
build\dsl.exe --list-models         # 列出所有注册的基座模型
```

`--base` 优先级高于 `registry.json`；指定不存在的模型会报错并列出可用项。

### 添加新基座模型

1. 用 `export_yolov8.py`（或官方 `yolo export`）把 `.pt` 转成 `.onnx`，放入 `models/base/<名字>/model.onnx`。
2. 同目录创建 `meta.json`（`name` 与目录名一致）与 `classes.json`（见
   [base_model_pack_format.md](base_model_pack_format.md) 的字段规则）。
3. 编辑 `models/registry.json` 的 `active_base`，或启动时用 `--base <名字>`。

## 3. 继承映射与置信度降级

`classes.json` 定义"子类 → 父类"（is-a）继承链。推理阈值统一放在
`config/settings.ini` 的 `[inference]` 段（GUI 设置页「推理阈值」面板可调），
其中 `fallback_threshold`（默认 0）控制置信度降级：

- 检测置信度 **≥ base_conf_threshold**（默认 0.25）才会被记录；
- **fallback_threshold = 0**：禁用降级。OIV7 模型本身就能直接输出 `fruit`、`food`、
  `animal`、`vehicle` 等父类别，无需再按置信度把子类改写为父类；
  若手动调高，则置信度低于该值的检测会降级为父类名（`original_class` 保留原类别）。

`cnt(fruit)` 既统计类别名为 `fruit` 的对象，也统计 `apple`/`banana` 等子类：

```dsl
cnt(fruit)                          # 全库水果总数
$ : (cnt(person) > 2)               # person 超过 2 个的图片
$ : (cnt(animal) > 0)               # animal 及其全部子类（cat/dog/…）
```

> 类别名与 `classes.json` 完全一致：小写，多词类别用下划线（如 `traffic_light`、
> `stop_sign`、`mobile_phone`）。父类别（`fruit`/`food`/`animal`/`vehicle`…）就是
> 模型的直接输出类别。

## 3.5 场景识别（Places365）

在缓存预处理阶段，每张图片还会通过 **Places365** 场景模型（ONNX Runtime / CPU，纯 C++）
计算一个 365 维场景概率向量，与物体检测并行、互不干扰。结果写入
`cache_index.json` 的 `img_attrs`：

```json
"img_attrs": {
  "...": "...",
  "scene_vector": [0.01, 0.02, 0.85, ...],   // 365 个场景的概率
  "dominant_scene": "beach",
  "indoor_score": 0.12                        // P(室内) = 前 205 类概率之和
}
```

DSL 场景宏（模型缺失时返回 0.0 / 空字符串，不报错）：

| 宏 | 说明 | 示例 |
|----|------|------|
| `img_scene("name")` | 场景概率（0~1） | `$ : (img_scene("beach") > 0.7)` |
| `img_scene_top()` | 概率最高的场景名 | `$ : (img_scene_top() == "forest")` |
| `img_is_indoor()` | 室内概率（0~1） | `$ : (img_is_indoor() > 0.6 && any(class == "person"))` |
| `img_scene_vec()` | 内部宏（返回最高概率） | — |

场景名取自 `models/scene/categories_places365.txt`（365 行）；名称大小写、
`-`/`_`/空格 均视为等价（`dining_room` == `dining room`）。

**模型准备**（用户操作，一次性）：

```powershell
cd dsl
# GoogLeNet（Caffe 官方权重，推荐）：
python caffe_places365_to_onnx.py ../googlenet_places365.caffemodel \
       models/scene/deploy_googlenet_places365.prototxt \
       models/scene/places365_googlenet.onnx
# 或 ResNet18（PyTorch 检查点）：
python export_places365.py resnet18_places365.pth.tar models/scene/places365_googlenet.onnx
# 并把 categories_places365.txt 放到 models/scene/（详见 models/scene/README.md）
```

> 场景识别与 YOLO 物体检测、颜色直方图、曝光/清晰度、EXIF、用户标记等特性平级，
> 可同时使用；推理在缓存构建阶段完成，查询阶段只读缓存，单张图片场景推理 < 50ms（CPU）。

## 4. 标签预筛选（Tag Pre-Filter）

在求值**之前**把 `$` 限制到 `user_tags` 匹配的图片，适合"先缩小范围再精确查询"。

```powershell
# 命令行：多条件 AND，同一 key 的多个值 OR
build\dsl.exe --json --photo .\photo --tag-filter "city=sh|bj" --tag-filter "level=3"
build\dsl.exe --json --photo .\photo --tag-filter "location="   # 该 key 任意值
```

- 每个 `--tag-filter key=v1|v2` 是一个条件；多个条件之间是 **AND**；同一 key 的多个值是 **OR**。
- `key=`（空值列表）表示"存在该 key 即可，值不限"。
- 匹配 0 张时返回**空结果**（不会悄悄回退到全库）。

GUI 用法：点击搜索栏的 **🏷️ 标签筛选**，在对话框里添加 `key + 值` 条件（支持多行、删除、
清空）。点击「应用筛选」后生效，条件会**持久化**（重启后仍在）；对话框再次打开时自动回填上次条件。

## 5. 资产管理（删除图片）

- **DSL 语句**：`del <目标>`，目标可以是字符串路径、图片集合表达式或变量。
  引擎会删除对应磁盘文件并同步更新缓存索引。

```dsl
del "000000000049.jpg"                    # 删除单张图片
del $ : (any(class == "cat"))             # 删除所有含猫的图片
people = % $ : (any(class == "person"))
del people                                # 删除变量指向的图片
```

- **GUI**：在结果网格中按住 Ctrl/Shift 多选缩略图，点 **🗑 删除选中**（带确认）。

> 删除为不可逆操作。GUI 检测到 DSL 含 `del` 时会弹出确认框。

## 6. 扩展包（Extension Pack）

扩展包对基座模型检测到的对象做"二次精细化分析"：把某个父类对象区域裁剪出来，
喂给专用的小模型（分类器或检测器），产出子类对象（如 `person` → `head/torso/arm/leg`）。

```
models/extensions/<name>/
├── config.json   # { name, parent_class, children, model_path, input_size, conf_threshold, crop_padding, is_classifier }
└── model.onnx    # 分类器 [1,nc] 或检测器 [1,4+nc,anchors]
```

`models/registry.json` 的 `active_extensions` 决定哪些包可被 `>>` 使用（未注册的包报错）：

```powershell
# 手写 models/extensions/<name>/config.json，并把 "<name>" 加入 registry.json 的 active_extensions
```

```dsl
people = % $ : (any(class == "person"))
parts = people >> demo_v1    # 对每个人物裁剪区域并运行扩展模型
^ parts                      # 上溯回这些部位所在的图片
```

`parts` 中的每个对象带 `parent_id`（关联父对象 id）、`super_class`（父类名）、
新的全局 `obj_id`，并映射回原图坐标。扩展包格式详见 [extension_pack_format.md](extension_pack_format.md)。

## 7. REPL 交互

| 指令 | 作用 |
|------|------|
| `exit` / `quit` | 退出 |
| `/reload` | 重读 `models/registry.json`（可切换基座模型与扩展包） |
| 其它输入 | 当作 DSL 表达式/赋值/`del` 执行 |

示例会话：

```dsl
dsl> $ : (any(class == "person"))
ImageSet (38 images):
  000000000368.jpg
  ...
dsl> % $ : (any(class == "person"))
ObjectSet (50 objects):
  [000000000081.jpg] airplane (x=0.329, ... area=0.19, conf=0.55, id=7)
  ...
```

> 在 Windows 上，若 REPL 中直接输入中文无法匹配，请确保控制台为 UTF-8 编码
> （程序启动时会自动设置，或在终端执行 `chcp 65001`）。

## 8. 桌面 GUI（tio.exe）

GUI 位于 `tio/gui`（Qt 6）。构建后在 `gui/build/tio.exe` 双击运行：

1. 在搜索框输入自然语言（如"一只猫在狗左边"），点「翻译为 DSL」调用 LLM 生成 DSL；
   也可直接在 DSL 编辑区手写后点「执行检索」。
2. 结果以缩略图网格展示（按模糊分数降序，低分自动过滤）。
3. 双击结果打开**图片详情**对话框：查看元数据（曝光/清晰度/EXIF），编辑用户标记。
4. 搜索栏 **🏷️ 标签筛选**：配置标签预筛选（见第 4 节）。
5. 搜索栏 **🗑 删除选中**：多选删除图片（见第 5 节）。
6. 右上角语言切换（EN/中文）、设置页（API/图库/模型/推理阈值/扩展/日志）、深浅主题切换。

## 9. 常见问题

**Q：切换模型后查询结果没变？**
A：请确认 `models/registry.json` 的 `active_base` 已修改且重启（或 REPL 中执行 `/reload`）；
缓存按模型名隔离，切换后会在下一次查询时重建。

**Q：`>> xxx` 报错 "not active in registry"？**
A：该扩展包未出现在 `registry.json` 的 `active_extensions`，请检查拼写或启用它。

**Q：标签筛选似乎没生效？**
A：确认图片存在对应的 `user_tags`（在详情面板编辑并已写回 `cache_index.json`）；
`--tag-filter "key="`（无值）表示"key 任意值"，`key=val` 为精确匹配。
匹配 0 张会得到空结果，而非全库。

**Q：首次构建很慢？**
A：CPU 推理正常现象。缓存生成后再次运行极快；减少 `photo/` 图片数可缩短首建时间。
