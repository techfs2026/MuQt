#include "searchmanager.h"
#include "perthreadmupdfrenderer.h"
#include "textcachemanager.h"
#include <QDebug>
#include <QMutexLocker>
#include <QThread>
#include <QCoreApplication>
#include <QMetaObject>
#include <QTimer>

// ----------------- SearchManager 实现 -----------------

SearchManager::SearchManager(PerThreadMuPDFRenderer* renderer,
                             TextCacheManager* textCacheManager,
                             QObject* parent)
    : QObject(parent)
    , m_renderer(renderer)
    , m_textCacheManager(textCacheManager)
    , m_currentMatchIndex(-1)
    , m_isSearching(false)
    , m_cancelRequested(false)
    , m_workerThread(nullptr)
    , m_worker(nullptr)
{
}

SearchManager::~SearchManager()
{
    // 请求取消并等待线程结束，确保析构时没有活动线程访问本对象
    cancelSearch();

    if (m_workerThread) {
        // 等待一小段时间让线程优雅退出
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

    // 如果已有搜索在运行，先尝试取消并等待其结束（短超时）
    if (isSearching()) {
        cancelSearch();
        if (m_workerThread) {
            // 等待直到线程结束或超时（3秒）
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

    // 确定起始页
    if (startPage < 0 || !m_renderer || startPage >= m_renderer->pageCount()) {
        startPage = 0;
    }

    // 创建新的工作线程和 worker
    QThread* thread = new QThread;
    SearchWorker* worker = new SearchWorker(this, query, options, startPage);

    // 记录 pointers（QPointer 方便后续检查）
    m_workerThread = thread;
    m_worker = worker;

    // 把 worker 移到线程
    worker->moveToThread(thread);

    // 连接：线程启动 => worker 处理
    connect(thread, &QThread::started, worker, &SearchWorker::process);

    // 连接 worker 的信号到 manager（使用 QueuedConnection 确保线程间安全）
    connect(worker, &SearchWorker::progress, this, &SearchManager::searchProgress, Qt::QueuedConnection);

    // 当 worker 完成/取消/报错时，让线程退出（quit），以便触发线程的 finished 信号并清理。
    connect(worker, &SearchWorker::finished, thread, &QThread::quit, Qt::QueuedConnection);
    connect(worker, &SearchWorker::cancelled, thread, &QThread::quit, Qt::QueuedConnection);
    connect(worker, &SearchWorker::error, thread, &QThread::quit, Qt::QueuedConnection);

    // 当线程退出时，删除 worker（worker 在其线程上下文中 deleteLater）与删除 thread 对象
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    // 处理完成/取消/错误，更新状态并发射公开信号（在主线程）
    connect(worker, &SearchWorker::finished, this, [this](const QString& q, int total) {
        {
            QMutexLocker locker(&m_mutex);
            m_isSearching.store(false);
            // m_worker / m_workerThread 将在 thread finished 时置空（见后面的连接）
        }
        emit searchCompleted(q, total);
    }, Qt::QueuedConnection);

    connect(worker, &SearchWorker::cancelled, this, [this]() {
        {
            QMutexLocker locker(&m_mutex);
            m_isSearching.store(false);
        }
        emit searchCancelled();
    }, Qt::QueuedConnection);

    connect(worker, &SearchWorker::error, this, [this](const QString& err) {
        {
            QMutexLocker locker(&m_mutex);
            m_isSearching.store(false);
        }
        emit searchError(err);
    }, Qt::QueuedConnection);

    // 当线程真正 finished 时，清理 manager 内部的指针引用（在主线程执行）
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (m_workerThread == thread) {
            m_workerThread = nullptr;
        }
        // m_worker 会在 worker 的 deleteLater 执行后自动变为 nullptr（QPointer）
        m_worker = nullptr;
    }, Qt::QueuedConnection);

    // 启动线程
    thread->start();
}

void SearchManager::cancelSearch()
{
    // 标记取消标志并请求 worker 执行取消
    m_cancelRequested.store(true);

    // 如果 worker 存在，向 worker 请求取消（QueuedConnection 安全跨线程）
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "requestCancel", Qt::QueuedConnection);
    }

    // Additionally, process events to help wake up the event loop if needed
    QCoreApplication::processEvents();
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

    // 移除重复项
    m_searchHistory.removeAll(query);

    // 添加到开头
    m_searchHistory.prepend(query);

    // 限制大小
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

// ----------------- 从缓存的文本数据中搜索 -----------------

QVector<SearchResult> SearchManager::searchPage(int pageIndex,
                                                const QString& query,
                                                const SearchOptions& options)
{
    QVector<SearchResult> results;

    if (!m_renderer || !m_renderer->isDocumentLoaded()) {
        return results;
    }

    // 从缓存获取文本数据
    PageTextData textData = m_textCacheManager->getPageTextData(pageIndex);

    if (textData.isEmpty()) {
        qDebug() << "searchPage: No text data cached for page" << pageIndex;
        return results;
    }

    // 在缓存的文本数据中搜索
    QString searchQuery = query;
    if (!options.caseSensitive) {
        searchQuery = searchQuery.toLower();
    }

    // 遍历所有文本块
    for (const TextBlock& block : textData.blocks) {
        for (const TextLine& line : block.lines) {
            // 构建当前行的文本
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

            // 在行文本中查找匹配
            int pos = 0;
            while ((pos = lineText.indexOf(searchQuery, pos)) != -1) {
                // 如果要求全词匹配，检查边界
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

                // 计算匹配文本的边界框
                int endPos = pos + searchQuery.length();
                if (endPos > lineChars.size()) {
                    break;
                }

                // 合并匹配字符的边界框
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

                pos++;  // 继续查找下一个匹配

                if (results.size() >= options.maxResults) {
                    // 直接返回，不再继续
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

    // 构建当前行的完整文本
    QString lineText;
    for (const TextChar& ch : currentLine.chars) {
        lineText.append(ch.character);
    }

    int start = qMax(0, matchPos - contextLength);
    int end = qMin(lineText.length(), matchPos + contextLength);

    QString context = lineText.mid(start, end - start);

    // 添加省略号
    if (start > 0) {
        context = "..." + context;
    }
    if (end < lineText.length()) {
        context = context + "...";
    }

    return context;
}

// ----------------- SearchWorker 实现 -----------------

SearchWorker::SearchWorker(SearchManager* manager,
                           const QString& query,
                           const SearchOptions& options,
                           int startPage)
    : QObject(nullptr)
    , m_manager(manager)
    , m_query(query)
    , m_options(options)
    , m_startPage(startPage)
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
        emit error("No document loaded");
        return;
    }

    qDebug() << "SearchWorker started - searching page:" << m_startPage;

    int pageIndex = m_startPage;
    int totalMatches = 0;

    // 检查取消请求（快速检查）
    if (m_cancelRequested.load() || (m_manager && m_manager->m_cancelRequested.load())) {
        qDebug() << "SearchWorker: cancelled before start";
        emit cancelled();
        return;
    }

    // 在工作线程中执行页面搜索（使用 manager 的 searchPage）
    QVector<SearchResult> pageResults;
    try {
        pageResults = m_manager->searchPage(pageIndex, m_query, m_options);
    } catch (...) {
        emit error("Exception during searchPage");
        return;
    }

    if (m_cancelRequested.load() || (m_manager && m_manager->m_cancelRequested.load())) {
        qDebug() << "SearchWorker: cancelled after searchPage";
        emit cancelled();
        return;
    }

    if (!pageResults.isEmpty()) {
        // 将结果追加到 manager（在追加时使用 manager 的 mutex 保护）
        {
            QMutexLocker locker(&m_manager->m_mutex);
            for (const SearchResult& r : pageResults) {
                m_manager->m_results.append(r);
            }
            totalMatches = pageResults.size();
        }
    }

    // 发送进度（这里仅用于单页显示）
    emit progress(1, 1, totalMatches);

    qDebug() << "SearchWorker: Search completed on page" << pageIndex << ", matches:" << totalMatches;

    emit finished(m_query, totalMatches);
}
