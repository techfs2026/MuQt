#ifndef THUMBNAILLOADSTRATEGY_H
#define THUMBNAILLOADSTRATEGY_H

#include <QObject>
#include <QVector>
#include <QSet>

/**
 * @brief 缩略图加载策略类型
 */
enum class LoadStrategyType {
    SMALL_DOC,      // <100页：同步全量加载
    MEDIUM_DOC,     // 100-500页：同步可见区+异步分批
    LARGE_DOC       // >500页：按需分页加载
};

/**
 * @brief 缩略图加载策略基类
 */
class ThumbnailLoadStrategy : public QObject
{
    Q_OBJECT

public:
    explicit ThumbnailLoadStrategy(int pageCount, QObject* parent = nullptr)
        : QObject(parent), m_pageCount(pageCount) {}

    virtual ~ThumbnailLoadStrategy() = default;

    virtual LoadStrategyType type() const = 0;

    /**
     * @brief 获取初始加载页面列表
     */
    virtual QVector<int> getInitialLoadPages(const QSet<int>& visiblePages) const = 0;

    /**
     * @brief 获取后续批次（用于异步加载）
     * @return 每个批次的页面列表
     */
    virtual QVector<QVector<int>> getBackgroundBatches() const = 0;

    /**
     * @brief 处理可见区域变化（用于大文档）
     */
    virtual QVector<int> handleVisibleChange(const QSet<int>& visiblePages) const = 0;

protected:
    int m_pageCount;
};

/**
 * @brief 小文档策略（<100页）：同步全量加载
 */
class SmallDocStrategy : public ThumbnailLoadStrategy
{
    Q_OBJECT

public:
    explicit SmallDocStrategy(int pageCount, QObject* parent = nullptr);

    LoadStrategyType type() const override { return LoadStrategyType::SMALL_DOC; }
    QVector<int> getInitialLoadPages(const QSet<int>& visiblePages) const override;
    QVector<QVector<int>> getBackgroundBatches() const override;
    QVector<int> handleVisibleChange(const QSet<int>& visiblePages) const override;
};

/**
 * @brief 中文档策略（100-500页）：同步可见区+异步智能分批
 */
class MediumDocStrategy : public ThumbnailLoadStrategy
{
    Q_OBJECT

public:
    explicit MediumDocStrategy(int pageCount, QObject* parent = nullptr);

    LoadStrategyType type() const override { return LoadStrategyType::MEDIUM_DOC; }
    QVector<int> getInitialLoadPages(const QSet<int>& visiblePages) const override;
    QVector<QVector<int>> getBackgroundBatches() const override;
    QVector<int> handleVisibleChange(const QSet<int>& visiblePages) const override;

private:
    static constexpr int BATCH_SIZE = 30;  // 每批30页
    static constexpr int INITIAL_MARGIN = 20;  // 初始可见区前后20页
};

/**
 * @brief 大文档策略（>500页）：按需分页加载
 */
class LargeDocStrategy : public ThumbnailLoadStrategy
{
    Q_OBJECT

public:
    explicit LargeDocStrategy(int pageCount, QObject* parent = nullptr);

    LoadStrategyType type() const override { return LoadStrategyType::LARGE_DOC; }
    QVector<int> getInitialLoadPages(const QSet<int>& visiblePages) const override;
    QVector<QVector<int>> getBackgroundBatches() const override;
    QVector<int> handleVisibleChange(const QSet<int>& visiblePages) const override;

private:
    static constexpr int PAGE_WINDOW = 8;  // 可见区前后各8页
    mutable QSet<int> m_loadedPages;  // 已加载的页面
};

/**
 * @brief 策略工厂
 */
class StrategyFactory
{
public:
    static ThumbnailLoadStrategy* createStrategy(int pageCount, QObject* parent = nullptr);

private:
    static constexpr int SMALL_DOC_THRESHOLD = 50;
    static constexpr int MEDIUM_DOC_THRESHOLD = 300;
};

#endif // THUMBNAILLOADSTRATEGY_H
