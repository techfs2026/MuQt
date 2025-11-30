#ifndef PDFPAGEWIDGET_H
#define PDFPAGEWIDGET_H

#include <QWidget>
#include <QImage>
#include <QPoint>
#include <QRect>

class PDFDocumentSession;
class PerThreadMuPDFRenderer;
class PageCacheManager;
class QScrollArea;

/**
 * @brief 纯渲染组件 - 只负责显示和基础事件捕获
 *
 * 职责：
 * 1. 绘制PDF页面（单页/双页/连续滚动）
 * 2. 处理鼠标事件并发射信号
 * 3. 提供坐标转换工具方法
 * 4. 管理当前显示的图像缓存
 *
 * 不负责：
 * - 业务逻辑（导航、缩放、搜索等）
 * - 直接调用Session方法
 * - 状态管理
 */
class PDFPageWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PDFPageWidget(PDFDocumentSession* session, QWidget* parent = nullptr);
    ~PDFPageWidget();

    // ==================== 被动更新方法（由Tab调用） ====================

    /**
     * @brief 设置要渲染的图像（由Tab提供已渲染的图像）
     */
    void setDisplayImages(const QImage& primaryImage, const QImage& secondaryImage = QImage());

    /**
     * @brief 刷新连续滚动模式的可见页面
     */
    void refreshVisiblePages();

    /**
     * @brief 启用/禁用文本选择拖拽模式
     */
    void setTextSelectionMode(bool enabled);

    /**
     * @brief 清除所有高亮（选择、搜索等）
     */
    void clearHighlights();

    // ==================== 工具方法 ====================

    /**
     * @brief 屏幕坐标转页面坐标
     */
    QPointF screenToPageCoord(const QPoint& screenPos, int pageX, int pageY) const;

    /**
     * @brief 获取鼠标位置对应的页面索引和偏移
     * @return 页面索引，-1表示不在任何页面上
     */
    int getPageAtPos(const QPoint& pos, int* pageX = nullptr, int* pageY = nullptr) const;

    /**
     * @brief 获取父级滚动区域
     */
    QScrollArea* getScrollArea() const;

    /**
     * @brief 获取视口大小
     */
    QSize getViewportSize() const;

    // ==================== 查询方法 ====================

    QString getCacheStatistics() const;

    QSize calculateRequiredSize() const;

signals:
    // ==================== 用户交互信号（发给Tab处理） ====================

    /**
     * @brief 页面被点击
     * @param pageIndex 页面索引
     * @param pagePos 页面内坐标
     * @param button 鼠标按钮
     * @param modifiers 键盘修饰键
     */
    void pageClicked(int pageIndex, const QPointF& pagePos, Qt::MouseButton button, Qt::KeyboardModifiers modifiers);

    /**
     * @brief 鼠标在页面上移动
     */
    void mouseMovedOnPage(int pageIndex, const QPointF& pagePos);

    /**
     * @brief 鼠标离开所有页面
     */
    void mouseLeftAllPages();

    /**
     * @brief 文本选择拖拽中
     */
    void textSelectionDragging(int pageIndex, const QPointF& pagePos);

    /**
     * @brief 文本选择结束
     */
    void textSelectionEnded();

    /**
     * @brief 请求显示右键菜单
     */
    void contextMenuRequested(int pageIndex, const QPointF& pagePos, const QPoint& globalPos);

    /**
     * @brief 请求渲染（在连续滚动模式下滚动时）
     */
    void visibleAreaChanged();

protected:
    // ==================== 事件处理 ====================

    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

    QSize sizeHint() const override;

private:
    // ==================== 绘制辅助方法 ====================

    void paintSinglePageMode(QPainter& painter);
    void paintDoublePageMode(QPainter& painter);
    void paintContinuousMode(QPainter& painter, const QRect& visibleRect);

    void drawPageImage(QPainter& painter, const QImage& image, int x, int y);
    void drawPagePlaceholder(QPainter& painter, const QRect& rect, int pageIndex);

    // 绘制叠加层（高亮、链接等）
    void drawOverlays(QPainter& painter, int pageIndex, int pageX, int pageY, double zoom);
    void drawSearchHighlights(QPainter& painter, int pageIndex, int pageX, int pageY, double zoom);
    void drawLinkAreas(QPainter& painter, int pageIndex, int pageX, int pageY, double zoom);
    void drawTextSelection(QPainter& painter, int pageIndex, int pageX, int pageY, double zoom);

private:
    // 核心引用（不拥有所有权）
    PDFDocumentSession* m_session;
    PerThreadMuPDFRenderer* m_renderer;
    PageCacheManager* m_cacheManager;

    // 当前显示的图像
    QImage m_currentImage;   // 主页面图像
    QImage m_secondImage;    // 双页模式的第二页

    // 交互状态
    bool m_isTextSelecting;  // 是否正在进行文本选择拖拽
    QPoint m_dragStartPos;   // 拖拽起始位置
};

#endif // PDFPAGEWIDGET_H
