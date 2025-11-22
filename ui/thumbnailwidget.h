#ifndef THUMBNAILWIDGET_H
#define THUMBNAILWIDGET_H

#include <QScrollArea>
#include <QGridLayout>
#include <QLabel>
#include <QMap>

class MuPDFRenderer;
class PDFContentHandler;
class ThumbnailItem;

class ThumbnailWidget : public QScrollArea
{
    Q_OBJECT

public:
    explicit ThumbnailWidget(MuPDFRenderer* renderer, PDFContentHandler* contentHandler, QWidget* parent = nullptr);
    ~ThumbnailWidget();

    void loadThumbnails(int pageCount);
    void clear();
    void highlightCurrentPage(int pageIndex);
    void setThumbnailSize(int width);
    int thumbnailSize() const { return m_thumbnailWidth; }

signals:
    void pageJumpRequested(int pageIndex);
    void loadProgress(int current, int total);

private slots:
    void onThumbnailClicked(int pageIndex);
    void onThumbnailReady(int pageIndex, const QImage& thumbnail);
    void onLoadProgress(int current, int total);
    void onLoadCompleted();

private:
    MuPDFRenderer* m_renderer;
    PDFContentHandler* m_contentHandler;
    QWidget* m_container;
    QGridLayout* m_layout;
    QMap<int, ThumbnailItem*> m_thumbnailItems;

    int m_thumbnailWidth;
    int m_currentPage;
    int m_columnsPerRow;

    static constexpr int DEFAULT_THUMBNAIL_WIDTH = 140;
    static constexpr int THUMBNAIL_SPACING = 16;
};

class ThumbnailItem : public QWidget
{
    Q_OBJECT
public:
    explicit ThumbnailItem(int pageIndex, int width, QWidget* parent = nullptr);

    void setThumbnail(const QImage& image);
    void setHighlight(bool highlight);

    int pageIndex() const { return m_pageIndex; }
    QLabel* imageLabel() const { return m_imageLabel; }

signals:
    void clicked(int pageIndex);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void updateStyle();

    int m_pageIndex;
    int m_width;
    int m_height;

    QWidget* m_imageContainer;
    QLabel* m_imageLabel;
    QLabel* m_pageLabel;

    bool m_isHighlighted;
    bool m_isHovered;

    static constexpr double A4_RATIO = 1.414;
};

#endif // THUMBNAILWIDGET_H
