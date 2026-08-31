#include "CollectionManager.h"
#include "SettingsManager.h"
#include "ModelManager.h"
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>

namespace {

QByteArray stripBom(const QByteArray& raw) {
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF &&
        (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) {
        return raw.mid(3);
    }
    return raw;
}

}  // namespace

CollectionManager* CollectionManager::instance() {
    static CollectionManager mgr;
    return &mgr;
}

CollectionManager::CollectionManager(QObject* parent) : QObject(parent) {}

QString CollectionManager::cacheFilePath() {
    QString dir = SettingsManager::projectDir() + "/cache/"
                  + ModelManager::instance()->activeModel();
    return dir + "/cache_index.json";
}

QJsonObject CollectionManager::loadIndex() {
    QFile f(cacheFilePath());
    if (!f.open(QIODevice::ReadOnly)) return {};
    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(stripBom(f.readAll()), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) return {};
    return doc.object();
}

bool CollectionManager::saveIndex(const QJsonObject& root) {
    QFile f(cacheFilePath());
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

QStringList CollectionManager::names() const {
    QStringList result;
    QJsonObject root = loadIndex();
    QJsonObject cols = root["collections"].toObject();
    for (auto it = cols.begin(); it != cols.end(); ++it) result << it.key();
    return result;
}

QStringList CollectionManager::imagesIn(const QString& name) const {
    QStringList result;
    QJsonObject root = loadIndex();
    QJsonObject cols = root["collections"].toObject();
    for (const QJsonValue& v : cols.value(name).toArray()) result << v.toString();
    return result;
}

bool CollectionManager::create(const QString& name) {
    const QString clean = name.trimmed();
    if (clean.isEmpty()) return false;
    QJsonObject root = loadIndex();
    QJsonObject cols = root["collections"].toObject();
    if (cols.contains(clean)) return false;
    cols[clean] = QJsonArray();
    root["collections"] = cols;
    if (!saveIndex(root)) return false;
    emit collectionsChanged();
    return true;
}

bool CollectionManager::rename(const QString& oldName, const QString& newName) {
    const QString clean = newName.trimmed();
    if (clean.isEmpty() || clean == oldName) return false;
    QJsonObject root = loadIndex();
    QJsonObject cols = root["collections"].toObject();
    if (!cols.contains(oldName) || cols.contains(clean)) return false;
    QJsonValue v = cols.take(oldName);
    cols[clean] = v;
    root["collections"] = cols;
    if (!saveIndex(root)) return false;
    emit collectionsChanged();
    return true;
}

bool CollectionManager::remove(const QString& name) {
    QJsonObject root = loadIndex();
    QJsonObject cols = root["collections"].toObject();
    if (!cols.contains(name)) return false;
    cols.remove(name);
    root["collections"] = cols;
    if (!saveIndex(root)) return false;
    emit collectionsChanged();
    return true;
}

bool CollectionManager::addImages(const QString& name, const QStringList& relPaths) {
    if (name.trimmed().isEmpty() || relPaths.isEmpty()) return false;
    QJsonObject root = loadIndex();
    QJsonObject cols = root["collections"].toObject();
    QJsonArray arr = cols.value(name).toArray();
    QStringList existing;
    for (const QJsonValue& v : arr) existing << v.toString();
    bool changed = false;
    for (const QString& p : relPaths) {
        if (!existing.contains(p)) {
            existing << p;
            changed = true;
        }
    }
    if (!changed) return true;  // nothing to add (already present)
    QJsonArray newArr;
    for (const QString& p : existing) newArr << p;
    cols[name] = newArr;
    root["collections"] = cols;
    if (!saveIndex(root)) return false;
    emit collectionsChanged();
    return true;
}

bool CollectionManager::removeImage(const QString& name, const QString& relPath) {
    QJsonObject root = loadIndex();
    QJsonObject cols = root["collections"].toObject();
    QJsonArray arr = cols.value(name).toArray();
    QJsonArray newArr;
    for (const QJsonValue& v : arr) {
        if (v.toString() != relPath) newArr << v;
    }
    if (newArr.size() == arr.size()) return false;
    cols[name] = newArr;
    root["collections"] = cols;
    if (!saveIndex(root)) return false;
    emit collectionsChanged();
    return true;
}

bool CollectionManager::updateImagePaths(const QHash<QString, QString>& oldToNew) {
    if (oldToNew.isEmpty()) return false;
    QJsonObject root = loadIndex();
    QJsonObject cols = root["collections"].toObject();
    bool changed = false;
    for (auto it = cols.begin(); it != cols.end(); ++it) {
        QJsonArray arr = it.value().toArray();
        QJsonArray newArr;
        bool c = false;
        for (const QJsonValue& v : arr) {
            QString p = v.toString();
            QString np = oldToNew.value(p, QString());
            if (!np.isEmpty() && np != p) { newArr << np; c = true; }
            else newArr << v;
        }
        if (c) { it.value() = newArr; changed = true; }
    }
    if (!changed) return false;
    root["collections"] = cols;
    saveIndex(root);
    return true;
}

QHash<QString, QStringList> CollectionManager::clusterImages(const QString& clusterName) {
    QHash<QString, QStringList> result;
    QJsonObject root = loadIndex();
    QJsonObject entries = root["entries"].toObject();
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        const QString rel = it.key();
        QJsonObject groups = it.value().toObject()["img_attrs"].toObject()["cluster_groups"].toObject();
        QJsonArray ids = groups.value(clusterName).toArray();
        for (const QJsonValue& id : ids) {
            const QString cid = id.toString();
            if (cid.isEmpty()) continue;
            auto& list = result[cid];
            if (!list.contains(rel)) list << rel;
        }
    }
    return result;
}
