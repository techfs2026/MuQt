#include "pdfpagewidget.h"
#include "pdfdocumentsession.h"
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
    , m_viewHandler(nullptr)
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
    m_viewHandler = m_session->viewHandler();
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

    // 连接Handler信号
    connect(m_viewHandler, &PDFViewHandler::pageChanged,
            this, &PDFPageWidget::onPageChangedFromHandler);
    connect(m_viewHandler, &PDFViewHandler::zoomChanged,
            this, &PDFPageWidget::onZoomChangedFromHandler);
    connect(m_viewHandler, &PDFViewHandler::displayModeChanged,
            this, &PDFPageWidget::onDisplayModeChanged);
    connect(m_viewHandler, &PDFViewHandler::continuousScrollChanged,
            this, [this](bool continuous) {
                m_cacheManager->clear();
                renderCurrentPage();
                emit continuousScrollChanged(continuous);
            });
    connect(m_viewHandler, &PDFViewHandler::rotationChanged,
            this, [this](int rotation) {
                renderCurrentPage();
            });

    connect(m_interactionHandler, &PDFInteractionHandler::textSelectionChanged,
            this, [this]() { update(); });
}

PDFPageWidget::~PDFPageWidget()
{
}

// ========== 页面导航 ==========

void PDFPageWidget::onPageChangedFromHandler(int pageIndex)
{
    // 更新缓存管理器
    double actualZoom = m_viewHandler->calculateActualZoom(getViewportSize());
    m_cacheManager->setCurrentPage(pageIndex, actualZoom, m_viewHandler->rotation());

    // 连续滚动模式:滚动到目标位置
    if (m_viewHandler->isContinuousScroll()) {
        int targetY = m_viewHandler->getScrollPositionForPage(
            pageIndex, AppConfig::PAGE_MARGIN);
        if (targetY >= 0) {
            QScrollArea* scrollArea = getScrollArea();
            if (scrollArea) {
                scrollArea->verticalScrollBar()->setValue(targetY);
            }
        }
    }

    // 触发渲染
    renderCurrentPage();

    // 转发信号给外部(MainWindow)
    emit pageChanged(pageIndex);
}

// ========== 缩放控制 ==========

void PDFPageWidget::setZoom(double zoom)
{
    m_viewHandler->setZoom(zoom);
}

void PDFPageWidget::onZoomChangedFromHandler(double zoom)
{
    renderCurrentPage();
    emit zoomChanged(zoom);
}

void PDFPageWidget::setZoomMode(ZoomMode mode)
{
    m_viewHandler->setZoomMode(mode);
    QSize viewportSize = getViewportSize();
    m_viewHandler->updateZoom(viewportSize);
}

void PDFPageWidget::updateZoom()
{
    QSize viewportSize = getViewportSize();
    m_viewHandler->updateZoom(viewportSize);
}

// ========== 旋转控制 ==========

void PDFPageWidget::setRotation(int rotation)
{
    m_viewHandler->setRotation(rotation);
}

// ========== 显示模式 ==========

void PDFPageWidget::setDisplayMode(PageDisplayMode mode)
{
    m_viewHandler->setDisplayMode(mode);
}

void PDFPageWidget::onDisplayModeChanged(PageDisplayMode mode)
{
    m_cacheManager->clear();
    renderCurrentPage();
    emit displayModeChanged(mode);
}

void PDFPageWidget::setContinuousScroll(bool continuous)
{
    m_viewHandler->setContinuousScroll(continuous);
}

// ========== 其他 ==========

void PDFPageWidget::refresh()
{
    renderCurrentPage();
}

QSize PDFPageWidget::sizeHint() const
{
    if (m_currentImage.isNull() && m_viewHandler->pageYPositions().isEmpty()) {
        QSize viewportSize = getViewportSize();
        if (viewportSize.isValid() && viewportSize.width() > 0 && viewportSize.height() > 0) {
            return viewportSize;
        }
        return QSize(800, 600);
    }

    const int margin = AppConfig::PAGE_MARGIN;

    // 连续滚动模式:基于预计算的位置
    if (m_viewHandler->isContinuousScroll() &&
        !m_viewHandler->pageYPositions().isEmpty()) {
        int maxWidth = 0;

        if (m_renderer && m_renderer->isDocumentLoaded()) {
            QSizeF pageSize = m_renderer->pageSize(0);
            if (m_viewHandler->rotation() == 90 || m_viewHandler->rotation() == 270) {
                pageSize.transpose();
            }

            double actualZoom = getActualZoom();
            maxWidth = qRound(pageSize.width() * actualZoom);
        }

        const QVector<int>& positions = m_viewHandler->pageYPositions();
        const QVector<int>& heights = m_viewHandler->pageHeights();
        int totalHeight = positions.last() + heights.last();
        return QSize(maxWidth + 2 * margin, totalHeight + 2 * margin);
    }

    // 单页/双页模式
    int contentWidth = m_currentImage.width();
    int contentHeight = m_currentImage.height();

    if (m_viewHandler->displayMode() == PageDisplayMode::DoublePage &&
        !m_secondImage.isNull()) {
        contentWidth = m_currentImage.width() + m_secondImage.width() +
                       AppConfig::DOUBLE_PAGE_SPACING;
        contentHeight = qMax(m_currentImage.height(), m_secondImage.height());
    }

    return QSize(contentWidth + 2 * margin, contentHeight + 2 * margin);
}

QString PDFPageWidget::getCacheStatistics() const
{
    if (!m_cacheManager) {
        return "Cache: Not initialized";
    }
    return m_cacheManager->getStatistics();
}

void PDFPageWidget::setLinksVisible(bool enabled)
{
    if (m_interactionHandler) {
        m_interactionHandler->setLinksVisible(enabled);
        update();
    }
}

void PDFPageWidget::copySelectedText()
{
    if (m_interactionHandler) {
        m_interactionHandler->copySelectedText();
    }
}

void PDFPageWidget::selectAll()
{
    if (m_interactionHandler && m_renderer && m_renderer->isDocumentLoaded()) {
        m_interactionHandler->selectAll(currentPage());
    }
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

    // 从Handler获取状态
    int currentPage = m_viewHandler->currentPage();
    QSize viewportSize = getViewportSize();
    double actualZoom = m_viewHandler->calculateActualZoom(viewportSize);
    int rotation = m_viewHandler->rotation();

    // 连续滚动模式
    if (m_viewHandler->isContinuousScroll()) {
        m_cacheManager->clear();
        m_viewHandler->calculatePagePositions(actualZoom);

        QSize targetSize = sizeHint();
        resize(targetSize);

        QTimer::singleShot(0, this, [this]() {
            refreshVisiblePages();
        });
    }
    // 单页/双页模式
    else {
        m_currentImage = renderSinglePage(currentPage, actualZoom);

        if (m_viewHandler->displayMode() == PageDisplayMode::DoublePage) {
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

    int rotation = m_viewHandler->rotation();

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

void PDFPageWidget::renderVisiblePages(const QRect& visibleRect)
{
    if (!m_renderer || !m_renderer->isDocumentLoaded() ||
        !m_viewHandler->isContinuousScroll()) {
        return;
    }

    // 如果位置还没计算,先计算
    if (m_viewHandler->pageYPositions().isEmpty()) {
        double actualZoom = getActualZoom();
        m_viewHandler->calculatePagePositions(actualZoom);

        if (m_viewHandler->pageYPositions().isEmpty()) {
            return;
        }
    }

    // 获取可见页面和扩展区域
    QSet<int> visiblePages = m_viewHandler->getVisiblePages(
        visibleRect,
        AppConfig::instance().preloadMargin(),
        AppConfig::PAGE_MARGIN
        );

    if (AppConfig::instance().debugMode()) {
        qDebug() << "renderVisiblePages - visible:" << visiblePages.size()
        << "cached:" << m_cacheManager->cacheSize();
        qDebug() << m_cacheManager->getStatistics();
    }

    // 标记可见页面
    m_cacheManager->markVisiblePages(visiblePages);

    // 计算实际缩放
    double actualZoom = getActualZoom();

    // 更新当前页面信息
    if (m_viewHandler->currentPage() >= 0) {
        m_cacheManager->setCurrentPage(
            m_viewHandler->currentPage(),
            actualZoom,
            m_viewHandler->rotation()
            );
    }

    // 渲染可见页面(如果还未缓存)
    for (int pageIndex : visiblePages) {
        if (!m_cacheManager->contains(pageIndex, actualZoom,
                                      m_viewHandler->rotation())) {
            QImage pageImage = renderSinglePage(pageIndex, actualZoom);

            if (AppConfig::instance().debugMode() && !pageImage.isNull()) {
                qDebug() << "Page" << pageIndex << "rendered and cached"
                         << "zoom:" << actualZoom
                         << "rotation:" << m_viewHandler->rotation();
            }
        }
    }
}

// ========== 缩放计算 ==========

double PDFPageWidget::getActualZoom() const
{
    QSize viewportSize = getViewportSize();
    return m_viewHandler->calculateActualZoom(viewportSize);
}

// ========== 连续滚动相关 ==========

void PDFPageWidget::updateCurrentPageFromScroll(int scrollY)
{
    if (!m_viewHandler->isContinuousScroll()) {
        return;
    }

    m_viewHandler->updateCurrentPageFromScroll(scrollY, AppConfig::PAGE_MARGIN);
    // Handler会发射pageChanged信号,触发缓存更新

    refreshVisiblePages();
}

void PDFPageWidget::refreshVisiblePages()
{
    if (!m_viewHandler->isContinuousScroll()) {
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

    // 使用Handler获取可见页面
    QSet<int> visiblePages = m_viewHandler->getVisiblePages(
        visibleRect,
        AppConfig::instance().preloadMargin(),
        AppConfig::PAGE_MARGIN
        );

    // 标记可见页面
    m_cacheManager->markVisiblePages(visiblePages);

    // 渲染可见页面
    QSize viewportSize = getViewportSize();
    double actualZoom = m_viewHandler->calculateActualZoom(viewportSize);

    for (int pageIndex : visiblePages) {
        if (!m_cacheManager->contains(pageIndex, actualZoom, m_viewHandler->rotation())) {
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

    // 连续滚动模式
    if (m_viewHandler->isContinuousScroll() &&
        !m_viewHandler->pageYPositions().isEmpty()) {
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
    if (m_viewHandler->displayMode() == PageDisplayMode::SinglePage ||
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

    double actualZoom = getActualZoom();
    int currentPage = m_viewHandler->currentPage();

    // 通过交互处理器绘制搜索高亮
    if (m_interactionHandler) {
        drawSearchHighlights(painter, currentPage, x, y, actualZoom);
        drawTextSelection(painter, currentPage, x, y, actualZoom);
        drawLinkAreas(painter, currentPage, x, y, actualZoom);
    }
}

void PDFPageWidget::paintDoublePageMode(QPainter& painter)
{
    int totalWidth = m_currentImage.width() + m_secondImage.width() +
                     AppConfig::DOUBLE_PAGE_SPACING;
    int maxHeight = qMax(m_currentImage.height(), m_secondImage.height());

    int startX = (width() - totalWidth) / 2;
    int startY = (height() - maxHeight) / 2;

    int currentPage = m_viewHandler->currentPage();
    double actualZoom = getActualZoom();

    // 绘制第一页
    int x1 = startX;
    int y1 = startY + (maxHeight - m_currentImage.height()) / 2;
    drawPageImage(painter, m_currentImage, x1, y1);

    if (m_interactionHandler->searchManager()) {
        drawSearchHighlights(painter, currentPage, x1, y1, actualZoom);
    }

    if (m_interactionHandler->textSelector()) {
        drawTextSelection(painter, currentPage, x1, y1, actualZoom);
    }

    if (m_interactionHandler->linkManager() && m_interactionHandler->linksVisible()) {
        drawLinkAreas(painter, currentPage, x1, y1, actualZoom);
    }

    // 绘制第二页
    int x2 = startX + m_currentImage.width() + AppConfig::DOUBLE_PAGE_SPACING;
    int y2 = startY + (maxHeight - m_secondImage.height()) / 2;
    drawPageImage(painter, m_secondImage, x2, y2);

    if (m_interactionHandler->searchManager() && !m_secondImage.isNull()) {
        int nextPage = currentPage + 1;
        if (nextPage < m_renderer->pageCount()) {
            drawSearchHighlights(painter, nextPage, x2, y2, actualZoom);
        }
    }

    if (m_interactionHandler->textSelector() && !m_secondImage.isNull()) {
        int nextPage = currentPage + 1;
        if (nextPage < m_renderer->pageCount()) {
            drawTextSelection(painter, nextPage, x2, y2, actualZoom);
        }
    }

    if (m_interactionHandler->linkManager() && m_interactionHandler->linksVisible() && !m_secondImage.isNull()) {
        int nextPage = currentPage + 1;
        if (nextPage < m_renderer->pageCount()) {
            drawLinkAreas(painter, nextPage, x2, y2, actualZoom);
        }
    }
}

void PDFPageWidget::paintContinuousMode(QPainter& painter, const QRect& visibleRect)
{
    const int margin = AppConfig::PAGE_MARGIN;
    double actualZoom = getActualZoom();
    int rotation = m_viewHandler->rotation();

    QList<PageCacheKey> cachedKeys = m_cacheManager->cachedKeys();

    for (const PageCacheKey& key : cachedKeys) {
        int pageIndex = key.pageIndex;

        if (qAbs(key.zoom - actualZoom) >= 0.001 || key.rotation != rotation) {
            continue;
        }

        if (pageIndex >= m_viewHandler->pageYPositions().size()) {
            continue;
        }

        QImage pageImage = m_cacheManager->getPage(pageIndex, actualZoom, rotation);
        if (pageImage.isNull()) {
            continue;
        }

        int pageY = m_viewHandler->pageYPositions()[pageIndex] + margin;
        int pageX = (width() - pageImage.width()) / 2;

        int pageBottom = pageY + pageImage.height();
        if (pageBottom >= visibleRect.top() && pageY <= visibleRect.bottom()) {
            drawPageImage(painter, pageImage, pageX, pageY);

            if (m_interactionHandler->searchManager()) {
                drawSearchHighlights(painter, pageIndex, pageX, pageY, actualZoom);
            }

            if (m_interactionHandler->textSelector()) {
                drawTextSelection(painter, pageIndex, pageX, pageY, actualZoom);
            }

            if (m_interactionHandler->linkManager() && m_interactionHandler->linksVisible()) {
                drawLinkAreas(painter, pageIndex, pageX, pageY, actualZoom);
            }
        }
    }

    // 绘制未加载页面的占位符
    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPointSize(10);
    painter.setFont(font);

    const QVector<int>& positions = m_viewHandler->pageYPositions();
    const QVector<int>& heights = m_viewHandler->pageHeights();

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

    int currentMatchIndex = m_interactionHandler->currentSearchMatchIndex();

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
    if (!m_interactionHandler || !m_interactionHandler->linksVisible()) {
        return;
    }

    QVector<PDFLink> links = m_interactionHandler->loadPageLinks(pageIndex);
    if (links.isEmpty()) {
        return;
    }

    const PDFLink* hoveredLink = m_interactionHandler->hoveredLink();

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
    if (!m_interactionHandler || !m_interactionHandler->hasTextSelection()) {
        return;
    }

    const TextSelection& selection = m_interactionHandler->currentTextSelection();

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

    // 连续滚动模式
    if (m_viewHandler->isContinuousScroll() &&
        !m_viewHandler->pageYPositions().isEmpty()) {

        const QVector<int>& positions = m_viewHandler->pageYPositions();
        const QVector<int>& heights = m_viewHandler->pageHeights();

        for (int i = 0; i < positions.size(); ++i) {
            int top = positions[i] + margin;
            int bottom = top + heights[i];

            if (pos.y() >= top && pos.y() <= bottom) {
                double actualZoom = getActualZoom();
                QSizeF pageSize = m_renderer->pageSize(i);
                if (m_viewHandler->rotation() == 90 ||
                    m_viewHandler->rotation() == 270) {
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
        int currentPage = m_viewHandler->currentPage();
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
        if (m_viewHandler->displayMode() == PageDisplayMode::DoublePage &&
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
    // 如果正在进行文本选择
    if (m_isTextSelecting && m_interactionHandler) {
        int pageX, pageY;
        int pageIndex = getPageAtPos(event->pos(), &pageX, &pageY);

        if (pageIndex >= 0) {
            double actualZoom = getActualZoom();
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

    double actualZoom = getActualZoom();
    QPointF pagePos = screenToPageCoord(event->pos(), pageX, pageY);

    const PDFLink* link = nullptr;
    if (m_interactionHandler) {
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

        if (m_renderer->isTextPDF()) {
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
    // 处理链接点击(优先级最高)
    if (event->button() == Qt::LeftButton && m_interactionHandler) {
        const PDFLink* hoveredLink = m_interactionHandler->hoveredLink();
        if (hoveredLink) {
            m_interactionHandler->handleLinkClick(hoveredLink);
            event->accept();
            return;
        }
    }

    // 处理文本选择
    if (event->button() == Qt::LeftButton &&
        m_renderer && m_renderer->isTextPDF() &&
        m_interactionHandler) {

        int pageX, pageY;
        int pageIndex = getPageAtPos(event->pos(), &pageX, &pageY);

        if (pageIndex >= 0) {
            double actualZoom = getActualZoom();
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
    if (!m_renderer || !m_renderer->isDocumentLoaded() || !m_interactionHandler) {
        return;
    }

    QMenu menu(this);

    // 如果有选中的文本
    if (m_interactionHandler->hasTextSelection()) {
        QAction* copyAction = menu.addAction(tr("Copy"));
        copyAction->setShortcut(QKeySequence::Copy);
        connect(copyAction, &QAction::triggered,
                this, &PDFPageWidget::copySelectedText);

        menu.addSeparator();
    }

    // 如果是文本PDF
    if (m_renderer->isTextPDF()) {
        int pageX, pageY;
        int pageIndex = getPageAtPos(event->pos(), &pageX, &pageY);

        if (pageIndex >= 0 && !m_interactionHandler->hasTextSelection()) {
            double actualZoom = getActualZoom();
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
        if (m_interactionHandler && m_interactionHandler->hasTextSelection()) {
            m_interactionHandler->clearTextSelection();
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
