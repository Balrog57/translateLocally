#ifndef DOCUMENTTRANSLATIONDIALOG_H
#define DOCUMENTTRANSLATIONDIALOG_H

#include <QDialog>
#include <QThread>
#include "DocumentProcessor.h"
#include "DocumentSplitter.h"
#include "LLMInterface.h"
#include "MarianInterface.h"
#include "settings/Settings.h"

namespace Ui {
class DocumentTranslationDialog;
}

class DocumentTranslationWorker : public QObject {
    Q_OBJECT
public:
    DocumentTranslationWorker(const QString &inputPath, const QString &outputPath,
                              Settings *settings, MarianInterface *translator);

public slots:
    void process();
    void cancel();

signals:
    void started();
    void translationProgress(int current, int total, QString status);
    void llmProgress(int current, int total, QString status);
    void finished(bool success, QString message);
    void error(QString message);

private:
    QString inputPath_;
    QString outputPath_;
    Settings *settings_;
    MarianInterface *translator_;
    LLMInterface *llm_;
    bool cancelled_;
};

class DocumentTranslationDialog : public QDialog {
    Q_OBJECT
public:
    explicit DocumentTranslationDialog(const QString &inputPath,
                                       Settings *settings,
                                       MarianInterface *translator,
                                       QWidget *parent = nullptr);
    ~DocumentTranslationDialog();

private slots:
    void onBrowseOutput();
    void onStartTranslation();
    void onCancel();
    void onTranslationProgress(int current, int total, QString status);
    void onLLMProgress(int current, int total, QString status);
    void onFinished(bool success, QString message);
    void onError(QString message);

private:
    Ui::DocumentTranslationDialog *ui_;
    QString inputPath_;
    Settings *settings_;
    MarianInterface *translator_;
    QThread *workerThread_;
    DocumentTranslationWorker *worker_;
    bool isRunning_;
};

#endif // DOCUMENTTRANSLATIONDIALOG_H
