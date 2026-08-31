#pragma once
#include <QDialog>
#include <QVector>
#include <QString>
#include <QJsonObject>

class QComboBox;
class QRadioButton;

struct ExportImage {
    QString rel;
    QString full;
    double score = 0;
};

// One-click report exporter for the current result grid.  Produces a
// self-contained HTML report (thumbnails embedded as base64) or a PDF built
// from a QTextDocument, covering either every result image or only the
// selected ones.  Each image card shows the thumbnail, filename, EXIF, quality
// scores, top scenes and detected objects (from the cache index).
class ExportReportDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExportReportDialog(const QVector<ExportImage>& allImages,
                                const QVector<ExportImage>& selectedImages,
                                QWidget* parent = nullptr);

private:
    QVector<ExportImage> scopeImages() const;
    bool generateHtml(const QString& path);
    bool generatePdf(const QString& path);

    struct Card {
        ExportImage img;
        QJsonObject attrs;
        QStringList objects;    // "class (confidence%)"
        QStringList topScenes;  // "name (probability%)"
        QByteArray thumbPng;    // PNG thumbnail bytes
    };
    QVector<Card> buildCards() const;
    QByteArray thumbnailPng(const QString& fullPath, int maxSide) const;
    QString topScenesHtml(const Card& c) const;
    QString objectsHtml(const Card& c) const;
    QString tagsHtml(const Card& c) const;

    QVector<ExportImage> all_;
    QVector<ExportImage> selected_;
    QComboBox* format_ = nullptr;
    QRadioButton* scopeAll_ = nullptr;
    QRadioButton* scopeSelected_ = nullptr;
};
