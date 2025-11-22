#ifndef PDFPAGEWIDGET_H
#define PDFPAGEWIDGET_H

#include <QWidget>
#include <QImage>
#include <QPoint>
#include <QDateTime>

#include "pdfviewhandler.h"

// Forward declarations
class PDFDocumentSession;  // 新增:从Session获取所有组件
class MuPDFRenderer;
class PageCacheManager;

class PDFInteractionHandler;
class QScrollArea;
struct SearchResult;
struct PDFLink;
struct TextSelection;

enum class ZoomMode;
enum class PageDisplayMode;
enum class SelectionMode;

class PDFPageWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PDFPageWidget(PDFDocumentSession* session, QWidget* parent = nullptr);
    ~PDFPageWidget();

    // 页面导航
    int currentPage() const { return m_viewHandler ? m_viewHandler->currentPage() : 0; }
    void setCurrentPage(int pageIndex, bool adjustForDoublePageMode = true) {
        if (m_viewHandler) m_viewHandler->setCurrentPage(pageIndex, adjustForDoublePageMode);
    }
    void previousPage() { if (m_viewHandler) m_viewHandler->previousPage(); }
    void nextPage() { if (m_viewHandler) m_viewHandler->nextPage(); }
    void firstPage() { if (m_viewHandler) m_viewHandler->firstPage(); }
    void lastPage() { if (m_viewHandler) m_viewHandler->lastPage(); }

    // 缩放控制
    double zoom() const { return m_viewHandler ? m_viewHandler->zoom() : 1.0; }
    void setZoom(double zoom);
    void zoomIn() { if (m_viewHandler) m_viewHandler->zoomIn(); }
    void zoomOut() { if (m_viewHandler) m_viewHandler->zoomOut(); }

    ZoomMode zoomMode() const {return m_viewHandler->zoomMode();};
    void setZoomMode(ZoomMode mode);
    void updateZoom();
    double getActualZoom() const;

    // 旋转控制
    int rotation() const { return m_viewHandler ? m_viewHandler->rotation() : 0; }
    void setRotation(int rotation);

    // 显示模式
    PageDisplayMode displayMode() const;
    void setDisplayMode(PageDisplayMode mode);
    bool isContinuousScroll() const {return m_viewHandler->isContinuousScroll();};
    void setContinuousScroll(bool continuous);

    // 其他
    void refresh();
    QSize sizeHint() const override;
    QString getCacheStatistics() const;

    // 交互功能(委托)
    void setLinksVisible(bool enabled);
    void copySelectedText();
    void selectAll();

    // 连续滚动
    void updateCurrentPageFromScroll(int scrollY);
    void refreshVisiblePages();

signals:
    void pageChanged(int pageIndex);
    void zoomChanged(double zoom);
    void displayModeChanged(PageDisplayMode mode);
    void continuousScrollChanged(bool continuous);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void onPageChangedFromHandler(int pageIndex);
    void onZoomChangedFromHandler(double zoom);
    void onDisplayModeChanged(PageDisplayMode mode);

private:
    void renderCurrentPage();
    QImage renderSinglePage(int pageIndex, double zoom);
    void renderVisiblePages(const QRect& visibleRect);

    void paintSinglePageMode(QPainter& painter);
    void paintDoublePageMode(QPainter& painter);
    void paintContinuousMode(QPainter& painter, const QRect& visibleRect);

    void drawPageImage(QPainter& painter, const QImage& image, int x, int y);
    void drawPagePlaceholder(QPainter& painter, const QRect& rect, int pageIndex);
    void drawSearchHighlights(QPainter& painter, int pageIndex, int pageX, int pageY, double zoom);
    void drawLinkAreas(QPainter& painter, int pageIndex, int pageX, int pageY, double zoom);
    void drawTextSelection(QPainter& painter, int pageIndex, int pageX, int pageY, double zoom);

    QPointF screenToPageCoord(const QPoint& screenPos, int pageX, int pageY) const;
    int getPageAtPos(const QPoint& pos, int* pageX, int* pageY) const;

    QScrollArea* getScrollArea() const;
    QSize getViewportSize() const;

private:
    // Session引用(不拥有所有权)
    PDFDocumentSession* m_session;

    // 组件引用(从Session获取,不拥有所有权)
    MuPDFRenderer* m_renderer;
    PDFViewHandler* m_viewHandler;
    PageCacheManager* m_cacheManager;
    PDFInteractionHandler* m_interactionHandler;

    // 渲染缓存
    QImage m_currentImage;
    QImage m_secondImage;

    // 交互状态
    bool m_isTextSelecting;
    QPoint m_lastMousePos;

    // 多击检测
    int m_clickCount;
    qint64 m_lastClickTime;
    QPoint m_lastClickPos;
};

#endif // PDFPAGEWIDGET_H
