#include "ModelManager.h"
#include "FileUtils.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

ModelManager* ModelManager::instance() {
    static ModelManager mgr;
    return &mgr;
}

ModelManager::ModelManager(QObject* parent) : QObject(parent) {}

void ModelManager::scan() {
    models_.clear();
    QDir dir(baseDir());
    for (const QFileInfo& fi : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!QFile::exists(fi.filePath() + "/model.onnx")) continue;
        ModelInfo info;
        info.name = fi.fileName();
        info.path = fi.absoluteFilePath();
        // meta.json
        QFile meta(fi.filePath() + "/meta.json");
        if (meta.open(QIODevice::ReadOnly)) {
            QJsonObject o = QJsonDocument::fromJson(meta.readAll()).object();
            info.type = o["type"].toString();
            info.inputSize = o["input_size"].toInt(640);
            info.classes = o["classes"].toInt(80);
        }
        models_.append(info);
    }
    emit modelsChanged();
}

QList<ModelInfo> ModelManager::models() const {
    return models_;
}

QString ModelManager::activeModel() const {
    QFile f(registryFile());
    if (f.open(QIODevice::ReadOnly)) {
        return QJsonDocument::fromJson(f.readAll()).object()["active_base"].toString();
    }
    return QString();
}

bool ModelManager::setActiveModel(const QString& name) {
    bool found = false;
    for (const ModelInfo& m : models_) {
        if (m.name == name) { found = true; break; }
    }
    if (!found) return false;

    QFile f(registryFile());
    if (!f.open(QIODevice::ReadWrite)) return false;
    QJsonObject reg = QJsonDocument::fromJson(f.readAll()).object();
    reg["active_base"] = name;
    f.resize(0);
    f.write(QJsonDocument(reg).toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

bool ModelManager::addModel(const QString& srcFolder) {
    QFileInfo src(srcFolder);
    if (!src.isDir()) return false;
    if (!QFile::exists(srcFolder + "/model.onnx")) return false;
    QString name = src.fileName();
    QDir dst(baseDir());
    QString target = dst.filePath(name);
    if (QFile::exists(target)) return false;  // already exists

    copyDirRecursive(srcFolder, target);
    scan();
    return QFile::exists(target + "/model.onnx");
}

bool ModelManager::removeModel(const QString& name) {
    QString target = baseDir() + "/" + name;
    if (!QFile::exists(target)) return false;
    QDir(target).removeRecursively();
    scan();
    return true;
}