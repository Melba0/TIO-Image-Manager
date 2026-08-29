#pragma once
#include <QWidget>
#include <QList>
#include <QPair>
class QPlainTextEdit;
class QComboBox;
class QLineEdit;

// Real-time log viewer: colored levels, filter by level/keyword, auto-scroll,
// clear (display only) and export to a .txt file.
class LogPanel : public QWidget {
    Q_OBJECT
public:
    explicit LogPanel(QWidget* parent = nullptr);

private slots:
    void onEntryAdded(int level, const QString& text);
    void applyFilter();
    void onClear();
    void onExport();

private:
    void appendLine(int level, const QString& text, bool scroll);
    bool passesFilter(int level, const QString& text) const;

    QPlainTextEdit* view_;
    QComboBox* levelFilter_;
    QLineEdit* keyword_;
    QList<QPair<int, QString>> all_;   // (level, text)
};
