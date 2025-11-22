#ifndef PDFVIEWHANDLER_H
#define PDFVIEWHANDLER_H

#include <QObject>
#include <QSize>
#include <QSizeF>
#include <QVector>
#include <QSet>
#include <QRect>

#include "datastructure.h"

class MuPDFRenderer;


/**
 * @brief PDF视图处理器 - 管理导航、缩放、滚动状态
 *
 * 职责：
 * 1. 管理当前页码、缩放级别、显示模式
 * 2. 计算缩放比例（FitWidth/FitPage）
 * 3. 处理连续滚动模式的页面位置计算
 * 4. 提供页面导航逻辑（上一页/下一页/跳转）
 */
class PDFViewHandler : public QObject
{
    Q_OBJECT

public:
    explicit PDFViewHandler(MuPDFRenderer* renderer, QObject* parent = nullptr);
    ~PDFViewHandler();

    // ==================== 导航相关 ====================

    /**
     * @brief 获取当前页码（0-based）
     */
    int currentPage() const { return m_currentPage; }

    /**
     * @brief 设置当前页码
     * @param pageIndex 目标页码（0-based）
     * @param adjustForDoublePageMode 是否自动调整到双页起始位置
     */
    void setCurrentPage(int pageIndex, bool adjustForDoublePageMode = true);

    /**
     * @brief 上一页
     */
    void previousPage();

    /**
     * @brief 下一页
     */
    void nextPage();

    /**
     * @brief 第一页
     */
    void firstPage();

    /**
     * @brief 最后一页
     */
    void lastPage();

    /**
     * @brief 获取上一页索引（考虑显示模式）
     */
    int getPreviousPageIndex() const;

    /**
     * @brief 获取下一页索引（考虑显示模式）
     */
    int getNextPageIndex() const;

    /**
     * @brief 获取双页模式的起始页索引（偶数页）
     */
    int getDoublePageStartIndex(int pageIndex) const;

    // ==================== 缩放相关 ====================

    /**
     * @brief 获取当前缩放比例
     */
    double zoom() const { return m_zoom; }

    /**
     * @brief 获取缩放模式
     */
    ZoomMode zoomMode() const { return m_zoomMode; }

    /**
     * @brief 设置缩放比例
     * @param zoom 目标缩放比例（将被限制在有效范围内）
     */
    void setZoom(double zoom);

    /**
     * @brief 设置缩放模式
     */
    void setZoomMode(ZoomMode mode);

    /**
     * @brief 放大
     */
    void zoomIn();

    /**
     * @brief 缩小
     */
    void zoomOut();

    /**
     * @brief 计算实际缩放比例（考虑FitWidth/FitPage模式）
     * @param viewportSize 视口大小
     */
    double calculateActualZoom(const QSize& viewportSize) const;

    /**
     * @brief 计算适应页面的缩放比例
     * @param viewportSize 视口大小
     */
    double calculateFitPageZoom(const QSize& viewportSize) const;

    /**
     * @brief 计算适应宽度的缩放比例
     * @param viewportSize 视口大小
     */
    double calculateFitWidthZoom(const QSize& viewportSize) const;

    /**
     * @brief 更新缩放（当窗口大小变化时调用）
     * @param viewportSize 新的视口大小
     */
    void updateZoom(const QSize& viewportSize);

    // ==================== 显示模式相关 ====================

    /**
     * @brief 获取显示模式
     */
    PageDisplayMode displayMode() const { return m_displayMode; }

    /**
     * @brief 设置显示模式
     */
    void setDisplayMode(PageDisplayMode mode);

    /**
     * @brief 是否为连续滚动模式
     */
    bool isContinuousScroll() const { return m_continuousScroll; }

    /**
     * @brief 设置连续滚动模式
     */
    void setContinuousScroll(bool continuous);

    // ==================== 连续滚动相关 ====================

    /**
     * @brief 计算连续滚动模式下的页面位置
     * @param zoom 缩放比例
     * @return 是否计算成功
     */
    bool calculatePagePositions(double zoom);

    /**
     * @brief 获取页面Y位置列表
     */
    const QVector<int>& pageYPositions() const { return m_pageYPositions; }

    /**
     * @brief 获取页面高度列表
     */
    const QVector<int>& pageHeights() const { return m_pageHeights; }

    /**
     * @brief 根据滚动位置更新当前页码
     * @param scrollY 滚动条Y位置
     * @param margin 边距
     * @return 当前页码（如果变化则发射信号）
     */
    int updateCurrentPageFromScroll(int scrollY, int margin = 0);

    /**
     * @brief 获取指定页码的滚动目标位置
     * @param pageIndex 页码
     * @param margin 边距
     * @return Y位置（如果页码无效返回-1）
     */
    int getScrollPositionForPage(int pageIndex, int margin = 0) const;

    /**
     * @brief 获取可见页面集合
     * @param visibleRect 可见区域
     * @param preloadMargin 预加载边距
     * @param margin 页面边距
     * @return 可见页面索引集合
     */
    QSet<int> getVisiblePages(const QRect& visibleRect,
                              int preloadMargin = 0,
                              int margin = 0) const;

    // ==================== 旋转相关 ====================

    /**
     * @brief 获取旋转角度
     */
    int rotation() const { return m_rotation; }

    /**
     * @brief 设置旋转角度（0, 90, 180, 270）
     */
    void setRotation(int rotation);

    // ==================== 工具方法 ====================

    /**
     * @brief 限制缩放比例在有效范围内
     */
    double clampZoom(double zoom) const;

    /**
     * @brief 检查页码是否有效
     */
    bool isValidPageIndex(int pageIndex) const;

    /**
     * @brief 重置所有状态
     */
    void reset();

signals:
    /**
     * @brief 当前页码变化
     * @param pageIndex 新的页码
     */
    void pageChanged(int pageIndex);

    /**
     * @brief 缩放比例变化
     * @param zoom 新的缩放比例
     */
    void zoomChanged(double zoom);

    /**
     * @brief 缩放模式变化
     * @param mode 新的缩放模式
     */
    void zoomModeChanged(ZoomMode mode);

    /**
     * @brief 显示模式变化
     * @param mode 新的显示模式
     */
    void displayModeChanged(PageDisplayMode mode);

    /**
     * @brief 连续滚动模式变化
     * @param continuous 是否连续滚动
     */
    void continuousScrollChanged(bool continuous);

    /**
     * @brief 旋转角度变化
     * @param rotation 新的旋转角度
     */
    void rotationChanged(int rotation);

    /**
     * @brief 页面位置计算完成（连续滚动模式）
     */
    void pagePositionsCalculated();

    /**
     * @brief 需要跳转到指定滚动位置
     * @param scrollY 目标Y位置
     */
    void scrollToPositionRequested(int scrollY);


private:
    MuPDFRenderer* m_renderer;

    // 导航状态
    int m_currentPage;

    // 缩放状态
    double m_zoom;
    ZoomMode m_zoomMode;

    // 显示模式
    PageDisplayMode m_displayMode;
    bool m_continuousScroll;

    // 旋转角度
    int m_rotation;

    // 连续滚动位置信息
    QVector<int> m_pageYPositions;  // 每页的Y位置
    QVector<int> m_pageHeights;     // 每页的高度

    // 常量
    static constexpr double DEFAULT_ZOOM = 1.0;
    static constexpr double MIN_ZOOM = 0.25;
    static constexpr double MAX_ZOOM = 4.0;
    static constexpr double ZOOM_STEP = 0.1;
};

#endif // PDFVIEWHANDLER_H
