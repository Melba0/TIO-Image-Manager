#!/usr/bin/env python
"""Convert an ultralytics YOLOv8 checkpoint (e.g. yolov8m.pt) into an ONNX
model consumable by the C++ engine (ONNX Runtime).

The engine expects an ONNX model with input  [1, 3, 640, 640] RGB (0..1) and
output (1, 4+nc, total_anchors):
  * rows 0..3    : box coordinates [x1, y1, x2, y2] ALREADY decoded to the
                   input (letterboxed) pixel space (DFL -> dist2bbox ->
                   stride scaling)
  * rows 4..4+nc : class scores, already sigmoided

This matches the format produced by `yolo export model=model.pt format=onnx
imgsz=640`, so models exported either way are interchangeable.

The original .pt is a pickle referencing ultralytics classes.  We provide
drop-in plain nn.Module implementations so pickle can restore the exact
weights/structure, then trace the model to ONNX.
"""
import pickle
import sys
import torch
import torch.nn as nn


# ---------------------------------------------------------------------------
# Plain PyTorch re-implementations of the ultralytics modules used in the
# checkpoint.  __init__ is never called during unpickling: the saved module
# tree (parameters/buffers/submodules) is restored directly by pickle, so the
# forward() bodies below only rely on attributes that exist in the checkpoint.
# ---------------------------------------------------------------------------
class Conv(nn.Module):
    """conv + bn + act (SiLU)."""

    def forward(self, x):
        return self.act(self.bn(self.conv(x)))


class DWConv(Conv):
    """Depthwise conv (same structure as Conv)."""

    pass


class Concat(nn.Module):
    def forward(self, x):
        return torch.cat(x, 1)


class Bottleneck(nn.Module):
    def forward(self, x):
        return x + self.cv2(self.cv1(x)) if self.add else self.cv2(self.cv1(x))


class C2f(nn.Module):
    """cv1 -> chunk2, m(...) bottlenecks, concat -> cv2."""

    def forward(self, x):
        y = list(self.cv1(x).chunk(2, 1))
        y.extend(m(y[-1]) for m in self.m)
        return self.cv2(torch.cat(y, 1))


class SPPF(nn.Module):
    """Spatial pyramid pooling - fast."""

    def forward(self, x):
        y = [self.cv1(x)]
        y.extend(self.m(y[-1]) for _ in range(3))
        return self.cv2(torch.cat(y, 1))


class DFL(nn.Module):
    """Distribution Focal Loss decoder: softmax distribution -> box distance."""

    def forward(self, x):
        b, _, a = x.shape  # x: (b, 4*c1, a)
        return self.conv(x.view(b, 4, self.c1, a).transpose(2, 1).softmax(1)).view(b, 4, a)


class Detect(nn.Module):
    """Detection head (ONNX export).

    Outputs (1, 4+nc, total_anchors): rows are
    [x1, y1, x2, y2, cls_scores...] with boxes decoded to the input
    (letterboxed) pixel space, matching `yolo export format=onnx`.
    """

    def forward(self, x):
        b = x[0].shape[0]
        outs = []
        for i, feat in enumerate(x):
            h, w = feat.shape[2], feat.shape[3]
            hw = h * w
            stride = float(self.stride[i])
            # anchor centres in input pixels
            yv, xv = torch.meshgrid(torch.arange(h), torch.arange(w), indexing="ij")
            cx = ((xv.float() + 0.5) * stride).reshape(1, 1, hw)
            cy = ((yv.float() + 0.5) * stride).reshape(1, 1, hw)
            y = torch.cat((self.cv2[i](feat), self.cv3[i](feat)), 1).view(b, self.no, hw)
            box, cls = y.split((self.reg_max * 4, self.nc), 1)
            dist = self.dfl(box)  # [b,4,hw] ltrb distances in grid units
            x1 = cx - dist[:, 0:1, :] * stride
            y1 = cy - dist[:, 1:2, :] * stride
            x2 = cx + dist[:, 2:3, :] * stride
            y2 = cy + dist[:, 3:4, :] * stride
            outs.append(torch.cat((x1, y1, x2, y2, cls.sigmoid()), 1))
        return torch.cat(outs, 2)


class DetectionModel(nn.Module):
    """Top-level model: Sequential of layers wired by each layer's .f field."""

    def forward(self, x):
        y = []
        for m in self.model:
            if m.f != -1:
                if isinstance(m.f, int):
                    x = y[m.f]
                else:
                    x = [x if j == -1 else y[j] for j in m.f]
            x = m(x)
            y.append(x)
        return x


# map original ultralytics fully-qualified names to our implementations
CLASS_MAP = {
    "ultralytics.nn.tasks.DetectionModel": DetectionModel,
    "ultralytics.nn.modules.conv.Conv": Conv,
    "ultralytics.nn.modules.conv.DWConv": DWConv,
    "ultralytics.nn.modules.conv.Concat": Concat,
    "ultralytics.nn.modules.block.Bottleneck": Bottleneck,
    "ultralytics.nn.modules.block.C2f": C2f,
    "ultralytics.nn.modules.block.SPPF": SPPF,
    "ultralytics.nn.modules.block.DFL": DFL,
    "ultralytics.nn.modules.head.Detect": Detect,
    "ultralytics.nn.modules.Detect": Detect,  # older checkpoints
}


class Stub(nn.Module):
    pass


class TorchUnpickler(pickle.Unpickler):
    def find_class(self, module, name):
        key = f"{module}.{name}"
        if key in CLASS_MAP:
            return CLASS_MAP[key]
        if module.startswith("ultralytics"):
            print(f"WARN: no mapping for {key}, using Stub", file=sys.stderr)
            return Stub
        return super().find_class(module, name)


class MyPickleModule:
    Unpickler = TorchUnpickler


def load_checkpoint(path):
    return torch.load(
        path, map_location="cpu", weights_only=False, pickle_module=MyPickleModule
    )


def main():
    # Usage: export_yolov8.py <source.pt> <output.onnx>
    src = sys.argv[1]
    dst = sys.argv[2]

    obj = load_checkpoint(src)
    model = obj["model"]
    model.eval()
    model.float()

    sd = model.state_dict()
    print(f"state_dict entries: {len(sd)}")

    # forward sanity on a fixed 640x640 input
    x = torch.randn(1, 3, 640, 640)
    with torch.no_grad():
        out = model(x)
    print("forward output:", tuple(out.shape))
    assert out.dim() == 3 and out.size(1) >= 4, f"unexpected output shape {tuple(out.shape)}"

    torch.onnx.export(
        model, x, dst,
        opset_version=12,
        input_names=["images"],
        output_names=["output0"],
    )
    print("saved:", dst)


if __name__ == "__main__":
    main()
