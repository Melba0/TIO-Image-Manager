#include "ResultItemWidget.h"
#include <QLabel>
#include <QVBoxLayout>
#include <QPixmap>

ResultItemWidget::ResultItemWidget(const QPixmap& thumb, double score, const QString& name,
                                   bool showScore, QWidget* parent)
    : QWidget(parent) {
    setObjectName("resultItem");

    QVBoxLayout* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(2);

    QLabel* img = new QLabel(this);
    img->setObjectName("thumbLabel");
    img->setFixedSize(120, 120);
    img->setAlignment(Qt::AlignCenter);
    if (!thumb.isNull()) img->setPixmap(thumb);
    lay->addWidget(img, 0, Qt::AlignHCenter);

    score_ = new QLabel(QString::number(score, 'f', 2), this);
    score_->setObjectName("scoreLabel");
    score_->setAlignment(Qt::AlignCenter);
    score_->setVisible(showScore);
    lay->addWidget(score_);

    name_ = new QLabel(name, this);
    name_->setObjectName("nameLabel");
    name_->setAlignment(Qt::AlignCenter);
    name_->setWordWrap(true);
    lay->addWidget(name_);
}

void ResultItemWidget::setShowScore(bool on) {
    score_->setVisible(on);
}