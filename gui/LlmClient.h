#pragma once
#include <QObject>
#include <QNetworkAccessManager>

class QNetworkReply;
class QUrl;

// Minimal OpenAI-compatible chat-completions client.
class LlmClient : public QObject {
    Q_OBJECT
public:
    explicit LlmClient(QObject* parent = nullptr);

    // Translate a natural-language request into DSL code.
    void translateToDsl(const QString& userInput, const QString& classesSummary,
                        const QString& extensionsSummary,
                        const QString& baseUrl, const QString& apiKey, const QString& model);
    // Send a trivial ping to verify connectivity / credentials.
    void testConnection(const QString& baseUrl, const QString& apiKey, const QString& model);

signals:
    void dslReady(const QString& dsl);
    void requestFailed(const QString& message);
    void connectionOk();

private:
    QNetworkReply* postChat(const QUrl& url, const QString& apiKey,
                            const QString& model, const QString& system, const QString& user);
    QNetworkAccessManager* net_;
};
