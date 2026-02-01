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
