# Python 工具脚本说明

`tio/dsl/` 下目前保留 **1 个** Python 脚本：`export_yolov8.py`，负责把 ultralytics
YOLOv8 检查点转换为引擎可用的 ONNX 模型。引擎本身是 C++ 程序，不直接调用这些脚本——
它们只用于在开发期准备 `.onnx` 模型。

| 脚本 | 作用 | 状态 |
|------|------|------|
| `export_yolov8.py` | 把 ultralytics YOLOv8 检查点 `.pt` → 引擎可用的 `model.onnx` | ✅ 使用中 |
| `export_places365.py` | 把 Places365 PyTorch 检查点 `.pth.tar` → 场景识别 `model.onnx` | ✅ 使用中 |
| `caffe_places365_to_onnx.py` | 把 Places365 Caffe 模型（caffemodel + prototxt）→ 场景识别 `model.onnx` | ✅ 使用中 |
| `export_ext_onnx.py` | 扩展包 TorchScript `.pt` → `.onnx` | ❌ 已移除 |
| `make_classes.py` | 生成 COCO-80 `classes.json` | ❌ 已移除（基座已换 OIV7） |
| `make_ext_model.py` | 生成合成扩展分类器 `.pt` | ❌ 已移除 |

## 环境依赖

- Python 3.10+
- PyTorch（CPU 版即可）：`pip install torch`
- 导出需要 `onnx`：`pip install onnx`

---

## `export_yolov8.py` —— 基座 YOLO 模型导出

**功能**：把官方/自定义训练的 ultralytics YOLOv8 检查点（如 `yolov8m-oiv7.pt`）转换为
ONNX Runtime 可加载的 `model.onnx`。

**为什么不用官方 `yolo export`？** 脚本在 Python 内重建了 YOLOv8 的模块结构
（`Conv / C2f / Bottleneck / SPPF / DFL / Detect`），无需安装 ultralytics 包即可解开
检查点的 pickle。输出格式与 `yolo export format=onnx` 完全一致，两种方式可互换。

**输出格式**：输入 `[1,3,640,640]` RGB（0~1），输出 `(1, 4+nc, total_anchors)`，
其中第 0–3 行为**已解码到输入像素空间**的框坐标 `[x1,y1,x2,y2]`，第 4 行起为 sigmoid
类别分数。C++ 端 `YoloInference::postprocess` 按此解码。当前基座 `yolov8m-oiv7` 为
nc=601，输出 `(1, 605, 8400)`。

**用法**：
```powershell
python export_yolov8.py <source.pt> <output.onnx>
# 例：
python export_yolov8.py yolov8m-oiv7.pt models/base/yolov8m-oiv7/model.onnx
```

**等价官方命令**：
```powershell
yolo export model=yolov8m-oiv7.pt format=onnx opset=12 imgsz=640
```

**注意事项**：
- 输出要求 `(1, 4+nc, N)`（`nc` = 类别数），不匹配会报错。
- 导出后删除旧的 `model.pt`，把 `.onnx` 放到包目录 `models/base/<name>/model.onnx`；
  引擎只加载 `model.onnx`。
- 还需要 `meta.json` 与 `classes.json` 才能被引擎注册，两者均为手工维护
  （字段规则见 [base_model_pack_format.md](base_model_pack_format.md)）。
- `classes.json` 的前 `classes` 个条目必须按模型输出下标顺序排列，父类链来自 Open Images
  官方层级，统一小写（父类如 `fruit`/`food`/`animal`/`vehicle` 本身就是输出类别）。

---

## `export_places365.py` —— Places365 场景模型导出

**功能**：把 MIT Places365 官方检查点（`.pth.tar`）转换为场景识别 `model.onnx`
（输入 `[1,3,224,224]` RGB / ImageNet 归一化，输出 `[1,365]` logits）。

**用法**：
```powershell
python export_places365.py resnet18_places365.pth.tar models/scene/places365_googlenet.onnx
# 或 ResNet50：python export_places365.py resnet50_places365.pth.tar models/scene/places365_googlenet.onnx
```

> 引擎按 `models/scene/places365_googlenet.onnx` 加载；如导出为其它文件名，
> 需同步修改 `main.cpp` 中的模型路径。

**配套文件**：`models/scene/categories_places365.txt`（官方 365 行场景名，与输出顺序一一对应；
前 205 类为室内）。详见 [models/scene/README.md](../models/scene/README.md)。

---

## `caffe_places365_to_onnx.py` —— Caffe GoogLeNet 场景模型转换

**功能**：把 MIT Places365 官方 **Caffe GoogLeNet** 模型（`googlenet_places365.caffemodel`
+ `deploy_googlenet_places365.prototxt`）转换为 ONNX（输入 `[1,3,224,224]` RGB，
输出 `[1,365]` logits）。不需要安装 Caffe：脚本直接解析 caffemodel 的 protobuf 权重
与 prototxt 的网络结构，重建等价的 ONNX 图（Conv/ReLU/LRN/Pooling/Concat/Gemm）。

**用法**：
```powershell
python caffe_places365_to_onnx.py <model.caffemodel> <deploy.prototxt> <output.onnx>
# 例：
python caffe_places365_to_onnx.py googlenet_places365.caffemodel \
       models/scene/deploy_googlenet_places365.prototxt \
       models/scene/places365_googlenet.onnx
```

**注意事项**：
- GoogLeNet 的池化使用 `ceil_mode=true`，转换脚本已正确映射到 ONNX 的 `ceil_mode`。
- 图在 `loss3/classifier`（365 logits，softmax 前）处截断，与引擎的输入约定一致。
- 当前 `models/scene/places365_googlenet.onnx` 即由此脚本生成。

---

## 推荐工作流

```powershell
# 1) 基座模型（以当前 OIV7 为例）
python export_yolov8.py yolov8m-oiv7.pt models/base/yolov8m-oiv7/model.onnx
#    （手工写好 models/base/yolov8m-oiv7/meta.json 与 classes.json）

# 2) 场景识别模型（可选，缺失时场景宏优雅降级）
python caffe_places365_to_onnx.py googlenet_places365.caffemodel \
       models/scene/deploy_googlenet_places365.prototxt \
       models/scene/places365_googlenet.onnx
#    （并把 categories_places365.txt 放入 models/scene/）
#    或：python export_places365.py resnet18_places365.pth.tar models/scene/places365_googlenet.onnx

# 3) 引擎使用
build\dsl.exe --list-models     # 检查模型是否被注册
build\dsl.exe test.dsl          # 执行检索
```
