#pragma once
#include <QObject>
#include <QSettings>
#include <QStringList>
#include <memory>

// Reads/writes config/settings.ini (relative to the DSL project dir):
// API config, gallery path list, dark-mode preference.
// The API key is stored Base64-encoded (light obfuscation, not encryption).
class SettingsManager : public QObject {
    Q_OBJECT
public:
    static SettingsManager* instance();

    // --- API ---
    QString baseUrl() const;
    QString apiKey() const;        // decoded
    QString modelName() const;
    QString enginePath() const;
    void setBaseUrl(const QString& v);
    void setApiKey(const QString& v);
    void setModelName(const QString& v);
    void setEnginePath(const QString& v);

    // --- Gallery (image library) ---
    QStringList galleryPaths() const;
    void setGalleryPaths(const QStringList& paths);
    void addGalleryPath(const QString& path);
    void removeGalleryPath(const QString& path);

    // --- Theme ---
    bool darkMode() const;
    void setDarkMode(bool on);

    // --- Tag pre-filter (persisted across runs) ---
    QStringList tagFilters() const;   // list of "key=v1|v2"
    void setTagFilters(const QStringList& filters);

    // --- Inference thresholds (persisted in [inference]) ---
    double fallbackThreshold() const;    // confidence-degradation fallback (0 = off)
    double baseConfThreshold() const;    // base detection confidence
    double iouThreshold() const;         // NMS IoU
    void setFallbackThreshold(double v);
    void setBaseConfThreshold(double v);
    void setIouThreshold(double v);

    // Generic passthrough to QSettings (for misc keys like general/*).
    QVariant value(const QString& key, const QVariant& def = QVariant()) const;
    void setValue(const QString& key, const QVariant& v);

    static QString maskKey(const QString& key);
    // Absolute DSL project dir (the folder containing models/registry.json).
    static QString projectDir();

private:
    SettingsManager();
    std::unique_ptr<QSettings> settings_;
};
