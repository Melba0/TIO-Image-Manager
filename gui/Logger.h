#pragma once
#include <QObject>
#include <QMutex>
#include <QFile>
#include <deque>
#include <QList>
#include <QPair>

// Global logger: captures qDebug/qWarning/qCritical via a Qt message handler,
// writes to logs/app-YYYYMMDD.log (daily rotation) and keeps a thread-safe
// in-memory ring buffer (max 1000 entries) for the UI viewer.
class Logger : public QObject {
    Q_OBJECT
public:
    enum Level { Info, Warning, Error };
    Q_ENUM(Level)

    static Logger* instance();
    static void install();          // install the Qt message handler

    void log(Level lvl, const QString& msg);
    void clearRing();
    QList<QPair<int, QString>> entries() const;   // (level, formatted text)

signals:
    void entryAdded(int level, const QString& text);

private:
    explicit Logger(QObject* parent = nullptr);
    static void handler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg);
    void ensureFile();

    mutable QMutex mutex_;
    std::deque<QPair<int, QString>> ring_;
    QString logDir_;
    QFile logFile_;
    QString currentDate_;
};
