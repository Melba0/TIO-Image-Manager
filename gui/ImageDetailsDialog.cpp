#include "ImageDetailsDialog.h"
#include "managers/SettingsManager.h"
#include "managers/ModelManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTableWidget>
#include <QPushButton>
#include <QHeaderView>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QPixmap>
#include <QMessageBox>
#include <QPushButton>
#include <algorithm>

namespace {

// QJsonDocument::fromJson does not skip a leading UTF-8 BOM that some editors
// (and PowerShell) write.
QByteArray stripBom(const QByteArray& raw) {
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF &&
        (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) {
        return raw.mid(3);
    }
    return raw;
}

}  // namespace

ImageDetailsDialog::ImageDetailsDialog(const QString& relPath, const QString& fullPath, QWidget* parent)
    : QDialog(parent), relPath_(relPath), fullPath_(fullPath) {
    setWindowTitle(tr("Image Details") + " — " + relPath_);
    resize(640, 520);

    QHBoxLayout* root = new QHBoxLayout(this);

    thumbLabel_ = new QLabel(this);
    thumbLabel_->setFixedSize(300, 300);
    thumbLabel_->setAlignment(Qt::AlignCenter);
    thumbLabel_->setStyleSheet("background:#2d2d30; border:1px solid #3e3e42;");
    QPixmap pm(fullPath_);
    if (!pm.isNull()) {
        thumbLabel_->setPixmap(pm.scaled(thumbLabel_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        thumbLabel_->setText(tr("(no preview)"));
    }
    root->addWidget(thumbLabel_, 0, Qt::AlignTop);

    QWidget* right = new QWidget(this);
    QVBoxLayout* rl = new QVBoxLayout(right);

    infoLabel_ = new QLabel(right);
    infoLabel_->setWordWrap(true);
    infoLabel_->setTextFormat(Qt::RichText);
    rl->addWidget(infoLabel_);

    tagsTable_ = new QTableWidget(0, 2, right);
    tagsTable_->setHorizontalHeaderLabels({tr("Key"), tr("Value")});
    tagsTable_->horizontalHeader()->setStretchLastSection(true);
    tagsTable_->setColumnWidth(0, 140);
    rl->addWidget(tagsTable_, 1);

    QHBoxLayout* tagBtns = new QHBoxLayout();
    QPushButton* addBtn = new QPushButton(tr("Add Tag"), right);
    QPushButton* delBtn = new QPushButton(tr("Remove Tag"), right);
    QPushButton* saveBtn = new QPushButton(tr("Save"), right);
    tagBtns->addWidget(addBtn);
    tagBtns->addWidget(delBtn);
    tagBtns->addStretch();
    tagBtns->addWidget(saveBtn);
    rl->addLayout(tagBtns);

    root->addWidget(right, 1);

    connect(addBtn, &QPushButton::clicked, this, [this]() {
        int row = tagsTable_->rowCount();
        tagsTable_->insertRow(row);
        tagsTable_->setItem(row, 0, new QTableWidgetItem(""));
        tagsTable_->setItem(row, 1, new QTableWidgetItem(""));
        tagsTable_->setCurrentCell(row, 0);
    });
    connect(delBtn, &QPushButton::clicked, this, [this]() {
        int row = tagsTable_->currentRow();
        if (row >= 0) tagsTable_->removeRow(row);
    });
    connect(saveBtn, &QPushButton::clicked, this, &ImageDetailsDialog::saveTags);

    loadEntry();
}

QString ImageDetailsDialog::cacheFilePath() const {
    QString dir = SettingsManager::projectDir() + "/cache/"
                  + ModelManager::instance()->activeModel();
    return dir + "/cache_index.json";
}

bool ImageDetailsDialog::loadEntry() {
    QFile f(cacheFilePath());
    if (!f.open(QIODevice::ReadOnly)) return false;

    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(stripBom(f.readAll()), &perr);
    f.close();
    if (perr.error != QJsonParseError::NoError) return false;

    QJsonObject root = doc.object();
    QJsonObject entries = root["entries"].toObject();
    if (!entries.contains(relPath_)) return false;
    entry_ = entries[relPath_].toObject();
    imgAttrs_ = entry_["img_attrs"].toObject();

    // ---- metadata summary ----
    auto a = imgAttrs_;
    QString txt;
    txt += QString("<b>%1:</b> %2 x %3 px<br>")
               .arg(tr("Size")).arg(a["width"].toInt()).arg(a["height"].toInt());

    // ---- Places365 scene recognition ----
    QString dom = a["dominant_scene"].toString();
    if (!dom.isEmpty()) {
        txt += QString("<b>%1:</b> %2<br>").arg(tr("Scene")).arg(dom);
        txt += tr("Indoor") + QString(": %1<br>").arg(a["indoor_score"].toDouble(), 0, 'f', 2);
        // Top-5 scenes by probability (from scene_vector, needs the labels file).
        QStringList top5 = topScenes(a);
        if (!top5.isEmpty()) {
            txt += QString("<b>%1</b><br>").arg(tr("Top scenes"));
            for (const QString& line : top5) txt += line + "<br>";
        }
    } else {
        txt += tr("Scene recognition unavailable") + "<br>";
    }

    txt += QString("<b>%1</b><br>").arg(tr("Exposure"));
    txt += tr("Overexposure") + QString(": %1 &nbsp; ").arg(a["overexposure_score"].toDouble(), 0, 'f', 2)
         + tr("Underexposure") + QString(": %1 &nbsp; ").arg(a["underexposure_score"].toDouble(), 0, 'f', 2)
         + tr("Quality") + QString(": %1<br>").arg(a["exposure_goodness"].toDouble(), 0, 'f', 2);
    txt += QString("<b>%1:</b> %2<br>")
               .arg(tr("Sharpness"), QString::number(a["global_blur_score"].toDouble(), 'f', 2));
    txt += QString("<b>%1</b><br>").arg(tr("Camera / EXIF"));
    if (a["iso"].toDouble() >= 0) {
        txt += tr("Camera") + QString(": %1 %2<br>").arg(a["camera_make"].toString(), a["camera_model"].toString());
        txt += tr("ISO") + QString(": %1 &nbsp; ").arg(a["iso"].toInt())
             + tr("Shutter") + QString(": 1/%1 s &nbsp; ")
                 .arg(a["shutter_speed"].toDouble() > 0 ? qRound(1.0 / a["shutter_speed"].toDouble()) : 0)
             + tr("Aperture") + QString(": f/%1<br>").arg(a["aperture"].toDouble(), 0, 'f', 1)
             + tr("Focal") + QString(": %1 mm<br>").arg(a["focal_length"].toDouble(), 0, 'f', 1)
             + tr("Date") + QString(": %1<br>").arg(a["datetime_original"].toString());
    } else {
        txt += tr("No EXIF data") + "<br>";
    }
    infoLabel_->setText(txt);

    reloadTagsTable();
    return true;
}

void ImageDetailsDialog::reloadTagsTable() {
    tagsTable_->setRowCount(0);
    QJsonObject tags = imgAttrs_["user_tags"].toObject();
    for (auto it = tags.begin(); it != tags.end(); ++it) {
        int row = tagsTable_->rowCount();
        tagsTable_->insertRow(row);
        tagsTable_->setItem(row, 0, new QTableWidgetItem(it.key()));
        tagsTable_->setItem(row, 1, new QTableWidgetItem(it.value().toString()));
    }
}

QStringList ImageDetailsDialog::topScenes(const QJsonObject& attrs) const {
    QStringList lines;
    QJsonArray vec = attrs["scene_vector"].toArray();
    if (vec.size() != 365) return lines;

    // 365 scene names from models/scene/categories_places365.txt.
    QStringList labels;
    QFile lf(SettingsManager::projectDir() + "/models/scene/categories_places365.txt");
    if (lf.open(QIODevice::ReadOnly)) {
        for (const QByteArray& line : lf.readAll().split('\n')) {
            QString s = QString::fromUtf8(line).trimmed();
            if (!s.isEmpty()) labels << s;
        }
    }
    if (labels.size() != 365) return lines;

    // index the top 5 by probability
    QVector<int> idx(365);
    for (int i = 0; i < 365; ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](int a, int b) {
        return vec[a].toDouble() > vec[b].toDouble();
    });
    for (int i = 0; i < 5 && i < 365; ++i) {
        lines << QString("%1. %2  (%3%)")
                     .arg(i + 1)
                     .arg(labels[idx[i]])
                     .arg(vec[idx[i]].toDouble() * 100.0, 0, 'f', 1);
    }
    return lines;
}

void ImageDetailsDialog::saveTags() {
    // Rebuild the user_tags object from the table.
    QJsonObject tags;
    for (int row = 0; row < tagsTable_->rowCount(); ++row) {
        QTableWidgetItem* k = tagsTable_->item(row, 0);
        QTableWidgetItem* v = tagsTable_->item(row, 1);
        if (!k || k->text().trimmed().isEmpty()) continue;
        tags[k->text().trimmed()] = v ? v->text() : QString();
    }
    imgAttrs_["user_tags"] = tags;
    entry_["img_attrs"] = imgAttrs_;

    // Load the whole cache index, patch this image, write back (no BOM).
    QFile f(cacheFilePath());
    if (!f.open(QIODevice::ReadWrite)) {
        QMessageBox::warning(this, tr("Save Tags"), tr("Cannot open cache file"));
        return;
    }
    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(stripBom(f.readAll()), &perr);
    if (perr.error != QJsonParseError::NoError) {
        QMessageBox::warning(this, tr("Save Tags"), tr("Cache file is not valid JSON"));
        f.close();
        return;
    }
    QJsonObject root = doc.object();
    QJsonObject entries = root["entries"].toObject();
    entries[relPath_] = entry_;
    root["entries"] = entries;

    f.resize(0);
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();

    tagsModified_ = true;
    QMessageBox::information(this, tr("Save Tags"), tr("Tags saved."));
}
