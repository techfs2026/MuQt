#ifndef THUMBNAILMANAGER_V2_H
#define THUMBNAILMANAGER_V2_H

#include <QObject>
#include <QThreadPool>
#include <QMutex>
#include <QTimer>
#include <memory>

#include "thumbnailbatchtask.h"
#include "thumbnailloadstrategy.h"

class MuPDFRenderer;
class ThumbnailCache;

/**
 * @brief 智能缩略图管理器 V2 - 基于文档大小的自适应加载策略
 */
class ThumbnailManagerV2 : public QObject
{
    Q_OBJECT

public:
    explicit ThumbnailManagerV2(MuPDFRenderer* renderer, QObject* parent = nullptr);
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
     * @brief 处理可见区域变化（仅对大文档有效）
     */
    void handleVisibleRangeChanged(const QSet<int>& visiblePages);

    /**
     * @brief 取消所有任务
     */
    void cancelAllTasks();

    /**
     * @brief 等待所有任务完成
     */
    void waitForCompletion();

    void syncLoadPages(const QVector<int>& pages);

    // ========== 管理 ==========
    void clear();
    QString getStatistics() const;
    int cachedCount() const;

signals:
    void thumbnailLoaded(int pageIndex, const QImage& thumbnail, bool isHighRes);
    void loadProgress(int loaded, int total);
    void batchCompleted(int batchIndex, int totalBatches);
    void allCompleted();

private slots:
    void onBatchTaskFinished();
    void processNextBatch();

private:
    void renderPagesSync(const QVector<int>& pages);
    void renderPagesAsync(const QVector<int>& pages, RenderPriority priority);
    void setupBackgroundBatches();

private:
    MuPDFRenderer* m_renderer;
    std::unique_ptr<ThumbnailCache> m_cache;
    std::unique_ptr<QThreadPool> m_threadPool;
    std::unique_ptr<ThumbnailLoadStrategy> m_strategy;

    int m_thumbnailWidth;
    int m_rotation;

    // 批次管理
    QVector<QVector<int>> m_backgroundBatches;
    int m_currentBatchIndex;
    QTimer* m_batchTimer;

    // 任务跟踪
    QMutex m_taskMutex;
    QVector<ThumbnailBatchTask*> m_activeTasks;

    void trackTask(ThumbnailBatchTask* task);
    void untrackTask(ThumbnailBatchTask* task);
};

#endif // THUMBNAILMANAGER_V2_H
