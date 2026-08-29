#pragma once
#include <QObject>
#include <QStringList>
#include "SettingsManager.h"

// Manages the gallery (image library) path list and provides asynchronous
// recursive image counting.  The engine itself performs indexing/caching.
class LibraryManager : public QObject {
    Q_OBJECT
public:
    static LibraryManager* instance();

    QStringList paths() const;               // from SettingsManager
    void addPath(const QString& path);
    void removePath(const QString& path);
    void setPaths(const QStringList& paths);

    // Count image files recursively over all gallery paths (async).
    void rescanAsync();
    qint64 countImages();                    // synchronous (small galleries)

signals:
    void countReady(qint64 count);
    void statusMessage(const QString& msg);

private:
    explicit LibraryManager(QObject* parent = nullptr);
    static qint64 countIn(const QString& dir);
};
