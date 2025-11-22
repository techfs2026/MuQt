#include "thumbnailmanager.h"
#include "mupdfrenderer.h"
#include <QDebug>
#include <QMutexLocker>
#include <QRunnable>
#include <QMetaObject>
#include <QThread>

// ========== 缩略图加载任务 ==========

class ThumbnailTask : public QRunnable
{
public:
    ThumbnailTask(ThumbnailManager* manager,
                  const QString& pdfPath,
                  int pageIndex,
                  int thumbnailWidth,
                  QAtomicInt* cancelFlag)
        : m_manager(manager)
        , m_pdfPath(pdfPath)
        , m_pageIndex(pageIndex)
        , m_thumbnailWidth(thumbnailWidth)
        , m_cancelFlag(cancelFlag)
    {
        setAutoDelete(true);
    }

    void run() override
    {
        // 取消检查点 1
        if (m_cancelFlag->loadAcquire()) {
            QMetaObject::invokeMethod(m_manager, "handleTaskDone",
                                      Qt::QueuedConnection,
                                      Q_ARG(int, m_pageIndex),
                                      Q_ARG(QImage, QImage()),
                                      Q_ARG(bool, false));
            return;
        }

        // 每个线程共享 renderer，避免重复 loadDocument
        static thread_local MuPDFRenderer renderer;
        static thread_local QString loadedPath;

        // 如果文档路径不同，需要重新加载
        if (loadedPath != m_pdfPath) {
            QString error;
            if (!renderer.loadDocument(m_pdfPath, &error)) {
                QMetaObject::invokeMethod(m_manager, "handleTaskDone",
                                          Qt::QueuedConnection,
                                          Q_ARG(int, m_pageIndex),
                                          Q_ARG(QImage, QImage()),
                                          Q_ARG(bool, false));
                return;
            }
            loadedPath = m_pdfPath;
        }

        // 取消检查点 2
        if (m_cancelFlag->loadAcquire()) {
            QMetaObject::invokeMethod(m_manager, "handleTaskDone",
                                      Qt::QueuedConnection,
                                      Q_ARG(int, m_pageIndex),
                                      Q_ARG(QImage, QImage()),
                                      Q_ARG(bool, false));
            return;
        }

        // 获取页面尺寸
        QSizeF pageSize = renderer.pageSize(m_pageIndex);
        if (pageSize.isEmpty()) {
            QMetaObject::invokeMethod(m_manager, "handleTaskDone",
                                      Qt::QueuedConnection,
                                      Q_ARG(int, m_pageIndex),
                                      Q_ARG(QImage, QImage()),
                                      Q_ARG(bool, false));
            return;
        }

        // 计算缩放比例
        double scale = double(m_thumbnailWidth) / pageSize.width();

        // 取消检查点 3
        if (m_cancelFlag->loadAcquire()) {
            QMetaObject::invokeMethod(m_manager, "handleTaskDone",
                                      Qt::QueuedConnection,
                                      Q_ARG(int, m_pageIndex),
                                      Q_ARG(QImage, QImage()),
                                      Q_ARG(bool, false));
            return;
        }

        // 渲染缩略图
        auto result = renderer.renderPage(m_pageIndex, scale, 0);

        // 最后取消检查
        if (m_cancelFlag->loadAcquire()) {
            QMetaObject::invokeMethod(m_manager, "handleTaskDone",
                                      Qt::QueuedConnection,
                                      Q_ARG(int, m_pageIndex),
                                      Q_ARG(QImage, QImage()),
                                      Q_ARG(bool, false));
            return;
        }

        // 返回结果
        QMetaObject::invokeMethod(m_manager, "handleTaskDone",
                                  Qt::QueuedConnection,
                                  Q_ARG(int, m_pageIndex),
                                  Q_ARG(QImage, result.success ? result.image : QImage()),
                                  Q_ARG(bool, result.success));
    }

private:
    ThumbnailManager* m_manager;
    QString m_pdfPath;
    int m_pageIndex;
    int m_thumbnailWidth;
    QAtomicInt* m_cancelFlag;
};

// ========== ThumbnailManager 实现 ==========

ThumbnailManager::ThumbnailManager(MuPDFRenderer* renderer, QObject* parent)
    : QObject(parent)
    , m_renderer(renderer)
    , m_thumbnailWidth(DEFAULT_THUMBNAIL_WIDTH)
    , m_totalPages(0)
{
    m_isLoading.storeRelease(0);
    m_cancelRequested.storeRelease(0);
    m_loadedCount.storeRelease(0);
    m_activeTasks.storeRelease(0);
}

ThumbnailManager::~ThumbnailManager()
{
    cancelLoading();
    m_threadPool.waitForDone(3000);
    clear();
}

void ThumbnailManager::startLoading(int pageCount, int thumbnailWidth)
{
    if (!m_renderer || pageCount <= 0) {
        qWarning() << "ThumbnailManager: Invalid parameters for loading";
        return;
    }

    // 如果已有正在进行的加载，先取消
    if (m_isLoading.loadAcquire()) {
        cancelLoading();

        // 短暂等待
        for (int i = 0; i < 30 && m_isLoading.loadAcquire(); ++i) {
            QThread::msleep(50);
        }
    }

    // 获取文档路径
    QString docPath;
    try {
        docPath = m_renderer->documentPath();
    } catch (...) {
        qWarning() << "ThumbnailManager: Failed to get document path";
        return;
    }

    if (docPath.isEmpty()) {
        qWarning() << "ThumbnailManager: Empty document path";
        return;
    }

    // 清空旧数据
    {
        QMutexLocker locker(&m_cacheMutex);
        m_thumbnailCache.clear();
    }

    {
        QMutexLocker locker(&m_queueMutex);
        m_pendingPages.clear();
        m_pendingPages.reserve(pageCount);
        for (int i = 0; i < pageCount; ++i) {
            m_pendingPages.append(i);
        }
    }

    // 设置状态
    m_thumbnailWidth = thumbnailWidth;
    m_totalPages = pageCount;
    m_isLoading.storeRelease(1);
    m_cancelRequested.storeRelease(0);
    m_loadedCount.storeRelease(0);
    m_activeTasks.storeRelease(0);

    // 设置线程池
    m_threadPool.setMaxThreadCount(maxConcurrency());

    qInfo() << "ThumbnailManager: Start loading" << pageCount
            << "thumbnails, width:" << thumbnailWidth
            << "concurrency:" << maxConcurrency();

    emit loadStarted(pageCount);

    // 启动异步加载
    startAsyncLoading();
}

void ThumbnailManager::cancelLoading()
{
    if (!m_isLoading.loadAcquire()) {
        return;
    }

    m_cancelRequested.storeRelease(1);
    qInfo() << "ThumbnailManager: Cancel requested";
}

QImage ThumbnailManager::getThumbnail(int pageIndex) const
{
    QMutexLocker locker(&m_cacheMutex);
    return m_thumbnailCache.value(pageIndex, QImage());
}

bool ThumbnailManager::isLoading() const
{
    return m_isLoading.loadAcquire() != 0;
}

int ThumbnailManager::loadedCount() const
{
    return m_loadedCount.loadAcquire();
}

void ThumbnailManager::setThumbnailWidth(int width)
{
    if (width < 80 || width > 400) {
        qWarning() << "ThumbnailManager: Invalid width:" << width;
        return;
    }

    m_thumbnailWidth = width;
}

void ThumbnailManager::clear()
{
    QMutexLocker cacheLock(&m_cacheMutex);
    m_thumbnailCache.clear();

    QMutexLocker queueLock(&m_queueMutex);
    m_pendingPages.clear();

    m_loadedCount.storeRelease(0);
    m_totalPages = 0;
}

bool ThumbnailManager::contains(int pageIndex) const
{
    QMutexLocker locker(&m_cacheMutex);
    return m_thumbnailCache.contains(pageIndex);
}

// ========== 私有方法 ==========

void ThumbnailManager::startAsyncLoading()
{
    QMutexLocker locker(&m_queueMutex);

    if (m_pendingPages.isEmpty()) {
        return;
    }

    int maxTasks = maxConcurrency();
    int currentActive = m_activeTasks.loadAcquire();

    while (!m_pendingPages.isEmpty() && currentActive < maxTasks) {
        int pageIndex = m_pendingPages.takeFirst();
        locker.unlock();  // 释放锁以避免死锁

        startTaskForPage(pageIndex);

        currentActive = m_activeTasks.loadAcquire();
        locker.relock();
    }
}

void ThumbnailManager::startTaskForPage(int pageIndex)
{
    if (!m_renderer) {
        return;
    }

    QString docPath = m_renderer->documentPath();
    if (docPath.isEmpty()) {
        return;
    }

    m_activeTasks.ref();

    auto* task = new ThumbnailTask(
        this,
        docPath,
        pageIndex,
        m_thumbnailWidth,
        &m_cancelRequested
        );

    m_threadPool.start(task);
}

void ThumbnailManager::handleTaskDone(int pageIndex, const QImage& thumbnail, bool success)
{
    // 减少活动任务计数
    m_activeTasks.deref();

    if (success && !thumbnail.isNull()) {
        // 存入缓存
        {
            QMutexLocker locker(&m_cacheMutex);
            m_thumbnailCache.insert(pageIndex, thumbnail);
        }

        // 增加已加载计数
        int loaded = m_loadedCount.fetchAndAddRelaxed(1) + 1;

        // 发送信号
        emit thumbnailReady(pageIndex, thumbnail);
        emit loadProgress(loaded, m_totalPages);
    } else if (!m_cancelRequested.loadAcquire()) {
        // 只在非取消状态下报告错误
        emit loadError(pageIndex, tr("Failed to render thumbnail"));
    }

    // 检查是否所有任务都完成
    int remaining = 0;
    {
        QMutexLocker locker(&m_queueMutex);
        remaining = m_pendingPages.size();
    }

    if (remaining > 0 && !m_cancelRequested.loadAcquire()) {
        // 继续加载下一批
        startAsyncLoading();
    } else if (remaining == 0 && m_activeTasks.loadAcquire() == 0) {
        // 所有任务完成
        m_isLoading.storeRelease(0);

        if (m_cancelRequested.loadAcquire()) {
            qInfo() << "ThumbnailManager: Loading cancelled";
            emit loadCancelled();
        } else {
            qInfo() << "ThumbnailManager: Loading completed -"
                    << m_loadedCount.loadAcquire() << "/" << m_totalPages;
            emit loadCompleted();
        }
    }
}

int ThumbnailManager::maxConcurrency() const
{
    // 使用 CPU 核心数的一半，最少 2 个，最多 4 个
    int cores = QThread::idealThreadCount();
    int concurrency = qMax(2, cores / 2);
    return qMin(concurrency, 4);
}
