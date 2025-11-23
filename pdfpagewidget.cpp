#include "pdfpagewidget.h"
#include "pdfdocumentsession.h"
#include "pdfdocumentstate.h"
#include "mupdfrenderer.h"
#include "pagecachemanager.h"
#include "pdfviewhandler.h"
#include "pdfinteractionhandler.h"
#include "searchmanager.h"
#include "linkmanager.h"
#include "textselector.h"
#include "appconfig.h"

#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QToolTip>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QMouseEvent>
#include <QDebug>
#include <QContextMenuEvent>
#include <QMenu>
#include <QTimer>
#include <QDateTime>
#include <QApplication>

PDFPageWidget::PDFPageWidget(PDFDocumentSession* session, QWidget* parent)
    : QWidget(parent)
    , m_session(session)
    , m_renderer(nullptr)
    , m_cacheManager(nullptr)
    , m_interactionHandler(nullptr)
    , m_isTextSelecting(false)
    , m_clickCount(0)
    , m_lastClickTime(0)
{
    if (!m_session) {
        qCritical() << "PDFPageWidget: session is null!";
        return;
    }

    // 从Session获取组件引用(不转移所有权)
    m_renderer = m_session->renderer();
    m_cacheManager = m_session->pageCache();
    m_interactionHandler = m_session->interactionHandler();

    // 设置背景色
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, AppConfig::instance().backgroundColor());
    setPalette(pal);

    // 设置最小尺寸
    setMinimumSize(200, 200);

    // 启用鼠标追踪
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    // 连接Session状态变化信号
    setupConnections();
}

PDFPageWidget::~PDFPageWidget()
{
}

// ========== 连接信号 ==========

void PDFPageWidget::setupConnections()
{
    // ========== Session状态变化信号 ==========

    // 缩放变化
    connect(m_session, &PDFDocumentSession::currentZoomChanged,
            this, &PDFPageWidget::onZoomChanged);

    // 旋转变化
    connect(m_session, &PDFDocumentSession::currentRotationChanged,
            this, [this](int rotation) {
                renderCurrentPage();
            });

    // 滚动位置请求
    connect(m_session, &PDFDocumentSession::scrollToPositionRequested,
            this, [this](int scrollY) {
                QScrollArea* scrollArea = getScrollArea();
                if (scrollArea) {
                    scrollArea->verticalScrollBar()->setValue(scrollY);
                }
            });

    // 文本选择变化
    connect(m_session, &PDFDocumentSession::textSelectionChanged,
            this, [this](bool hasSelection) {
                update();
            });
}

// ========== 导航方法 (委托给Session) ==========

void PDFPageWidget::setCurrentPage(int pageIndex)
{
    m_session->goToPage(pageIndex);
}

void PDFPageWidget::previousPage()
{
    m_session->previousPage();
}

void PDFPageWidget::nextPage()
{
    m_session->nextPage();
}

int PDFPageWidget::currentPage() const
{
    return m_session->state()->currentPage();
}

// ========== 缩放方法 (委托给Session) ==========

void PDFPageWidget::setZoom(double zoom)
{
    m_session->setZoom(zoom);
}

void PDFPageWidget::setZoomMode(ZoomMode mode)
{
    m_session->setZoomMode(mode);
    QSize viewportSize = getViewportSize();
    m_session->updateZoom(viewportSize);
}

void PDFPageWidget::zoomIn()
{
    m_session->zoomIn();
}

void PDFPageWidget::zoomOut()
{
    m_session->zoomOut();
}

double PDFPageWidget::zoom() const
{
    return m_session->state()->currentZoom();
}

ZoomMode PDFPageWidget::zoomMode() const
{
    return m_session->state()->currentZoomMode();
}

void PDFPageWidget::updateZoom()
{
    QSize viewportSize = getViewportSize();
    m_session->updateZoom(viewportSize);
}

// ========== 显示模式方法 (委托给Session) ==========

void PDFPageWidget::setDisplayMode(PageDisplayMode mode)
{
    m_session->setDisplayMode(mode);
}

void PDFPageWidget::setContinuousScroll(bool continuous)
{
    m_session->setContinuousScroll(continuous);
}

PageDisplayMode PDFPageWidget::displayMode() const
{
    return m_session->state()->currentDisplayMode();
}

bool PDFPageWidget::isContinuousScroll() const
{
    return m_session->state()->isContinuousScroll();
}

// ========== 旋转方法 (委托给Session) ==========

void PDFPageWidget::setRotation(int rotation)
{
    m_session->setRotation(rotation);
}

// ========== 其他方法 ==========

void PDFPageWidget::refresh()
{
    renderCurrentPage();
}

void PDFPageWidget::setLinksVisible(bool enabled)
{
    m_session->setLinksVisible(enabled);
    update();
}

void PDFPageWidget::copySelectedText()
{
    m_session->copySelectedText();
}

void PDFPageWidget::selectAll()
{
    if (m_session->state()->isDocumentLoaded()) {
        m_session->selectAll(m_session->state()->currentPage());
    }
}

QString PDFPageWidget::getCacheStatistics() const
{
    if (!m_cacheManager) {
        return "Cache: Not initialized";
    }
    return m_cacheManager->getStatistics();
}

// ========== 状态变化响应 ==========

void PDFPageWidget::onPageChanged(int pageIndex)
{
    // 更新缓存管理器
    const PDFDocumentState* state = m_session->state();
    m_cacheManager->setCurrentPage(
        pageIndex,
        state->currentZoom(),
        state->currentRotation()
        );

    // 连续滚动模式:滚动到目标位置(由Session发出scrollToPositionRequested信号)
    // 非连续滚动模式:触发渲染
    if (!state->isContinuousScroll()) {
        renderCurrentPage();
    }
}

void PDFPageWidget::onZoomChanged(double zoom)
{
    renderCurrentPage();
    emit zoomChanged(zoom);
}

// ========== 尺寸计算 ==========

QSize PDFPageWidget::sizeHint() const
{
    const PDFDocumentState* state = m_session->state();

    if (m_currentImage.isNull() && state->pageYPositions().isEmpty()) {
        QSize viewportSize = getViewportSize();
        if (viewportSize.isValid() && viewportSize.width() > 0 && viewportSize.height() > 0) {
            return viewportSize;
        }
        return QSize(800, 600);
    }

    const int margin = AppConfig::PAGE_MARGIN;

    // 连续滚动模式:基于预计算的位置
    if (state->isContinuousScroll() && !state->pageYPositions().isEmpty()) {
        int maxWidth = 0;

        if (m_renderer && m_renderer->isDocumentLoaded()) {
            QSizeF pageSize = m_renderer->pageSize(0);
            if (state->currentRotation() == 90 || state->currentRotation() == 270) {
                pageSize.transpose();
            }

            double actualZoom = state->currentZoom();
            maxWidth = qRound(pageSize.width() * actualZoom);
        }

        const QVector<int>& positions = state->pageYPositions();
        const QVector<int>& heights = state->pageHeights();
        int totalHeight = positions.last() + heights.last();
        return QSize(maxWidth + 2 * margin, totalHeight + 2 * margin);
    }

    // 单页/双页模式
    int contentWidth = m_currentImage.width();
    int contentHeight = m_currentImage.height();

    if (state->currentDisplayMode() == PageDisplayMode::DoublePage &&
        !m_secondImage.isNull()) {
        contentWidth = m_currentImage.width() + m_secondImage.width() +
                       AppConfig::DOUBLE_PAGE_SPACING;
        contentHeight = qMax(m_currentImage.height(), m_secondImage.height());
    }

    return QSize(contentWidth + 2 * margin, contentHeight + 2 * margin);
}

// ========== 渲染相关 ==========

void PDFPageWidget::renderCurrentPage()
{
    if (!m_renderer || !m_renderer->isDocumentLoaded()) {
        m_currentImage = QImage();
        m_secondImage = QImage();
        m_cacheManager->clear();

        // 未加载文档:让控件自动填满 viewport
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        updateGeometry();
        update();
        return;
    }

    // 从State获取状态
    const PDFDocumentState* state = m_session->state();
    int currentPage = state->currentPage();
    double actualZoom = state->currentZoom();
    int rotation = state->currentRotation();
    PageDisplayMode displayMode = state->currentDisplayMode();
    bool continuousScroll = state->isContinuousScroll();

    // 连续滚动模式
    if (continuousScroll) {
        m_cacheManager->clear();
        qDebug() << "m_session->calculatePagePositions();";
        // 计算页面位置(通过Session)
        m_session->calculatePagePositions();

        // 位置计算完成后会触发pagePositionsChanged信号
        // 在该信号的槽函数中会调用refreshVisiblePages()
    }
    // 单页/双页模式
    else {
        m_currentImage = renderSinglePage(currentPage, actualZoom);

        if (displayMode == PageDisplayMode::DoublePage) {
            int nextPage = currentPage + 1;
            if (nextPage < m_renderer->pageCount()) {
                m_secondImage = renderSinglePage(nextPage, actualZoom);
            } else {
                m_secondImage = QImage();
            }
        } else {
            m_secondImage = QImage();
        }

        QSize targetSize = sizeHint();
        resize(targetSize);
        update();
    }
}

QImage PDFPageWidget::renderSinglePage(int pageIndex, double zoom)
{
    if (!m_renderer || pageIndex < 0 || pageIndex >= m_renderer->pageCount()) {
        return QImage();
    }

    const PDFDocumentState* state = m_session->state();
    int rotation = state->currentRotation();

    // 检查缓存
    if (m_cacheManager->contains(pageIndex, zoom, rotation)) {
        QImage cached = m_cacheManager->getPage(pageIndex, zoom, rotation);
        if (AppConfig::instance().debugMode()) {
            qDebug() << "Cache HIT: Page" << pageIndex
                     << "zoom:" << zoom << "rotation:" << rotation;
        }
        return cached;
    }

    // 缓存未命中,渲染新页面
    if (AppConfig::instance().debugMode()) {
        qDebug() << "Cache MISS: Page" << pageIndex
                 << "zoom:" << zoom << "rotation:" << rotation;
    }

    auto result = m_renderer->renderPage(pageIndex, zoom, rotation);

    if (result.success) {
        m_cacheManager->addPage(pageIndex, zoom, rotation, result.image);
        return result.image;
    } else {
        if (AppConfig::instance().debugMode()) {
            qWarning() << "Failed to render page" << pageIndex
                       << ":" << result.errorMessage;
        }
        return QImage();
    }
}

// ========== 连续滚动相关 ==========

void PDFPageWidget::updateCurrentPageFromScroll(int scrollY)
{
    const PDFDocumentState* state = m_session->state();

    if (!state->isContinuousScroll()) {
        return;
    }

    // 委托给Session更新当前页
    m_session->updateCurrentPageFromScroll(scrollY, AppConfig::PAGE_MARGIN);

    // Session会更新State，State会发出currentPageChanged信号
    // 然后触发refreshVisiblePages()
    refreshVisiblePages();
}

void PDFPageWidget::refreshVisiblePages()
{
    const PDFDocumentState* state = m_session->state();

    if (!state->isContinuousScroll()) {
        return;
    }

    QScrollArea* scrollArea = getScrollArea();
    if (!scrollArea || !scrollArea->viewport()) {
        return;
    }

    int scrollY = scrollArea->verticalScrollBar()->value();
    QRect visibleRect(0, scrollY,
                      scrollArea->viewport()->width(),
                      scrollArea->viewport()->height());

    // 使用Session的ViewHandler获取可见页面
    QSet<int> visiblePages = m_session->viewHandler()->getVisiblePages(
        visibleRect,
        AppConfig::instance().preloadMargin(),
        AppConfig::PAGE_MARGIN,
        state->pageYPositions(),
        state->pageHeights()
        );

    // 标记可见页面
    m_cacheManager->markVisiblePages(visiblePages);

    // 渲染可见页面
    double actualZoom = state->currentZoom();
    int rotation = state->currentRotation();

    for (int pageIndex : visiblePages) {
        if (!m_cacheManager->contains(pageIndex, actualZoom, rotation)) {
            renderSinglePage(pageIndex, actualZoom);
        }
    }

    update();
}

// ========== 绘制相关 ==========

void PDFPageWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const PDFDocumentState* state = m_session->state();

    // 连续滚动模式
    if (state->isContinuousScroll() && !state->pageYPositions().isEmpty()) {
        paintContinuousMode(painter, event->rect());
        return;
    }

    // 无文档:居中显示提示文字
    if (m_currentImage.isNull()) {
        painter.setPen(Qt::white);
        QFont font = painter.font();
        font.setPointSize(12);
        painter.setFont(font);

        QScrollArea *scrollArea = getScrollArea();
        painter.drawText(scrollArea->viewport()->rect(),
                         Qt::AlignCenter, tr("No document loaded"));
        return;
    }

    // 单页/双页模式
    if (state->currentDisplayMode() == PageDisplayMode::SinglePage ||
        m_secondImage.isNull()) {
        paintSinglePageMode(painter);
    } else {
        paintDoublePageMode(painter);
    }
}

void PDFPageWidget::paintSinglePageMode(QPainter& painter)
{
    int x = (width() - m_currentImage.width()) / 2;
    int y = (height() - m_currentImage.height()) / 2;

    drawPageImage(painter, m_currentImage, x, y);

    const PDFDocumentState* state = m_session->state();
    double actualZoom = state->currentZoom();
    int currentPage = state->currentPage();

    // 通过交互处理器绘制搜索高亮
    if (m_interactionHandler) {
        drawSearchHighlights(painter, currentPage, x, y, actualZoom);
        drawTextSelection(painter, currentPage, x, y, actualZoom);

        if (state->linksVisible()) {
            drawLinkAreas(painter, currentPage, x, y, actualZoom);
        }
    }
}

void PDFPageWidget::paintDoublePageMode(QPainter& painter)
{
    int totalWidth = m_currentImage.width() + m_secondImage.width() +
                     AppConfig::DOUBLE_PAGE_SPACING;
    int maxHeight = qMax(m_currentImage.height(), m_secondImage.height());

    int startX = (width() - totalWidth) / 2;
    int startY = (height() - maxHeight) / 2;

    const PDFDocumentState* state = m_session->state();
    int currentPage = state->currentPage();
    double actualZoom = state->currentZoom();

    // 绘制第一页
    int x1 = startX;
    int y1 = startY + (maxHeight - m_currentImage.height()) / 2;
    drawPageImage(painter, m_currentImage, x1, y1);

    if (m_interactionHandler) {
        drawSearchHighlights(painter, currentPage, x1, y1, actualZoom);
        drawTextSelection(painter, currentPage, x1, y1, actualZoom);

        if (state->linksVisible()) {
            drawLinkAreas(painter, currentPage, x1, y1, actualZoom);
        }
    }

    // 绘制第二页
    int x2 = startX + m_currentImage.width() + AppConfig::DOUBLE_PAGE_SPACING;
    int y2 = startY + (maxHeight - m_secondImage.height()) / 2;
    drawPageImage(painter, m_secondImage, x2, y2);

    if (m_interactionHandler && !m_secondImage.isNull()) {
        int nextPage = currentPage + 1;
        if (nextPage < m_renderer->pageCount()) {
            drawSearchHighlights(painter, nextPage, x2, y2, actualZoom);
            drawTextSelection(painter, nextPage, x2, y2, actualZoom);

            if (state->linksVisible()) {
                drawLinkAreas(painter, nextPage, x2, y2, actualZoom);
            }
        }
    }
}

void PDFPageWidget::paintContinuousMode(QPainter& painter, const QRect& visibleRect)
{
    const int margin = AppConfig::PAGE_MARGIN;
    const PDFDocumentState* state = m_session->state();
    double actualZoom = state->currentZoom();
    int rotation = state->currentRotation();

    QList<PageCacheKey> cachedKeys = m_cacheManager->cachedKeys();

    for (const PageCacheKey& key : cachedKeys) {
        int pageIndex = key.pageIndex;

        if (qAbs(key.zoom - actualZoom) >= 0.001 || key.rotation != rotation) {
            continue;
        }

        if (pageIndex >= state->pageYPositions().size()) {
            continue;
        }

        QImage pageImage = m_cacheManager->getPage(pageIndex, actualZoom, rotation);
        if (pageImage.isNull()) {
            continue;
        }

        int pageY = state->pageYPositions()[pageIndex] + margin;
        int pageX = (width() - pageImage.width()) / 2;

        int pageBottom = pageY + pageImage.height();
        if (pageBottom >= visibleRect.top() && pageY <= visibleRect.bottom()) {
            drawPageImage(painter, pageImage, pageX, pageY);

            if (m_interactionHandler) {
                drawSearchHighlights(painter, pageIndex, pageX, pageY, actualZoom);
                drawTextSelection(painter, pageIndex, pageX, pageY, actualZoom);

                if (state->linksVisible()) {
                    drawLinkAreas(painter, pageIndex, pageX, pageY, actualZoom);
                }
            }
        }
    }

    // 绘制未加载页面的占位符
    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPointSize(10);
    painter.setFont(font);

    const QVector<int>& positions = state->pageYPositions();
    const QVector<int>& heights = state->pageHeights();

    for (int i = 0; i < positions.size(); ++i) {
        if (!m_cacheManager->contains(i, actualZoom, rotation)) {
            int pageY = positions[i] + margin;
            int pageHeight = heights[i];

            if (pageY + pageHeight >= visibleRect.top() &&
                pageY <= visibleRect.bottom()) {
                QRect placeholderRect(margin, pageY,
                                      width() - 2 * margin, pageHeight);
                drawPagePlaceholder(painter, placeholderRect, i);
            }
        }
    }
}

void PDFPageWidget::drawPageImage(QPainter& painter, const QImage& image, int x, int y)
{
    // 绘制阴影
    QRect shadowRect = image.rect().translated(x + AppConfig::SHADOW_OFFSET,
                                               y + AppConfig::SHADOW_OFFSET);
    painter.fillRect(shadowRect, QColor(0, 0, 0, 100));

    // 绘制页面
    painter.drawImage(x, y, image);
}

void PDFPageWidget::drawPagePlaceholder(QPainter& painter,
                                        const QRect& rect, int pageIndex)
{
    painter.fillRect(rect, QColor(80, 80, 80));
    painter.drawText(rect, Qt::AlignCenter,
                     tr("Loading page %1...").arg(pageIndex + 1));
}

void PDFPageWidget::drawSearchHighlights(QPainter& painter,
                                         int pageIndex,
                                         int pageX,
                                         int pageY,
                                         double zoom)
{
    if (!m_interactionHandler) {
        return;
    }

    QVector<SearchResult> results = m_interactionHandler->getPageSearchResults(pageIndex);
    if (results.isEmpty()) {
        return;
    }

    const PDFDocumentState* state = m_session->state();
    int currentMatchIndex = state->searchCurrentMatchIndex();

    for (const SearchResult& result : results) {
        bool isCurrent = false; // 判断逻辑保持不变

        for (const QRectF& quad : result.quads) {
            QRectF scaledQuad = QRectF(
                quad.x() * zoom,
                quad.y() * zoom,
                quad.width() * zoom,
                quad.height() * zoom
                );
            scaledQuad.translate(pageX, pageY);

            if (isCurrent) {
                painter.fillRect(scaledQuad, QColor(255, 165, 0, 120));
                painter.setPen(QPen(QColor(255, 140, 0), 2));
                painter.drawRect(scaledQuad);
            } else {
                painter.fillRect(scaledQuad, QColor(255, 255, 0, 80));
            }
        }
    }
}

void PDFPageWidget::drawLinkAreas(QPainter& painter, int pageIndex,
                                  int pageX, int pageY, double zoom)
{
    if (!m_interactionHandler) {
        return;
    }

    QVector<PDFLink> links = m_interactionHandler->loadPageLinks(pageIndex);
    if (links.isEmpty()) {
        return;
    }

    // 注意：hoveredLink是InteractionHandler的中间状态，可以访问
    const PDFLink* hoveredLink = nullptr;
    if (m_interactionHandler->linkManager()) {
        // hoveredLink需要从InteractionHandler获取
        // 这里假设InteractionHandler提供了访问方法
    }

    for (const PDFLink& link : links) {
        QRectF scaledRect(
            link.rect.x() * zoom,
            link.rect.y() * zoom,
            link.rect.width() * zoom,
            link.rect.height() * zoom
            );
        scaledRect.translate(pageX, pageY);

        bool isHovered = (&link == hoveredLink);

        if (isHovered) {
            painter.fillRect(scaledRect, QColor(0, 120, 215, 80));
            painter.setPen(QPen(QColor(0, 120, 215), 2));
        } else {
            painter.fillRect(scaledRect, QColor(0, 120, 215, 30));
            painter.setPen(QPen(QColor(0, 120, 215, 100), 1, Qt::DashLine));
        }

        painter.drawRect(scaledRect);
    }
}

void PDFPageWidget::drawTextSelection(QPainter& painter, int pageIndex,
                                      int pageX, int pageY, double zoom)
{
    if (!m_interactionHandler) {
        return;
    }

    const TextSelection& selection = m_interactionHandler->getCurrentTextSelection();

    if (selection.pageIndex != pageIndex) {
        return;
    }

    QColor highlightColor;
    switch (selection.mode) {
    case SelectionMode::Word:
        highlightColor = QColor(100, 150, 255, 100);
        break;
    case SelectionMode::Line:
        highlightColor = QColor(150, 200, 255, 90);
        break;
    default:
        highlightColor = QColor(0, 120, 215, 80);
        break;
    }

    painter.save();
    painter.setBrush(highlightColor);
    painter.setPen(Qt::NoPen);

    for (const QRectF& rect : selection.highlightRects) {
        QRectF scaledRect(
            rect.x() * zoom + pageX,
            rect.y() * zoom + pageY,
            rect.width() * zoom,
            rect.height() * zoom
            );

        painter.drawRect(scaledRect);
    }

    painter.restore();
}

// ========== 坐标转换 ==========

QPointF PDFPageWidget::screenToPageCoord(const QPoint& screenPos,
                                         int pageX, int pageY) const
{
    double pageCoordX = screenPos.x() - pageX;
    double pageCoordY = screenPos.y() - pageY;
    return QPointF(pageCoordX, pageCoordY);
}

int PDFPageWidget::getPageAtPos(const QPoint& pos, int* pageX, int* pageY) const
{
    if (!m_renderer || !m_renderer->isDocumentLoaded()) {
        return -1;
    }

    const int margin = AppConfig::PAGE_MARGIN;
    const PDFDocumentState* state = m_session->state();

    // 连续滚动模式
    if (state->isContinuousScroll() && !state->pageYPositions().isEmpty()) {

        const QVector<int>& positions = state->pageYPositions();
        const QVector<int>& heights = state->pageHeights();

        for (int i = 0; i < positions.size(); ++i) {
            int top = positions[i] + margin;
            int bottom = top + heights[i];

            if (pos.y() >= top && pos.y() <= bottom) {
                double actualZoom = state->currentZoom();
                QSizeF pageSize = m_renderer->pageSize(i);
                if (state->currentRotation() == 90 ||
                    state->currentRotation() == 270) {
                    pageSize.transpose();
                }
                int pageWidth = qRound(pageSize.width() * actualZoom);
                int left = (width() - pageWidth) / 2;
                int right = left + pageWidth;

                if (pos.x() >= left && pos.x() <= right) {
                    if (pageX) *pageX = left;
                    if (pageY) *pageY = top;
                    return i;
                }
            }
        }
        return -1;
    }
    // 单页/双页模式
    else {
        int currentPage = state->currentPage();
        int contentX = (width() - m_currentImage.width()) / 2;
        int contentY = (height() - m_currentImage.height()) / 2;

        // 检查第一页
        QRect firstPageRect(contentX, contentY,
                            m_currentImage.width(), m_currentImage.height());
        if (firstPageRect.contains(pos)) {
            if (pageX) *pageX = contentX;
            if (pageY) *pageY = contentY;
            return currentPage;
        }

        // 双页模式:检查第二页
        if (state->currentDisplayMode() == PageDisplayMode::DoublePage &&
            !m_secondImage.isNull()) {
            int secondX = contentX + m_currentImage.width() +
                          AppConfig::DOUBLE_PAGE_SPACING;
            int maxHeight = qMax(m_currentImage.height(), m_secondImage.height());
            int secondY = contentY + (maxHeight - m_secondImage.height()) / 2;

            QRect secondPageRect(secondX, secondY,
                                 m_secondImage.width(), m_secondImage.height());
            if (secondPageRect.contains(pos)) {
                if (pageX) *pageX = secondX;
                if (pageY) *pageY = secondY;
                return currentPage + 1;
            }
        }
    }

    return -1;
}

// ========== 鼠标事件处理 ==========

void PDFPageWidget::mouseMoveEvent(QMouseEvent* event)
{
    const PDFDocumentState* state = m_session->state();

    // 如果正在进行文本选择
    if (m_isTextSelecting && m_interactionHandler) {
        int pageX, pageY;
        int pageIndex = getPageAtPos(event->pos(), &pageX, &pageY);

        if (pageIndex >= 0) {
            double actualZoom = state->currentZoom();
            QPointF pagePos = screenToPageCoord(event->pos(), pageX, pageY);

            m_interactionHandler->updateTextSelection(pageIndex, pagePos, actualZoom);
            m_lastMousePos = event->pos();
        }

        event->accept();
        return;
    }

    // 链接检测
    if (!m_renderer || !m_renderer->isDocumentLoaded()) {
        if (cursor().shape() != Qt::ArrowCursor) {
            setCursor(Qt::ArrowCursor);
        }
        QWidget::mouseMoveEvent(event);
        return;
    }

    int pageX, pageY;
    int pageIndex = getPageAtPos(event->pos(), &pageX, &pageY);

    if (pageIndex < 0) {
        if (m_interactionHandler) {
            m_interactionHandler->clearHoveredLink();
        }
        if (cursor().shape() != Qt::ArrowCursor) {
            setCursor(Qt::ArrowCursor);
        }
        QToolTip::hideText();
        QWidget::mouseMoveEvent(event);
        return;
    }

    double actualZoom = state->currentZoom();
    QPointF pagePos = screenToPageCoord(event->pos(), pageX, pageY);

    const PDFLink* link = nullptr;
    if (m_interactionHandler && state->linksVisible()) {
        link = m_interactionHandler->hitTestLink(pageIndex, pagePos, actualZoom);
    }

    if (link) {
        if (cursor().shape() != Qt::PointingHandCursor) {
            setCursor(Qt::PointingHandCursor);
        }

        QString tooltip;
        if (link->isInternal()) {
            tooltip = tr("Go to page %1").arg(link->targetPage + 1);
        } else if (link->isExternal()) {
            tooltip = tr("Open: %1").arg(link->uri);
        }
        QToolTip::showText(event->globalPosition().toPoint(), tooltip, this);
    } else {
        QToolTip::hideText();

        if (state->isTextPDF()) {
            if (cursor().shape() != Qt::IBeamCursor) {
                setCursor(Qt::IBeamCursor);
            }
        } else {
            if (cursor().shape() != Qt::ArrowCursor) {
                setCursor(Qt::ArrowCursor);
            }
        }
    }

    QWidget::mouseMoveEvent(event);
}

void PDFPageWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_isTextSelecting) {
        m_isTextSelecting = false;

        if (m_interactionHandler) {
            m_interactionHandler->endTextSelection();
        }

        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void PDFPageWidget::mousePressEvent(QMouseEvent* event)
{
    const PDFDocumentState* state = m_session->state();

    // 处理链接点击(优先级最高)
    if (event->button() == Qt::LeftButton && m_interactionHandler && state->linksVisible()) {
        // 注意：这里需要访问InteractionHandler的中间状态(hoveredLink)
        // 由于InteractionHandler保留了中间状态，这里可以正常工作
        int pageX, pageY;
        int pageIndex = getPageAtPos(event->pos(), &pageX, &pageY);

        if (pageIndex >= 0 && state->linksVisible()) {
            double actualZoom = state->currentZoom();
            QPointF pagePos = screenToPageCoord(event->pos(), pageX, pageY);

            const PDFLink* link = m_interactionHandler->hitTestLink(pageIndex, pagePos, actualZoom);
            if (link) {
                m_session->handleLinkClick(link);
                event->accept();
                return;
            }
        }
    }

    // 处理文本选择
    if (event->button() == Qt::LeftButton &&
        m_renderer && state->isTextPDF() &&
        m_interactionHandler) {

        int pageX, pageY;
        int pageIndex = getPageAtPos(event->pos(), &pageX, &pageY);

        if (pageIndex >= 0) {
            double actualZoom = state->currentZoom();
            QPointF pagePos = screenToPageCoord(event->pos(), pageX, pageY);

            // 检测多击
            qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
            qint64 timeDiff = currentTime - m_lastClickTime;

            const int doubleClickTime = QApplication::doubleClickInterval();

            if (timeDiff < doubleClickTime &&
                (event->pos() - m_lastClickPos).manhattanLength() < 5) {
                m_clickCount++;
            } else {
                m_clickCount = 1;
            }

            m_lastClickTime = currentTime;
            m_lastClickPos = event->pos();

            // Shift+点击:扩展选择
            if (event->modifiers() & Qt::ShiftModifier) {
                m_interactionHandler->extendTextSelection(pageIndex, pagePos, actualZoom);
                m_isTextSelecting = false;
            }
            // 三击:选择整行
            else if (m_clickCount >= 3) {
                m_interactionHandler->selectLine(pageIndex, pagePos, actualZoom);
                m_isTextSelecting = false;
                m_clickCount = 0;
            }
            // 双击:选择单词
            else if (m_clickCount == 2) {
                m_interactionHandler->selectWord(pageIndex, pagePos, actualZoom);
                m_isTextSelecting = false;
            }
            // 单击:开始字符级别选择
            else {
                m_interactionHandler->startTextSelection(pageIndex, pagePos, actualZoom);
                m_isTextSelecting = true;
                m_lastMousePos = event->pos();
            }

            event->accept();
            return;
        }
    }

    QWidget::mousePressEvent(event);
}

void PDFPageWidget::contextMenuEvent(QContextMenuEvent* event)
{
    const PDFDocumentState* state = m_session->state();

    if (!state->isDocumentLoaded() || !m_interactionHandler) {
        return;
    }

    QMenu menu(this);

    // 如果有选中的文本
    if (state->hasTextSelection()) {
        QAction* copyAction = menu.addAction(tr("Copy"));
        copyAction->setShortcut(QKeySequence::Copy);
        connect(copyAction, &QAction::triggered,
                this, &PDFPageWidget::copySelectedText);

        menu.addSeparator();
    }

    // 如果是文本PDF
    if (state->isTextPDF()) {
        int pageX, pageY;
        int pageIndex = getPageAtPos(event->pos(), &pageX, &pageY);

        if (pageIndex >= 0 && !state->hasTextSelection()) {
            double actualZoom = state->currentZoom();
            QPointF pagePos = screenToPageCoord(event->pos(), pageX, pageY);

            QAction* selectWordAction = menu.addAction(tr("Select Word"));
            connect(selectWordAction, &QAction::triggered, this, [=]() {
                m_interactionHandler->selectWord(pageIndex, pagePos, actualZoom);
            });

            QAction* selectLineAction = menu.addAction(tr("Select Line"));
            connect(selectLineAction, &QAction::triggered, this, [=]() {
                m_interactionHandler->selectLine(pageIndex, pagePos, actualZoom);
            });

            menu.addSeparator();
        }

        QAction* selectAllAction = menu.addAction(tr("Select All"));
        selectAllAction->setShortcut(QKeySequence::SelectAll);
        connect(selectAllAction, &QAction::triggered,
                this, &PDFPageWidget::selectAll);
    }

    if (!menu.isEmpty()) {
        menu.exec(event->globalPos());
    }
}

void PDFPageWidget::keyPressEvent(QKeyEvent* event)
{
    // Ctrl+A: 全选
    if (event->matches(QKeySequence::SelectAll)) {
        selectAll();
        event->accept();
        return;
    }

    // Ctrl+C: 复制
    if (event->matches(QKeySequence::Copy)) {
        copySelectedText();
        event->accept();
        return;
    }

    // Escape: 清除选择
    if (event->key() == Qt::Key_Escape) {
        if (m_session->state()->hasTextSelection()) {
            m_session->clearTextSelection();
            event->accept();
            return;
        }
    }

    QWidget::keyPressEvent(event);
}

// ========== 工具方法 ==========

QScrollArea* PDFPageWidget::getScrollArea() const
{
    QWidget* parentWgt = parentWidget();
    if (parentWgt) {
        return qobject_cast<QScrollArea*>(parentWgt->parentWidget());
    }
    return nullptr;
}

QSize PDFPageWidget::getViewportSize() const
{
    // 如果在QScrollArea中,使用viewport的尺寸
    QScrollArea* scrollArea = getScrollArea();
    if (scrollArea) {
        return scrollArea->viewport()->size();
    }

    // 否则使用widget的尺寸
    return size();
}
