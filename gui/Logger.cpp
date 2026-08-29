#include "Logger.h"
#include "managers/SettingsManager.h"
#include <QDir>
#include <QTime>
#include <QDate>
#include <QMutexLocker>
#include <QDebug>

Logger* Logger::instance() {
    static Logger logger;
    return &logger;
}

Logger::Logger(QObject* parent)
    : QObject(parent), logDir_(SettingsManager::projectDir() + "/logs") {
    QDir().mkpath(logDir_);
}

void Logger::install() {
    qInstallMessageHandler(&Logger::handler);
}

void Logger::handler(QtMsgType type, const QMessageLogContext&, const QString& msg) {
    Level lvl = Info;
    if (type == QtWarningMsg) lvl = Warning;
    else if (type == QtCriticalMsg || type == QtFatalMsg) lvl = Error;
    instance()->log(lvl, msg);
}

void Logger::ensureFile() {
    QString date = QDate::currentDate().toString("yyyyMMdd");
    if (logFile_.isOpen() && currentDate_ != date) {
        logFile_.close();
    }
    if (!logFile_.isOpen()) {
        logFile_.setFileName(logDir_ + "/app-" + date + ".log");
        logFile_.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
        currentDate_ = date;
    }
}

void Logger::log(Level lvl, const QString& msg) {
    QString time = QTime::currentTime().toString("HH:mm:ss");
    QString levelStr = lvl == Info ? "INFO" : (lvl == Warning ? "WARNING" : "ERROR");
    QString line = QString("[%1] [%2] %3").arg(time, levelStr, msg);

    QMutexLocker lock(&mutex_);
    ring_.push_back({(int)lvl, line});
    if ((int)ring_.size() > 1000) ring_.pop_front();
    ensureFile();
    if (logFile_.isOpen()) {
        logFile_.write((line + "\n").toUtf8());
        logFile_.flush();
    }
    emit entryAdded((int)lvl, line);
}

void Logger::clearRing() {
    QMutexLocker lock(&mutex_);
    ring_.clear();
}

QList<QPair<int, QString>> Logger::entries() const {
    QMutexLocker lock(&mutex_);
    QList<QPair<int, QString>> out;
    for (const auto& e : ring_) out << e;
    return out;
}