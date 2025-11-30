#include "thumbnailbatchtask.h"
#include "perthreadmupdfrenderer.h"
#include "thumbnailcache.h"
#include "thumbnailmanagerv2.h"
#include <QElapsedTimer>
#include <QDebug>

ThumbnailBatchTask::ThumbnailBatchTask(const QString& docPath,
                                       ThumbnailCache* cache,
                                       ThumbnailManagerV2* manager,
                                       const QVector<int>& pageIndices,
                                       RenderPriority priority,
                                       int thumbnailWidth,
                                       int rotation,
                                       double devicePixelRatio)
    : m_renderer(std::make_unique<PerThreadMuPDFRenderer>(docPath))
    , m_cache(cache)
    , m_manager(manager)
    , m_pageIndices(pageIndices)
    , m_priority(priority)
    , m_thumbnailWidth(thumbnailWidth)
    , m_rotation(rotation)
    , m_devicePixelRatio(devicePixelRatio)
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

    for (int pageIndex : m_pageIndices) {
        if (isAborted()) {
            qDebug() << "ThumbnailBatchTask: Aborted after rendering" << rendered << "pages";
            break;
        }

        if (!m_manager) {
            qWarning() << "ThumbnailBatchTask: Manager destroyed during rendering";
            break;
        }

        if (rendered >= batchLimit) {
            qDebug() << "ThumbnailBatchTask: Batch limit reached";
            break;
        }

        if (timer.elapsed() > timeBudget) {
            qDebug() << "ThumbnailBatchTask: Time budget exceeded:" << timer.elapsed() << "ms";
            break;
        }

        // 检查是否已缓存
        if (m_cache->has(pageIndex)) {
            continue;
        }

        // 计算缩放比例（使用高DPI渲染宽度）
        QSizeF pageSize = m_renderer->pageSize(pageIndex);
        if (pageSize.isEmpty()) {
            qWarning() << "ThumbnailBatchTask: Invalid page size for page" << pageIndex;
            continue;
        }

        double zoom = m_thumbnailWidth / pageSize.width();

        // 渲染页面
        RenderResult thumbnailRes = m_renderer->renderPage(pageIndex, zoom, m_rotation);

        QImage thumbnail = thumbnailRes.image;

        if (thumbnail.isNull()) {
            qWarning() << "ThumbnailBatchTask: Failed to render page" << pageIndex;
            continue;
        }

        // 设置设备像素比
        thumbnail.setDevicePixelRatio(m_devicePixelRatio);

        // 保存到缓存
        m_cache->set(pageIndex, thumbnail);

        // 通知UI
        if (m_manager) {
            QMetaObject::invokeMethod(m_manager, "thumbnailLoaded",
                                      Qt::QueuedConnection,
                                      Q_ARG(int, pageIndex),
                                      Q_ARG(QImage, thumbnail));
        }

        rendered++;
    }

    qint64 elapsed = timer.elapsed();
    if (rendered > 0) {
        qDebug() << "ThumbnailBatchTask: Rendered" << rendered
                 << "pages in" << elapsed << "ms"
                 << "(" << (elapsed / rendered) << "ms/page)"
                 << "at" << m_thumbnailWidth << "px (DPR:" << m_devicePixelRatio << ")";
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
        return 100;
    case RenderPriority::HIGH:
        return 500;
    case RenderPriority::MEDIUM:
        return 2000;
    case RenderPriority::LOW:
        return 5000;
    }
    return 1000;
}

int ThumbnailBatchTask::getBatchLimit() const
{
    switch (m_priority) {
    case RenderPriority::IMMEDIATE:
        return 10;
    case RenderPriority::HIGH:
        return 10;
    case RenderPriority::MEDIUM:
        return 20;
    case RenderPriority::LOW:
        return 50;
    }
    return 10;
}
