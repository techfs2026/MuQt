#ifndef SEARCHMANAGER_H
#define SEARCHMANAGER_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QRectF>
#include <QMutex>
#include <QThread>
#include <QPointer>
#include <QStringList>
#include <atomic>

extern "C" {
#include <mupdf/fitz.h>
}

class MuPDFRenderer;
class TextCacheManager;
class SearchManager;
struct PageTextData;
struct TextBlock;
struct TextLine;
struct TextChar;

// ========== 搜索选项 ==========

struct SearchOptions {
    bool caseSensitive = false;
    bool wholeWords = false;
    int maxResults = 1000;
};

// ========== 搜索结果 ==========

struct SearchResult {
    int pageIndex;
    QVector<QRectF> quads;  // 匹配文本的位置
    QString context;        // 上下文

    SearchResult() : pageIndex(-1) {}
    explicit SearchResult(int page) : pageIndex(page) {}

    bool isValid() const { return pageIndex >= 0 && !quads.isEmpty(); }
};

// ========== 搜索工作线程 ==========

class SearchWorker : public QObject
{
    Q_OBJECT

public:
    SearchWorker(SearchManager* manager,
                 const QString& query,
                 const SearchOptions& options,
                 int startPage);

public slots:
    void process();
    void requestCancel();

signals:
    void progress(int currentPage, int totalPages, int matchCount);
    void finished(const QString& query, int totalMatches);
    void cancelled();
    void error(const QString& errorMsg);

private:
    SearchManager* m_manager;
    QString m_query;
    SearchOptions m_options;
    int m_startPage;
    std::atomic_bool m_cancelRequested;
};

// ========== 搜索管理器 ==========

class SearchManager : public QObject
{
    Q_OBJECT

public:
    explicit SearchManager(MuPDFRenderer* renderer,
                           TextCacheManager* textCacheManager,
                           QObject* parent = nullptr);
    ~SearchManager();

    // 搜索控制
    void startSearch(const QString& query,
                     const SearchOptions& options = SearchOptions(),
                     int startPage = 0);
    void cancelSearch();
    bool isSearching() const;

    // 结果访问
    QVector<SearchResult> getAllResults() const;
    QVector<SearchResult> getPageResults(int pageIndex) const;
    int totalMatches() const;

    // 当前匹配导航
    int currentMatchIndex() const;
    void setCurrentMatchIndex(int index);
    SearchResult nextMatch();
    SearchResult previousMatch();

    // 结果管理
    void clearResults();

    // 搜索历史
    void addToHistory(const QString& query);
    QStringList getHistory(int maxCount = 10) const;
    void clearHistory();

signals:
    void searchProgress(int currentPage, int totalPages, int matchCount);
    void searchCompleted(const QString& query, int totalMatches);
    void searchCancelled();
    void searchError(const QString& error);

private:
    friend class SearchWorker;

    // 搜索单页（从缓存的文本数据）
    QVector<SearchResult> searchPage(int pageIndex,
                                     const QString& query,
                                     const SearchOptions& options);

    // 辅助方法：从文本数据中提取上下文
    QString getContextFromTextData(const PageTextData& textData,
                                   const TextBlock& currentBlock,
                                   const TextLine& currentLine,
                                   int matchPos,
                                   int contextLength);

    MuPDFRenderer* m_renderer;
    TextCacheManager* m_textCacheManager;

    // 搜索结果
    QVector<SearchResult> m_results;
    int m_currentMatchIndex;

    // 当前搜索
    QString m_currentQuery;
    SearchOptions m_currentOptions;

    // 搜索状态
    mutable QMutex m_mutex; // 保护 m_results, m_currentMatchIndex, m_searchHistory 等共享数据
    std::atomic_bool m_isSearching;
    std::atomic_bool m_cancelRequested;
    QPointer<QThread> m_workerThread;
    QPointer<SearchWorker> m_worker; // 当前 worker（如果有）

    // 搜索历史
    QStringList m_searchHistory;
    static constexpr int MAX_HISTORY = 20;
};

#endif // SEARCHMANAGER_H
