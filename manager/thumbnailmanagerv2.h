#ifndef THUMBNAILMANAGER_V2_H
#define THUMBNAILMANAGER_V2_H

#include <QObject>
#include <QThreadPool>
#include <QMutex>
#include <QTimer>
#include <memory>

#include "thumbnailbatchtask.h"
#include "thumbnailloadstrategy.h"

class ThreadSafeRenderer;
class ThumbnailCache;

/**
 * @brief 智能缩略图管理器 V2 - 高DPI支持版
 *
 * 加载策略:
 * - 小文档(<50页): 全量同步加载
 * - 中文档(50-200页): 可见区同步 + 后台批次异步
 * - 大文档(>200页): 慢速滚动预加载 + 滚动停止同步加载
 *
 * 高DPI支持:
 * - 自动检测屏幕设备像素比（1x, 2x, 3x等）
 * - 按设备像素比渲染高分辨率缩略图
 * - 在高DPI屏幕上显示清晰图像
 */
class ThumbnailManagerV2 : public QObject
{
    Q_OBJECT

public:
    explicit ThumbnailManagerV2(ThreadSafeRenderer* renderer, QObject* parent = nullptr);
    ~ThumbnailManagerV2();

    // ========== 配置 ==========
    void setThumbnailWidth(int width);
    void setRotation(int rotation);

    // ========== 获取缩略图 ==========
    QImage getThumbnail(int pageIndex) const;
    bool hasThumbnail(int pageIndex) const;

    // ========== 加载控制 ==========

    /**
     * @brief 启动缩略图加载（根据策略自动选择）
     * @param initialVisible 初始可见页面
     */
    void startLoading(const QSet<int>& initialVisible);

    /**
     * @brief 同步加载指定页面（主要用于大文档滚动停止后）
     * @param pages 需要加载的页面索引
     */
    void syncLoadPages(const QVector<int>& pages);

    /**
     * @brief 处理慢速滚动（大文档专用）
     * @param visiblePages 当前可见的页面索引
     */
    void handleSlowScroll(const QSet<int>& visiblePages);

    /**
     * @brief 取消所有后台任务（仅中文档使用）
     */
    void cancelAllTasks();

    /**
     * @brief 等待所有任务完成
     */
    void waitForCompletion();

    // ========== 管理 ==========
    void clear();
    QString getStatistics() const;
    int cachedCount() const;

    /**
     * @brief 是否应该响应滚动事件
     */
    bool shouldRespondToScroll() const;

    ThumbnailLoadStrategy* thumbnailLoadStrategy() const {return m_strategy.get();}

signals:
    void thumbnailLoaded(int pageIndex, const QImage& thumbnail);
    void loadProgress(int loaded, int total);
    void batchCompleted(int batchIndex, int totalBatches);
    void allCompleted();

    void loadingStarted(int totalPages, const QString& strategy);
    void loadingStatusChanged(const QString& status);

private slots:
    void processNextBatch();

private:
    // 检测设备像素比
    void detectDevicePixelRatio();

    // 获取实际渲染宽度（显示宽度 × 设备像素比）
    int getRenderWidth() const;

    // 同步渲染
    void renderPagesSync(const QVector<int>& pages);

    // 异步渲染
    void renderPagesAsync(const QVector<int>& pages, RenderPriority priority);

    // 设置中文档后台批次
    void setupBackgroundBatches();

    void trackTask(ThumbnailBatchTask* task);

private:
    ThreadSafeRenderer* m_renderer;
    std::unique_ptr<ThumbnailCache> m_cache;
    std::unique_ptr<QThreadPool> m_threadPool;
    std::unique_ptr<ThumbnailLoadStrategy> m_strategy;

    int m_thumbnailWidth;      // 显示宽度（逻辑像素）
    int m_rotation;
    double m_devicePixelRatio; // 设备像素比（1.0, 2.0, 3.0等）

    // 批次管理（仅中文档使用）
    QVector<QVector<int>> m_backgroundBatches;
    int m_currentBatchIndex;
    QTimer* m_batchTimer;

    // 任务跟踪（仅中文档使用）
    QMutex m_taskMutex;

    bool m_isLoadingInProgress;
};

#endif // THUMBNAILMANAGER_V2_H
