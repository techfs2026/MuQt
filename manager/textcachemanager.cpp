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

// ========================================
// PageExtractTask - 批处理文本提取任务
// ========================================
class PageExtractTask : public QRunnable
{
public:
    PageExtractTask(TextCacheManager* manager,
                    const QString& pdfPath,
                    const QVector<int>& pageIndices)
        : m_manager(manager)
        , m_pdfPath(pdfPath)
        , m_pageIndices(pageIndices)
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

        // 检查取消
        if (m_manager->m_cancelRequested.loadAcquire()) {
            qDebug() << "PageExtractTask: Cancelled before start, pages:" << m_pageIndices;
            reportAllFailed();
            return;
        }

        // 创建独立的渲染器实例（线程安全）
        // 一个 task 共享一个 renderer 实例，避免重复打开文档
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

        // 批量提取文本
        int successCount = 0;
        int failCount = 0;

        for (int pageIndex : m_pageIndices) {
            // 每页处理前都检查取消
            if (m_manager->m_cancelRequested.loadAcquire()) {
                qDebug() << "PageExtractTask: Cancelled at page" << pageIndex;
                reportDone(pageIndex, PageTextData(), false);
                failCount++;
                continue;
            }

            // 验证页面索引
            if (pageIndex < 0 || pageIndex >= totalPages) {
                qWarning() << "PageExtractTask: Invalid page index" << pageIndex
                           << "total pages:" << totalPages;
                reportDone(pageIndex, PageTextData(), false);
                failCount++;
                continue;
            }

            // 提取文本
            PageTextData pageData;

            m_renderer->extractText(pageIndex, pageData);

            // 区分空白页和真正的错误
            QString error = m_renderer->getLastError();
            bool hasError = !error.isEmpty();
            bool isBlankPage = pageData.blocks.isEmpty() && !hasError;
            bool success = !hasError; // 只要没有错误就算成功（空白页也是成功）

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
                                  Q_ARG(bool, ok));
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
    std::unique_ptr<PerThreadMuPDFRenderer> m_renderer;
};

// ========================================
// TextCacheManager 实现
// ========================================
TextCacheManager::TextCacheManager(PerThreadMuPDFRenderer* renderer, QObject* parent)
    : QObject(parent)
    , m_renderer(renderer)
    , m_maxCacheSize(-1)
    , m_isPreloading(0)
    , m_cancelRequested(0)
    , m_preloadedPages(0)
    , m_hitCount(0)
    , m_missCount(0)
{
}

TextCacheManager::~TextCacheManager()
{
    cancelPreload();
    m_threadPool.waitForDone(3000);
    clear();
}

void TextCacheManager::startPreload()
{
    if (!m_renderer) {
        emit preloadError(QStringLiteral("No renderer assigned"));
        return;
    }

    // 获取文档路径
    QString pdfPath;
    try {
        pdfPath = m_renderer->documentPath();
    } catch (...) {
        emit preloadError(QStringLiteral("Failed to get document path"));
        return;
    }

    if (pdfPath.isEmpty()) {
        emit preloadError(QStringLiteral("Empty document path"));
        return;
    }

    // 如果已有预加载在运行，先取消
    if (m_isPreloading.loadAcquire()) {
        cancelPreload();
        for (int i = 0; i < 30 && m_isPreloading.loadAcquire(); ++i) {
            QThread::msleep(50);
            QCoreApplication::processEvents();
        }
    }

    int pageCount = m_renderer->pageCount();
    if (pageCount <= 0) {
        emit preloadError(QStringLiteral("Invalid page count"));
        return;
    }

    // 设置并发状态
    m_isPreloading.storeRelease(1);
    m_cancelRequested.storeRelease(0);
    m_preloadedPages.storeRelease(0);
    m_remainingTasks.storeRelease(pageCount);

    // 配置线程池（使用一半的CPU核心）
    int threadCount = qMax(4, QThread::idealThreadCount() / 2);
    m_threadPool.setMaxThreadCount(threadCount);

    // 计算每个批次的大小
    // 批次大小 = ceil(页数 / 线程数)，确保页面均匀分布
    int batchSize = qMax(1, (pageCount + threadCount - 1) / threadCount);

    qDebug() << "TextCacheManager: Starting preload for" << pageCount << "pages"
             << "with" << threadCount << "threads"
             << "batch size:" << batchSize;

    // 收集需要处理的页面（跳过已缓存的）
    QVector<int> pagesToProcess;
    for (int i = 0; i < pageCount; ++i) {
        QMutexLocker locker(&m_mutex);
        if (m_cache.contains(i)) {
            // 已在缓存中，直接计数
            m_preloadedPages.ref();
            m_remainingTasks.fetchAndSubRelaxed(1);
            emit preloadProgress(m_preloadedPages.loadAcquire(), pageCount);
        } else {
            pagesToProcess.append(i);
        }
    }

    // 按批次提交任务
    int tasksSubmitted = 0;
    for (int i = 0; i < pagesToProcess.size(); i += batchSize) {
        // 创建当前批次的页面列表
        QVector<int> batch;
        for (int j = 0; j < batchSize && (i + j) < pagesToProcess.size(); ++j) {
            batch.append(pagesToProcess[i + j]);
        }

        if (!batch.isEmpty()) {
            PageExtractTask* task = new PageExtractTask(this, pdfPath, batch);
            m_threadPool.start(task);
            ++tasksSubmitted;
        }
    }

    qDebug() << "TextCacheManager: Submitted" << tasksSubmitted << "tasks"
             << "for" << pagesToProcess.size() << "pages";

    // Edge case: 所有页都已在缓存中
    if (m_remainingTasks.loadAcquire() <= 0) {
        m_isPreloading.storeRelease(0);
        emit preloadCompleted();
    }
}

void TextCacheManager::cancelPreload()
{
    if (!m_isPreloading.loadAcquire()) {
        return;
    }

    m_cancelRequested.storeRelease(1);
    qDebug() << "TextCacheManager: Cancel requested";
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

    // 如果超过最大缓存大小，移除最旧的条目
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

void TextCacheManager::handleTaskDone(int pageIndex, PageTextData pageData, bool ok)
{
    // 无论成功与否，都要递减剩余任务计数
    int remaining = m_remainingTasks.fetchAndSubRelaxed(1) - 1;
    Q_UNUSED(remaining);

    if (ok) {
        // 成功提取，写入缓存
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

    // 发送进度信号
    int loaded = m_preloadedPages.loadAcquire();
    int total = loaded + qMax(0, m_remainingTasks.loadAcquire());
    emit preloadProgress(loaded, total);

    // 检查是否所有任务都完成
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
