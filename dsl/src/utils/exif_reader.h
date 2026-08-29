#pragma once
#include <string>

// Minimal, dependency-free EXIF reader (JPEG APP1 "Exif" segment + TIFF/IFD).
//
// Extracts the fields the DSL exposes via the img_camera / img_iso / ... macros.
// Any parse failure leaves the fields at their defaults, so the engine never
// crashes on malformed or missing metadata.  (A future swap to exiv2 only needs
// to reimplement this function.)
struct ExifInfo {
    std::string make;
    std::string model;
    std::string datetime_original;
    float iso = -1;
    float shutter_speed = -1;   // seconds
    float aperture = -1;        // f-number
    float focal_length = -1;    // mm
    bool has_exif = false;
};

// Parse EXIF from a JPEG file.  Returns false when the file is not a JPEG with
// a readable Exif APP1 segment.
bool readExifFromJpeg(const std::string& path, ExifInfo& out);
