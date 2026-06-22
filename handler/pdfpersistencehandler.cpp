#include "pdfpersistencehandler.h"

#include "annotationmanager.h"
#include "annotationpdfio.h"
#include "pagecachemanager.h"
#include "pdfcontenthandler.h"
#include "perthreadmupdfrenderer.h"

#include <QStringList>

PDFPersistenceHandler::PDFPersistenceHandler(PerThreadMuPDFRenderer* renderer,
                                             PageCacheManager* pageCache,
                                             PDFContentHandler* contentHandler,
                                             AnnotationManager* annotationManager,
                                             QObject* parent)
    : QObject(parent)
    , m_renderer(renderer)
    , m_pageCache(pageCache)
    , m_contentHandler(contentHandler)
    , m_annotationManager(annotationManager)
{
    if (m_contentHandler) {
        connect(m_contentHandler, &PDFContentHandler::unsavedOutlineChangesChanged,
                this, &PDFPersistenceHandler::refreshUnsavedState);
        connect(m_contentHandler, &PDFContentHandler::documentClosed,
                this, &PDFPersistenceHandler::refreshUnsavedState);
    }
    if (m_annotationManager) {
        connect(m_annotationManager, &AnnotationManager::dirtyChanged,
                this, &PDFPersistenceHandler::refreshUnsavedState);
    }
    refreshUnsavedState();
}

bool PDFPersistenceHandler::hasUnsavedChanges() const
{
    if (!m_renderer || !m_renderer->isDocumentLoaded()) {
        return false;
    }

    return (m_contentHandler && m_contentHandler->hasUnsavedOutlineChanges()) ||
           (m_annotationManager && m_annotationManager->isDirty());
}

PDFPersistenceSaveResult PDFPersistenceHandler::saveDocument()
{
    PDFPersistenceSaveResult result;
    if (!m_renderer || !m_renderer->isDocumentLoaded()) {
        result.errorMessage = tr("No document loaded");
        emit documentSaveCompleted(false, result.errorMessage);
        return result;
    }

    QStringList errors;
    if (m_contentHandler && m_contentHandler->hasUnsavedOutlineChanges()) {
        result.outlineSaved = m_contentHandler->saveOutlineChanges(QString());
        if (!result.outlineSaved) {
            errors.append(tr("Failed to save outline"));
        }
    }

    if (m_annotationManager && m_annotationManager->isDirty()) {
        QString annotationError;
        result.annotationsSaved = AnnotationPdfIO::save(
            m_renderer, m_annotationManager, &annotationError);
        if (!result.annotationsSaved) {
            errors.append(annotationError.isEmpty()
                              ? tr("Failed to save annotations")
                              : annotationError);
        } else {
            if (m_pageCache) {
                m_pageCache->clear();
            }
            emit documentVisualsChanged();
        }
    }

    result.success = errors.isEmpty();
    result.errorMessage = errors.join(QLatin1Char('\n'));
    refreshUnsavedState();
    emit documentSaveCompleted(result.success, result.errorMessage);
    return result;
}

void PDFPersistenceHandler::refreshUnsavedState()
{
    const bool hasUnsaved = hasUnsavedChanges();
    if (m_hasUnsavedChanges == hasUnsaved) {
        return;
    }
    m_hasUnsavedChanges = hasUnsaved;
    emit unsavedChangesChanged(hasUnsaved);
}
