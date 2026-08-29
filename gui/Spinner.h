#pragma once
#include <QWidget>
class QTimer;

// A small rotating "wait" indicator (no dependency on QProgressIndicator).
class Spinner : public QWidget {
    Q_OBJECT
public:
    explicit Spinner(QWidget* parent = nullptr);
    void start();
    void stop();
    void setActive(bool on);
    bool isActive() const { return active_; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QTimer* timer_;
    int angle_ = 0;
    bool active_ = false;
};
