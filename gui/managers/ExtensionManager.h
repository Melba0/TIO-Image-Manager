#pragma once
#include <QObject>
#include <QList>
#include "SettingsManager.h"

struct ExtPack {
    QString name;
    QString parentClass;
    QStringList children;
    QString path;       // absolute folder
    bool active = false;
};

// Scans extension packs (models/extensions + legacy extensions) and toggles
// their enabled state by editing models/registry.json's active_extensions list.
class ExtensionManager : public QObject {
    Q_OBJECT
public:
    static ExtensionManager* instance();

    void scan();
    QList<ExtPack> packs() const;
    QStringList activeExtensions() const;      // from registry.json
    bool setActive(const QString& name, bool on);
    bool addPack(const QString& srcFolder);
    bool removePack(const QString& name);

signals:
    void packsChanged();

private:
    explicit ExtensionManager(QObject* parent = nullptr);
    QString registryFile() const { return SettingsManager::projectDir() + "/models/registry.json"; }
    QString extDir() const { return SettingsManager::projectDir() + "/models/extensions"; }
    QList<ExtPack> packs_;
};
