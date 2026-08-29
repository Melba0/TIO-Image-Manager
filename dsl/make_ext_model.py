#!/usr/bin/env python
"""Generate a tiny synthetic TorchScript classifier for extension packs.

Usage: make_ext_model.py <num_classes> <output.pt>
The model takes a 224x224 RGB image (0-1) and outputs logits [1, num_classes].
"""
import sys
import torch
import torch.nn as nn


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 3
    out = sys.argv[2] if len(sys.argv) > 2 else "model.pt"

    class Net(nn.Module):
        def __init__(self, num_classes):
            super().__init__()
            self.conv = nn.Sequential(
                nn.Conv2d(3, 16, 3, 2, 1), nn.ReLU(),
                nn.Conv2d(16, 32, 3, 2, 1), nn.ReLU(),
                nn.Conv2d(32, 64, 3, 2, 1), nn.ReLU(),
                nn.Conv2d(64, 128, 3, 2, 1), nn.ReLU(),
            )
            self.fc = nn.Linear(128 * 14 * 14, num_classes)

        def forward(self, x):
            return self.fc(self.conv(x).flatten(1))

    model = Net(n)
    model.eval()
    traced = torch.jit.trace(model, torch.randn(1, 3, 224, 224))
    traced.save(out)
    print(f"saved {out} ({n} classes)")


if __name__ == "__main__":
    main()
