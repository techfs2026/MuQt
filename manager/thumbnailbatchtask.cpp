#include "thumbnailbatchtask.h"
#include "threadsaferenderer.h"
#include "thumbnailcache.h"
#include "thumbnailmanager.h"
#include <QElapsedTimer>
#include <QDebug>

ThumbnailBatchTask::ThumbnailBatchTask(ThreadSafeRenderer* renderer,
                                       ThumbnailCache* cache,
                                       ThumbnailManager* manager,
                                       const QVector<int>& pageIndices,
                                       RenderPriority priority,
                                       bool isLowRes,
                                       int lowResWidth,
                                       int highResWidth,
                                       int rotation)
    : m_renderer(renderer)
    , m_cache(cache)
    , m_manager(manager)
    , m_pageIndices(pageIndices)
    , m_priority(priority)
    , m_isLowRes(isLowRes)
    , m_lowResWidth(lowResWidth)
    , m_highResWidth(highResWidth)
    , m_rotation(rotation)
    , m_aborted(0)
{
    setAutoDelete(true);
}

ThumbnailBatchTask::~ThumbnailBatchTask()
{
}

void ThumbnailBatchTask::run()
{
    if (!m_renderer || !m_cache || !m_manager) {
        qWarning() << "ThumbnailBatchTask: Invalid renderer, cache or manager";
        return;
    }

    QElapsedTimer timer;
    timer.start();

    int timeBudget = getTimeBudget();
    int batchLimit = getBatchLimit();
    int rendered = 0;

    QString priorityStr = m_isLowRes ? "LOW-RES" : "HIGH-RES";

    for (int pageIndex : m_pageIndices) {
        // 检查中断标志
        if (isAborted()) {
            qDebug() << "ThumbnailBatchTask:" << priorityStr
                     << "aborted after rendering" << rendered << "pages";
            break;
        }

        if (!m_manager) {
            qWarning() << "ThumbnailBatchTask: Manager destroyed during rendering";
            break;
        }

        // 检查批大小限制
        if (rendered >= batchLimit) {
            qDebug() << "ThumbnailBatchTask:" << priorityStr
                     << "batch limit reached";
            break;
        }

        // 检查时间预算
        if (timer.elapsed() > timeBudget) {
            qDebug() << "ThumbnailBatchTask:" << priorityStr
                     << "time budget exceeded:" << timer.elapsed() << "ms";
            break;
        }

        // 检查是否已缓存
        if (m_isLowRes && m_cache->hasLowRes(pageIndex)) {
            continue;
        }
        if (!m_isLowRes && m_cache->hasHighRes(pageIndex)) {
            continue;
        }

        // 计算缩放比例
        QSizeF pageSize = m_renderer->getPageSize(pageIndex);
        if (pageSize.isEmpty()) {
            qWarning() << "ThumbnailBatchTask: Invalid page size for page" << pageIndex;
            continue;
        }

        int targetWidth = m_isLowRes ? m_lowResWidth : m_highResWidth;
        double zoom = targetWidth / pageSize.width();

        // 渲染页面(线程安全)
        QImage thumbnail = m_renderer->renderPage(pageIndex, zoom, m_rotation);

        if (thumbnail.isNull()) {
            qWarning() << "ThumbnailBatchTask: Failed to render page" << pageIndex;
            continue;
        }

        // 保存到缓存
        if (m_isLowRes) {
            m_cache->setLowRes(pageIndex, thumbnail);
        } else {
            m_cache->setHighRes(pageIndex, thumbnail);
        }

        if (m_manager) {
            QMetaObject::invokeMethod(m_manager.data(), "thumbnailLoaded",
                                      Qt::QueuedConnection,
                                      Q_ARG(int, pageIndex),
                                      Q_ARG(QImage, thumbnail),
                                      Q_ARG(bool, !m_isLowRes));
        }

        rendered++;
    }

    qint64 elapsed = timer.elapsed();
    if (rendered > 0) {
        qDebug() << "ThumbnailBatchTask:" << priorityStr
                 << "rendered" << rendered << "pages in" << elapsed << "ms"
                 << "(" << (elapsed / rendered) << "ms/page)";
    }
}

void ThumbnailBatchTask::abort()
{
    m_aborted.storeRelaxed(1);
}

bool ThumbnailBatchTask::isAborted() const
{
    return m_aborted.loadRelaxed() != 0;
}

int ThumbnailBatchTask::getTimeBudget() const
{
    switch (m_priority) {
    case RenderPriority::IMMEDIATE:
        return 100;   // 100ms(低清渲染很快)
    case RenderPriority::HIGH:
        return 500;   // 500ms(高清可见区)
    case RenderPriority::MEDIUM:
        return 2000;  // 2s(高清预加载区)
    case RenderPriority::LOW:
        return 5000;  // 5s(低清全文档)
    }
    return 1000;
}

int ThumbnailBatchTask::getBatchLimit() const
{
    switch (m_priority) {
    case RenderPriority::IMMEDIATE:
        return 10;  // 立即渲染可见区(约 10 页)
    case RenderPriority::HIGH:
        return 10;  // 高优先级(可见区)
    case RenderPriority::MEDIUM:
        return 20;  // 中优先级(预加载区)
    case RenderPriority::LOW:
        return 50;  // 低优先级(大批量)
    }
    return 10;
}
