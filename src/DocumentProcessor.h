#ifndef DOCUMENTPROCESSOR_H
#define DOCUMENTPROCESSOR_H

#include <QObject>
#include <QString>
#include <QList>
#include "DocumentSplitter.h"
#include "DocumentMerger.h"

class DocumentProcessor : public QObject {
    Q_OBJECT
public:
    explicit DocumentProcessor(const QString &inputPath, const QString &outputPath, QObject *parent = nullptr);
    explicit DocumentProcessor(QObject *parent = nullptr);

    bool open();
    QList<DocumentSplitter::Segment> getSegments();
    void setTranslatedSegments(const QList<DocumentSplitter::Segment> &segments);
    bool save();

    // Legacy support for MainWindow
    QString extractText(const QString &filePath);

private:
    QString m_inputPath;
    QString m_outputPath;
    DocumentSplitter m_splitter;
    DocumentMerger m_merger;
    QList<DocumentSplitter::Segment> m_segments;
    QList<DocumentSplitter::Segment> m_translatedSegments;
};

#endif // DOCUMENTPROCESSOR_H
