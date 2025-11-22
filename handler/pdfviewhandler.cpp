#include "pdfviewhandler.h"
#include "mupdfrenderer.h"
#include "appconfig.h"
#include <QDebug>
#include <algorithm>

PDFViewHandler::PDFViewHandler(MuPDFRenderer* renderer, QObject* parent)
    : QObject(parent)
    , m_renderer(renderer)
{
}

PDFViewHandler::~PDFViewHandler()
{
}

// ==================== 导航相关 ====================

void PDFViewHandler::requestGoToPage(int pageIndex,
                                     bool adjustForDoublePageMode,
                                     PageDisplayMode currentDisplayMode,
                                     int currentPage)
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
        currentDisplayMode == PageDisplayMode::DoublePage) {
        pageIndex = getDoublePageStartIndex(pageIndex);
    }

    if (currentPage != pageIndex) {
        emit pageNavigationCompleted(pageIndex);
    }
}

void PDFViewHandler::requestPreviousPage(PageDisplayMode currentDisplayMode,
                                         bool isContinuousScroll,
                                         int currentPage)
{
    int prevPage = getPreviousPageIndex(currentDisplayMode, isContinuousScroll, currentPage);
    if (prevPage >= 0) {
        emit pageNavigationCompleted(prevPage);
    }
}

void PDFViewHandler::requestNextPage(PageDisplayMode currentDisplayMode,
                                     bool isContinuousScroll,
                                     int currentPage,
                                     int pageCount)
{
    if (!m_renderer || !m_renderer->isDocumentLoaded()) {
        return;
    }

    int nextPage = getNextPageIndex(currentDisplayMode, isContinuousScroll, currentPage);

    if (nextPage < pageCount) {
        emit pageNavigationCompleted(nextPage);
    }
}

void PDFViewHandler::requestFirstPage(PageDisplayMode currentDisplayMode)
{
    int firstPage = 0;

    // 双页模式下第一页也是0
    emit pageNavigationCompleted(firstPage);
}

void PDFViewHandler::requestLastPage(PageDisplayMode currentDisplayMode, int pageCount)
{
    if (!m_renderer || !m_renderer->isDocumentLoaded()) {
        return;
    }

    int lastPage = pageCount - 1;

    // 双页模式：调整到起始位置
    if (currentDisplayMode == PageDisplayMode::DoublePage) {
        lastPage = getDoublePageStartIndex(lastPage);
    }

    emit pageNavigationCompleted(lastPage);
}

int PDFViewHandler::getPreviousPageIndex(PageDisplayMode displayMode,
                                         bool continuousScroll,
                                         int currentPage) const
{
    if (displayMode == PageDisplayMode::DoublePage && !continuousScroll) {
        // 双页模式：跳2页
        return currentPage - 2;
    }
    // 单页模式：跳1页
    return currentPage - 1;
}

int PDFViewHandler::getNextPageIndex(PageDisplayMode displayMode,
                                     bool continuousScroll,
                                     int currentPage) const
{
    if (displayMode == PageDisplayMode::DoublePage && !continuousScroll) {
        // 双页模式：跳2页
        return currentPage + 2;
    }
    // 单页模式：跳1页
    return currentPage + 1;
}

int PDFViewHandler::getDoublePageStartIndex(int pageIndex) const
{
    // 双页模式：返回偶数页起始位置（0,2,4,6...）
    return (pageIndex / 2) * 2;
}

// ==================== 缩放相关 ====================

void PDFViewHandler::requestSetZoom(double zoom)
{
    zoom = clampZoom(zoom);
    emit zoomSettingCompleted(zoom, ZoomMode::Custom);
}

void PDFViewHandler::requestSetZoomMode(ZoomMode mode)
{
    // 模式变化，具体zoom值由Session根据viewport计算
    emit zoomSettingCompleted(-1.0, mode); // -1表示需要重新计算
}

void PDFViewHandler::requestZoomIn(double currentZoom)
{
    double newZoom = clampZoom(currentZoom + ZOOM_STEP);
    emit zoomSettingCompleted(newZoom, ZoomMode::Custom);
}

void PDFViewHandler::requestZoomOut(double currentZoom)
{
    double newZoom = clampZoom(currentZoom - ZOOM_STEP);
    emit zoomSettingCompleted(newZoom, ZoomMode::Custom);
}

double PDFViewHandler::calculateActualZoom(const QSize& viewportSize,
                                           ZoomMode zoomMode,
                                           double customZoom,
                                           int currentPage,
                                           PageDisplayMode displayMode,
                                           int rotation) const
{
    double actualZoom = customZoom;

    if (zoomMode == ZoomMode::FitPage) {
        actualZoom = calculateFitPageZoom(viewportSize, currentPage, rotation);
    } else if (zoomMode == ZoomMode::FitWidth) {
        actualZoom = calculateFitWidthZoom(viewportSize, currentPage, displayMode,
                                           rotation, m_renderer->pageCount());
    }

    return clampZoom(actualZoom);
}

double PDFViewHandler::calculateFitPageZoom(const QSize& viewportSize,
                                            int currentPage,
                                            int rotation) const
{
    if (!m_renderer || !m_renderer->isDocumentLoaded()) {
        return DEFAULT_ZOOM;
    }

    QSizeF pageSize = m_renderer->pageSize(currentPage);
    if (pageSize.isEmpty()) {
        return DEFAULT_ZOOM;
    }

    // 考虑旋转
    if (rotation == 90 || rotation == 270) {
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

double PDFViewHandler::calculateFitWidthZoom(const QSize& viewportSize,
                                             int currentPage,
                                             PageDisplayMode displayMode,
                                             int rotation,
                                             int pageCount) const
{
    if (!m_renderer || !m_renderer->isDocumentLoaded()) {
        return DEFAULT_ZOOM;
    }

    QSizeF pageSize = m_renderer->pageSize(currentPage);
    if (pageSize.isEmpty()) {
        return DEFAULT_ZOOM;
    }

    // 考虑旋转
    if (rotation == 90 || rotation == 270) {
        pageSize.transpose();
    }

    // 双页模式需要考虑两页宽度
    if (displayMode == PageDisplayMode::DoublePage) {
        int nextPage = currentPage + 1;
        if (nextPage < pageCount) {
            QSizeF secondPageSize = m_renderer->pageSize(nextPage);
            if (!secondPageSize.isEmpty()) {
                if (rotation == 90 || rotation == 270) {
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

void PDFViewHandler::requestUpdateZoom(const QSize& viewportSize,
                                       ZoomMode zoomMode,
                                       double currentZoom,
                                       int currentPage,
                                       PageDisplayMode displayMode,
                                       int rotation)
{
    if (zoomMode == ZoomMode::Custom) {
        return; // Custom模式不自动更新
    }

    double newZoom = calculateActualZoom(viewportSize, zoomMode, currentZoom,
                                         currentPage, displayMode, rotation);
    newZoom = clampZoom(newZoom);

    if (qAbs(currentZoom - newZoom) > 0.001) {
        emit zoomSettingCompleted(newZoom, zoomMode);
    }
}

// ==================== 显示模式相关 ====================

void PDFViewHandler::requestSetDisplayMode(PageDisplayMode mode,
                                           bool currentContinuousScroll,
                                           int currentPage)
{
    int adjustedPage = currentPage;

    // 切换到双页模式时，自动调整页码
    if (mode == PageDisplayMode::DoublePage) {
        adjustedPage = getDoublePageStartIndex(currentPage);
    }

    emit displayModeSettingCompleted(mode, adjustedPage);
}

void PDFViewHandler::requestSetContinuousScroll(bool continuous)
{
    emit continuousScrollSettingCompleted(continuous);
}

// ==================== 连续滚动相关 ====================

bool PDFViewHandler::calculatePagePositions(double zoom,
                                            int rotation,
                                            int pageCount,
                                            QVector<int>& outPositions,
                                            QVector<int>& outHeights)
{
    if (!m_renderer || !m_renderer->isDocumentLoaded()) {
        return false;
    }

    outPositions.clear();
    outPositions.reserve(pageCount);
    outHeights.clear();
    outHeights.reserve(pageCount);

    const int pageGap = AppConfig::PAGE_GAP;
    int currentY = 0;

    for (int i = 0; i < pageCount; ++i) {
        QSizeF pageSize = m_renderer->pageSize(i);

        // 考虑旋转
        if (rotation == 90 || rotation == 270) {
            pageSize.transpose();
        }

        int height = qRound(pageSize.height() * zoom);

        outPositions.append(currentY);
        outHeights.append(height);

        currentY += height + pageGap;
    }

    emit pagePositionsCalculated(outPositions, outHeights);
    return true;
}

int PDFViewHandler::calculateCurrentPageFromScroll(int scrollY,
                                                   int margin,
                                                   const QVector<int>& pageYPositions) const
{
    if (pageYPositions.isEmpty()) {
        return -1;
    }

    int adjustedY = scrollY - margin;

    // 找到当前显示的页面
    for (int i = pageYPositions.size() - 1; i >= 0; --i) {
        if (adjustedY >= pageYPositions[i]) {
            return i;
        }
    }

    return 0;
}

int PDFViewHandler::getScrollPositionForPage(int pageIndex,
                                             int margin,
                                             const QVector<int>& pageYPositions) const
{
    if (pageYPositions.isEmpty()) {
        return -1;
    }

    if (pageIndex < 0 || pageIndex >= pageYPositions.size()) {
        return -1;
    }

    return pageYPositions[pageIndex] + margin;
}

QSet<int> PDFViewHandler::getVisiblePages(const QRect& visibleRect,
                                          int preloadMargin,
                                          int margin,
                                          const QVector<int>& pageYPositions,
                                          const QVector<int>& pageHeights) const
{
    QSet<int> visiblePages;

    if (pageYPositions.isEmpty()) {
        return visiblePages;
    }

    // 扩展可见区域，预加载周围页面
    QRect extended = visibleRect.adjusted(0, -preloadMargin, 0, preloadMargin);

    // 找出可见的页面
    for (int i = 0; i < pageYPositions.size(); ++i) {
        int pageTop = pageYPositions[i] + margin;
        int pageBottom = pageTop + pageHeights[i];

        // 判断页面是否在扩展的可见区域内
        if (pageBottom >= extended.top() && pageTop <= extended.bottom()) {
            visiblePages.insert(i);
        }
    }

    return visiblePages;
}

// ==================== 旋转相关 ====================

void PDFViewHandler::requestSetRotation(int rotation)
{
    // 规范化旋转角度
    rotation = rotation % 360;
    if (rotation < 0) {
        rotation += 360;
    }

    // 只允许90度的倍数
    rotation = (rotation / 90) * 90;

    emit rotationSettingCompleted(rotation);
}

// ==================== 工具方法 ====================

double PDFViewHandler::clampZoom(double zoom) const
{
    return std::clamp(zoom, MIN_ZOOM, MAX_ZOOM);
}

bool PDFViewHandler::isValidPageIndex(int pageIndex, int pageCount) const
{
    return pageIndex >= 0 && pageIndex < pageCount;
}
