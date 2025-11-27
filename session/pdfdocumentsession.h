#ifndef PDFDOCUMENTSESSION_H
#define PDFDOCUMENTSESSION_H

#include <QObject>
#include <QString>
#include <QImage>
#include <QSize>
#include <QPointF>
#include <memory>

#include "datastructure.h"
#include "textcachemanager.h"
#include "pdfcontenthandler.h"
#include "pdfdocumentstate.h"

// Forward declarations
class MuPDFRenderer;
class PageCacheManager;
class PDFViewHandler;
class PDFContentHandler;
class PDFInteractionHandler;
class PDFDocumentState;
class OutlineItem;
class OutlineEditor;
struct SearchResult;
struct PDFLink;

/**
 * @brief PDF文档会话 - 协调所有Handler和State
 *
 * 架构说明：
 * 1. Handler负责业务逻辑，发出xxxCompleted信号
 * 2. Session监听Handler信号，更新State，发出xxxChanged信号
 * 3. UI监听Session的xxxChanged信号进行界面更新
 *
 * 信号流向：Handler → Session → State → UI
 */
class PDFDocumentSession : public QObject
{
    Q_OBJECT

public:
    explicit PDFDocumentSession(QObject* parent = nullptr);
    ~PDFDocumentSession();

    // ==================== 核心组件访问 ====================

    MuPDFRenderer* renderer() const { return m_renderer.get(); }
    PageCacheManager* pageCache() const { return m_pageCache.get(); }
    TextCacheManager* textCache() const { return m_textCache.get(); }

    PDFViewHandler* viewHandler() const { return m_viewHandler.get(); }
    PDFContentHandler* contentHandler() const { return m_contentHandler.get(); }
    PDFInteractionHandler* interactionHandler() const { return m_interactionHandler.get(); }

    /**
     * @brief 获取文档状态对象（只读）
     */
    const PDFDocumentState* state() const { return m_state.get(); }

    // ==================== 文档生命周期 ====================

    /**
     * @brief 加载PDF文档
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
     * @brief 检测PDF类型
     */
    bool isTextPDF(int samplePages = 5) const;

    // ==================== 便捷方法 - 导航 ====================

    /**
     * @brief 跳转到指定页
     */
    void goToPage(int pageIndex, bool adjustForDoublePageMode = true);

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

    // ==================== 便捷方法 - 缩放 ====================

    /**
     * @brief 设置缩放比例
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
     * @brief 实际大小
     */
    void actualSize();

    /**
     * @brief 适应页面
     */
    void fitPage();

    /**
     * @brief 适应宽度
     */
    void fitWidth();

    /**
     * @brief 更新缩放（窗口大小变化时调用）
     */
    void updateZoom(const QSize& viewportSize);

    // ==================== 便捷方法 - 显示模式 ====================

    /**
     * @brief 设置显示模式
     */
    void setDisplayMode(PageDisplayMode mode);

    /**
     * @brief 设置连续滚动模式
     */
    void setContinuousScroll(bool continuous);

    /**
     * @brief 设置旋转角度
     */
    void setRotation(int rotation);

    // ==================== 便捷方法 - 内容管理 ====================

    /**
     * @brief 加载文档大纲
     */
    bool loadOutline();

    /**
     * @brief 获取大纲根节点
     */
    OutlineItem* outlineRoot() const;

    /**
     * @brief 获取大纲编辑器
     */
    OutlineEditor* outlineEditor() const;

    void loadThumbnails();  // 新增：启动缩略图加载流程

    QImage getThumbnail(int pageIndex, bool preferHighRes = false) const;
    bool hasThumbnail(int pageIndex) const;
    void setThumbnailSize(int lowResWidth, int highResWidth);
    void setThumbnailRotation(int rotation);
    void cancelThumbnailTasks();
    void clearThumbnails();
    QString getThumbnailStatistics() const;
    int cachedThumbnailCount() const;

    // ==================== 便捷方法 - 搜索 ====================

    /**
     * @brief 开始搜索
     */
    void startSearch(const QString& query,
                     bool caseSensitive = false,
                     bool wholeWords = false,
                     int startPage = 0);

    /**
     * @brief 取消搜索
     */
    void cancelSearch();

    /**
     * @brief 查找下一个
     */
    SearchResult findNext();

    /**
     * @brief 查找上一个
     */
    SearchResult findPrevious();

    // ==================== 便捷方法 - 文本选择 ====================

    /**
     * @brief 开始文本选择
     */
    void startTextSelection(int pageIndex, const QPointF& pagePos, double zoom);

    /**
     * @brief 更新文本选择
     */
    void updateTextSelection(int pageIndex, const QPointF& pagePos, double zoom);

    /**
     * @brief 扩展文本选择
     */
    void extendTextSelection(int pageIndex, const QPointF& pagePos, double zoom);

    /**
     * @brief 结束文本选择
     */
    void endTextSelection();

    /**
     * @brief 清除文本选择
     */
    void clearTextSelection();

    /**
     * @brief 选择单词
     */
    void selectWord(int pageIndex, const QPointF& pagePos, double zoom);

    /**
     * @brief 选择整行
     */
    void selectLine(int pageIndex, const QPointF& pagePos, double zoom);

    /**
     * @brief 全选
     */
    void selectAll(int pageIndex);

    /**
     * @brief 复制选中文本
     */
    void copySelectedText();

    // ==================== 便捷方法 - 链接 ====================

    /**
     * @brief 设置链接可见性
     */
    void setLinksVisible(bool visible);

    /**
     * @brief 命中测试链接
     */
    const PDFLink* hitTestLink(int pageIndex, const QPointF& pagePos, double zoom);

    /**
     * @brief 清除悬停链接
     */
    void clearHoveredLink();

    /**
     * @brief 处理链接点击
     */
    bool handleLinkClick(const PDFLink* link);

    // ==================== 连续滚动辅助方法 ====================

    /**
     * @brief 计算页面位置
     */
    void calculatePagePositions();

    /**
     * @brief 从滚动位置更新当前页
     */
    void updateCurrentPageFromScroll(int scrollY, int margin = 0);

    /**
     * @brief 获取页面滚动位置
     */
    int getScrollPositionForPage(int pageIndex, int margin = 0) const;

    // ==================== 统计信息 ====================

    QString getCacheStatistics() const;
    QString getTextCacheStatistics() const;

signals:
    // ==================== 文档状态变化信号 ====================

    /**
     * @brief 文档加载状态变化
     */
    void documentLoaded(const QString& path, int pageCount);

    /**
     * @brief 文档类型检测完成
     */
    void documentTypeChanged(bool isTextPDF);

    /**
     * @brief 文档错误
     */
    void documentError(const QString& error);

    // ==================== 导航状态变化信号 ====================

    /**
     * @brief 当前页码变化
     */
    void currentPageChanged(int pageIndex);

    // ==================== 缩放状态变化信号 ====================
    void zoomSettingCompleted(double zoom, ZoomMode mode);
    /**
     * @brief 当前缩放比例变化
     */
    void currentZoomChanged(double zoom);

    /**
     * @brief 缩放模式变化
     */
    void currentZoomModeChanged(ZoomMode mode);

    // ==================== 显示模式状态变化信号 ====================

    /**
     * @brief 显示模式变化
     */
    void currentDisplayModeChanged(PageDisplayMode mode);

    /**
     * @brief 连续滚动模式变化
     */
    void continuousScrollChanged(bool continuous);

    /**
     * @brief 旋转角度变化
     */
    void currentRotationChanged(int rotation);

    // ==================== 连续滚动状态变化信号 ====================

    /**
     * @brief 页面位置计算完成
     */
    void pagePositionsChanged(const QVector<int>& positions, const QVector<int>& heights);

    /**
     * @brief 需要滚动到指定位置
     */
    void scrollToPositionRequested(int scrollY);

    // ==================== 交互状态变化信号 ====================

    /**
     * @brief 链接可见性变化
     */
    void linksVisibleChanged(bool visible);

    /**
     * @brief 文本选择状态变化
     */
    void textSelectionChanged(bool hasSelection);

    /**
     * @brief 搜索状态变化
     */
    void searchStateChanged(bool searching, int totalMatches, int currentIndex);

    // ==================== 内容事件信号（非状态变化）====================

    /**
     * @brief 大纲加载完成
     */
    void outlineLoaded(bool success, int itemCount);


    void thumbnailLoaded(int pageIndex, const QImage& thumbnail);
    void thumbnailLoadProgress(int current, int total);

    /**
     * @brief 搜索进度更新
     */
    void searchProgressUpdated(int currentPage, int totalPages, int matchCount);

    /**
     * @brief 搜索完成
     */
    void searchCompleted(const QString& query, int totalMatches);

    /**
     * @brief 搜索取消
     */
    void searchCancelled();

    // ==================== 用户交互事件信号 ====================

    /**
     * @brief 链接悬停
     */
    void linkHovered(const PDFLink* link);

    /**
     * @brief 内部链接请求
     */
    void internalLinkRequested(int targetPage);

    /**
     * @brief 外部链接请求
     */
    void externalLinkRequested(const QString& uri);

    /**
     * @brief 文本复制完成
     */
    void textCopied(int characterCount);

    /**
     * @brief 文本预加载进度
     */
    void textPreloadProgress(int loaded, int total);

    /**
     * @brief 文本预加载完成
     */
    void textPreloadCompleted();

    /**
     * @brief 文本预加载取消
     */
    void textPreloadCancelled();

private:
    void setupConnections();
    void updateCacheAfterStateChange();

private:
    // 核心组件
    std::unique_ptr<MuPDFRenderer> m_renderer;
    std::unique_ptr<PageCacheManager> m_pageCache;
    std::unique_ptr<TextCacheManager> m_textCache;

    // Handler（处理业务逻辑）
    std::unique_ptr<PDFViewHandler> m_viewHandler;
    std::unique_ptr<PDFContentHandler> m_contentHandler;
    std::unique_ptr<PDFInteractionHandler> m_interactionHandler;

    // State（集中管理状态）
    std::unique_ptr<PDFDocumentState> m_state;
};

#endif // PDFDOCUMENTSESSION_H
