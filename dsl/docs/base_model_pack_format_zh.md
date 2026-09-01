# 主模型包内部 JSON 格式（Base Model Pack）

[**中文**](base_model_pack_format_zh.md) ・ [**English**](base_model_pack_format.md)

主模型包（base model pack）是基座目标检测模型的最小发布单元，位于 `models/base/<包名>/` 目录下，
由 **三个必需文件** 组成：

```
models/base/<name>/
├── model.onnx   # ONNX 模型（必须存在，否则该包不会被注册）
├── meta.json    # 模型元信息（类型、输入尺寸、类别数）
└── classes.json # 输出类别 + 继承（is-a）关系链
```

> 包名 = 目录名（即 `models/registry.json` 中 `active_base` 使用的 id）。

---

## 1. `meta.json`

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `name` | string | 推荐 | 包显示名（引擎实际使用目录名作为 id） |
| `type` | string | 否 | 默认 `"detector"`，当前仅支持检测器 |
| `input_size` | int | 否 | 推理边长，默认 `640`（letterbox 后送入网络的尺寸） |
| `classes` | int | 否 | 输出类别数，默认 `80` |

示例（`models/base/yolov8m-oiv7/meta.json`）：

```json
{
  "name": "yolov8m-oiv7",
  "type": "detector",
  "input_size": 640,
  "classes": 601
}
```

引擎读取逻辑（`ModelRegistry::scanBaseModels`）：

```cpp
info.type        = j.value("type", "detector");
info.input_size  = j.value("input_size", 640);
info.classes     = j.value("classes", 80);
```

---

## 2. `classes.json`

定义模型的输出类别及其父类（is-a）关系，格式为顶层 `"classes"` 数组：

```json
{
  "classes": [
    { "name": "person",   "parent": "root" },
    { "name": "car",      "parent": "vehicle" },
    { "name": "vehicle",  "parent": "machine" },
    { "name": "machine",  "parent": "root" }
  ]
}
```

### 字段规则

| 字段 | 类型 | 说明 |
|------|------|------|
| `name` | string | 类别名（支持 UTF-8，如中文）。**前 `classes` 个条目按数组顺序对应模型输出通道下标**，即 `getOutputClassNames()` 返回的 `[0, classes)` 区间 |
| `parent` | string | 父类名。缺失时默认 `"root"`；`"root"` 是通用祖先（`isChildOf(child, "root")` 恒真） |

### 重要约定

- **输出类别必须排在最前面**：引擎用 `std::min(classes, classes.json 条数)` 截取前 `classes` 个
  条目作为按输出下标索引的类别名；多出的条目视为父类定义（不占用输出通道）。
- 父类可以是任意层级，支持多级继承，例如 `car -> vehicle -> machine -> root`。
- 引擎会做循环保护，`isChildOf` 在出现环时返回 `false`。

### 生成工具

当前基座为 Open Images V7（`yolov8m-oiv7`，601 个输出类别）。类别名由检查点的
`model.names` 直接导出，父类链来自 Open Images 官方层级（`bbox_labels_600_hierarchy.json`）；
两者都转为小写以匹配 DSL 的类别约定（如 `cat`、`fruit`、`vehicle`）。父类如
`fruit`、`food`、`animal`、`vehicle` 本身就是模型的输出类别，因此 `classes.json` 无需额外追加父类条目。

新增模型时用官方 `yolo export` 导出 ONNX 后，按本文件的字段规则手工编写 `classes.json`
（或用任意脚本从 `model.names` + OI 层级生成）。

---

## 3. `model.onnx`（ONNX 输出格式）

`model.onnx` 是 **固定输入尺寸（640×640）导出** 的 ONNX 模型，输入为 `[1,3,640,640]`
RGB 张量（数值 0–1，letterbox 预处理），输出：

```
(1, 4 + nc, total_anchors)
```

各维度含义（引擎 `YoloInference::postprocess` 按此解码）：

- **第 0–3 行**：`[x1, y1, x2, y2]` —— 边界框坐标，**已解码到 letterbox 后的输入像素空间**
  （即模型内部完成 DFL 解码 + dist2bbox + stride 缩放）。
- **第 4 行起**：`nc` 个类别的 **sigmoid 后** 分数。
- 锚点顺序与输出是否 8400 无关：解码不再依赖网格坐标。

引擎只做 un-letterbox 映射：

```
ux1 = (x1 - pad_x) / scale
uy1 = (y1 - pad_y) / scale
ux2 = (x2 - pad_x) / scale
uy2 = (y2 - pad_y) / scale
```

> 这是模型**导出时**的格式要求，与预训练权重内部结构无关：导出脚本把检测头改写为
> 直接输出 `[x1, y1, x2, y2, cls...]` 张量（YOLOv8 先过 DFL 解出四边距离，再乘 stride 转像素），
> 与 ultralytics `yolo export format=onnx` 的输出一致。

### 转换工具

使用官方 ultralytics CLI 将 YOLOv8 检查点导出为上述格式：

```powershell
yolo export model=yolov8m-oiv7.pt format=onnx opset=12 imgsz=640
```

导出后删除旧的 `model.pt`，把 ONNX 文件放到 `model.onnx`。

---

## 4. 注册：`models/registry.json`

`active_base` 决定当前使用哪个基座模型包：

```json
{
  "active_base": "yolov8m-oiv7",
  "active_extensions": ["botany_v1", "person_parts_v1"]
}
```

- `active_base` 必须与 `models/base/` 下的某个目录名一致；不一致时引擎打印告警并回退到
  `models/base/` 中第一个可用包。
- 引擎启动时读取一次；交互模式可用 `/reload` 热重载（切换模型后缓存自动重建）。

---

## 5. 校验规则（引擎启动时）

`models/base/` 下每个子目录被注册的**硬性条件**：

1. `model.onnx` 存在；
2. `meta.json` 存在且可被解析（解析失败打印 `[Registry] Failed to parse ...` 并跳过该包）。

`classes.json` 缺失时不报错，但该模型将没有任何类别定义（所有类别名查询返回空）。
模型文件若不存在，引擎会打印导出提示命令。
