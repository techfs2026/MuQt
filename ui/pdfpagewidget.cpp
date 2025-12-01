#include "pdfpagewidget.h"
#include "pdfdocumentsession.h"
#include "pdfdocumentstate.h"
#include "perthreadmupdfrenderer.h"
#include "pagecachemanager.h"
#include "pdfinteractionhandler.h"
#include "textselector.h"
#include "linkmanager.h"
#include "appconfig.h"

#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QMouseEvent>
#include <QDebug>

PDFPageWidget::PDFPageWidget(PDFDocumentSession* session, QWidget* parent)
    : QWidget(parent)
    , m_session(session)
    , m_renderer(nullptr)
    , m_cacheManager(nullptr)
    , m_isTextSelecting(false)
{
    if (!m_session) {
        qCritical() << "PDFPageWidget: session is null!";
        return;
    }

    // 从Session获取组件引用
    m_renderer = m_session->renderer();
    m_cacheManager = m_session->pageCache();

    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

PDFPageWidget::~PDFPageWidget()
{
}

// ==================== 被动更新方法 ====================

void PDFPageWidget::setDisplayImages(const QImage& primaryImage, const QImage& secondaryImage)
{
    m_currentImage = primaryImage;
    m_secondImage = secondaryImage;

    // 更新尺寸
    QSize targetSize = sizeHint();
    resize(targetSize);

    update();
}

void PDFPageWidget::refreshVisiblePages()
{
    // 连续滚动模式：通知Tab需要更新可见区域
    emit visibleAreaChanged();
}

void PDFPageWidget::setTextSelectionMode(bool enabled)
{
    m_isTextSelecting = enabled;
    if (!enabled) {
        setCursor(Qt::IBeamCursor);
    }
}

void PDFPageWidget::clearHighlights()
{
    update();
}

// ==================== 工具方法 ====================

QPointF PDFPageWidget::screenToPageCoord(const QPoint& screenPos, int pageX, int pageY) const
{
    return QPointF(screenPos.x() - pageX, screenPos.y() - pageY);
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
                if (state->currentRotation() == 90 || state->currentRotation() == 270) {
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
        QRect firstPageRect(contentX, contentY, m_currentImage.width(), m_currentImage.height());
        if (firstPageRect.contains(pos)) {
            if (pageX) *pageX = contentX;
            if (pageY) *pageY = contentY;
            return currentPage;
        }

        // 双页模式：检查第二页
        if (state->currentDisplayMode() == PageDisplayMode::DoublePage && !m_secondImage.isNull()) {
            int secondX = contentX + m_currentImage.width() + AppConfig::DOUBLE_PAGE_SPACING;
            int maxHeight = qMax(m_currentImage.height(), m_secondImage.height());
            int secondY = contentY + (maxHeight - m_secondImage.height()) / 2;

            QRect secondPageRect(secondX, secondY, m_secondImage.width(), m_secondImage.height());
            if (secondPageRect.contains(pos)) {
                if (pageX) *pageX = secondX;
                if (pageY) *pageY = secondY;
                return currentPage + 1;
            }
        }
    }

    return -1;
}

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
    QScrollArea* scrollArea = getScrollArea();
    if (scrollArea) {
        return scrollArea->viewport()->size();
    }
    return size();
}

QSize PDFPageWidget::calculateRequiredSize() const
{
    return sizeHint();
}

QString PDFPageWidget::getCacheStatistics() const
{
    if (!m_cacheManager) {
        return "Cache: Not initialized";
    }
    return m_cacheManager->getStatistics();
}

// ==================== 尺寸计算 ====================

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

    // 连续滚动模式
    if (state->isContinuousScroll() && !state->pageYPositions().isEmpty()) {
        int maxWidth = 0;

        if (m_renderer && m_renderer->isDocumentLoaded()) {
            QSizeF pageSize = m_renderer->pageSize(0);
            if (state->currentRotation() == 90 || state->currentRotation() == 270) {
                pageSize.transpose();
            }
            maxWidth = qRound(pageSize.width() * state->currentZoom());
        }

        const QVector<int>& positions = state->pageYPositions();
        const QVector<int>& heights = state->pageHeights();
        int totalHeight = positions.last() + heights.last();
        return QSize(maxWidth + 2 * margin, totalHeight + 2 * margin);
    }

    // 单页/双页模式
    int contentWidth = m_currentImage.width();
    int contentHeight = m_currentImage.height();

    if (state->currentDisplayMode() == PageDisplayMode::DoublePage && !m_secondImage.isNull()) {
        contentWidth = m_currentImage.width() + m_secondImage.width() + AppConfig::DOUBLE_PAGE_SPACING;
        contentHeight = qMax(m_currentImage.height(), m_secondImage.height());
    }

    return QSize(contentWidth + 2 * margin, contentHeight + 2 * margin);
}

// ==================== 绘制相关 ====================

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

    // 无文档
    if (m_currentImage.isNull()) {
        painter.setPen(Qt::white);
        QFont font = painter.font();
        font.setPointSize(12);
        painter.setFont(font);

        QScrollArea* scrollArea = getScrollArea();
        if (scrollArea && scrollArea->viewport()) {
            painter.drawText(scrollArea->viewport()->rect(), Qt::AlignCenter, tr("未加载文档"));
        }
        return;
    }

    // 单页/双页模式
    if (state->currentDisplayMode() == PageDisplayMode::SinglePage || m_secondImage.isNull()) {
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
    drawOverlays(painter, state->currentPage(), x, y, state->currentZoom());
}

void PDFPageWidget::paintDoublePageMode(QPainter& painter)
{
    int totalWidth = m_currentImage.width() + m_secondImage.width() + AppConfig::DOUBLE_PAGE_SPACING;
    int maxHeight = qMax(m_currentImage.height(), m_secondImage.height());

    int startX = (width() - totalWidth) / 2;
    int startY = (height() - maxHeight) / 2;

    const PDFDocumentState* state = m_session->state();
    int currentPage = state->currentPage();
    double actualZoom = state->currentZoom();

    // 第一页
    int x1 = startX;
    int y1 = startY + (maxHeight - m_currentImage.height()) / 2;
    drawPageImage(painter, m_currentImage, x1, y1);
    drawOverlays(painter, currentPage, x1, y1, actualZoom);

    // 第二页
    int x2 = startX + m_currentImage.width() + AppConfig::DOUBLE_PAGE_SPACING;
    int y2 = startY + (maxHeight - m_secondImage.height()) / 2;
    drawPageImage(painter, m_secondImage, x2, y2);

    if (!m_secondImage.isNull()) {
        int nextPage = currentPage + 1;
        if (nextPage < m_renderer->pageCount()) {
            drawOverlays(painter, nextPage, x2, y2, actualZoom);
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
            drawOverlays(painter, pageIndex, pageX, pageY, actualZoom);
        }
    }

    // 绘制占位符
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

            if (pageY + pageHeight >= visibleRect.top() && pageY <= visibleRect.bottom()) {
                QRect placeholderRect(margin, pageY, width() - 2 * margin, pageHeight);
                drawPagePlaceholder(painter, placeholderRect, i);
            }
        }
    }
}

void PDFPageWidget::drawPageImage(QPainter& painter, const QImage& image, int x, int y)
{
    // 阴影
    QRect shadowRect = image.rect().translated(x + AppConfig::SHADOW_OFFSET, y + AppConfig::SHADOW_OFFSET);
    painter.fillRect(shadowRect, QColor(0, 0, 0, 100));

    // 页面
    painter.drawImage(x, y, image);
}

void PDFPageWidget::drawPagePlaceholder(QPainter& painter, const QRect& rect, int pageIndex)
{
    painter.fillRect(rect, QColor(80, 80, 80));
    painter.drawText(rect, Qt::AlignCenter, tr("加载页面%1中...").arg(pageIndex + 1));
}

void PDFPageWidget::drawOverlays(QPainter& painter, int pageIndex, int pageX, int pageY, double zoom)
{
    const PDFDocumentState* state = m_session->state();

    drawSearchHighlights(painter, pageIndex, pageX, pageY, zoom);
    drawTextSelection(painter, pageIndex, pageX, pageY, zoom);

    if (state->linksVisible()) {
        drawLinkAreas(painter, pageIndex, pageX, pageY, zoom);
    }
}

void PDFPageWidget::drawSearchHighlights(QPainter& painter, int pageIndex, int pageX, int pageY, double zoom)
{
    PDFInteractionHandler* handler = m_session->interactionHandler();
    if (!handler) return;

    QVector<SearchResult> results = handler->getPageSearchResults(pageIndex);
    if (results.isEmpty()) return;

    const PDFDocumentState* state = m_session->state();
    int currentMatchIndex = state->searchCurrentMatchIndex();

    for (const SearchResult& result : results) {
        bool isCurrent = false; // 判断逻辑保持不变

        for (const QRectF& quad : result.quads) {
            QRectF scaledQuad(quad.x() * zoom, quad.y() * zoom, quad.width() * zoom, quad.height() * zoom);
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

void PDFPageWidget::drawLinkAreas(QPainter& painter, int pageIndex, int pageX, int pageY, double zoom)
{
    PDFInteractionHandler* handler = m_session->interactionHandler();
    if (!handler) return;

    QVector<PDFLink> links = handler->loadPageLinks(pageIndex);
    if (links.isEmpty()) return;

    for (const PDFLink& link : links) {
        QRectF scaledRect(link.rect.x() * zoom, link.rect.y() * zoom,
                          link.rect.width() * zoom, link.rect.height() * zoom);
        scaledRect.translate(pageX, pageY);

        painter.fillRect(scaledRect, QColor(0, 120, 215, 30));
        painter.setPen(QPen(QColor(0, 120, 215, 100), 1, Qt::DashLine));
        painter.drawRect(scaledRect);
    }
}

void PDFPageWidget::drawTextSelection(QPainter& painter, int pageIndex, int pageX, int pageY, double zoom)
{
    PDFInteractionHandler* handler = m_session->interactionHandler();
    if (!handler) return;

    const TextSelection& selection = handler->getCurrentTextSelection();
    if (selection.pageIndex != pageIndex) return;

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
        QRectF scaledRect(rect.x() * zoom + pageX, rect.y() * zoom + pageY,
                          rect.width() * zoom, rect.height() * zoom);
        painter.drawRect(scaledRect);
    }

    painter.restore();
}

// ==================== 鼠标事件处理 ====================

void PDFPageWidget::mouseMoveEvent(QMouseEvent* event)
{
    const PDFDocumentState* state = m_session->state();

    // 文本选择拖拽中
    if (m_isTextSelecting) {
        int pageX, pageY;
        int pageIndex = getPageAtPos(event->pos(), &pageX, &pageY);

        if (pageIndex >= 0) {
            QPointF pagePos = screenToPageCoord(event->pos(), pageX, pageY);
            emit textSelectionDragging(pageIndex, pagePos);
        }

        event->accept();
        return;
    }

    // 普通移动：检测页面和链接
    int pageX, pageY;
    int pageIndex = getPageAtPos(event->pos(), &pageX, &pageY);

    if (pageIndex < 0) {
        emit mouseLeftAllPages();
        if (cursor().shape() != Qt::ArrowCursor) {
            setCursor(Qt::ArrowCursor);
        }
        QWidget::mouseMoveEvent(event);
        return;
    }

    QPointF pagePos = screenToPageCoord(event->pos(), pageX, pageY);
    emit mouseMovedOnPage(pageIndex, pagePos);

    QWidget::mouseMoveEvent(event);
}

void PDFPageWidget::mousePressEvent(QMouseEvent* event)
{
    int pageX, pageY;
    int pageIndex = getPageAtPos(event->pos(), &pageX, &pageY);

    if (pageIndex < 0) {
        QWidget::mousePressEvent(event);
        return;
    }

    QPointF pagePos = screenToPageCoord(event->pos(), pageX, pageY);

    // 发射点击信号，由Tab处理所有业务逻辑
    emit pageClicked(pageIndex, pagePos, event->button(), event->modifiers());

    // 记录拖拽起始点
    if (event->button() == Qt::LeftButton) {
        m_dragStartPos = event->pos();
    }

    event->accept();
}

void PDFPageWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_isTextSelecting) {
        m_isTextSelecting = false;
        emit textSelectionEnded();
        event->accept();
        return;
    }

    // 右键菜单
    if (event->button() == Qt::RightButton) {
        int pageX, pageY;
        int pageIndex = getPageAtPos(event->pos(), &pageX, &pageY);

        if (pageIndex >= 0) {
            QPointF pagePos = screenToPageCoord(event->pos(), pageX, pageY);
            emit contextMenuRequested(pageIndex, pagePos, event->globalPosition().toPoint());
        }
    }

    QWidget::mouseReleaseEvent(event);
}
