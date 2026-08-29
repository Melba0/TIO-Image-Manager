#define NOMINMAX
#include "YoloInference.h"
#include "engine/OnnxInference.h"
#include "../utils/exif_reader.h"

#include <windows.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <array>
#include <unordered_map>
#include <iostream>

using namespace Gdiplus;

namespace {

std::wstring toWide(const std::string& s) {
    if (s.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring ws(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &ws[0], size);
    return ws;
}

float clamp01(float x) {
    return x < 0 ? 0 : (x > 1 ? 1 : x);
}

}  // namespace

YoloInference::YoloInference(const std::string& model_path, const std::vector<std::string>& class_names,
                             int input_size, float conf_thresh, float iou_thresh)
    : class_names_(class_names), input_size_(input_size),
      conf_thresh_(conf_thresh), iou_thresh_(iou_thresh) {
    backend_ = std::make_unique<OnnxInference>(input_size_, 4);
    backend_->loadModel(model_path);
}

bool YoloInference::valid() const {
    return backend_ && backend_->valid();
}

YoloInference::~YoloInference() = default;

std::vector<float> YoloInference::loadAndPreprocess(Gdiplus::Bitmap& bmp,
                                                    double& scale, int& pad_x, int& pad_y) {
    const int S = input_size_;
    if (bmp.GetLastStatus() != Ok) return {};

    int ow = bmp.GetWidth();
    int oh = bmp.GetHeight();
    if (ow <= 0 || oh <= 0) return {};

    scale = std::min((double)S / oh, (double)S / ow);
    int nh = (int)std::round(oh * scale);
    int nw = (int)std::round(ow * scale);
    pad_x = (S - nw) / 2;
    pad_y = (S - nh) / 2;

    Bitmap canvas(S, S, PixelFormat24bppRGB);
    Graphics g(&canvas);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.Clear(Color(114, 114, 114));
    Rect dest(pad_x, pad_y, nw, nh);
    g.DrawImage(&bmp, dest, 0, 0, ow, oh, UnitPixel);

    Rect rect(0, 0, S, S);
    BitmapData data;
    if (canvas.LockBits(&rect, ImageLockModeRead, PixelFormat24bppRGB, &data) != Ok) {
        return {};
    }

    std::vector<float> tensor((size_t)3 * S * S);
    float* ptr = tensor.data();
    const unsigned char* src = (const unsigned char*)data.Scan0;
    int stride = data.Stride;

    for (int y = 0; y < S; ++y) {
        const unsigned char* row = src + (size_t)y * stride;
        for (int x = 0; x < S; ++x) {
            unsigned char b = row[x * 3 + 0];
            unsigned char g = row[x * 3 + 1];
            unsigned char r = row[x * 3 + 2];
            ptr[0 * S * S + y * S + x] = r / 255.0f;
            ptr[1 * S * S + y * S + x] = g / 255.0f;
            ptr[2 * S * S + y * S + x] = b / 255.0f;
        }
    }

    canvas.UnlockBits(&data);
    return tensor;
}

namespace {
// Classify a pixel's HSV into one of the 12 named colors.
std::string classifyColor(float h, float s, float v) {
    if (v < 0.15f) return "black";
    if (s < 0.15f) {
        if (v > 0.85f) return "white";
        return "gray";
    }
    // brown = dark, saturated orange/red
    auto brownish = [&]() { return s > 0.35f && v < 0.55f; };
    if (h < 15 || h >= 345) return brownish() ? "brown" : "red";
    if (h < 45) return brownish() ? "brown" : "orange";
    if (h < 70) return "yellow";
    if (h < 150) return "green";
    if (h < 190) return "cyan";
    if (h < 260) return "blue";
    if (h < 290) return "purple";
    return "pink";
}

// Correlated color temperature (Kelvin) from mean sRGB via McCamy's formula.
float cctFromRgb(double r, double g, double b) {
    auto lin = [](double c) {
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    r = lin(std::max(0.0, std::min(1.0, r)));
    g = lin(std::max(0.0, std::min(1.0, g)));
    b = lin(std::max(0.0, std::min(1.0, b)));
    double X = 0.4124564 * r + 0.3575761 * g + 0.1804375 * b;
    double Y = 0.2126729 * r + 0.7151522 * g + 0.0721750 * b;
    double Z = 0.0193339 * r + 0.1191920 * g + 0.9503041 * b;
    double sum = X + Y + Z;
    if (sum < 1e-9) return 0;
    double x = X / sum, y = Y / sum;
    double n = (x - 0.3320) / (y - 0.1858);
    double cct = -449.0 * n * n * n + 3525.0 * n * n - 6823.3 * n + 5520.33;
    if (cct < 2000) cct = 2000;
    if (cct > 10000) cct = 10000;
    return (float)cct;
}
}  // namespace

Attr YoloInference::computeAttr(Gdiplus::Bitmap& bmp, int x1, int y1, int x2, int y2) {
    Attr a;
    int W = bmp.GetWidth(), H = bmp.GetHeight();
    x1 = std::max(0, x1); y1 = std::max(0, y1);
    x2 = std::min(W, x2); y2 = std::min(H, y2);
    if (x2 <= x1 || y2 <= y1) return a;

    Rect whole(0, 0, W, H);
    BitmapData data;
    if (bmp.LockBits(&whole, ImageLockModeRead, PixelFormat24bppRGB, &data) != Ok) return a;
    const unsigned char* src = (const unsigned char*)data.Scan0;
    int stride = data.Stride;

    int rw = x2 - x1, rh = y2 - y1;
    long n = 0;
    double sum_h = 0, sum_s = 0, sum_v = 0;
    double sum_h2 = 0, sum_s2 = 0, sum_v2 = 0;
    double sum_r = 0, sum_g = 0, sum_b = 0;
    std::array<int, 32> hue_hist{};
    std::unordered_map<std::string, long> color_counts;
    std::vector<unsigned char> lbp_vals;
    lbp_vals.reserve((size_t)rw * rh);
    double lap_sum = 0, lap_sum2 = 0;
    long lap_n = 0;

    auto px = [&](int x, int y) -> unsigned char {
        const unsigned char* p = src + (size_t)y * stride + (size_t)x * 3;
        return (unsigned char)(0.114f * p[0] + 0.587f * p[1] + 0.299f * p[2]);
    };

    for (int y = y1; y < y2; ++y) {
        for (int x = x1; x < x2; ++x) {
            const unsigned char* p = src + (size_t)y * stride + (size_t)x * 3;
            unsigned char B = p[0], G = p[1], R = p[2];

            double r = R / 255.0, g = G / 255.0, b = B / 255.0;
            double mx = std::max(r, std::max(g, b));
            double mn = std::min(r, std::min(g, b));
            double d = mx - mn;
            double hh = 0, ss = 0, vv = mx;
            if (d > 1e-6) {
                ss = d / mx;
                if (mx == r) hh = 60.0 * std::fmod((g - b) / d, 6.0);
                else if (mx == g) hh = 60.0 * ((b - r) / d + 2.0);
                else hh = 60.0 * ((r - g) / d + 4.0);
                if (hh < 0) hh += 360.0;
            }
            sum_h += hh; sum_s += ss; sum_v += vv;
            sum_h2 += hh * hh; sum_s2 += ss * ss; sum_v2 += vv * vv;
            sum_r += r; sum_g += g; sum_b += b;
            ++n;

            int bin = (int)(hh / 11.25);
            if (bin >= 32) bin = 31;
            ++hue_hist[bin];

            color_counts[classifyColor((float)hh, (float)ss, (float)vv)]++;

            // local binary pattern (8 neighbors, radius 1) using grayscale
            if (x > x1 && y > y1 && x < x2 - 1 && y < y2 - 1) {
                unsigned char c = px(x, y);
                unsigned char code = 0;
                code |= (px(x - 1, y - 1) >= c) << 0;
                code |= (px(x,     y - 1) >= c) << 1;
                code |= (px(x + 1, y - 1) >= c) << 2;
                code |= (px(x + 1, y)     >= c) << 3;
                code |= (px(x + 1, y + 1) >= c) << 4;
                code |= (px(x,     y + 1) >= c) << 5;
                code |= (px(x - 1, y + 1) >= c) << 6;
                code |= (px(x - 1, y)     >= c) << 7;
                lbp_vals.push_back(code);

                // local sharpness: 4-neighbour Laplacian variance of the region
                float lap = 4.0f * c - px(x - 1, y) - px(x + 1, y) - px(x, y - 1) - px(x, y + 1);
                lap_sum += lap;
                lap_sum2 += lap * lap;
                ++lap_n;
            }
        }
    }

    if (n > 0) {
        a.h = (float)(sum_h / n);
        a.s = (float)(sum_s / n);
        a.v = (float)(sum_v / n);
        a.h_std = (float)std::sqrt(std::max(0.0, sum_h2 / n - a.h * a.h));
        a.s_std = (float)std::sqrt(std::max(0.0, sum_s2 / n - a.s * a.s));
        a.v_std = (float)std::sqrt(std::max(0.0, sum_v2 / n - a.v * a.v));
        a.color_temperature = cctFromRgb(sum_r / n, sum_g / n, sum_b / n);
        for (int i = 0; i < 32; ++i) a.hue_hist[i] = (float)hue_hist[i] / (float)n;

        auto it = std::max_element(color_counts.begin(), color_counts.end(),
                                   [](const auto& a, const auto& b) { return a.second < b.second; });
        if (it != color_counts.end()) a.dominant_color_name = it->first;
    }

    // local sharpness: normalized Laplacian variance of the region (0..1).
    if (lap_n > 0) {
        double mean = lap_sum / lap_n;
        double var = lap_sum2 / lap_n - mean * mean;
        if (var < 0) var = 0;
        a.local_blur_score = (float)(var / (var + 3000.0));
    }

    if (!lbp_vals.empty()) {
        int uniform = 0;
        for (unsigned char code : lbp_vals) {
            int tr = 0;
            for (int i = 0; i < 8; ++i) {
                int b1 = (code >> i) & 1;
                int b2 = (code >> ((i + 1) % 8)) & 1;
                if (b1 != b2) ++tr;
            }
            if (tr <= 2) ++uniform;
        }
        a.lbp = 1.0f - (float)uniform / (float)lbp_vals.size();
    }

    bmp.UnlockBits(&data);
    return a;
}

ImageAttrs YoloInference::computeImageAttrs(Gdiplus::Bitmap& bmp) {
    ImageAttrs ia;
    int W = bmp.GetWidth(), H = bmp.GetHeight();
    if (W <= 0 || H <= 0) return ia;
    ia.width = W;
    ia.height = H;

    Rect whole(0, 0, W, H);
    BitmapData data;
    if (bmp.LockBits(&whole, ImageLockModeRead, PixelFormat24bppRGB, &data) != Ok) return ia;
    const unsigned char* src = (const unsigned char*)data.Scan0;
    int stride = data.Stride;

    long n = 0;
    double sum_h = 0, sum_s = 0, sum_v = 0, sum_r = 0, sum_g = 0, sum_b = 0;
    std::array<int, 32> hue_hist{};
    std::array<long, 64> luma_hist{};
    long over_cnt = 0, under_cnt = 0;
    std::unordered_map<std::string, long> color_counts;

    for (int y = 0; y < H; ++y) {
        const unsigned char* row = src + (size_t)y * stride;
        for (int x = 0; x < W; ++x) {
            unsigned char B = row[x * 3 + 0];
            unsigned char G = row[x * 3 + 1];
            unsigned char R = row[x * 3 + 2];
            double r = R / 255.0, g = G / 255.0, b = B / 255.0;
            double mx = std::max(r, std::max(g, b));
            double mn = std::min(r, std::min(g, b));
            double d = mx - mn;
            double hh = 0, ss = 0, vv = mx;
            if (d > 1e-6) {
                ss = d / mx;
                if (mx == r) hh = 60.0 * std::fmod((g - b) / d, 6.0);
                else if (mx == g) hh = 60.0 * ((b - r) / d + 2.0);
                else hh = 60.0 * ((r - g) / d + 4.0);
                if (hh < 0) hh += 360.0;
            }
            sum_h += hh; sum_s += ss; sum_v += vv;
            sum_r += r; sum_g += g; sum_b += b;
            ++n;

            int bin = (int)(hh / 11.25);
            if (bin >= 32) bin = 31;
            ++hue_hist[bin];

            // luminance (value channel) histogram + exposure counters
            int l = (int)(vv * 255.0);
            if (l > 255) l = 255;
            ++luma_hist[std::min(63, l / 4)];
            if (l > 240) ++over_cnt;
            if (l < 30) ++under_cnt;

            color_counts[classifyColor((float)hh, (float)ss, (float)vv)]++;
        }
    }

    // global sharpness: Laplacian variance of the grayscale image
    double lap_sum = 0, lap_sum2 = 0;
    long lap_n = 0;
    if (W > 2 && H > 2) {
        for (int y = 1; y < H - 1; ++y) {
            const unsigned char* row = src + (size_t)y * stride;
            for (int x = 1; x < W - 1; ++x) {
                auto gray = [&](int yy, int xx) -> float {
                    const unsigned char* p = src + (size_t)yy * stride + (size_t)xx * 3;
                    return 0.114f * p[0] + 0.587f * p[1] + 0.299f * p[2];
                };
                float c = gray(y, x);
                float lap = 4.0f * c - gray(y, x - 1) - gray(y, x + 1) - gray(y - 1, x) - gray(y + 1, x);
                lap_sum += lap;
                lap_sum2 += lap * lap;
                ++lap_n;
            }
        }
    }
    bmp.UnlockBits(&data);

    if (n > 0) {
        ia.avg_hue = (float)(sum_h / n);
        ia.avg_saturation = (float)(sum_s / n);
        ia.avg_value = (float)(sum_v / n);
        ia.color_temperature = cctFromRgb(sum_r / n, sum_g / n, sum_b / n);
        for (int i = 0; i < 32; ++i) ia.global_hue_hist[i] = (float)hue_hist[i] / (float)n;
        for (int i = 0; i < 64; ++i) ia.luma_hist[i] = (float)luma_hist[i] / (float)n;

        // exposure: fraction of blown-out highlights / deep shadows
        ia.overexposure_score = clamp01((float)over_cnt / (float)n);
        ia.underexposure_score = clamp01((float)under_cnt / (float)n);
        ia.exposure_goodness = 1.0f - (ia.overexposure_score + ia.underexposure_score) / 2.0f;

        auto it = std::max_element(color_counts.begin(), color_counts.end(),
                                   [](const auto& a, const auto& b) { return a.second < b.second; });
        if (it != color_counts.end()) ia.dominant_color = it->first;
    }

    // global sharpness: normalized Laplacian variance (0..1, 1 = sharpest).
    if (lap_n > 0) {
        double mean = lap_sum / lap_n;
        double var = lap_sum2 / lap_n - mean * mean;
        if (var < 0) var = 0;
        ia.global_blur_raw = (float)var;
        ia.global_blur_score = (float)(var / (var + 3000.0));
    }
    return ia;
}

std::vector<DetectionBox> YoloInference::detect(const std::string& image_path) {
    if (!valid()) return {};

    Bitmap bmp(toWide(image_path).c_str());
    if (bmp.GetLastStatus() != Ok) return {};

    double scale = 1.0;
    int pad_x = 0, pad_y = 0;
    std::vector<float> input = loadAndPreprocess(bmp, scale, pad_x, pad_y);
    if (input.empty()) return {};

    int orig_w = bmp.GetWidth();
    int orig_h = bmp.GetHeight();

    InferenceTensor out = backend_->run(input.data(), input.size());
    auto boxes = postprocess(out, orig_w, orig_h, scale, pad_x, pad_y);

    // fill atmospheric attributes (HSV mean + LBP variance) for each box
    for (auto& b : boxes) {
        int px1 = (int)((b.x - b.w / 2) * orig_w);
        int py1 = (int)((b.y - b.h / 2) * orig_h);
        int px2 = (int)((b.x + b.w / 2) * orig_w);
        int py2 = (int)((b.y + b.h / 2) * orig_h);
        b.attr = computeAttr(bmp, px1, py1, px2, py2);
    }
    return boxes;
}

ImageAttrs YoloInference::detectImageAttrs(const std::string& image_path) {
    Bitmap bmp(toWide(image_path).c_str());
    if (bmp.GetLastStatus() != Ok) return {};
    ImageAttrs ia = computeImageAttrs(bmp);

    // EXIF metadata (best-effort; defaults remain when absent).
    ExifInfo exif;
    if (readExifFromJpeg(image_path, exif)) {
        ia.camera_make = exif.make;
        ia.camera_model = exif.model;
        ia.iso = exif.iso;
        ia.shutter_speed = exif.shutter_speed;
        ia.aperture = exif.aperture;
        ia.focal_length = exif.focal_length;
        if (!exif.datetime_original.empty()) ia.datetime_original = exif.datetime_original;
    }
    return ia;
}

std::vector<DetectionBox> YoloInference::postprocess(const InferenceTensor& preds,
                                                     int orig_w, int orig_h,
                                                     double scale, int pad_x, int pad_y) {
    const auto& names = class_names_;
    std::vector<DetectionBox> raw;

    // ONNX YOLOv8 output: (1, 4+nc, N) with rows
    // [x1, y1, x2, y2, cls_scores...].  Boxes are ALREADY decoded to
    // input_size (letterboxed) pixel coordinates by the exported model
    // (DFL + dist2bbox + stride scaling), so we only un-letterbox here.
    if (preds.dim() == 3 && preds.shape.size() >= 2 && preds.shape[1] == 4 + (int)names.size()) {
        int64_t n = preds.shape[2];
        const float* p = preds.data.data();
        int nc = (int)names.size();

        for (int64_t j = 0; j < n; ++j) {
            float x1 = p[0 * n + j];
            float y1 = p[1 * n + j];
            float x2 = p[2 * n + j];
            float y2 = p[3 * n + j];

            float best_conf = 0.0f;
            int best_cls = -1;
            for (int c = 0; c < nc; ++c) {
                float s = p[(4 + c) * n + j];
                if (s > best_conf) {
                    best_conf = s;
                    best_cls = c;
                }
            }
            if (best_conf < conf_thresh_) continue;

            // un-letterbox
            float ux1 = (x1 - pad_x) / (float)scale;
            float uy1 = (y1 - pad_y) / (float)scale;
            float ux2 = (x2 - pad_x) / (float)scale;
            float uy2 = (y2 - pad_y) / (float)scale;

            ux1 = std::max(0.0f, ux1);
            uy1 = std::max(0.0f, uy1);
            ux2 = std::min((float)orig_w, ux2);
            uy2 = std::min((float)orig_h, uy2);
            if (ux2 <= ux1 || uy2 <= uy1) continue;

            DetectionBox box;
            box.class_name = (best_cls >= 0 && best_cls < nc) ? names[best_cls] : "unknown";
            box.x = ((ux1 + ux2) / 2.0) / orig_w;
            box.y = ((uy1 + uy2) / 2.0) / orig_h;
            box.w = (ux2 - ux1) / orig_w;
            box.h = (uy2 - uy1) / orig_h;
            box.area = box.w * box.h;
            box.confidence = best_conf;
            raw.push_back(box);
        }
    } else {
        std::cerr << "[Yolo] Unexpected model output shape (expected (1, "
                  << 4 + (int)names.size() << ", N)):";
        for (auto d : preds.shape) std::cerr << " " << d;
        std::cerr << std::endl;
    }

    return nonMaxSuppression(std::move(raw));
}

std::vector<DetectionBox> YoloInference::nonMaxSuppression(std::vector<DetectionBox> detections) {
    if (detections.empty()) return {};

    std::sort(detections.begin(), detections.end(), [](const DetectionBox& a, const DetectionBox& b) {
        return a.area > b.area;
    });

    std::vector<DetectionBox> result;
    std::vector<bool> suppressed(detections.size(), false);

    for (size_t i = 0; i < detections.size(); ++i) {
        if (suppressed[i]) continue;
        result.push_back(detections[i]);

        for (size_t j = i + 1; j < detections.size(); ++j) {
            if (suppressed[j]) continue;
            if (detections[i].class_name != detections[j].class_name) continue;

            double ix1 = detections[i].x - detections[i].w / 2;
            double iy1 = detections[i].y - detections[i].h / 2;
            double ix2 = detections[i].x + detections[i].w / 2;
            double iy2 = detections[i].y + detections[i].h / 2;
            double jx1 = detections[j].x - detections[j].w / 2;
            double jy1 = detections[j].y - detections[j].h / 2;
            double jx2 = detections[j].x + detections[j].w / 2;
            double jy2 = detections[j].y + detections[j].h / 2;

            double x1 = std::max(ix1, jx1);
            double y1 = std::max(iy1, jy1);
            double x2 = std::min(ix2, jx2);
            double y2 = std::min(iy2, jy2);
            if (x1 >= x2 || y1 >= y2) continue;

            double inter = (x2 - x1) * (y2 - y1);
            double area_i = detections[i].w * detections[i].h;
            double area_j = detections[j].w * detections[j].h;
            double iou = inter / (area_i + area_j - inter);
            if (iou > iou_thresh_) suppressed[j] = true;
        }
    }

    return result;
}
