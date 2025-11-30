#include "pdfdocumenttab.h"
#include "pdfdocumentsession.h"
#include "pdfdocumentstate.h"
#include "pdfpagewidget.h"
#include "navigationpanel.h"
#include "searchwidget.h"
#include "perthreadmupdfrenderer.h"
#include "pagecachemanager.h"
#include "pdfinteractionhandler.h"
#include "textcachemanager.h"
#include "pdfviewhandler.h"
#include "linkmanager.h"
#include "appconfig.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QProgressBar>
#include <QSplitter>
#include <QFileInfo>
#include <QTimer>
#include <QMessageBox>
#include <QMenu>
#include <QApplication>
#include <QToolTip>

PDFDocumentTab::PDFDocumentTab(QWidget* parent)
    : QWidget(parent)
    , m_session(nullptr)
    , m_pageWidget(nullptr)
    , m_navigationPanel(nullptr)
    , m_searchWidget(nullptr)
    , m_splitter(nullptr)
    , m_scrollArea(nullptr)
    , m_textPreloadProgress(nullptr)
    , m_lastClickTime(0)
    , m_clickCount(0)
    , m_isUserScrolling(false)
{
    setupUI();
    setupConnections();
}

PDFDocumentTab::~PDFDocumentTab()
{
}

// ==================== UI设置 ====================

void PDFDocumentTab::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 创建Session
    m_session = new PDFDocumentSession(this);

    // 创建导航面板
    m_navigationPanel = new NavigationPanel(m_session, this);
    m_navigationPanel->setVisible(false);

    // 创建滚动区域
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(false);
    m_scrollArea->setAlignment(Qt::AlignCenter);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    // 创建页面显示组件
    m_pageWidget = new PDFPageWidget(m_session, this);
    m_scrollArea->setWidget(m_pageWidget);

    // 创建搜索工具栏
    m_searchWidget = new SearchWidget(m_session, this);
    m_searchWidget->setVisible(false);

    // 创建进度条
    m_textPreloadProgress = new QProgressBar(this);
    m_textPreloadProgress->setMaximumWidth(200);
    m_textPreloadProgress->setMaximumHeight(20);
    m_textPreloadProgress->setVisible(false);
    m_textPreloadProgress->setTextVisible(true);
    m_textPreloadProgress->setAlignment(Qt::AlignCenter);

    // 组装主布局
    mainLayout->addWidget(m_searchWidget);
    mainLayout->addWidget(m_scrollArea, 1);
    mainLayout->addWidget(m_textPreloadProgress);

    // 设置样式
    m_scrollArea->setStyleSheet("QScrollArea { background-color: #F0F0F0; border: none; }");
}

void PDFDocumentTab::setupConnections()
{
    // ==================== Session状态变化信号 ====================

    connect(m_session, &PDFDocumentSession::documentLoaded,
            this, [this](const QString& path, int pageCount) {
                onDocumentLoaded(path, pageCount);
            });

    connect(m_session, &PDFDocumentSession::documentError,
            this, &PDFDocumentTab::documentError);

    connect(m_session, &PDFDocumentSession::currentPageChanged,
            this, &PDFDocumentTab::onPageChanged);

    connect(m_session, &PDFDocumentSession::currentZoomChanged,
            this, &PDFDocumentTab::onZoomChanged);

    connect(m_session, &PDFDocumentSession::zoomSettingCompleted,
            this, [this](double zoom, ZoomMode mode) {
                if (zoom < 0) {
                    // 需要重新计算zoom
                    QSize viewportSize = m_scrollArea->viewport()->size();
                    m_session->updateZoom(viewportSize);
                } else {
                    onZoomChanged(zoom);
                }
            });

    connect(m_session, &PDFDocumentSession::currentDisplayModeChanged,
            this, &PDFDocumentTab::onDisplayModeChanged);

    connect(m_session, &PDFDocumentSession::continuousScrollChanged,
            this, &PDFDocumentTab::onContinuousScrollChanged);

    connect(m_session, &PDFDocumentSession::pagePositionsChanged,
            this, &PDFDocumentTab::onPagePositionsChanged);

    connect(m_session, &PDFDocumentSession::currentRotationChanged,
            this, [this](int rotation) {
                renderAndUpdatePages();
            });

    connect(m_session, &PDFDocumentSession::scrollToPositionRequested,
            this, [this](int scrollY) {
                m_scrollArea->verticalScrollBar()->setValue(scrollY);
            });

    connect(m_session, &PDFDocumentSession::requestCurrentScrollPosition,
            this, [this]() {
                int scrollY = m_scrollArea->verticalScrollBar()->value();
                m_session->saveViewportState(scrollY);
            });

    connect(m_session, &PDFDocumentSession::textSelectionChanged,
            this, &PDFDocumentTab::onTextSelectionChanged);

    connect(m_session, &PDFDocumentSession::internalLinkRequested,
            this, [this](int targetPage) {
                m_session->goToPage(targetPage);
            });

    connect(m_session, &PDFDocumentSession::textPreloadProgress,
            this, &PDFDocumentTab::onTextPreloadProgress);

    connect(m_session, &PDFDocumentSession::textPreloadCompleted,
            this, &PDFDocumentTab::onTextPreloadCompleted);

    connect(m_session, &PDFDocumentSession::textPreloadCancelled,
            this, [this]() {
                if (m_textPreloadProgress) {
                    m_textPreloadProgress->setVisible(false);
                }
            });

    connect(m_session, &PDFDocumentSession::searchCompleted,
            this, &PDFDocumentTab::onSearchCompleted);

    // ==================== PageWidget用户交互信号 ====================

    connect(m_pageWidget, &PDFPageWidget::pageClicked,
            this, &PDFDocumentTab::onPageClicked);

    connect(m_pageWidget, &PDFPageWidget::mouseMovedOnPage,
            this, &PDFDocumentTab::onMouseMovedOnPage);

    connect(m_pageWidget, &PDFPageWidget::mouseLeftAllPages,
            this, &PDFDocumentTab::onMouseLeftAllPages);

    connect(m_pageWidget, &PDFPageWidget::textSelectionDragging,
            this, &PDFDocumentTab::onTextSelectionDragging);

    connect(m_pageWidget, &PDFPageWidget::textSelectionEnded,
            this, &PDFDocumentTab::onTextSelectionEnded);

    connect(m_pageWidget, &PDFPageWidget::contextMenuRequested,
            this, &PDFDocumentTab::onContextMenuRequested);

    connect(m_pageWidget, &PDFPageWidget::visibleAreaChanged,
            this, &PDFDocumentTab::onVisibleAreaChanged);

    // ==================== 滚动条信号 ====================

    connect(m_scrollArea->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &PDFDocumentTab::onScrollValueChanged);

    // ==================== 搜索相关 ====================

    connect(m_searchWidget, &SearchWidget::closeRequested,
            this, &PDFDocumentTab::hideSearchBar);

    connect(m_searchWidget, &SearchWidget::searchResultNavigated,
            this, [this](const SearchResult& result) {
                // 页面已经通过 Session 跳转了，这里只需要更新高亮
                m_pageWidget->update();
            });

    // ==================== 导航面板 ====================

    connect(m_navigationPanel, &NavigationPanel::pageJumpRequested,
            this, [this](int pageIndex) {
                m_session->goToPage(pageIndex);
            });
}

// ==================== 文档操作 ====================

bool PDFDocumentTab::loadDocument(const QString& filePath, QString* errorMessage)
{
    if (!m_session->loadDocument(filePath, errorMessage)) {
        return false;
    }
    return true;
}

void PDFDocumentTab::closeDocument()
{
    m_session->closeDocument();
}

bool PDFDocumentTab::isDocumentLoaded() const
{
    return m_session && m_session->state()->isDocumentLoaded();
}

QString PDFDocumentTab::documentPath() const
{
    return m_session->documentPath();
}

QString PDFDocumentTab::documentTitle() const
{
    if (documentPath().isEmpty()) {
        return tr("New Tab");
    }
    return QFileInfo(documentPath()).fileName();
}

// ==================== 导航操作 ====================

void PDFDocumentTab::previousPage()
{
    m_session->previousPage();
}

void PDFDocumentTab::nextPage()
{
    m_session->nextPage();
}

void PDFDocumentTab::firstPage()
{
    m_session->firstPage();
}

void PDFDocumentTab::lastPage()
{
    m_session->lastPage();
}

void PDFDocumentTab::goToPage(int pageIndex)
{
    m_session->goToPage(pageIndex);
}

// ==================== 缩放操作 ====================

void PDFDocumentTab::zoomIn()
{
    m_session->zoomIn();
}

void PDFDocumentTab::zoomOut()
{
    m_session->zoomOut();
}

void PDFDocumentTab::actualSize()
{
    m_session->actualSize();
}

void PDFDocumentTab::fitPage()
{
    m_session->fitPage();
    updateScrollBarPolicy();
}

void PDFDocumentTab::fitWidth()
{
    m_session->fitWidth();
    updateScrollBarPolicy();
}

void PDFDocumentTab::setZoom(double zoom)
{
    m_session->setZoom(zoom);
}

// ==================== 视图操作 ====================

void PDFDocumentTab::setDisplayMode(PageDisplayMode mode)
{
    if (mode != m_session->state()->currentDisplayMode()) {
        m_session->setDisplayMode(mode);
    }
}

void PDFDocumentTab::setContinuousScroll(bool continuous)
{
    m_session->setContinuousScroll(continuous);
}

// ==================== 搜索操作 ====================

void PDFDocumentTab::showSearchBar()
{
    if (!m_session->state()->isTextPDF()) {
        QMessageBox::information(this, tr("Search Unavailable"),
                                 tr("This PDF is a scanned document and does not contain searchable text.\n\n"
                                    "To search this document, you would need to use OCR (Optical Character Recognition)."));
        return;
    }

    if (m_session->textCache()->isPreloading()) {
        int progress = m_session->textCache()->computePreloadProgress();

        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            tr("Text Extraction in Progress"),
            tr("Text extraction is in progress (%1%).\n\n"
               "You can search now, but only extracted pages will be searchable.\n\n"
               "Continue with search?").arg(progress),
            QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::No) {
            return;
        }
    }

    m_searchWidget->showAndFocus();
}

void PDFDocumentTab::hideSearchBar()
{
    m_searchWidget->hide();
    m_session->cancelSearch();
    m_pageWidget->clearHighlights();
    m_pageWidget->setFocus();
}

bool PDFDocumentTab::isSearchBarVisible() const
{
    return m_searchWidget && m_searchWidget->isVisible();
}

// ==================== 文本操作 ====================

void PDFDocumentTab::copySelectedText()
{
    if (m_session->state()->hasTextSelection()) {
        m_session->copySelectedText();
    }
}

void PDFDocumentTab::selectAll()
{
    if (m_session->state()->isDocumentLoaded()) {
        m_session->selectAll(m_session->state()->currentPage());
    }
}

// ==================== 链接操作 ====================

void PDFDocumentTab::setLinksVisible(bool visible)
{
    m_session->setLinksVisible(visible);
    m_pageWidget->update();
}

bool PDFDocumentTab::linksVisible() const
{
    return m_session->state()->linksVisible();
}

// ==================== 状态查询 ====================

int PDFDocumentTab::currentPage() const
{
    return m_session->state()->currentPage();
}

int PDFDocumentTab::pageCount() const
{
    return m_session->state()->pageCount();
}

double PDFDocumentTab::zoom() const
{
    return m_session->state()->currentZoom();
}

ZoomMode PDFDocumentTab::zoomMode() const
{
    return m_session->state()->currentZoomMode();
}

PageDisplayMode PDFDocumentTab::displayMode() const
{
    return m_session->state()->currentDisplayMode();
}

bool PDFDocumentTab::isContinuousScroll() const
{
    return m_session->state()->isContinuousScroll();
}

bool PDFDocumentTab::hasTextSelection() const
{
    return m_session->state()->hasTextSelection();
}

bool PDFDocumentTab::isTextPDF() const
{
    return m_session->state()->isTextPDF();
}

QSize PDFDocumentTab::getViewportSize() const
{
    if (m_scrollArea && m_scrollArea->viewport()) {
        return m_scrollArea->viewport()->size();
    }
    return QSize(800, 600); // 默认值
}

void PDFDocumentTab::updateZoom(const QSize& viewportSize)
{
    if (m_session) {
        m_session->updateZoom(viewportSize);
    }
}

void PDFDocumentTab::findNext()
{
    if (m_session) {
        SearchResult result = m_session->findNext();
        if (result.isValid()) {
            // 结果已经通过 Session 的信号处理了
            m_pageWidget->update();
        }
    }
}

void PDFDocumentTab::findPrevious()
{
    if (m_session) {
        SearchResult result = m_session->findPrevious();
        if (result.isValid()) {
            // 结果已经通过 Session 的信号处理了
            m_pageWidget->update();
        }
    }
}

// ==================== Session状态变化响应 ====================

void PDFDocumentTab::onDocumentLoaded(const QString& filePath, int pageCount)
{
    if (m_navigationPanel) {
        m_navigationPanel->loadDocument(pageCount);
    }

    if (m_session->state()->isTextPDF()) {
        m_session->textCache()->startPreload();
    }

    QTimer::singleShot(0, this, [this]() {
        const PDFDocumentState* state = m_session->state();
        if (state->isDocumentLoaded()) {
            ZoomMode mode = state->currentZoomMode();
            if (mode == ZoomMode::FitWidth || mode == ZoomMode::FitPage) {
                QSize viewportSize = m_scrollArea->viewport()->size();
                qDebug() << "onDocumentLoaded 2, viewport:" << viewportSize;
                m_session->updateZoom(viewportSize);
            }
        }
    });

    emit documentLoaded(filePath, pageCount);
}

void PDFDocumentTab::onPageChanged(int pageIndex)
{
    // 更新导航面板
    if (m_navigationPanel) {
        m_navigationPanel->updateCurrentPage(pageIndex);
    }

    // 渲染并更新页面
    renderAndUpdatePages();

    emit pageChanged(pageIndex);
}

void PDFDocumentTab::onZoomChanged(double zoom)
{
    renderAndUpdatePages();
    emit zoomChanged(zoom);
}

void PDFDocumentTab::onDisplayModeChanged(PageDisplayMode mode)
{
    updateScrollBarPolicy();
    m_session->textCache()->clear();

    // 如果是自适应模式，需要重新计算缩放
    if (m_session->state()->currentZoomMode() != ZoomMode::Custom) {
        QSize viewportSize = m_scrollArea->viewport()->size();
        m_session->updateZoom(viewportSize);
    }

    renderAndUpdatePages();
    emit displayModeChanged(mode);
}

void PDFDocumentTab::onContinuousScrollChanged(bool continuous)
{
    updateScrollBarPolicy();

    // 如果是自适应模式，需要重新计算缩放
    if (m_session->state()->currentZoomMode() != ZoomMode::Custom) {
        QSize viewportSize = m_scrollArea->viewport()->size();
        m_session->updateZoom(viewportSize);
    }

    renderAndUpdatePages();
    emit continuousScrollChanged(continuous);
}

void PDFDocumentTab::onPagePositionsChanged(const QVector<int>& positions, const QVector<int>& heights)
{
    QSize targetSize = m_pageWidget->calculateRequiredSize();
    m_pageWidget->resize(targetSize);

    refreshVisiblePages();

    if (m_session->state()->isContinuousScroll()) {
        if (!m_isUserScrolling) {
            int targetY = -1;

            // 优先使用State中保存的恢复位置
            if (m_session->state()->needRestoreViewport()) {
                targetY = m_session->state()->getRestoredScrollPosition(AppConfig::PAGE_MARGIN);
                m_session->clearViewportRestore();
            } else {
                // 默认行为：当前页顶部
                int currentPage = m_session->state()->currentPage();
                targetY = m_session->getScrollPositionForPage(currentPage, AppConfig::PAGE_MARGIN);
            }

            if (targetY >= 0) {
                QTimer::singleShot(0, this, [this, targetY]() {
                    m_scrollArea->verticalScrollBar()->setValue(targetY);
                });
            }
        }
    }
}

void PDFDocumentTab::onTextSelectionChanged(bool hasSelection)
{
    m_pageWidget->update();
    emit textSelectionChanged();
}

void PDFDocumentTab::onTextPreloadProgress(int current, int total)
{
    if (m_textPreloadProgress) {
        m_textPreloadProgress->setVisible(true);
        m_textPreloadProgress->setMaximum(total);
        m_textPreloadProgress->setValue(current);
        m_textPreloadProgress->setFormat(QString("%1/%2").arg(current).arg(total));
    }
}

void PDFDocumentTab::onTextPreloadCompleted()
{
    if (m_textPreloadProgress) {
        m_textPreloadProgress->setVisible(false);
    }
}

void PDFDocumentTab::onSearchCompleted(const QString& query, int totalMatches)
{
    m_pageWidget->update();
    emit searchCompleted(query, totalMatches);
}

// ==================== PageWidget用户交互响应 ====================

void PDFDocumentTab::onPageClicked(int pageIndex, const QPointF& pagePos, Qt::MouseButton button, Qt::KeyboardModifiers modifiers)
{
    if (button != Qt::LeftButton) return;

    const PDFDocumentState* state = m_session->state();
    double zoom = state->currentZoom();

    // 1. 优先检查链接
    if (state->linksVisible()) {
        const PDFLink* link = m_session->hitTestLink(pageIndex, pagePos, zoom);
        if (link) {
            m_session->handleLinkClick(link);
            return;
        }
    }

    // 2. 处理文本PDF的选择
    if (state->isTextPDF()) {
        // Shift扩展选择
        if (modifiers & Qt::ShiftModifier) {
            m_session->extendTextSelection(pageIndex, pagePos, zoom);
            return;
        }

        // 多击检测
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 timeDiff = now - m_lastClickTime;

        const int doubleClickTime = QApplication::doubleClickInterval();

        if (timeDiff < doubleClickTime && (m_lastClickPos - QPoint(pagePos.x(), pagePos.y())).manhattanLength() < 5) {
            m_clickCount++;
        } else {
            m_clickCount = 1;
        }

        m_lastClickTime = now;
        m_lastClickPos = QPoint(pagePos.x(), pagePos.y());

        // 三击：选择整行
        if (m_clickCount >= 3) {
            m_session->selectLine(pageIndex, pagePos, zoom);
            m_clickCount = 0;
        }
        // 双击：选择单词
        else if (m_clickCount == 2) {
            m_session->selectWord(pageIndex, pagePos, zoom);
        }
        // 单击：开始字符级别选择
        else {
            m_session->startTextSelection(pageIndex, pagePos, zoom);
            m_pageWidget->setTextSelectionMode(true);
        }
    }
}

void PDFDocumentTab::onMouseMovedOnPage(int pageIndex, const QPointF& pagePos)
{
    updateCursorForPage(pageIndex, pagePos);
}

void PDFDocumentTab::onMouseLeftAllPages()
{
    m_session->clearHoveredLink();
    QToolTip::hideText();
}

void PDFDocumentTab::onTextSelectionDragging(int pageIndex, const QPointF& pagePos)
{
    const PDFDocumentState* state = m_session->state();
    m_session->updateTextSelection(pageIndex, pagePos, state->currentZoom());
}

void PDFDocumentTab::onTextSelectionEnded()
{
    m_session->endTextSelection();
}

void PDFDocumentTab::onContextMenuRequested(int pageIndex, const QPointF& pagePos, const QPoint& globalPos)
{
    showContextMenu(pageIndex, pagePos, globalPos);
}

void PDFDocumentTab::onVisibleAreaChanged()
{
    refreshVisiblePages();
}

void PDFDocumentTab::onScrollValueChanged(int value)
{
    const PDFDocumentState* state = m_session->state();
    if (state->isContinuousScroll()) {
        m_isUserScrolling = true;

        m_session->updateCurrentPageFromScroll(value, AppConfig::PAGE_MARGIN);
        refreshVisiblePages();

        m_isUserScrolling = false;
    }
}

// ==================== 渲染协调 ====================

void PDFDocumentTab::renderAndUpdatePages()
{
    const PDFDocumentState* state = m_session->state();

    if (!state->isDocumentLoaded()) {
        return;
    }

    if (state->isContinuousScroll()) {
        m_session->calculatePagePositions();
    } else {
        // 单页/双页模式
        QImage img1 = renderPage(state->currentPage());
        QImage img2;

        if (state->currentDisplayMode() == PageDisplayMode::DoublePage) {
            int nextPage = state->currentPage() + 1;
            if (nextPage < state->pageCount()) {
                img2 = renderPage(nextPage);
            }
        }

        m_pageWidget->setDisplayImages(img1, img2);
    }
}

QImage PDFDocumentTab::renderPage(int pageIndex)
{
    if (pageIndex < 0 || pageIndex >= m_session->pageCount()) {
        return QImage();
    }

    const PDFDocumentState* state = m_session->state();
    double zoom = state->currentZoom();
    int rotation = state->currentRotation();

    PageCacheManager* cache = m_session->pageCache();
    PerThreadMuPDFRenderer* renderer = m_session->renderer();

    // 检查缓存
    if (cache->contains(pageIndex, zoom, rotation)) {
        return cache->getPage(pageIndex, zoom, rotation);
    }

    // 渲染新页面
    auto result = renderer->renderPage(pageIndex, zoom, rotation);
    if (result.success) {
        cache->addPage(pageIndex, zoom, rotation, result.image);
        return result.image;
    }

    return QImage();
}

void PDFDocumentTab::refreshVisiblePages()
{
    const PDFDocumentState* state = m_session->state();

    if (!state->isContinuousScroll()) {
        return;
    }

    if (!m_scrollArea || !m_scrollArea->viewport()) {
        return;
    }

    int scrollY = m_scrollArea->verticalScrollBar()->value();
    QRect visibleRect(0, scrollY, m_scrollArea->viewport()->width(), m_scrollArea->viewport()->height());

    // 使用Session的ViewHandler获取可见页面
    QSet<int> visiblePages = m_session->viewHandler()->getVisiblePages(
        visibleRect,
        AppConfig::instance().preloadMargin(),
        AppConfig::PAGE_MARGIN,
        state->pageYPositions(),
        state->pageHeights()
        );

    // 标记可见页面
    PageCacheManager* cache = m_session->pageCache();
    cache->markVisiblePages(visiblePages);

    // 渲染可见页面
    double zoom = state->currentZoom();
    int rotation = state->currentRotation();

    bool anyRendered = false;
    for (int pageIndex : visiblePages) {
        if (!cache->contains(pageIndex, zoom, rotation)) {
            renderPage(pageIndex);
            anyRendered = true;
        }
    }

    if (anyRendered) {
        m_pageWidget->update();
    }
}

// ==================== 辅助方法 ====================

void PDFDocumentTab::updateScrollBarPolicy()
{
    const PDFDocumentState* state = m_session->state();

    if (!state->isDocumentLoaded()) {
        return;
    }

    bool continuous = state->isContinuousScroll();
    ZoomMode zoomMode = state->currentZoomMode();

    if (continuous) {
        m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    } else if (zoomMode == ZoomMode::FitPage) {
        m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    } else {
        m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }
}

void PDFDocumentTab::updateCursorForPage(int pageIndex, const QPointF& pagePos)
{
    const PDFDocumentState* state = m_session->state();
    double zoom = state->currentZoom();

    // 检查链接
    if (state->linksVisible()) {
        const PDFLink* link = m_session->hitTestLink(pageIndex, pagePos, zoom);
        if (link) {
            m_pageWidget->setCursor(Qt::PointingHandCursor);

            QString tooltip;
            if (link->isInternal()) {
                tooltip = tr("Go to page %1").arg(link->targetPage + 1);
            } else if (link->isExternal()) {
                tooltip = tr("Open: %1").arg(link->uri);
            }
            QToolTip::showText(QCursor::pos(), tooltip, m_pageWidget);
            return;
        }
    }

    QToolTip::hideText();

    // 设置默认光标
    if (state->isTextPDF()) {
        m_pageWidget->setCursor(Qt::IBeamCursor);
    } else {
        m_pageWidget->setCursor(Qt::ArrowCursor);
    }
}

void PDFDocumentTab::showContextMenu(int pageIndex, const QPointF& pagePos, const QPoint& globalPos)
{
    const PDFDocumentState* state = m_session->state();

    if (!state->isDocumentLoaded()) {
        return;
    }

    QMenu menu(this);

    // 如果有选中的文本
    if (state->hasTextSelection()) {
        QAction* copyAction = menu.addAction(tr("Copy"));
        copyAction->setShortcut(QKeySequence::Copy);
        connect(copyAction, &QAction::triggered, this, &PDFDocumentTab::copySelectedText);

        menu.addSeparator();
    }

    // 如果是文本PDF
    if (state->isTextPDF()) {
        if (!state->hasTextSelection()) {
            double zoom = state->currentZoom();

            QAction* selectWordAction = menu.addAction(tr("Select Word"));
            connect(selectWordAction, &QAction::triggered, this, [=]() {
                m_session->selectWord(pageIndex, pagePos, zoom);
            });

            QAction* selectLineAction = menu.addAction(tr("Select Line"));
            connect(selectLineAction, &QAction::triggered, this, [=]() {
                m_session->selectLine(pageIndex, pagePos, zoom);
            });

            menu.addSeparator();
        }

        QAction* selectAllAction = menu.addAction(tr("Select All"));
        selectAllAction->setShortcut(QKeySequence::SelectAll);
        connect(selectAllAction, &QAction::triggered, this, &PDFDocumentTab::selectAll);
    }

    if (!menu.isEmpty()) {
        menu.exec(globalPos);
    }
}
