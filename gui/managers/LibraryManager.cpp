#include "LibraryManager.h"
#include <QDir>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrent>

LibraryManager* LibraryManager::instance() {
    static LibraryManager mgr;
    return &mgr;
}

LibraryManager::LibraryManager(QObject* parent) : QObject(parent) {}

QStringList LibraryManager::paths() const {
    return SettingsManager::instance()->galleryPaths();
}

void LibraryManager::addPath(const QString& path) {
    SettingsManager::instance()->addGalleryPath(path);
    emit statusMessage(QString("已添加图库路径: %1").arg(path));
    rescanAsync();
}

void LibraryManager::removePath(const QString& path) {
    SettingsManager::instance()->removeGalleryPath(path);
    emit statusMessage(QString("已从图库移除（未删除文件）: %1").arg(path));
    rescanAsync();
}

void LibraryManager::setPaths(const QStringList& paths) {
    SettingsManager::instance()->setGalleryPaths(paths);
    rescanAsync();
}

qint64 LibraryManager::countIn(const QString& dir) {
    qint64 n = 0;
    QDir d(dir);
    if (!d.exists()) return 0;
    const QStringList exts = {"jpg", "jpeg", "png", "bmp", "webp"};
    const QFileInfoList entries = d.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : entries) {
        if (fi.isDir()) {
            n += countIn(fi.absoluteFilePath());
        } else if (fi.isFile()) {
            if (exts.contains(fi.suffix().toLower())) ++n;
        }
    }
    return n;
}

qint64 LibraryManager::countImages() {
    qint64 n = 0;
    for (const QString& p : paths()) n += countIn(p);
    return n;
}

void LibraryManager::rescanAsync() {
    QStringList plist = paths();
    QFutureWatcher<qint64>* watcher = new QFutureWatcher<qint64>(this);
    connect(watcher, &QFutureWatcher<qint64>::finished, this, [this, watcher]() {
        emit countReady(watcher->result());
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([plist]() {
        qint64 n = 0;
        for (const QString& p : plist) n += countIn(p);
        return n;
    }));
    emit statusMessage("正在扫描图库...");
}