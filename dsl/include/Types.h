#pragma once
#include <string>
#include <vector>
#include <array>
#include <map>

// 32-bin normalized hue histogram (each bin 0..1, sum == 1).  Shared by
// object attributes and whole-image attributes, and exposed to the DSL via the
// obj_hist / img_hist / hist_sim macros.
using HistVec = std::array<float, 32>;

// A detected object. x/y/w/h are normalized to the image dimensions.
//
// Atmospheric / appearance attributes computed during cache preprocessing:
//   h/s/v  = mean HSV of the object region (h in degrees 0-360, s/v in 0-1)
//   *_std  = HSV standard deviations
//   color_temperature = correlated color temperature in Kelvin (2000~10000)
//   dominant_color_name = nearest named color (red/orange/yellow/green/cyan/
//                         blue/purple/pink/brown/gray/white/black)
//   hue_hist = 32-bin normalized hue histogram (sum == 1)
//   lbp    = normalized local-binary-pattern roughness (0-1)
struct Attr {
    float h = 0, s = 0, v = 0;
    float h_std = 0, s_std = 0, v_std = 0;
    float color_temperature = 0;
    std::string dominant_color_name;
    HistVec hue_hist{};
    float lbp = 0;
    float local_blur_score = 0;   // sharpness of the object region (0..1, higher = sharper)
};

// Global (whole-image) attributes, stored per image alongside the objects.
struct ImageAttrs {
    float color_temperature = 0;      // Kelvin estimate
    float avg_hue = 0;                // 0-360
    float avg_saturation = 0;         // 0-1
    float avg_value = 0;              // 0-1
    std::string dominant_color;       // named color of the whole image
    HistVec global_hue_hist{};

    // ---- exposure (computed from the luminance histogram) ----
    std::array<float, 64> luma_hist{}; // 64-bin luminance histogram (0..255 range, normalized)
    float overexposure_score = 0;      // 0..1, higher = more blown-out highlights (V > 240)
    float underexposure_score = 0;     // 0..1, higher = darker shadows (V < 30)
    float exposure_goodness = 1;       // 0..1, 1 = perfectly exposed

    // ---- sharpness (Laplacian variance, normalized 0..1, 1 = sharpest) ----
    float global_blur_score = 0;
    float global_blur_raw = 0;         // raw Laplacian variance (for renormalization/debug)

    // ---- EXIF (empty/-1 when the file has no usable metadata) ----
    std::string camera_make;
    std::string camera_model;
    float iso = -1;
    float shutter_speed = -1;          // seconds
    float aperture = -1;               // f-number
    float focal_length = -1;           // mm
    std::string datetime_original;
    int width = 0, height = 0;

    // ---- user tags (key-value annotations, edited via the GUI) ----
    std::map<std::string, std::string> user_tags;

    // ---- Places365 scene recognition (computed during cache preprocessing) ----
    // scene_vector: softmax probabilities over the 365 Places365 scene classes
    // (index i -> categories_places365.txt line i).  All zeros when the scene
    // model is unavailable.
    std::array<float, 365> scene_vector{};
    std::string dominant_scene;          // highest-probability scene name ("" if unavailable)
    float indoor_score = 0;              // P(indoor) = sum over the 205 indoor classes
};

// One active tag-filter condition for the pre-filter pipeline (applied before
// the DSL runs; `$` then iterates only the images matching ALL filters).
struct TagFilter {
    std::string key;                 // tag name
    std::vector<std::string> values; // allowed values (OR); empty = any value for key
};

struct DetectedObject {
    std::string image_path;
    std::string class_name;
    double x = 0, y = 0, w = 0, h = 0, area = 0;

    double confidence = 0;
    float score = 0;           // transient fuzzy score (not cached); base = confidence
    Attr attr;                 // HSV mean + LBP variance of the region

    // Confidence-fallback bookkeeping (see IsaManager):
    std::string original_class;  // class detected by the raw model (if fallback applied)
    std::string super_class;     // is-a parent class (always recorded if known)
    bool is_fallback = false;    // true when class_name was degraded to super_class

    // Object graph ids:
    int parent_id = -1;  // id of the parent object (set for objects produced by `>>`)
    int obj_id = -1;     // globally unique object id
    int img_id = -1;     // image id (index in the photo cache)
};

// Monotonic global object-id generator.  Shared by the cache builder and the
// extension (expansion) engine so ids stay unique across both.
class ObjectIdGenerator {
public:
    int next() { return next_++; }
    int peek() const { return next_; }
    void reset() { next_ = 0; }
    void set(int v) { next_ = v; }

private:
    int next_ = 0;
};