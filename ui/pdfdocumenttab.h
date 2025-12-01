#ifndef PDFDOCUMENTTAB_H
#define PDFDOCUMENTTAB_H

#include <QWidget>
#include <QDateTime>

#include "datastructure.h"
#include "navigationpanel.h"

class PDFDocumentSession;
class PDFPageWidget;
class SearchWidget;
class QScrollArea;
class QSplitter;
class QProgressBar;

/**
 * @brief PDF文档标签页 - UI协调层
 *
 * 职责：
 * 1. 管理UI组件（PageWidget、NavigationPanel、SearchWidget等）
 * 2. 接收用户操作（工具栏、快捷键、PageWidget事件）
 * 3. 调用Session业务方法
 * 4. 接收Session状态变化信号，协调UI更新
 * 5. 处理复杂的用户交互逻辑（多击检测、右键菜单等）
 */
class PDFDocumentTab : public QWidget
{
    Q_OBJECT

public:
    explicit PDFDocumentTab(QWidget* parent = nullptr);
    ~PDFDocumentTab();

    // ==================== 文档操作 ====================

    bool loadDocument(const QString& filePath, QString* errorMessage = nullptr);
    void closeDocument();
    bool isDocumentLoaded() const;
    QString documentPath() const;
    QString documentTitle() const;

    // ==================== 导航操作（委托给Session） ====================

    void previousPage();
    void nextPage();
    void firstPage();
    void lastPage();
    void goToPage(int pageIndex);

    // ==================== 缩放操作 ====================

    void zoomIn();
    void zoomOut();
    void actualSize();
    void fitPage();
    void fitWidth();
    void setZoom(double zoom);

    // ==================== 视图操作 ====================

    void setDisplayMode(PageDisplayMode mode);
    void setContinuousScroll(bool continuous);

    // ==================== 搜索操作 ====================

    void showSearchBar();
    void hideSearchBar();
    bool isSearchBarVisible() const;

    // ==================== 文本操作 ====================

    void copySelectedText();
    void selectAll();

    // ==================== 链接操作 ====================

    void setLinksVisible(bool visible);
    bool linksVisible() const;

    // ==================== 状态查询 ====================

    int currentPage() const;
    int pageCount() const;
    double zoom() const;
    ZoomMode zoomMode() const;
    PageDisplayMode displayMode() const;
    bool isContinuousScroll() const;
    bool hasTextSelection() const;
    bool isTextPDF() const;

    /**
     * @brief 获取导航面板（可能返回 nullptr）
     */
    NavigationPanel* navigationPanel() const { return m_navigationPanel; }

    /**
     * @brief 获取搜索组件（可能返回 nullptr）
     */
    SearchWidget* searchWidget() const { return m_searchWidget; }

    /**
     * @brief 获取视口大小（用于缩放计算）
     */
    QSize getViewportSize() const;

    /**
     * @brief 手动触发缩放更新（窗口大小改变时）
     */
    void updateZoom(const QSize& viewportSize);

    /**
     * @brief 查找下一个匹配
     */
    void findNext();

    /**
     * @brief 查找上一个匹配
     */
    void findPrevious();

    void applyModernStyle();

    void setPaperEffectEnabled(bool enabled);
    bool paperEffectEnabled() const;

signals:

    void documentLoaded(const QString& filePath, int pageCount);
    void documentError(const QString& errorMessage);
    void pageChanged(int pageIndex);
    void zoomChanged(double zoom);
    void displayModeChanged(PageDisplayMode mode);
    void continuousScrollChanged(bool continuous);
    void searchCompleted(const QString& query, int totalMatches);
    void textSelectionChanged();
    void paperEffectChanged(bool enabled);

private slots:

    void onDocumentLoaded(const QString& filePath, int pageCount);
    void onPageChanged(int pageIndex);
    void onZoomChanged(double zoom);
    void onDisplayModeChanged(PageDisplayMode mode);
    void onContinuousScrollChanged(bool continuous);
    void onPagePositionsChanged(const QVector<int>& positions, const QVector<int>& heights);
    void onTextSelectionChanged(bool hasSelection);
    void onTextPreloadProgress(int current, int total);
    void onTextPreloadCompleted();
    void onSearchCompleted(const QString& query, int totalMatches);

    void onPageClicked(int pageIndex, const QPointF& pagePos, Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
    void onMouseMovedOnPage(int pageIndex, const QPointF& pagePos);
    void onMouseLeftAllPages();
    void onTextSelectionDragging(int pageIndex, const QPointF& pagePos);
    void onTextSelectionEnded();
    void onContextMenuRequested(int pageIndex, const QPointF& pagePos, const QPoint& globalPos);
    void onVisibleAreaChanged();

    void onScrollValueChanged(int value);

private:

    void setupUI();
    void setupConnections();

    /**
     * @brief 渲染当前页面并更新PageWidget
     */
    void renderAndUpdatePages();

    /**
     * @brief 渲染单个页面
     */
    QImage renderPage(int pageIndex);

    /**
     * @brief 刷新连续滚动模式的可见页面
     */
    void refreshVisiblePages();

    void updateScrollBarPolicy();
    void updateCursorForPage(int pageIndex, const QPointF& pagePos);
    void showContextMenu(int pageIndex, const QPointF& pagePos, const QPoint& globalPos);

private:
    // 核心组件
    PDFDocumentSession* m_session;
    PDFPageWidget* m_pageWidget;
    NavigationPanel* m_navigationPanel;
    SearchWidget* m_searchWidget;

    // UI组件
    QScrollArea* m_scrollArea;
    QSplitter* m_splitter;
    QProgressBar* m_textPreloadProgress;

    // 交互状态
    qint64 m_lastClickTime;     // 上次点击时间（用于多击检测）
    QPoint m_lastClickPos;      // 上次点击位置
    int m_clickCount;           // 连续点击次数

    // 添加标志位：标记是否是用户主动滚动触发的页面变化
    bool m_isUserScrolling;
};

#endif // PDFDOCUMENTTAB_H
