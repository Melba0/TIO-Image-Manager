#pragma once
#include <QDialog>
#include <QVector>
#include <QString>
#include <QJsonObject>
#include <QHash>
#include <functional>

class QLineEdit;
class QComboBox;
class QLabel;
class QTableWidget;

struct BatchImage {
    QString rel;
    QString full;
    double score = 0;
};

// Batch editing of several selected images: add/remove user tags, set the
// "评分" (rating) tag, and bulk-rename files using a template with {index},
// {date}, {scene} and {object} placeholders.  Tag edits go straight into
// cache_index.json; renaming also moves the files on disk and keeps the cache
// entries, collections and smart collections in sync.
class BatchEditDialog : public QDialog {
    Q_OBJECT
public:
    explicit BatchEditDialog(const QVector<BatchImage>& images, QWidget* parent = nullptr);
    bool changed() const { return changed_; }

private:
    QJsonObject loadIndex() const;
    bool saveIndex(const QJsonObject& root) const;

    void applyAddTag();
    void applyRemoveTag();
    void applyRating();
    void previewRename();
    void doRename();

    // Patch img_attrs for every selected image in one read-modify-write pass.
    void patchAll(const std::function<void(QJsonObject&, QJsonObject&)>& fn);
    QString renderTemplate(const QString& tpl, int index,
                           const QJsonObject& attrs, const QJsonObject& entry) const;
    void refreshDelKeys(const QJsonObject& root);

    QVector<BatchImage> images_;
    QLineEdit* addKey_ = nullptr;
    QLineEdit* addValue_ = nullptr;
    QComboBox* delKey_ = nullptr;
    QComboBox* rating_ = nullptr;
    QLineEdit* tpl_ = nullptr;
    QTableWidget* preview_ = nullptr;
    QLabel* status_ = nullptr;
    QHash<QString, QString> pendingRename_;  // rel -> new rel (after preview)
    QStringList existingKeys_;               // user-tag keys across the selection
    bool changed_ = false;
};
