#!/usr/bin/env python
"""Convert an official Places365 checkpoint into an ONNX model for the engine.

Two supported sources:

A) PyTorch ResNet checkpoints (resnet18_places365.pth.tar / resnet50_places365.pth.tar):
    python export_places365.py resnet18_places365.pth.tar models/scene/places365_resnet18.onnx

B) Caffe GoogLeNet (googlenet_places365.caffemodel + deploy prototxt):
    python caffe_places365_to_onnx.py googlenet_places365.caffemodel \
           models/scene/deploy_googlenet_places365.prototxt \
           models/scene/places365_resnet18.onnx

Input contract the engine expects:
  * input  : [1, 3, 224, 224] RGB, ImageNet-normalized: (x/255 - mean)/std
             with mean = [0.485, 0.456, 0.406], std = [0.229, 0.224, 0.225]
  * output : [1, 365] logits (one per Places365 scene, in the same order as
             categories_places365.txt)

The official checkpoints ship as torch.load pickles.  Download from:
    http://places2.csail.mit.edu/models_places365/resnet18_places365.pth.tar
    http://places2.csail.mit.edu/models_places365/resnet50_places365.pth.tar
    http://places2.csail.mit.edu/models_places365/googlenet_places365.caffemodel
    http://places2.csail.mit.edu/models_places365/deploy_googlenet_places365.prototxt

Categories (place next to the model):
    https://raw.githubusercontent.com/csailvision/places365/master/categories_places365.txt
"""
import sys
import torch
import torch.nn as nn
import torch.onnx

try:
    import torchvision.models as models
except ImportError:
    print("torchvision is required to rebuild the model structure.")
    sys.exit(1)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    src = sys.argv[1]
    dst = sys.argv[2]

    # num_classes = 365 (Places365)
    arch = "resnet18" if "resnet18" in dst.lower() or "resnet18" in src.lower() else "resnet50"
    if "resnet50" in dst.lower() or "resnet50" in src.lower():
        arch = "resnet50"
    print("architecture:", arch)

    checkpoint = torch.load(src, map_location="cpu")
    state = checkpoint.get("state_dict", checkpoint)
    state = {k.replace("module.", ""): v for k, v in state.items()}

    model = models.resnet18(num_classes=365) if arch == "resnet18" else models.resnet50(num_classes=365)
    model.load_state_dict(state)
    model.eval()

    x = torch.randn(1, 3, 224, 224)
    with torch.no_grad():
        out = model(x)
    print("output shape:", tuple(out.shape))
    assert out.dim() == 2 and out.size(1) == 365, f"unexpected output shape {tuple(out.shape)}"

    torch.onnx.export(
        model, x, dst,
        opset_version=11,
        input_names=["input"],
        output_names=["output"],
        dynamic_axes={"input": {0: "batch_size"}},
    )
    print("saved:", dst)


if __name__ == "__main__":
    main()
