#pragma once
#include <QString>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>

// Recursively copy a directory tree (Qt has no built-in copyRecursively).
inline bool copyDirRecursive(const QString& src, const QString& dst) {
    QDir srcDir(src);
    QDir().mkpath(dst);
    QDirIterator it(src, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    while (it.hasNext()) {
        it.next();
        QString rel = srcDir.relativeFilePath(it.filePath());
        QString target = dst + "/" + rel;
        if (it.fileInfo().isDir()) {
            if (!copyDirRecursive(it.filePath(), target)) return false;
        } else {
            if (!QFile::copy(it.filePath(), target)) return false;
        }
    }
    return true;
}
