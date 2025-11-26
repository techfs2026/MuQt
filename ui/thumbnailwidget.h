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

class ThumbnailItem;

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
    void highlightCurrentPage(int pageIndex);
    void setThumbnailSize(int width);

    static constexpr int DEFAULT_THUMBNAIL_WIDTH = 120;
    static constexpr int THUMBNAIL_SPACING = 12;
    static constexpr double A4_RATIO = 1.414;

signals:
    void pageJumpRequested(int pageIndex);
    void visibleRangeChanged(const QSet<int>& visibleIndices, int margin);
    void initialVisibleReady(const QSet<int>& initialVisible);
    void syncLoadRequested(const QSet<int>& unloadedVisible);

public slots:
    void onThumbnailLoaded(int pageIndex, const QImage& thumbnail, bool isHighRes);

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
    QSet<int> getUnloadedVisiblePages() const;

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
};

class ThumbnailItem : public QWidget
{
    Q_OBJECT

public:
    explicit ThumbnailItem(int pageIndex, int width, QWidget* parent = nullptr);

    void setPlaceholder(const QString& text);
    void setThumbnail(const QImage& image, bool isHighRes);
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
    bool m_isHighRes;
    bool m_isHighlighted;
    bool m_isHovered;

    QWidget* m_imageContainer;
    QLabel* m_imageLabel;
    QLabel* m_pageLabel;
};

#endif // THUMBNAILWIDGET_H
