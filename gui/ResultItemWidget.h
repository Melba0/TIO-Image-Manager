#pragma once
#include <QWidget>
class QLabel;

// Custom grid cell: thumbnail on top, teal score label + filename below.
// No HTML markup (plain text labels) so nothing leaks into the display.
class ResultItemWidget : public QWidget {
    Q_OBJECT
public:
    ResultItemWidget(const QPixmap& thumb, double score, const QString& name,
                     bool showScore, QWidget* parent = nullptr);
    void setShowScore(bool on);

private:
    QLabel* score_;
    QLabel* name_;
};
