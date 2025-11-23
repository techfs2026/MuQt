#include "pdfdocumentsession.h"
#include "mupdfrenderer.h"
#include "pagecachemanager.h"
#include "textcachemanager.h"
#include "pdfviewhandler.h"
#include "pdfcontenthandler.h"
#include "pdfinteractionhandler.h"
#include "pdfdocumentstate.h"
#include "outlineitem.h"
#include "outlineeditor.h"
#include "appconfig.h"
#include <QDebug>
#include <QFileInfo>

PDFDocumentSession::PDFDocumentSession(QObject* parent)
    : QObject(parent)
{
    // 创建核心组件
    m_renderer = std::make_unique<MuPDFRenderer>();

    // 创建缓存管理器
    m_pageCache = std::make_unique<PageCacheManager>(
        AppConfig::instance().maxCacheSize(),
        PageCacheManager::CacheStrategy::NearCurrent
        );

    m_textCache = std::make_unique<TextCacheManager>(m_renderer.get(), this);

    // 创建Handler
    m_viewHandler = std::make_unique<PDFViewHandler>(m_renderer.get(), this);
    m_contentHandler = std::make_unique<PDFContentHandler>(m_renderer.get(), this);
    m_interactionHandler = std::make_unique<PDFInteractionHandler>(
        m_renderer.get(),
        m_textCache.get(),
        this
        );

    // 创建State
    m_state = std::make_unique<PDFDocumentState>(this);

    // 连接信号
    setupConnections();

    qInfo() << "PDFDocumentSession: Initialized";
}

PDFDocumentSession::~PDFDocumentSession()
{
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
        return false;
    }

    m_currentFilePath = filePath;

    // 更新State
    int pageCount = m_contentHandler->pageCount();
    bool isTextPDF = m_contentHandler->isTextPDF(5);

    m_state->setDocumentLoaded(true, filePath, pageCount, isTextPDF);
    m_state->setCurrentPage(0); // 重置到第一页

    qInfo() << "PDFDocumentSession: Document loaded -"
            << QFileInfo(filePath).fileName()
            << "Type:" << (isTextPDF ? "Text PDF" : "Scanned PDF");

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
        m_interactionHandler->clearHoveredLink();
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
    if (m_contentHandler) {
        m_contentHandler->closeDocument();
    }

    m_currentFilePath.clear();

    // 重置State
    m_state->reset();

    qInfo() << "PDFDocumentSession: Document closed";
}

bool PDFDocumentSession::isDocumentLoaded() const
{
    return m_state->isDocumentLoaded();
}

QString PDFDocumentSession::documentPath() const
{
    return m_state->documentPath();
}

int PDFDocumentSession::pageCount() const
{
    return m_state->pageCount();
}

bool PDFDocumentSession::isTextPDF(int samplePages) const
{
    return m_contentHandler ? m_contentHandler->isTextPDF(samplePages) : false;
}

// ==================== 便捷方法 - 导航 ====================

void PDFDocumentSession::goToPage(int pageIndex, bool adjustForDoublePageMode)
{
    if (m_viewHandler) {
        m_viewHandler->requestGoToPage(
            pageIndex,
            adjustForDoublePageMode,
            m_state->currentDisplayMode(),
            m_state->currentPage()
            );
    }
}

void PDFDocumentSession::previousPage()
{
    if (m_viewHandler) {
        m_viewHandler->requestPreviousPage(
            m_state->currentDisplayMode(),
            m_state->isContinuousScroll(),
            m_state->currentPage()
            );
    }
}

void PDFDocumentSession::nextPage()
{
    if (m_viewHandler) {
        m_viewHandler->requestNextPage(
            m_state->currentDisplayMode(),
            m_state->isContinuousScroll(),
            m_state->currentPage(),
            m_state->pageCount()
            );
    }
}

void PDFDocumentSession::firstPage()
{
    if (m_viewHandler) {
        m_viewHandler->requestFirstPage(m_state->currentDisplayMode());
    }
}

void PDFDocumentSession::lastPage()
{
    if (m_viewHandler) {
        m_viewHandler->requestLastPage(
            m_state->currentDisplayMode(),
            m_state->pageCount()
            );
    }
}

// ==================== 便捷方法 - 缩放 ====================

void PDFDocumentSession::setZoom(double zoom)
{
    if (m_viewHandler) {
        m_viewHandler->requestSetZoom(zoom);
    }
}

void PDFDocumentSession::setZoomMode(ZoomMode mode)
{
    if (m_viewHandler) {
        m_viewHandler->requestSetZoomMode(mode);
    }
}

void PDFDocumentSession::zoomIn()
{
    if (m_viewHandler) {
        m_viewHandler->requestZoomIn(m_state->currentZoom());
    }
}

void PDFDocumentSession::zoomOut()
{
    if (m_viewHandler) {
        m_viewHandler->requestZoomOut(m_state->currentZoom());
    }
}

void PDFDocumentSession::actualSize()
{
    if (m_viewHandler) {
        m_viewHandler->requestSetZoom(AppConfig::DEFAULT_ZOOM);
    }
}

void PDFDocumentSession::fitPage()
{
    setZoomMode(ZoomMode::FitPage);
}

void PDFDocumentSession::fitWidth()
{
    setZoomMode(ZoomMode::FitWidth);
}

void PDFDocumentSession::updateZoom(const QSize& viewportSize)
{
    if (m_viewHandler) {
        m_viewHandler->requestUpdateZoom(
            viewportSize,
            m_state->currentZoomMode(),
            m_state->currentZoom(),
            m_state->currentPage(),
            m_state->currentDisplayMode(),
            m_state->currentRotation()
            );
    }
}

// ==================== 便捷方法 - 显示模式 ====================

void PDFDocumentSession::setDisplayMode(PageDisplayMode mode)
{
    if (m_viewHandler) {
        m_viewHandler->requestSetDisplayMode(
            mode,
            m_state->isContinuousScroll(),
            m_state->currentPage()
            );
    }
}

void PDFDocumentSession::setContinuousScroll(bool continuous)
{
    if (m_viewHandler) {
        m_viewHandler->requestSetContinuousScroll(continuous);
    }
}

void PDFDocumentSession::setRotation(int rotation)
{
    if (m_viewHandler) {
        m_viewHandler->requestSetRotation(rotation);
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

OutlineEditor* PDFDocumentSession::outlineEditor() const
{
    return m_contentHandler ? m_contentHandler->outlineEditor() : nullptr;
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

void PDFDocumentSession::startSearch(const QString& query,
                                     bool caseSensitive,
                                     bool wholeWords,
                                     int startPage)
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

SearchResult PDFDocumentSession::findNext()
{
    return m_interactionHandler ? m_interactionHandler->findNext() : SearchResult();
}

SearchResult PDFDocumentSession::findPrevious()
{
    return m_interactionHandler ? m_interactionHandler->findPrevious() : SearchResult();
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
        m_interactionHandler->requestSetLinksVisible(visible);
    }
}

const PDFLink* PDFDocumentSession::hitTestLink(int pageIndex, const QPointF& pagePos, double zoom)
{
    return m_interactionHandler ?
               m_interactionHandler->hitTestLink(pageIndex, pagePos, zoom) :
               nullptr;
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

// ==================== 连续滚动辅助方法 ====================

void PDFDocumentSession::calculatePagePositions()
{
    if (!m_viewHandler) {
        return;
    }

    QVector<int> positions;
    QVector<int> heights;

    bool success = m_viewHandler->calculatePagePositions(
        m_state->currentZoom(),
        m_state->currentRotation(),
        m_state->pageCount(),
        positions,
        heights
        );
}

void PDFDocumentSession::updateCurrentPageFromScroll(int scrollY, int margin)
{
    if (!m_viewHandler) {
        return;
    }

    int newPage = m_viewHandler->calculateCurrentPageFromScroll(
        scrollY,
        margin,
        m_state->pageYPositions()
        );

    if (newPage >= 0 && newPage != m_state->currentPage()) {
        m_state->setCurrentPage(newPage);
    }
}

int PDFDocumentSession::getScrollPositionForPage(int pageIndex, int margin) const
{
    if (!m_viewHandler) {
        return -1;
    }

    return m_viewHandler->getScrollPositionForPage(
        pageIndex,
        margin,
        m_state->pageYPositions()
        );
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
    // ========== ViewHandler信号连接 ==========

    if (m_viewHandler) {
        // 页面导航完成 -> 更新State
        connect(m_viewHandler.get(), &PDFViewHandler::pageNavigationCompleted,
                this, [this](int newPageIndex) {
                    m_state->setCurrentPage(newPageIndex);
                    updateCacheAfterStateChange();
                    if(m_state->isContinuousScroll()) {
                        int targetY = m_viewHandler->getScrollPositionForPage(
                            newPageIndex, AppConfig::PAGE_MARGIN, m_state->pageYPositions());
                        emit scrollToPositionRequested(targetY);
                    }
                });

        // 缩放设置完成 -> 更新State
        connect(m_viewHandler.get(), &PDFViewHandler::zoomSettingCompleted,
                this, [this](double newZoom, ZoomMode newMode) {
                    m_state->setCurrentZoomMode(newMode);

                    // 如果zoom为-1，表示需要重新计算
                    if (newZoom < 0) {
                        // 触发重新计算（由UI调用updateZoom）
                        return;
                    }

                    m_state->setCurrentZoom(newZoom);

                    // 如果是连续滚动模式，需要重新计算页面位置
                    if (m_state->isContinuousScroll()) {
                        calculatePagePositions();
                    }

                    updateCacheAfterStateChange();
                });

        // 显示模式设置完成 -> 更新State
        connect(m_viewHandler.get(), &PDFViewHandler::displayModeSettingCompleted,
                this, [this](PageDisplayMode newMode, int adjustedPage) {
                    // 双页模式自动关闭连续滚动
                    if (newMode == PageDisplayMode::DoublePage && m_state->isContinuousScroll()) {
                        m_state->setContinuousScroll(false);
                    }

                    m_state->setCurrentDisplayMode(newMode);

                    // 双页模式下可能调整了页码
                    if (adjustedPage != m_state->currentPage()) {
                        m_state->setCurrentPage(adjustedPage);
                    }


                });

        // 连续滚动设置完成 -> 更新State
        connect(m_viewHandler.get(), &PDFViewHandler::continuousScrollSettingCompleted,
                this, [this](bool continuous) {
                    qDebug() << "m_state->setContinuousScroll(continuous);";
                    m_state->setContinuousScroll(continuous);
                });

        connect(m_viewHandler.get(), &PDFViewHandler::pagePositionsCalculated,
                this, [this](const QVector<int>& positions,const QVector<int>& heights) {
                    qDebug() << "m_state->setPagePositions(positions, heights);";
                    m_state->setPagePositions(positions, heights);
                });



        // 旋转设置完成 -> 更新State
        connect(m_viewHandler.get(), &PDFViewHandler::rotationSettingCompleted,
                this, [this](int newRotation) {
                    m_state->setCurrentRotation(newRotation);

                    // 旋转变化需要重新计算页面位置
                    if (m_state->isContinuousScroll()) {
                        calculatePagePositions();
                    }

                    updateCacheAfterStateChange();
                });

        // 页面位置计算完成 -> 更新State（已在calculatePagePositions中处理）

        // 滚动位置请求 -> 直接转发
        connect(m_viewHandler.get(), &PDFViewHandler::scrollToPositionRequested,
                this, &PDFDocumentSession::scrollToPositionRequested);
    }

    // ========== ContentHandler信号连接 ==========

    if (m_contentHandler) {
        // 文档事件直接转发（非状态变化）
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

    // ========== InteractionHandler信号连接 ==========

    if (m_interactionHandler) {
        // 搜索事件
        connect(m_interactionHandler.get(), &PDFInteractionHandler::searchProgressUpdated,
                this, &PDFDocumentSession::searchProgressUpdated);

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

        // 链接事件
        connect(m_interactionHandler.get(), &PDFInteractionHandler::linksVisibilityChanged,
                this, [this](bool visible) {
                    m_state->setLinksVisible(visible);
                });

        connect(m_interactionHandler.get(), &PDFInteractionHandler::linkHovered,
                this, &PDFDocumentSession::linkHovered);

        connect(m_interactionHandler.get(), &PDFInteractionHandler::internalLinkRequested,
                this, &PDFDocumentSession::internalLinkRequested);

        connect(m_interactionHandler.get(), &PDFInteractionHandler::externalLinkRequested,
                this, &PDFDocumentSession::externalLinkRequested);

        // 文本选择事件
        connect(m_interactionHandler.get(), &PDFInteractionHandler::textSelectionChanged,
                this, [this](bool hasSelection, const QString& selectedText) {
                    Q_UNUSED(selectedText);
                    m_state->setHasTextSelection(hasSelection);
                    emit textSelectionChanged(hasSelection);
                });

        connect(m_interactionHandler.get(), &PDFInteractionHandler::textCopied,
                this, &PDFDocumentSession::textCopied);
    }

    // ========== State信号转发（State -> Session -> UI）==========

    if (m_state) {
        // 文档状态
        connect(m_state.get(), &PDFDocumentState::documentLoadedChanged,
                this, &PDFDocumentSession::documentLoadedChanged);
        connect(m_state.get(), &PDFDocumentState::documentTypeChanged,
                this, &PDFDocumentSession::documentTypeChanged);

        // 导航状态
        connect(m_state.get(), &PDFDocumentState::currentPageChanged,
                this, &PDFDocumentSession::currentPageChanged);

        // 缩放状态
        connect(m_state.get(), &PDFDocumentState::currentZoomChanged,
                this, &PDFDocumentSession::currentZoomChanged);
        connect(m_state.get(), &PDFDocumentState::currentZoomModeChanged,
                this, &PDFDocumentSession::currentZoomModeChanged);

        // 显示模式状态
        connect(m_state.get(), &PDFDocumentState::currentDisplayModeChanged,
                this, &PDFDocumentSession::currentDisplayModeChanged);
        connect(m_state.get(), &PDFDocumentState::continuousScrollChanged,
                this, &PDFDocumentSession::continuousScrollChanged);
        connect(m_state.get(), &PDFDocumentState::currentRotationChanged,
                this, &PDFDocumentSession::currentRotationChanged);

        // 连续滚动状态
        connect(m_state.get(), &PDFDocumentState::pagePositionsChanged,
                this, &PDFDocumentSession::pagePositionsChanged);

        // 交互状态
        connect(m_state.get(), &PDFDocumentState::linksVisibleChanged,
                this, &PDFDocumentSession::linksVisibleChanged);
        connect(m_state.get(), &PDFDocumentState::searchStateChanged,
                this, &PDFDocumentSession::searchStateChanged);
    }

    // ========== TextCacheManager信号连接 ==========

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

    // 更新页面缓存的当前状态
    m_pageCache->setCurrentPage(
        m_state->currentPage(),
        m_state->currentZoom(),
        m_state->currentRotation()
        );
}
