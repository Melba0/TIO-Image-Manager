# Places365 场景识别模型 / Scene Recognition Model

Places365（MIT）场景识别模型。用于在缓存预处理阶段为每张图片计算 365 类场景概率
（`scene_vector`）、主场景（`dominant_scene`）与室内概率（`indoor_score`），
并通过 DSL 宏 `img_scene("beach")` / `img_scene_top()` / `img_is_indoor()` 检索。

## 当前模型：GoogLeNet（Caffe 官方权重）

本目录已包含三个文件：

| 文件 | 说明 |
|------|------|
| `places365_googlenet.onnx` | GoogLeNet-Places365 转换后的 ONNX（输入 224×224，输出 365 logits） |
| `categories_places365.txt` | 官方 365 行场景名（`/b/beach 48` 格式） |
| `deploy_googlenet_places365.prototxt` | Caffe 网络定义（转换依据，运行时不需要） |
| `meta.json` | `{ "name": "Places365-GoogLeNet", "classes": 365, "input_size": 224 }` |

转换方式：`caffe_places365_to_onnx.py`（见下）把官方
`googlenet_places365.caffemodel` + `deploy_googlenet_places365.prototxt`
转换为 ONNX。ONNX 图在 `loss3/classifier` 处截断（365 维 logits，softmax 前）。

## 备选：ResNet18 / ResNet50（PyTorch 检查点）

如果改用 ResNet 系列，用 `dsl/export_places365.py` 从官方 `.pth.tar` 导出（注意：
导出文件名需为引擎期望的 `places365_googlenet.onnx` 或同时修改 `main.cpp`
中的模型路径）：

```powershell
python export_places365.py resnet18_places365.pth.tar models/scene/places365_googlenet.onnx
```

并把 `categories_places365.txt` 放到本目录即可（`meta.json` 的 `name` 可相应更新）。

## 模型输入输出约定

- 输入：`[1, 3, 224, 224]` RGB，ImageNet 归一化 `(x/255 - mean)/std`
  （`mean=[0.485,0.456,0.406]`，`std=[0.229,0.224,0.225]`）
- 输出：`[1, 365]` logits，顺序与 `categories_places365.txt` 一一对应
- 前 205 个类别为室内，后 160 个为室外；`indoor_score` = 前 205 类概率之和

> 引擎按"取 `/x/name` 的最后一段、去掉行尾序号"解析类别名，因此 DSL 里用
> `img_scene("beach")`（而非 `/b/beach`）。

## 缺失时的行为

模型/类别文件缺失时引擎不崩溃：状态栏提示场景识别不可用，场景宏返回 0.0 / 空字符串，
缓存中不写入场景字段（或全零向量）。
