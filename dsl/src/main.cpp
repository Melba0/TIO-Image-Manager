#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <iomanip>
#include <cmath>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include "parser/Lexer.h"
#include "parser/Parser.h"
#include <stdexcept>
#include "executor/Evaluator.h"
#include "executor/Context.h"
#include <stdexcept>
#include "cache/CacheManager.h"
#include "utils/filesystem_utils.h"
#include "ExtensionManager.h"
#include "ModelRegistry.h"
#include <stdexcept>
#include "BuiltinMacros.h"
namespace fs = std::filesystem;

struct GdiPlusGuard {
    ULONG_PTR token_ = 0;
    GdiPlusGuard() {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&token_, &input, nullptr);
    }
    ~GdiPlusGuard() { Gdiplus::GdiplusShutdown(token_); }
};

static void printUsage() {
    std::cout << "Usage: dsl [options] [script.dsl]\n"
              << "  --base <name>    override the active base model (from models/registry.json)\n"
              << "  --dsl <code>     inline DSL code (with --json)\n"
              << "  --photo <dir>    add an image directory to index (repeatable)\n"
              << "  --list-models    list all registered base models and exit\n"
              << "  --json           output the query result as JSON to stdout\n"
              << "                   (DSL read from --dsl, a script file, or stdin until EOF)\n"
              << "  --warmup         build/refresh the inference cache and exit (no query)\n"
              << "  --hard           boolean filtering at score 0.5 instead of soft ranking\n"
              << "  -h, --help       show this help\n"
              << "No arguments starts the interactive REPL.\n";
}

static void printValue(const Value& v) {
    switch (v.type) {
        case Value::IMAGE_SET: {
            // Sort by fuzzy score ascending (ties broken by path).
            std::vector<std::pair<float, std::string>> items;
            for (const auto& kv : v.image_scores) items.emplace_back(kv.second, kv.first);
            std::stable_sort(items.begin(), items.end(),
                             [](const auto& a, const auto& b) {
                                 if (a.first != b.first) return a.first < b.first;
                                 return a.second < b.second;
                             });
            std::cout << "ImageSet (" << items.size() << " images, sorted by score):" << std::endl;
            for (const auto& it : items) {
                std::cout << "  [" << std::fixed << std::setprecision(3) << it.first
                          << "] " << it.second << std::endl;
            }
            if (items.empty()) std::cout << "  (empty)" << std::endl;
            break;
        }
        case Value::OBJECT_SET: {
            std::cout << "ObjectSet (" << v.object_set.size() << " objects):" << std::endl;
            for (const auto& obj : v.object_set) {
                std::cout << "  [" << obj.image_path << "] " << obj.class_name;
                if (!obj.super_class.empty() && obj.super_class != obj.class_name) {
                    std::cout << " (super=" << obj.super_class << ")";
                }
                if (obj.is_fallback) {
                    std::cout << " [fallback from " << obj.original_class << "]";
                }
                std::cout << " (x=" << obj.x << ", y=" << obj.y
                          << ", w=" << obj.w << ", h=" << obj.h
                          << ", area=" << obj.area << ", conf=" << obj.confidence;
                if (obj.parent_id >= 0) std::cout << ", parent=" << obj.parent_id;
                std::cout << ", id=" << obj.obj_id
                          << ", score=" << (obj.score > 0 ? obj.score : (float)obj.confidence)
                          << ")" << std::endl;
            }
            if (v.object_set.empty()) std::cout << "  (empty)" << std::endl;
            break;
        }
        case Value::NUM:
            std::cout << v.num_val << std::endl;
            break;
        case Value::SCORE:
            std::cout << v.score_val << std::endl;
            break;
        case Value::BOOL:
            std::cout << (v.bool_val ? "true" : "false") << std::endl;
            break;
        case Value::STRING:
            std::cout << "\"" << v.str_val << "\"" << std::endl;
            break;
        case Value::OBJECT:
            std::cout << "Object: [" << v.object.image_path << "] " << v.object.class_name
                      << " (area=" << v.object.area << ", conf=" << v.object.confidence
                      << ", id=" << v.object.obj_id << ")" << std::endl;
            break;
        case Value::ATTR:
            std::cout << "Attr(h=" << v.attr.h << ", s=" << v.attr.s
                      << ", v=" << v.attr.v << ", lbp=" << v.attr.lbp << ")" << std::endl;
            break;
        default:
            std::cout << "(none)" << std::endl;
            break;
    }
}

static void runREPL(CacheManager& cache, std::unique_ptr<Context>& ctx,
                    std::unique_ptr<Evaluator>& evaluator,
                    ModelRegistry& registry, ExtensionManager& ext_mgr,
                    bool hard_mode) {
    std::string line;
    std::cout << "DSL Interpreter v5.0 (active base: " << registry.getActiveBaseName()
              << ")  type '/reload' to reload config, 'exit' to quit" << std::endl;

    while (true) {
        std::cout << "dsl> ";
        std::getline(std::cin, line);
        if (!std::cin || line == "exit" || line == "quit") break;

        if (line.empty()) continue;

        // ---- hot reload: re-read registry.json ----
        if (line == "/reload") {
            std::string old_base = registry.getActiveBaseName();
            if (registry.reload()) {
                ext_mgr.setActiveExtensions(registry.getActiveExtensions());
                std::string new_base = registry.getActiveBaseName();
                if (new_base != old_base && registry.hasBaseModel(new_base)) {
                    cache.invalidate();
                    ctx = std::make_unique<Context>(cache.getPhotoCache(), &registry, &ext_mgr);
                    registerBuiltinMacros(*ctx);
                    evaluator = std::make_unique<Evaluator>(*ctx);
                    evaluator->setHardMode(hard_mode);
                    std::cout << "[Reload] Switched base model to '" << new_base
                              << "'. Cache will rebuild on the next query." << std::endl;
                } else {
                    std::cout << "[Reload] Config reloaded (base model unchanged: '"
                              << new_base << "')." << std::endl;
                }
            } else {
                std::cout << "[Reload] Failed to reload registry.json." << std::endl;
            }
            continue;
        }

        if (!cache.ensureCacheReady()) {
            std::cerr << "Error: failed to prepare cache for base model '"
                      << registry.getActiveBaseName() << "'." << std::endl;
            continue;
        }

        try {
            Lexer lexer(line);
            Parser parser(lexer);
            auto program = parser.parseProgram();

            Value result = evaluator->evaluate(*program);
            printValue(result);
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }
}

static void runFile(const std::string& filename, CacheManager& cache, Context& ctx,
                    bool hard_mode) {
    std::string source = readFile(filename);
    if (source.empty()) {
        std::cerr << "Failed to read file: " << filename << std::endl;
        return;
    }

    try {
        Lexer lexer(source);
        Parser parser(lexer);
        auto program = parser.parseProgram();

        Evaluator evaluator(ctx);
        evaluator.setHardMode(hard_mode);
        Value result = evaluator.evaluate(*program);
        // Per spec: if `out` was explicitly assigned, print that instead.
        if (ctx.hasVariable("out")) {
            result = ctx.getVariable("out");
        }
        printValue(result);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

// --json mode: evaluate the DSL and write a single JSON object to stdout.
//   images: {"type":"images","photo_dir":...,"photo_roots":...,"results":[{"path","score"},...]}  (ascending)
//   scalar: {"type":"scalar","value":...}
//   error:  {"type":"error","message":...}
static void runJson(const std::string& dsl_code, Context& ctx, Evaluator& evaluator,
                    const std::string& photo_dir, const std::vector<std::string>& photo_dirs) {
    nlohmann::json out;
    out["photo_dir"] = photo_dir;
    {
        nlohmann::json roots = nlohmann::json::array();
        for (const auto& d : photo_dirs) {
            roots.push_back({{"prefix", fs::path(d).filename().string()}, {"dir", d}});
        }
        out["photo_roots"] = roots;
    }
    try {
        Lexer lexer(dsl_code);
        Parser parser(lexer);
        auto program = parser.parseProgram();
        Value result = evaluator.evaluate(*program);
        if (ctx.hasVariable("out")) {
            result = ctx.getVariable("out");
        }

        if (result.type == Value::IMAGE_SET || result.type == Value::OBJECT_SET) {
            out["type"] = "images";
            std::unordered_map<std::string, float> by_img;
            if (result.type == Value::IMAGE_SET) {
                for (const auto& kv : result.image_scores) by_img[kv.first] = kv.second;
            } else {
                for (const auto& o : result.object_set) {
                    float s = o.score > 0 ? o.score : (float)o.confidence;
                    auto it = by_img.find(o.image_path);
                    if (it == by_img.end() || s > it->second) by_img[o.image_path] = s;
                }
            }
            std::vector<std::pair<float, std::string>> items;
            for (const auto& kv : by_img) items.emplace_back(kv.second, kv.first);
            std::stable_sort(items.begin(), items.end(),
                             [](const auto& a, const auto& b) {
                                 if (a.first != b.first) return a.first < b.first;
                                 return a.second < b.second;
                             });
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& it : items) {
                arr.push_back({{"path", it.second}, {"score", it.first}});
            }
            out["results"] = arr;
        } else {
            out["type"] = "scalar";
            if (result.type == Value::NUM) out["value"] = result.num_val;
            else if (result.type == Value::SCORE) out["value"] = result.score_val;
            else if (result.type == Value::BOOL) out["value"] = result.bool_val;
            else if (result.type == Value::STRING) out["value"] = result.str_val;
            else out["value"] = nullptr;
        }
    } catch (const std::exception& e) {
        out["type"] = "error";
        out["message"] = e.what();
    }

    std::string json_str = out.dump();
    std::fwrite(json_str.data(), 1, json_str.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

// On Windows, convert the UTF-16 command line into UTF-8 arguments so non-ASCII
// tag keys/values and paths survive (argv from the CRT is ANSI-encoded).
std::vector<std::string> utf8Args() {
#ifdef _WIN32
    int n = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &n);
    std::vector<std::string> args;
    for (int i = 0; i < n; ++i) {
        int size = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(size > 0 ? (size_t)size - 1 : 0, '\0');
        if (size > 1) WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, &s[0], size, nullptr, nullptr);
        args.push_back(std::move(s));
    }
    LocalFree(wargv);
    return args;
#else
    std::vector<std::string> args;
    // Not used on non-Windows builds.
    return args;
#endif
}

int main(int argc, char* argv[]) {
    GdiPlusGuard gdiplus_guard;

    // Use UTF-8 for console input/output so Chinese class names round-trip.
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    // Parse command line options (UTF-8 on Windows).
    std::vector<std::string> args = utf8Args();
    std::string base_override;
    std::string dsl_file;
    std::string dsl_inline;
    std::vector<std::string> photo_dirs;
    std::vector<TagFilter> tag_filters;
    bool list_models = false;
    bool hard_mode = false;
    bool json_mode = false;
    bool warmup_mode = false;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--base" && i + 1 < args.size()) {
            base_override = args[++i];
        } else if (a == "--dsl" && i + 1 < args.size()) {
            dsl_inline = args[++i];
        } else if (a == "--photo" && i + 1 < args.size()) {
            photo_dirs.push_back(args[++i]);
        } else if (a == "--tag-filter" && i + 1 < args.size()) {
            // Syntax: key=v1|v2 (values are OR-ed; empty value list = any value)
            const std::string& spec = args[++i];
            size_t eq = spec.find('=');
            TagFilter f;
            if (eq != std::string::npos) {
                f.key = spec.substr(0, eq);
                std::string vals = spec.substr(eq + 1);
                if (!vals.empty()) {
                    size_t pos = 0, sep;
                    while ((sep = vals.find('|', pos)) != std::string::npos) {
                        f.values.push_back(vals.substr(pos, sep - pos));
                        pos = sep + 1;
                    }
                    f.values.push_back(vals.substr(pos));
                }
            } else {
                f.key = spec;
            }
            tag_filters.push_back(std::move(f));
        } else if (a == "--list-models") {
            list_models = true;
        } else if (a == "--json") {
            json_mode = true;
        } else if (a == "--warmup") {
            warmup_mode = true;
            json_mode = true;   // route progress to stderr, keep stdout pure JSON
        } else if (a == "--hard") {
            hard_mode = true;
        } else if (a == "-h" || a == "--help") {
            printUsage();
            return 0;
        } else if (!a.empty() && a[0] == '-') {
            std::cerr << "Unknown option: " << a << std::endl;
            printUsage();
            return 1;
        } else {
            dsl_file = a;
        }
    }

    // In --json mode route all engine informational output to stderr so stdout
    // stays pure JSON (the GUI reads stdout only).
    if (json_mode) {
        std::cout.rdbuf(std::cerr.rdbuf());
    }

    // Resolve paths relative to the executable so the program works from any CWD.
    std::string exe_dir;
    if (!args.empty() && !args[0].empty()) {
        exe_dir = fs::path(args[0]).parent_path().string();
    }
    if (exe_dir.empty() || !fs::exists(exe_dir)) {
        exe_dir = fs::current_path().string();
    }

    // Walk upward from the exe to find the project directory (one containing models/).
    std::string project_dir;
    auto probe = fs::path(exe_dir);
    while (true) {
        if (fs::exists(probe / "models" / "registry.json")) {
            project_dir = probe.string();
            break;
        }
        // GUI/engine may live next to the DSL project (tio/gui vs tio/dsl).
        if (fs::exists(probe / "dsl" / "models" / "registry.json")) {
            project_dir = (probe / "dsl").string();
            break;
        }
        if (!probe.has_parent_path() || probe.parent_path() == probe) break;
        probe = probe.parent_path();
    }
    if (project_dir.empty()) {
        std::cerr << "Project directory (containing models/registry.json) not found." << std::endl;
        return 1;
    }

    // Photos live in a sibling directory named "photo" of the project dir.
    // --photo (repeatable) overrides/extends the default gallery directories.
    std::string photo_dir;
    auto parent_photo = (fs::path(project_dir).parent_path() / "photo").string();
    auto own_photo = (fs::path(project_dir) / "photo").string();
    if (fs::exists(parent_photo)) photo_dir = parent_photo;
    else if (fs::exists(own_photo)) photo_dir = own_photo;
    if (photo_dir.empty()) {
        auto cwd = fs::current_path();
        if (fs::exists(cwd / "photo")) photo_dir = (cwd / "photo").string();
        else if (fs::exists(cwd.parent_path() / "photo")) photo_dir = (cwd.parent_path() / "photo").string();
    }
    if (!photo_dir.empty() && !fs::exists(photo_dir)) photo_dir.clear();
    if (photo_dirs.empty()) {
        if (photo_dir.empty()) {
            std::cerr << "Photo directory not found." << std::endl;
            return 1;
        }
        photo_dirs.push_back(photo_dir);
    } else {
        for (auto& d : photo_dirs) {
            if (!fs::exists(d)) {
                std::cerr << "Photo directory not found: " << d << std::endl;
                return 1;
            }
        }
    }
    photo_dir = photo_dirs[0];

    // ---- Model registry (base models + active switch) ----
    ModelRegistry registry((fs::path(project_dir) / "models").string(),
                           (fs::path(project_dir) / "cache").string());
    if (!registry.scan()) {
        std::cerr << "Failed to scan models/ registry." << std::endl;
        return 1;
    }

    if (list_models) {
        std::cout << "Registered base models:" << std::endl;
        for (const auto& m : registry.listAllBaseModels()) {
            std::cout << "  " << m.name
                      << "  (type=" << m.type
                      << ", input_size=" << m.input_size
                      << ", classes=" << m.classes << ")" << std::endl;
        }
        std::cout << "Active base: " << registry.getActiveBaseName() << std::endl;
        return 0;
    }

    if (!base_override.empty()) {
        if (!registry.setActiveBase(base_override)) {
            std::cerr << "Unknown base model: " << base_override << std::endl;
            std::cout << "Available: ";
            for (const auto& m : registry.listAllBaseModels()) std::cout << m.name << " ";
            std::cout << std::endl;
            return 1;
        }
        std::cout << "[CLI] Base model overridden to: " << base_override << std::endl;
    }

    const ModelInfo* base = registry.getActiveBaseModel();
    if (!base) {
        std::cerr << "No active base model configured." << std::endl;
        return 1;
    }

    std::cout << "[Main] active base model: " << base->name << " (" << base->path << ")" << std::endl;
    std::cout << "[Main] photo dir: " << photo_dir << std::endl;

    // Load configuration (confidence thresholds).  The GUI persists these in
    // config/settings.ini under [inference] (written with QSettings); that is
    // the single source of truth for the engine.
    const std::string settings_ini = (fs::path(project_dir) / "config" / "settings.ini").string();
    float fallback_threshold = 0.0f;
    float base_conf_threshold = 0.25f;
    float iou_threshold = 0.45f;

    auto readIniFloat = [](const std::string& path, const std::string& section,
                           const std::string& key, float def) -> float {
        std::ifstream f(path);
        if (!f) return def;
        std::string line;
        std::string cur_section;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::string t = line;
            size_t b = t.find_first_not_of(" \t");
            if (b == std::string::npos) continue;
            t = t.substr(b);
            if (t.empty() || t[0] == ';' || t[0] == '#') continue;
            if (t.front() == '[') {
                size_t close = t.find(']');
                cur_section = (close == std::string::npos) ? "" : t.substr(1, close - 1);
                continue;
            }
            size_t eq = t.find('=');
            if (eq == std::string::npos) continue;
            std::string k = t.substr(0, eq);
            size_t kb = k.find_last_not_of(" \t");
            if (kb == std::string::npos) continue;
            k = k.substr(0, kb + 1);
            if (cur_section != section || k != key) continue;
            std::string v = t.substr(eq + 1);
            size_t vb = v.find_first_not_of(" \t");
            if (vb != std::string::npos) v = v.substr(vb);
            size_t ve = v.find_last_not_of(" \t");
            if (ve != std::string::npos) v = v.substr(0, ve + 1);
            // QSettings quotes strings but numbers are written bare; strip any quotes.
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
            try {
                return std::stof(v);
            } catch (...) {
                return def;
            }
        }
        return def;
    };

    {
        float ini_fb = readIniFloat(settings_ini, "inference", "fallback_threshold", NAN);
        float ini_bc = readIniFloat(settings_ini, "inference", "base_conf_threshold", NAN);
        float ini_iou = readIniFloat(settings_ini, "inference", "iou_threshold", NAN);
        if (!std::isnan(ini_fb)) fallback_threshold = ini_fb;
        if (!std::isnan(ini_bc)) base_conf_threshold = ini_bc;
        if (!std::isnan(ini_iou)) iou_threshold = ini_iou;
    }

    std::cout << "[Main] thresholds: base_conf=" << base_conf_threshold
              << " iou=" << iou_threshold
              << " fallback=" << fallback_threshold << std::endl;

    // Object id generator shared by cache builder and extension expansion.
    ObjectIdGenerator id_gen;

    // Extension packs: primary location models/extensions, legacy extensions/ too.
    std::vector<std::string> ext_dirs = {
        (fs::path(project_dir) / "models" / "extensions").string(),
        (fs::path(project_dir) / "extensions").string(),
    };
    ExtensionManager ext_mgr(ext_dirs, photo_dir, registry, id_gen);
    ext_mgr.scan();
    ext_mgr.setActiveExtensions(registry.getActiveExtensions());
    std::cout << "[Main] extension packs: " << ext_mgr.extensions().size()
              << ", active: " << ext_mgr.activeExtensions().size() << std::endl;

    CacheManager cache(photo_dirs, (fs::path(project_dir) / "cache").string(),
                       registry, id_gen,
                       fallback_threshold, base_conf_threshold, iou_threshold,
                       (fs::path(project_dir) / "models" / "scene" / "places365_googlenet.onnx").string(),
                       (fs::path(project_dir) / "models" / "scene" / "categories_places365.txt").string());
    if (!cache.ensureCacheReady()) {
        std::cerr << "Failed to load or build cache." << std::endl;
        return 1;
    }

    // --warmup: cache is ready; the GUI calls this at startup so the first
    // real query is fast.  Emit a JSON summary on stdout and exit.
    if (warmup_mode) {
        nlohmann::json out;
        out["type"] = "warmup";
        out["photo_dir"] = photo_dir;
        out["images"] = (int)cache.getPhotoCache().images.size();
        std::string json_str = out.dump();
        std::fwrite(json_str.data(), 1, json_str.size(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
        return 0;
    }

    std::unique_ptr<Context> ctx =
        std::make_unique<Context>(cache.getPhotoCache(), &registry, &ext_mgr);
    registerBuiltinMacros(*ctx);

    // Load the Places365 scene labels into the context so img_scene("name")
    // can map scene names to vector indices.  Empty when the file is missing.
    {
        std::vector<std::string> scene_labels;
        std::ifstream slf((fs::path(project_dir) / "models" / "scene" / "categories_places365.txt").string());
        std::string line;
        while (slf && std::getline(slf, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            // "/b/beach 48" -> "beach"  (drop trailing index + path prefix)
            std::string name = line;
            size_t sp = name.find_last_of(" \t");
            if (sp != std::string::npos) {
                bool all_digits = true;
                for (size_t k = sp + 1; k < name.size(); ++k) {
                    if (!std::isdigit((unsigned char)name[k])) { all_digits = false; break; }
                }
                if (all_digits) name = name.substr(0, sp);
            }
            size_t sl = name.find_last_of('/');
            if (sl != std::string::npos) name = name.substr(sl + 1);
            if (!name.empty()) scene_labels.push_back(name);
        }
        ctx->setSceneLabels(scene_labels);
    }

    std::unique_ptr<Evaluator> evaluator = std::make_unique<Evaluator>(*ctx);
    evaluator->setHardMode(hard_mode);

    // Wire asset-management hooks:
    //  * the `del` statement deletes files + cache entries through CacheManager
    //  * an active tag pre-filter makes `$` iterate only the matching images
    ctx->setDeleteImagesCallback([&cache](const std::vector<std::string>& rels) {
        return cache.removeImages(rels);
    });
    if (!tag_filters.empty()) {
        auto pre = cache.applyTagFilters(tag_filters);
        std::cout << "[Main] Tag pre-filter active: " << pre.size()
                  << " / " << cache.getPhotoCache().images.size() << " images." << std::endl;
        ctx->setPrefilteredIds(std::move(pre));
    }

    if (json_mode) {
        // DSL from --dsl, a script file, or stdin (until EOF).
        std::string code = dsl_inline;
        if (code.empty() && !dsl_file.empty()) {
            code = readFile(dsl_file);
        }
        if (code.empty()) {
            std::ostringstream ss;
            ss << std::cin.rdbuf();
            code = ss.str();
        }
        runJson(code, *ctx, *evaluator, photo_dir, photo_dirs);
        return 0;
    }

    if (!dsl_file.empty()) {
        runFile(dsl_file, cache, *ctx, hard_mode);
    } else {
        runREPL(cache, ctx, evaluator, registry, ext_mgr, hard_mode);
    }

    return 0;
}