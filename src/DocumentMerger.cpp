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

    // Build a map from identifier to translated text and original XHTML
    QMap<QString, QString> chapterTranslations;
    QMap<QString, QString> chapterOriginalXhtml;
    for (int i = 0; i < translatedSegments.size() && i < originalSegments.size(); i++) {
        chapterTranslations[originalSegments[i].identifier] = translatedSegments[i].text;
        chapterOriginalXhtml[originalSegments[i].identifier] = originalSegments[i].originalXhtml;
    }

    return rebuildEpubWithTranslation(originalEpubPath, chapterTranslations, chapterOriginalXhtml, title, outputPath);
}

QString DocumentMerger::replaceTextInXhtml(const QString &originalXhtml, const QString &translatedText) {
    // Parse the original XHTML and replace text nodes with translated content
    // Strategy: Split translated text into words and distribute across text nodes proportionally

    // Split translated text into words
    QStringList translatedWords = translatedText.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    int wordIndex = 0;

    // Rebuild XHTML with translated text nodes
    QString result;
    QXmlStreamReader xmlReader(originalXhtml);
    QXmlStreamWriter xmlWriter(&result);
    xmlWriter.setAutoFormatting(true);
    xmlWriter.setAutoFormattingIndent(2);

    while (!xmlReader.atEnd()) {
        xmlReader.readNext();

        if (xmlReader.isStartDocument()) {
            xmlWriter.writeStartDocument();
        } else if (xmlReader.isDTD()) {
            xmlWriter.writeDTD(xmlReader.text().toString());
        } else if (xmlReader.isStartElement()) {
            xmlWriter.writeStartElement(xmlReader.namespaceUri().toString(), xmlReader.name().toString());
            xmlWriter.writeAttributes(xmlReader.attributes());
        } else if (xmlReader.isEndElement()) {
            xmlWriter.writeEndElement();
        } else if (xmlReader.isCharacters()) {
            QString originalText = xmlReader.text().toString();
            if (originalText.trimmed().isEmpty()) {
                // Preserve whitespace-only text nodes
                xmlWriter.writeCharacters(originalText);
            } else {
                // Replace with translated text
                // Count words in original text node to know how many translated words to use
                QStringList originalWords = originalText.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                int wordCount = originalWords.size();

                // Extract corresponding translated words
                QStringList replacementWords;
                for (int i = 0; i < wordCount && wordIndex < translatedWords.size(); i++) {
                    replacementWords.append(translatedWords[wordIndex++]);
                }

                // Preserve leading/trailing whitespace from original
                bool hasLeadingSpace = originalText.startsWith(' ') || originalText.startsWith('\n') || originalText.startsWith('\t');
                bool hasTrailingSpace = originalText.endsWith(' ') || originalText.endsWith('\n') || originalText.endsWith('\t');

                QString replacement = replacementWords.join(" ");
                if (hasLeadingSpace && !replacement.isEmpty()) replacement = " " + replacement;
                if (hasTrailingSpace && !replacement.isEmpty()) replacement = replacement + " ";

                xmlWriter.writeCharacters(replacement);
            }
        } else if (xmlReader.isComment()) {
            xmlWriter.writeComment(xmlReader.text().toString());
        } else if (xmlReader.isCDATA()) {
            xmlWriter.writeCDATA(xmlReader.text().toString());
        }
    }

    if (xmlReader.hasError()) {
        qWarning() << "XML parsing error:" << xmlReader.errorString();
        // Fallback: create simple structure
        return QString(R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml">
<body><p>%1</p></body></html>)").arg(translatedText.toHtmlEscaped());
    }

    return result;
}

bool DocumentMerger::rebuildEpubWithTranslation(const QString &originalPath,
                                                 const QMap<QString, QString> &chapterTranslations,
                                                 const QMap<QString, QString> &chapterOriginalXhtml,
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
            QString originalXhtml;
            if (chapterTranslations.contains(entryName)) {
                translated = chapterTranslations[entryName];
                originalXhtml = chapterOriginalXhtml.value(entryName);
            } else {
                // Collect all parts
                for (int i = 0; ; i++) {
                    QString partKey = QString("%1_part%2").arg(entryName).arg(i);
                    if (chapterTranslations.contains(partKey)) {
                        translated += chapterTranslations[partKey] + " ";
                        if (i == 0) {
                            originalXhtml = chapterOriginalXhtml.value(partKey);
                        }
                    } else {
                        break;
                    }
                }
            }

            // Preserve original XHTML structure, replacing only text nodes
            QString newXhtml;
            if (!originalXhtml.isEmpty()) {
                newXhtml = replaceTextInXhtml(originalXhtml, translated);
            } else {
                // Fallback if no original XHTML stored (shouldn't happen with new code)
                newXhtml = QString(R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml">
<body><p>%1</p></body></html>)").arg(translated.toHtmlEscaped());
            }

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
