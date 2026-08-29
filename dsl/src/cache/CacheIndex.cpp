#include "CacheIndex.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

// ---- DetectedObject <-> JSON ----
json objectToJson(const DetectedObject& o) {
    return json{
        {"class", o.class_name},
        {"x", o.x}, {"y", o.y}, {"w", o.w}, {"h", o.h}, {"area", o.area},
        {"confidence", o.confidence},
        {"original_class", o.original_class},
        {"super_class", o.super_class},
        {"is_fallback", o.is_fallback},
        {"parent_id", o.parent_id},
        {"obj_id", o.obj_id},
        {"img_id", o.img_id},
        {"attr", json{{"h", o.attr.h}, {"s", o.attr.s}, {"v", o.attr.v},
                      {"lbp", o.attr.lbp},
                      {"h_std", o.attr.h_std}, {"s_std", o.attr.s_std}, {"v_std", o.attr.v_std},
                      {"color_temperature", o.attr.color_temperature},
                      {"dominant_color_name", o.attr.dominant_color_name},
                      {"hue_hist", o.attr.hue_hist},
                      {"local_blur_score", o.attr.local_blur_score}}},
    };
}

DetectedObject objectFromJson(const json& obj) {
    DetectedObject d;
    d.class_name = obj.value("class", "");
    d.x = obj.value("x", 0.0);
    d.y = obj.value("y", 0.0);
    d.w = obj.value("w", 0.0);
    d.h = obj.value("h", 0.0);
    d.area = obj.value("area", 0.0);
    d.confidence = obj.value("confidence", 0.0);
    d.original_class = obj.value("original_class", "");
    d.super_class = obj.value("super_class", "");
    d.is_fallback = obj.value("is_fallback", false);
    d.parent_id = obj.value("parent_id", -1);
    d.obj_id = obj.value("obj_id", -1);
    d.img_id = obj.value("img_id", -1);
    if (obj.contains("attr")) {
        const auto& at = obj["attr"];
        d.attr.h = at.value("h", 0.f);
        d.attr.s = at.value("s", 0.f);
        d.attr.v = at.value("v", 0.f);
        d.attr.lbp = at.value("lbp", 0.f);
        d.attr.h_std = at.value("h_std", 0.f);
        d.attr.s_std = at.value("s_std", 0.f);
        d.attr.v_std = at.value("v_std", 0.f);
        d.attr.color_temperature = at.value("color_temperature", 0.f);
        d.attr.dominant_color_name = at.value("dominant_color_name", "");
        d.attr.local_blur_score = at.value("local_blur_score", 0.f);
        if (at.contains("hue_hist")) {
            const auto& hh = at["hue_hist"];
            for (int i = 0; i < 32 && i < (int)hh.size(); ++i) d.attr.hue_hist[i] = hh[i].get<float>();
        }
    }
    return d;
}

// ---- ImageAttrs <-> JSON ----
json attrsToJson(const ImageAttrs& ia) {
    return json{
        {"color_temperature", ia.color_temperature},
        {"avg_hue", ia.avg_hue},
        {"avg_saturation", ia.avg_saturation},
        {"avg_value", ia.avg_value},
        {"dominant_color", ia.dominant_color},
        {"global_hue_hist", ia.global_hue_hist},
        {"luma_hist", ia.luma_hist},
        {"overexposure_score", ia.overexposure_score},
        {"underexposure_score", ia.underexposure_score},
        {"exposure_goodness", ia.exposure_goodness},
        {"global_blur_score", ia.global_blur_score},
        {"global_blur_raw", ia.global_blur_raw},
        {"camera_make", ia.camera_make},
        {"camera_model", ia.camera_model},
        {"iso", ia.iso},
        {"shutter_speed", ia.shutter_speed},
        {"aperture", ia.aperture},
        {"focal_length", ia.focal_length},
        {"datetime_original", ia.datetime_original},
        {"width", ia.width},
        {"height", ia.height},
        {"user_tags", ia.user_tags},
    };
}

ImageAttrs attrsFromJson(const json& ia) {
    ImageAttrs a;
    a.color_temperature = ia.value("color_temperature", 0.f);
    a.avg_hue = ia.value("avg_hue", 0.f);
    a.avg_saturation = ia.value("avg_saturation", 0.f);
    a.avg_value = ia.value("avg_value", 0.f);
    a.dominant_color = ia.value("dominant_color", "");
    a.overexposure_score = ia.value("overexposure_score", 0.f);
    a.underexposure_score = ia.value("underexposure_score", 0.f);
    a.exposure_goodness = ia.value("exposure_goodness", 1.f);
    a.global_blur_score = ia.value("global_blur_score", 0.f);
    a.global_blur_raw = ia.value("global_blur_raw", 0.f);
    a.camera_make = ia.value("camera_make", "");
    a.camera_model = ia.value("camera_model", "");
    a.iso = ia.value("iso", -1.f);
    a.shutter_speed = ia.value("shutter_speed", -1.f);
    a.aperture = ia.value("aperture", -1.f);
    a.focal_length = ia.value("focal_length", -1.f);
    a.datetime_original = ia.value("datetime_original", "");
    a.width = ia.value("width", 0);
    a.height = ia.value("height", 0);
    if (ia.contains("user_tags")) {
        for (const auto& [k, v] : ia["user_tags"].items()) a.user_tags[k] = v.get<std::string>();
    }
    if (ia.contains("global_hue_hist")) {
        const auto& hh = ia["global_hue_hist"];
        for (int i = 0; i < 32 && i < (int)hh.size(); ++i) a.global_hue_hist[i] = hh[i].get<float>();
    }
    if (ia.contains("luma_hist")) {
        const auto& lh = ia["luma_hist"];
        for (int i = 0; i < 64 && i < (int)lh.size(); ++i) a.luma_hist[i] = lh[i].get<float>();
    }
    return a;
}

}  // namespace

bool CacheIndex::loadFromFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;

    try {
        json j = json::parse(f);
        if (j.value("version", "") != version) return false;

        model_name = j.value("model_name", "");
        photo_dirs.clear();
        for (const auto& d : j.value("photo_dirs", json::array())) photo_dirs.push_back(d);
        next_obj_id = j.value("next_obj_id", 0);
        next_img_id = j.value("next_img_id", 0);

        entries.clear();
        for (auto& [rel, e] : j["entries"].items()) {
            CacheEntry ce;
            ce.path = rel;
            ce.mtime = e.value("mtime", (int64_t)0);
            ce.size = e.value("size", (int64_t)0);
            ce.img_id = e.value("img_id", -1);
            if (e.contains("img_attrs")) ce.img_attrs = attrsFromJson(e["img_attrs"]);
            for (const auto& o : e.value("objects", json::array())) ce.objects.push_back(objectFromJson(o));
            entries[rel] = std::move(ce);
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool CacheIndex::loadLegacyFromFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;

    try {
        json j = json::parse(f);

        entries.clear();
        model_name = j.value("model", "");
        photo_dirs.clear();
        for (const auto& d : j.value("photo_dirs", json::array())) photo_dirs.push_back(d);
        next_obj_id = j.value("next_obj_id", 0);
        next_img_id = 0;

        int img_idx = 0;
        for (const auto& entry : j.value("images", json::array())) {
            CacheEntry e;
            e.path = entry.value("path", "");
            e.mtime = entry.value("last_modified", (int64_t)0);
            e.size = 0;  // unknown for legacy entries; 0 disables size comparison
            e.img_id = img_idx++;
            if (entry.contains("img_attrs")) e.img_attrs = attrsFromJson(entry["img_attrs"]);
            for (const auto& o : entry.value("objects", json::array())) e.objects.push_back(objectFromJson(o));
            entries[e.path] = std::move(e);
        }
        next_img_id = img_idx;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool CacheIndex::saveToFile(const std::string& path) const {
    try {
        fs::create_directories(fs::path(path).parent_path());

        json j;
        j["version"] = version;
        j["model_name"] = model_name;
        j["photo_dirs"] = photo_dirs;
        j["next_obj_id"] = next_obj_id;
        j["next_img_id"] = next_img_id;

        json e = json::object();
        for (const auto& [rel, ce] : entries) {
            json entry;
            entry["mtime"] = ce.mtime;
            entry["size"] = ce.size;
            entry["img_id"] = ce.img_id;
            entry["img_attrs"] = attrsToJson(ce.img_attrs);
            json objs = json::array();
            for (const auto& o : ce.objects) objs.push_back(objectToJson(o));
            entry["objects"] = objs;
            e[rel] = std::move(entry);
        }
        j["entries"] = e;

        std::ofstream out(path, std::ios::trunc);
        if (!out) return false;
        out << j.dump(2);
        out.close();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void CacheIndex::refreshCounters() {
    next_obj_id = 0;
    next_img_id = 0;
    for (const auto& [rel, ce] : entries) {
        next_img_id = std::max(next_img_id, ce.img_id + 1);
        for (const auto& o : ce.objects) next_obj_id = std::max(next_obj_id, o.obj_id + 1);
    }
}
