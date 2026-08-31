#pragma once
#include <QObject>
#include <QStringList>
#include <QHash>

// CRUD for "smart collections" — saved DSL queries that act as dynamic albums.
// Stored in config/smart_collections.json (same directory as settings.ini) as
// a flat JSON object: {"<name>": "<dsl string>"}.  Smart collections are
// re-executed every time they are opened, so their contents stay live.
class SmartCollectionManager : public QObject {
    Q_OBJECT
public:
    static SmartCollectionManager* instance();
    static QString filePath();

    QStringList names() const;
    QString dslFor(const QString& name) const;
    bool save(const QString& name, const QString& dsl);  // add or update
    bool rename(const QString& oldName, const QString& newName);
    bool remove(const QString& name);

    // Rewrite image path references inside saved DSL strings after files are
    // renamed (old relative path -> new relative path).
    void updateImagePaths(const QHash<QString, QString>& oldToNew);

signals:
    void smartCollectionsChanged();

private:
    explicit SmartCollectionManager(QObject* parent = nullptr);
    void reload();
    bool persist() const;
    QHash<QString, QString> map_;
};
