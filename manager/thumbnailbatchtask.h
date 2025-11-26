#ifndef THUMBNAILBATCHTASK_H
#define THUMBNAILBATCHTASK_H

#include <QRunnable>
#include <QVector>
#include <QAtomicInt>
#include <QImage>
#include <QPointer>

class ThreadSafeRenderer;
class ThumbnailCache;
class ThumbnailManagerV2;

/**
 * @brief 渲染优先级
 */
enum class RenderPriority {
    IMMEDIATE,    // 立即渲染（低清，同步）
    HIGH,         // 高优先级（高清，可见区）
    MEDIUM,       // 中优先级（高清，预加载区）
    LOW           // 低优先级（低清，全文档）
};

/**
 * @brief 批量缩略图渲染任务
 *
 * 特点：
 * - 支持中断（检查 abort 标志）
 * - 支持时间预算（超时自动退出）
 * - 线程安全（使用 ThreadSafeRenderer）
 */
class ThumbnailBatchTask : public QRunnable
{
public:
    /**
     * @brief 构造函数
     * @param renderer 线程安全渲染器
     * @param cache 缓存管理器
     * @param pageIndices 要渲染的页面列表
     * @param priority 优先级
     * @param isLowRes 是否渲染低清版本
     * @param lowResWidth 低清宽度
     * @param highResWidth 高清宽度
     * @param rotation 旋转角度
     */
    explicit ThumbnailBatchTask(const QString& docPath,
                                ThumbnailCache* cache,
                                ThumbnailManagerV2* manager,
                                const QVector<int>& pageIndices,
                                RenderPriority priority,
                                bool isLowRes,
                                int lowResWidth,
                                int highResWidth,
                                int rotation);

    ~ThumbnailBatchTask();

    /**
     * @brief 执行任务
     */
    void run() override;

    /**
     * @brief 请求中断任务
     */
    void abort();

    /**
     * @brief 检查是否已中断
     */
    bool isAborted() const;

private:
    std::unique_ptr<ThreadSafeRenderer> m_renderer;
    QPointer<ThumbnailManagerV2> m_manager;
    ThumbnailCache* m_cache;
    QVector<int> m_pageIndices;
    RenderPriority m_priority;
    bool m_isLowRes;
    int m_lowResWidth;
    int m_highResWidth;
    int m_rotation;

    QAtomicInt m_aborted;

    // 时间预算（毫秒）
    int getTimeBudget() const;

    // 批大小限制
    int getBatchLimit() const;
};

#endif // THUMBNAILBATCHTASK_H
