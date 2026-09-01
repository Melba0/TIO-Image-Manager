# DSL Language Reference

[**English**](dsl_reference.md) ・ [**中文**](dsl_reference_zh.md)

This page is the complete syntax and semantics reference for the image-retrieval DSL. Every query
is executed line by line in the REPL; when running a script file, the result of the last
expression is printed by default (or the variable `out` if it was explicitly assigned).

## 0. Recommended Syntax (modern)

The new syntax introduced and recommended by this project:

```
filter images:   $ : (condition)   # ImageSet → keep images satisfying an image-level condition
existential:     any(condition)    # the current image/set has an object satisfying the condition
universal:       all(condition)    # all objects in the current image/set satisfy the condition
```

```dsl
$ : (any(class == "person"))             # all images with a person
$ : (all(area > 0.05))                   # images where every object has area > 0.05
$ : (cnt(fruit) > 2 && any(class == "cat"))
```

> The legacy quantifier syntax `$ any (cond)` remains backward-compatible (identical results),
> but new code should always use the new syntax.
> `any(...)`/`all(...)` in the new form are semantically equivalent to the legacy quantifiers.

## 1. Data Types

| Type | Description |
|------|-------------|
| `ImageSet` | a set of images (represented internally by relative image paths) |
| `ObjectSet` | a set of objects (detection boxes, see attributes below) |
| `Object` | a single object |
| `Num` | a floating-point number |
| `Bool` | a boolean |
| `String` | a string (double-quoted `"..."` or a bare identifier; UTF-8 incl. Chinese supported) |
| `HistVec` | a 32-bin normalized hue histogram (produced by `obj_hist`/`img_hist` etc.) |

### Object attributes (accessible on every object in an ObjectSet)

| Attribute | Type | Description |
|-----------|------|-------------|
| `class` | String | current class name |
| `x` `y` | Num | center coordinates (normalized 0–1) |
| `w` `h` | Num | width / height (normalized) |
| `area` | Num | area ratio (`w * h`) |
| `confidence` | Num | detection confidence |
| `super_class` | String | parent class name (from inheritance mapping, may be empty) |
| `original_class` | String | original class before fallback (only non-empty when `is_fallback`) |
| `attr.h/s/v` | Num | object-region HSV means (h 0–360, s/v 0–1) |
| `attr.lbp` | Num | object-region LBP roughness (0–1) |
| `attr.color_temperature` | Num | region color temperature (Kelvin) |
| `attr.dominant_color_name` | String | region dominant color name |
| `attr.local_blur_score` | Num | object-region sharpness (0–1, higher = sharper) |

## 2. Operators

### Arithmetic (higher precedence than comparisons)
`+` `-` `*` `/`

### Comparison
`>` `<` `>=` `<=` `==` `!=`

### Logic
`&&` (and) `||` (or) `!` (not)

### Sets
`|` (union) `&` (intersection) `-` (difference), apply to ImageSet / ObjectSet.

### Unary operators

| Symbol | Effect | Example |
|--------|--------|---------|
| `$` | the whole library (inside a quantifier: "the current image") | `$` |
| `%` | extract objects: ImageSet → ObjectSet | `% $ : (any(class == "cat"))` |
| `^` | lift images: ObjectSet → ImageSet (dedup) | `^ (% $ : (any(class == "cat")))` |
| `!` | logical not | `!(area < 0.05)` |

### Filter `:` and condition functions `any()` / `all()`

```dsl
$ : (any(class == "cat"))                      # images with a cat
$ : (all(area > 0.05) && any(class == "dog"))  # no tiny objects AND has a dog
people = % $ : (any(class == "person"))
```

- `ImageSet : (cond)` → evaluate the image-level condition `cond` per image, keep matching images
  (FilterExpr).
- `any(cond)` / `all(cond)` → whether (all) objects in the current image/set satisfy `cond`.

### Album set `collection("name")`

Returns the image set (ImageSet) of a user-created **virtual album** (albums are managed by the
GUI and stored in the `collections` field of `cache_index.json`; they are logical groupings and
do not move files). Combines freely with set operations and filters:

```dsl
collection("My Trip")                              # all images in the album
collection("My Trip") : (any(class == "cat"))      # search cats inside the album
collection("Cat Picks") | collection("Dog Picks")  # union
```

- The album name must exactly match the GUI name (UTF-8, case-sensitive).
- A missing album returns an empty set (no error).
- `collection(...)` is not affected by the tag pre-filter (`$` still respects it; intersect with
  `& $` to combine).

### Extension operator `>>`
Runs the extension model on cropped regions of the objects in an ObjectSet matching the
extension pack's parent class, producing a new ObjectSet (child objects carry `parent_id`
linking to their parent).

```dsl
parts = flowers >> botany_v1
```

> The extension pack must appear in `active_extensions` of `models/registry.json`, otherwise:
> `Extension "xxx" is not active in registry.`

## 3. Quantifiers / Condition Functions `any` / `all`

Act on a set (ImageSet or ObjectSet):

- `ImageSet any (condition)` → ImageSet (keep images that "have an object satisfying the condition")
- `ImageSet all (condition)` → ImageSet (keep images where "all objects satisfy the condition")
- `ObjectSet any (condition)` → Bool (does any object in the set satisfy the condition)
- `ObjectSet all (condition)` → Bool (do all objects in the set satisfy the condition)

### Full form
```dsl
$ : (any(class == "cat"))
$ : (any((class == "dog") && (area > 0.05)))
```

### Shorthand (direct class name, equivalent to `class == "class name"`)
```dsl
$ : (any flower)
% $ : (any flower)
```

## 4. Count Function `cnt(class name)`

Counts the number of objects matching a class in the current iteration context (returns `Num`):

- Inside `ImageSet : (cond)`: counts objects **in the current image**.
- Inside `ObjectSet any/all`: counts objects in that object set.
- At top level: counts the whole library.

Matching rules (counted if any holds):
1. `obj.class_name == class name`
2. `obj.super_class == class name`
3. `obj.class_name` is an inheritance subclass of `class name` (per `classes.json`)

```dsl
$ : (cnt(fruit) > 2)      # images with more than 2 fruit
cnt(person)               # total person objects in the library
cnt(animal)               # animal and all its subclasses (cat/dog/…)
```

> Class names must exactly match `classes.json` (lowercase; multi-word classes use underscores,
> e.g. `traffic_light`, `stop_sign`). Parent classes (`fruit`/`food`/`animal`/`vehicle`…) are
> themselves model-output classes.

## 5. Variables & `out`

- Variables are assigned with `=`, and **cannot be reassigned** (reassignment is an error).
- After a script finishes: if a variable `out` exists, print `out`; otherwise print the result of
  the last expression.

```dsl
cat_pics = $ : (any(class == "cat"))
dog_pics = $ : (any(class == "dog"))
both = cat_pics & dog_pics
out = both
```

## 6. Delete Statement `del`

`del <target>` deletes image files and synchronously updates the cache index (top-level statement):

```dsl
del "000000000049.jpg"                        # string path
del $ : (any(class == "cat"))                 # image-set expression
people = % $ : (any(class == "person"))
del people                                    # variable
```

## 7. Macro System

Macros are named expression templates. Built-in macros and user macros share one macro table and
the same call syntax.

### Defining user macros

```
macro <name>(<arg1>, <arg2>, ...) = <expression>
```

```dsl
macro half_area(x) = x.area / 2
macro has_big_cat(set) = set : (any(class == "cat" && big))
```

### Calling macros

```dsl
big(obj)              # explicit argument
half_area(obj)        # user macro
max(0.1, 0.2)         # math function
warm & bright         # bare call: broadcasts the current object to a single-argument macro
```

A bare call (like `big`, `warm`) automatically passes the **current object** as the argument
inside a quantifier.

### Built-in macros

**Math functions** (return Num):

| Function | Description |
|----------|-------------|
| `max(a,b)` `min(a,b)` | maximum / minimum |
| `abs(x)` `sqrt(x)` | absolute value / square root |
| `pow(a,b)` | power |
| `log(x)` `exp(x)` | natural log / exponential |

**Color macros** (based on object/image HSV and color temperature):

| Macro | Description |
|-------|-------------|
| `color(obj, "blue")` | whether the region's dominant color is the given one (red/orange/yellow/green/cyan/blue/purple/pink/brown/gray/white/black) |
| `cct(obj)` / `warmth(obj)` / `coolness(obj)` / `brightness(obj)` / `saturation(obj)` | region color temp / warmth / coolness / brightness / saturation (0–1) |
| `img_temp()` / `img_warmth()` / `img_coolness()` / `img_bright()` / `img_colorful()` | whole-image color temp / warmth / coolness / brightness / colorfulness |
| `img_color("blue")` | whether the whole image's dominant color is the given one |

**Histogram macros** (32-bin hue histogram):

| Macro | Description |
|-------|-------------|
| `obj_hist(obj)` | 32-bin histogram of the object region |
| `img_hist()` | whole-image 32-bin histogram |
| `hist_sim(A, B)` | cosine similarity of two histograms (0–1) |
| `hist_value(obj, idx)` | bin `idx` (0–31) of the object histogram (out of range is an error) |
| `img_hist_value(idx)` | bin `idx` of the whole-image histogram |

> Color↔bin reference: red 0,31 / orange 1,2 / yellow 3-5 / green 9-11 / cyan 14,15 / blue
> 19-21 / purple 24-26 / pink 27-29.

**Image quality / EXIF / user-tag macros**:

| Macro | Description |
|-------|-------------|
| `img_over()` / `img_under()` / `img_exp_good()` | overexposure / underexposure / exposure quality (0–1) |
| `img_hist_val(idx)` | luma histogram bin (0–63) |
| `img_blur()` / `img_blurry()` / `obj_blur(obj)` | global sharpness / 1−sharpness / object-region sharpness |
| `img_camera()` / `img_iso()` / `img_shutter()` / `img_aperture()` / `img_fl()` / `img_date()` | EXIF (empty/-1 when absent) |
| `img_tag(key)` / `img_has_tag(key)` / `img_tag_equals(k,v)` | user-tag queries |
| `stof(s)` / `str_contains(s, sub)` | string→number / containment check |

**Places365 scene macros** (based on the 365-dim scene probability vector in the cache; return
0.0 / empty string when the model is missing):

| Macro | Description |
|-------|-------------|
| `img_scene("beach")` | probability of the scene for the image (0–1; name case and `-`/`_`/space insensitive) |
| `img_scene_top()` | name of the highest-probability scene (string) |
| `img_scene_vec()` | internal: returns the top scene probability (the 365-dim vector is not exposed to DSL) |
| `img_is_indoor()` | indoor probability (sum of the first 205 classes, 0–1) |

**Spatial / geometry macros** (take an object, return Bool):

| Macro | Logic |
|-------|-------|
| `big(x)` | `x.area > 0.2` |
| `small(x)` | `x.area < 0.05` |
| `left(x)` `right(x)` | center x in the left / right third |
| `top(x)` `bottom(x)` | center y in the top / bottom third |
| `square(x)` | `abs(x.w/x.h - 1) < 0.1` |

**Clustering macros** (V2, read the cluster ids cached on objects; requires an enabled
clustering extension pack):

| Macro | Description |
|-------|-------------|
| `cluster_id(obj, "face_cluster")` | the object's id under that clustering (string; empty when unassigned) |
| `cluster_sim(a, b, "face_cluster")` | whether a and b are in the same cluster (fuzzy 1/0) |

```dsl
# images whose object belongs to cluster person_001
$ : (any(cluster_id(obj, "face_cluster") == "face_cluster_person_001"))
# two images share an object in the same cluster
$ : (any(cluster_sim(obj, obj, "face_cluster")))
```

**Relationship macros** (take two objects):

| Macro | Logic |
|-------|-------|
| `left_of(a,b)` | `a.x + a.w < b.x` |
| `above(a,b)` | `a.y + a.h < b.y` |
| `inside(a,b)` | a's box is fully inside b |

**Atmosphere macros** (based on the HSV means and LBP roughness `attr` precomputed for the
object region):

| Macro | Logic |
|-------|-------|
| `warm(obj)` | `obj.attr.h ∈ (5, 45)` |
| `cool(obj)` | `obj.attr.h ∈ (180, 260)` |
| `bright(obj)` | `obj.attr.v > 0.75` |
| `dark(obj)` | `obj.attr.v < 0.25` |
| `smooth(obj)` | `obj.attr.lbp < 0.2` |
| `rough(obj)` | `obj.attr.lbp > 0.6` |

### Examples

```dsl
out = $ : (any(warm && bright))                       # images with a bright warm object
out = $ : (any(area > half_area(obj)))                # custom macro + object broadcast
out = $ : (any(hist_value(obj, 0) + hist_value(obj, 31) > 0.3))  # large red share
out = $ : (img_exp_good() > 0.9 && any(class == "person"))       # well-exposed and has a person
```

## 8. Operator Precedence (high → low)

```
! - (unary) > % ^ $ (unary) > . (attribute) > * / > + - > > < >= <= == != > && > ||
> | & - (set) > : (filter) > >> > any/all (postfix)
```

## 9. Syntax Examples

```dsl
# find images with a cat
pic1 = $ : (any(class == "cat"))

# a cat on the left side (x<0.3) of the image
pic2 = $ : (any((% $ : (any(class == "cat"))) any (x < 0.3)))

# has an object with area>0.2, and that object is not a person
pic3 = $ : (any((% $ : (any(area > 0.2))) any (class != "person")))

# set operations: images containing both a cat and a dog
cat_pics = $ : (any(class == "cat"))
dog_pics = $ : (any(class == "dog"))
result = cat_pics & dog_pics

# inheritance count: images with more than 2 fruit
fruit_pics = $ : (cnt(fruit) > 2)

# scene recognition (Places365): find beach / kitchen or dining-room photos
beach_pics = $ : (img_scene("beach") > 0.7)
food_pics  = $ : (img_scene("kitchen") > 0.5 || img_scene("dining_room") > 0.5)

# indoor photos with a person
indoor_people = $ : (img_is_indoor() > 0.6 && any(class == "person"))

# photos whose dominant scene is a forest
forest_pics = $ : (img_scene_top() == "forest")

# extension refinement: flower → flower parts → lift
flowers = % $ : (any flower)
parts = flowers >> botany_v1
out = ^ parts

# tag pre-filter is done on the command line; DSL does not need to be aware:
#   dsl.exe --json --photo .\photo --tag-filter "city=sh|bj"
```

## 10. Notes

- Class names, attribute names are UTF-8 encoded; save script files as UTF-8 (no BOM).
- String literals `"..."` and bare identifiers are equivalent in a class-name context.
- In `class == "cat"`, `class` is the current object's class; inside a quantifier the current
  object is bound automatically.
- `del` is irreversible: it deletes the file on disk and updates the cache index.
