#include "pdfviewhandler.h"
#include "mupdfrenderer.h"
#include "appconfig.h"
#include <QDebug>
#include <algorithm>

PDFViewHandler::PDFViewHandler(MuPDFRenderer* renderer, QObject* parent)
    : QObject(parent)
    , m_renderer(renderer)
    , m_currentPage(0)
    , m_zoom(DEFAULT_ZOOM)
    , m_zoomMode(ZoomMode::FitWidth)
    , m_displayMode(PageDisplayMode::SinglePage)
    , m_continuousScroll(false)
    , m_rotation(0)
{
}

PDFViewHandler::~PDFViewHandler()
{
}

// ==================== 导航相关 ====================

void PDFViewHandler::setCurrentPage(int pageIndex, bool adjustForDoublePageMode)
{
    if (!m_renderer || !m_renderer->isDocumentLoaded()) {
        return;
    }

    int pageCount = m_renderer->pageCount();
    if (pageIndex < 0 || pageIndex >= pageCount) {
        return;
    }

    // 双页模式：调整到双页起始位置
    if (adjustForDoublePageMode &&
        m_displayMode == PageDisplayMode::DoublePage &&
        !m_continuousScroll) {
        pageIndex = getDoublePageStartIndex(pageIndex);
    }

    if (m_currentPage != pageIndex) {
        m_currentPage = pageIndex;
        emit pageChanged(m_currentPage);
    }
}

void PDFViewHandler::previousPage()
{
    int prevPage = getPreviousPageIndex();
    if (prevPage >= 0) {
        setCurrentPage(prevPage, false);
    }
}

void PDFViewHandler::nextPage()
{
    if (!m_renderer || !m_renderer->isDocumentLoaded()) {
        return;
    }

    int nextPage = getNextPageIndex();
    int pageCount = m_renderer->pageCount();

    if (nextPage < pageCount) {
        setCurrentPage(nextPage, false);
    }
}

void PDFViewHandler::firstPage()
{
    setCurrentPage(0, true);
}

void PDFViewHandler::lastPage()
{
    if (!m_renderer || !m_renderer->isDocumentLoaded()) {
        return;
    }

    int lastPage = m_renderer->pageCount() - 1;
    setCurrentPage(lastPage, true);
}

int PDFViewHandler::getPreviousPageIndex() const
{
    if (m_displayMode == PageDisplayMode::DoublePage && !m_continuousScroll) {
        // 双页模式：跳2页
        return m_currentPage - 2;
    }
    // 单页模式：跳1页
    return m_currentPage - 1;
}

int PDFViewHandler::getNextPageIndex() const
{
    if (m_displayMode == PageDisplayMode::DoublePage && !m_continuousScroll) {
        // 双页模式：跳2页
        return m_currentPage + 2;
    }
    // 单页模式：跳1页
    return m_currentPage + 1;
}

int PDFViewHandler::getDoublePageStartIndex(int pageIndex) const
{
    // 双页模式：返回偶数页起始位置（0,2,4,6...）
    return (pageIndex / 2) * 2;
}

// ==================== 缩放相关 ====================

void PDFViewHandler::setZoom(double zoom)
{
    zoom = clampZoom(zoom);

    if (qAbs(m_zoom - zoom) > 0.001) {
        m_zoom = zoom;
        m_zoomMode = ZoomMode::Custom;
        emit zoomChanged(m_zoom);
        emit zoomModeChanged(m_zoomMode);
    }
}

void PDFViewHandler::setZoomMode(ZoomMode mode)
{
    if (m_zoomMode != mode) {
        m_zoomMode = mode;
        emit zoomModeChanged(m_zoomMode);
        // 注意：不在这里计算缩放，由调用者根据viewport大小调用updateZoom
    }
}

void PDFViewHandler::zoomIn()
{
    setZoom(m_zoom + ZOOM_STEP);
}

void PDFViewHandler::zoomOut()
{
    setZoom(m_zoom - ZOOM_STEP);
}

double PDFViewHandler::calculateActualZoom(const QSize& viewportSize) const
{
    double actualZoom = m_zoom;

    if (m_zoomMode == ZoomMode::FitPage) {
        actualZoom = calculateFitPageZoom(viewportSize);
    } else if (m_zoomMode == ZoomMode::FitWidth) {
        actualZoom = calculateFitWidthZoom(viewportSize);
    }

    return clampZoom(actualZoom);
}

double PDFViewHandler::calculateFitPageZoom(const QSize& viewportSize) const
{
    if (!m_renderer || !m_renderer->isDocumentLoaded()) {
        return DEFAULT_ZOOM;
    }

    QSizeF pageSize = m_renderer->pageSize(m_currentPage);
    if (pageSize.isEmpty()) {
        return DEFAULT_ZOOM;
    }

    // 考虑旋转
    if (m_rotation == 90 || m_rotation == 270) {
        pageSize.transpose();
    }

    const int margin = AppConfig::PAGE_MARGIN;
    int availableWidth = viewportSize.width() - 2 * margin;
    int availableHeight = viewportSize.height() - 2 * margin;

    if (availableWidth <= 0 || availableHeight <= 0) {
        return DEFAULT_ZOOM;
    }

    double widthZoom = availableWidth / pageSize.width();
    double heightZoom = availableHeight / pageSize.height();

    return std::min(widthZoom, heightZoom);
}

double PDFViewHandler::calculateFitWidthZoom(const QSize& viewportSize) const
{
    if (!m_renderer || !m_renderer->isDocumentLoaded()) {
        return DEFAULT_ZOOM;
    }

    QSizeF pageSize = m_renderer->pageSize(m_currentPage);
    if (pageSize.isEmpty()) {
        return DEFAULT_ZOOM;
    }

    // 考虑旋转
    if (m_rotation == 90 || m_rotation == 270) {
        pageSize.transpose();
    }

    // 双页模式需要考虑两页宽度
    if (m_displayMode == PageDisplayMode::DoublePage) {
        int nextPage = m_currentPage + 1;
        if (nextPage < m_renderer->pageCount()) {
            QSizeF secondPageSize = m_renderer->pageSize(nextPage);
            if (!secondPageSize.isEmpty()) {
                if (m_rotation == 90 || m_rotation == 270) {
                    secondPageSize.transpose();
                }
                pageSize.setWidth(pageSize.width() + secondPageSize.width() +
                                  AppConfig::DOUBLE_PAGE_SPACING);
            }
        }
    }

    const int margin = AppConfig::PAGE_MARGIN;
    int availableWidth = viewportSize.width() - 2 * margin;

    if (availableWidth <= 0) {
        return DEFAULT_ZOOM;
    }

    return availableWidth / pageSize.width();
}

void PDFViewHandler::updateZoom(const QSize& viewportSize)
{
    if (m_zoomMode == ZoomMode::Custom) {
        return;
    }

    double newZoom = calculateActualZoom(viewportSize);
    newZoom = clampZoom(newZoom);

    if (qAbs(m_zoom - newZoom) > 0.001) {
        m_zoom = newZoom;
        emit zoomChanged(m_zoom);
    }
}

// ==================== 显示模式相关 ====================

void PDFViewHandler::setDisplayMode(PageDisplayMode mode)
{
    if (m_displayMode != mode) {
        m_displayMode = mode;
        emit displayModeChanged(m_displayMode);

        // 切换到双页模式时，自动关闭连续滚动并调整页码
        if (mode == PageDisplayMode::DoublePage) {
            if (m_continuousScroll) {
                setContinuousScroll(false);
            }

            int adjustedPage = getDoublePageStartIndex(m_currentPage);
            if (adjustedPage != m_currentPage) {
                setCurrentPage(adjustedPage, false);
            }
        }
    }
}

void PDFViewHandler::setContinuousScroll(bool continuous)
{
    if (m_continuousScroll != continuous) {
        m_continuousScroll = continuous;
        emit continuousScrollChanged(m_continuousScroll);

        // 切换连续滚动模式时清空位置信息
        if (!continuous) {
            m_pageYPositions.clear();
            m_pageHeights.clear();
        }
    }
}

// ==================== 连续滚动相关 ====================

bool PDFViewHandler::calculatePagePositions(double zoom)
{
    if (!m_renderer || !m_renderer->isDocumentLoaded()) {
        return false;
    }

    int pageCount = m_renderer->pageCount();
    m_pageYPositions.clear();
    m_pageYPositions.reserve(pageCount);
    m_pageHeights.clear();
    m_pageHeights.reserve(pageCount);

    const int pageGap = AppConfig::PAGE_GAP;
    int currentY = 0;

    // 只计算位置，不渲染
    for (int i = 0; i < pageCount; ++i) {
        QSizeF pageSize = m_renderer->pageSize(i);

        // 考虑旋转
        if (m_rotation == 90 || m_rotation == 270) {
            pageSize.transpose();
        }

        int height = qRound(pageSize.height() * zoom);

        m_pageYPositions.append(currentY);
        m_pageHeights.append(height);

        currentY += height + pageGap;
    }

    emit pagePositionsCalculated();
    return true;
}

int PDFViewHandler::updateCurrentPageFromScroll(int scrollY, int margin)
{
    if (!m_continuousScroll || m_pageYPositions.isEmpty()) {
        return m_currentPage;
    }

    int adjustedY = scrollY - margin;

    // 找到当前显示的页面
    for (int i = m_pageYPositions.size() - 1; i >= 0; --i) {
        if (adjustedY >= m_pageYPositions[i]) {
            if (m_currentPage != i) {
                m_currentPage = i;
                emit pageChanged(m_currentPage);
            }
            return m_currentPage;
        }
    }

    return m_currentPage;
}

int PDFViewHandler::getScrollPositionForPage(int pageIndex, int margin) const
{
    if (!m_continuousScroll || m_pageYPositions.isEmpty()) {
        return -1;
    }

    if (pageIndex < 0 || pageIndex >= m_pageYPositions.size()) {
        return -1;
    }

    return m_pageYPositions[pageIndex] + margin;
}

QSet<int> PDFViewHandler::getVisiblePages(const QRect& visibleRect,
                                          int preloadMargin,
                                          int margin) const
{
    QSet<int> visiblePages;

    if (m_pageYPositions.isEmpty()) {
        return visiblePages;
    }

    // 扩展可见区域，预加载周围页面
    QRect extended = visibleRect.adjusted(0, -preloadMargin, 0, preloadMargin);

    // 找出可见的页面
    for (int i = 0; i < m_pageYPositions.size(); ++i) {
        int pageTop = m_pageYPositions[i] + margin;
        int pageBottom = pageTop + m_pageHeights[i];

        // 判断页面是否在扩展的可见区域内
        if (pageBottom >= extended.top() && pageTop <= extended.bottom()) {
            visiblePages.insert(i);
        }
    }

    return visiblePages;
}

// ==================== 旋转相关 ====================

void PDFViewHandler::setRotation(int rotation)
{
    // 规范化旋转角度
    rotation = rotation % 360;
    if (rotation < 0) {
        rotation += 360;
    }

    // 只允许90度的倍数
    rotation = (rotation / 90) * 90;

    if (m_rotation != rotation) {
        m_rotation = rotation;
        emit rotationChanged(m_rotation);
    }
}

// ==================== 工具方法 ====================

double PDFViewHandler::clampZoom(double zoom) const
{
    return std::clamp(zoom, MIN_ZOOM, MAX_ZOOM);
}

bool PDFViewHandler::isValidPageIndex(int pageIndex) const
{
    if (!m_renderer || !m_renderer->isDocumentLoaded()) {
        return false;
    }

    return pageIndex >= 0 && pageIndex < m_renderer->pageCount();
}

void PDFViewHandler::reset()
{
    m_currentPage = 0;
    m_zoom = DEFAULT_ZOOM;
    m_zoomMode = ZoomMode::FitWidth;
    m_displayMode = PageDisplayMode::SinglePage;
    m_continuousScroll = false;
    m_rotation = 0;
    m_pageYPositions.clear();
    m_pageHeights.clear();

    emit pageChanged(m_currentPage);
    emit zoomChanged(m_zoom);
    emit zoomModeChanged(m_zoomMode);
    emit displayModeChanged(m_displayMode);
    emit continuousScrollChanged(m_continuousScroll);
    emit rotationChanged(m_rotation);
}
