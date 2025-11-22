#include "pdfdocumentsession.h"
#include "mupdfrenderer.h"
#include "pagecachemanager.h"
#include "textcachemanager.h"
#include "pdfviewhandler.h"
#include "pdfcontenthandler.h"
#include "pdfinteractionhandler.h"
#include "outlineitem.h"
#include "outlineeditor.h"
#include "searchmanager.h"
#include "linkmanager.h"
#include "textselector.h"
#include "datastructure.h"
#include "appconfig.h"
#include <QDebug>
#include <QFileInfo>

PDFDocumentSession::PDFDocumentSession(QObject* parent)
    : QObject(parent)
    , m_isTextPDF(false)
{
    // 创建核心组件
    m_renderer = std::make_unique<MuPDFRenderer>();

    // 创建缓存管理器(Session级别)
    m_pageCache = std::make_unique<PageCacheManager>(
        AppConfig::instance().maxCacheSize(),
        PageCacheManager::CacheStrategy::NearCurrent
        );

    m_textCache = std::make_unique<TextCacheManager>(m_renderer.get(), this);

    // 创建Handler(依赖注入)
    m_viewHandler = std::make_unique<PDFViewHandler>(m_renderer.get(), this);
    m_contentHandler = std::make_unique<PDFContentHandler>(m_renderer.get(), this);
    m_interactionHandler = std::make_unique<PDFInteractionHandler>(
        m_renderer.get(),
        m_textCache.get(),
        this
        );

    // 连接信号
    setupConnections();

    qInfo() << "PDFDocumentSession: Initialized";
}

PDFDocumentSession::~PDFDocumentSession()
{
    cleanupResources();
    qInfo() << "PDFDocumentSession: Destroyed";
}

// ==================== 文档生命周期 ====================

bool PDFDocumentSession::loadDocument(const QString& filePath, QString* errorMessage)
{
    if (filePath.isEmpty()) {
        if (errorMessage) *errorMessage = tr("Empty file path");
        return false;
    }

    // 关闭当前文档
    if (isDocumentLoaded()) {
        closeDocument();
    }

    // 使用ContentHandler加载
    QString error;
    if (!m_contentHandler->loadDocument(filePath, &error)) {
        if (errorMessage) *errorMessage = error;
        emit documentError(error);
        return false;
    }

    m_currentFilePath = filePath;

    // 检测PDF类型
    m_isTextPDF = m_contentHandler->isTextPDF(5);

    qInfo() << "PDFDocumentSession: Document loaded -"
            << QFileInfo(filePath).fileName()
            << "Type:" << (m_isTextPDF ? "Text PDF" : "Scanned PDF");

    // 信号已由ContentHandler发出
    return true;
}

void PDFDocumentSession::closeDocument()
{
    if (!isDocumentLoaded()) {
        return;
    }

    // 取消所有正在进行的操作
    if (m_interactionHandler) {
        m_interactionHandler->cancelSearch();
        m_interactionHandler->clearTextSelection();
    }

    if (m_textCache) {
        m_textCache->cancelPreload();
    }

    if (m_contentHandler) {
        m_contentHandler->cancelThumbnailLoading();
    }

    // 清空缓存
    if (m_pageCache) {
        m_pageCache->clear();
    }

    if (m_textCache) {
        m_textCache->clear();
    }

    // 关闭文档
    m_contentHandler->closeDocument();

    m_currentFilePath.clear();
    m_isTextPDF = false;

    qInfo() << "PDFDocumentSession: Document closed";

    // 信号已由ContentHandler发出
}

bool PDFDocumentSession::isDocumentLoaded() const
{
    return m_contentHandler && m_contentHandler->isDocumentLoaded();
}

QString PDFDocumentSession::documentPath() const
{
    return m_currentFilePath;
}

int PDFDocumentSession::pageCount() const
{
    return m_contentHandler ? m_contentHandler->pageCount() : 0;
}

bool PDFDocumentSession::isTextPDF(int samplePages) const
{
    return m_contentHandler ? m_contentHandler->isTextPDF(samplePages) : false;
}

// ==================== 大纲编辑器访问 ====================

OutlineEditor* PDFDocumentSession::outlineEditor() const
{
    return m_contentHandler ? m_contentHandler->outlineEditor() : nullptr;
}

// ==================== 便捷方法 - 导航 ====================

int PDFDocumentSession::currentPage() const
{
    return m_viewHandler ? m_viewHandler->currentPage() : 0;
}

void PDFDocumentSession::setCurrentPage(int pageIndex, bool adjustForDoublePageMode)
{
    if (m_viewHandler) {
        m_viewHandler->setCurrentPage(pageIndex, adjustForDoublePageMode);
    }
}

void PDFDocumentSession::previousPage()
{
    if (m_viewHandler) {
        m_viewHandler->previousPage();
    }
}

void PDFDocumentSession::nextPage()
{
    if (m_viewHandler) {
        m_viewHandler->nextPage();
    }
}

void PDFDocumentSession::firstPage()
{
    if (m_viewHandler) {
        m_viewHandler->firstPage();
    }
}

void PDFDocumentSession::lastPage()
{
    if (m_viewHandler) {
        m_viewHandler->lastPage();
    }
}

// ==================== 便捷方法 - 缩放 ====================

double PDFDocumentSession::zoom() const
{
    return m_viewHandler ? m_viewHandler->zoom() : 1.0;
}

void PDFDocumentSession::setZoom(double zoom)
{
    if (m_viewHandler) {
        m_viewHandler->setZoom(zoom);
    }
}

ZoomMode PDFDocumentSession::zoomMode() const
{
    return m_viewHandler ? m_viewHandler->zoomMode() : ZoomMode::Custom;
}

void PDFDocumentSession::setZoomMode(ZoomMode mode)
{
    if (m_viewHandler) {
        m_viewHandler->setZoomMode(mode);
    }
}

void PDFDocumentSession::zoomIn()
{
    if (m_viewHandler) {
        m_viewHandler->zoomIn();
    }
}

void PDFDocumentSession::zoomOut()
{
    if (m_viewHandler) {
        m_viewHandler->zoomOut();
    }
}

void PDFDocumentSession::actualSize()
{
    if (m_viewHandler) {
        m_viewHandler->setZoom(AppConfig::DEFAULT_ZOOM);
    }
}

void PDFDocumentSession::fitPage()
{
    if (m_viewHandler) {
        m_viewHandler->setZoomMode(ZoomMode::FitPage);
    }
}

void PDFDocumentSession::fitWidth()
{
    if (m_viewHandler) {
        m_viewHandler->setZoomMode(ZoomMode::FitWidth);
    }
}

void PDFDocumentSession::updateZoom(const QSize& viewportSize)
{
    if (m_viewHandler) {
        m_viewHandler->updateZoom(viewportSize);
    }
}

// ==================== 便捷方法 - 显示模式 ====================

PageDisplayMode PDFDocumentSession::displayMode() const
{
    return m_viewHandler ? m_viewHandler->displayMode() : PageDisplayMode::SinglePage;
}

void PDFDocumentSession::setDisplayMode(PageDisplayMode mode)
{
    if (m_viewHandler) {
        m_viewHandler->setDisplayMode(mode);
    }
}

bool PDFDocumentSession::isContinuousScroll() const
{
    return m_viewHandler ? m_viewHandler->isContinuousScroll() : false;
}

void PDFDocumentSession::setContinuousScroll(bool continuous)
{
    if (m_viewHandler) {
        m_viewHandler->setContinuousScroll(continuous);
    }
}

// ==================== 便捷方法 - 内容管理 ====================

bool PDFDocumentSession::loadOutline()
{
    return m_contentHandler ? m_contentHandler->loadOutline() : false;
}

OutlineItem* PDFDocumentSession::outlineRoot() const
{
    return m_contentHandler ? m_contentHandler->outlineRoot() : nullptr;
}

void PDFDocumentSession::startLoadThumbnails(int thumbnailWidth)
{
    if (m_contentHandler) {
        m_contentHandler->startLoadThumbnails(thumbnailWidth);
    }
}

void PDFDocumentSession::cancelThumbnailLoading()
{
    if (m_contentHandler) {
        m_contentHandler->cancelThumbnailLoading();
    }
}

QImage PDFDocumentSession::getThumbnail(int pageIndex) const
{
    return m_contentHandler ? m_contentHandler->getThumbnail(pageIndex) : QImage();
}

// ==================== 便捷方法 - 搜索 ====================

void PDFDocumentSession::startSearch(const QString& query, bool caseSensitive,
                                     bool wholeWords, int startPage)
{
    if (m_interactionHandler) {
        m_interactionHandler->startSearch(query, caseSensitive, wholeWords, startPage);
    }
}

void PDFDocumentSession::cancelSearch()
{
    if (m_interactionHandler) {
        m_interactionHandler->cancelSearch();
    }
}

bool PDFDocumentSession::isSearching() const
{
    return m_interactionHandler ? m_interactionHandler->isSearching() : false;
}

SearchResult PDFDocumentSession::findNext()
{
    return m_interactionHandler ? m_interactionHandler->findNext() : SearchResult();
}

SearchResult PDFDocumentSession::findPrevious()
{
    return m_interactionHandler ? m_interactionHandler->findPrevious() : SearchResult();
}

int PDFDocumentSession::totalSearchMatches() const
{
    return m_interactionHandler ? m_interactionHandler->totalSearchMatches() : 0;
}

int PDFDocumentSession::currentSearchMatchIndex() const
{
    return m_interactionHandler ? m_interactionHandler->currentSearchMatchIndex() : -1;
}

// ==================== 便捷方法 - 文本选择 ====================

void PDFDocumentSession::startTextSelection(int pageIndex, const QPointF& pagePos, double zoom)
{
    if (m_interactionHandler) {
        m_interactionHandler->startTextSelection(pageIndex, pagePos, zoom);
    }
}

void PDFDocumentSession::updateTextSelection(int pageIndex, const QPointF& pagePos, double zoom)
{
    if (m_interactionHandler) {
        m_interactionHandler->updateTextSelection(pageIndex, pagePos, zoom);
    }
}

void PDFDocumentSession::extendTextSelection(int pageIndex, const QPointF& pagePos, double zoom)
{
    if (m_interactionHandler) {
        m_interactionHandler->extendTextSelection(pageIndex, pagePos, zoom);
    }
}

void PDFDocumentSession::endTextSelection()
{
    if (m_interactionHandler) {
        m_interactionHandler->endTextSelection();
    }
}

void PDFDocumentSession::clearTextSelection()
{
    if (m_interactionHandler) {
        m_interactionHandler->clearTextSelection();
    }
}

void PDFDocumentSession::selectWord(int pageIndex, const QPointF& pagePos, double zoom)
{
    if (m_interactionHandler) {
        m_interactionHandler->selectWord(pageIndex, pagePos, zoom);
    }
}

void PDFDocumentSession::selectLine(int pageIndex, const QPointF& pagePos, double zoom)
{
    if (m_interactionHandler) {
        m_interactionHandler->selectLine(pageIndex, pagePos, zoom);
    }
}

void PDFDocumentSession::selectAll(int pageIndex)
{
    if (m_interactionHandler) {
        m_interactionHandler->selectAll(pageIndex);
    }
}

bool PDFDocumentSession::hasTextSelection() const
{
    return m_interactionHandler ? m_interactionHandler->hasTextSelection() : false;
}

QString PDFDocumentSession::selectedText() const
{
    return m_interactionHandler ? m_interactionHandler->selectedText() : QString();
}

void PDFDocumentSession::copySelectedText()
{
    if (m_interactionHandler) {
        m_interactionHandler->copySelectedText();
    }
}

// ==================== 便捷方法 - 链接 ====================

void PDFDocumentSession::setLinksVisible(bool visible)
{
    if (m_interactionHandler) {
        m_interactionHandler->setLinksVisible(visible);
    }
}

bool PDFDocumentSession::linksVisible() const
{
    return m_interactionHandler ? m_interactionHandler->linksVisible() : false;
}

const PDFLink* PDFDocumentSession::hitTestLink(int pageIndex, const QPointF& pagePos, double zoom)
{
    return m_interactionHandler ? m_interactionHandler->hitTestLink(pageIndex, pagePos, zoom) : nullptr;
}

void PDFDocumentSession::clearHoveredLink()
{
    if (m_interactionHandler) {
        m_interactionHandler->clearHoveredLink();
    }
}

bool PDFDocumentSession::handleLinkClick(const PDFLink* link)
{
    return m_interactionHandler ? m_interactionHandler->handleLinkClick(link) : false;
}

// ==================== 统计信息 ====================

QString PDFDocumentSession::getCacheStatistics() const
{
    return m_pageCache ? m_pageCache->getStatistics() : QString();
}

QString PDFDocumentSession::getTextCacheStatistics() const
{
    return m_textCache ? m_textCache->getStatistics() : QString();
}

// ==================== 私有方法 ====================

void PDFDocumentSession::setupConnections()
{
    // --- ViewHandler信号 ---
    if (m_viewHandler) {
        connect(m_viewHandler.get(), &PDFViewHandler::pageChanged,
                this, &PDFDocumentSession::pageChanged);
        connect(m_viewHandler.get(), &PDFViewHandler::zoomChanged,
                this, &PDFDocumentSession::zoomChanged);
        connect(m_viewHandler.get(), &PDFViewHandler::zoomModeChanged,
                this, &PDFDocumentSession::zoomModeChanged);
        connect(m_viewHandler.get(), &PDFViewHandler::displayModeChanged,
                this, &PDFDocumentSession::displayModeChanged);
        connect(m_viewHandler.get(), &PDFViewHandler::continuousScrollChanged,
                this, &PDFDocumentSession::continuousScrollChanged);

        // 当ViewHandler状态变化时,更新缓存管理器
        connect(m_viewHandler.get(), &PDFViewHandler::pageChanged,
                this, [this](int pageIndex) {
                    if (m_pageCache) {
                        QSize dummySize(800, 600); // TODO: 从哪里获取viewport size?
                        double actualZoom = m_viewHandler->calculateActualZoom(dummySize);
                        m_pageCache->setCurrentPage(pageIndex, actualZoom,
                                                    m_viewHandler->rotation());
                    }
                });
    }

    // --- ContentHandler信号 ---
    if (m_contentHandler) {
        connect(m_contentHandler.get(), &PDFContentHandler::documentLoaded,
                this, &PDFDocumentSession::documentLoaded);
        connect(m_contentHandler.get(), &PDFContentHandler::documentClosed,
                this, &PDFDocumentSession::documentClosed);
        connect(m_contentHandler.get(), &PDFContentHandler::documentError,
                this, &PDFDocumentSession::documentError);

        connect(m_contentHandler.get(), &PDFContentHandler::outlineLoaded,
                this, &PDFDocumentSession::outlineLoaded);

        connect(m_contentHandler.get(), &PDFContentHandler::thumbnailLoadStarted,
                this, &PDFDocumentSession::thumbnailLoadStarted);
        connect(m_contentHandler.get(), &PDFContentHandler::thumbnailLoadProgress,
                this, &PDFDocumentSession::thumbnailLoadProgress);
        connect(m_contentHandler.get(), &PDFContentHandler::thumbnailReady,
                this, &PDFDocumentSession::thumbnailReady);
        connect(m_contentHandler.get(), &PDFContentHandler::thumbnailLoadCompleted,
                this, &PDFDocumentSession::thumbnailLoadCompleted);
    }

    // --- InteractionHandler信号 ---
    if (m_interactionHandler) {
        connect(m_interactionHandler.get(), &PDFInteractionHandler::searchProgress,
                this, &PDFDocumentSession::searchProgress);
        connect(m_interactionHandler.get(), &PDFInteractionHandler::searchCompleted,
                this, &PDFDocumentSession::searchCompleted);
        connect(m_interactionHandler.get(), &PDFInteractionHandler::searchCancelled,
                this, &PDFDocumentSession::searchCancelled);

        connect(m_interactionHandler.get(), &PDFInteractionHandler::textSelectionChanged,
                this, &PDFDocumentSession::textSelectionChanged);
        connect(m_interactionHandler.get(), &PDFInteractionHandler::textCopied,
                this, &PDFDocumentSession::textCopied);

        connect(m_interactionHandler.get(), &PDFInteractionHandler::linkHovered,
                this, &PDFDocumentSession::linkHovered);
        connect(m_interactionHandler.get(), &PDFInteractionHandler::internalLinkRequested,
                this, &PDFDocumentSession::internalLinkRequested);
        connect(m_interactionHandler.get(), &PDFInteractionHandler::externalLinkRequested,
                this, &PDFDocumentSession::externalLinkRequested);
    }

    // --- TextCacheManager信号 ---
    if (m_textCache) {
        connect(m_textCache.get(), &TextCacheManager::preloadProgress,
                this, &PDFDocumentSession::textPreloadProgress);
        connect(m_textCache.get(), &TextCacheManager::preloadCompleted,
                this, &PDFDocumentSession::textPreloadCompleted);
        connect(m_textCache.get(), &TextCacheManager::preloadCancelled,
                this, &PDFDocumentSession::textPreloadCancelled);
    }
}

void PDFDocumentSession::cleanupResources()
{
    closeDocument();
}
