#ifndef THUMBNAILMANAGER_H
#define THUMBNAILMANAGER_H

#include <QObject>
#include <QImage>
#include <QMap>
#include <QVector>
#include <QAtomicInt>
#include <QThreadPool>
#include <QMutex>

class MuPDFRenderer;

/**
 * @brief 缩略图管理器
 *
 * 负责异步加载和缓存 PDF 页面缩略图
 * - 支持多线程并行加载
 * - 支持取消操作
 * - 自动管理缓存
 */
class ThumbnailManager : public QObject
{
    Q_OBJECT

public:
    explicit ThumbnailManager(MuPDFRenderer* renderer, QObject* parent = nullptr);
    ~ThumbnailManager();

    /**
     * @brief 开始加载缩略图
     * @param pageCount 总页数
     * @param thumbnailWidth 缩略图宽度（默认 120）
     */
    void startLoading(int pageCount, int thumbnailWidth = 120);

    /**
     * @brief 取消当前加载任务
     */
    void cancelLoading();

    /**
     * @brief 获取指定页面的缩略图
     * @param pageIndex 页面索引
     * @return 缩略图图像，如果不存在返回空图像
     */
    QImage getThumbnail(int pageIndex) const;

    /**
     * @brief 检查缩略图是否正在加载
     */
    bool isLoading() const;

    /**
     * @brief 获取已加载的缩略图数量
     */
    int loadedCount() const;

    /**
     * @brief 设置缩略图宽度
     * @param width 缩略图宽度
     */
    void setThumbnailWidth(int width);

    /**
     * @brief 获取缩略图宽度
     */
    int thumbnailWidth() const { return m_thumbnailWidth; }

    /**
     * @brief 清空所有缩略图缓存
     */
    void clear();

    /**
     * @brief 检查是否包含指定页面的缩略图
     */
    bool contains(int pageIndex) const;

signals:
    /**
     * @brief 加载开始
     * @param totalPages 总页数
     */
    void loadStarted(int totalPages);

    /**
     * @brief 加载进度
     * @param loadedCount 已加载数量
     * @param totalCount 总数量
     */
    void loadProgress(int loadedCount, int totalCount);

    /**
     * @brief 单个缩略图加载完成
     * @param pageIndex 页面索引
     * @param thumbnail 缩略图
     */
    void thumbnailReady(int pageIndex, const QImage& thumbnail);

    /**
     * @brief 所有缩略图加载完成
     */
    void loadCompleted();

    /**
     * @brief 加载取消
     */
    void loadCancelled();

    /**
     * @brief 加载错误
     * @param pageIndex 页面索引
     * @param errorMessage 错误信息
     */
    void loadError(int pageIndex, const QString& errorMessage);

public slots:
    /**
     * @brief 处理任务完成（由工作线程调用）
     * @param pageIndex 页面索引
     * @param thumbnail 缩略图
     * @param success 是否成功
     */
    void handleTaskDone(int pageIndex, const QImage& thumbnail, bool success);

private:
    MuPDFRenderer* m_renderer;
    QThreadPool m_threadPool;

    // 缩略图缓存
    mutable QMutex m_cacheMutex;
    QMap<int, QImage> m_thumbnailCache;

    // 加载状态
    QAtomicInt m_isLoading;
    QAtomicInt m_cancelRequested;
    QAtomicInt m_loadedCount;
    QAtomicInt m_activeTasks;

    // 配置
    int m_thumbnailWidth;
    int m_totalPages;

    // 待加载队列
    mutable QMutex m_queueMutex;
    QVector<int> m_pendingPages;

    // 启动异步加载任务
    void startAsyncLoading();

    // 为指定页面启动加载任务
    void startTaskForPage(int pageIndex);

    // 默认缩略图宽度
    static constexpr int DEFAULT_THUMBNAIL_WIDTH = 120;

    // 最大并发任务数（使用 CPU 核心数的一半）
    int maxConcurrency() const;
};

#endif // THUMBNAILMANAGER_H
