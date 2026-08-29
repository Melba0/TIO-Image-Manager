#pragma once
#include <QObject>
#include <QTranslator>

// Runtime EN <-> ZH switching.  The UI source strings are English (wrapped in
// tr()); when "zh" is selected a custom QTranslator maps them to Simplified
// Chinese.  The choice is persisted via SettingsManager ("language").
class ZhTranslator : public QTranslator {
public:
    QString translate(const char* context, const char* sourceText,
                      const char* disambiguation = nullptr, int n = -1) const override;
};

class LanguageManager : public QObject {
    Q_OBJECT
public:
    static LanguageManager* instance();

    QString language() const;              // "en" or "zh"
    void setLanguage(const QString& lang); // "en" / "zh"

signals:
    void languageChanged();

private:
    explicit LanguageManager(QObject* parent = nullptr);
    ZhTranslator translator_;
};
