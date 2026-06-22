#include "searchmanager.h"
#include "perthreadmupdfrenderer.h"
#include "textcachemanager.h"
#include <QDebug>
#include <QMutexLocker>
#include <QThread>
#include <QCoreApplication>
#include <QMetaObject>
#include <QTimer>
#include <memory>

SearchManager::SearchManager(PerThreadMuPDFRenderer* renderer,
                             TextCacheManager* textCacheManager,
                             PDFBackgroundTaskHandler* backgroundTaskHandler,
                             QObject* parent)
    : QObject(parent)
    , m_renderer(renderer)
    , m_textCacheManager(textCacheManager)
    , m_backgroundTaskHandler(backgroundTaskHandler)
    , m_currentMatchIndex(-1)
    , m_isSearching(false)
    , m_cancelRequested(false)
    , m_workerThread(nullptr)
    , m_worker(nullptr)
{
}

SearchManager::~SearchManager()
{
    cancelSearch();

    if (m_workerThread) {
        if (!m_workerThread->wait(3000)) {
            qWarning() << "Worker thread did not exit in time, forcing termination";
            m_workerThread->terminate();
            m_workerThread->wait();
        }
    }
}

void SearchManager::startSearch(const QString& query,
                                const SearchOptions& options,
                                int startPage)
{
    if (query.isEmpty() || !m_renderer || !m_renderer->isDocumentLoaded()) {
        return;
    }

    if (isSearching()) {
        cancelSearch();
        if (m_workerThread) {
            if (!m_workerThread->wait(3000)) {
                qWarning() << "Previous search did not stop in 3s; will force stop as last resort";
                m_workerThread->terminate();
                m_workerThread->wait();
            }
        }
    }

    {
        QMutexLocker locker(&m_mutex);
        m_currentQuery = query;
        m_currentOptions = options;
        m_results.clear();
        m_currentMatchIndex = -1;
        m_isSearching.store(true);
        m_cancelRequested.store(false);
    }

    if (startPage < 0 || !m_renderer || startPage >= m_renderer->pageCount()) {
        startPage = 0;
    }
    m_startPage = startPage;

    if (!m_backgroundTaskHandler) {
        m_isSearching.store(false);
        return;
    }

    const PDFBackgroundTaskToken taskToken =
        m_backgroundTaskHandler->startTask(PDFBackgroundTaskType::Search);
    m_activeTask = taskToken;

    QThread* thread = new QThread;
    SearchWorker* worker = new SearchWorker(this, query, options, taskToken);

    m_workerThread = thread;
    m_worker = worker;

    worker->moveToThread(thread);

    connect(thread, &QThread::started, worker, &SearchWorker::process);

    connect(worker, &SearchWorker::progress, this,
            [this, taskToken](int currentPage, int totalPages, int matchCount) {
                if (m_backgroundTaskHandler->isCurrent(taskToken)) {
                    emit searchProgress(currentPage, totalPages, matchCount);
                }
            }, Qt::QueuedConnection);

    connect(worker, &SearchWorker::finished, thread, &QThread::quit, Qt::QueuedConnection);
    connect(worker, &SearchWorker::cancelled, thread, &QThread::quit, Qt::QueuedConnection);
    connect(worker, &SearchWorker::error, thread, &QThread::quit, Qt::QueuedConnection);

    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    connect(worker, &SearchWorker::finished, this, [this, taskToken](const QString& q, int total) {
        if (!m_backgroundTaskHandler->isCurrent(taskToken)) {
            return;
        }
        {
            QMutexLocker locker(&m_mutex);
            m_isSearching.store(false);
        }
        emit searchCompleted(q, total);
    }, Qt::QueuedConnection);

    connect(worker, &SearchWorker::cancelled, this, [this, taskToken]() {
        if (!m_backgroundTaskHandler->isCurrent(taskToken)) {
            return;
        }
        {
            QMutexLocker locker(&m_mutex);
            m_isSearching.store(false);
        }
        emit searchCancelled();
    }, Qt::QueuedConnection);

    connect(worker, &SearchWorker::error, this, [this, taskToken](const QString& err) {
        if (!m_backgroundTaskHandler->isCurrent(taskToken)) {
            return;
        }
        {
            QMutexLocker locker(&m_mutex);
            m_isSearching.store(false);
        }
        emit searchError(err);
    }, Qt::QueuedConnection);

    connect(thread, &QThread::finished, this, [this, thread]() {
        if (m_workerThread == thread) {
            m_workerThread = nullptr;
            m_worker = nullptr;
        }
    }, Qt::QueuedConnection);

    thread->start();
}

void SearchManager::cancelSearch()
{
    if (m_backgroundTaskHandler) {
        m_backgroundTaskHandler->invalidate(PDFBackgroundTaskType::Search);
    }
    m_cancelRequested.store(true);
    const bool wasSearching = m_isSearching.exchange(false);

    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "requestCancel", Qt::QueuedConnection);
    }

    QCoreApplication::processEvents();

    if (wasSearching) {
        emit searchCancelled();
    }
}

bool SearchManager::isSearching() const
{
    return m_isSearching.load();
}

QVector<SearchResult> SearchManager::getAllResults() const
{
    QMutexLocker locker(&m_mutex);
    return m_results;
}

QVector<SearchResult> SearchManager::getPageResults(int pageIndex) const
{
    QMutexLocker locker(&m_mutex);

    QVector<SearchResult> pageResults;
    for (const SearchResult& result : m_results) {
        if (result.pageIndex == pageIndex) {
            pageResults.append(result);
        }
    }
    return pageResults;
}

int SearchManager::totalMatches() const
{
    QMutexLocker locker(&m_mutex);
    return m_results.size();
}

int SearchManager::currentMatchIndex() const
{
    QMutexLocker locker(&m_mutex);
    return m_currentMatchIndex;
}

void SearchManager::setCurrentMatchIndex(int index)
{
    QMutexLocker locker(&m_mutex);
    if (index >= -1 && index < m_results.size()) {
        m_currentMatchIndex = index;
    }
}

SearchResult SearchManager::nextMatch()
{
    QMutexLocker locker(&m_mutex);

    if (m_results.isEmpty()) {
        return SearchResult();
    }

    m_currentMatchIndex = (m_currentMatchIndex + 1) % m_results.size();
    return m_results[m_currentMatchIndex];
}

SearchResult SearchManager::previousMatch()
{
    QMutexLocker locker(&m_mutex);

    if (m_results.isEmpty()) {
        return SearchResult();
    }

    m_currentMatchIndex--;
    if (m_currentMatchIndex < 0) {
        m_currentMatchIndex = m_results.size() - 1;
    }

    return m_results[m_currentMatchIndex];
}

SearchResult SearchManager::firstMatchFromStartPage()
{
    QMutexLocker locker(&m_mutex);

    if (m_results.isEmpty()) {
        m_currentMatchIndex = -1;
        return SearchResult();
    }

    // m_results 按页码升序排列：取第一个 pageIndex >= 起始页的匹配；
    // 若起始页及之后都没有，则回绕到全文第一个匹配（下标 0）。
    int index = 0;
    for (int i = 0; i < m_results.size(); ++i) {
        if (m_results[i].pageIndex >= m_startPage) {
            index = i;
            break;
        }
    }

    m_currentMatchIndex = index;
    return m_results[m_currentMatchIndex];
}

void SearchManager::clearResults()
{
    QMutexLocker locker(&m_mutex);
    m_results.clear();
    m_currentMatchIndex = -1;
    m_currentQuery.clear();
}

void SearchManager::addToHistory(const QString& query)
{
    if (query.isEmpty()) {
        return;
    }

    QMutexLocker locker(&m_mutex);

    m_searchHistory.removeAll(query);

    m_searchHistory.prepend(query);

    while (m_searchHistory.size() > MAX_HISTORY) {
        m_searchHistory.removeLast();
    }
}

QStringList SearchManager::getHistory(int maxCount) const
{
    QMutexLocker locker(&m_mutex);

    if (maxCount <= 0 || maxCount >= m_searchHistory.size()) {
        return m_searchHistory;
    }

    return m_searchHistory.mid(0, maxCount);
}

void SearchManager::clearHistory()
{
    QMutexLocker locker(&m_mutex);
    m_searchHistory.clear();
}

QVector<SearchResult> SearchManager::searchInPage(int pageIndex,
                                                  const PageTextData& textData,
                                                  const QString& query,
                                                  const SearchOptions& options)
{
    QVector<SearchResult> results;

    if (textData.isEmpty()) {
        return results;
    }

    QString searchQuery = query;
    if (!options.caseSensitive) {
        searchQuery = searchQuery.toLower();
    }

    for (const TextBlock& block : textData.blocks) {
        for (const TextLine& line : block.lines) {
            QString lineText;
            QVector<TextChar> lineChars;

            for (const TextChar& ch : line.chars) {
                QString charStr = QString(ch.character);
                if (!options.caseSensitive) {
                    charStr = charStr.toLower();
                }
                lineText.append(charStr);
                lineChars.append(ch);
            }

            int pos = 0;
            while ((pos = lineText.indexOf(searchQuery, pos)) != -1) {
                if (options.wholeWords) {
                    bool validStart = (pos == 0 || !lineText[pos - 1].isLetterOrNumber());
                    bool validEnd = (pos + searchQuery.length() >= lineText.length() ||
                                     !lineText[pos + searchQuery.length()].isLetterOrNumber());

                    if (!validStart || !validEnd) {
                        pos++;
                        continue;
                    }
                }

                SearchResult result(pageIndex);

                int endPos = pos + searchQuery.length();
                if (endPos > lineChars.size()) {
                    break;
                }

                QRectF matchRect;
                for (int i = pos; i < endPos; ++i) {
                    if (matchRect.isNull()) {
                        matchRect = lineChars[i].bbox;
                    } else {
                        matchRect = matchRect.united(lineChars[i].bbox);
                    }
                }

                result.quads.append(matchRect);
                result.context = getContextFromTextData(textData, block, line, pos, 30);

                results.append(result);

                pos++;

                if (results.size() >= options.maxResults) {
                    return results;
                }
            }
        }
    }

    return results;
}

QString SearchManager::getContextFromTextData(const PageTextData& textData,
                                              const TextBlock& currentBlock,
                                              const TextLine& currentLine,
                                              int matchPos,
                                              int contextLength)
{
    Q_UNUSED(textData);
    Q_UNUSED(currentBlock);

    QString lineText;
    for (const TextChar& ch : currentLine.chars) {
        lineText.append(ch.character);
    }

    int start = qMax(0, matchPos - contextLength);
    int end = qMin(lineText.length(), matchPos + contextLength);

    QString context = lineText.mid(start, end - start);

    if (start > 0) {
        context = "..." + context;
    }
    if (end < lineText.length()) {
        context = context + "...";
    }

    return context;
}

SearchWorker::SearchWorker(SearchManager* manager,
                           const QString& query,
                           const SearchOptions& options,
                           PDFBackgroundTaskToken taskToken)
    : QObject(nullptr)
    , m_manager(manager)
    , m_query(query)
    , m_options(options)
    , m_taskToken(taskToken)
    , m_cancelRequested(false)
{
}

void SearchWorker::requestCancel()
{
    m_cancelRequested.store(true);
}

void SearchWorker::process()
{
    if (!m_manager || !m_manager->m_renderer ||
        !m_manager->m_renderer->isDocumentLoaded()) {
        emit error(tr("No document loaded"));
        return;
    }

    auto isCancelled = [this]() {
        return m_cancelRequested.load() ||
               (m_manager && m_manager->m_cancelRequested.load()) ||
               !m_manager->m_backgroundTaskHandler ||
               !m_manager->m_backgroundTaskHandler->isCurrent(m_taskToken);
    };

    // 文档信息只在开始时从共享 renderer 读一次，循环内不再触碰它
    const int totalPages = m_manager->m_renderer->pageCount();
    const QString pdfPath = m_manager->m_renderer->documentPath();

    if (totalPages <= 0) {
        emit finished(m_query, 0);
        return;
    }

    qDebug() << "SearchWorker started -" << totalPages << "pages";

    // 仅当某页文本未命中缓存时才惰性创建（打开整篇 PDF 有开销）
    std::unique_ptr<PerThreadMuPDFRenderer> ownRenderer;

    const int progressStep = qMax(1, totalPages / 100);
    int totalMatches = 0;
    bool reachedLimit = false;

    // 按页码升序遍历全文，使 m_results 始终按文档顺序排列（高亮序号、x/总数 编号
    // 都以全文为准）。"自当前页向后跳"的体验由搜索完成后的初始定位负责。
    for (int processed = 0; processed < totalPages; ++processed) {
        if (isCancelled()) {
            qDebug() << "SearchWorker: cancelled at" << processed << "/" << totalPages;
            emit cancelled();
            return;
        }

        const int pageIndex = processed;

        // 取页面文本：优先缓存；未缓存则当场提取并回填缓存
        PageTextData textData;
        if (m_manager->m_textCacheManager->contains(pageIndex)) {
            textData = m_manager->m_textCacheManager->getPageTextData(pageIndex);
        } else {
            if (!ownRenderer) {
                ownRenderer = std::make_unique<PerThreadMuPDFRenderer>(pdfPath);
            }
            if (ownRenderer->isDocumentLoaded()) {
                PageTextData extracted;
                if (ownRenderer->extractText(pageIndex, extracted)) {
                    extracted.pageIndex = pageIndex;
                    m_manager->m_textCacheManager->addPageTextData(pageIndex, extracted);
                    textData = extracted;
                }
            }
        }

        QVector<SearchResult> pageResults;
        try {
            pageResults = m_manager->searchInPage(pageIndex, textData, m_query, m_options);
        } catch (...) {
            qWarning() << "SearchWorker: exception searching page" << pageIndex;
            pageResults.clear();
        }

        if (!pageResults.isEmpty()) {
            QMutexLocker locker(&m_manager->m_mutex);
            for (const SearchResult& r : pageResults) {
                if (totalMatches >= m_options.maxResults) {
                    reachedLimit = true;
                    break;
                }
                m_manager->m_results.append(r);
                ++totalMatches;
            }
        }

        if (reachedLimit) {
            emit progress(processed + 1, totalPages, totalMatches);
            qDebug() << "SearchWorker: reached maxResults limit" << m_options.maxResults;
            break;
        }

        if (processed % progressStep == 0 || processed == totalPages - 1) {
            emit progress(processed + 1, totalPages, totalMatches);
        }
    }

    if (isCancelled()) {
        emit cancelled();
        return;
    }

    qDebug() << "SearchWorker: completed," << totalMatches << "matches across"
             << totalPages << "pages";

    emit finished(m_query, totalMatches);
}
