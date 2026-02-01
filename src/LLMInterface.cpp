#include "LLMInterface.h"
#include <QUrl>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QDebug>

LLMInterface::LLMInterface(Settings *settings, QObject *parent)
    : QObject(parent), settings_(settings), networkManager_(new QNetworkAccessManager(this)) {
}

void LLMInterface::verifyTranslation(const QString &sourceText, const QString &translatedText) {
    qDebug() << "LLMInterface: Starting verification. Enabled:" << settings_->llmEnabled();
    if (!settings_->llmEnabled() || sourceText.trimmed().isEmpty()) return;

    // Abort active requests
    for (auto* reply : activeRequests_.keys()) {
        reply->abort();
        reply->deleteLater();
    }
    activeRequests_.clear();
    chunks_.clear();
    completedCount_ = 0;

    // Split text into chunks (approx 4000 chars) for better parallel processing.
    // Avoid too many small requests which choke local LLMs.
    QStringList sourceLines = sourceText.split('\n');
    QStringList transLines = translatedText.split('\n');
    int maxLines = qMax(sourceLines.size(), transLines.size());

    QString currentSource;
    QString currentTrans;
    for (int i = 0; i < maxLines; ++i) {
        if (i < sourceLines.size()) currentSource += sourceLines[i] + "\n";
        if (i < transLines.size()) currentTrans += transLines[i] + "\n";

    // Optimized chunk size for Desktop LLMs with large context (e.g. Qwen, Llama).
    // 3000 chars is roughly 1000-1500 tokens, which fits perfectly in modern 32k+ context windows.
    // This reduces the number of requests and improves translation coherence.
    if (currentSource.length() > 3000 || i == maxLines - 1) {
            Chunk c = {static_cast<int>(chunks_.size()), currentSource.trimmed(), currentTrans.trimmed(), currentTrans.trimmed(), false};
            chunks_.append(c);
            currentSource.clear();
            currentTrans.clear();
        }
    }

    qDebug() << "LLMInterface: Created" << chunks_.size() << "chunks.";
    if (chunks_.isEmpty()) return;

    emit verificationStarted();
    emit verificationProgress(0, chunks_.size());
    processQueue();
}

void LLMInterface::processQueue() {
    int maxConcurrent = 1; // Strict sequencing for local LLM stability
    if (activeRequests_.size() >= maxConcurrent) return;

    for (int i = 0; i < chunks_.size(); ++i) {
        if (!chunks_[i].completed) {
            bool isRunning = false;
            auto it = activeRequests_.begin();
            while (it != activeRequests_.end()) {
                if (it.value() == i) { isRunning = true; break; }
                ++it;
            }
            if (!isRunning) {
                qDebug() << "LLMInterface: Sending chunk" << i;
                sendRequest(i);
                if (activeRequests_.size() >= maxConcurrent) break;
            }
        }
    }
}

void LLMInterface::sendRequest(int index) {
    QString provider = settings_->llmProvider();

    QString context;
    if (index > 0) {
        context = QString("Context (previous): %1\n").arg(chunks_[index-1].source.right(300));
    }

    QString prompt = QString("### Instructions:\n"
                             "1. You are a professional translator. Compare the 'Source Text' (English) and the 'Machine Translation' (French).\n"
                             "2. Produce a high-quality, natural French version.\n"
                             "3. DO NOT use <think> tags. DO NOT provide any reasoning, notes, or explanations.\n"
                             "4. Output ONLY the final French refined text.\n\n"
                             "### Context:\n%1\n"
                             "### Source Text (English):\n%2\n\n"
                             "### Machine Translation (French to improve):\n%3\n\n"
                             "### Final Refined Translation (French):")
                             .arg(context, chunks_[index].source, chunks_[index].machineTranslation);

    if (provider == "Ollama") {
        callOllama(index, prompt);
    } else if (provider == "LM Studio") {
        callLMStudio(index, prompt);
    } else if (provider == "OpenAI") {
        callOpenAI(index, prompt);
    } else if (provider == "Claude") {
        callClaude(index, prompt);
    } else if (provider == "Google Gemini") {
        callGoogleGemini(index, prompt);
    }
}

void LLMInterface::callOllama(int index, const QString &prompt) {
    QString baseUrl = settings_->llmUrl().trimmed();
    if (baseUrl.endsWith("/")) baseUrl.chop(1);
    if (!baseUrl.contains("/api/generate") && !baseUrl.isEmpty()) {
        baseUrl += "/api/generate";
    }

    QUrl url(baseUrl);
    qDebug() << "LLMInterface: Posting to Ollama:" << url.toString();

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(30000); // Forcer l'attente infinie pour les modèles lents

    QJsonObject json;
    json["model"] = settings_->llmModel();
    json["prompt"] = prompt;
    json["stream"] = false;

    QNetworkReply *reply = networkManager_->post(request, QJsonDocument(json).toJson());
    activeRequests_.insert(reply, index);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { handleReply(reply); });
}

void LLMInterface::callLMStudio(int index, const QString &prompt) {
    QString baseUrl = settings_->llmUrl().trimmed();
    if (baseUrl.endsWith("/")) baseUrl.chop(1);

    if (!baseUrl.contains("/v1/") && !baseUrl.isEmpty()) {
        baseUrl += "/v1/chat/completions";
    }

    QUrl url(baseUrl);
    qDebug() << "LLMInterface: Posting to LM Studio:" << url.toString();

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(30000); // Forcer l'attente infinie pour les modèles lents

    QJsonObject message;
    message["role"] = "user";
    message["content"] = prompt;

    QJsonArray messages;
    messages.append(message);

    QJsonObject json;
    json["model"] = settings_->llmModel().isEmpty() ? "default" : settings_->llmModel();
    json["messages"] = messages;
    json["temperature"] = 0.3;

    QNetworkReply *reply = networkManager_->post(request, QJsonDocument(json).toJson());
    activeRequests_.insert(reply, index);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { handleReply(reply); });
}

void LLMInterface::handleReply(QNetworkReply *reply) {
    if (!activeRequests_.contains(reply)) {
        reply->deleteLater();
        return;
    }

    int index = activeRequests_.take(reply);
    QString result;

    if (reply->error() == QNetworkReply::NoError) {
        QByteArray response = reply->readAll();
        qDebug() << "LLMInterface: Received response for chunk" << index;
        QJsonDocument doc = QJsonDocument::fromJson(response);

        if (!doc.isNull() && doc.isObject()) {
            if (settings_->llmProvider() == "Ollama") {
                result = doc.object()["response"].toString();
            } else if (settings_->llmProvider() == "LM Studio" || settings_->llmProvider() == "OpenAI") {
                // OpenAI-compatible format: choices[0].message.content
                QJsonArray choices = doc.object()["choices"].toArray();
                if (!choices.isEmpty()) {
                    result = choices[0].toObject()["message"].toObject()["content"].toString();
                } else {
                    qWarning() << "LLMInterface: Empty choices in response";
                }
            } else if (settings_->llmProvider() == "Claude") {
                // Anthropic Claude API: content[0].text
                QJsonArray content = doc.object()["content"].toArray();
                if (!content.isEmpty()) {
                    result = content[0].toObject()["text"].toString();
                } else {
                    qWarning() << "LLMInterface: Empty content in Claude response";
                }
            } else if (settings_->llmProvider() == "Google Gemini") {
                // Check for API error in response first
                if (doc.object().contains("error")) {
                    QJsonObject errorObj = doc.object()["error"].toObject();
                    QString errorMsg = errorObj["message"].toString();
                    int errorCode = errorObj["code"].toInt();
                    qWarning() << "LLMInterface: Gemini API error:" << errorCode << errorMsg;
                    emit error(tr("Gemini API error: %1").arg(errorMsg));
                    // Stop further processing - clear pending requests
                    reply->deleteLater();
                    activeRequests_.clear();
                    return;
                }

                // Gemini API: candidates[0].content.parts[0].text
                QJsonArray candidates = doc.object()["candidates"].toArray();
                if (!candidates.isEmpty()) {
                    QJsonObject contentObj = candidates[0].toObject()["content"].toObject();
                    QJsonArray parts = contentObj["parts"].toArray();
                    if (!parts.isEmpty()) {
                        result = parts[0].toObject()["text"].toString();
                    }
                } else {
                    qWarning() << "LLMInterface: Empty candidates in Gemini response";
                    emit error(tr("Gemini returned empty response. Check your API quota."));
                }
            }

            // Cleanup: Strip <think> tags and their content if the model ignored our instructions
            int thinkStart = result.indexOf("<think>");
            while (thinkStart != -1) {
                int thinkEnd = result.indexOf("</think>", thinkStart);
                if (thinkEnd != -1) {
                    result.remove(thinkStart, (thinkEnd + 8) - thinkStart);
                } else {
                    result.remove(thinkStart, result.length() - thinkStart);
                }
                thinkStart = result.indexOf("<think>");
            }
        } else {
            qWarning() << "LLMInterface: Failed to parse JSON response";
        }
    } else {
        if (reply->error() != QNetworkReply::OperationCanceledError) {
            qWarning() << "LLMInterface: Network error for chunk" << index << ":" << reply->errorString();
            emit error(tr("Network error: %1").arg(reply->errorString()));
        }
    }

    if (!result.isEmpty()) {
        chunks_[index].refinedTranslation = result.trimmed();
    }

    reply->deleteLater();
    chunks_[index].completed = true;
    completedCount_++;
    qDebug() << "LLMInterface: Chunk" << index << "done." << completedCount_ << "/" << chunks_.size();

    // Construct current full text from completed AND in-progress chunks
    QString currentFullText;
    for (const auto& chunk : chunks_) {
        currentFullText += chunk.refinedTranslation + "\n\n";
    }
    emit partialResultReady(currentFullText.trimmed());
    emit verificationProgress(completedCount_, chunks_.size());

    if (completedCount_ == chunks_.size()) {
        qDebug() << "LLMInterface: All chunks completed.";
        emit verificationReady(currentFullText.trimmed());
    } else {
        processQueue();
    }
}

void LLMInterface::callOpenAI(int index, const QString &prompt) {
    QString apiKey = settings_->openaiApiKey();
    if (apiKey.isEmpty()) {
        emit error(tr("OpenAI API key is not configured. Please set it in Settings."));
        return;
    }

    QUrl url("https://api.openai.com/v1/chat/completions");
    qDebug() << "LLMInterface: Posting to OpenAI";

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(apiKey).toUtf8());
    request.setTransferTimeout(30000);

    QJsonObject message;
    message["role"] = "user";
    message["content"] = prompt;

    QJsonArray messages;
    messages.append(message);

    QJsonObject json;
    json["model"] = settings_->llmModel().isEmpty() ? "gpt-4o-mini" : settings_->llmModel();
    json["messages"] = messages;
    json["temperature"] = 0.3;

    QNetworkReply *reply = networkManager_->post(request, QJsonDocument(json).toJson());
    activeRequests_.insert(reply, index);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { handleReply(reply); });
}

void LLMInterface::callClaude(int index, const QString &prompt) {
    QString apiKey = settings_->claudeApiKey();
    if (apiKey.isEmpty()) {
        emit error(tr("Claude API key is not configured. Please set it in Settings."));
        return;
    }

    QUrl url("https://api.anthropic.com/v1/messages");
    qDebug() << "LLMInterface: Posting to Claude";

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("x-api-key", apiKey.toUtf8());
    request.setRawHeader("anthropic-version", "2023-06-01");
    request.setTransferTimeout(30000);

    QJsonObject message;
    message["role"] = "user";
    message["content"] = prompt;

    QJsonArray messages;
    messages.append(message);

    QJsonObject json;
    json["model"] = settings_->llmModel().isEmpty() ? "claude-3-haiku-20240307" : settings_->llmModel();
    json["max_tokens"] = 4096;
    json["messages"] = messages;

    QNetworkReply *reply = networkManager_->post(request, QJsonDocument(json).toJson());
    activeRequests_.insert(reply, index);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { handleReply(reply); });
}

void LLMInterface::callGoogleGemini(int index, const QString &prompt) {
    QString apiKey = settings_->geminiApiKey();
    if (apiKey.isEmpty()) {
        emit error(tr("Google Gemini API key is not configured. Please set it in Settings."));
        return;
    }

    QString model = settings_->llmModel().isEmpty() ? "gemini-1.5-flash" : settings_->llmModel();
    QUrl url(QString("https://generativelanguage.googleapis.com/v1beta/models/%1:generateContent?key=%2")
             .arg(model, apiKey));
    qDebug() << "LLMInterface: Posting to Google Gemini";

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(30000);

    QJsonObject textPart;
    textPart["text"] = prompt;

    QJsonArray parts;
    parts.append(textPart);

    QJsonObject content;
    content["parts"] = parts;

    QJsonArray contents;
    contents.append(content);

    QJsonObject json;
    json["contents"] = contents;

    QNetworkReply *reply = networkManager_->post(request, QJsonDocument(json).toJson());
    activeRequests_.insert(reply, index);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { handleReply(reply); });
}

// Cancel any ongoing verification
void LLMInterface::cancelVerification() {
    qDebug() << "LLMInterface: Cancelling verification";

    // Copy keys and clear map first to prevent iterator invalidation
    // because abort() might trigger finished() signal which modifies activeRequests_
    QList<QNetworkReply*> replies = activeRequests_.keys();
    activeRequests_.clear();

    for (QNetworkReply* reply : replies) {
        if (reply) {
            reply->disconnect(); // Prevent signals from firing
            reply->abort();
            reply->deleteLater();
        }
    }

    chunks_.clear();
    completedCount_ = 0;
}

void LLMInterface::discoverLocalModels() {
    QString provider = settings_->llmProvider();
    if (provider == "Ollama") {
        fetchOllamaModels();
    } else if (provider == "LM Studio") {
        fetchLMStudioModels();
    }
}

void LLMInterface::fetchOllamaModels() {
    QString baseUrl = settings_->llmUrl().trimmed();
    if (baseUrl.isEmpty()) baseUrl = "http://localhost:11434";
    if (baseUrl.endsWith("/")) baseUrl.chop(1);

    QUrl url(baseUrl + "/api/tags");
    qDebug() << "LLMInterface: Fetching Ollama models from" << url.toString();

    QNetworkRequest request(url);
    QNetworkReply *reply = networkManager_->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QStringList models;
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (!doc.isNull() && doc.isObject()) {
                QJsonArray modelsArray = doc.object()["models"].toArray();
                for (const auto &m : modelsArray) {
                    models.append(m.toObject()["name"].toString());
                }
            }
        } else {
            qWarning() << "LLMInterface: Failed to fetch Ollama models:" << reply->errorString();
        }
        reply->deleteLater();
        emit modelsDiscovered(models);
    });
}

void LLMInterface::fetchLMStudioModels() {
    QString baseUrl = settings_->llmUrl().trimmed();
    if (baseUrl.isEmpty()) baseUrl = "http://localhost:1234";
    if (baseUrl.endsWith("/")) baseUrl.chop(1);

    QUrl url(baseUrl + "/v1/models");
    qDebug() << "LLMInterface: Fetching LM Studio models from" << url.toString();

    QNetworkRequest request(url);
    QNetworkReply *reply = networkManager_->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QStringList models;
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (!doc.isNull() && doc.isObject()) {
                QJsonArray dataArray = doc.object()["data"].toArray();
                for (const auto &m : dataArray) {
                    models.append(m.toObject()["id"].toString());
                }
            }
        } else {
            qWarning() << "LLMInterface: Failed to fetch LM Studio models:" << reply->errorString();
        }
        reply->deleteLater();
        emit modelsDiscovered(models);
    });
}
