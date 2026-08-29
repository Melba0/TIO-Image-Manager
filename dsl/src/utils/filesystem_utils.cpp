#include "filesystem_utils.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace fs = std::filesystem;

std::vector<std::string> listImageFiles(const std::string& dir) {
    std::vector<std::string> result;
    if (!fs::exists(dir)) return result;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".jpg" || ext == ".jpeg" || ext == ".png") {
                result.push_back(entry.path().filename().string());
            }
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

int64_t getFileModifiedTime(const std::string& path) {
    auto ft = fs::last_write_time(path);
#ifdef _WIN32
    // MSVC std::filesystem: file_time_type uses the 1601-01-01 FILETIME epoch
    // with 100ns ticks. Convert exactly to Unix seconds (deterministic).
    constexpr int64_t kTicksPerSecond = 10'000'000LL;
    constexpr int64_t kFiletimeToUnixEpoch = 11644473600LL;  // seconds 1601 -> 1970
    return ft.time_since_epoch().count() / kTicksPerSecond - kFiletimeToUnixEpoch;
#else
    // Portable conversion from the filesystem clock to the system clock.
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::duration_cast<std::chrono::seconds>(
        sctp.time_since_epoch()).count();
#endif
}

int64_t getFileSize(const std::string& path) {
    std::error_code ec;
    auto sz = fs::file_size(path, ec);
    return ec ? -1 : (int64_t)sz;
}

std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) return "";
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::string getParentDir(const std::string& path) {
    return fs::path(path).parent_path().string();
}

std::string getFilename(const std::string& path) {
    return fs::path(path).filename().string();
}

bool isAbsolute(const std::string& path) {
    return fs::path(path).is_absolute();
}

std::string normalizePath(const std::string& path) {
    auto p = fs::path(path);
    p.make_preferred();
    return p.string();
}