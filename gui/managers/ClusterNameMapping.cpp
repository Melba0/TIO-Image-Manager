#include "ClusterNameMapping.h"
#include "SettingsManager.h"
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
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

ClusterNameMapping* ClusterNameMapping::instance() {
    static ClusterNameMapping mgr;
    return &mgr;
}

ClusterNameMapping::ClusterNameMapping(QObject* parent) : QObject(parent) {
    reload();
}

QString ClusterNameMapping::filePath() {
    return SettingsManager::projectDir() + "/config/cluster_name_mappings.json";
}

void ClusterNameMapping::reload() {
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

bool ClusterNameMapping::persist() const {
    QStringList keys = map_.keys();
    std::sort(keys.begin(), keys.end());
    QJsonObject obj;
    for (const QString& k : keys) obj[k] = map_.value(k);
    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QFile f(filePath());
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

bool ClusterNameMapping::hasMapping(const QString& clusterId) const {
    return map_.contains(clusterId);
}

QString ClusterNameMapping::displayName(const QString& clusterId,
                                        const QString& clusterName) const {
    if (map_.contains(clusterId)) return map_.value(clusterId);
    // Strip the "<cluster_name>_" prefix for a readable raw id (e.g.
    // "face_cluster_person_001" -> "person_001").
    if (!clusterName.isEmpty() && clusterId.startsWith(clusterName + "_")) {
        return clusterId.mid(clusterName.size() + 1);
    }
    int sep = clusterId.indexOf('_');
    if (sep > 0 && sep < clusterId.size() - 1) return clusterId.mid(sep + 1);
    return clusterId;
}

void ClusterNameMapping::setMapping(const QString& clusterId, const QString& displayName) {
    const QString clean = displayName.trimmed();
    if (clusterId.isEmpty()) return;
    if (clean.isEmpty()) {
        map_.remove(clusterId);
    } else {
        map_[clusterId] = clean;
    }
    persist();
    emit mappingsChanged();
}

void ClusterNameMapping::removeMapping(const QString& clusterId) {
    if (map_.remove(clusterId) > 0) {
        persist();
        emit mappingsChanged();
    }
}
