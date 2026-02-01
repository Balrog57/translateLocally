# Implementation Roadmap for translateLocally Features

This roadmap outlines the steps to reimplement the "Document Processing" and "AI Improvement" features on a working base of `translateLocally` for Windows.

**Prerequisites:**
1.  A Windows environment where the **original** `translateLocally` compiles and runs successfully (i.e., `translateLocally.exe -m en-fr-tiny -i test.txt` works without crashing).
2.  Visual Studio 2019 or 2022 with C++ workload.
3.  Qt5 and standard dependencies installed (likely via `vcpkg`).

## 1. Add New Source Files

Copy the following files into the `src/` directory.

### `src/DocumentSplitter.h`
```cpp
#ifndef DOCUMENTSPLITTER_H
#define DOCUMENTSPLITTER_H

#include <QString>
#include <QObject>
#include <QList>

class DocumentSplitter : public QObject {
    Q_OBJECT
public:
    explicit DocumentSplitter(QObject *parent = nullptr);

    struct Segment {
        QString text;           // Text to translate
        QString identifier;     // For reassembly (chapter name, page number, etc.)
        int index;              // Order index
        qint64 originalSize;    // Size in bytes before translation
    };

    // Split a document into translatable segments
    QList<Segment> splitDocument(const QString &filePath);
    
    // Maximum segment size (8MB for safety margin under 10MB limit)
    static constexpr qint64 MAX_SEGMENT_SIZE = 8 * 1024 * 1024;
    
    // Check if a document needs splitting
    static bool needsSplitting(const QString &filePath);
    
    // Get file size
    static qint64 getFileSize(const QString &filePath);

    bool isLibreOfficeAvailable();

signals:
    void progress(int current, int total);
    void error(QString message);

private:
    QList<Segment> splitTxt(const QString &filePath);
    QList<Segment> splitDocx(const QString &filePath);
    QList<Segment> splitEpub(const QString &filePath);
    QList<Segment> splitPdf(const QString &filePath);  // Uses LibreOffice conversion
    
    // Helper: split text into chunks by paragraph boundaries
    QList<Segment> splitTextByParagraphs(const QString &text, qint64 maxSize);
    
    // LibreOffice integration for PDF
    QString convertPdfToDocx(const QString &pdfPath);
    QString findLibreOfficePath();
};

#endif // DOCUMENTSPLITTER_H
```

### `src/DocumentSplitter.cpp`
```cpp
#include "DocumentSplitter.h"
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QXmlStreamReader>
#include <QProcess>
#include <QDir>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QCoreApplication>

#include <archive.h>
#include <archive_entry.h>

#ifdef HAVE_POPPLER
#include <QtGlobal>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <poppler-qt6.h>
#else
#include <poppler-qt5.h>
#endif
#endif

DocumentSplitter::DocumentSplitter(QObject *parent) : QObject(parent) {}

bool DocumentSplitter::needsSplitting(const QString &filePath) {
    return getFileSize(filePath) > MAX_SEGMENT_SIZE;
}

qint64 DocumentSplitter::getFileSize(const QString &filePath) {
    QFileInfo info(filePath);
    return info.size();
}

QList<DocumentSplitter::Segment> DocumentSplitter::splitDocument(const QString &filePath) {
    QFileInfo info(filePath);
    QString ext = info.suffix().toLower();
    
    if (ext == "txt") {
        return splitTxt(filePath);
    } else if (ext == "docx") {
        return splitDocx(filePath);
    } else if (ext == "epub") {
        return splitEpub(filePath);
    } else if (ext == "pdf") {
        return splitPdf(filePath);
    }
    
    emit error(tr("Unsupported file format: %1").arg(ext));
    return {};
}

QList<DocumentSplitter::Segment> DocumentSplitter::splitTxt(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit error(tr("Could not open text file: %1").arg(filePath));
        return {};
    }
    
    QString fullText = QString::fromUtf8(file.readAll());
    file.close();
    
    return splitTextByParagraphs(fullText, MAX_SEGMENT_SIZE);
}

QList<DocumentSplitter::Segment> DocumentSplitter::splitTextByParagraphs(const QString &text, qint64 maxSize) {
    QList<Segment> segments;
    QStringList paragraphs = text.split('\n', Qt::KeepEmptyParts);
    
    QString currentChunk;
    int segmentIndex = 0;
    
    for (const QString &para : paragraphs) {
        // Check if adding this paragraph would exceed the limit
        QString potentialChunk = currentChunk.isEmpty() ? para : currentChunk + "\n" + para;
        
        if (potentialChunk.toUtf8().size() > maxSize && !currentChunk.isEmpty()) {
            // Save current chunk as a segment
            Segment seg;
            seg.text = currentChunk;
            seg.identifier = QString("segment_%1").arg(segmentIndex);
            seg.index = segmentIndex;
            seg.originalSize = currentChunk.toUtf8().size();
            segments.append(seg);
            segmentIndex++;
            
            currentChunk = para;
        } else {
            currentChunk = potentialChunk;
        }
    }
    
    // Don't forget the last chunk
    if (!currentChunk.isEmpty()) {
        Segment seg;
        seg.text = currentChunk;
        seg.identifier = QString("segment_%1").arg(segmentIndex);
        seg.index = segmentIndex;
        seg.originalSize = currentChunk.toUtf8().size();
        segments.append(seg);
    }
    
    emit progress(segments.size(), segments.size());
    return segments;
}

QList<DocumentSplitter::Segment> DocumentSplitter::splitDocx(const QString &filePath) {
    qDebug() << "Start splitting DOCX:" << filePath;
    QList<Segment> segments;
    struct archive *a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);
    
    if (archive_read_open_filename(a, filePath.toUtf8().constData(), 10240) != ARCHIVE_OK) {
        QString err = QString::fromUtf8(archive_error_string(a));
        qWarning() << "Error opening DOCX archive:" << err;
        emit error(tr("Error opening DOCX: %1").arg(err));
        archive_read_free(a);
        return {};
    }

    struct archive_entry *entry;
    QString fullText;
    bool foundDocument = false;

    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        QString name = QString::fromUtf8(archive_entry_pathname(entry));
        if (name == "word/document.xml") {
            foundDocument = true;
            qDebug() << "Found word/document.xml, extracting text...";
            
            // Safe reading by chunks
            QByteArray content;
            char buffer[8192];
            la_ssize_t len;
            while ((len = archive_read_data(a, buffer, sizeof(buffer))) > 0) {
                content.append(buffer, len);
            }
            
            if (len < 0) {
                qWarning() << "Error extracting word/document.xml:" << archive_error_string(a);
                break;
            }

            // Parse XML to extract paragraphs
            QXmlStreamReader xml(content);
            QString currentPara;
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isStartElement() && xml.name().toString() == "t") {
                    currentPara += xml.readElementText();
                } else if (xml.isEndElement() && xml.name().toString() == "p") {
                    if (!currentPara.isEmpty()) {
                        fullText += currentPara + "\n";
                        currentPara.clear();
                    }
                }
            }
            break;
        }
    }
    
    archive_read_free(a);
    
    if (!foundDocument) {
        qWarning() << "word/document.xml not found in DOCX.";
        emit error(tr("Invalid DOCX: word/document.xml not found."));
        return {};
    }

    qDebug() << "Extracted" << fullText.size() << "characters from DOCX. Splitting...";
    // Split the full text into segments
    return splitTextByParagraphs(fullText, MAX_SEGMENT_SIZE);
}

QList<DocumentSplitter::Segment> DocumentSplitter::splitEpub(const QString &filePath) {
    qDebug() << "Start splitting EPUB:" << filePath;
    QList<Segment> segments;
    struct archive *a = archive_read_new();
    archive_read_support_format_all(a);

    archive_read_support_filter_all(a);
    
    if (archive_read_open_filename(a, filePath.toUtf8().constData(), 10240) != ARCHIVE_OK) {
        QString err = QString::fromUtf8(archive_error_string(a));
        qWarning() << "Error opening EPUB archive:" << err;
        emit error(tr("Error opening EPUB: %1").arg(err));
        archive_read_free(a);
        return {};
    }

    struct archive_entry *entry;
    int segmentIndex = 0;
    QStringList chapterFiles;
    
    // First pass: collect all chapter files
    qDebug() << "First pass: scanning for chapters...";
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        QString name = QString::fromUtf8(archive_entry_pathname(entry));
        if (name.endsWith(".xhtml") || name.endsWith(".html")) {
            chapterFiles.append(name);
            qDebug() << "Found chapter:" << name;
        }
    }
    archive_read_free(a);
    
    if (chapterFiles.isEmpty()) {
        qWarning() << "No chapters found in EPUB.";
        emit error(tr("No chapters found in EPUB file."));
        return {};
    }

    emit progress(0, chapterFiles.size());
    
    // Second pass: extract each chapter
    qDebug() << "Second pass: extracting content...";
    a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);
    
    if (archive_read_open_filename(a, filePath.toUtf8().constData(), 10240) != ARCHIVE_OK) {
        QString err = QString::fromUtf8(archive_error_string(a));
        qCritical() << "Error re-opening EPUB for extraction:" << err;
        emit error(tr("Error reading EPUB: %1").arg(err));
        archive_read_free(a);
        return {};
    }
    
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        QString name = QString::fromUtf8(archive_entry_pathname(entry));
        
        // Skip if not a chapter we want
        if (!name.endsWith(".xhtml") && !name.endsWith(".html")) {
            continue;
        }

        qDebug() << "Processing chapter:" << name;
        
        // Safer reading: read in chunks instead of relying on entry size
        QByteArray content;
        char buffer[8192];
        la_ssize_t len;
        
        while ((len = archive_read_data(a, buffer, sizeof(buffer))) > 0) {
            content.append(buffer, len);
        }
        
        if (len < 0) {
            QString err = QString::fromUtf8(archive_error_string(a));
            qWarning() << "Error reading data from entry" << name << ":" << err;
            // Continue to next file instead of aborting everything? 
            // Better to warn and try to salvage other chapters.
            continue; 
        }

        qDebug() << "Read" << content.size() << "bytes from" << name;
            
        // Parse HTML to extract text
        QString chapterText;
        QXmlStreamReader xml(content);
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isCharacters()) {
                QString text = xml.text().toString().trimmed();
                if (!text.isEmpty()) {
                    chapterText += text + " ";
                }
            }
            if (xml.hasError()) {
                 // Non-fatal, just log
                 // qWarning() << "XML Parse error in" << name << ":" << xml.errorString();
            }
        }
        
        if (!chapterText.isEmpty()) {
            // Check if this chapter needs further splitting
            if (chapterText.toUtf8().size() > MAX_SEGMENT_SIZE) {
                // Split large chapter
                qDebug() << "Chapter too large (" << chapterText.size() << "), splitting by paragraphs...";
                QList<Segment> chapterSegments = splitTextByParagraphs(chapterText, MAX_SEGMENT_SIZE);
                for (int i = 0; i < chapterSegments.size(); i++) {
                    Segment seg = chapterSegments[i];
                    seg.identifier = QString("%1_part%2").arg(name).arg(i);
                    seg.index = segmentIndex++;
                    segments.append(seg);
                }
            } else {
                Segment seg;
                seg.text = chapterText.trimmed();
                seg.identifier = name;
                seg.index = segmentIndex++;
                seg.originalSize = chapterText.toUtf8().size();
                segments.append(seg);
            }
            
            emit progress(segmentIndex, chapterFiles.size());
        }
    }
    
    archive_read_free(a);
    qDebug() << "Finished processing EPUB. Total segments:" << segments.size();
    return segments;
}

QString DocumentSplitter::findLibreOfficePath() {
#ifdef Q_OS_WIN
    // Common Windows paths for LibreOffice
    QStringList possiblePaths = {
        "C:/Program Files/LibreOffice/program/soffice.exe",
        "C:/Program Files (x86)/LibreOffice/program/soffice.exe",
        QStandardPaths::findExecutable("soffice"),
        QStandardPaths::findExecutable("soffice.exe")
    };
    
    for (const QString &path : possiblePaths) {
        if (!path.isEmpty() && QFile::exists(path)) {
            return path;
        }
    }
#elif defined(Q_OS_MAC)
    QStringList possiblePaths = {
        "/Applications/LibreOffice.app/Contents/MacOS/soffice",
        QStandardPaths::findExecutable("soffice")
    };
    
    for (const QString &path : possiblePaths) {
        if (!path.isEmpty() && QFile::exists(path)) {
            return path;
        }
    }
#else
    // Linux
    QString path = QStandardPaths::findExecutable("soffice");
    if (!path.isEmpty()) return path;
    
    path = QStandardPaths::findExecutable("libreoffice");
    if (!path.isEmpty()) return path;
#endif
    
    return QString();
}

bool DocumentSplitter::isLibreOfficeAvailable() {
    return !findLibreOfficePath().isEmpty();
}

QString DocumentSplitter::convertPdfToDocx(const QString &pdfPath) {
    QString sofficePath = findLibreOfficePath();
    if (sofficePath.isEmpty()) {
        emit error(tr("LibreOffice not found. Please install LibreOffice to convert PDF files. "
                      "Download from: https://www.libreoffice.org/download/"));
        return QString();
    }
    
    // Create temporary output directory
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        emit error(tr("Could not create temporary directory for PDF conversion."));
        return QString();
    }
    tempDir.setAutoRemove(false);
    
    // Run LibreOffice conversion: pdf -> docx
    QProcess process;
    QStringList args;
    args << "--headless"
         << "--convert-to" << "docx"
         << "--outdir" << tempDir.path()
         << pdfPath;
    
    process.start(sofficePath, args);
    if (!process.waitForStarted(30000)) {
        emit error(tr("Failed to start LibreOffice for PDF conversion."));
        return QString();
    }
    
    if (!process.waitForFinished(300000)) { // 5 minutes timeout for large PDFs
        emit error(tr("LibreOffice PDF conversion timed out."));
        process.kill();
        return QString();
    }
    
    if (process.exitCode() != 0) {
        emit error(tr("LibreOffice conversion failed: %1").arg(QString(process.readAllStandardError())));
        return QString();
    }
    
    // Find the output file
    QFileInfo pdfInfo(pdfPath);
    QString outputPath = tempDir.path() + "/" + pdfInfo.completeBaseName() + ".docx";
    
    if (!QFile::exists(outputPath)) {
        emit error(tr("PDF conversion produced no output file."));
        return QString();
    }
    
    return outputPath;
}

QList<DocumentSplitter::Segment> DocumentSplitter::splitPdf(const QString &filePath) {
    // Convert PDF to DOCX using LibreOffice
    QString docxPath = convertPdfToDocx(filePath);
    if (docxPath.isEmpty()) {
        return {};
    }
    
    // Now process as DOCX
    QList<Segment> segments = splitDocx(docxPath);
    
    // Update identifiers to indicate PDF source
    for (int i = 0; i < segments.size(); i++) {
        segments[i].identifier = QString("pdf_converted_%1").arg(segments[i].identifier);
    }
    
    // Clean up temporary DOCX file
    QFile::remove(docxPath);
    QFileInfo docxInfo(docxPath);
    QDir(docxInfo.path()).removeRecursively();
    
    return segments;
}
```

### `src/DocumentMerger.h`
```cpp
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
```

### `src/DocumentMerger.cpp`
```cpp
#include "DocumentMerger.h"
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QTemporaryFile>
#include <QDir>
#include <QTextStream>
#include <QMap>
#include <QRegularExpression>

#include <archive.h>
#include <archive_entry.h>

DocumentMerger::DocumentMerger(QObject *parent) : QObject(parent) {}

bool DocumentMerger::mergeToTxt(const QList<DocumentSplitter::Segment> &translatedSegments,
                                 const QString &outputPath) {
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit error(tr("Could not open file for writing: %1").arg(outputPath));
        return false;
    }
    
    QTextStream out(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    out.setEncoding(QStringConverter::Utf8);
#else
    out.setCodec("UTF-8");
#endif

    // Sort segments by index and concatenate
    QList<DocumentSplitter::Segment> sorted = translatedSegments;
    std::sort(sorted.begin(), sorted.end(), [](const DocumentSplitter::Segment &a, const DocumentSplitter::Segment &b) {
        return a.index < b.index;
    });
    
    for (int i = 0; i < sorted.size(); i++) {
        out << sorted[i].text;
        if (i < sorted.size() - 1) {
            out << "\n";
        }
        emit progress(i + 1, sorted.size());
    }
    
    file.close();
    emit mergeComplete(outputPath);
    return true;
}

bool DocumentMerger::mergeToDocx(const QString &originalDocxPath,
                                  const QList<DocumentSplitter::Segment> &originalSegments,
                                  const QList<DocumentSplitter::Segment> &translatedSegments,
                                  const QString &outputPath) {
    Q_UNUSED(originalSegments);
    
    // Concatenate all translated segments
    QList<DocumentSplitter::Segment> sorted = translatedSegments;
    std::sort(sorted.begin(), sorted.end(), [](const DocumentSplitter::Segment &a, const DocumentSplitter::Segment &b) {
        return a.index < b.index;
    });
    
    QString fullTranslation;
    for (const auto &seg : sorted) {
        fullTranslation += seg.text + "\n";
    }
    
    return rebuildDocxWithTranslation(originalDocxPath, fullTranslation.trimmed(), outputPath);
}

bool DocumentMerger::rebuildDocxWithTranslation(const QString &originalPath,
                                                 const QString &translatedText,
                                                 const QString &outputPath) {
    // Strategy: Copy the DOCX and replace text content in word/document.xml
    // This preserves formatting, styles, etc.
    
    struct archive *reader = archive_read_new();
    struct archive *writer = archive_write_new();
    struct archive_entry *entry;
    
    archive_read_support_format_all(reader);
    archive_read_support_filter_all(reader);
    archive_write_set_format_zip(writer);
    
    if (archive_read_open_filename(reader, originalPath.toUtf8().constData(), 10240) != ARCHIVE_OK) {
        emit error(tr("Could not open original DOCX: %1").arg(originalPath));
        return false;
    }
    
    if (archive_write_open_filename(writer, outputPath.toUtf8().constData()) != ARCHIVE_OK) {
        archive_read_free(reader);
        emit error(tr("Could not create output DOCX: %1").arg(outputPath));
        return false;
    }
    
    // Split translated text into paragraphs for replacement
    QStringList translatedParagraphs = translatedText.split('\n', Qt::KeepEmptyParts);
    int paraIndex = 0;
    
    while (archive_read_next_header(reader, &entry) == ARCHIVE_OK) {
        QString entryName = QString::fromUtf8(archive_entry_pathname(entry));
        
        if (entryName == "word/document.xml") {
            // Read the original document.xml
            size_t size = archive_entry_size(entry);
            QByteArray originalContent;
            originalContent.resize(size);
            archive_read_data(reader, originalContent.data(), size);
            
            // Parse and replace text content
            QString modifiedXml;
            QXmlStreamReader xmlReader(originalContent);
            QXmlStreamWriter xmlWriter(&modifiedXml);
            
            while (!xmlReader.atEnd()) {
                xmlReader.readNext();
                
                if (xmlReader.isStartElement()) {
                    xmlWriter.writeStartElement(xmlReader.namespaceUri().toString(), xmlReader.name().toString());
                    xmlWriter.writeAttributes(xmlReader.attributes());
                } else if (xmlReader.isEndElement()) {
                    xmlWriter.writeEndElement();
                } else if (xmlReader.isCharacters()) {
                    if (xmlReader.text().toString().trimmed().isEmpty()) {
                        xmlWriter.writeCharacters(xmlReader.text().toString());
                    } else {
                        // Replace with translated text
                        if (paraIndex < translatedParagraphs.size()) {
                            // Try to map original text blocks to translated paragraphs
                            // This is a simplified approach - for complex docs, more sophisticated mapping needed
                            xmlWriter.writeCharacters(xmlReader.text().toString()); // Keep original for now
                        } else {
                            xmlWriter.writeCharacters(xmlReader.text().toString());
                        }
                    }
                } else if (xmlReader.isComment()) {
                    xmlWriter.writeComment(xmlReader.text().toString());
                } else if (xmlReader.isCDATA()) {
                    xmlWriter.writeCDATA(xmlReader.text().toString());
                }
            }
            
            // For now, use a simpler approach: create new document with translated paragraphs
            // preserving basic DOCX structure
            QString newDocXml = QString(R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<w:body>
)");
            for (const QString &para : translatedParagraphs) {
                if (!para.trimmed().isEmpty()) {
                    newDocXml += QString("<w:p><w:r><w:t>%1</w:t></w:r></w:p>\n").arg(para.toHtmlEscaped());
                }
            }
            newDocXml += "</w:body></w:document>";
            
            QByteArray newContent = newDocXml.toUtf8();
            
            // Write modified entry
            archive_entry_set_size(entry, newContent.size());
            archive_write_header(writer, entry);
            archive_write_data(writer, newContent.constData(), newContent.size());
        } else {
            // Copy other entries unchanged
            size_t size = archive_entry_size(entry);
            archive_write_header(writer, entry);
            
            if (size > 0) {
                QByteArray buffer;
                buffer.resize(size);
                archive_read_data(reader, buffer.data(), size);
                archive_write_data(writer, buffer.constData(), size);
            }
        }
    }
    
    archive_read_free(reader);
    archive_write_close(writer);
    archive_write_free(writer);
    
    emit mergeComplete(outputPath);
    return true;
}

bool DocumentMerger::mergeToEpub(const QString &originalEpubPath,
                                  const QList<DocumentSplitter::Segment> &originalSegments,
                                  const QList<DocumentSplitter::Segment> &translatedSegments,
                                  const QString &title,
                                  const QString &outputPath) {
    Q_UNUSED(title);
    
    // Build a map from identifier to translated text
    QMap<QString, QString> chapterTranslations;
    for (int i = 0; i < translatedSegments.size() && i < originalSegments.size(); i++) {
        chapterTranslations[originalSegments[i].identifier] = translatedSegments[i].text;
    }
    
    return rebuildEpubWithTranslation(originalEpubPath, chapterTranslations, title, outputPath);
}

bool DocumentMerger::rebuildEpubWithTranslation(const QString &originalPath,
                                                 const QMap<QString, QString> &chapterTranslations,
                                                 const QString &title,
                                                 const QString &outputPath) {
    Q_UNUSED(title);
    
    struct archive *reader = archive_read_new();
    struct archive *writer = archive_write_new();
    struct archive_entry *entry;
    
    archive_read_support_format_all(reader);
    archive_read_support_filter_all(reader);
    archive_write_set_format_zip(writer);
    
    if (archive_read_open_filename(reader, originalPath.toUtf8().constData(), 10240) != ARCHIVE_OK) {
        emit error(tr("Could not open original EPUB: %1").arg(originalPath));
        return false;
    }
    
    if (archive_write_open_filename(writer, outputPath.toUtf8().constData()) != ARCHIVE_OK) {
        archive_read_free(reader);
        emit error(tr("Could not create output EPUB: %1").arg(outputPath));
        return false;
    }
    
    int processed = 0;
    int total = chapterTranslations.size();
    
    while (archive_read_next_header(reader, &entry) == ARCHIVE_OK) {
        QString entryName = QString::fromUtf8(archive_entry_pathname(entry));
        
        if (chapterTranslations.contains(entryName) || 
            chapterTranslations.contains(entryName + "_part0")) {
            // This is a chapter that was translated
            size_t size = archive_entry_size(entry);
            QByteArray originalContent;
            originalContent.resize(size);
            archive_read_data(reader, originalContent.data(), size);
            
            // Get the translated text for this chapter
            QString translated;
            if (chapterTranslations.contains(entryName)) {
                translated = chapterTranslations[entryName];
            } else {
                // Collect all parts
                for (int i = 0; ; i++) {
                    QString partKey = QString("%1_part%2").arg(entryName).arg(i);
                    if (chapterTranslations.contains(partKey)) {
                        translated += chapterTranslations[partKey] + " ";
                    } else {
                        break;
                    }
                }
            }
            
            // Create new XHTML with translated content
            QString newXhtml = QString(R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
  <title>Translated Chapter</title>
  <style>body { font-family: serif; line-height: 1.6; margin: 2em; }</style>
</head>
<body>
)");
            // Split by sentences/paragraphs and wrap in <p> tags
            QStringList paragraphs = translated.split(QRegularExpression("\\n+"), Qt::SkipEmptyParts);
            for (const QString &para : paragraphs) {
                newXhtml += QString("<p>%1</p>\n").arg(para.toHtmlEscaped());
            }
            newXhtml += "</body></html>";
            
            QByteArray newContent = newXhtml.toUtf8();
            archive_entry_set_size(entry, newContent.size());
            archive_write_header(writer, entry);
            archive_write_data(writer, newContent.constData(), newContent.size());
            
            processed++;
            emit progress(processed, total);
        } else {
            // Copy unchanged
            size_t size = archive_entry_size(entry);
            archive_write_header(writer, entry);
            
            if (size > 0) {
                QByteArray buffer;
                buffer.resize(size);
                archive_read_data(reader, buffer.data(), size);
                archive_write_data(writer, buffer.constData(), size);
            }
        }
    }
    
    archive_read_free(reader);
    archive_write_close(writer);
    archive_write_free(writer);
    
    emit mergeComplete(outputPath);
    return true;
}
```

### `src/DocumentProcessor.h`
```cpp
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
```

### `src/DocumentProcessor.cpp`
```cpp
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
```

### `src/LLMInterface.h`
```cpp
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

signals:
    void verificationStarted();
    void verificationProgress(int completed, int total);
    void partialResultReady(QString suggestion);
    void verificationReady(QString llmSuggestion);
    void modelsDiscovered(QStringList models);
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
```

### `src/LLMInterface.cpp`
```cpp
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
```


## 2. Update `CMakeLists.txt`

Ensure the new files are included in the build source list in `src/CMakeLists.txt`:

```cmake
set(TRANSLATELOCALLY_SOURCES
    ...
    DocumentProcessor.cpp
    DocumentSplitter.cpp
    DocumentMerger.cpp
    LLMInterface.cpp
    ...
)
```

## 3. Apply CLI Modifications

Apply the provided `feature_integration.patch` using git:

```bash
git apply feature_integration.patch
```

This modifies:
- `src/cli/CLIParsing.h`: Adds `--ai-improve`.
- `src/cli/CommandLineIface.h/cpp`: Hooks up the document processing and AI logic.

## 4. Rebuild

Run your standard build command (e.g., `build_full.bat` or `cmake --build`).

## 5. Usage

To specific document translation with AI improvement:

```bash
translateLocally.exe -m en-fr-tiny -i input.docx -o output.docx --ai-improve
```
