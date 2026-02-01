#ifndef LLMINTERFACE_H
#define LLMINTERFACE_H

#include <QObject>
#include <QString>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QList>
#include "settings/Settings.h"

class LLMInterface : public QObject {
    Q_OBJECT
public:
    explicit LLMInterface(Settings *settings, QObject *parent = nullptr);

    void verifyTranslation(const QString &sourceText, const QString &translatedText);
    void cancelVerification();
    void discoverLocalModels();
    void testConnection();

signals:
    void verificationStarted();
    void verificationProgress(int completed, int total);
    void partialResultReady(QString suggestion);
    void verificationReady(QString llmSuggestion);
    void modelsDiscovered(QStringList models);
    void connectionTestResult(bool success, QString message);
    void error(QString message);

private slots:
    void handleReply(QNetworkReply *reply);

private:
    struct Chunk {
        int index;
        QString source;
        QString machineTranslation;
        QString refinedTranslation;
        bool completed = false;
    };

    Settings *settings_;
    QNetworkAccessManager *networkManager_;
    QList<Chunk> chunks_;
    QMap<QNetworkReply*, int> activeRequests_;
    int completedCount_ = 0;

    void processQueue();
    void sendRequest(int chunkIndex);
    void callOllama(int chunkIndex, const QString &prompt);
    void callLMStudio(int chunkIndex, const QString &prompt);
    void callOpenAI(int chunkIndex, const QString &prompt);
    void callClaude(int chunkIndex, const QString &prompt);
    void callGoogleGemini(int chunkIndex, const QString &prompt);

    void fetchOllamaModels();
    void fetchLMStudioModels();
};

#endif // LLMINTERFACE_H
