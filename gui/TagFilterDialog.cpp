#include "TagFilterDialog.h"
#include "managers/SettingsManager.h"
#include "managers/ModelManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QScrollArea>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace {

QByteArray stripBomTag(const QByteArray& raw) {
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF &&
        (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) {
        return raw.mid(3);
    }
    return raw;
}

// Dark styling consistent with the main window (the app's global stylesheet
// covers QLineEdit/QPushButton; here we handle the dialog/scroll containers).
const char* kDialogQss = R"(
QDialog { background-color: #1e1e1e; }
QScrollArea { background-color: #1e1e1e; border: none; }
QScrollArea > QWidget > QWidget { background-color: #1e1e1e; }
QLabel { color: #cccccc; }
)";

}  // namespace

TagFilterDialog::TagFilterDialog(const QVector<QPair<QString, QStringList>>& initial,
                                 QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Filter Options"));
    resize(620, 420);
    setStyleSheet(QString::fromUtf8(kDialogQss));

    QVBoxLayout* root = new QVBoxLayout(this);

    QLabel* title = new QLabel(tr("Pre-filter images by tags (DSL $ will only iterate matching images)"), this);
    root->addWidget(title);

    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    QWidget* container = new QWidget(scroll);
    rowsLayout_ = new QVBoxLayout(container);
    rowsLayout_->setSpacing(8);
    scroll->setWidget(container);
    root->addWidget(scroll, 1);

    QHBoxLayout* bottom = new QHBoxLayout();
    QPushButton* addTag = new QPushButton(tr("Add Tag"), this);
    QPushButton* clearAll = new QPushButton(tr("Clear All"), this);
    QPushButton* cancel = new QPushButton(tr("Cancel"), this);
    QPushButton* apply = new QPushButton(tr("Apply"), this);
    for (QPushButton* b : {addTag, clearAll, cancel, apply}) {
        b->setDefault(false);
        b->setAutoDefault(false);
    }
    bottom->addWidget(addTag);
    bottom->addWidget(clearAll);
    bottom->addStretch();
    bottom->addWidget(cancel);
    bottom->addWidget(apply);
    root->addLayout(bottom);

    loadExistingKeys();
    if (!initial.isEmpty()) {
        for (const auto& p : initial) addRow(p.first, p.second);
    } else if (existingKeys_.isEmpty()) {
        addRow();
    } else {
        addRow(existingKeys_.first());
    }

    connect(addTag, &QPushButton::clicked, this, [this]() { addRow(); });
    connect(clearAll, &QPushButton::clicked, this, [this]() {
        for (auto& r : rows_) {
            if (r.widget) r.widget->deleteLater();
        }
        rows_.clear();
        addRow();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(apply, &QPushButton::clicked, this, &TagFilterDialog::applyFilters);
}

void TagFilterDialog::loadExistingKeys() {
    existingKeys_.clear();
    QString cacheFile = SettingsManager::projectDir() + "/cache/"
                        + ModelManager::instance()->activeModel() + "/cache_index.json";
    QFile f(cacheFile);
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(stripBomTag(f.readAll()), &perr);
    if (perr.error != QJsonParseError::NoError) return;
    const QJsonObject entries = doc.object()["entries"].toObject();
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        const QJsonObject tags = it.value().toObject()["img_attrs"].toObject()["user_tags"].toObject();
        for (auto tk = tags.begin(); tk != tags.end(); ++tk) {
            if (!existingKeys_.contains(tk.key())) existingKeys_ << tk.key();
        }
    }
    existingKeys_.sort();
}

void TagFilterDialog::addRow(const QString& key, const QStringList& values) {
    Row row;
    row.widget = new QWidget(this);
    QHBoxLayout* lay = new QHBoxLayout(row.widget);
    lay->setContentsMargins(0, 0, 0, 0);

    // key (editable)
    row.keyEdit = new QLineEdit(key, row.widget);
    row.keyEdit->setPlaceholderText(tr("Tag name"));
    row.keyEdit->setFixedWidth(150);
    lay->addWidget(row.keyEdit);

    // values: chip row + an "add value" input
    QWidget* valuesArea = new QWidget(row.widget);
    QVBoxLayout* valLay = new QVBoxLayout(valuesArea);
    valLay->setContentsMargins(0, 0, 0, 0);
    valLay->setSpacing(4);

    QWidget* chipRow = new QWidget(valuesArea);
    QHBoxLayout* chipLay = new QHBoxLayout(chipRow);
    chipLay->setContentsMargins(0, 0, 0, 0);
    chipLay->setSpacing(4);

    QWidget* addWrap = new QWidget(valuesArea);
    QHBoxLayout* addLay = new QHBoxLayout(addWrap);
    addLay->setContentsMargins(0, 0, 0, 0);
    QLineEdit* addEdit = new QLineEdit(addWrap);
    addEdit->setPlaceholderText(tr("+ new value, press Enter to confirm"));
    addEdit->setMaximumWidth(180);
    addLay->addWidget(addEdit);
    addLay->addStretch();

    row.values = values;

    // Rebuild the chips from the LIVE row data (not a stale copy).
    auto rebuildChips = [this, rowWidget = row.widget, chipLay]() {
        Row* live = nullptr;
        for (auto& r : rows_) {
            if (r.widget == rowWidget) { live = &r; break; }
        }
        if (!live) return;
        while (QLayoutItem* it = chipLay->takeAt(0)) {
            if (QWidget* w = it->widget()) w->deleteLater();
            delete it;
        }
        for (const QString& v : live->values) {
            QPushButton* chip = new QPushButton(v, chipLay->parentWidget());
            chip->setDefault(false);
            chip->setAutoDefault(false);
            chip->setToolTip(tr("Click to remove this value"));
            chip->setStyleSheet(
                "QPushButton{background:#0e639c;color:#fff;border:none;border-radius:9px;"
                "padding:2px 10px;font-size:12px;} QPushButton:hover{background:#c0504d;}");
            connect(chip, &QPushButton::clicked, this, [this, rowWidget, chipLay, chip]() {
                for (auto& r : rows_) {
                    if (r.widget == rowWidget) { r.values.removeAll(chip->text()); break; }
                }
                chipLay->removeWidget(chip);
                chip->deleteLater();
            });
            chipLay->addWidget(chip);
        }
        chipLay->addStretch();
    };

    connect(addEdit, &QLineEdit::returnPressed, this, [this, rowWidget = row.widget, addEdit, rebuildChips]() {
        QString v = addEdit->text().trimmed();
        if (v.isEmpty()) return;
        for (auto& r : rows_) {
            if (r.widget == rowWidget) {
                if (!r.values.contains(v)) r.values << v;
                break;
            }
        }
        addEdit->clear();
        rebuildChips();
    });

    valLay->addWidget(chipRow);
    valLay->addWidget(addWrap);
    lay->addWidget(valuesArea, 1);

    QPushButton* remove = new QPushButton("×", row.widget);
    remove->setFixedSize(24, 24);
    remove->setDefault(false);
    remove->setAutoDefault(false);
    remove->setToolTip(tr("Remove this condition"));
    connect(remove, &QPushButton::clicked, this, [this, row]() {
        if (row.widget) {
            row.widget->deleteLater();
            for (auto it = rows_.begin(); it != rows_.end(); ++it) {
                if (it->widget == row.widget) { rows_.erase(it); break; }
            }
        }
    });
    lay->addWidget(remove);

    rows_.push_back(row);
    rowsLayout_->addWidget(row.widget);
    rebuildChips();   // render any initial values
}

void TagFilterDialog::applyFilters() {
    for (auto it = rows_.begin(); it != rows_.end();) {
        if (it->keyEdit && it->keyEdit->text().trimmed().isEmpty()) it = rows_.erase(it);
        else ++it;
    }
    accept();
}

QVector<QPair<QString, QStringList>> TagFilterDialog::filters() const {
    QVector<QPair<QString, QStringList>> out;
    for (const auto& r : rows_) {
        if (!r.keyEdit) continue;
        QString key = r.keyEdit->text().trimmed();
        if (key.isEmpty()) continue;
        out.append({key, r.values});
    }
    return out;
}
