#include "CacheManager.h"
#include "../utils/filesystem_utils.h"
#include "../cluster/Clustering.h"
#include "ExtensionManager.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <iostream>

namespace fs = std::filesystem;

CacheManager::CacheManager(const std::vector<std::string>& photo_dirs, const std::string& cache_root,
                           ModelRegistry& registry,
                           ObjectIdGenerator& id_gen,
                           float fallback_threshold, float base_conf_threshold, float iou_threshold,
                           const std::string& scene_model_path, const std::string& scene_labels_path)
    : photo_dirs_(photo_dirs), cache_root_(cache_root), registry_(registry),
      id_gen_(id_gen),
      fallback_threshold_(fallback_threshold), base_conf_threshold_(base_conf_threshold),
      iou_threshold_(iou_threshold),
      scene_model_path_(scene_model_path), scene_labels_path_(scene_labels_path) {
    if (photo_dirs_.empty()) photo_dirs_.push_back(fs::current_path().string());
    refreshCachePaths();
}

std::string CacheManager::relImagePath(const std::string& dir, const std::string& file) const {
    if (photo_dirs_.size() <= 1) return file;
    return (fs::path(dir).filename().string()) + "/" + file;
}

void CacheManager::refreshCachePaths() {
    cache_dir_ = registry_.getBaseCacheDir();
    cache_file_ = (fs::path(cache_dir_) / "cache_index.json").string();
    if (!fs::exists(cache_dir_)) {
        fs::create_directories(cache_dir_);
    }
}

bool CacheManager::ensureCacheReady() {
    if (cache_valid_) return true;
    return loadOrBuildCache();
}

bool CacheManager::loadOrBuildCache() {
    if (loadIndex()) {
        return applyIncrementalUpdate();
    }
    return buildIndexFromScratch();
}

void CacheManager::invalidate() {
    yolo_.reset();
    index_.entries.clear();
    cache_valid_ = false;
    cache_data_.images.clear();
    image_index_.clear();
    refreshCachePaths();
}

// ---- index I/O ----

bool CacheManager::loadIndex() {
    if (index_.loadFromFile(cache_file_)) {
        id_gen_.set(index_.next_obj_id);
        std::cout << "[Cache] Loaded " << index_.entries.size() << " images from cache index." << std::endl;
        return true;
    }
    // Legacy migration: the pre-incremental builds wrote metadata.json.
    std::string legacy = (fs::path(cache_dir_) / "metadata.json").string();
    if (index_.loadLegacyFromFile(legacy)) {
        saveIndex();   // write the new format so the next startup skips migration
        id_gen_.set(index_.next_obj_id);
        std::cout << "[Cache] Migrated " << index_.entries.size() << " images from legacy metadata.json." << std::endl;
        return true;
    }
    return false;
}

bool CacheManager::saveIndex() {
    index_.refreshCounters();
    id_gen_.set(index_.next_obj_id);
    return index_.saveToFile(cache_file_);
}

// ---- incremental update ----

std::vector<CacheEntry> CacheManager::scanPhotoDirs() const {
    std::vector<CacheEntry> result;
    for (const auto& dir : photo_dirs_) {
        auto images = listImageFiles(dir);
        for (const auto& img : images) {
            auto full = (fs::path(dir) / img).string();
            CacheEntry e;
            e.path = relImagePath(dir, img);
            e.mtime = getFileModifiedTime(full);
            e.size = getFileSize(full);
            result.push_back(std::move(e));
        }
    }
    return result;
}

std::string resolvePhotoPath(const std::string& rel, const std::vector<std::string>& photo_dirs) {
    if (photo_dirs.size() <= 1) {
        return (photo_dirs.empty() ? fs::path(rel) : fs::path(photo_dirs[0]) / rel).string();
    }
    // rel = "<dirbasename>/<file>"
    size_t slash = rel.find('/');
    if (slash == std::string::npos || slash + 1 >= rel.size()) return rel;
    std::string dirname = rel.substr(0, slash);
    std::string file = rel.substr(slash + 1);
    for (const auto& d : photo_dirs) {
        if (fs::path(d).filename().string() == dirname) return (fs::path(d) / file).string();
    }
    return rel;
}

bool CacheManager::applyIncrementalUpdate() {
    auto current = scanPhotoDirs();

    std::unordered_map<std::string, CacheEntry> current_map;
    for (auto& e : current) current_map[e.path] = e;

    // Diff the gallery against the loaded index.
    std::vector<CacheEntry> to_infer;       // added + modified
    std::vector<std::string> to_remove;     // deleted files
    bool changed = false;

    for (auto& e : current) {
        auto it = index_.entries.find(e.path);
        if (it == index_.entries.end()) {
            to_infer.push_back(e);                       // new file
            changed = true;
        } else if (it->second.mtime != e.mtime ||
                   (it->second.size != 0 && it->second.size != e.size)) {
            to_infer.push_back(e);                       // modified file
            changed = true;
        }
    }
    for (const auto& [path, entry] : index_.entries) {
        if (current_map.find(path) == current_map.end()) {
            to_remove.push_back(path);                   // deleted file
            changed = true;
        }
    }

    if (!changed) {
        // Fast path: nothing to do, just materialize the in-memory cache.
        std::cout << "[Cache] Cache is up to date (" << index_.entries.size() << " images)." << std::endl;
        rebuildPhotoCache();
        cache_valid_ = true;
        return true;
    }

    for (const auto& p : to_remove) {
        index_.entries.erase(p);
    }
    if (!to_remove.empty()) {
        std::cout << "[Cache] Incremental update: removed " << to_remove.size() << " image(s)." << std::endl;
    }

    if (!to_infer.empty()) {
        std::cout << "[Cache] Incremental update: inferring " << to_infer.size()
                  << " image(s) (" << index_.entries.size() << " cached)." << std::endl;
        for (auto& e : to_infer) {
            auto it = index_.entries.find(e.path);
            if (it != index_.entries.end()) {
                e.img_id = it->second.img_id;   // keep the stable image id on modification
            } else {
                e.img_id = index_.next_img_id++;
            }

            std::string full = resolvePhotoPath(e.path, photo_dirs_);
            std::cout << "[Cache] Processing: " << e.path << " ..." << std::endl;
            if (!inferEntry(full, e)) {
                std::cerr << "[Cache] Skipped image (inference failed): " << e.path << std::endl;
                continue;
            }
            index_.entries[e.path] = std::move(e);
        }
        // New / changed objects may shift global cluster assignments.
        runClustering();
    }

    saveIndex();
    rebuildPhotoCache();
    cache_valid_ = true;
    return true;
}

// Re-run the clustering pass for every active clustering pack.  Cluster ids are
// deterministic: the candidate objects are visited in (path, obj_id) order, so
// the same gallery always yields the same ids (and GUI renames stay valid as
// long as the cache is not rebuilt).
void CacheManager::runClustering() {
    if (!ext_mgr_) return;
    auto packs = ext_mgr_->clusterPacks();
    if (packs.empty()) return;

    bool changed = false;
    for (const auto* pack : packs) {
        struct Item {
            std::string rel;
            DetectedObject* obj;
            const std::vector<float>* emb;
        };
        std::vector<Item> items;
        for (auto& [rel, ce] : index_.entries) {
            for (auto& o : ce.objects) {
                auto it = o.embeddings.find(pack->embedding_name);
                if (it != o.embeddings.end()) items.push_back({rel, &o, &it->second});
            }
        }
        if (items.empty()) continue;

        // Deterministic visit order -> stable cluster ids across runs.
        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
            if (a.rel != b.rel) return a.rel < b.rel;
            return a.obj->obj_id < b.obj->obj_id;
        });

        std::vector<std::vector<float>> embs;
        embs.reserve(items.size());
        for (const auto& it : items) embs.push_back(*it.emb);

        auto labels = clusterDbscan(embs, pack->cluster_threshold, 1);
        if (labels.size() != items.size()) continue;

        std::string parent = pack->parent_class.empty() ? "item" : pack->parent_class;
        std::unordered_map<int, std::string> id_by_label;
        int counter = 0;
        for (size_t i = 0; i < items.size(); ++i) {
            int lab = labels[i];
            if (lab < 0) continue;
            if (!id_by_label.count(lab)) {
                ++counter;
                id_by_label[lab] = pack->cluster_name + "_" + parent + "_" + zeroPad(counter, 3);
            }
            items[i].obj->cluster_ids[pack->cluster_name] = id_by_label[lab];
        }

        // Rebuild per-image cluster_groups for this cluster name.
        for (auto& [rel, ce] : index_.entries) {
            ce.img_attrs.cluster_groups[pack->cluster_name].clear();
        }
        for (const auto& it : items) {
            auto cid = it.obj->cluster_ids.find(pack->cluster_name);
            if (cid == it.obj->cluster_ids.end()) continue;
            auto& groups = index_.entries[it.rel].img_attrs.cluster_groups[pack->cluster_name];
            if (std::find(groups.begin(), groups.end(), cid->second) == groups.end()) {
                groups.push_back(cid->second);
            }
        }
        for (auto& [rel, ce] : index_.entries) {
            std::sort(ce.img_attrs.cluster_groups[pack->cluster_name].begin(),
                      ce.img_attrs.cluster_groups[pack->cluster_name].end());
        }
        std::cout << "[Cache] Clustering '" << pack->cluster_name << "': "
                  << items.size() << " objects, " << counter << " clusters." << std::endl;
        changed = true;
    }

    if (changed) {
        saveIndex();
        rebuildPhotoCache();
    }
}

bool CacheManager::buildIndexFromScratch() {
    const ModelInfo* base = registry_.getActiveBaseModel();
    if (!base) {
        std::cerr << "[Cache] No active base model in registry." << std::endl;
        return false;
    }

    std::cout << "[Cache] No cache index found; building full cache for base model '"
              << base->name << "' (input_size=" << base->input_size << ") ..." << std::endl;

    index_.entries.clear();
    index_.model_name = base->name;
    index_.photo_dirs = photo_dirs_;
    index_.next_img_id = 0;

    for (const auto& dir : photo_dirs_) {
        auto images = listImageFiles(dir);
        for (const auto& img : images) {
            auto full_path = (fs::path(dir) / img).string();
            CacheEntry e;
            e.path = relImagePath(dir, img);
            e.mtime = getFileModifiedTime(full_path);
            e.size = getFileSize(full_path);
            e.img_id = index_.next_img_id++;

            std::cout << "[Cache] Processing: " << e.path << " ..." << std::endl;
            if (!inferEntry(full_path, e)) {
                std::cerr << "[Cache] Skipped image (inference failed): " << e.path << std::endl;
                continue;
            }
            index_.entries[e.path] = std::move(e);
        }
    }
    if (index_.entries.empty()) {
        std::cerr << "[Cache] No images found in any photo directory." << std::endl;
        return false;
    }

    runClustering();   // assign cluster ids over the freshly built gallery

    saveIndex();
    rebuildPhotoCache();
    cache_valid_ = true;
    return true;
}

bool CacheManager::inferEntry(const std::string& full_path, CacheEntry& e) {
    if (!yolo_) {
        const ModelInfo* base = registry_.getActiveBaseModel();
        if (!base) return false;
        auto class_names = registry_.getOutputClassNames();
        yolo_ = std::make_unique<YoloInference>(base->path, class_names,
                                                base->input_size, base_conf_threshold_,
                                                iou_threshold_);
        if (!yolo_->valid()) {
            std::cerr << "[Cache] Failed to load model: " << base->path << std::endl;
            yolo_.reset();
            return false;
        }
    }

    auto detections = yolo_->detect(full_path);
    e.img_attrs = yolo_->detectImageAttrs(full_path);

    // ---- Places365 scene recognition (independent of YOLO, same raw image) ----
    // Load the scene model lazily on first use; absent model = degrade to
    // empty scene data (macros return 0.0 / "").
    if (!scene_ && !scene_tried_) {
        scene_tried_ = true;
        if (!scene_model_path_.empty() && !scene_labels_path_.empty()) {
            auto scene = std::make_unique<SceneInference>();
            if (scene->loadModel(scene_model_path_, scene_labels_path_)) {
                scene_ = std::move(scene);
            } else {
                std::cerr << "[Cache] Scene recognition disabled (model not loaded)." << std::endl;
            }
        } else {
            std::cerr << "[Cache] Scene recognition disabled (no scene model configured)." << std::endl;
        }
    }
    if (scene_) {
        e.img_attrs.scene_vector = scene_->getSceneVector(full_path);
        e.img_attrs.dominant_scene = scene_->getDominantScene(full_path);
        // indoor = sum of the 205 indoor classes (indices 0..204).
        double indoor = 0.0;
        for (int i = 0; i < 205; ++i) indoor += e.img_attrs.scene_vector[i];
        e.img_attrs.indoor_score = (float)indoor;
    }

    e.objects.clear();
    for (const auto& det : detections) {
        DetectedObject d;
        d.class_name = det.class_name;
        d.x = det.x;
        d.y = det.y;
        d.w = det.w;
        d.h = det.h;
        d.area = det.area;
        d.confidence = det.confidence;
        d.attr = det.attr;
        d = applyFallback(d);
        d.img_id = e.img_id;
        d.obj_id = id_gen_.next();
        e.objects.push_back(d);
    }

    // ---- clustering packs: extract embeddings for matching objects ----
    // Each active clustering pack may attach an embedding to the objects of its
    // parent class.  Persisted with the object so the global clustering pass
    // (runClustering) can re-run over old + new embeddings without re-inference.
    if (ext_mgr_) {
        auto packs = ext_mgr_->clusterPacks();
        if (!packs.empty()) {
            for (auto& obj : e.objects) {
                for (const auto* pack : packs) {
                    if (obj.class_name != pack->parent_class && obj.super_class != pack->parent_class) {
                        continue;
                    }
                    if (obj.embeddings.count(pack->embedding_name)) continue;
                    auto emb = ext_mgr_->extractEmbedding(full_path, obj, pack->name);
                    if (emb.size() >= 8) obj.embeddings[pack->embedding_name] = std::move(emb);
                }
            }
        }
    }
    return true;
}

void CacheManager::rebuildPhotoCache() {
    cache_data_.images.clear();
    cache_data_.photo_dir = photo_dirs_.empty() ? "" : photo_dirs_[0];
    cache_data_.collections = index_.collections;
    image_index_.clear();

    std::vector<std::pair<std::string, const CacheEntry*>> sorted;
    sorted.reserve(index_.entries.size());
    for (const auto& [rel, ce] : index_.entries) sorted.emplace_back(rel, &ce);
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    for (const auto& [rel, ce] : sorted) {
        ImageCacheEntry ic;
        ic.path = rel;
        ic.last_modified = ce->mtime;
        ic.img_attrs = ce->img_attrs;
        ic.objects = ce->objects;
        image_index_[rel] = cache_data_.images.size();
        cache_data_.images.push_back(std::move(ic));
    }
}

DetectedObject CacheManager::applyFallback(const DetectedObject& det) const {
    DetectedObject out = det;
    std::string super = registry_.getParent(det.class_name);

    // Degrade to the parent class only when the confidence is low AND the
    // parent is meaningful (not the default "root").
    if (det.confidence < fallback_threshold_ && !super.empty() && super != "root") {
        out.original_class = det.class_name;
        out.class_name = super;
        out.super_class = super;
        out.is_fallback = true;
    } else {
        out.original_class.clear();
        out.super_class = super;
        out.is_fallback = false;
    }
    return out;
}

const std::vector<DetectedObject>* CacheManager::getObjectsForImage(const std::string& rel_path) const {
    auto it = image_index_.find(rel_path);
    if (it != image_index_.end()) {
        return &cache_data_.images[it->second].objects;
    }
    return nullptr;
}

std::vector<std::string> CacheManager::removeImages(const std::vector<std::string>& relPaths) {
    std::vector<std::string> removed;
    for (const auto& rel : relPaths) {
        auto it = index_.entries.find(rel);
        if (it == index_.entries.end()) {
            // Maybe an absolute path was given: find the entry whose resolved
            // full path equals it.
            std::string key;
            for (const auto& [k, ce] : index_.entries) {
                if (resolvePhotoPath(k, photo_dirs_) == rel) { key = k; break; }
            }
            if (key.empty()) continue;
            it = index_.entries.find(key);
        }

        std::string full = resolvePhotoPath(it->first, photo_dirs_);
        std::error_code ec;
        if (fs::exists(full) && !fs::remove(full, ec)) {
            std::cerr << "[Cache] Failed to delete file: " << full << std::endl;
        }
        index_.entries.erase(it);
        removed.push_back(rel);
    }
    if (!removed.empty()) {
        saveIndex();
        rebuildPhotoCache();
        std::cout << "[Cache] Deleted " << removed.size() << " image(s)." << std::endl;
    }
    return removed;
}

std::unordered_set<std::string> CacheManager::applyTagFilters(const std::vector<TagFilter>& filters) const {
    std::unordered_set<std::string> result;
    if (filters.empty()) return result;
    for (const auto& [rel, ce] : index_.entries) {
        bool match = true;
        for (const auto& f : filters) {
            auto it = ce.img_attrs.user_tags.find(f.key);
            if (it == ce.img_attrs.user_tags.end()) { match = false; break; }
            if (!f.values.empty()) {
                bool in = false;
                for (const auto& v : f.values) {
                    if (it->second == v) { in = true; break; }
                }
                if (!in) { match = false; break; }
            }
        }
        if (match) result.insert(rel);
    }
    return result;
}

// ---- virtual album (collection) management ----

void CacheManager::addToCollection(const std::string& name, const std::string& rel_path) {
    if (name.empty() || rel_path.empty()) return;
    auto& paths = index_.collections[name];
    if (std::find(paths.begin(), paths.end(), rel_path) == paths.end()) {
        paths.push_back(rel_path);
        saveIndex();
        rebuildPhotoCache();
    }
}

void CacheManager::removeFromCollection(const std::string& name, const std::string& rel_path) {
    auto it = index_.collections.find(name);
    if (it == index_.collections.end()) return;
    auto& paths = it->second;
    paths.erase(std::remove(paths.begin(), paths.end(), rel_path), paths.end());
    saveIndex();
    rebuildPhotoCache();
}

void CacheManager::deleteCollection(const std::string& name) {
    if (index_.collections.erase(name) > 0) {
        saveIndex();
        rebuildPhotoCache();
    }
}

bool CacheManager::renameImage(const std::string& old_rel, const std::string& new_rel) {
    auto it = index_.entries.find(old_rel);
    if (it == index_.entries.end()) return false;
    if (old_rel == new_rel || new_rel.empty()) return true;

    CacheEntry e = std::move(it->second);
    e.path = new_rel;
    index_.entries.erase(it);
    index_.entries[new_rel] = std::move(e);

    // Keep the relative path references in every collection in sync.
    for (auto& [name, paths] : index_.collections) {
        for (auto& p : paths) {
            if (p == old_rel) p = new_rel;
        }
    }
    saveIndex();
    rebuildPhotoCache();
    return true;
}
