#ifndef PDFPERSISTENCEHANDLER_H
#define PDFPERSISTENCEHANDLER_H

#include <QObject>
#include <QString>

class PerThreadMuPDFRenderer;
class PageCacheManager;
class PDFContentHandler;
class AnnotationManager;

struct PDFPersistenceSaveResult {
    bool success = false;
    bool outlineSaved = false;
    bool annotationsSaved = false;
    QString errorMessage;
};

// 协调“保存一份 PDF”这个跨组件事务；具体读写仍留在原有对象中。
class PDFPersistenceHandler : public QObject
{
    Q_OBJECT

public:
    PDFPersistenceHandler(PerThreadMuPDFRenderer* renderer,
                          PageCacheManager* pageCache,
                          PDFContentHandler* contentHandler,
                          AnnotationManager* annotationManager,
                          QObject* parent = nullptr);

    bool hasUnsavedChanges() const;
    PDFPersistenceSaveResult saveDocument();

public slots:
    void refreshUnsavedState();

signals:
    void unsavedChangesChanged(bool hasUnsaved);
    void documentSaveCompleted(bool success, const QString& errorMessage);
    void documentVisualsChanged();

private:
    PerThreadMuPDFRenderer* m_renderer;
    PageCacheManager* m_pageCache;
    PDFContentHandler* m_contentHandler;
    AnnotationManager* m_annotationManager;
    bool m_hasUnsavedChanges = false;
};

#endif // PDFPERSISTENCEHANDLER_H
