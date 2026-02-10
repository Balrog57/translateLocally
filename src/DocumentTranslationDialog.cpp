#include "DocumentTranslationDialog.h"
#include "ui_DocumentTranslationDialog.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QEventLoop>
#include <QDesktopServices>
#include <QUrl>
#include <QPushButton>

// Worker Implementation
DocumentTranslationWorker::DocumentTranslationWorker(
    const QString &inputPath, const QString &outputPath,
    Settings *settings, MarianInterface *translator)
    : inputPath_(inputPath), outputPath_(outputPath),
      settings_(settings), translator_(translator), cancelled_(false) {
    llm_ = new LLMInterface(settings_, this);
}

void DocumentTranslationWorker::cancel() {
    cancelled_ = true;
    if (llm_) llm_->cancelVerification();
}

void DocumentTranslationWorker::process() {
    emit started();

    DocumentProcessor processor(inputPath_, outputPath_);

    if (!processor.open()) {
        emit error(tr("Failed to open document: %1").arg(inputPath_));
        emit finished(false, tr("Failed to open document"));
        return;
    }

    QList<DocumentSplitter::Segment> segments = processor.getSegments();
    if (segments.isEmpty()) {
        emit error(tr("No text found in document"));
        emit finished(false, tr("Document is empty"));
        return;
    }

    QList<DocumentSplitter::Segment> translatedSegments;
    int total = segments.size();
    bool useAI = settings_->llmEnabled();

    for (int i = 0; i < segments.size() && !cancelled_; ++i) {
        const auto &seg = segments[i];
        emit translationProgress(i + 1, total,
            tr("Translating segment %1 of %2...").arg(i + 1).arg(total));

        // Use event loop to wait for translation
        QEventLoop loop;
        bool translationDone = false;
        QString translatedText;

        QMetaObject::Connection conn = QObject::connect(
            translator_, &MarianInterface::translationReady,
            [&](Translation t) {
                translatedText = t.translation();
                translationDone = true;
                loop.quit();
            });

        QMetaObject::Connection errConn = QObject::connect(
            translator_, &MarianInterface::error,
            [&](QString msg) {
                emit error(msg);
                loop.quit();
            });

        translator_->translate(seg.text, false);
        loop.exec();

        QObject::disconnect(conn);
        QObject::disconnect(errConn);

        if (cancelled_) break;

        DocumentSplitter::Segment transSeg = seg;
        transSeg.text = translatedText;

        // AI improvement if enabled
        if (useAI && !translatedText.isEmpty()) {
            emit llmProgress(0, 100,
                tr("AI improving segment %1 of %2...").arg(i + 1).arg(total));

            QEventLoop aiLoop;

            // Connect to chunk-level progress updates
            QMetaObject::Connection aiProgress = QObject::connect(
                llm_, &LLMInterface::verificationProgress,
                [&](int completed, int totalChunks) {
                    int percentage = (totalChunks > 0) ? (completed * 100 / totalChunks) : 0;
                    emit llmProgress(percentage, 100,
                        tr("AI improving segment %1 of %2 (chunk %3/%4)...")
                            .arg(i + 1).arg(total).arg(completed).arg(totalChunks));
                });

            QMetaObject::Connection aiConn = QObject::connect(
                llm_, &LLMInterface::verificationReady,
                [&](QString suggestion) {
                    if (!suggestion.isEmpty()) {
                        transSeg.text = suggestion;
                    }
                    aiLoop.quit();
                });

            QMetaObject::Connection aiErr = QObject::connect(
                llm_, &LLMInterface::error,
                [&](QString msg) {
                    emit error(tr("AI error: %1").arg(msg));
                    aiLoop.quit();
                });

            llm_->verifyTranslation(seg.text, translatedText);
            aiLoop.exec();

            QObject::disconnect(aiProgress);
            QObject::disconnect(aiConn);
            QObject::disconnect(aiErr);
        }

        translatedSegments.append(transSeg);
    }

    if (cancelled_) {
        emit finished(false, tr("Translation cancelled"));
        return;
    }

    processor.setTranslatedSegments(translatedSegments);
    if (processor.save()) {
        emit finished(true, tr("Successfully saved to: %1").arg(outputPath_));
    } else {
        emit error(tr("Failed to save translated document"));
        emit finished(false, tr("Save failed"));
    }
}

// Dialog Implementation
DocumentTranslationDialog::DocumentTranslationDialog(
    const QString &inputPath, Settings *settings,
    MarianInterface *translator, QWidget *parent)
    : QDialog(parent), ui_(new Ui::DocumentTranslationDialog),
      inputPath_(inputPath), settings_(settings), translator_(translator),
      workerThread_(nullptr), worker_(nullptr), isRunning_(false) {

    ui_->setupUi(this);

    // Set input file display
    ui_->inputFileLabel->setText(QFileInfo(inputPath).fileName());

    // Generate default output path
    QFileInfo info(inputPath);
    QString defaultOutput = info.absolutePath() + "/" +
                           info.completeBaseName() + "_translated." + info.suffix();
    ui_->outputFileEdit->setText(defaultOutput);

    // Show AI status
    ui_->aiStatusLabel->setText(settings_->llmEnabled()
        ? tr("AI Improvement: Enabled (%1)").arg(settings_->llmProvider())
        : tr("AI Improvement: Disabled"));

    // Connect buttons
    connect(ui_->browseButton, &QPushButton::clicked, this, &DocumentTranslationDialog::onBrowseOutput);
    connect(ui_->startButton, &QPushButton::clicked, this, &DocumentTranslationDialog::onStartTranslation);
    connect(ui_->cancelButton, &QPushButton::clicked, this, &DocumentTranslationDialog::onCancel);

    // Initial state
    ui_->translationProgress->setValue(0);
    ui_->llmProgress->setValue(0);
    ui_->llmProgress->setVisible(settings_->llmEnabled());
    ui_->llmProgressLabel->setVisible(settings_->llmEnabled());
}

DocumentTranslationDialog::~DocumentTranslationDialog() {
    if (workerThread_) {
        workerThread_->quit();
        workerThread_->wait();
    }
    delete ui_;
}

void DocumentTranslationDialog::onBrowseOutput() {
    QFileInfo info(inputPath_);
    QString filter;
    if (info.suffix().toLower() == "pdf") {
        filter = tr("Word Documents (*.docx)");
    } else {
        filter = tr("Documents (*.%1)").arg(info.suffix());
    }

    QString path = QFileDialog::getSaveFileName(this,
        tr("Save Translated Document"),
        ui_->outputFileEdit->text(),
        filter);

    if (!path.isEmpty()) {
        ui_->outputFileEdit->setText(path);
    }
}

void DocumentTranslationDialog::onStartTranslation() {
    if (isRunning_) return;

    QString outputPath = ui_->outputFileEdit->text();
    if (outputPath.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("Please specify an output file."));
        return;
    }

    isRunning_ = true;
    ui_->startButton->setEnabled(false);
    ui_->browseButton->setEnabled(false);
    ui_->outputFileEdit->setEnabled(false);

    workerThread_ = new QThread(this);
    worker_ = new DocumentTranslationWorker(inputPath_, outputPath, settings_, translator_);
    worker_->moveToThread(workerThread_);

    connect(workerThread_, &QThread::started, worker_, &DocumentTranslationWorker::process);
    connect(worker_, &DocumentTranslationWorker::translationProgress,
            this, &DocumentTranslationDialog::onTranslationProgress);
    connect(worker_, &DocumentTranslationWorker::llmProgress,
            this, &DocumentTranslationDialog::onLLMProgress);
    connect(worker_, &DocumentTranslationWorker::finished,
            this, &DocumentTranslationDialog::onFinished);
    connect(worker_, &DocumentTranslationWorker::error,
            this, &DocumentTranslationDialog::onError);
    connect(worker_, &DocumentTranslationWorker::finished, workerThread_, &QThread::quit);
    connect(workerThread_, &QThread::finished, worker_, &QObject::deleteLater);

    workerThread_->start();
}

void DocumentTranslationDialog::onCancel() {
    if (isRunning_ && worker_) {
        worker_->cancel();
    }
    close();
}

void DocumentTranslationDialog::onTranslationProgress(int current, int total, QString status) {
    ui_->translationProgress->setMaximum(total);
    ui_->translationProgress->setValue(current);
    ui_->translationProgressLabel->setText(status);
}

void DocumentTranslationDialog::onLLMProgress(int current, int total, QString status) {
    ui_->llmProgress->setMaximum(total);
    ui_->llmProgress->setValue(current);
    ui_->llmProgressLabel->setText(status);
}

void DocumentTranslationDialog::onFinished(bool success, QString message) {
    isRunning_ = false;
    ui_->startButton->setEnabled(true);
    ui_->browseButton->setEnabled(true);
    ui_->outputFileEdit->setEnabled(true);

    if (success) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle(tr("Translation Complete"));
        msgBox.setText(message);
        msgBox.setIcon(QMessageBox::Information);

        QPushButton *openFileBtn = msgBox.addButton(tr("Open File"), QMessageBox::ActionRole);
        QPushButton *showInFolderBtn = msgBox.addButton(tr("Show in Folder"), QMessageBox::ActionRole);
        msgBox.addButton(QMessageBox::Close);

        msgBox.exec();

        if (msgBox.clickedButton() == openFileBtn) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(ui_->outputFileEdit->text()));
            accept();
        } else if (msgBox.clickedButton() == showInFolderBtn) {
            QFileInfo fileInfo(ui_->outputFileEdit->text());
            QDesktopServices::openUrl(QUrl::fromLocalFile(fileInfo.absolutePath()));
            accept();
        } else {
            accept();
        }
    } else {
        QMessageBox::warning(this, tr("Translation Failed"), message);
    }
}

void DocumentTranslationDialog::onError(QString message) {
    ui_->statusLabel->setText(tr("Error: %1").arg(message));
}
