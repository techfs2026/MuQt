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
 */
class PDFInteractionHandler : public QObject
{
    Q_OBJECT

public:
    explicit PDFInteractionHandler(MuPDFRenderer* renderer,
                                   TextCacheManager* textCacheManager,
                                   QObject* parent = nullptr);
    ~PDFInteractionHandler();

    // ========== 搜索相关 ==========

    /**
     * @brief 开始搜索
     * @param query 搜索关键词
     * @param caseSensitive 是否大小写敏感
     * @param wholeWords 是否全词匹配
     * @param startPage 起始页码
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
     * @brief 是否正在搜索
     */
    bool isSearching() const;

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
     * @brief 获取总匹配数
     */
    int totalSearchMatches() const;

    /**
     * @brief 获取当前匹配索引
     */
    int currentSearchMatchIndex() const;

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

    // ========== 链接相关 ==========

    /**
     * @brief 设置链接是否可见
     */
    void setLinksVisible(bool visible);

    /**
     * @brief 链接是否可见
     */
    bool linksVisible() const { return m_linksVisible; }

    /**
     * @brief 命中测试链接
     * @param pageIndex 页码
     * @param pagePos 页面坐标(已缩放)
     * @param zoom 缩放比例
     * @return 命中的链接指针,如果没有命中返回nullptr
     */
    const PDFLink* hitTestLink(int pageIndex, const QPointF& pagePos, double zoom);

    /**
     * @brief 获取当前悬停的链接
     */
    const PDFLink* hoveredLink() const { return m_hoveredLink; }

    /**
     * @brief 清除悬停链接
     */
    void clearHoveredLink();

    /**
     * @brief 处理链接点击
     * @return true表示成功处理,false表示失败
     */
    bool handleLinkClick(const PDFLink* link);

    /**
     * @brief 加载指定页的链接
     */
    QVector<PDFLink> loadPageLinks(int pageIndex);

    // ========== 文本选择相关 ==========

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
     * @brief 是否有选中的文本
     */
    bool hasTextSelection() const;

    /**
     * @brief 获取选中的文本
     */
    QString selectedText() const;

    /**
     * @brief 获取当前文本选择
     */
    const TextSelection& currentTextSelection() const;

    /**
     * @brief 复制选中的文本到剪贴板
     */
    void copySelectedText();

    /**
     * @brief 是否正在进行文本选择
     */
    bool isTextSelecting() const;

    // ========== 访问器 ==========

    SearchManager* searchManager() const { return m_searchManager.get(); }
    LinkManager* linkManager() const { return m_linkManager.get(); }
    TextSelector* textSelector() const { return m_textSelector.get(); }

signals:
    // 搜索信号
    void searchProgress(int currentPage, int totalPages, int matchCount);
    void searchCompleted(const QString& query, int totalMatches);
    void searchCancelled();
    void searchError(const QString& error);

    // 链接信号
    void linkHovered(const PDFLink* link);
    void linkClicked(const PDFLink* link);
    void internalLinkRequested(int targetPage);
    void externalLinkRequested(const QString& uri);
    void linkError(const QString& error);

    // 文本选择信号
    void textSelectionChanged();
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

    // 链接状态
    bool m_linksVisible;
    const PDFLink* m_hoveredLink;
};

#endif // PDFINTERACTIONHANDLER_H
