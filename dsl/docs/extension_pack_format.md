# 扩展包内部 JSON 格式（Extension Pack）

扩展包（extension pack）对基座模型检测到的对象做 **二次精细化分析**：把某个父类对象区域
裁剪出来，喂给专用的小模型（分类器或检测器），产出"子类"对象（如 `person` → `head/torso/arm/leg`）。

扩展包位于 `models/extensions/<包名>/` 下，由 **两个必需文件** 组成：

```
models/extensions/<name>/
├── config.json   # 包配置（必须存在，否则该包不会被注册）
└── model.onnx    # ONNX 模型（分类器或检测器）
```

> 包名 = 目录名（同时是 `registry.json` `active_extensions` 中使用的 id）。
> 兼容性：引擎同时扫描 `models/extensions/` 与项目根的 `extensions/`（同名时前者优先）。

---

## 1. `config.json`

示例（`models/extensions/botany_v1/config.json`）：

```json
{
  "name": "botany_v1",
  "parent_class": "flower",
  "children": ["petal", "stamen", "stem"],
  "model_path": "extensions/botany_v1/model.onnx",
  "input_size": 224,
  "conf_threshold": 0.15,
  "crop_padding": 0.1,
  "is_classifier": true
}
```

### 字段说明

| 字段 | 类型 | 必填 | 默认 | 说明 |
|------|------|------|------|------|
| `name` | string | 否 | 目录名 | 包 id（实际以目录名为准） |
| `parent_class` | string | 推荐 | 空 | 触发细化的父类名。基座模型检测到该类别（或其子类）的对象时执行扩展 |
| `children` | array[string] | 推荐 | 空 | 扩展模型能输出的子类名列表（用于 DSL 汇总/界面显示） |
| `model_path` | string | **是** | 无 | `model.onnx` 路径。**相对路径以项目根为基准**（`extension_dirs[0]` 的父目录，即 `<project>/models/extensions` → `<project>`）；绝对路径亦可。为空则跳过该包 |
| `input_size` | int | 否 | `224` | 裁剪区域缩放到的边长（letterbox 后送入扩展模型） |
| `conf_threshold` | float | 否 | `0.3` | 子检测置信度下限，低于该值的子对象被丢弃 |
| `crop_padding` | float | 否 | `0.1` | 父对象包围盒向外扩大的比例（应对裁剪误差） |
| `is_classifier` | bool | 否 | `false` | `true` = 分类器输出；`false` = 检测器输出（见下节） |

### 引擎读取逻辑（`ExtensionManager::scan`）

```cpp
pack.name          = j.value("name", 目录名);
pack.parent_class  = j.value("parent_class", "");
for (auto& c : j.value("children", {})) pack.children.push_back(c);
pack.model_path    = j.value("model_path", "");
pack.input_size    = j.value("input_size", 224);
pack.conf_threshold= j.value("conf_threshold", 0.3f);
pack.crop_padding  = j.value("crop_padding", 0.1f);
pack.is_classifier = j.value("is_classifier", false);
```

---

## 2. `model.onnx`（扩展模型输出格式）

扩展模型的输入均为裁剪并缩放到 `input_size` 的 RGB 张量 `[1, 3, input_size, input_size]`（数值 0–1）。

### 分类器（`is_classifier: true`）

输出 `[1, nc]`（或 `[nc]`）的 **logits**（引擎内部做 softmax），对应 `children` 数组的顺序：

```
logits = model(crop)   # (1, nc)
```

每个子类的得分 = `softmax(logits)[0][i]`，取 `max` 且 `>= conf_threshold` 的类别作为子对象类别，
子对象位置取父对象包围盒。

### 检测器（`is_classifier: false`）

与主模型包相同的检测输出格式（见《主模型包内部 JSON 格式》第 3 节）：

```
(1, 4 + nc, total_anchors)
```

- 第 0–3 行：`[x1, y1, x2, y2]`，坐标为裁剪区像素；
- 第 4 行起：sigmoid 后的类别分数；
- 子对象坐标由裁剪区坐标映射回整图坐标。

### 生成工具

扩展包模型需自行训练或转换；`make_ext_model.py`/`export_ext_onnx.py` 已随旧基座模型移除。
可参考 `export_yolov8.py` 的思路，或用官方 `yolo export` 把自定义 `.pt` 转成 `.onnx`，
再把 `config.json` 的 `model_path` 指向 `extensions/<name>/model.onnx`，可删除 `.pt`。

---

## 3. 注册：`models/registry.json`

`active_extensions` 决定哪些扩展包允许被 `>>` 使用（未注册的包会报错）：

```json
{
  "active_base": "yolov8m-oiv7",
  "active_extensions": ["botany_v1", "person_parts_v1"]
}
```

- 只有出现在该数组中的包才能参与 DSL `>>` 扩展；
- 引擎 `ExtensionManager::setActiveExtensions` 会过滤掉"已注册但不在数组中"的包。

---

## 4. 执行流程（`>>` 扩展）

1. 对当前对象集合中 `class` 等于 `parent_class`（或其子类）的对象逐个处理；
2. 按 `crop_padding` 扩大包围盒并裁剪到 `input_size × input_size`；
3. 运行扩展模型，得到子类对象（检测器映射回整图坐标）；
4. 子对象带 `parent_id`（指向父对象）与 `score` 返回，供 DSL 继续运算（如 `^ parts` 上溯图片）。

### 依赖提示

- 若 `parent_class` 不在当前基座模型的 `classes.json` 中，GUI 会显示 ⚠ 警告；
  引擎执行 `>>` 时会因找不到父类而输出空结果（不崩溃）。
- 扩展包本身与基座模型解耦：切换基座模型（`active_base`）后无需修改扩展包，
  只要新基座仍包含 `parent_class` 即可复用。
