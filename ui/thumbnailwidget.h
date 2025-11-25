#ifndef THUMBNAILWIDGET_NEW_H
#define THUMBNAILWIDGET_NEW_H

#include <QScrollArea>
#include <QGridLayout>
#include <QLabel>
#include <QMap>
#include <QVector>
#include <QRect>
#include <QTimer>
#include <QQueue>
#include <QPair>
#include <QPointer>

#include "pdfdocumentsession.h"

class MuPDFRenderer;
class ThumbnailManager;
class ThumbnailItem;

/**
 * @brief 滚动状态
 */
enum class ScrollState {
    IDLE,           // 静止
    SLOW_SCROLL,    // 慢速滚动 (< 1000px/s)
    FAST_SCROLL,    // 快速滚动 (1000-3000px/s)
    FLING           // 惯性滑动 (> 3000px/s)
};

/**
 * @brief 智能缩略图面板（双分辨率 + 动态策略）
 *
 * 特点：
 * - 滚动速度检测
 * - 动态预加载边距
 * - 渐进式加载（低清 → 高清）
 * - 节流与防抖
 */
class ThumbnailWidget : public QScrollArea
{
    Q_OBJECT

public:
    explicit ThumbnailWidget(PDFDocumentSession* session,
                             QWidget* parent = nullptr);
    ~ThumbnailWidget();

    /**
     * @brief 加载缩略图（创建占位符）
     */
    void loadThumbnails(int pageCount);

    /**
     * @brief 清空所有缩略图
     */
    void clear();

    /**
     * @brief 高亮显示指定页面
     */
    void highlightCurrentPage(int pageIndex);

    /**
     * @brief 设置缩略图尺寸
     */
    void setThumbnailSize(int width);

    /**
     * @brief 获取缩略图尺寸
     */
    int thumbnailSize() const { return m_thumbnailWidth; }

signals:
    /**
     * @brief 请求跳转到指定页面
     */
    void pageJumpRequested(int pageIndex);



protected:
    void scrollContentsBy(int dx, int dy) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onThumbnailClicked(int pageIndex);
    void onScrollThrottle();      // 节流触发
    void onScrollDebounce();      // 防抖触发

public slots:
    void onThumbnailLoaded(int pageIndex, const QImage& thumbnail, bool isHighRes);

private:
    MuPDFRenderer* m_renderer;
    QPointer<ThumbnailManager> m_thumbnailManager;

    QWidget* m_container;
    QGridLayout* m_layout;
    QMap<int, ThumbnailItem*> m_thumbnailItems;

    // 位置信息
    QVector<QRect> m_itemRects;

    int m_thumbnailWidth;
    int m_currentPage;
    int m_columnsPerRow;

    // 滚动状态
    ScrollState m_scrollState;
    QQueue<QPair<int, qint64>> m_scrollHistory;  // (position, timestamp)

    // 定时器
    QTimer* m_throttleTimer;   // 节流定时器 (30ms)
    QTimer* m_debounceTimer;   // 防抖定时器 (150ms)

    // 配置常量
    static constexpr int DEFAULT_THUMBNAIL_WIDTH = 120;
    static constexpr int THUMBNAIL_SPACING = 16;
    static constexpr double A4_RATIO = 1.414;

    // 内部方法
    void calculateItemPositions();
    QRect calculateItemRect(int row, int col) const;
    QSet<int> getVisibleIndices(int margin) const;

    /**
     * @brief 检测滚动状态
     */
    ScrollState detectScrollState();

    /**
     * @brief 获取动态预加载边距
     */
    int getPreloadMargin(ScrollState state) const;

    /**
     * @brief 加载可见区域缩略图
     */
    void loadVisibleThumbnails(ScrollState state);

    /**
     * @brief 启动后台全文档低清渲染
     */
    void startBackgroundLowResRendering();
};

/**
 * @brief 缩略图项（支持双分辨率渐进显示）
 */
class ThumbnailItem : public QWidget
{
    Q_OBJECT

public:
    explicit ThumbnailItem(int pageIndex, int width, QWidget* parent = nullptr);

    /**
     * @brief 设置缩略图（支持淡入动画）
     */
    void setThumbnail(const QImage& image, bool isHighRes);

    /**
     * @brief 设置占位符状态
     */
    void setPlaceholder(const QString& text);

    /**
     * @brief 设置错误状态
     */
    void setError(const QString& error);

    /**
     * @brief 设置高亮状态
     */
    void setHighlight(bool highlight);

    /**
     * @brief 是否已有图片
     */
    bool hasImage() const { return m_hasImage; }

    /**
     * @brief 是否是高清图片
     */
    bool isHighRes() const { return m_isHighRes; }

    /**
     * @brief 获取页面索引
     */
    int pageIndex() const { return m_pageIndex; }

signals:
    void clicked(int pageIndex);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void updateStyle();
    QPixmap createRoundedPixmap(const QImage& image);

    int m_pageIndex;
    int m_width;
    int m_height;

    QWidget* m_imageContainer;
    QLabel* m_imageLabel;
    QLabel* m_pageLabel;

    bool m_hasImage;
    bool m_isHighRes;
    bool m_isHighlighted;
    bool m_isHovered;

    static constexpr double A4_RATIO = 1.414;
};

#endif // THUMBNAILWIDGET_NEW_H
