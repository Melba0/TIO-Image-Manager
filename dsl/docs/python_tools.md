# Python 工具脚本说明

`tio/dsl/` 下共有 4 个 Python 脚本，负责**模型转换**与**配套配置生成**。引擎本身是 C++ 程序，
不直接调用这些脚本——它们只用于在开发期准备 `.onnx` 模型与 `classes.json`。

| 脚本 | 作用 |
|------|------|
| `export_yolov8.py` | 把 ultralytics YOLOv8 检查点 `.pt` → 引擎可用的 `model.onnx` |
| `export_ext_onnx.py` | 把扩展包的 TorchScript `.pt` → `.onnx` |
| `make_classes.py` | 生成基座模型的 `classes.json`（COCO-80 + 父类继承链） |
| `make_ext_model.py` | 生成合成扩展分类器模型（`.pt`，用于演示/测试） |

## 环境依赖

- Python 3.10+
- PyTorch（CPU 版即可）：`pip install torch`
- 前两个导出脚本需要 `onnx`：`pip install onnx`

---

## 1. `export_yolov8.py` —— 基座 YOLO 模型导出

**功能**：把官方/自定义训练的 ultralytics YOLOv8 检查点（如 `yolov8m.pt`）转换为
ONNX Runtime 可加载的 `model.onnx`。

**为什么不用官方 `yolo export`？** 脚本在 Python 内重建了 YOLOv8 的模块结构
（`Conv / C2f / Bottleneck / SPPF / DFL / Detect`），无需安装 ultralytics 包即可解开
检查点的 pickle。输出格式与 `yolo export format=onnx` 完全一致，两种方式可互换。

**输出格式**：输入 `[1,3,640,640]` RGB（0~1），输出 `(1, 4+nc, total_anchors)`，
其中第 0–3 行为**已解码到输入像素空间**的框坐标 `[x1,y1,x2,y2]`，第 4 行起为 sigmoid
类别分数。C++ 端 `YoloInference::postprocess` 按此解码。

**用法**：
```powershell
python export_yolov8.py <source.pt> <output.onnx>
# 例：
python export_yolov8.py yolov8m.pt models/base/yolov8m/model.onnx
```

**等价官方命令**：
```powershell
yolo export model=yolov8m.pt format=onnx opset=12 imgsz=640
```

**注意事项**：
- 输出要求 `(1, 84, 8400)`（nc=80），不匹配会报错。
- 导出后删除旧的 `model.pt`，把 `.onnx` 放到包目录 `models/base/<name>/model.onnx`；
  引擎只加载 `model.onnx`。
- 需要先有 `classes.json`（见 `make_classes.py`）与 `meta.json` 才能被引擎注册。

---

## 2. `export_ext_onnx.py` —— 扩展包模型导出

**功能**：把扩展包的 TorchScript 分类器/检测器 `.pt` 转换为 `.onnx`。
扩展包 `.pt` 通常由 `make_ext_model.py`（或用户自己的训练脚本）生成。

**用法**：
```powershell
python export_ext_onnx.py <model.pt> <model.onnx> [input_size]
# 例（input_size 需与 config.json 中的 input_size 一致，默认 224）：
python export_ext_onnx.py models/extensions/botany_v1/model.pt models/extensions/botany_v1/model.onnx 224
```

**注意事项**：
- `input_size` 必须与扩展包 `config.json` 里的 `input_size` 一致（当前 botany_v1 /
  person_parts_v1 均为 224）。
- 导出后更新 `config.json` 的 `model_path` 指向 `.onnx`：
  ```json
  "model_path": "extensions/botany_v1/model.onnx"
  ```

---

## 3. `make_classes.py` —— 生成 classes.json

**功能**：生成基座模型的类别定义文件 `classes.json`，包含 COCO-80 输出类别及**父类继承链**
（如 `apple -> fruit -> food -> consumable`）。引擎用它实现继承查询（`cnt(fruit)` 会统计
apple/banana 等子类）与置信度降级（低分 apple 归入 fruit）。

**用法**：
```powershell
python make_classes.py <output.json>
# 例：
python make_classes.py models/base/yolov8m/classes.json
```

**输出结构**：
```json
{
  "classes": [
    { "name": "person",  "parent": "root" },
    { "name": "car",     "parent": "vehicle" },
    { "name": "vehicle", "parent": "machine" },
    ...
  ]
}
```

**注意事项**：
- 前 `classes` 个条目（`meta.json` 中 `classes` 字段，默认 80）按数组顺序对应模型输出
  通道下标，**顺序不可改**；后面的条目是父类定义（不占输出通道）。
- 仅适用于 COCO-80 训练的模型。若用自定义类别，需修改脚本里的 `COCO` 与 `PARENT` 表。

---

## 4. `make_ext_model.py` —— 生成合成扩展分类器

**功能**：生成一个**未训练的小型卷积分类器**（4 层 Conv + 全连接），输出 `[1, num_classes]`
logits。用于演示扩展包 `>>` 细化流程（无需真实标注数据）。

**用法**：
```powershell
python make_ext_model.py <num_classes> <output.pt>
# 例：
python make_ext_model.py 3 models/extensions/demo_v1/model.pt
```

**注意事项**：
- 生成的是**随机权重**模型，仅供流程演示；真实使用请替换为训练好的模型。
- 产出的 `.pt` 需再用 `export_ext_onnx.py` 转成 `.onnx`：
  ```powershell
  python export_ext_onnx.py models/extensions/demo_v1/model.pt models/extensions/demo_v1/model.onnx 224
  ```

---

## 推荐工作流

```powershell
# 1) 基座模型
python make_classes.py models/base/yolov8m/classes.json          # 生成类别/继承
python export_yolov8.py yolov8m.pt models/base/yolov8m/model.onnx # .pt -> .onnx
#    （手动写好 models/base/yolov8m/meta.json）

# 2) 扩展包
python make_ext_model.py 3 models/extensions/demo_v1/model.pt     # 合成分类器
python export_ext_onnx.py models/extensions/demo_v1/model.pt models/extensions/demo_v1/model.onnx 224
#    （手动写好 models/extensions/demo_v1/config.json，model_path 指向 .onnx）

# 3) 引擎使用
build\dsl.exe --list-models     # 检查模型是否被注册
build\dsl.exe test.dsl          # 执行检索
```
