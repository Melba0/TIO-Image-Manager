# 扩展包内部 JSON 格式（Extension Pack）

扩展包（extension pack）对基座模型检测到的对象做 **二次精细化分析**：把某个父类对象区域
裁剪出来，喂给专用的小模型，产出"子类"对象（如 `person` → `head/torso/arm/leg`）。

V2 还支持 **聚类 / 嵌入扩展包**：模型输出固定维度特征向量（embedding），引擎在缓存构建时
自动为每个父类对象提取 embedding，并对全库做 DBSCAN 聚类，结果（`cluster_ids` 与每图的
`cluster_groups`）持久化进 `cache_index.json`，供 GUI 左侧"分组视图"展示与重命名。

扩展包位于 `models/extensions/<包名>/` 下，由 **两个必需文件** 组成：

```
models/extensions/<name>/
├── config.json   # 包配置（必须存在，否则该包不会被注册）
└── model.onnx    # ONNX 模型（分类器 / 检测器 / 嵌入模型）
```

> 包名 = 目录名（同时是 `registry.json` `active_extensions` 中使用的 id）。
> 兼容性：引擎同时扫描 `models/extensions/` 与项目根的 `extensions/`（同名时前者优先）。

---

## 1. `config.json`

### 1.1 检测 / 分类扩展包（V1）

示例（`models/extensions/botany_v1/config.json`）：

```json
{
  "name": "botany_v1",
  "parent_class": "flower",
  "children": ["petal", "stamen", "stem"],
  "model_path": "model.onnx",
  "input_size": 224,
  "conf_threshold": 0.15,
  "crop_padding": 0.1,
  "is_classifier": true
}
```

### 1.2 聚类 / 嵌入扩展包（V2）

示例（`models/extensions/face_recognition_v1/config.json`）：

```json
{
  "name": "face_recognition_v1",
  "parent_class": "person",
  "children": ["face"],
  "model_path": "model.onnx",
  "input_size": 112,
  "conf_threshold": 0.0,
  "crop_padding": 0.1,
  "is_classifier": false,
  "input_normalize": "imagenet",
  "capabilities": {
    "can_extract_embedding": true,
    "embedding_name": "face",
    "can_cluster": true,
    "cluster_name": "face_cluster",
    "cluster_threshold": 0.55
  },
  "gui": {
    "group_label": "人物",
    "show_in_sidebar": true,
    "icon": "👤"
  }
}
```

- `>>` 对聚类包退化为恒等变换（不产生子对象），embedding 提取由缓存管线在构建时完成。

### 字段说明

| 字段 | 类型 | 必填 | 默认 | 说明 |
|------|------|------|------|------|
| `name` | string | 否 | 目录名 | 包 id（实际以目录名为准） |
| `parent_class` | string | 推荐 | 空 | 触发细化的父类名。基座模型检测到该类别（或其子类）的对象时执行扩展 / 提取 embedding |
| `children` | array[string] | 推荐 | 空 | 扩展模型能输出的子类名列表（用于 DSL 汇总/界面显示） |
| `model_path` | string | **是** | 无 | `model.onnx` 路径。**相对路径以包自身目录为基准**（即 `<包目录>/model.onnx`）；绝对路径亦可。为空则跳过该包 |
| `input_size` | int | 否 | `224` | 裁剪区域缩放到的边长（缩放后送入扩展模型） |
| `conf_threshold` | float | 否 | `0.3` | 子检测置信度下限（聚类包通常忽略，设 0） |
| `crop_padding` | float | 否 | `0.1` | 父对象包围盒向外扩大的比例（应对裁剪误差） |
| `is_classifier` | bool | 否 | `false` | `true` = 分类器输出；`false` = 检测器输出 |
| `input_normalize` | string | 否 | `""` | 输入预处理：`""`/`"none"` = 原始 0–1；`"imagenet"` = 按 ImageNet 均值/方差逐通道归一化（face 模型训练常用） |
| `capabilities.can_extract_embedding` | bool | 否 | `false` | 模型输出固定维度 embedding（如 MobileFaceNet 512/128 维） |
| `capabilities.embedding_name` | string | 否 | 空 | embedding 存入对象 `embeddings[<embedding_name>]` 的键名 |
| `capabilities.can_cluster` | bool | 否 | `false` | 该包驱动一个全局聚类过程（DBSCAN） |
| `capabilities.cluster_name` | string | 否 | 空 | 聚类标签，写入对象 `cluster_ids[<cluster_name>]` 与每图 `cluster_groups[<cluster_name>]` |
| `capabilities.cluster_threshold` | float | 否 | `0.55` | 余弦相似度阈值（0–1），两个 embedding 点积 ≥ 阈值视为同簇 |
| `gui.group_label` | string | 否 | 空 | 左侧边栏分组分支标题（如 "人物"） |
| `gui.show_in_sidebar` | bool | 否 | `true` | 是否在左侧边栏生成分组视图 |
| `gui.icon` | string | 否 | 空 | 分组分支前缀图标（如 "👤"） |

### 引擎读取逻辑（`ExtensionManager::scan`）

```cpp
pack.name             = j.value("name", 目录名);
pack.parent_class     = j.value("parent_class", "");
pack.model_path       = j.value("model_path", "");
pack.input_size       = j.value("input_size", 224);
pack.conf_threshold   = j.value("conf_threshold", 0.3f);
pack.crop_padding     = j.value("crop_padding", 0.1f);
pack.is_classifier    = j.value("is_classifier", false);
pack.input_normalize  = j.value("input_normalize", "");
pack.can_extract_embedding = cap.value("can_extract_embedding", false);
pack.embedding_name   = cap.value("embedding_name", "");
pack.can_cluster      = cap.value("can_cluster", false);
pack.cluster_name     = cap.value("cluster_name", "");
pack.cluster_threshold= cap.value("cluster_threshold", 0.55f);
pack.gui_group_label  = g.value("group_label", "");
pack.gui_show_in_sidebar = g.value("show_in_sidebar", true);
pack.gui_icon         = g.value("icon", "");
```

---

## 2. `model.onnx`（扩展模型输出格式）

扩展模型的输入均为裁剪并缩放到 `input_size` 的 RGB 张量 `[1, 3, input_size, input_size]`。
数值 0–1（除非配置了 `input_normalize: "imagenet"`）。

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

### 嵌入模型（聚类包）

输出一维向量 `[1, D]` 或 `[D]`（如 MobileFaceNet 128/512 维）。引擎将其 L2 归一化后存入
对象 `embeddings[embedding_name]`；聚类阶段用余弦相似度（点积）做 DBSCAN。

### 生成工具

`mobilefacenet.pt`（PyTorch 权重）可先用 PyTorch 转成 ONNX：
`torch.onnx.export(model, x, "model.onnx", input_names=["input"], output_names=["output"])`，
再放入 `models/extensions/<name>/model.onnx`（`.pt` 可删）。

---

## 3. 注册：`models/registry.json`

`active_extensions` 决定哪些扩展包被激活（未激活的包不参与 `>>`，聚类包也不提取 embedding）：

```json
{
  "active_base": "yolov8m-oiv7",
  "active_extensions": ["botany_v1", "person_parts_v1", "face_recognition_v1"]
}
```

- 只有出现在该数组中的包才会被 `ExtensionManager::setActiveExtensions` 启用。

---

## 4. 执行流程

### 4.1 `>>` 扩展（检测 / 分类包）

1. 对当前对象集合中 `class` 等于 `parent_class`（或其子类）的对象逐个处理；
2. 按 `crop_padding` 扩大包围盒并裁剪到 `input_size × input_size`；
3. 运行扩展模型，得到子类对象（检测器映射回整图坐标）；
4. 子对象带 `parent_id`（指向父对象）与 `score` 返回，供 DSL 继续运算（如 `^ parts` 上溯图片）。

> 聚类包在 `>>` 下退化为恒等变换：缓存管线已在构建时把 embedding / cluster_ids 挂到对象上。

### 4.2 聚类管线（聚类包）

1. 缓存构建（`CacheManager::inferEntry`）时，对每个匹配 `parent_class` 的对象运行嵌入模型，
   把 L2 归一化 embedding 存入对象并持久化到 `cache_index.json`；
2. 全量/增量构建结束后，`CacheManager::runClustering` 对每个 `capabilities.can_cluster` 的激活包：
   - 收集全库该 `embedding_name` 的 embedding，按 (路径, obj_id) 排序保证确定性；
   - DBSCAN（minPts=1，余弦阈值 `cluster_threshold`）分配聚类；
   - 聚类 id 格式：`<cluster_name>_<parent_class>_<NNN>`（如 `face_cluster_person_001`）；
   - 写回每个对象的 `cluster_ids[<cluster_name>]`，并重建每图 `cluster_groups[<cluster_name>]`；
3. GUI 左侧自动生成分支（标题 = `gui.icon + gui.group_label`），每个聚类为一个可点击项
   （显示映射名/原始 id + 照片数），点击显示该组照片，双击弹出重命名（写入
   `config/cluster_name_mappings.json`，如 `"face_cluster_person_001": "张三"`）。

### DSL 宏（聚类包）

- `cluster_id(obj, "<cluster_name>")` → 对象的聚类 id（字符串，无则空串）；
- `cluster_sim(a, b, "<cluster_name>")` → 两对象是否在同一聚类（模糊分 1/0）。

```dsl
# 属于 face_cluster 聚类、且 id 为 person_001 的图片
$ : (any(cluster_id(obj, "face_cluster") == "face_cluster_person_001"))
# 两张图中存在同一聚类对象
$ : (any(cluster_sim(obj, obj, "face_cluster")))
```

### 依赖提示

- 若 `parent_class` 不在当前基座模型的 `classes.json` 中，GUI 会显示 ⚠ 警告；
  引擎执行 `>>` 时会因找不到父类而输出空结果（不崩溃）。
- 聚类包模型缺失时（未下载 `model.onnx`），提取失败会优雅降级：无 embedding、无分组，不报错。
- 扩展包本身与基座模型解耦：切换基座模型（`active_base`）后无需修改扩展包，
  只要新基座仍包含 `parent_class` 即可复用。
