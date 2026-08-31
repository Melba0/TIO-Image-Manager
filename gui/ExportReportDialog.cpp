#include "ExportReportDialog.h"
#include "managers/CollectionManager.h"
#include "managers/SettingsManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QComboBox>
#include <QRadioButton>
#include <QPushButton>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QImage>
#include <QPixmap>
#include <QBuffer>
#include <QByteArray>
#include <QTextDocument>
#include <QTextCursor>
#include <QTextImageFormat>
#include <QTextTableFormat>
#include <QTextCharFormat>
#include <QTextBlockFormat>
#include <QPrinter>
#include <QTextStream>
#include <QMessageBox>
#include <QJsonArray>
#include <QUrl>
#include <QFont>
#include <QBrush>
#include <QColor>
#include <QTextTable>
#include <QPageSize>
#include <QPageLayout>
#include <algorithm>

namespace {

QString htmlEscape(const QString& s) {
    QString out = s;
    out.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;");
    return out;
}

// 365 scene names from models/scene/categories_places365.txt.
QStringList sceneLabels() {
    QStringList labels;
    QFile lf(SettingsManager::projectDir() + "/models/scene/categories_places365.txt");
    if (lf.open(QIODevice::ReadOnly)) {
        for (const QByteArray& line : lf.readAll().split('\n')) {
            QString s = QString::fromUtf8(line).trimmed();
            if (!s.isEmpty()) labels << s;
        }
    }
    return labels;
}

QString num2(double v) { return QString::number(v, 'f', 2); }

}  // namespace

ExportReportDialog::ExportReportDialog(const QVector<ExportImage>& all,
                                       const QVector<ExportImage>& selected,
                                       QWidget* parent)
    : QDialog(parent), all_(all), selected_(selected) {
    setWindowTitle(tr("Export Report"));
    setMinimumWidth(360);

    QFormLayout* form = new QFormLayout();

    format_ = new QComboBox(this);
    format_->addItem(tr("HTML"), "html");
    format_->addItem(tr("PDF"), "pdf");
    form->addRow(tr("Format:"), format_);

    scopeAll_ = new QRadioButton(tr("All results (%1)").arg(all_.size()), this);
    scopeSelected_ = new QRadioButton(tr("Selected only (%1)").arg(selected_.size()), this);
    scopeAll_->setChecked(true);
    scopeSelected_->setEnabled(!selected_.isEmpty());
    QWidget* scopeBox = new QWidget(this);
    QVBoxLayout* sc = new QVBoxLayout(scopeBox);
    sc->setContentsMargins(0, 0, 0, 0);
    sc->addWidget(scopeAll_);
    sc->addWidget(scopeSelected_);
    form->addRow(tr("Scope:"), scopeBox);

    QPushButton* okBtn = new QPushButton(tr("Export"), this);
    QPushButton* cancelBtn = new QPushButton(tr("Cancel"), this);
    QHBoxLayout* btns = new QHBoxLayout();
    btns->addStretch();
    btns->addWidget(okBtn);
    btns->addWidget(cancelBtn);

    QVBoxLayout* lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addLayout(btns);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(okBtn, &QPushButton::clicked, this, [this]() {
        QVector<ExportImage> imgs = scopeImages();
        if (imgs.isEmpty()) {
            QMessageBox::warning(this, tr("Export Report"), tr("Nothing to export."));
            return;
        }
        const bool pdf = (format_->currentData().toString() == "pdf");
        QString filter = pdf ? tr("PDF files (*.pdf)") : tr("HTML files (*.html)");
        QString def = pdf ? "report.pdf" : "report.html";
        QString path = QFileDialog::getSaveFileName(this, tr("Save Report"), def, filter);
        if (path.isEmpty()) return;
        bool ok = pdf ? generatePdf(path) : generateHtml(path);
        if (ok) {
            QMessageBox::information(this, tr("Export Report"),
                                     tr("Report saved to:\n%1").arg(path));
            accept();
        } else {
            QMessageBox::warning(this, tr("Export Report"), tr("Failed to write report."));
        }
    });
}

QVector<ExportImage> ExportReportDialog::scopeImages() const {
    return scopeSelected_->isChecked() ? selected_ : all_;
}

QByteArray ExportReportDialog::thumbnailPng(const QString& fullPath, int maxSide) const {
    QImage img(fullPath);
    if (img.isNull()) return QByteArray();
    if (img.width() > maxSide || img.height() > maxSide) {
        img = img.scaled(QSize(maxSide, maxSide), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return bytes;
}

QVector<ExportReportDialog::Card> ExportReportDialog::buildCards() const {
    const QStringList labels = sceneLabels();
    QJsonObject root = CollectionManager::loadIndex();
    QJsonObject entries = root["entries"].toObject();

    QVector<Card> cards;
    for (const ExportImage& ei : scopeImages()) {
        Card c;
        c.img = ei;
        c.thumbPng = thumbnailPng(ei.full, 160);
        QJsonObject entry = entries.value(ei.rel).toObject();
        c.attrs = entry["img_attrs"].toObject();

        // objects: class + confidence
        for (const QJsonValue& v : entry.value("objects").toArray()) {
            QJsonObject o = v.toObject();
            QString cls = o["class"].toString();
            double conf = o["confidence"].toDouble();
            if (!cls.isEmpty()) c.objects << QString("%1 (%2%)").arg(cls).arg(conf * 100.0, 0, 'f', 1);
        }

        // top-3 scenes by probability
        QJsonArray vec = c.attrs["scene_vector"].toArray();
        if (vec.size() == 365 && labels.size() == 365) {
            QVector<int> idx(365);
            for (int i = 0; i < 365; ++i) idx[i] = i;
            std::sort(idx.begin(), idx.end(), [&](int a, int b) {
                return vec[a].toDouble() > vec[b].toDouble();
            });
            for (int i = 0; i < 3 && i < 365; ++i) {
                if (vec[idx[i]].toDouble() > 0.001) {
                    c.topScenes << QString("%1 (%2%)")
                                       .arg(labels[idx[i]])
                                       .arg(vec[idx[i]].toDouble() * 100.0, 0, 'f', 1);
                }
            }
        }
        cards.push_back(c);
    }
    return cards;
}

QString ExportReportDialog::topScenesHtml(const Card& c) const {
    if (c.topScenes.isEmpty()) return QString();
    QStringList lines;
    for (const QString& s : c.topScenes) lines << htmlEscape(s);
    return "<b>" + tr("Scenes") + ":</b> " + lines.join(" · ");
}

QString ExportReportDialog::objectsHtml(const Card& c) const {
    if (c.objects.isEmpty()) return QString();
    QStringList lines;
    for (const QString& s : c.objects) lines << htmlEscape(s);
    return "<b>" + tr("Objects") + ":</b> " + lines.join(" · ");
}

QString ExportReportDialog::tagsHtml(const Card& c) const {
    QJsonObject tags = c.attrs["user_tags"].toObject();
    if (tags.isEmpty()) return QString();
    QStringList parts;
    for (auto it = tags.begin(); it != tags.end(); ++it) {
        parts << htmlEscape(it.key() + "=" + it.value().toString());
    }
    return "<b>" + tr("Tags") + ":</b> " + parts.join(" · ");
}

bool ExportReportDialog::generateHtml(const QString& path) {
    QVector<Card> cards = buildCards();
    if (cards.isEmpty()) return false;

    QString body;
    for (const Card& c : cards) {
        const auto a = c.attrs;
        QString details;
        details += "<b>" + tr("File") + ":</b> " + htmlEscape(c.img.rel) + "<br>";
        details += "<b>" + tr("Path") + ":</b> " + htmlEscape(c.img.full) + "<br>";
        details += "<b>" + tr("Size") + ":</b> " +
                   QString::number(a["width"].toInt()) + " x " +
                   QString::number(a["height"].toInt()) + " px<br>";
        QFileInfo fi(c.img.full);
        details += "<b>" + tr("Modified") + ":</b> " +
                   htmlEscape(fi.lastModified().toString(Qt::ISODate)) + "<br>";
        details += "<b>" + tr("Score") + ":</b> " + num2(c.img.score) + "<br>";

        if (a["iso"].toDouble() >= 0) {
            QString cam = a["camera_make"].toString() + " " + a["camera_model"].toString();
            details += "<b>" + tr("Camera") + ":</b> " + htmlEscape(cam.trimmed()) + "<br>";
            details += "<b>ISO:</b> " + QString::number(a["iso"].toInt());
            details += " &nbsp; <b>" + tr("Shutter") + ":</b> ";
            double shut = a["shutter_speed"].toDouble();
            details += shut > 0 ? QString("1/%1 s").arg(qRound(1.0 / shut)) : "-";
            details += " &nbsp; <b>" + tr("Aperture") + ":</b> f/" + num2(a["aperture"].toDouble());
            details += " &nbsp; <b>" + tr("Focal") + ":</b> " + num2(a["focal_length"].toDouble()) + " mm<br>";
            details += "<b>" + tr("Date") + ":</b> " + htmlEscape(a["datetime_original"].toString()) + "<br>";
        }
        details += "<b>" + tr("Exposure") + ":</b> " + tr("over") + " " + num2(a["overexposure_score"].toDouble())
                 + " &nbsp; " + tr("under") + " " + num2(a["underexposure_score"].toDouble())
                 + " &nbsp; " + tr("good") + " " + num2(a["exposure_goodness"].toDouble()) + "<br>";
        details += "<b>" + tr("Sharpness") + ":</b> " + num2(a["global_blur_score"].toDouble()) + "<br>";
        QString scenes = topScenesHtml(c);
        if (!scenes.isEmpty()) details += scenes + "<br>";
        QString objs = objectsHtml(c);
        if (!objs.isEmpty()) details += objs + "<br>";
        QString tags = tagsHtml(c);
        if (!tags.isEmpty()) details += tags + "<br>";

        QString cardHtml;
        cardHtml += "<div style=\"display:inline-block; width:230px; vertical-align:top; "
                    "border:1px solid #ccc; border-radius:6px; margin:6px; padding:8px; "
                    "text-align:center; background:#fff;\">";
        if (!c.thumbPng.isEmpty()) {
            cardHtml += QString("<img src=\"data:image/png;base64,%1\" "
                                "style=\"max-width:200px; max-height:200px;\"><br>")
                            .arg(QString::fromLatin1(c.thumbPng.toBase64()));
        } else {
            cardHtml += "<i>" + tr("(no preview)") + "</i><br>";
        }
        cardHtml += "<b>" + htmlEscape(QFileInfo(c.img.rel).fileName()) + "</b><br>";
        cardHtml += tr("Score") + ": " + num2(c.img.score) + "<br>";
        cardHtml += QString("<details style=\"text-align:left; font-size:11px; margin-top:6px;\">"
                            "<summary>%1</summary>%2</details>")
                        .arg(tr("Details"))
                        .arg(details);
        cardHtml += "</div>";

        body += cardHtml;
    }

    QString html = QString(
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<title>%1</title></head><body style=\"font-family:'Segoe UI',Arial,sans-serif; margin:16px;\">"
        "<div style=\"background:#eef3fb; padding:12px 16px; border-radius:8px;\">"
        "<h2 style=\"margin:0 0 6px 0;\">📷 %2</h2>"
        "%3: %4 &nbsp;·&nbsp; %5: %6"
        "</div><div style=\"margin-top:10px;\">%7</div>"
        "</body></html>")
        .arg(tr("Image Retrieval Report"), tr("Image Retrieval Report"),
             tr("Generated"), QDateTime::currentDateTime().toString(Qt::ISODate),
             tr("Images"), QString::number(cards.size()),
             body);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(html.toUtf8());
    f.close();
    return true;
}

bool ExportReportDialog::generatePdf(const QString& path) {
    QVector<Card> cards = buildCards();
    if (cards.isEmpty()) return false;

    QTextDocument doc;
    doc.setPageSize(QSizeF(600, 800));

    QTextCursor cur(&doc);
    QTextBlockFormat center;
    center.setAlignment(Qt::AlignCenter);

    QTextCharFormat titleFmt;
    titleFmt.setFontPointSize(20);
    titleFmt.setFontWeight(QFont::Bold);
    cur.insertBlock(center, titleFmt);
    cur.insertText(tr("Image Retrieval Report"), titleFmt);

    QTextCharFormat metaFmt;
    metaFmt.setFontPointSize(10);
    cur.insertBlock(center, metaFmt);
    cur.insertText(tr("Generated") + ": " + QDateTime::currentDateTime().toString(Qt::ISODate)
                       + "    " + tr("Images") + ": " + QString::number(cards.size()),
                   metaFmt);
    cur.insertBlock();

    for (const Card& c : cards) {
        QTextTableFormat tfmt;
        tfmt.setBorder(1);
        tfmt.setBorderBrush(QBrush(QColor("#bbbbbb")));
        tfmt.setCellPadding(6);
        tfmt.setCellSpacing(0);
        tfmt.setWidth(QTextLength(QTextLength::PercentageLength, 100));
        cur.insertTable(1, 2, tfmt);

        // left cell: thumbnail
        QTextTableCell thumbCell = cur.currentTable()->cellAt(0, 0);
        {
            QTextCursor tc = thumbCell.firstCursorPosition();
            if (!c.thumbPng.isEmpty()) {
                static int imgSeq = 0;
                QImage img;
                img.loadFromData(c.thumbPng, "PNG");
                QTextImageFormat ifmt;
                ifmt.setName(QString("_rep%1").arg(++imgSeq));
                ifmt.setWidth(img.width());
                ifmt.setHeight(img.height());
                doc.addResource(QTextDocument::ImageResource, QUrl(ifmt.name()), img);
                tc.insertImage(ifmt);
            }
        }
        // right cell: details
        QTextTableCell infoCell = cur.currentTable()->cellAt(0, 1);
        {
            QTextCursor tc = infoCell.firstCursorPosition();
            QTextCharFormat h;
            h.setFontWeight(QFont::Bold);
            h.setFontPointSize(10);
            tc.insertText(QFileInfo(c.img.rel).fileName(), h);
            QTextCharFormat n;
            n.setFontPointSize(8.5);
            tc.insertText("\n" + tr("Score") + ": " + num2(c.img.score), n);

            const auto a = c.attrs;
            auto line = [&](const QString& t) {
                tc.insertText("\n" + t, n);
            };
            line(QString("%1: %2 x %3 px")
                     .arg(tr("Size")).arg(a["width"].toInt()).arg(a["height"].toInt()));
            if (a["iso"].toDouble() >= 0) {
                QString cam = (a["camera_make"].toString() + " " + a["camera_model"].toString()).trimmed();
                line(tr("Camera") + ": " + cam);
                line(QString("ISO: %1   " + tr("Shutter") + ": %2 s   " + tr("Aperture") + ": f/%3")
                         .arg(a["iso"].toInt())
                         .arg(a["shutter_speed"].toDouble() > 0
                                  ? QString("1/%1").arg(qRound(1.0 / a["shutter_speed"].toDouble()))
                                  : QString("-"))
                         .arg(num2(a["aperture"].toDouble())));
            }
            line(QString(tr("Exposure") + ": " + tr("over") + " %1  " + tr("under") + " %2  " + tr("good") + " %3")
                     .arg(num2(a["overexposure_score"].toDouble()),
                          num2(a["underexposure_score"].toDouble()),
                          num2(a["exposure_goodness"].toDouble())));
            line(tr("Sharpness") + ": " + num2(a["global_blur_score"].toDouble()));
            if (!c.topScenes.isEmpty()) line(tr("Scenes") + ": " + c.topScenes.join(" · "));
            if (!c.objects.isEmpty()) line(tr("Objects") + ": " + c.objects.join(" · "));
            QString t = tagsHtml(c);
            if (!t.isEmpty()) line(tr("Tags") + ": " + t);
        }

        // move past the table for the next card
        cur.movePosition(QTextCursor::End);
        cur.insertBlock();
    }

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageMargins(QMarginsF(12, 12, 12, 12), QPageLayout::Millimeter);
    doc.print(&printer);
    return true;
}
