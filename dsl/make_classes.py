#!/usr/bin/env python
"""Generate classes.json (COCO-80 classes + parent chain) for a base model package.

Usage: make_classes.py <output.json>
"""
import json
import sys

# COCO 80 output classes, in the model's output-index order (index -> class name).
COCO = [
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat",
    "traffic light", "fire hydrant", "stop sign", "parking meter", "bench",
    "bird", "cat", "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra", "giraffe",
    "backpack", "umbrella", "handbag", "tie", "suitcase",
    "frisbee", "skis", "snowboard", "sports ball", "kite", "baseball bat",
    "baseball glove", "skateboard", "surfboard", "tennis racket",
    "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl",
    "banana", "apple", "sandwich", "orange", "broccoli", "carrot",
    "hot dog", "pizza", "donut", "cake",
    "chair", "couch", "potted plant", "bed", "dining table", "toilet",
    "tv", "laptop", "mouse", "remote", "keyboard", "cell phone",
    "microwave", "oven", "toaster", "sink", "refrigerator",
    "book", "clock", "vase", "scissors", "teddy bear", "hair drier", "toothbrush",
]

PARENT = {
    "person": "root",
    "bicycle": "vehicle", "car": "vehicle", "motorcycle": "vehicle",
    "airplane": "vehicle", "bus": "vehicle", "train": "vehicle",
    "truck": "vehicle", "boat": "vehicle",
    "traffic light": "infrastructure", "fire hydrant": "infrastructure",
    "stop sign": "infrastructure", "parking meter": "infrastructure",
    "bench": "furniture",
    "bird": "animal", "cat": "animal", "dog": "animal", "horse": "animal",
    "sheep": "animal", "cow": "animal", "elephant": "animal", "bear": "animal",
    "zebra": "animal", "giraffe": "animal",
    "backpack": "accessory", "umbrella": "accessory", "handbag": "accessory",
    "tie": "accessory", "suitcase": "accessory",
    "frisbee": "sports_equipment", "skis": "sports_equipment",
    "snowboard": "sports_equipment", "sports ball": "sports_equipment",
    "kite": "sports_equipment", "baseball bat": "sports_equipment",
    "baseball glove": "sports_equipment", "skateboard": "sports_equipment",
    "surfboard": "sports_equipment", "tennis racket": "sports_equipment",
    "bottle": "container", "wine glass": "container", "cup": "container",
    "bowl": "container",
    "fork": "utensil", "knife": "utensil", "spoon": "utensil",
    "banana": "fruit", "apple": "fruit", "orange": "fruit",
    "sandwich": "food", "hot dog": "food", "pizza": "food",
    "donut": "food", "cake": "food",
    "broccoli": "vegetable", "carrot": "vegetable",
    "chair": "furniture", "couch": "furniture", "potted plant": "flower",
    "bed": "furniture", "dining table": "furniture", "toilet": "furniture",
    "tv": "electronics", "laptop": "electronics", "mouse": "electronics",
    "remote": "electronics", "keyboard": "electronics", "cell phone": "electronics",
    "microwave": "appliance", "oven": "appliance", "toaster": "appliance",
    "sink": "appliance", "refrigerator": "appliance", "hair drier": "appliance",
    "book": "object", "clock": "object", "vase": "object",
    "scissors": "object", "toothbrush": "object",
    "teddy bear": "toy",
}

# Nested parent entries (parents that have their own parents).
PARENT_PARENT = {
    "animal": "living_thing",
    "fruit": "food",
    "vegetable": "food",
    "food": "consumable",
    "flower": "plant",
    "vehicle": "machine",
    "electronics": "machine",
    "appliance": "machine",
    "machine": "root",
    "furniture": "object",
    "container": "object",
    "utensil": "object",
    "accessory": "object",
    "sports_equipment": "object",
    "infrastructure": "object",
    "toy": "object",
    "living_thing": "root",
    "plant": "root",
    "consumable": "root",
    "object": "root",
}


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "classes.json"
    classes = []
    for name in COCO:
        classes.append({"name": name, "parent": PARENT.get(name, "root")})
    for name, parent in PARENT_PARENT.items():
        if name not in PARENT:  # don't duplicate output classes
            classes.append({"name": name, "parent": parent})
    with open(out, "w", encoding="utf-8") as f:
        json.dump({"classes": classes}, f, ensure_ascii=False, indent=2)
    print(f"saved {out} ({len(classes)} entries)")


if __name__ == "__main__":
    main()
