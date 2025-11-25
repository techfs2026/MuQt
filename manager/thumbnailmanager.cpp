#include "thumbnailmanager.h"
#include "thumbnailcache.h"
#include "threadsaferenderer.h"
#include "mupdfrenderer.h"
#include <QDebug>
#include <QMutexLocker>
#include <QElapsedTimer>

ThumbnailManager::ThumbnailManager(MuPDFRenderer* renderer, QObject* parent)
    : QObject(parent)
    , m_renderer(renderer)
    , m_cache(std::make_unique<ThumbnailCache>())
    , m_threadPool(std::make_unique<QThreadPool>())
    , m_lowResWidth(40)
    , m_highResWidth(120)
    , m_rotation(0)
{
    // 配置线程池
    m_threadPool->setMaxThreadCount(3);  // 最多 3 个渲染线程
    m_threadPool->setExpiryTimeout(30000);  // 30s 后回收空闲线程

    qInfo() << "ThumbnailManager: Initialized with"
            << m_threadPool->maxThreadCount() << "threads";
}

ThumbnailManager::~ThumbnailManager()
{
    qInfo() << "ThumbnailManager: Destructor called";

    clear();

    m_threadSafeRenderer.reset();  // 重置线程安全渲染器

    qInfo() << "ThumbnailManager: Destroyed";
}

// ========== 配置 ==========

void ThumbnailManager::setLowResWidth(int width)
{
    if (width < 20 || width > 100) {
        qWarning() << "ThumbnailManager: Invalid low-res width:" << width;
        return;
    }
    m_lowResWidth = width;
}

void ThumbnailManager::setHighResWidth(int width)
{
    if (width < 80 || width > 400) {
        qWarning() << "ThumbnailManager: Invalid high-res width:" << width;
        return;
    }
    m_highResWidth = width;
}

void ThumbnailManager::setRotation(int rotation)
{
    m_rotation = rotation;
}

// ========== 获取缩略图 ==========

QImage ThumbnailManager::getThumbnail(int pageIndex, bool preferHighRes)
{
    if (preferHighRes) {
        // 优先返回高清
        QImage highRes = m_cache->getHighRes(pageIndex);
        if (!highRes.isNull()) {
            return highRes;
        }

        // 高清不存在,返回低清
        return m_cache->getLowRes(pageIndex);
    } else {
        // 只返回低清
        return m_cache->getLowRes(pageIndex);
    }
}

bool ThumbnailManager::hasThumbnail(int pageIndex) const
{
    return m_cache->hasLowRes(pageIndex) || m_cache->hasHighRes(pageIndex);
}

// ========== 渲染请求 ==========

void ThumbnailManager::renderLowResImmediate(const QVector<int>& pageIndices)
{
    if (!m_renderer || pageIndices.isEmpty()) {
        return;
    }

    qDebug() << "ThumbnailManager: Rendering" << pageIndices.size()
             << "low-res thumbnails immediately (UI thread)";

    QElapsedTimer timer;
    timer.start();

    int rendered = 0;
    for (int pageIndex : pageIndices) {
        // 跳过已缓存的
        if (m_cache->hasLowRes(pageIndex)) {
            continue;
        }

        // 计算缩放比例
        QSizeF pageSize = m_renderer->pageSize(pageIndex);
        if (pageSize.isEmpty()) {
            continue;
        }

        double zoom = m_lowResWidth / pageSize.width();

        // 同步渲染(使用 UI 线程的 renderer)
        MuPDFRenderer::RenderResult result = m_renderer->renderPage(pageIndex, zoom, m_rotation);

        if (result.success && !result.image.isNull()) {
            m_cache->setLowRes(pageIndex, result.image);
            emit thumbnailLoaded(pageIndex, result.image, false);
            rendered++;
        }
    }

    qint64 elapsed = timer.elapsed();
    qDebug() << "ThumbnailManager: Rendered" << rendered
             << "low-res thumbnails in" << elapsed << "ms"
             << "(" << (rendered > 0 ? elapsed / rendered : 0) << "ms/page)";
}

void ThumbnailManager::renderHighResAsync(const QVector<int>& pageIndices,
                                          RenderPriority priority)
{
    if (!m_renderer || pageIndices.isEmpty()) {
        return;
    }

    // 创建线程安全渲染器(如果还没有)
    if (!m_threadSafeRenderer) {
        QString docPath = m_renderer->currentFilePath();
        if (docPath.isEmpty()) {
            qWarning() << "ThumbnailManager: No document loaded";
            return;
        }
        m_threadSafeRenderer = std::make_unique<ThreadSafeRenderer>(docPath);
        if (!m_threadSafeRenderer->isValid()) {
            qCritical() << "ThumbnailManager: Failed to create thread-safe renderer";
            m_threadSafeRenderer.reset();
            return;
        }
    }

    // 过滤已缓存的页面
    QVector<int> toRender;
    for (int pageIndex : pageIndices) {
        if (!m_cache->hasHighRes(pageIndex)) {
            toRender.append(pageIndex);
        }
    }

    if (toRender.isEmpty()) {
        return;
    }

    qDebug() << "ThumbnailManager: Scheduling" << toRender.size()
             << "high-res thumbnails (priority:"
             << static_cast<int>(priority) << ")";

    // 创建批任务(传入 this 用于发送信号)
    auto* task = new ThumbnailBatchTask(
        m_threadSafeRenderer.get(),
        m_cache.get(),
        this,  // 🔥 关键修复: 传入 manager 用于发送信号
        toRender,
        priority,
        false,  // 高清
        m_lowResWidth,
        m_highResWidth,
        m_rotation
        );

    // 跟踪任务
    trackTask(task);

    // 提交到线程池
    m_threadPool->start(task, static_cast<int>(priority));
}

void ThumbnailManager::renderLowResAsync(const QVector<int>& pageIndices)
{
    if (!m_renderer || pageIndices.isEmpty()) {
        return;
    }

    // 创建线程安全渲染器(如果还没有)
    if (!m_threadSafeRenderer) {
        QString docPath = m_renderer->currentFilePath();
        if (docPath.isEmpty()) {
            qWarning() << "ThumbnailManager: No document loaded";
            return;
        }
        m_threadSafeRenderer = std::make_unique<ThreadSafeRenderer>(docPath);
        if (!m_threadSafeRenderer->isValid()) {
            qCritical() << "ThumbnailManager: Failed to create thread-safe renderer";
            m_threadSafeRenderer.reset();
            return;
        }
    }

    // 过滤已缓存的页面
    QVector<int> toRender;
    for (int pageIndex : pageIndices) {
        if (!m_cache->hasLowRes(pageIndex)) {
            toRender.append(pageIndex);
        }
    }

    if (toRender.isEmpty()) {
        return;
    }

    qDebug() << "ThumbnailManager: Scheduling" << toRender.size()
             << "low-res thumbnails (background)";

    // 创建批任务(传入 this 用于发送信号)
    auto* task = new ThumbnailBatchTask(
        m_threadSafeRenderer.get(),
        m_cache.get(),
        this,  // 🔥 关键修复: 传入 manager 用于发送信号
        toRender,
        RenderPriority::LOW,
        true,  // 低清
        m_lowResWidth,
        m_highResWidth,
        m_rotation
        );

    // 跟踪任务
    trackTask(task);

    // 提交到线程池(最低优先级)
    m_threadPool->start(task, 0);
}

// ========== 任务控制 ==========

void ThumbnailManager::cancelAllTasks()
{
    QMutexLocker locker(&m_taskMutex);

    qDebug() << "ThumbnailManager: Cancelling" << m_activeTasks.size() << "tasks";

    // 遍历并中断所有任务
    for (ThumbnailBatchTask* task : m_activeTasks) {
        if (task) {
            task->abort();
        }
    }

    m_activeTasks.clear();
}

void ThumbnailManager::cancelLowPriorityTasks()
{
    QMutexLocker locker(&m_taskMutex);

    // TODO: 需要在任务中记录优先级,才能选择性取消
    // 目前简单实现:取消所有任务
    for (ThumbnailBatchTask* task : m_activeTasks) {
        task->abort();
    }
}

void ThumbnailManager::waitForCompletion()
{
    m_threadPool->waitForDone();
}

// ========== 管理 ==========

void ThumbnailManager::clear()
{
    cancelAllTasks();
    waitForCompletion();
    m_cache->clear();

    qInfo() << "ThumbnailManager: Cache cleared";
}

QString ThumbnailManager::getStatistics() const
{
    return m_cache->getStatistics();
}

int ThumbnailManager::cachedCount() const
{
    return qMax(m_cache->lowResCount(), m_cache->highResCount());
}

// ========== 私有方法 ==========

void ThumbnailManager::trackTask(ThumbnailBatchTask* task)
{
    QMutexLocker locker(&m_taskMutex);
    m_activeTasks.append(task);
}

void ThumbnailManager::untrackTask(ThumbnailBatchTask* task)
{
    QMutexLocker locker(&m_taskMutex);
    m_activeTasks.removeOne(task);
}
