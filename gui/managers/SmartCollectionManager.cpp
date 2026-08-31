#include "SmartCollectionManager.h"
#include "SettingsManager.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <algorithm>

namespace {

QByteArray stripBom(const QByteArray& raw) {
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF &&
        (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) {
        return raw.mid(3);
    }
    return raw;
}

}  // namespace

SmartCollectionManager* SmartCollectionManager::instance() {
    static SmartCollectionManager mgr;
    return &mgr;
}

SmartCollectionManager::SmartCollectionManager(QObject* parent) : QObject(parent) {
    reload();
}

QString SmartCollectionManager::filePath() {
    return SettingsManager::projectDir() + "/config/smart_collections.json";
}

void SmartCollectionManager::reload() {
    map_.clear();
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(stripBom(f.readAll()), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) return;
    QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        map_.insert(it.key(), it.value().toString());
    }
}

QStringList SmartCollectionManager::names() const {
    QStringList keys = map_.keys();
    std::sort(keys.begin(), keys.end());
    return keys;
}

QString SmartCollectionManager::dslFor(const QString& name) const {
    return map_.value(name);
}

bool SmartCollectionManager::save(const QString& name, const QString& dsl) {
    const QString clean = name.trimmed();
    if (clean.isEmpty()) return false;
    map_[clean] = dsl;
    if (!persist()) return false;
    emit smartCollectionsChanged();
    return true;
}

bool SmartCollectionManager::rename(const QString& oldName, const QString& newName) {
    const QString clean = newName.trimmed();
    if (clean.isEmpty() || clean == oldName) return false;
    if (!map_.contains(oldName)) return false;
    QString dsl = map_.take(oldName);
    map_[clean] = dsl;
    if (!persist()) return false;
    emit smartCollectionsChanged();
    return true;
}

bool SmartCollectionManager::remove(const QString& name) {
    if (map_.remove(name) == 0) return false;
    if (!persist()) return false;
    emit smartCollectionsChanged();
    return true;
}

bool SmartCollectionManager::persist() const {
    QJsonObject obj;
    QStringList keys = map_.keys();
    std::sort(keys.begin(), keys.end());
    for (const QString& k : keys) obj[k] = map_.value(k);
    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QFile f(filePath());
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

void SmartCollectionManager::updateImagePaths(const QHash<QString, QString>& oldToNew) {
    if (oldToNew.isEmpty()) return;
    bool changed = false;
    for (auto it = map_.begin(); it != map_.end(); ++it) {
        QString dsl = it.value();
        bool c = false;
        for (auto p = oldToNew.begin(); p != oldToNew.end(); ++p) {
            // Replace old rel path with the new one wherever it appears as a
            // quoted literal in the DSL (e.g. del "old.jpg" / collection("name")).
            const QString oldQ = '"' + p.key() + '"';
            const QString newQ = '"' + p.value() + '"';
            if (dsl.contains(oldQ)) {
                dsl.replace(oldQ, newQ);
                c = true;
            }
        }
        if (c) { it.value() = dsl; changed = true; }
    }
    if (changed) {
        persist();
        emit smartCollectionsChanged();
    }
}
