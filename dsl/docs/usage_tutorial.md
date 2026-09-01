# Usage Tutorial

[**English**](usage_tutorial.md) ・ [**中文**](usage_tutorial_zh.md)

This tutorial walks through the practical use of each interpreter feature: first run,
incremental cache, model registry, inheritance queries, tag pre-filtering, asset management,
albums, clustering, and working with the desktop GUI.

## 1. First Run & Incremental Cache

On startup the program scans the images under `tio/photo/` and runs YOLO inference with the
currently active base model.

```powershell
cd dsl
build\dsl.exe
```

Example startup log:

```
[Main] active base model: yolov8m-oiv7 (C:\...\dsl\models\base\yolov8m-oiv7\model.onnx)
[Main] photo dir: C:\...\tio\photo
[Main] thresholds: base_conf=0.25 iou=0.45 fallback=0
[Cache] Loaded 128 images from cache index.
[Cache] Cache is up to date (128 images).
```

- **First run** (no cache index): YOLO inference on every image (a few minutes on CPU), results
  written to `cache/yolov8m-oiv7/cache_index.json`.
- **Subsequent runs**: scan the gallery, compare `mtime`/`size`, **only re-infer added/modified
  images**, prune deleted ones; if nothing changed, load the index directly (sub-second, no model
  load).
- **Cache invalidation**: rebuild is triggered when:
  - image files are added / deleted / modified;
  - the cache format version changes;
  - **the base model changes** (caches are per-model subdirectories).

### Cache contents (cache_index.json, version 1.2)

```json
{
  "version": "1.2",
  "model_name": "yolov8m-oiv7",
  "next_obj_id": 225,
  "next_img_id": 128,
  "entries": {
    "000000000049.jpg": {
      "mtime": 1661439700,
      "size": 158392,
      "img_id": 6,
      "objects": [ { "class": "person", "x": 0.32, "y": 0.64, "w": 0.22, "h": 0.27,
                     "area": 0.062, "confidence": 0.361, "original_class": "",
                     "super_class": "person", "is_fallback": false, "parent_id": -1,
                     "obj_id": 0, "img_id": 6 } ],
      "img_attrs": { "color_temperature": 5200, "avg_hue": 28.1, "avg_saturation": 0.41,
                     "avg_value": 0.62, "dominant_color": "orange",
                     "global_hue_hist": [ ... ], "luma_hist": [ ... ],
                     "overexposure_score": 0.02, "underexposure_score": 0.05,
                     "exposure_goodness": 0.965, "global_blur_score": 0.71,
                     "global_blur_raw": 8123.4, "camera_make": "...", "camera_model": "...",
                     "iso": 100, "shutter_speed": 0.008, "aperture": 2.8, "focal_length": 50,
                     "datetime_original": "...", "width": 640, "height": 480,
                     "user_tags": { "city": "sh", "level": "3" } }
    }
  }
}
```

> The `user_tags` are edited in the GUI detail panel and written back to this file; the tag
> pre-filter works on top of them.

## 2. Model Registry

### Directory conventions

```
models/
├── registry.json          # switches
└── base/
    ├── yolov8m-oiv7/      # one directory per model
    │   ├── model.onnx     # ONNX model (the engine only loads .onnx)
    │   ├── meta.json      # { "name", "type", "input_size", "classes" }
    │   └── classes.json   # output classes + parent inheritance chain
```

`meta.json` example:

```json
{ "name": "yolov8m-oiv7", "type": "detector", "input_size": 640, "classes": 601 }
```

`registry.json` example:

```json
{ "active_base": "yolov8m-oiv7", "active_extensions": [] }
```

### Switching models (edit config, then restart)

Edit `models/registry.json`, change `active_base` to another model name, restart (or `/reload`
in the REPL). When the program detects that `cache/<new-model>/` does not exist (or the old cache
belongs to another model), it automatically rebuilds the cache with the new model.

### Temporary switch on the command line

```powershell
build\dsl.exe --base yolov8m-oiv7   # use yolov8m-oiv7 for this run
build\dsl.exe --list-models         # list all registered base models
```

`--base` takes precedence over `registry.json`; an unknown name prints an error and lists the
available ones.

### Adding a new base model

1. Convert the `.pt` to `.onnx` (see [base_model_pack_format.md](base_model_pack_format.md)),
   place it at `models/base/<name>/model.onnx`.
2. In the same directory create `meta.json` (`name` must match the directory) and `classes.json`
   (see the field rules in [base_model_pack_format.md](base_model_pack_format.md)).
3. Set `active_base` in `models/registry.json`, or pass `--base <name>` at startup.

## 3. Inheritance Mapping & Confidence Fallback

`classes.json` defines the "child → parent" (is-a) inheritance chains. Inference thresholds live
in the `[inference]` section of `config/settings.ini` (adjustable in the GUI settings page),
where `fallback_threshold` (default 0) controls confidence fallback:

- a detection with confidence **≥ base_conf_threshold** (default 0.25) is recorded;
- **fallback_threshold = 0**: fallback disabled. The OIV7 model already outputs parent classes
  (`fruit`, `food`, `animal`, `vehicle`, …) directly, so there is no need to rewrite subclasses
  into parents by confidence; if you raise it manually, detections below that confidence are
  rewritten to the parent class name (`original_class` keeps the original).

`cnt(fruit)` counts objects whose class is `fruit` as well as subclasses like `apple`/`banana`:

```dsl
cnt(fruit)                          # total fruit in the library
$ : (cnt(person) > 2)               # images with more than 2 people
$ : (cnt(animal) > 0)               # animal and all its subclasses (cat/dog/…)
```

> Class names must exactly match `classes.json`: lowercase, multi-word classes use underscores
> (e.g. `traffic_light`, `stop_sign`, `mobile_phone`). Parent classes
> (`fruit`/`food`/`animal`/`vehicle`…) are direct model output classes.

## 3.5 Scene Recognition (Places365)

During the cache pre-processing phase, every image is also run through the **Places365** scene
model (ONNX Runtime / CPU, pure C++) to compute a 365-dim scene probability vector, in parallel
with and independent from object detection. The result is written into `img_attrs` of
`cache_index.json`:

```json
"img_attrs": {
  "...": "...",
  "scene_vector": [0.01, 0.02, 0.85, ...],   // probabilities of the 365 scenes
  "dominant_scene": "beach",
  "indoor_score": 0.12                        // P(indoor) = sum of the first 205 classes
}
```

DSL scene macros (return 0.0 / empty string when the model is missing, no error):

| Macro | Description | Example |
|-------|-------------|---------|
| `img_scene("name")` | scene probability (0–1) | `$ : (img_scene("beach") > 0.7)` |
| `img_scene_top()` | name of the highest-probability scene | `$ : (img_scene_top() == "forest")` |
| `img_is_indoor()` | indoor probability (0–1) | `$ : (img_is_indoor() > 0.6 && any(class == "person"))` |
| `img_scene_vec()` | internal macro (returns the top probability) | — |

Scene names come from `models/scene/categories_places365.txt` (365 lines); name case and
`-`/`_`/space are treated as equivalent (`dining_room` == `dining room`).

**Model preparation** (one-time): the model files are already provided in `models/scene/`
(`places365_googlenet.onnx` + `categories_places365.txt`). To regenerate the ONNX from the
official Caffe weights or a PyTorch checkpoint, export with `torch.onnx` following the
conventions in `models/scene/README.md`.

> Scene recognition is on par with YOLO object detection, color histograms, exposure/sharpness,
> EXIF and user tags and can be used simultaneously; inference happens during the cache build,
> queries only read the cache, and per-image scene inference is < 50 ms (CPU).

## 4. Tag Pre-Filter

Restricts `$` to images whose `user_tags` match, **before** evaluation — good for
"narrow down first, then query precisely".

```powershell
# CLI: AND across conditions, OR across values of the same key
build\dsl.exe --json --photo .\photo --tag-filter "city=sh|bj" --tag-filter "level=3"
build\dsl.exe --json --photo .\photo --tag-filter "location="   # any value for this key
```

- Each `--tag-filter key=v1|v2` is one condition; multiple conditions are **AND**; multiple values
  of the same key are **OR**.
- `key=` (empty value list) means "the key exists, any value".
- Matching 0 images returns an **empty result** (never silently falls back to the whole library).

GUI usage: click **🏷️ Tag Filter** in the search bar, add `key + value` conditions in the dialog
(multi-line, delete, clear supported). Click **Apply Filter** to activate; conditions **persist**
(survive restart) and the dialog pre-fills the last conditions when reopened.

## 5. Asset Management (deleting images)

- **DSL statement**: `del <target>`, where the target is a string path, an image-set expression
  or a variable. The engine deletes the files on disk and synchronously updates the cache index.

```dsl
del "000000000049.jpg"                    # delete a single image
del $ : (any(class == "cat"))             # delete all images with a cat
people = % $ : (any(class == "person"))
del people                                # delete the images pointed to by a variable
```

- **GUI**: Ctrl/Shift multi-select thumbnails in the result grid, then **🗑 Delete Selected**
  (with confirmation).

> Deletion is irreversible. The GUI shows a confirmation dialog when it detects `del` in the DSL.

## 5.5 Albums (virtual collections)

Albums are logical groups managed by the GUI and stored in the `collections` field of
`cache_index.json` — they never move files on disk.

- **Create / manage**: in the GUI left sidebar, right-click the 📁 Albums branch → New Album, then
  drag thumbnails into it (or right-click a thumbnail → Add to Album).
- **Query from DSL**: `collection("name")` returns the album's image set and combines with filters
  and set operations (see the DSL reference):

```dsl
collection("My Trip")                              # all images in the album
collection("My Trip") : (any(class == "cat"))      # cats inside the album
collection("Cat Picks") | collection("Dog Picks")  # union of two albums
```

## 5.6 Clustering / People Groups (V2)

With a clustering extension pack active (e.g. `face_recognition_v1`), the cache build extracts an
embedding per matching object and DBSCAN-clusters the whole library. The GUI shows a group branch
(e.g. 👤 People) in the left sidebar:

- each cluster is a clickable item (mapped name or raw id + photo count); clicking shows that
  group's photos in the grid;
- double-click / right-click → Rename to give the cluster a human-friendly name (persisted in
  `config/cluster_name_mappings.json`);
- query clusters from DSL with the clustering macros:

```dsl
# images whose object belongs to cluster person_001
$ : (any(cluster_id(obj, "face_cluster") == "face_cluster_person_001"))
```

## 6. Extension Packs

Extension packs perform "secondary fine-grained analysis" on objects detected by the base model:
the region of a parent-class object is cropped and fed to a dedicated small model (classifier or
detector) to produce child objects (e.g. `person` → `head/torso/arm/leg`).

```
models/extensions/<name>/
├── config.json   # { name, parent_class, children, model_path, input_size, conf_threshold, crop_padding, is_classifier }
└── model.onnx    # classifier [1,nc] or detector [1,4+nc,anchors] or embedding [1,D]
```

`active_extensions` in `models/registry.json` decides which packs are usable with `>>`
(unregistered packs error):

```powershell
# hand-write models/extensions/<name>/config.json and add "<name>" to registry.json active_extensions
```

```dsl
people = % $ : (any(class == "person"))
parts = people >> demo_v1    # crop each person and run the extension model
^ parts                      # lift back to the containing images
```

Every object in `parts` carries `parent_id` (linking to the parent), `super_class` (the parent
class) and a new global `obj_id`, mapped back to original-image coordinates. See
[extension_pack_format.md](extension_pack_format.md) for the pack format.

## 7. REPL Interaction

| Command | Effect |
|---------|--------|
| `exit` / `quit` | quit |
| `/reload` | re-read `models/registry.json` (switches base model and extension packs) |
| anything else | executed as a DSL expression / assignment / `del` |

Example session:

```dsl
dsl> $ : (any(class == "person"))
ImageSet (38 images):
  000000000368.jpg
  ...
dsl> % $ : (any(class == "person"))
ObjectSet (50 objects):
  [000000000081.jpg] airplane (x=0.329, ... area=0.19, conf=0.55, id=7)
  ...
```

> On Windows, if typing Chinese directly in the REPL fails to match, make sure the console is
> UTF-8 (the program sets it at startup, or run `chcp 65001` in the terminal).

## 8. Desktop GUI (tio.exe)

The GUI lives in `tio/gui` (Qt 6). After building, double-click `gui/build/tio.exe`:

1. Type a natural-language sentence in the search box (e.g. *"a cat to the left of a dog"*), click
   **Translate to DSL** to have the LLM generate DSL; or type DSL directly in the editor and click
   **Search**.
2. Results are shown as a thumbnail grid (sorted by fuzzy score descending; low scores filtered).
3. Double-click a result to open the **image-detail dialog**: view metadata (exposure /
   sharpness / EXIF / scene), edit user tags.
4. **🏷️ Tag Filter** in the search bar: configure tag pre-filtering (see §4).
5. **🗑 Delete Selected** in the search bar: multi-select delete (see §5).
6. Top-right: language switch (EN/中文), settings page (API / gallery / models / inference
   thresholds / extensions / logs), dark & light theme toggle.
7. Left sidebar: 📁 Albums, 🔍 Smart Albums and 👤 People branches — albums, smart albums
   (auto-grouped by tag/attr rules) and cluster groups.

## 9. FAQ

**Q: Results don't change after switching models?**
A: Confirm `active_base` in `models/registry.json` was changed and you restarted (or ran `/reload`
in the REPL); caches are isolated per model name and are rebuilt on the next query after a switch.

**Q: `>> xxx` errors "not active in registry"?**
A: That pack is not in `active_extensions` of `registry.json`; check the spelling or enable it.

**Q: The tag filter seems to have no effect?**
A: Make sure the images have the corresponding `user_tags` (edited in the detail panel and written
back to `cache_index.json`); `--tag-filter "key="` (no value) means "any value for key",
`key=val` is exact matching. Matching 0 images gives an empty result, not the whole library.

**Q: The first build is very slow?**
A: That's normal for CPU inference. Subsequent runs are extremely fast once the cache exists;
reducing the number of images in `photo/` shortens the first build.
