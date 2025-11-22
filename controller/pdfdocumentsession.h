#ifndef PDFDOCUMENTSESSION_H
#define PDFDOCUMENTSESSION_H

#include <QObject>
#include <QString>
#include <QImage>
#include <memory>

#include "pdfcontenthandler.h"
#include "pdfviewhandler.h"
#include "pdfinteractionhandler.h"
#include "textcachemanager.h"

// Forward declarations
class MuPDFRenderer;
class PageCacheManager;
class OutlineItem;
class OutlineEditor;
struct SearchResult;
struct PDFLink;
struct TextSelection;
enum class ZoomMode;
enum class PageDisplayMode;

/**
 * @brief PDF文档会话 - 单一文档的协调中心
 *
 * 职责:
 * 1. 拥有并管理所有文档相关组件的生命周期
 * 2. 提供统一的外部接口,隐藏内部实现细节
 * 3. 协调各组件间的交互(如缓存更新、状态同步)
 * 4. 转发信号给UI层
 *
 * 设计原则:
 * - 组件所有权集中化(单一职责)
 * - 缓存全局共享(Session级别)
 * - Handler内部封装(最小暴露)
 */
class PDFDocumentSession : public QObject
{
    Q_OBJECT

public:
    explicit PDFDocumentSession(QObject* parent = nullptr);
    ~PDFDocumentSession();

    // ==================== 文档生命周期 ====================

    /**
     * @brief 加载PDF文档
     * @param filePath 文件路径
     * @param errorMessage 错误信息输出
     * @return 成功返回true
     */
    bool loadDocument(const QString& filePath, QString* errorMessage = nullptr);

    /**
     * @brief 关闭当前文档
     */
    void closeDocument();

    /**
     * @brief 是否已加载文档
     */
    bool isDocumentLoaded() const;

    /**
     * @brief 获取文档路径
     */
    QString documentPath() const;

    /**
     * @brief 获取文档页数
     */
    int pageCount() const;

    /**
     * @brief 检测PDF类型(文本/扫描)
     */
    bool isTextPDF(int samplePages = 5) const;

    // ==================== Handler访问器 ====================

    /**
     * @brief 获取视图处理器(导航/缩放/滚动)
     */
    PDFViewHandler* viewHandler() const { return m_viewHandler.get(); }

    /**
     * @brief 获取内容处理器(大纲/缩略图)
     */
    PDFContentHandler* contentHandler() const { return m_contentHandler.get(); }

    /**
     * @brief 获取交互处理器(搜索/链接/选择)
     */
    PDFInteractionHandler* interactionHandler() const { return m_interactionHandler.get(); }

    /**
     * @brief 获取大纲编辑器
     */
    OutlineEditor* outlineEditor() const;

    // ==================== 核心组件访问 ====================

    /**
     * @brief 获取渲染器(谨慎使用)
     */
    MuPDFRenderer* renderer() const { return m_renderer.get(); }

    /**
     * @brief 获取页面缓存管理器
     */
    PageCacheManager* pageCache() const { return m_pageCache.get(); }

    /**
     * @brief 获取文本缓存管理器
     */
    TextCacheManager* textCache() const { return m_textCache.get(); }

    // ==================== 便捷方法(委托给Handler) ====================

    // --- 导航相关 ---
    int currentPage() const;
    void setCurrentPage(int pageIndex, bool adjustForDoublePageMode = true);
    void previousPage();
    void nextPage();
    void firstPage();
    void lastPage();

    // --- 缩放相关 ---
    double zoom() const;
    void setZoom(double zoom);
    ZoomMode zoomMode() const;
    void setZoomMode(ZoomMode mode);
    void zoomIn();
    void zoomOut();
    void actualSize();
    void fitPage();
    void fitWidth();
    void updateZoom(const QSize& viewportSize);

    // --- 显示模式 ---
    PageDisplayMode displayMode() const;
    void setDisplayMode(PageDisplayMode mode);
    bool isContinuousScroll() const;
    void setContinuousScroll(bool continuous);

    // --- 内容管理 ---
    bool loadOutline();
    OutlineItem* outlineRoot() const;
    void startLoadThumbnails(int thumbnailWidth = 120);
    void cancelThumbnailLoading();
    QImage getThumbnail(int pageIndex) const;

    // --- 搜索功能 ---
    void startSearch(const QString& query, bool caseSensitive = false,
                     bool wholeWords = false, int startPage = 0);
    void cancelSearch();
    bool isSearching() const;
    SearchResult findNext();
    SearchResult findPrevious();
    int totalSearchMatches() const;
    int currentSearchMatchIndex() const;

    // --- 文本选择 ---
    void startTextSelection(int pageIndex, const QPointF& pagePos, double zoom);
    void updateTextSelection(int pageIndex, const QPointF& pagePos, double zoom);
    void extendTextSelection(int pageIndex, const QPointF& pagePos, double zoom);
    void endTextSelection();
    void clearTextSelection();
    void selectWord(int pageIndex, const QPointF& pagePos, double zoom);
    void selectLine(int pageIndex, const QPointF& pagePos, double zoom);
    void selectAll(int pageIndex);
    bool hasTextSelection() const;
    QString selectedText() const;
    void copySelectedText();

    // --- 链接处理 ---
    void setLinksVisible(bool visible);
    bool linksVisible() const;
    const PDFLink* hitTestLink(int pageIndex, const QPointF& pagePos, double zoom);
    void clearHoveredLink();
    bool handleLinkClick(const PDFLink* link);

    // ==================== 统计信息 ====================

    /**
     * @brief 获取缓存统计
     */
    QString getCacheStatistics() const;

    /**
     * @brief 获取文本缓存统计
     */
    QString getTextCacheStatistics() const;

signals:
    // --- 文档信号 ---
    void documentLoaded(const QString& filePath, int pageCount);
    void documentClosed();
    void documentError(const QString& error);

    // --- 导航信号 ---
    void pageChanged(int pageIndex);
    void zoomChanged(double zoom);
    void zoomModeChanged(ZoomMode mode);
    void displayModeChanged(PageDisplayMode mode);
    void continuousScrollChanged(bool continuous);

    // --- 内容信号 ---
    void outlineLoaded(bool success, int itemCount);
    void thumbnailLoadStarted(int totalPages);
    void thumbnailLoadProgress(int loaded, int total);
    void thumbnailReady(int pageIndex, const QImage& thumbnail);
    void thumbnailLoadCompleted();

    // --- 搜索信号 ---
    void searchProgress(int currentPage, int totalPages, int matchCount);
    void searchCompleted(const QString& query, int totalMatches);
    void searchCancelled();

    // --- 交互信号 ---
    void textSelectionChanged();
    void textCopied(int characterCount);
    void linkHovered(const PDFLink* link);
    void internalLinkRequested(int targetPage);
    void externalLinkRequested(const QString& uri);

    // --- 文本预加载信号 ---
    void textPreloadProgress(int current, int total);
    void textPreloadCompleted();
    void textPreloadCancelled();

private:
    void setupConnections();

private:
    // 核心组件
    std::unique_ptr<MuPDFRenderer> m_renderer;

    // 缓存管理(Session级别,全局共享)
    std::unique_ptr<PageCacheManager> m_pageCache;
    std::unique_ptr<TextCacheManager> m_textCache;

    // Handler(封装业务逻辑)
    std::unique_ptr<PDFViewHandler> m_viewHandler;
    std::unique_ptr<PDFContentHandler> m_contentHandler;
    std::unique_ptr<PDFInteractionHandler> m_interactionHandler;

    // 状态
    QString m_currentFilePath;
    bool m_isTextPDF;
};

#endif // PDFDOCUMENTSESSION_H
