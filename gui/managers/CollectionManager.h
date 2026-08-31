#pragma once
#include <QObject>
#include <QStringList>
#include <QHash>
#include <QJsonObject>

// CRUD for user-created virtual albums ("collections").  A collection is a
// logical grouping of image relative paths, persisted inside the engine's
// cache_index.json under the top-level "collections" field (same place as the
// per-image user tags).  Collections never move files on disk.
class CollectionManager : public QObject {
    Q_OBJECT
public:
    static CollectionManager* instance();

    // Absolute path of the active model's cache_index.json.
    static QString cacheFilePath();
    // Whole cache_index.json parsed from disk ({} on missing / invalid file).
    static QJsonObject loadIndex();
    static bool saveIndex(const QJsonObject& root);

    QStringList names() const;
    QStringList imagesIn(const QString& name) const;

    bool create(const QString& name);
    bool rename(const QString& oldName, const QString& newName);
    bool remove(const QString& name);
    bool addImages(const QString& name, const QStringList& relPaths);
    bool removeImage(const QString& name, const QString& relPath);

    // Rewrite image references across ALL collections after files are renamed
    // (map old relative path -> new relative path).  Returns true when any
    // collection changed.
    static bool updateImagePaths(const QHash<QString, QString>& oldToNew);

    // ---- clustering (V2) ----
    // Invert the per-image cluster_groups for a clustering pack: returns
    // cluster_id -> image relative paths (for GUI sidebar grouping).
    static QHash<QString, QStringList> clusterImages(const QString& clusterName);

signals:
    void collectionsChanged();

private:
    explicit CollectionManager(QObject* parent = nullptr);
};
