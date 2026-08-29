#pragma once
#include <QObject>
#include <QList>
#include "SettingsManager.h"

struct ModelInfo {
    QString name;       // folder name (package id)
    QString path;       // absolute folder
    QString type;
    int inputSize = 640;
    int classes = 80;
};

// Scans models/base, reads meta.json, and toggles the active base model by
// editing models/registry.json (the engine picks it up on the next query).
class ModelManager : public QObject {
    Q_OBJECT
public:
    static ModelManager* instance();

    void scan();
    QList<ModelInfo> models() const;
    QString activeModel() const;                 // from registry.json
    bool setActiveModel(const QString& name);    // writes registry.json active_base
    bool addModel(const QString& srcFolder);     // copies folder into models/base
    bool removeModel(const QString& name);       // deletes the folder (UI confirms)

signals:
    void modelsChanged();

private:
    explicit ModelManager(QObject* parent = nullptr);
    QString baseDir() const { return SettingsManager::projectDir() + "/models/base"; }
    QString registryFile() const { return SettingsManager::projectDir() + "/models/registry.json"; }
    QList<ModelInfo> models_;
};
