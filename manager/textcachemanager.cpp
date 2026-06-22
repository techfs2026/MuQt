#include "textcachemanager.h"
#include "perthreadmupdfrenderer.h"
#include <QDebug>
#include <QMutexLocker>
#include <QCoreApplication>
#include <QRunnable>
#include <QMetaObject>
#include <QThread>
#include <QVector>
#include <memory>

class PageExtractTask : public QRunnable
{
public:
    PageExtractTask(TextCacheManager* manager,
                    const QString& pdfPath,
                    const QVector<int>& pageIndices,
                    PDFBackgroundTaskToken taskToken)
        : m_manager(manager)
        , m_pdfPath(pdfPath)
        , m_pageIndices(pageIndices)
        , m_taskToken(taskToken)
        , m_renderer(nullptr)
    {
        setAutoDelete(true);
    }

    void run() override
    {
        if (!m_manager || m_pageIndices.isEmpty()) {
            qWarning() << "PageExtractTask: Invalid manager or empty page list";
            return;
        }

        if (m_manager->m_cancelRequested.loadAcquire() ||
            !m_manager->m_backgroundTaskHandler ||
            !m_manager->m_backgroundTaskHandler->isCurrent(m_taskToken)) {
            qDebug() << "PageExtractTask: Cancelled before start, pages:" << m_pageIndices;
            reportAllFailed();
            return;
        }

        qDebug() << "PageExtractTask: Creating renderer for batch, pages:" << m_pageIndices.size()
                 << "first:" << m_pageIndices.first() << "last:" << m_pageIndices.last();

        m_renderer = std::make_unique<PerThreadMuPDFRenderer>(m_pdfPath);

        if (!m_renderer->isDocumentLoaded()) {
            qWarning() << "PageExtractTask: Failed to load document, error:"
                       << m_renderer->getLastError();
            reportAllFailed();
            return;
        }

        int totalPages = m_renderer->pageCount();
        qDebug() << "PageExtractTask: Document loaded successfully, total pages:" << totalPages;

        int successCount = 0;
        int failCount = 0;

        for (int pageIndex : m_pageIndices) {
            if (m_manager->m_cancelRequested.loadAcquire() ||
                !m_manager->m_backgroundTaskHandler->isCurrent(m_taskToken)) {
                qDebug() << "PageExtractTask: Cancelled at page" << pageIndex;
                reportDone(pageIndex, PageTextData(), false);
                failCount++;
                continue;
            }

            if (pageIndex < 0 || pageIndex >= totalPages) {
                qWarning() << "PageExtractTask: Invalid page index" << pageIndex
                           << "total pages:" << totalPages;
                reportDone(pageIndex, PageTextData(), false);
                failCount++;
                continue;
            }

            PageTextData pageData;

            m_renderer->extractText(pageIndex, pageData);

            QString error = m_renderer->getLastError();
            bool hasError = !error.isEmpty();
            bool isBlankPage = pageData.blocks.isEmpty() && !hasError;
            bool success = !hasError;

            if (hasError) {
                qWarning() << "PageExtractTask: Failed to extract text from page" << pageIndex
                           << "Error:" << error;
                failCount++;
            } else if (isBlankPage) {
                qDebug() << "PageExtractTask: Page" << pageIndex
                         << "is blank (no text content)";
                successCount++;
            } else {
                successCount++;
            }

            reportDone(pageIndex, pageData, success);
        }

        qDebug() << "PageExtractTask: Batch completed"
                 << "success:" << successCount
                 << "failed:" << failCount
                 << "pages:" << m_pageIndices.size();
    }

private:
    void reportDone(int pageIndex, const PageTextData& data, bool ok)
    {
        QMetaObject::invokeMethod(m_manager, "handleTaskDone",
                                  Qt::QueuedConnection,
                                  Q_ARG(int, pageIndex),
                                  Q_ARG(PageTextData, data),
                                  Q_ARG(bool, ok),
                                  Q_ARG(PDFBackgroundTaskToken, m_taskToken));
    }

    void reportAllFailed()
    {
        for (int pageIndex : m_pageIndices) {
            reportDone(pageIndex, PageTextData(), false);
        }
    }

    TextCacheManager* m_manager;
    QString m_pdfPath;
    QVector<int> m_pageIndices;
    PDFBackgroundTaskToken m_taskToken;
    std::unique_ptr<PerThreadMuPDFRenderer> m_renderer;
};

TextCacheManager::TextCacheManager(PerThreadMuPDFRenderer* renderer,
                                   PDFBackgroundTaskHandler* backgroundTaskHandler,
                                   QObject* parent)
    : QObject(parent)
    , m_renderer(renderer)
    , m_backgroundTaskHandler(backgroundTaskHandler)
    , m_maxCacheSize(-1)
    , m_isPreloading(0)
    , m_cancelRequested(0)
    , m_preloadedPages(0)
    , m_hitCount(0)
    , m_missCount(0)
{
    qRegisterMetaType<PageTextData>("PageTextData");
}

TextCacheManager::~TextCacheManager()
{
    cancelPreload();
    m_threadPool.waitForDone(3000);
    clear();
}

void TextCacheManager::startPreload()
{
    if (!m_renderer || !m_backgroundTaskHandler) {
        emit preloadError(tr("No renderer assigned"));
        return;
    }

    QString pdfPath;
    try {
        pdfPath = m_renderer->documentPath();
    } catch (...) {
        emit preloadError(tr("Failed to get document path"));
        return;
    }

    if (pdfPath.isEmpty()) {
        emit preloadError(tr("Empty document path"));
        return;
    }

    if (m_isPreloading.loadAcquire()) {
        cancelPreload();
        for (int i = 0; i < 30 && m_isPreloading.loadAcquire(); ++i) {
            QThread::msleep(50);
            QCoreApplication::processEvents();
        }
    }

    int pageCount = m_renderer->pageCount();
    if (pageCount <= 0) {
        emit preloadError(tr("Invalid page count"));
        return;
    }

    m_isPreloading.storeRelease(1);
    m_cancelRequested.storeRelease(0);
    m_activeTask = m_backgroundTaskHandler->startTask(PDFBackgroundTaskType::TextPreload);
    m_preloadedPages.storeRelease(0);
    m_remainingTasks.storeRelease(pageCount);

    int threadCount = qMax(4, QThread::idealThreadCount() / 2);
    m_threadPool.setMaxThreadCount(threadCount);

    int batchSize = qMax(1, (pageCount + threadCount - 1) / threadCount);

    qDebug() << "TextCacheManager: Starting preload for" << pageCount << "pages"
             << "with" << threadCount << "threads"
             << "batch size:" << batchSize;

    QVector<int> pagesToProcess;
    for (int i = 0; i < pageCount; ++i) {
        QMutexLocker locker(&m_mutex);
        if (m_cache.contains(i)) {
            m_preloadedPages.ref();
            m_remainingTasks.fetchAndSubRelaxed(1);
            emit preloadProgress(m_preloadedPages.loadAcquire(), pageCount);
        } else {
            pagesToProcess.append(i);
        }
    }

    int tasksSubmitted = 0;
    for (int i = 0; i < pagesToProcess.size(); i += batchSize) {
        QVector<int> batch;
        for (int j = 0; j < batchSize && (i + j) < pagesToProcess.size(); ++j) {
            batch.append(pagesToProcess[i + j]);
        }

        if (!batch.isEmpty()) {
            PageExtractTask* task = new PageExtractTask(this, pdfPath, batch, m_activeTask);
            m_threadPool.start(task);
            ++tasksSubmitted;
        }
    }

    qDebug() << "TextCacheManager: Submitted" << tasksSubmitted << "tasks"
             << "for" << pagesToProcess.size() << "pages";

    if (m_remainingTasks.loadAcquire() <= 0) {
        m_isPreloading.storeRelease(0);
        emit preloadCompleted();
    }
}

void TextCacheManager::cancelPreload()
{
    if (m_backgroundTaskHandler) {
        m_backgroundTaskHandler->invalidate(PDFBackgroundTaskType::TextPreload);
    }

    if (!m_isPreloading.loadAcquire()) {
        return;
    }

    m_cancelRequested.storeRelease(1);
    m_isPreloading.storeRelease(0);
    qDebug() << "TextCacheManager: Cancel requested";
    emit preloadCancelled();
}

bool TextCacheManager::isPreloading() const
{
    return m_isPreloading.loadAcquire() != 0;
}

int TextCacheManager::computePreloadProgress() const
{
    return m_preloadedPages.loadAcquire();
}

PageTextData TextCacheManager::getPageTextData(int pageIndex)
{
    QMutexLocker locker(&m_mutex);
    if (m_cache.contains(pageIndex)) {
        ++m_hitCount;
        return m_cache.value(pageIndex);
    }
    ++m_missCount;
    return PageTextData();
}

void TextCacheManager::addPageTextData(int pageIndex, const PageTextData& data)
{
    QMutexLocker locker(&m_mutex);

    if (m_maxCacheSize > 0 && m_cache.size() >= m_maxCacheSize) {
        if (!m_cache.isEmpty()) {
            auto it = m_cache.begin();
            m_cache.erase(it);
        }
    }

    m_cache.insert(pageIndex, data);
}

bool TextCacheManager::contains(int pageIndex) const
{
    QMutexLocker locker(&m_mutex);
    return m_cache.contains(pageIndex);
}

void TextCacheManager::clear()
{
    QMutexLocker locker(&m_mutex);

    if (m_isPreloading.loadAcquire()) {
        qWarning() << "TextCacheManager::clear() called while preload active!";
    }

    m_cache.clear();
    m_hitCount = 0;
    m_missCount = 0;
}

void TextCacheManager::setMaxCacheSize(int maxPages)
{
    QMutexLocker locker(&m_mutex);
    m_maxCacheSize = maxPages;
}

int TextCacheManager::cacheSize() const
{
    QMutexLocker locker(&m_mutex);
    return m_cache.size();
}

QString TextCacheManager::getStatistics() const
{
    QMutexLocker locker(&m_mutex);
    qint64 total = m_hitCount + m_missCount;
    double hitRate = (total > 0) ? (m_hitCount * 100.0 / total) : 0.0;

    return QString("TextCache: %1 pages, Hit Rate: %2%, Hits: %3, Misses: %4")
        .arg(m_cache.size())
        .arg(hitRate, 0, 'f', 1)
        .arg(m_hitCount)
        .arg(m_missCount);
}

void TextCacheManager::handleTaskDone(int pageIndex, PageTextData pageData, bool ok,
                                      PDFBackgroundTaskToken taskToken)
{
    if (!m_backgroundTaskHandler || !m_backgroundTaskHandler->isCurrent(taskToken) ||
        taskToken.documentGeneration != m_activeTask.documentGeneration ||
        taskToken.taskGeneration != m_activeTask.taskGeneration) {
        return;
    }
    int remaining = m_remainingTasks.fetchAndSubRelaxed(1) - 1;
    Q_UNUSED(remaining);

    if (ok) {
        {
            QMutexLocker locker(&m_mutex);
            if (m_maxCacheSize > 0 && m_cache.size() >= m_maxCacheSize) {
                if (!m_cache.isEmpty()) {
                    auto it = m_cache.begin();
                    m_cache.erase(it);
                }
            }
            m_cache.insert(pageIndex, pageData);
        }

        m_preloadedPages.ref();
    }

    int loaded = m_preloadedPages.loadAcquire();
    int total = loaded + qMax(0, m_remainingTasks.loadAcquire());
    emit preloadProgress(loaded, total);

    if (m_remainingTasks.loadAcquire() <= 0) {
        if (m_cancelRequested.loadAcquire()) {
            m_isPreloading.storeRelease(0);
            qDebug() << "TextCacheManager: Preload cancelled";
            emit preloadCancelled();
        } else {
            m_isPreloading.storeRelease(0);
            qDebug() << "TextCacheManager: Preload completed";
            emit preloadCompleted();
        }
    }
}
