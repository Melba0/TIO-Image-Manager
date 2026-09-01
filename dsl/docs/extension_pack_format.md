# Extension Pack JSON Format

[**English**](extension_pack_format.md) ・ [**中文**](extension_pack_format_zh.md)

An extension pack performs **fine-grained secondary analysis** on the objects detected by the
base model: the region of a parent-class object is cropped and fed to a dedicated small model to
produce "child" objects (e.g. `person` → `head/torso/arm/leg`).

V2 additionally supports **clustering / embedding packs**: the model outputs a fixed-dimension
feature vector (embedding). During the cache build the engine automatically extracts an
embedding for each parent-class object and runs DBSCAN clustering over the whole library; the
results (`cluster_ids` plus each image's `cluster_groups`) persist into `cache_index.json` and
drive the GUI's left "grouped view" display and renaming.

Extension packs live under `models/extensions/<pack-name>/` and consist of **two required files**:

```
models/extensions/<name>/
├── config.json   # pack config (required; otherwise the pack is not registered)
└── model.onnx    # ONNX model (classifier / detector / embedding model)
```

> The pack name = directory name (also the id used in `registry.json` `active_extensions`).
> Compatibility: the engine scans both `models/extensions/` and a project-root `extensions/`
> (the former wins on name conflicts).

---

## 1. `config.json`

### 1.1 Detection / classification extension packs (V1)

Example (`models/extensions/botany_v1/config.json`):

```json
{
  "name": "botany_v1",
  "parent_class": "flower",
  "children": ["petal", "stamen", "stem"],
  "model_path": "model.onnx",
  "input_size": 224,
  "conf_threshold": 0.15,
  "crop_padding": 0.1,
  "is_classifier": true
}
```

### 1.2 Clustering / embedding extension packs (V2)

Example (`models/extensions/face_recognition_v1/config.json`):

```json
{
  "name": "face_recognition_v1",
  "parent_class": "person",
  "children": ["face"],
  "model_path": "model.onnx",
  "input_size": 112,
  "conf_threshold": 0.0,
  "crop_padding": 0.1,
  "is_classifier": false,
  "input_normalize": "imagenet",
  "capabilities": {
    "can_extract_embedding": true,
    "embedding_name": "face",
    "can_cluster": true,
    "cluster_name": "face_cluster",
    "cluster_threshold": 0.55
  },
  "gui": {
    "group_label": "People",
    "show_in_sidebar": true,
    "icon": "👤"
  }
}
```

- For a clustering pack, `>>` degrades to the identity transform (no child objects are
  produced); embedding extraction is done by the cache pipeline at build time.

### Field reference

| Field | Type | Required | Default | Notes |
|-------|------|----------|---------|-------|
| `name` | string | no | directory name | pack id (the directory name is authoritative) |
| `parent_class` | string | recommended | empty | parent class triggering refinement. When the base model detects an object of this class (or a subclass), the extension runs / an embedding is extracted |
| `children` | array[string] | recommended | empty | child class names the extension model can output (for DSL aggregation / UI display) |
| `model_path` | string | **yes** | none | path to `model.onnx`. **Relative paths are resolved against the pack's own directory** (i.e. `<pack-dir>/model.onnx`); absolute paths also work. If empty, the pack is skipped |
| `input_size` | int | no | `224` | side length the crop is resized to (before feeding the extension model) |
| `conf_threshold` | float | no | `0.3` | lower bound for child-detection confidence (usually ignored for clustering packs; set 0) |
| `crop_padding` | float | no | `0.1` | ratio by which the parent object's bbox is enlarged (compensates crop errors) |
| `is_classifier` | bool | no | `false` | `true` = classifier output; `false` = detector output |
| `input_normalize` | string | no | `""` | input preprocessing: `""`/`"none"` = raw 0–1; `"imagenet"` = per-channel normalization with ImageNet mean/variance (common for face models) |
| `capabilities.can_extract_embedding` | bool | no | `false` | the model outputs a fixed-dimension embedding (e.g. MobileFaceNet 512/128 dims) |
| `capabilities.embedding_name` | string | no | empty | key under which the embedding is stored on the object: `embeddings[<embedding_name>]` |
| `capabilities.can_cluster` | bool | no | `false` | this pack drives a global clustering process (DBSCAN) |
| `capabilities.cluster_name` | string | no | empty | clustering label; written to object `cluster_ids[<cluster_name>]` and each image's `cluster_groups[<cluster_name>]` |
| `capabilities.cluster_threshold` | float | no | `0.55` | cosine similarity threshold (0–1); two embedding dot products ≥ threshold are in the same cluster |
| `gui.group_label` | string | no | empty | left-sidebar group branch title (e.g. "People") |
| `gui.show_in_sidebar` | bool | no | `true` | whether to generate a grouped view in the left sidebar |
| `gui.icon` | string | no | empty | group branch prefix icon (e.g. "👤") |

### Engine read logic (`ExtensionManager::scan`)

```cpp
pack.name             = j.value("name", directory name);
pack.parent_class     = j.value("parent_class", "");
pack.model_path       = j.value("model_path", "");
pack.input_size       = j.value("input_size", 224);
pack.conf_threshold   = j.value("conf_threshold", 0.3f);
pack.crop_padding     = j.value("crop_padding", 0.1f);
pack.is_classifier    = j.value("is_classifier", false);
pack.input_normalize  = j.value("input_normalize", "");
pack.can_extract_embedding = cap.value("can_extract_embedding", false);
pack.embedding_name   = cap.value("embedding_name", "");
pack.can_cluster      = cap.value("can_cluster", false);
pack.cluster_name     = cap.value("cluster_name", "");
pack.cluster_threshold= cap.value("cluster_threshold", 0.55f);
pack.gui_group_label  = g.value("group_label", "");
pack.gui_show_in_sidebar = g.value("show_in_sidebar", true);
pack.gui_icon         = g.value("icon", "");
```

---

## 2. `model.onnx` (extension model output format)

The extension model input is always an RGB tensor `[1, 3, input_size, input_size]` cropped and
resized to `input_size`. Values are 0–1 (unless `input_normalize: "imagenet"` is configured).

### Classifier (`is_classifier: true`)

Outputs `[1, nc]` (or `[nc]`) **logits** (softmax is applied inside the engine), corresponding
in order to the `children` array:

```
logits = model(crop)   # (1, nc)
```

Each child's score = `softmax(logits)[0][i]`; the class with `max` score and `>= conf_threshold`
becomes the child object's class; the child object position is the parent object's bbox.

### Detector (`is_classifier: false`)

Same detection output format as the base model pack (see §3 of the Base Model Pack Format):

```
(1, 4 + nc, total_anchors)
```

- Rows 0–3: `[x1, y1, x2, y2]`, coordinates in crop pixels;
- Row 4 onward: class scores after sigmoid;
- Child coordinates are mapped from crop coordinates back to whole-image coordinates.

### Embedding model (clustering pack)

Outputs a one-dimensional vector `[1, D]` or `[D]` (e.g. MobileFaceNet 128/512 dims). The engine
L2-normalizes it and stores it in `embeddings[embedding_name]` on the object; clustering uses
cosine similarity (dot product) with DBSCAN.

### Generation tool

`mobilefacenet.pt` (PyTorch weights) can be converted to ONNX with PyTorch first:
`torch.onnx.export(model, x, "model.onnx", input_names=["input"], output_names=["output"])`,
then placed at `models/extensions/<name>/model.onnx` (the `.pt` may be deleted).

---

## 3. Registration: `models/registry.json`

`active_extensions` decides which packs are activated (inactive packs do not participate in
`>>`; clustering packs do not extract embeddings either):

```json
{
  "active_base": "yolov8m-oiv7",
  "active_extensions": ["botany_v1", "person_parts_v1", "face_recognition_v1"]
}
```

- Only packs present in this array are enabled by `ExtensionManager::setActiveExtensions`.

---

## 4. Execution Flow

### 4.1 `>>` refinement (detection / classification packs)

1. Process each object in the current ObjectSet whose `class` equals `parent_class` (or a
   subclass);
2. Enlarge the bbox by `crop_padding` and crop/resize to `input_size × input_size`;
3. Run the extension model to obtain child objects (detectors map back to whole-image
   coordinates);
4. Child objects carry `parent_id` (pointing at the parent) and `score`, and are returned for
   further DSL operations (e.g. `^ parts` lifts back to images).

> A clustering pack degrades to the identity transform under `>>`: the cache pipeline already
> attached the embedding / cluster_ids to the objects at build time.

### 4.2 Clustering pipeline (clustering packs)

1. During cache build (`CacheManager::inferEntry`), run the embedding model on every object
   matching `parent_class`, store the L2-normalized embedding on the object and persist it to
   `cache_index.json`;
2. After the full/incremental build, `CacheManager::runClustering` runs for every active pack
   with `capabilities.can_cluster`:
   - collects all embeddings of that `embedding_name` across the library, sorted by
     (path, obj_id) for determinism;
   - DBSCAN (minPts=1, cosine threshold `cluster_threshold`) assigns clusters;
   - cluster id format: `<cluster_name>_<parent_class>_<NNN>` (e.g. `face_cluster_person_001`);
   - writes back each object's `cluster_ids[<cluster_name>]` and rebuilds each image's
     `cluster_groups[<cluster_name>]`;
3. The GUI auto-generates a branch on the left (title = `gui.icon + gui.group_label`), each
   cluster being a clickable item (shows mapped name / raw id + photo count); clicking shows that
   group's photos, double-click opens a rename dialog (written to
   `config/cluster_name_mappings.json`, e.g. `"face_cluster_person_001": "Zhang San"`).

### DSL macros (clustering packs)

- `cluster_id(obj, "<cluster_name>")` → the object's cluster id (string; empty when unassigned);
- `cluster_sim(a, b, "<cluster_name>")` → whether two objects are in the same cluster (fuzzy 1/0).

```dsl
# images belonging to face_cluster cluster with id person_001
$ : (any(cluster_id(obj, "face_cluster") == "face_cluster_person_001"))
# two images contain objects in the same cluster
$ : (any(cluster_sim(obj, obj, "face_cluster")))
```

### Dependency notes

- If `parent_class` is not in the active base model's `classes.json`, the GUI shows a ⚠ warning;
  the engine returns an empty result for `>>` (no crash).
- If the clustering pack's model is missing (`model.onnx` not downloaded), extraction fails
  gracefully: no embeddings, no groups, no error.
- Extension packs are decoupled from the base model: after switching `active_base`, no pack
  changes are needed as long as the new base still contains `parent_class`.
