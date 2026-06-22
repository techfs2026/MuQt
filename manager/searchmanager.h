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

#include "datastructure.h"
#include "pdfbackgroundtaskhandler.h"

extern "C" {
#include <mupdf/fitz.h>
}

class PerThreadMuPDFRenderer;
class TextCacheManager;
class SearchManager;
struct PageTextData;
struct TextBlock;
struct TextLine;
struct TextChar;

class SearchWorker : public QObject
{
    Q_OBJECT

public:
    SearchWorker(SearchManager* manager,
                 const QString& query,
                 const SearchOptions& options,
                 PDFBackgroundTaskToken taskToken);

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
    PDFBackgroundTaskToken m_taskToken;
    std::atomic_bool m_cancelRequested;
};

class SearchManager : public QObject
{
    Q_OBJECT

public:
    explicit SearchManager(PerThreadMuPDFRenderer* renderer,
                           TextCacheManager* textCacheManager,
                           PDFBackgroundTaskHandler* backgroundTaskHandler,
                           QObject* parent = nullptr);
    ~SearchManager();

    void startSearch(const QString& query,
                     const SearchOptions& options = SearchOptions(),
                     int startPage = 0);
    void cancelSearch();
    bool isSearching() const;

    QVector<SearchResult> getAllResults() const;
    QVector<SearchResult> getPageResults(int pageIndex) const;
    int totalMatches() const;

    int currentMatchIndex() const;
    void setCurrentMatchIndex(int index);
    SearchResult nextMatch();
    SearchResult previousMatch();

    // 结果按页码升序存储；本方法把当前项定位到"发起搜索那一页及之后"的第一个匹配
    // （若该页之后无匹配则回绕到全文第一个），用于搜索完成后的初始跳转。
    SearchResult firstMatchFromStartPage();

    void clearResults();

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

    QVector<SearchResult> searchInPage(int pageIndex,
                                       const PageTextData& textData,
                                       const QString& query,
                                       const SearchOptions& options);

    QString getContextFromTextData(const PageTextData& textData,
                                   const TextBlock& currentBlock,
                                   const TextLine& currentLine,
                                   int matchPos,
                                   int contextLength);

    PerThreadMuPDFRenderer* m_renderer;
    TextCacheManager* m_textCacheManager;
    PDFBackgroundTaskHandler* m_backgroundTaskHandler;
    PDFBackgroundTaskToken m_activeTask;

    QVector<SearchResult> m_results;
    int m_currentMatchIndex;
    int m_startPage = 0;  // 本次搜索发起时所在页，用于初始跳转

    QString m_currentQuery;
    SearchOptions m_currentOptions;

    mutable QMutex m_mutex;
    std::atomic_bool m_isSearching;
    std::atomic_bool m_cancelRequested;
    QPointer<QThread> m_workerThread;
    QPointer<SearchWorker> m_worker;

    QStringList m_searchHistory;
    static constexpr int MAX_HISTORY = 20;
};

#endif // SEARCHMANAGER_H
