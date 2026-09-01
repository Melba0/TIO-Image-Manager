# Base Model Pack JSON Format

[**English**](base_model_pack_format.md) ・ [**中文**](base_model_pack_format_zh.md)

A base model pack is the smallest distributable unit of a base object-detection model. It lives
in `models/base/<pack-name>/` and consists of **three required files**:



```
models/base/<name>/
├── model.onnx   # ONNX model (must exist, otherwise the pack is not registered)
├── meta.json    # model metadata (type, input size, number of classes)
└── classes.json # output classes + inheritance (is-a) chains
```

> The pack name = directory name (the id used by `active_base` in `models/registry.json`).

---

## 1. `meta.json`

| Field | Type | Required | Notes |
|-------|------|----------|-------|
| `name` | string | recommended | pack display name (the engine uses the directory name as the id) |
| `type` | string | no | default `"detector"`; only detectors are currently supported |
| `input_size` | int | no | inference side length, default `640` (size after letterbox feeding the network) |
| `classes` | int | no | number of output classes, default `80` |

Example (`models/base/yolov8m-oiv7/meta.json`):

```json
{
  "name": "yolov8m-oiv7",
  "type": "detector",
  "input_size": 640,
  "classes": 601
}
```

Engine read logic (`ModelRegistry::scanBaseModels`):

```cpp
info.type        = j.value("type", "detector");
info.input_size  = j.value("input_size", 640);
info.classes     = j.value("classes", 80);
```

---

## 2. `classes.json`

Defines the model's output classes and their parent (is-a) relationships. The format is a
top-level `"classes"` array:

```json
{
  "classes": [
    { "name": "person",   "parent": "root" },
    { "name": "car",      "parent": "vehicle" },
    { "name": "vehicle",  "parent": "machine" },
    { "name": "machine",  "parent": "root" }
  ]
}
```

### Field rules

| Field | Type | Notes |
|-------|------|-------|
| `name` | string | class name (UTF-8 supported, e.g. Chinese). **The first `classes` entries map to the model's output channel indices in array order**, i.e. the `[0, classes)` range returned by `getOutputClassNames()` |
| `parent` | string | parent class name. Defaults to `"root"` when missing; `"root"` is the universal ancestor (`isChildOf(child, "root")` is always true) |

### Important conventions

- **Output classes must come first**: the engine truncates to the first `min(classes,
  classes.json entry count)` entries as output-indexed class names; extra entries are treated
  as parent definitions (they do not consume output channels).
- Parents can be any depth; multi-level inheritance is supported, e.g.
  `car -> vehicle -> machine -> root`.
- The engine guards against cycles; `isChildOf` returns `false` when a cycle is found.

### Generation

The current base model is Open Images V7 (`yolov8m-oiv7`, 601 output classes). Class names are
exported directly from the checkpoint's `model.names`; parent chains come from the official
Open Images hierarchy (`bbox_labels_600_hierarchy.json`); both are lowercased to match the DSL
class convention (e.g. `cat`, `fruit`, `vehicle`). Parents such as `fruit`, `food`, `animal`,
`vehicle` are themselves output classes, so `classes.json` needs no extra parent entries.

When adding a new model, export the ONNX (e.g. with `yolo export`) and hand-write `classes.json`
per the field rules above (or generate it with any script from `model.names` + the OI hierarchy).

---

## 3. `model.onnx` (ONNX output format)

`model.onnx` is an ONNX model **exported at a fixed input size (640×640)**, input `[1,3,640,640]`
RGB tensor (values 0–1, letterbox preprocessing), output:

```
(1, 4 + nc, total_anchors)
```

Dimension semantics (decoded by `YoloInference::postprocess`):

- **Rows 0–3**: `[x1, y1, x2, y2]` — bounding-box coordinates, **already decoded into the
  letterboxed input-pixel space** (the model internally performs DFL decoding + dist2bbox +
  stride scaling).
- **Row 4 onward**: `nc` class scores **after sigmoid**.
- The anchor count / whether output is 8400 is irrelevant: decoding no longer depends on grid
  coordinates.

The engine only performs the un-letterbox mapping:

```
ux1 = (x1 - pad_x) / scale
uy1 = (y1 - pad_y) / scale
ux2 = (x2 - pad_x) / scale
uy2 = (y2 - pad_y) / scale
```

> This is a format requirement **at export time**, unrelated to the pretrained weights: the
> export script rewrites the detection head to directly output an `[x1, y1, x2, y2, cls...]`
> tensor (YOLOv8 first solves the four edge distances via DFL, then multiplies by stride to
> pixels), consistent with the ultralytics `yolo export format=onnx` output.

### Conversion tools

Use the official ultralytics CLI to export a YOLOv8 checkpoint to the above format:

```powershell
yolo export model=yolov8m-oiv7.pt format=onnx opset=12 imgsz=640
```

After export, delete the old `model.pt` and place the ONNX file as `model.onnx`.

---

## 4. Registration: `models/registry.json`

`active_base` decides which base model pack is used:

```json
{
  "active_base": "yolov8m-oiv7",
  "active_extensions": ["botany_v1", "person_parts_v1"]
}
```

- `active_base` must match a directory name under `models/base/`; otherwise the engine prints a
  warning and falls back to the first available pack in `models/base/`.
- The engine reads it once at startup; `/reload` hot-reloads it in interactive mode (the cache
  is automatically rebuilt after switching models).

---

## 5. Validation rules (at engine startup)

Hard conditions for a subdirectory under `models/base/` to be registered:

1. `model.onnx` exists;
2. `meta.json` exists and parses (on failure, prints `[Registry] Failed to parse ...` and skips
   the pack).

A missing `classes.json` is not an error, but the model then has no class definitions (all class
name lookups return empty). If the model file is missing, the engine prints the export hint
command.
