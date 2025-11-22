#ifndef PAGECACHEMANAGER_H
#define PAGECACHEMANAGER_H

#include <QImage>
#include <QMap>
#include <QSet>
#include <QMutex>
#include <QHash>

/**
 * @brief 页面缓存键
 *
 * 包含页码、缩放比例和旋转角度，确保缓存的唯一性
 */
struct PageCacheKey {
    int pageIndex;      ///< 页码
    double zoom;        ///< 缩放比例
    int rotation;       ///< 旋转角度

    /**
     * @brief 构造函数
     */
    PageCacheKey(int page = -1, double z = 1.0, int rot = 0)
        : pageIndex(page), zoom(z), rotation(rot) {}

    /**
     * @brief 相等比较（考虑浮点数精度）
     */
    bool operator==(const PageCacheKey& other) const {
        return pageIndex == other.pageIndex &&
               qAbs(zoom - other.zoom) < 0.001 &&
               rotation == other.rotation;
    }

    /**
     * @brief 小于比较（用于QMap排序）
     */
    bool operator<(const PageCacheKey& other) const {
        if (pageIndex != other.pageIndex)
            return pageIndex < other.pageIndex;
        if (qAbs(zoom - other.zoom) >= 0.001)
            return zoom < other.zoom;
        return rotation < other.rotation;
    }

    /**
     * @brief 生成哈希值（用于QHash，可选）
     */
    uint hash() const {
        return qHash(pageIndex) ^ qHash(qRound(zoom * 1000)) ^ qHash(rotation);
    }

    /**
     * @brief 转换为字符串（调试用）
     */
    QString toString() const {
        return QString("Page:%1,Zoom:%2,Rot:%3")
        .arg(pageIndex).arg(zoom, 0, 'f', 2).arg(rotation);
    }
};

// 为QHash提供哈希函数
inline uint qHash(const PageCacheKey& key, uint seed = 0) {
    return key.hash() ^ seed;
}

/**
 * @brief 页面缓存管理器（修复版）
 *
 * 负责管理PDF页面的渲染缓存，特性：
 * - 缓存键包含页码、缩放、旋转
 * - LRU缓存淘汰策略
 * - 智能预加载
 * - 线程安全
 * - 内存使用监控
 */
class PageCacheManager
{
public:
    /**
     * @brief 缓存策略
     */
    enum class CacheStrategy {
        LRU,            ///< 最近最少使用
        MRU,            ///< 最近最多使用
        NearCurrent     ///< 优先保留当前页附近的页面
    };

    /**
     * @brief 构造函数
     * @param maxSize 最大缓存页面数
     * @param strategy 缓存策略
     */
    explicit PageCacheManager(int maxSize = 10,
                              CacheStrategy strategy = CacheStrategy::NearCurrent);

    /**
     * @brief 添加页面到缓存
     * @param pageIndex 页码
     * @param zoom 缩放比例
     * @param rotation 旋转角度
     * @param image 渲染的图像
     * @return 是否成功添加
     */
    bool addPage(int pageIndex, double zoom, int rotation, const QImage& image);

    /**
     * @brief 获取缓存的页面
     * @param pageIndex 页码
     * @param zoom 缩放比例
     * @param rotation 旋转角度
     * @return 图像，如果不在缓存中返回空QImage
     */
    QImage getPage(int pageIndex, double zoom, int rotation);

    /**
     * @brief 检查页面是否在缓存中
     * @param pageIndex 页码
     * @param zoom 缩放比例
     * @param rotation 旋转角度
     * @return 是否存在
     */
    bool contains(int pageIndex, double zoom, int rotation) const;

    /**
     * @brief 移除指定页面
     * @param pageIndex 页码
     * @param zoom 缩放比例
     * @param rotation 旋转角度
     */
    void removePage(int pageIndex, double zoom, int rotation);

    /**
     * @brief 清空所有缓存
     */
    void clear();

    /**
     * @brief 清空指定缩放/旋转的所有缓存
     * @param zoom 缩放比例（-1表示清空所有缩放）
     * @param rotation 旋转角度（-1表示清空所有旋转）
     */
    void clearByZoomRotation(double zoom = -1, int rotation = -1);

    /**
     * @brief 设置最大缓存大小
     * @param maxSize 最大页面数
     */
    void setMaxSize(int maxSize);

    /**
     * @brief 获取最大缓存大小
     */
    int maxSize() const { return m_maxSize; }

    /**
     * @brief 设置缓存策略
     * @param strategy 策略类型
     */
    void setStrategy(CacheStrategy strategy);

    /**
     * @brief 获取当前缓存策略
     */
    CacheStrategy strategy() const { return m_strategy; }

    /**
     * @brief 获取当前缓存的页面数
     */
    int cacheSize() const;

    /**
     * @brief 获取所有缓存的键
     */
    QList<PageCacheKey> cachedKeys() const;

    /**
     * @brief 设置当前页码和参数（用于NearCurrent策略）
     * @param pageIndex 当前页码
     * @param zoom 当前缩放
     * @param rotation 当前旋转
     */
    void setCurrentPage(int pageIndex, double zoom, int rotation);

    /**
     * @brief 获取缓存占用的内存大小（估算，字节）
     */
    qint64 memoryUsage() const;

    /**
     * @brief 标记页面集合为可见（用于优化淘汰策略）
     * @param visiblePages 可见页面索引集合
     */
    void markVisiblePages(const QSet<int>& visiblePages);

    /**
     * @brief 获取缓存统计信息（调试用）
     */
    QString getStatistics() const;

private:
    /**
     * @brief 执行缓存淘汰
     */
    void evict();

    /**
     * @brief 根据策略选择要淘汰的键
     * @return 要淘汰的缓存键
     */
    PageCacheKey selectKeyToEvict();

    /**
     * @brief 更新访问记录
     * @param key 缓存键
     */
    void updateAccessTime(const PageCacheKey& key);

private:
    mutable QMutex m_mutex;                     ///< 线程安全锁

    int m_maxSize;                              ///< 最大缓存大小
    CacheStrategy m_strategy;                   ///< 缓存策略

    QMap<PageCacheKey, QImage> m_cache;         ///< 页面缓存
    QMap<PageCacheKey, qint64> m_accessTime;    ///< 访问时间戳
    QSet<int> m_visiblePages;                   ///< 当前可见的页面集合

    PageCacheKey m_currentKey;                  ///< 当前页面键
    qint64 m_timeCounter;                       ///< 时间计数器（用于LRU）

    // 统计信息
    qint64 m_hitCount;                          ///< 缓存命中次数
    qint64 m_missCount;                         ///< 缓存未命中次数
};

#endif // PAGECACHEMANAGER_H
