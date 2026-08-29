#include "SettingsManager.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>

namespace {
const QString kBaseUrl = "https://api.openai.com/v1/chat/completions";
const QString kModel = "gpt-4o-mini";
}  // namespace

SettingsManager* SettingsManager::instance() {
    static SettingsManager mgr;
    return &mgr;
}

// Absolute path of the DSL engine project dir (the folder containing
// models/registry.json).  The GUI may live next to it (tio/gui vs tio/dsl) or
// inside it; walk upward checking both <dir>/models/registry.json and
// <dir>/dsl/models/registry.json.
QString SettingsManager::projectDir() {
    QDir dir(QCoreApplication::applicationDirPath());
    while (true) {
        if (QFile::exists(dir.filePath("models/registry.json"))) {
            return QDir::cleanPath(dir.absolutePath());
        }
        if (QFile::exists(dir.filePath("dsl/models/registry.json"))) {
            return QDir::cleanPath(dir.filePath("dsl"));
        }
        if (!dir.cdUp()) break;
    }
    return QDir::cleanPath(QCoreApplication::applicationDirPath());
}

SettingsManager::SettingsManager() {
    QString configDir = projectDir() + "/config";
    QDir().mkpath(configDir);
    settings_ = std::make_unique<QSettings>(configDir + "/settings.ini", QSettings::IniFormat);
}

QString SettingsManager::baseUrl() const {
    return settings_->value("llm/base_url", kBaseUrl).toString();
}

QString SettingsManager::apiKey() const {
    QString b64 = settings_->value("llm/api_key").toString();
    return QString::fromUtf8(QByteArray::fromBase64(b64.toUtf8()));
}

QString SettingsManager::modelName() const {
    return settings_->value("llm/model", kModel).toString();
}

QString SettingsManager::enginePath() const {
    QString stored = settings_->value("engine/path").toString();
    if (!stored.isEmpty() && QFile::exists(stored)) return stored;
    QString dflt = projectDir() + "/build/dsl.exe";
    if (QFile::exists(dflt)) return dflt;
    return stored.isEmpty() ? dflt : stored;
}

void SettingsManager::setBaseUrl(const QString& v) {
    settings_->setValue("llm/base_url", v);
    settings_->sync();
}

void SettingsManager::setApiKey(const QString& v) {
    settings_->setValue("llm/api_key", QString::fromUtf8(v.toUtf8().toBase64()));
    settings_->sync();
}

void SettingsManager::setModelName(const QString& v) {
    settings_->setValue("llm/model", v);
    settings_->sync();
}

void SettingsManager::setEnginePath(const QString& v) {
    settings_->setValue("engine/path", v);
    settings_->sync();
}

QStringList SettingsManager::galleryPaths() const {
    QStringList list = settings_->value("library/paths").toStringList();
    if (list.isEmpty()) {
        // default: <project>/../photo (tio/photo) or <project>/photo
        QString p = projectDir() + "/../photo";
        if (QFile::exists(p)) list << QDir::cleanPath(p);
        else if (QFile::exists(projectDir() + "/photo")) list << QDir::cleanPath(projectDir() + "/photo");
    }
    return list;
}

void SettingsManager::setGalleryPaths(const QStringList& paths) {
    settings_->setValue("library/paths", paths);
    settings_->sync();
}

void SettingsManager::addGalleryPath(const QString& path) {
    QStringList list = galleryPaths();
    QString clean = QDir::cleanPath(path);
    if (!list.contains(clean)) {
        list.append(clean);
        setGalleryPaths(list);
    }
}

void SettingsManager::removeGalleryPath(const QString& path) {
    QStringList list = galleryPaths();
    list.removeAll(QDir::cleanPath(path));
    setGalleryPaths(list);
}

bool SettingsManager::darkMode() const {
    return settings_->value("ui/dark_mode", true).toBool();
}

void SettingsManager::setDarkMode(bool on) {
    settings_->setValue("ui/dark_mode", on);
    settings_->sync();
}

QStringList SettingsManager::tagFilters() const {
    return settings_->value("filter/tag_filters").toStringList();
}

void SettingsManager::setTagFilters(const QStringList& filters) {
    settings_->setValue("filter/tag_filters", filters);
    settings_->sync();
}

QVariant SettingsManager::value(const QString& key, const QVariant& def) const {
    return settings_->value(key, def);
}

void SettingsManager::setValue(const QString& key, const QVariant& v) {
    settings_->setValue(key, v);
    settings_->sync();
}

QString SettingsManager::maskKey(const QString& key) {
    if (key.isEmpty()) return QString();
    if (key.size() <= 8) return QString(key.size(), '*');
    return key.left(4) + "..." + key.right(4);
}