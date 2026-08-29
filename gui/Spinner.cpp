#include "Spinner.h"
#include <QPainter>
#include <QTimer>
#include <cmath>

Spinner::Spinner(QWidget* parent)
    : QWidget(parent), timer_(new QTimer(this)) {
    setFixedSize(22, 22);
    timer_->setInterval(80);
    connect(timer_, &QTimer::timeout, this, [this]() {
        angle_ = (angle_ + 30) % 360;
        update();
    });
}

void Spinner::start() {
    active_ = true;
    angle_ = 0;
    timer_->start();
    update();
}

void Spinner::stop() {
    active_ = false;
    timer_->stop();
    update();
}

void Spinner::setActive(bool on) {
    if (on) start(); else stop();
}

void Spinner::paintEvent(QPaintEvent*) {
    if (!active_) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor c(14, 99, 156);
    QRect r = rect().adjusted(2, 2, -2, -2);
    for (int i = 0; i < 8; ++i) {
        int a = angle_ + i * 45;
        c.setAlphaF(0.25 + 0.75 * (i / 7.0));
        p.setPen(QPen(c, 2.0, Qt::SolidLine, Qt::RoundCap));
        double rad = a * 3.14159265 / 180.0;
        QPointF center = r.center();
        int radius = r.width() / 2 - 1;
        p.drawLine(center + QPointF(std::cos(rad), std::sin(rad)) * (radius - 3),
                   center + QPointF(std::cos(rad), std::sin(rad)) * radius);
    }
}