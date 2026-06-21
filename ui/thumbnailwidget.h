#ifndef THUMBNAILWIDGET_H
#define THUMBNAILWIDGET_H

#include <QScrollArea>
#include <QGridLayout>
#include <QLabel>
#include <QTimer>
#include <QQueue>
#include <QSet>
#include <QMap>
#include <QVector>
#include <QRect>
#include <QImage>

class ThumbnailItem;
class ThumbnailManagerV2;

enum class ScrollState {
    IDLE,
    SLOW_SCROLL,
    FAST_SCROLL,
    FLING
};

class ThumbnailWidget : public QScrollArea
{
    Q_OBJECT

public:
    explicit ThumbnailWidget(QWidget* parent = nullptr);
    ~ThumbnailWidget();

    void initializeThumbnails(int pageCount);
    void clear();
    // 非活动页签只更新高亮，不接管用户正在浏览的滚动位置。
    void highlightCurrentPage(int pageIndex, bool ensureVisible = false);
    void setThumbnailSize(int width);

    void setThumbnailManager(ThumbnailManagerV2* manager);

    QSet<int> getUnloadedVisiblePages() const;

    static constexpr int DEFAULT_THUMBNAIL_WIDTH = 168;
    static constexpr int THUMBNAIL_SPACING = 12;
    static constexpr double A4_RATIO = 1.414;

signals:
    void pageJumpRequested(int pageIndex);
    void visibleRangeChanged(const QSet<int>& visibleIndices, int margin);
    void initialVisibleReady(const QSet<int>& initialVisible);
    void syncLoadRequested(const QSet<int>& unloadedVisible);
    void slowScrollDetected(const QSet<int>& visiblePages);

public slots:
    void onThumbnailLoaded(int pageIndex, const QImage& thumbnail);

protected:
    void scrollContentsBy(int dx, int dy) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onScrollThrottle();
    void onScrollDebounce();
    void onThumbnailClicked(int pageIndex);

private:
    void calculateItemPositions();
    QRect calculateItemRect(int row, int col) const;
    QSet<int> getVisibleIndices(int margin) const;
    ScrollState detectScrollState();
    int getPreloadMargin(ScrollState state) const;
    void notifyVisibleRange();
    bool isLargeLoadMode();

private:
    QWidget* m_container;
    QGridLayout* m_layout;
    QMap<int, ThumbnailItem*> m_thumbnailItems;
    QVector<QRect> m_itemRects;

    int m_thumbnailWidth;
    int m_currentPage;
    int m_columnsPerRow;

    ScrollState m_scrollState;
    QQueue<QPair<int, qint64>> m_scrollHistory;

    QTimer* m_throttleTimer;
    QTimer* m_debounceTimer;

    ThumbnailManagerV2* m_manager;
};

class ThumbnailItem : public QWidget
{
    Q_OBJECT

public:
    explicit ThumbnailItem(int pageIndex, int width, QWidget* parent = nullptr);

    void setPlaceholder(const QString& text);
    void setThumbnail(const QImage& image);
    void setError(const QString& error);
    void setHighlight(bool highlight);

    bool hasImage() const { return m_hasImage; }

signals:
    void clicked(int pageIndex);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void updateStyle();
    QPixmap createRoundedPixmap(const QImage& image);

private:
    int m_pageIndex;
    int m_width;
    int m_height;
    bool m_hasImage;
    bool m_isHighlighted;
    bool m_isHovered;
    QImage m_thumbnailImage;

    QWidget* m_imageContainer;
    QLabel* m_imageLabel;
    QLabel* m_pageLabel;
};

#endif // THUMBNAILWIDGET_H
