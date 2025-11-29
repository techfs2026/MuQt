#ifndef PDFVIEWHANDLER_H
#define PDFVIEWHANDLER_H

#include <QObject>
#include <QSize>
#include <QSizeF>
#include <QVector>
#include <QSet>
#include <QRect>

#include "datastructure.h"

class ThreadSafeRenderer;

/**
 * @brief PDF视图处理器 - 处理视图相关的计算和逻辑
 *
 * 职责：
 * 1. 执行页面导航逻辑
 * 2. 计算缩放比例（FitWidth/FitPage）
 * 3. 处理连续滚动模式的页面位置计算
 * 4. 不再存储最终状态，只处理逻辑和发出完成信号
 *
 * 注意：状态由PDFDocumentState管理
 */
class PDFViewHandler : public QObject
{
    Q_OBJECT

public:
    explicit PDFViewHandler(ThreadSafeRenderer* renderer, QObject* parent = nullptr);
    ~PDFViewHandler();

    // ==================== 导航相关 ====================

    /**
     * @brief 请求跳转到指定页码
     * @param pageIndex 目标页码（0-based）
     * @param adjustForDoublePageMode 是否自动调整到双页起始位置
     * @param currentDisplayMode 当前显示模式
     * @param currentPage 当前页码
     */
    void requestGoToPage(int pageIndex,
                         bool adjustForDoublePageMode,
                         PageDisplayMode currentDisplayMode,
                         int currentPage);

    /**
     * @brief 请求上一页
     */
    void requestPreviousPage(PageDisplayMode currentDisplayMode,
                             bool isContinuousScroll,
                             int currentPage);

    /**
     * @brief 请求下一页
     */
    void requestNextPage(PageDisplayMode currentDisplayMode,
                         bool isContinuousScroll,
                         int currentPage,
                         int pageCount);

    /**
     * @brief 请求第一页
     */
    void requestFirstPage(PageDisplayMode currentDisplayMode);

    /**
     * @brief 请求最后一页
     */
    void requestLastPage(PageDisplayMode currentDisplayMode, int pageCount);

    /**
     * @brief 获取上一页索引（考虑显示模式）
     */
    int getPreviousPageIndex(PageDisplayMode displayMode,
                             bool continuousScroll,
                             int currentPage) const;

    /**
     * @brief 获取下一页索引（考虑显示模式）
     */
    int getNextPageIndex(PageDisplayMode displayMode,
                         bool continuousScroll,
                         int currentPage) const;

    /**
     * @brief 获取双页模式的起始页索引（偶数页）
     */
    int getDoublePageStartIndex(int pageIndex) const;

    // ==================== 缩放相关 ====================

    /**
     * @brief 请求设置缩放比例
     * @param zoom 目标缩放比例
     */
    void requestSetZoom(double zoom);

    /**
     * @brief 请求设置缩放模式
     */
    void requestSetZoomMode(ZoomMode mode);

    /**
     * @brief 请求放大
     */
    void requestZoomIn(double currentZoom);

    /**
     * @brief 请求缩小
     */
    void requestZoomOut(double currentZoom);

    /**
     * @brief 计算实际缩放比例（考虑FitWidth/FitPage模式）
     */
    double calculateActualZoom(const QSize& viewportSize,
                               ZoomMode zoomMode,
                               double customZoom,
                               int currentPage,
                               PageDisplayMode displayMode,
                               int rotation) const;

    /**
     * @brief 计算适应页面的缩放比例
     */
    double calculateFitPageZoom(const QSize& viewportSize,
                                int currentPage,
                                int rotation) const;

    /**
     * @brief 计算适应宽度的缩放比例
     */
    double calculateFitWidthZoom(const QSize& viewportSize,
                                 int currentPage,
                                 PageDisplayMode displayMode,
                                 int rotation,
                                 int pageCount) const;

    /**
     * @brief 请求更新缩放（当窗口大小变化时调用）
     */
    void requestUpdateZoom(const QSize& viewportSize,
                           ZoomMode zoomMode,
                           double currentZoom,
                           int currentPage,
                           PageDisplayMode displayMode,
                           int rotation);

    // ==================== 显示模式相关 ====================

    /**
     * @brief 请求设置显示模式
     */
    void requestSetDisplayMode(PageDisplayMode mode,
                               bool currentContinuousScroll,
                               int currentPage);

    /**
     * @brief 请求设置连续滚动模式
     */
    void requestSetContinuousScroll(bool continuous);

    // ==================== 连续滚动相关 ====================

    /**
     * @brief 计算连续滚动模式下的页面位置
     * @return 成功返回true，失败返回false
     */
    bool calculatePagePositions(double zoom,
                                int rotation,
                                int pageCount,
                                QVector<int>& outPositions,
                                QVector<int>& outHeights);

    /**
     * @brief 根据滚动位置计算当前页码
     */
    int calculateCurrentPageFromScroll(int scrollY,
                                       int margin,
                                       const QVector<int>& pageYPositions) const;

    /**
     * @brief 获取指定页码的滚动目标位置
     */
    int getScrollPositionForPage(int pageIndex,
                                 int margin,
                                 const QVector<int>& pageYPositions) const;

    /**
     * @brief 获取可见页面集合
     */
    QSet<int> getVisiblePages(const QRect& visibleRect,
                              int preloadMargin,
                              int margin,
                              const QVector<int>& pageYPositions,
                              const QVector<int>& pageHeights) const;

    // ==================== 旋转相关 ====================

    /**
     * @brief 请求设置旋转角度（0, 90, 180, 270）
     */
    void requestSetRotation(int rotation);

    // ==================== 工具方法 ====================

    /**
     * @brief 限制缩放比例在有效范围内
     */
    double clampZoom(double zoom) const;

    /**
     * @brief 检查页码是否有效
     */
    bool isValidPageIndex(int pageIndex, int pageCount) const;

signals:
    // ==================== 导航完成信号 ====================

    /**
     * @brief 页面导航完成
     * @param newPageIndex 新的页码
     */
    void pageNavigationCompleted(int newPageIndex);

    // ==================== 缩放完成信号 ====================

    /**
     * @brief 缩放设置完成
     * @param newZoom 新的缩放比例
     * @param newMode 新的缩放模式
     */
    void zoomSettingCompleted(double newZoom, ZoomMode newMode);

    // ==================== 显示模式完成信号 ====================

    /**
     * @brief 显示模式设置完成
     * @param newMode 新的显示模式
     * @param adjustedPage 调整后的页码（双页模式下可能会调整）
     */
    void displayModeSettingCompleted(PageDisplayMode newMode, int adjustedPage);

    /**
     * @brief 连续滚动模式设置完成
     * @param continuous 是否连续滚动
     */
    void continuousScrollSettingCompleted(bool continuous);

    // ==================== 旋转完成信号 ====================

    /**
     * @brief 旋转设置完成
     * @param newRotation 新的旋转角度
     */
    void rotationSettingCompleted(int newRotation);

    // ==================== 页面位置计算完成信号 ====================

    /**
     * @brief 页面位置计算完成
     * @param positions Y位置列表
     * @param heights 高度列表
     */
    void pagePositionsCalculated(const QVector<int>& positions,
                                 const QVector<int>& heights);

    /**
     * @brief 需要滚动到指定位置
     * @param scrollY 目标Y位置
     */
    void scrollToPositionRequested(int scrollY);

    /**
     * @brief 当前页从滚动位置更新完成
     * @param newPageIndex 新的页码
     */
    void currentPageUpdatedFromScroll(int newPageIndex);

private:
    ThreadSafeRenderer* m_renderer;

    // 常量
    static constexpr double DEFAULT_ZOOM = 1.0;
    static constexpr double MIN_ZOOM = 0.25;
    static constexpr double MAX_ZOOM = 4.0;
    static constexpr double ZOOM_STEP = 0.1;
};

#endif // PDFVIEWHANDLER_H
