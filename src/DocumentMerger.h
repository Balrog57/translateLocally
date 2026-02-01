#ifndef DOCUMENTMERGER_H
#define DOCUMENTMERGER_H

#include <QString>
#include <QObject>
#include <QList>
#include "DocumentSplitter.h"

class DocumentMerger : public QObject {
    Q_OBJECT
public:
    explicit DocumentMerger(QObject *parent = nullptr);

    // Merge translated segments back into documents
    bool mergeToTxt(const QList<DocumentSplitter::Segment> &translatedSegments,
                    const QString &outputPath);

    bool mergeToDocx(const QString &originalDocxPath,
                     const QList<DocumentSplitter::Segment> &originalSegments,
                     const QList<DocumentSplitter::Segment> &translatedSegments,
                     const QString &outputPath);

    bool mergeToEpub(const QString &originalEpubPath,
                     const QList<DocumentSplitter::Segment> &originalSegments,
                     const QList<DocumentSplitter::Segment> &translatedSegments,
                     const QString &title,
                     const QString &outputPath);

signals:
    void progress(int current, int total);
    void mergeComplete(QString path);
    void error(QString message);

private:
    // Helper to rebuild DOCX with translated content
    bool rebuildDocxWithTranslation(const QString &originalPath,
                                    const QString &translatedText,
                                    const QString &outputPath);

    // Helper to rebuild EPUB with translated chapters
    bool rebuildEpubWithTranslation(const QString &originalPath,
                                    const QMap<QString, QString> &chapterTranslations,
                                    const QString &title,
                                    const QString &outputPath);
};

#endif // DOCUMENTMERGER_H
