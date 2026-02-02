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

        // Store original XHTML content
        QString originalXhtml = QString::fromUtf8(content);

        // Parse HTML to extract text, preserving paragraph structure
        QString chapterText;
        QString currentPara;
        QXmlStreamReader xml(content);
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isCharacters()) {
                QString text = xml.text().toString().trimmed();
                if (!text.isEmpty()) {
                    currentPara += text + " ";
                }
            } else if (xml.isEndElement()) {
                QString tagName = xml.name().toString();
                // Detect paragraph/heading end tags
                if (tagName == "p" || tagName == "h1" || tagName == "h2" ||
                    tagName == "h3" || tagName == "h4" || tagName == "h5" || tagName == "h6") {
                    if (!currentPara.trimmed().isEmpty()) {
                        chapterText += currentPara.trimmed() + "\n";
                        currentPara.clear();
                    }
                }
            }
            if (xml.hasError()) {
                 // Non-fatal, just log
                 // qWarning() << "XML Parse error in" << name << ":" << xml.errorString();
            }
        }
        // Add any remaining text
        if (!currentPara.trimmed().isEmpty()) {
            chapterText += currentPara.trimmed() + "\n";
        }

        if (!chapterText.isEmpty()) {
            // Check if this chapter needs further splitting
            if (chapterText.toUtf8().size() > MAX_SEGMENT_SIZE) {
                // Split large chapter - for now, store original XHTML in first part only
                // This is a limitation: proper splitting would require DOM manipulation
                qDebug() << "Chapter too large (" << chapterText.size() << "), splitting by paragraphs...";
                QList<Segment> chapterSegments = splitTextByParagraphs(chapterText, MAX_SEGMENT_SIZE);
                for (int i = 0; i < chapterSegments.size(); i++) {
                    Segment seg = chapterSegments[i];
                    seg.identifier = QString("%1_part%2").arg(name).arg(i);
                    seg.index = segmentIndex++;
                    // Store original XHTML only in first part (limitation of current approach)
                    if (i == 0) {
                        seg.originalXhtml = originalXhtml;
                    }
                    segments.append(seg);
                }
            } else {
                Segment seg;
                seg.text = chapterText.trimmed();
                seg.identifier = name;
                seg.index = segmentIndex++;
                seg.originalSize = chapterText.toUtf8().size();
                seg.originalXhtml = originalXhtml;  // Store original XHTML structure
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
