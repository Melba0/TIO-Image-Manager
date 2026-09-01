# Places365 Scene Recognition Model

[**English**](README.md) ・ [**中文**](README_zh.md)

The Places365 (MIT) scene-recognition model. During the cache pre-processing phase it computes, for
each image, the 365-class scene probability (`scene_vector`), the dominant scene
(`dominant_scene`) and the indoor probability (`indoor_score`), searchable via the DSL macros
`img_scene("beach")` / `img_scene_top()` / `img_is_indoor()`.

## Current model: GoogLeNet (official Caffe weights)

This directory already contains the files:

| File | Description |
|------|-------------|
| `places365_googlenet.onnx` | GoogLeNet-Places365 converted ONNX (input 224×224, output 365 logits) |
| `categories_places365.txt` | official 365 scene names (`/b/beach 48` format) |
| `deploy_googlenet_places365.prototxt` | Caffe network definition (conversion reference; not needed at runtime) |
| `meta.json` | `{ "name": "Places365-GoogLeNet", "classes": 365, "input_size": 224 }` |

The conversion takes the official `googlenet_places365.caffemodel` +
`deploy_googlenet_places365.prototxt` and produces the ONNX graph truncated at
`loss3/classifier` (365-dim logits, before softmax).

## Alternative: ResNet18 / ResNet50 (PyTorch checkpoints)

If you switch to a ResNet variant, export it from the official `.pth.tar` with `torch.onnx`
(note: export to the engine-expected filename `places365_googlenet.onnx`, or change the model
path in `main.cpp`):

```powershell
python -c "import torch,torch.onnx; ..."   # export resnet18_places365.pth.tar to places365_googlenet.onnx
```

Then place `categories_places365.txt` in this directory (you may update the `name` in
`meta.json`).

## Model input/output conventions

- Input: `[1, 3, 224, 224]` RGB, ImageNet normalization `(x/255 - mean)/std`
  (`mean=[0.485,0.456,0.406]`, `std=[0.229,0.224,0.225]`)
- Output: `[1, 365]` logits, in 1:1 order with `categories_places365.txt`
- The first 205 classes are indoor, the last 160 outdoor; `indoor_score` = sum of the first 205
  class probabilities

> The engine parses class names by taking the last segment of `/x/name` and stripping the trailing
> index, so DSL uses `img_scene("beach")` (not `/b/beach`).

## Behavior when missing

If the model/category files are missing the engine does not crash: the status bar notes scene
recognition is unavailable, scene macros return 0.0 / empty string, and the cache is written
without the scene fields (or with an all-zero vector).
