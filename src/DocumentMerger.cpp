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

QString DocumentMerger::replaceTextInWordXml(const QString &originalXml, const QString &translatedText) {
    // PARAGRAPH-BY-PARAGRAPH: Preserve paragraph structure and properties
    // Put translated text in first <w:t>, keep paragraph formatting

    if (translatedText.trimmed().isEmpty()) {
        return originalXml;
    }

    // Split translated text by newlines (one line = one paragraph from DocumentSplitter)
    QStringList translatedParas = translatedText.split('\n', Qt::SkipEmptyParts);
    int transIndex = 0;

    QString result = originalXml;

    // Find all paragraphs with text content
    QRegularExpression paraPattern("<w:p(\\s[^>]*)?>.*?</w:p>", QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator paraIt = paraPattern.globalMatch(originalXml);

    QVector<QPair<int, int>> replacements; // positions
    QStringList newParas;

    while (paraIt.hasNext()) {
        QRegularExpressionMatch paraMatch = paraIt.next();
        QString originalPara = paraMatch.captured(0);

        // Check if paragraph has text content
        QRegularExpression wtPattern("<w:t[^>]*>([^<]+)</w:t>");
        if (!wtPattern.match(originalPara).hasMatch()) {
            continue; // Skip paragraphs without text (e.g., just properties/formatting)
        }

        // Get next translated paragraph
        if (transIndex >= translatedParas.size()) {
            break; // No more translations
        }
        QString transPara = translatedParas[transIndex++];

        // Replace: keep paragraph structure, put translated text in FIRST <w:t>, remove others
        QString newPara = originalPara;

        // Find first <w:t> and replace its content
        QRegularExpressionMatch firstWt = wtPattern.match(newPara);
        if (firstWt.hasMatch()) {
            QString newWt = QString("<w:t xml:space=\"preserve\">%1</w:t>").arg(transPara.toHtmlEscaped());
            newPara.replace(firstWt.capturedStart(), firstWt.capturedLength(), newWt);

            // Remove all subsequent <w:t> nodes in this paragraph
            QRegularExpression allWtPattern("<w:t[^>]*>[^<]*</w:t>");
            int offset = firstWt.capturedStart() + newWt.length();
            QString afterFirst = newPara.mid(offset);
            afterFirst.replace(allWtPattern, "");
            newPara = newPara.left(offset) + afterFirst;
        }

        replacements.append(qMakePair(paraMatch.capturedStart(), paraMatch.capturedLength()));
        newParas.append(newPara);
    }

    // Replace in reverse order to maintain positions
    for (int i = replacements.size() - 1; i >= 0; i--) {
        result.replace(replacements[i].first, replacements[i].second, newParas[i]);
    }

    return result;
}

bool DocumentMerger::rebuildDocxWithTranslation(const QString &originalPath,
                                                 const QString &translatedText,
                                                 const QString &outputPath) {
    // Strategy: Copy the DOCX and replace text content in word/document.xml
    // while preserving ALL formatting, styles, tables, images, etc.

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

    while (archive_read_next_header(reader, &entry) == ARCHIVE_OK) {
        QString entryName = QString::fromUtf8(archive_entry_pathname(entry));

        if (entryName == "word/document.xml") {
            // Read the original document.xml
            size_t size = archive_entry_size(entry);
            QByteArray originalContent;
            originalContent.resize(size);
            archive_read_data(reader, originalContent.data(), size);

            // Replace text in XML while preserving structure
            QString originalXml = QString::fromUtf8(originalContent);
            QString modifiedXml = replaceTextInWordXml(originalXml, translatedText);

            QByteArray newContent = modifiedXml.toUtf8();

            // Write modified entry
            archive_entry_set_size(entry, newContent.size());
            archive_write_header(writer, entry);
            archive_write_data(writer, newContent.constData(), newContent.size());
        } else {
            // Copy other entries unchanged (styles, images, etc.)
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
    // PARAGRAPH-BY-PARAGRAPH APPROACH for EPUB: Similar to DOCX
    // Preserve paragraph structure, replace text content while keeping HTML tags

    if (translatedText.trimmed().isEmpty()) {
        return originalXhtml;
    }

    // Split translated text by newlines (DocumentSplitter concatenates with \n)
    QStringList translatedParas = translatedText.split('\n', Qt::SkipEmptyParts);
    int transIndex = 0;

    QString result = originalXhtml;

    // Find all <p>, <h1>, <h2>, <h3>, <h4>, <h5>, <h6> elements
    // Pattern matches: <tag ...> content </tag>
    QRegularExpression paraPattern("<(p|h[1-6])(\\s[^>]*)?>.*?</\\1>",
                                    QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator paraIt = paraPattern.globalMatch(originalXhtml);

    QVector<QPair<int, int>> replacements;
    QStringList newParas;

    while (paraIt.hasNext() && transIndex < translatedParas.size()) {
        QRegularExpressionMatch paraMatch = paraIt.next();
        QString originalPara = paraMatch.captured(0);
        QString tagName = paraMatch.captured(1); // p, h1, h2, etc.
        QString tagAttrs = paraMatch.captured(2); // attributes

        // Extract text content from this paragraph (strip all tags)
        QString paraText;
        QRegularExpression tagPattern("<[^>]+>");
        paraText = originalPara;
        paraText.remove(tagPattern); // Remove all HTML tags
        paraText = paraText.trimmed();

        // Skip empty paragraphs
        if (paraText.isEmpty()) {
            continue;
        }

        // Get corresponding translated paragraph
        QString transPara = translatedParas[transIndex++].trimmed();
        if (transPara.isEmpty()) {
            transPara = " "; // Keep structure, insert space
        }

        // Rebuild paragraph: For simplicity, replace ALL content between opening and closing tag
        // This preserves the paragraph tag itself but loses inline formatting
        // Future improvement: preserve <b>, <i>, etc. tags with more sophisticated parsing
        QString newPara = originalPara;

        // Pattern to match: <tag...>CONTENT</tag>
        // We want to replace CONTENT with the translated text
        QRegularExpression contentPattern(QString("(<(%1)([^>]*)>).*?(</\\2>)")
                                          .arg(tagName),
                                          QRegularExpression::DotMatchesEverythingOption);
        QRegularExpressionMatch contentMatch = contentPattern.match(originalPara);

        if (contentMatch.hasMatch()) {
            // Build: <opening tag>translated text</closing tag>
            newPara = contentMatch.captured(1) + transPara + contentMatch.captured(4);
        }

        replacements.append(qMakePair(paraMatch.capturedStart(), paraMatch.capturedLength()));
        newParas.append(newPara);
    }

    // Replace paragraphs in reverse order to maintain positions
    for (int i = replacements.size() - 1; i >= 0; i--) {
        result.replace(replacements[i].first, replacements[i].second, newParas[i]);
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
