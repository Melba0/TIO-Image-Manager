#include "BatchEditDialog.h"
#include "managers/CollectionManager.h"
#include "managers/SmartCollectionManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMessageBox>
#include <QGroupBox>
#include <QColor>
#include <QSet>

namespace {

QString sanitizeName(const QString& s) {
    QString out;
    for (const QChar& ch : s) {
        if (ch.isPrint() && !QString("<>:\"/\\|?*").contains(ch)) out += ch;
    }
    return out.trimmed();
}

}  // namespace

BatchEditDialog::BatchEditDialog(const QVector<BatchImage>& images, QWidget* parent)
    : QDialog(parent), images_(images) {
    setWindowTitle(tr("Batch Edit") + QString(" (%1)").arg(images_.size()));
    resize(620, 560);

    QVBoxLayout* lay = new QVBoxLayout(this);

    // ---- tag operations ----
    QGroupBox* tagBox = new QGroupBox(tr("Tags"), this);
    QFormLayout* tagForm = new QFormLayout(tagBox);
    addKey_ = new QLineEdit(tagBox);
    addValue_ = new QLineEdit(tagBox);
    delKey_ = new QComboBox(tagBox);
    delKey_->setEditable(true);
    rating_ = new QComboBox(tagBox);
    for (int i = 1; i <= 5; ++i) rating_->addItem(QString::number(i) + " ★", i);
    QPushButton* addBtn = new QPushButton(tr("Add / Update Tag"), tagBox);
    QPushButton* delBtn = new QPushButton(tr("Remove Tag"), tagBox);
    QPushButton* rateBtn = new QPushButton(tr("Set Rating"), tagBox);
    tagForm->addRow(tr("Add tag (key):"), addKey_);
    tagForm->addRow(tr("Value:"), addValue_);
    tagForm->addRow(QString(), addBtn);
    tagForm->addRow(tr("Remove tag:"), delKey_);
    tagForm->addRow(QString(), delBtn);
    tagForm->addRow(tr("Rating:"), rating_);
    tagForm->addRow(QString(), rateBtn);
    lay->addWidget(tagBox);

    // ---- rename ----
    QGroupBox* renBox = new QGroupBox(tr("Rename Files"), this);
    QVBoxLayout* rl = new QVBoxLayout(renBox);
    QHBoxLayout* tplRow = new QHBoxLayout();
    tplRow->addWidget(new QLabel(tr("Template:"), renBox));
    tpl_ = new QLineEdit(renBox);
    tpl_->setPlaceholderText(tr("e.g. 2024-{date}-{scene}-{index}"));
    tplRow->addWidget(tpl_, 1);
    QPushButton* previewBtn = new QPushButton(tr("Preview"), renBox);
    QPushButton* doBtn = new QPushButton(tr("Apply Rename"), renBox);
    tplRow->addWidget(previewBtn);
    tplRow->addWidget(doBtn);
    rl->addLayout(tplRow);
    rl->addWidget(new QLabel(tr("Placeholders: {index} (1,2,...)  {date} (YYYYMMDD)  "
                                "{scene}  {object}"), renBox));
    preview_ = new QTableWidget(0, 3, renBox);
    preview_->setHorizontalHeaderLabels({tr("Original"), tr("New"), tr("Status")});
    preview_->horizontalHeader()->setStretchLastSection(true);
    preview_->setColumnWidth(0, 160);
    preview_->setColumnWidth(1, 180);
    rl->addWidget(preview_);
    lay->addWidget(renBox);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    lay->addWidget(status_);

    QPushButton* closeBtn = new QPushButton(tr("Close"), this);
    QHBoxLayout* bottom = new QHBoxLayout();
    bottom->addStretch();
    bottom->addWidget(closeBtn);
    lay->addLayout(bottom);

    connect(addBtn, &QPushButton::clicked, this, &BatchEditDialog::applyAddTag);
    connect(delBtn, &QPushButton::clicked, this, &BatchEditDialog::applyRemoveTag);
    connect(rateBtn, &QPushButton::clicked, this, &BatchEditDialog::applyRating);
    connect(previewBtn, &QPushButton::clicked, this, &BatchEditDialog::previewRename);
    connect(doBtn, &QPushButton::clicked, this, &BatchEditDialog::doRename);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    refreshDelKeys(loadIndex());
}

QJsonObject BatchEditDialog::loadIndex() const {
    return CollectionManager::loadIndex();
}

bool BatchEditDialog::saveIndex(const QJsonObject& root) const {
    return CollectionManager::saveIndex(root);
}

void BatchEditDialog::refreshDelKeys(const QJsonObject& root) {
    QSet<QString> keys;
    QJsonObject entries = root["entries"].toObject();
    for (const BatchImage& im : images_) {
        QJsonObject tags = entries.value(im.rel).toObject()["img_attrs"].toObject()["user_tags"].toObject();
        for (auto it = tags.begin(); it != tags.end(); ++it) keys.insert(it.key());
    }
    existingKeys_ = keys.values();
    std::sort(existingKeys_.begin(), existingKeys_.end());
    delKey_->clear();
    delKey_->addItems(existingKeys_);
    if (!existingKeys_.isEmpty()) delKey_->setCurrentIndex(0);
}

void BatchEditDialog::patchAll(const std::function<void(QJsonObject&, QJsonObject&)>& fn) {
    QJsonObject root = loadIndex();
    if (root.isEmpty()) {
        status_->setText(tr("Cannot read cache index."));
        return;
    }
    QJsonObject entries = root["entries"].toObject();
    int n = 0;
    for (const BatchImage& im : images_) {
        if (!entries.contains(im.rel)) continue;
        QJsonObject entry = entries[im.rel].toObject();
        QJsonObject attrs = entry["img_attrs"].toObject();
        fn(entry, attrs);
        entry["img_attrs"] = attrs;
        entries[im.rel] = entry;
        ++n;
    }
    root["entries"] = entries;
    if (!saveIndex(root)) {
        status_->setText(tr("Failed to write cache index."));
        return;
    }
    refreshDelKeys(root);
    changed_ = true;
    status_->setText(tr("Applied to %1 image(s).").arg(n));
}

void BatchEditDialog::applyAddTag() {
    const QString key = addKey_->text().trimmed();
    if (key.isEmpty()) {
        status_->setText(tr("Tag key is empty."));
        return;
    }
    const QString value = addValue_->text();
    patchAll([&](QJsonObject&, QJsonObject& attrs) {
        QJsonObject tags = attrs["user_tags"].toObject();
        tags[key] = value;
        attrs["user_tags"] = tags;
    });
}

void BatchEditDialog::applyRemoveTag() {
    const QString key = delKey_->currentText().trimmed();
    if (key.isEmpty()) {
        status_->setText(tr("Choose a tag to remove."));
        return;
    }
    patchAll([&](QJsonObject&, QJsonObject& attrs) {
        QJsonObject tags = attrs["user_tags"].toObject();
        if (tags.contains(key)) {
            tags.remove(key);
            attrs["user_tags"] = tags;
        }
    });
}

void BatchEditDialog::applyRating() {
    const QString rating = QString::number(rating_->currentData().toInt());
    patchAll([&](QJsonObject&, QJsonObject& attrs) {
        QJsonObject tags = attrs["user_tags"].toObject();
        tags["评分"] = rating;
        attrs["user_tags"] = tags;
    });
}

QString BatchEditDialog::renderTemplate(const QString& tpl, int index,
                                        const QJsonObject& attrs, const QJsonObject& entry) const {
    QString out = tpl;
    out.replace("{index}", QString::number(index));
    QString date = attrs["datetime_original"].toString();
    if (date.size() >= 10) date = date.left(10).remove('-');
    if (date.isEmpty()) date = "nodate";
    out.replace("{date}", date);
    QString scene = attrs["dominant_scene"].toString();
    if (scene.isEmpty()) scene = "unknown";
    out.replace("{scene}", scene);
    QString obj;
    QJsonArray objs = entry["objects"].toArray();
    if (!objs.isEmpty()) obj = objs.first().toObject()["class"].toString();
    if (obj.isEmpty()) obj = "unknown";
    out.replace("{object}", obj);
    return sanitizeName(out);
}

void BatchEditDialog::previewRename() {
    const QString tpl = tpl_->text();
    if (tpl.trimmed().isEmpty()) {
        status_->setText(tr("Template is empty."));
        return;
    }
    QJsonObject root = loadIndex();
    QJsonObject entries = root["entries"].toObject();

    pendingRename_.clear();
    preview_->setRowCount(0);
    int index = 0;
    QStringList taken;  // already-claimed new full paths within this batch
    for (const BatchImage& im : images_) {
        ++index;
        QJsonObject entry = entries.value(im.rel).toObject();
        QJsonObject attrs = entry["img_attrs"].toObject();

        QFileInfo fi(im.full);
        const QString ext = fi.suffix();
        QString base = renderTemplate(tpl, index, attrs, entry);
        if (base.isEmpty()) base = "img";
        QString newName = ext.isEmpty() ? base : base + "." + ext;
        QString newFull = fi.dir().filePath(newName);

        // rel path: replace only the basename (keep any directory prefix).
        QString newRel = im.rel;
        QString oldBase = QFileInfo(im.rel).fileName();
        if (newRel.endsWith(oldBase)) {
            newRel.replace(newRel.size() - oldBase.size(), oldBase.size(), newName);
        } else {
            newRel = newName;
        }

        QString status;
        bool conflict = false;
        if (newFull == im.full) {
            status = tr("unchanged");
        } else if (QFile::exists(newFull) || taken.contains(newFull)) {
            status = tr("CONFLICT - target exists");
            conflict = true;
        } else {
            status = tr("ok");
            pendingRename_[im.rel] = newRel;
        }
        taken << newFull;

        int row = preview_->rowCount();
        preview_->insertRow(row);
        preview_->setItem(row, 0, new QTableWidgetItem(QFileInfo(im.rel).fileName()));
        preview_->setItem(row, 1, new QTableWidgetItem(newName));
        QTableWidgetItem* st = new QTableWidgetItem(status);
        if (conflict) st->setForeground(QColor("#e06060"));
        preview_->setItem(row, 2, st);
    }
    status_->setText(tr("Preview: %1 rename(s) ready. Click \"Apply Rename\" to confirm.")
                         .arg(pendingRename_.size()));
}

void BatchEditDialog::doRename() {
    if (pendingRename_.isEmpty()) {
        status_->setText(tr("Run Preview first (no valid renames)."));
        return;
    }
    QJsonObject root = loadIndex();
    if (root.isEmpty()) {
        status_->setText(tr("Cannot read cache index."));
        return;
    }
    QJsonObject entries = root["entries"].toObject();

    // 1) rename the files on disk
    QHash<QString, QString> oldToNew;  // old rel -> new rel
    QStringList errors;
    for (const BatchImage& im : images_) {
        const QString newRel = pendingRename_.value(im.rel);
        if (newRel.isEmpty() || newRel == im.rel) continue;
        QFileInfo fi(im.full);
        QString newFull = fi.dir().filePath(QFileInfo(newRel).fileName());
        if (QFile::rename(im.full, newFull)) {
            oldToNew[im.rel] = newRel;
        } else {
            errors << im.full;
        }
    }

    // 2) update cache index entry keys (keep the entry data intact)
    if (!oldToNew.isEmpty()) {
        QJsonObject newEntries;
        for (auto it = entries.begin(); it != entries.end(); ++it) {
            QString key = it.key();
            if (oldToNew.contains(key)) key = oldToNew.value(key);
            newEntries[key] = it.value();
        }
        root["entries"] = newEntries;
        saveIndex(root);
    }

    // 3) keep collections and smart collections in sync
    CollectionManager::updateImagePaths(oldToNew);
    SmartCollectionManager::instance()->updateImagePaths(oldToNew);

    QString msg = tr("Renamed %1 image(s).").arg(oldToNew.size());
    if (!errors.isEmpty()) {
        msg += tr("\nFailed: %1").arg(errors.size());
    }
    status_->setText(msg);
    if (!oldToNew.isEmpty()) changed_ = true;
    pendingRename_.clear();
    if (oldToNew.isEmpty()) {
        status_->setText(tr("Nothing was renamed.") + (errors.isEmpty() ? QString() : tr("\nAll targets failed.")));
    }
    refreshDelKeys(root);
}
