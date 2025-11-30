#ifndef THUMBNAILBATCHTASK_H
#define THUMBNAILBATCHTASK_H

#include <QRunnable>
#include <QVector>
#include <QAtomicInt>
#include <QImage>
#include <QPointer>

class PerThreadMuPDFRenderer;
class ThumbnailCache;
class ThumbnailManagerV2;

/**
 * @brief 渲染优先级
 */
enum class RenderPriority {
    IMMEDIATE,    // 立即渲染（同步）
    HIGH,         // 高优先级（可见区）
    MEDIUM,       // 中优先级（预加载区）
    LOW           // 低优先级（后台批次）
};

/**
 * @brief 缩略图批次渲染任务（支持高DPI）
 */
class ThumbnailBatchTask : public QRunnable
{
public:
    ThumbnailBatchTask(const QString& docPath,
                       ThumbnailCache* cache,
                       ThumbnailManagerV2* manager,
                       const QVector<int>& pageIndices,
                       RenderPriority priority,
                       int thumbnailWidth,        // 实际渲染宽度（已乘以DPR）
                       int rotation,
                       double devicePixelRatio);  // 设备像素比

    ~ThumbnailBatchTask();

    void run() override;
    void abort();
    bool isAborted() const;

private:
    int getTimeBudget() const;
    int getBatchLimit() const;

    std::unique_ptr<PerThreadMuPDFRenderer> m_renderer;
    ThumbnailCache* m_cache;
    ThumbnailManagerV2* m_manager;
    QVector<int> m_pageIndices;
    RenderPriority m_priority;
    int m_thumbnailWidth;        // 实际渲染宽度
    int m_rotation;
    double m_devicePixelRatio;   // 设备像素比
    QAtomicInt m_aborted;
};

#endif // THUMBNAILBATCHTASK_H
