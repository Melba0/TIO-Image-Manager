#include "exif_reader.h"
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>

namespace {

// Big-endian / little-endian helpers over a byte buffer.
struct ExifReader {
    const uint8_t* p;
    size_t n;
    bool le;

    bool inRange(size_t off, size_t len) const { return off + len <= n && off + len >= off; }

    uint16_t u16(size_t off) const {
        if (!inRange(off, 2)) return 0;
        return le ? (uint16_t)(p[off] | (p[off + 1] << 8))
                  : (uint16_t)((p[off] << 8) | p[off + 1]);
    }
    uint32_t u32(size_t off) const {
        if (!inRange(off, 4)) return 0;
        if (le) return (uint32_t)p[off] | ((uint32_t)p[off + 1] << 8) |
                       ((uint32_t)p[off + 2] << 16) | ((uint32_t)p[off + 3] << 24);
        return ((uint32_t)p[off] << 24) | ((uint32_t)p[off + 1] << 16) |
               ((uint32_t)p[off + 2] << 8) | (uint32_t)p[off + 3];
    }
    // TIFF RATIONAL = two uint32 (numerator, denominator).
    double rational(size_t off) const {
        uint32_t num = u32(off), den = u32(off + 4);
        if (den == 0) return 0.0;
        return (double)num / (double)den;
    }
    // ASCII value (NULL-terminated within the buffer).
    std::string ascii(size_t off, uint32_t len) const {
        if (!inRange(off, len)) return {};
        std::string s((const char*)p + off, (size_t)len);
        auto z = s.find('\0');
        if (z != std::string::npos) s.resize(z);
        // trim trailing spaces
        while (!s.empty() && s.back() == ' ') s.pop_back();
        return s;
    }
};

}  // namespace

bool readExifFromJpeg(const std::string& path, ExifInfo& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    if (data.size() < 8) return false;

    // JPEG SOI marker.
    if (data[0] != 0xFF || data[1] != 0xD8) return false;

    // Walk the JPEG segment table looking for APP1 "Exif\0\0".
    size_t pos = 2;
    const uint8_t* p = data.data();
    size_t n = data.size();
    while (pos + 4 <= n) {
        if (p[pos] != 0xFF) { pos += 1; continue; }
        uint8_t marker = p[pos + 1];
        if (marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7)) { pos += 2; continue; }
        if (marker == 0xDA) break;  // start of scan data
        if (pos + 4 > n) break;
        uint16_t segLen = (uint16_t)((p[pos + 2] << 8) | p[pos + 3]);
        if (segLen < 2 || pos + 2 + segLen > n) break;
        if (marker == 0xE1 && segLen >= 6 && std::memcmp(p + pos + 4, "Exif\0\0", 6) == 0) {
            // TIFF header starts at pos + 10.
            size_t tiff = pos + 10;
            if (tiff + 8 > n) return false;
            ExifReader r{p, n, false};
            r.le = (p[tiff] == 'I' && p[tiff + 1] == 'I');
            bool mm = (p[tiff] == 'M' && p[tiff + 1] == 'M');
            if (!r.le && !mm) return false;
            if (r.u16(tiff + 2) != 42) return false;
            uint32_t ifd0 = r.u32(tiff + 4);
            if (tiff + ifd0 + 2 > n) return false;

            uint32_t exif_ifd = 0;
            // IFD0 tags.
            size_t e0 = tiff + ifd0;
            uint16_t cnt0 = r.u16(e0);
            for (uint16_t i = 0; i < cnt0; ++i) {
                size_t e = e0 + 2 + (size_t)i * 12;
                if (e + 12 > n) break;
                uint16_t tag = r.u16(e);
                uint16_t type = r.u16(e + 2);
                uint32_t len = r.u32(e + 4);
                uint32_t voff = r.u32(e + 8);
                auto valOffset = [&](size_t base) -> size_t {
                    return (type == 1 || type == 2 || type == 6 || type == 7)
                               ? (len <= 4 ? base : tiff + voff)
                               : (len <= 2 ? base : tiff + voff);
                };
                size_t base = e + 8;
                switch (tag) {
                    case 0x010F: out.make = r.ascii(valOffset(base), len); break;
                    case 0x0110: out.model = r.ascii(valOffset(base), len); break;
                    case 0x0132: out.datetime_original = r.ascii(valOffset(base), len); break;
                    case 0x8769: exif_ifd = r.u32(valOffset(base)); break;
                    default: break;
                }
            }

            // Exif IFD.
            if (exif_ifd != 0 && tiff + exif_ifd + 2 <= n) {
                size_t e1 = tiff + exif_ifd;
                uint16_t cnt1 = r.u16(e1);
                for (uint16_t i = 0; i < cnt1; ++i) {
                    size_t e = e1 + 2 + (size_t)i * 12;
                    if (e + 12 > n) break;
                    uint16_t tag = r.u16(e);
                    uint16_t type = r.u16(e + 2);
                    uint32_t len = r.u32(e + 4);
                    uint32_t voff = r.u32(e + 8);
                    auto valOffset = [&](size_t base) -> size_t {
                        return (type == 1 || type == 2 || type == 6 || type == 7)
                                   ? (len <= 4 ? base : tiff + voff)
                                   : (len <= 2 ? base : tiff + voff);
                    };
                    size_t base = e + 8;
                    switch (tag) {
                        case 0x829A: // ExposureTime (RATIONAL)
                            if (type == 5) out.shutter_speed = (float)r.rational(valOffset(base));
                            break;
                        case 0x829D: // FNumber (RATIONAL)
                            if (type == 5) out.aperture = (float)r.rational(valOffset(base));
                            break;
                        case 0x8827: // ISOSpeedRatings (SHORT)
                            out.iso = (float)r.u16(valOffset(base));
                            break;
                        case 0x920A: // FocalLength (RATIONAL)
                            if (type == 5) out.focal_length = (float)r.rational(valOffset(base));
                            break;
                        case 0x9003: // DateTimeOriginal (ASCII)
                            if (out.datetime_original.empty())
                                out.datetime_original = r.ascii(valOffset(base), len);
                            break;
                        default: break;
                    }
                }
            }
            out.has_exif = true;
            return true;
        }
        pos += 2 + segLen;
    }
    return false;
}
