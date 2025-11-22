#ifndef PDFINTERACTIONHANDLER_H
#define PDFINTERACTIONHANDLER_H

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <memory>

// Forward declarations
class MuPDFRenderer;
class TextCacheManager;
class SearchManager;
class LinkManager;
class TextSelector;
struct SearchResult;
struct PDFLink;
struct TextSelection;

/**
 * @brief PDF交互处理器
 *
 * 统一管理PDF文档的用户交互功能:
 * - 搜索功能(SearchManager)
 * - 链接处理(LinkManager)
 * - 文本选择(TextSelector)
 *
 * 注意：不存储最终状态，只处理交互逻辑并发出完成信号
 */
class PDFInteractionHandler : public QObject
{
    Q_OBJECT

public:
    explicit PDFInteractionHandler(MuPDFRenderer* renderer,
                                   TextCacheManager* textCacheManager,
                                   QObject* parent = nullptr);
    ~PDFInteractionHandler();

    // ==================== 搜索相关 ====================

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
     * @brief 跳转到下一个搜索结果
     */
    SearchResult findNext();

    /**
     * @brief 跳转到上一个搜索结果
     */
    SearchResult findPrevious();

    /**
     * @brief 清除搜索结果
     */
    void clearSearchResults();

    /**
     * @brief 获取指定页的搜索结果
     */
    QVector<SearchResult> getPageSearchResults(int pageIndex) const;

    /**
     * @brief 添加搜索历史
     */
    void addSearchHistory(const QString& query);

    /**
     * @brief 获取搜索历史
     */
    QStringList getSearchHistory(int maxCount = 10) const;

    // ==================== 链接相关 ====================

    /**
     * @brief 请求设置链接可见性
     */
    void requestSetLinksVisible(bool visible);

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

    /**
     * @brief 加载指定页的链接
     */
    QVector<PDFLink> loadPageLinks(int pageIndex);

    // ==================== 文本选择相关 ====================

    /**
     * @brief 开始文本选择
     */
    void startTextSelection(int pageIndex, const QPointF& pagePos, double zoom);

    /**
     * @brief 更新文本选择
     */
    void updateTextSelection(int pageIndex, const QPointF& pagePos, double zoom);

    /**
     * @brief 扩展文本选择(Shift+点击)
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
     * @brief 全选当前页
     */
    void selectAll(int pageIndex);

    /**
     * @brief 获取选中的文本
     */
    QString getSelectedText() const;

    /**
     * @brief 获取当前文本选择
     */
    const TextSelection& getCurrentTextSelection() const;

    /**
     * @brief 复制选中的文本到剪贴板
     */
    void copySelectedText();

    /**
     * @brief 是否正在进行文本选择
     */
    bool isTextSelecting() const;

    // ==================== 访问器 ====================

    SearchManager* searchManager() const { return m_searchManager.get(); }
    LinkManager* linkManager() const { return m_linkManager.get(); }
    TextSelector* textSelector() const { return m_textSelector.get(); }

signals:
    // ==================== 搜索完成信号 ====================

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

    /**
     * @brief 搜索错误
     */
    void searchError(const QString& error);

    /**
     * @brief 搜索结果导航完成
     * @param result 找到的结果
     * @param currentIndex 当前匹配索引
     * @param totalMatches 总匹配数
     */
    void searchNavigationCompleted(const SearchResult& result,
                                   int currentIndex,
                                   int totalMatches);

    // ==================== 链接完成信号 ====================

    /**
     * @brief 链接可见性设置完成
     */
    void linksVisibilityChanged(bool visible);

    /**
     * @brief 链接悬停状态变化
     */
    void linkHovered(const PDFLink* link);

    /**
     * @brief 链接点击完成
     */
    void linkClicked(const PDFLink* link);

    /**
     * @brief 需要跳转到内部链接
     */
    void internalLinkRequested(int targetPage);

    /**
     * @brief 需要打开外部链接
     */
    void externalLinkRequested(const QString& uri);

    /**
     * @brief 链接处理错误
     */
    void linkError(const QString& error);

    // ==================== 文本选择完成信号 ====================

    /**
     * @brief 文本选择状态变化
     * @param hasSelection 是否有选择
     * @param selectedText 选中的文本
     */
    void textSelectionChanged(bool hasSelection, const QString& selectedText);

    /**
     * @brief 文本复制完成
     */
    void textCopied(int characterCount);

private:
    void setupConnections();

private:
    MuPDFRenderer* m_renderer;
    TextCacheManager* m_textCacheManager;

    // 子管理器
    std::unique_ptr<SearchManager> m_searchManager;
    std::unique_ptr<LinkManager> m_linkManager;
    std::unique_ptr<TextSelector> m_textSelector;

    // 中间状态（悬停链接）
    const PDFLink* m_hoveredLink;
};

#endif // PDFINTERACTIONHANDLER_H
