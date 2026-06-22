#include "pdfdocumentsession.h"
#include "perthreadmupdfrenderer.h"
#include "pagecachemanager.h"
#include "textcachemanager.h"
#include "pdfviewhandler.h"
#include "pdfcontenthandler.h"
#include "pdfinteractionhandler.h"
#include "pdfannotationhandler.h"
#include "pdfbackgroundtaskhandler.h"
#include "pdfpersistencehandler.h"
#include "annotationmanager.h"
#include "pdfdocumentstate.h"
#include "appconfig.h"
#include <QDebug>
#include <QFileInfo>

PDFDocumentSession::PDFDocumentSession(QObject* parent)
    : QObject(parent)
{
    m_renderer = std::make_unique<PerThreadMuPDFRenderer>();

    m_pageCache = std::make_unique<PageCacheManager>(
        AppConfig::instance().maxCacheSize(),
        PageCacheManager::CacheStrategy::NearCurrent
        );

    m_backgroundTaskHandler = std::make_unique<PDFBackgroundTaskHandler>(this);
    m_textCache = std::make_unique<TextCacheManager>(
        m_renderer.get(), m_backgroundTaskHandler.get(), this);

    m_state = std::make_unique<PDFDocumentState>(this);

    m_annotationManager = std::make_unique<AnnotationManager>(this);
    m_annotationHandler = std::make_unique<PDFAnnotationHandler>(m_annotationManager.get(), this);

    m_viewHandler = std::make_unique<PDFViewHandler>(m_renderer.get(), m_state.get(), this);
    m_contentHandler = std::make_unique<PDFContentHandler>(m_renderer.get(), this);
    m_interactionHandler = std::make_unique<PDFInteractionHandler>(
        m_renderer.get(),
        m_textCache.get(),
        m_backgroundTaskHandler.get(),
        this
        );
    m_persistenceHandler = std::make_unique<PDFPersistenceHandler>(
        m_renderer.get(), m_pageCache.get(), m_contentHandler.get(),
        m_annotationManager.get(), this);

    setupConnections();
}

PDFDocumentSession::~PDFDocumentSession()
{
    disconnect(m_interactionHandler.get(), nullptr, this, nullptr);
    closeDocument();
    qInfo() << "PDFDocumentSession: Destroyed";
}

bool PDFDocumentSession::loadDocument(const QString& filePath, QString* errorMessage)
{
    if (filePath.isEmpty()) {
        if (errorMessage) *errorMessage = tr("Empty file path");
        return false;
    }

    if (m_state->isDocumentLoaded()) {
        closeDocument();
    }

    if (m_backgroundTaskHandler) {
        m_backgroundTaskHandler->invalidateAll();
    }

    QString error;
    if (!m_contentHandler->loadDocument(filePath, &error)) {
        if (errorMessage) *errorMessage = error;
        return false;
    }

    return true;
}

void PDFDocumentSession::closeDocument()
{
    if (!m_state->isDocumentLoaded()) {
        return;
    }

    if (m_backgroundTaskHandler) {
        m_backgroundTaskHandler->invalidateAll();
    }

    if (m_interactionHandler && m_state->isTextPDF()) {
        m_interactionHandler->cancelSearch();
        m_interactionHandler->clearHoveredLink();
        m_interactionHandler->clearTextSelection();
    }

    if (m_textCache) {
        m_textCache->cancelPreload();
    }

    if (m_pageCache) {
        m_pageCache->clear();
    }

    if (m_textCache) {
        m_textCache->clear();
    }

    if (m_annotationManager) {
        m_annotationManager->reset();
    }

    if (m_contentHandler) {
        m_contentHandler->closeDocument();
    }

    m_state->reset();

    if (m_persistenceHandler) {
        m_persistenceHandler->refreshUnsavedState();
    }

    qInfo() << "PDFDocumentSession: Document closed";
}


void PDFDocumentSession::calculatePagePositions()
{
    QVector<int> positions;
    QVector<int> heights;
    m_viewHandler->calculatePagePositions(positions, heights);
}

void PDFDocumentSession::updateCurrentPageFromScroll(int scrollY, int margin)
{
    int newPage = m_viewHandler->calculateCurrentPageFromScroll(scrollY, margin);

    if (newPage >= 0 && newPage != m_state->currentPage()) {
        m_state->setCurrentPage(newPage);
        emit currentPageChanged(newPage);
    }
}

void PDFDocumentSession::saveViewportState(int scrollY)
{
    m_state->saveViewportState(scrollY);
}

void PDFDocumentSession::clearViewportRestore()
{
    m_state->clearViewportRestore();
}

void PDFDocumentSession::setupConnections()
{
    if (m_viewHandler) {
        connect(m_viewHandler.get(), &PDFViewHandler::pageNavigationCompleted,
                this, [this](int newPageIndex) {
                    m_state->setCurrentPage(newPageIndex);
                    updateCacheAfterStateChange();
                    if(m_state->isContinuousScroll()) {
                        int targetY = m_viewHandler->getScrollPositionForPage(
                            newPageIndex, AppConfig::PAGE_MARGIN);
                        emit scrollToPositionRequested(targetY);
                    }
                    emit currentPageChanged(newPageIndex);
                });

        connect(m_viewHandler.get(), &PDFViewHandler::zoomSettingCompleted,
                this, [this](double newZoom, ZoomMode newMode) {
                    m_state->setCurrentZoomMode(newMode);

                    if (newZoom < 0) {
                        emit zoomSettingCompleted(newZoom, newMode);
                        return;
                    }

                    if (m_state->isContinuousScroll() &&
                        qAbs(m_state->currentZoom() - newZoom) > 0.001) {
                        emit requestCurrentScrollPosition();
                    }

                    m_state->setCurrentZoom(newZoom);

                    updateCacheAfterStateChange();

                    emit currentZoomChanged(newZoom);
                });

        connect(m_viewHandler.get(), &PDFViewHandler::displayModeSettingCompleted,
                this, [this](PageDisplayMode newMode, int adjustedPage) {
                    if (newMode == PageDisplayMode::DoublePage && m_state->isContinuousScroll()) {
                        m_state->setContinuousScroll(false);
                        emit continuousScrollChanged(false);
                    }

                    m_state->setCurrentDisplayMode(newMode);

                    if (adjustedPage != m_state->currentPage()) {
                        m_state->setCurrentPage(adjustedPage);
                    }

                    emit currentDisplayModeChanged(newMode);
                });

        connect(m_viewHandler.get(), &PDFViewHandler::continuousScrollSettingCompleted,
                this, [this](bool continuous) {
                    m_state->setContinuousScroll(continuous);
                    emit continuousScrollChanged(continuous);
                });

        connect(m_viewHandler.get(), &PDFViewHandler::pagePositionsCalculated,
                this, [this](const QVector<int>& positions, const QVector<int>& heights) {
                    m_state->setPagePositions(positions, heights);
                    emit pagePositionsChanged(positions, heights);
                });

        connect(m_viewHandler.get(), &PDFViewHandler::rotationSettingCompleted,
                this, [this](int newRotation) {
                    m_state->setCurrentRotation(newRotation);

                    if (m_state->isContinuousScroll()) {
                        calculatePagePositions();
                    }

                    updateCacheAfterStateChange();

                    emit currentRotationChanged(newRotation);
                });

        connect(m_viewHandler.get(), &PDFViewHandler::scrollToPositionRequested,
                this, &PDFDocumentSession::scrollToPositionRequested);
    }

    if (m_contentHandler) {
        connect(m_contentHandler.get(), &PDFContentHandler::documentLoaded,
                this, [this](const QString& filePath, int pageCount) {
                    bool isTextPDF = m_contentHandler->isTextPDF(5);
                    m_state->setDocumentLoaded(true, filePath, pageCount, isTextPDF);
                    m_state->setCurrentPage(0);

                    qInfo() << "PDFDocumentSession: Document loaded -"
                            << QFileInfo(filePath).fileName()
                            << "Type:" << (isTextPDF ? "Text PDF" : "Scanned PDF");

                    emit documentLoaded(filePath, pageCount);
                });
    }

    if (m_interactionHandler) {
        connect(m_interactionHandler.get(), &PDFInteractionHandler::searchCompleted,
                this, [this](const QString& query, int totalMatches) {
                    m_state->setSearchState(false, totalMatches, -1);
                    emit searchCompleted(query, totalMatches);
                });

        connect(m_interactionHandler.get(), &PDFInteractionHandler::searchCancelled,
                this, [this]() {
                    m_state->setSearchState(false, 0, -1);
                    emit searchCancelled();
                });

        connect(m_interactionHandler.get(), &PDFInteractionHandler::searchNavigationCompleted,
                this, [this](const SearchResult& result, int currentIndex, int totalMatches) {
                    m_state->setSearchState(false, totalMatches, currentIndex);
                });

        connect(m_interactionHandler.get(), &PDFInteractionHandler::linksVisibilityChanged,
                this, [this](bool visible) {
                    m_state->setLinksVisible(visible);
                });

        connect(m_interactionHandler.get(), &PDFInteractionHandler::textSelectionChanged,
                this, [this](bool hasSelection, const QString& selectedText) {
                    Q_UNUSED(selectedText);
                    m_state->setHasTextSelection(hasSelection);
                    emit textSelectionChanged(hasSelection);
                });
    }

    if (m_textCache) {
        connect(m_textCache.get(), &TextCacheManager::preloadProgress,
                this, &PDFDocumentSession::textPreloadProgress);
        connect(m_textCache.get(), &TextCacheManager::preloadCompleted,
                this, &PDFDocumentSession::textPreloadCompleted);
        connect(m_textCache.get(), &TextCacheManager::preloadCancelled,
                this, &PDFDocumentSession::textPreloadCancelled);
    }
}

void PDFDocumentSession::updateCacheAfterStateChange()
{
    if (!m_pageCache) {
        return;
    }

    m_pageCache->setCurrentPage(
        m_state->currentPage(),
        m_state->currentZoom(),
        m_state->currentRotation()
        );
}

void PDFDocumentSession::setPaperEffectEnabled(bool enabled)
{
    if (m_renderer) {
        m_renderer->setPaperEffectEnabled(enabled);

        if (m_pageCache) {
            m_pageCache->clear();
        }

        emit paperEffectChanged(enabled);
    }
}
