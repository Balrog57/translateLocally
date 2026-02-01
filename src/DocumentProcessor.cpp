#include "DocumentProcessor.h"
#include <QFileInfo>
#include <QDebug>

DocumentProcessor::DocumentProcessor(const QString &inputPath, const QString &outputPath, QObject *parent)
    : QObject(parent), m_inputPath(inputPath), m_outputPath(outputPath), m_splitter(this), m_merger(this)
{
}

DocumentProcessor::DocumentProcessor(QObject *parent)
    : QObject(parent), m_splitter(this), m_merger(this)
{
}

bool DocumentProcessor::open() {
    QFileInfo info(m_inputPath);
    if (!info.exists()) {
        qCritical() << "Input file does not exist:" << m_inputPath;
        return false;
    }

    if (DocumentSplitter::needsSplitting(m_inputPath)) {
        m_segments = m_splitter.splitDocument(m_inputPath);
        return !m_segments.isEmpty();
    } else {
        m_segments = m_splitter.splitDocument(m_inputPath);
        return !m_segments.isEmpty();
    }
}

QList<DocumentSplitter::Segment> DocumentProcessor::getSegments() {
    return m_segments;
}

void DocumentProcessor::setTranslatedSegments(const QList<DocumentSplitter::Segment> &segments) {
    m_translatedSegments = segments;
}

bool DocumentProcessor::save() {
    QFileInfo info(m_inputPath);
    QString ext = info.suffix().toLower();

    if (ext == "txt") {
        return m_merger.mergeToTxt(m_translatedSegments, m_outputPath);
    } else if (ext == "docx") {
        return m_merger.mergeToDocx(m_inputPath, m_segments, m_translatedSegments, m_outputPath);
    } else if (ext == "epub") {
        QString title = info.completeBaseName();
        return m_merger.mergeToEpub(m_inputPath, m_segments, m_translatedSegments, title, m_outputPath);
    } else if (ext == "pdf") {
         if (!m_outputPath.toLower().endsWith(".docx")) {
             qWarning() << "Saving PDF translation as DOCX (PDF export not supported yet).";
         }
         return m_merger.mergeToDocx(m_inputPath, m_segments, m_translatedSegments, m_outputPath);
    }

    return false;
}

QString DocumentProcessor::extractText(const QString &filePath) {
    QList<DocumentSplitter::Segment> segments = m_splitter.splitDocument(filePath);
    QString fullText;
    for (const auto &seg : segments) {
        fullText += seg.text + "\n";
    }
    return fullText.trimmed();
}
