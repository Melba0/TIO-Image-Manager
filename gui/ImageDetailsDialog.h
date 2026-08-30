#pragma once
#include <QDialog>
#include <QString>
#include <QJsonObject>
class QLabel;
class QTableWidget;

// Image details dialog: shows the cached metadata (exposure / sharpness / EXIF)
// of one image and lets the user edit its key-value tags.  Tags are persisted
// directly into the engine's cache_index.json so the DSL can query them.
class ImageDetailsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ImageDetailsDialog(const QString& relPath, const QString& fullPath,
                                QWidget* parent = nullptr);
    bool tagsChanged() const { return tagsModified_; }

private:
    QString cacheFilePath() const;
    bool loadEntry();                 // read the image entry from cache_index.json
    void reloadTagsTable();
    void saveTags();
    QStringList topScenes(const QJsonObject& attrs) const;  // top-5 "name (p%)" lines

    QString relPath_;
    QString fullPath_;
    QLabel* thumbLabel_ = nullptr;
    QLabel* infoLabel_ = nullptr;
    QTableWidget* tagsTable_ = nullptr;
    QJsonObject entry_;               // {"img_attrs": {...}, ...} for this image
    QJsonObject imgAttrs_;
    bool tagsModified_ = false;
};
