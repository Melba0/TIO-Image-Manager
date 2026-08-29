#pragma once
#include <string>
#include <vector>
#include <memory>
#include "Types.h"
#include "InferenceBackend.h"

namespace Gdiplus { class Bitmap; }

struct DetectionBox {
    std::string class_name;
    double x = 0; // center x (normalized)
    double y = 0; // center y (normalized)
    double w = 0; // width (normalized)
    double h = 0; // height (normalized)
    double area = 0;
    double confidence = 0;
    Attr attr;    // HSV mean + LBP variance of the region
};

class OnnxInference;

// Base detection model wrapper: letterbox + ONNX inference + box decoding +
// NMS.  Detections are returned in normalized image coordinates.
class YoloInference {
public:
    // class_names: output class names from the model package's classes.json
    // (index i maps to the model's i-th output class).
    YoloInference(const std::string& model_path, const std::vector<std::string>& class_names,
                  int input_size = 640, float conf_thresh = 0.25f, float iou_thresh = 0.45f);
    ~YoloInference();
    bool valid() const;
    int inputSize() const { return input_size_; }
    std::vector<DetectionBox> detect(const std::string& image_path);
    // Whole-image color attributes (used by CacheManager for img_* macros).
    ImageAttrs detectImageAttrs(const std::string& image_path);

private:
    std::unique_ptr<OnnxInference> backend_;
    std::vector<std::string> class_names_;
    int input_size_ = 640;
    float conf_thresh_;
    float iou_thresh_;

    // Letterbox the image into a CHW float buffer (values 0..1).  On failure
    // returns an empty vector.  scale / pad_x / pad_y describe the mapping so
    // boxes can be un-letterboxed to the original image.
    std::vector<float> loadAndPreprocess(Gdiplus::Bitmap& bmp, double& scale, int& pad_x, int& pad_y);
    // Decode the raw model output (1, 4+nc, N) whose rows are
    // [x1, y1, x2, y2, cls...] in input_size pixels, then un-letterbox.
    std::vector<DetectionBox> postprocess(const InferenceTensor& preds, int orig_w, int orig_h,
                                          double scale, int pad_x, int pad_y);
    std::vector<DetectionBox> nonMaxSuppression(std::vector<DetectionBox> detections);
    // HSV stats + LBP variance + color features of the region [x1,y1,x2,y2].
    Attr computeAttr(Gdiplus::Bitmap& bmp, int x1, int y1, int x2, int y2);
    // Whole-image HSV stats + color temperature + dominant color.
    ImageAttrs computeImageAttrs(Gdiplus::Bitmap& bmp);
};
