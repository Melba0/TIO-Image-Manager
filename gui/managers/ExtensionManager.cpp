#include "ExtensionManager.h"
#include "FileUtils.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

ExtensionManager* ExtensionManager::instance() {
    static ExtensionManager mgr;
    return &mgr;
}

ExtensionManager::ExtensionManager(QObject* parent) : QObject(parent) {}

QStringList ExtensionManager::activeExtensions() const {
    QFile f(registryFile());
    if (!f.open(QIODevice::ReadOnly)) return {};
    QStringList list;
    for (const QJsonValue& v : QJsonDocument::fromJson(f.readAll()).object()["active_extensions"].toArray()) {
        list << v.toString();
    }
    return list;
}

void ExtensionManager::scan() {
    packs_.clear();
    QStringList active = activeExtensions();

    QStringList dirs = {extDir(), SettingsManager::projectDir() + "/extensions"};
    for (const QString& d : dirs) {
        QDir dir(d);
        if (!dir.exists()) continue;
        for (const QFileInfo& fi : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QString cfgPath = fi.filePath() + "/config.json";
            if (!QFile::exists(cfgPath)) continue;
            ExtPack pack;
            pack.name = fi.fileName();
            pack.path = fi.absoluteFilePath();
            pack.active = active.contains(pack.name);
            QFile cfg(cfgPath);
            if (cfg.open(QIODevice::ReadOnly)) {
                QJsonObject o = QJsonDocument::fromJson(cfg.readAll()).object();
                pack.parentClass = o["parent_class"].toString();
                for (const QJsonValue& c : o["children"].toArray()) pack.children << c.toString();
            }
            packs_.append(pack);
        }
    }
    emit packsChanged();
}

QList<ExtPack> ExtensionManager::packs() const {
    return packs_;
}

bool ExtensionManager::setActive(const QString& name, bool on) {
    bool found = false;
    for (const ExtPack& p : packs_) {
        if (p.name == name) { found = true; break; }
    }
    if (!found) return false;

    QFile f(registryFile());
    if (!f.open(QIODevice::ReadWrite)) return false;
    QJsonObject reg = QJsonDocument::fromJson(f.readAll()).object();
    QJsonArray arr = reg["active_extensions"].toArray();
    QStringList list;
    for (const QJsonValue& v : arr) list << v.toString();
    if (on) {
        if (!list.contains(name)) list << name;
    } else {
        list.removeAll(name);
    }
    arr = QJsonArray();
    for (const QString& s : list) arr.append(s);
    reg["active_extensions"] = arr;
    f.resize(0);
    f.write(QJsonDocument(reg).toJson(QJsonDocument::Indented));
    f.close();
    scan();
    return true;
}

bool ExtensionManager::addPack(const QString& srcFolder) {
    QFileInfo src(srcFolder);
    if (!src.isDir()) return false;
    if (!QFile::exists(srcFolder + "/config.json")) return false;
    QString name = src.fileName();
    QDir dst(extDir());
    QString target = dst.filePath(name);
    if (QFile::exists(target)) return false;

    copyDirRecursive(srcFolder, target);
    scan();
    return QFile::exists(target + "/config.json");
}

bool ExtensionManager::removePack(const QString& name) {
    QString target = extDir() + "/" + name;
    if (!QFile::exists(target)) return false;
    setActive(name, false);  // also drop from active_extensions
    QDir(target).removeRecursively();
    scan();
    return true;
}