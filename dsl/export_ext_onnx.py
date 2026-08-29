import sys
import torch

# Export an extension pack's TorchScript classifier (traced conv+fc) to ONNX.
# Usage: export_ext_onnx.py <model.pt> <model.onnx> <input_size> <num_classes_hint>

src = sys.argv[1]
dst = sys.argv[2]
input_size = int(sys.argv[3]) if len(sys.argv) > 3 else 224

model = torch.jit.load(src, map_location="cpu")
model.eval()
model.float()

x = torch.randn(1, 3, input_size, input_size)
with torch.no_grad():
    out = model(x)
print("input:", tuple(x.shape), "output:", tuple(out.shape))

torch.onnx.export(
    model, x, dst,
    opset_version=12,
    input_names=["images"],
    output_names=["output0"],
)
print("saved:", dst)
