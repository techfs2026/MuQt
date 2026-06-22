#ifndef PDFINTERACTIONHANDLER_H
#define PDFINTERACTIONHANDLER_H

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <memory>

class PerThreadMuPDFRenderer;
class TextCacheManager;
class PDFBackgroundTaskHandler;
class SearchManager;
class LinkManager;
class TextSelector;
struct SearchResult;
struct PDFLink;
struct TextSelection;

class PDFInteractionHandler : public QObject
{
    Q_OBJECT

public:
    explicit PDFInteractionHandler(PerThreadMuPDFRenderer* renderer,
                                   TextCacheManager* textCacheManager,
                                   PDFBackgroundTaskHandler* backgroundTaskHandler,
                                   QObject* parent = nullptr);
    ~PDFInteractionHandler();

    void startSearch(const QString& query,
                     bool caseSensitive = false,
                     bool wholeWords = false,
                     int startPage = 0);

    void cancelSearch();

    SearchResult findNext();

    SearchResult findPrevious();

    // 搜索完成后的初始定位：跳到发起搜索那一页及之后的第一个匹配
    SearchResult findFirstFromStartPage();

    void clearSearchResults();

    QVector<SearchResult> getPageSearchResults(int pageIndex) const;

    void addSearchHistory(const QString& query);

    QStringList getSearchHistory(int maxCount = 10) const;

    void requestSetLinksVisible(bool visible);

    const PDFLink* hitTestLink(int pageIndex, const QPointF& pagePos, double zoom);

    void clearHoveredLink();

    bool handleLinkClick(const PDFLink* link);

    QVector<PDFLink> loadPageLinks(int pageIndex);

    void startTextSelection(int pageIndex, const QPointF& pagePos, double zoom);

    void updateTextSelection(int pageIndex, const QPointF& pagePos, double zoom);

    void extendTextSelection(int pageIndex, const QPointF& pagePos, double zoom);

    void endTextSelection();

    void clearTextSelection();

    void selectWord(int pageIndex, const QPointF& pagePos, double zoom);

    void selectLine(int pageIndex, const QPointF& pagePos, double zoom);

    void selectAll(int pageIndex);

    QString getSelectedText() const;

    const TextSelection& getCurrentTextSelection() const;

    void copySelectedText();

    bool isTextSelecting() const;

    SearchManager* searchManager() const { return m_searchManager.get(); }
    LinkManager* linkManager() const { return m_linkManager.get(); }
    TextSelector* textSelector() const { return m_textSelector.get(); }

signals:

    void searchProgressUpdated(int currentPage, int totalPages, int matchCount);

    void searchCompleted(const QString& query, int totalMatches);

    void searchCancelled();

    void searchError(const QString& error);

    void searchNavigationCompleted(const SearchResult& result,
                                   int currentIndex,
                                   int totalMatches);

    void linksVisibilityChanged(bool visible);

    void linkHovered(const PDFLink* link);

    void linkClicked(const PDFLink* link);

    void internalLinkRequested(int targetPage);

    void externalLinkRequested(const QString& uri);

    void linkError(const QString& error);

    void textSelectionChanged(bool hasSelection, const QString& selectedText);

    void textCopied(int characterCount);

private:
    void setupConnections();

private:
    PerThreadMuPDFRenderer* m_renderer;
    TextCacheManager* m_textCacheManager;

    std::unique_ptr<SearchManager> m_searchManager;
    std::unique_ptr<LinkManager> m_linkManager;
    std::unique_ptr<TextSelector> m_textSelector;

    const PDFLink* m_hoveredLink;
};

#endif // PDFINTERACTIONHANDLER_H
