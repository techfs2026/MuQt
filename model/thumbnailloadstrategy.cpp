#include "thumbnailloadstrategy.h"
#include <QDebug>
#include <algorithm>

// ========== SmallDocStrategy ==========

SmallDocStrategy::SmallDocStrategy(int pageCount, QObject* parent)
    : ThumbnailLoadStrategy(pageCount, parent)
{
    qInfo() << "SmallDocStrategy: Initialized for" << pageCount << "pages (sync full load)";
}

QVector<int> SmallDocStrategy::getInitialLoadPages(const QSet<int>& visiblePages) const
{
    Q_UNUSED(visiblePages);

    // 小文档直接全量同步加载
    QVector<int> allPages;
    allPages.reserve(m_pageCount);
    for (int i = 0; i < m_pageCount; ++i) {
        allPages.append(i);
    }

    qDebug() << "SmallDocStrategy: Loading all" << m_pageCount << "pages synchronously";
    return allPages;
}

QVector<QVector<int>> SmallDocStrategy::getBackgroundBatches() const
{
    // 小文档不需要后台批次
    return {};
}

QVector<int> SmallDocStrategy::handleVisibleChange(const QSet<int>& visiblePages) const
{
    Q_UNUSED(visiblePages);
    // 小文档已全部加载，无需处理
    return {};
}

// ========== MediumDocStrategy ==========

MediumDocStrategy::MediumDocStrategy(int pageCount, QObject* parent)
    : ThumbnailLoadStrategy(pageCount, parent)
{
    qInfo() << "MediumDocStrategy: Initialized for" << pageCount
            << "pages (sync visible + async batches)";
}

QVector<int> MediumDocStrategy::getInitialLoadPages(const QSet<int>& visiblePages) const
{
    if (visiblePages.isEmpty()) {
        return {};
    }

    // 计算可见区域的扩展范围
    int minPage = *std::min_element(visiblePages.begin(), visiblePages.end());
    int maxPage = *std::max_element(visiblePages.begin(), visiblePages.end());

    int startPage = qMax(0, minPage - INITIAL_MARGIN);
    int endPage = qMin(m_pageCount - 1, maxPage + INITIAL_MARGIN);

    QVector<int> initialPages;
    initialPages.reserve(endPage - startPage + 1);
    for (int i = startPage; i <= endPage; ++i) {
        initialPages.append(i);
    }

    qDebug() << "MediumDocStrategy: Initial sync load pages"
             << startPage << "to" << endPage
             << "(" << initialPages.size() << "pages)";

    return initialPages;
}

QVector<QVector<int>> MediumDocStrategy::getBackgroundBatches() const
{
    QVector<QVector<int>> batches;

    // 将剩余页面分批
    int batchCount = (m_pageCount + BATCH_SIZE - 1) / BATCH_SIZE;
    batches.reserve(batchCount);

    for (int batchIdx = 0; batchIdx < batchCount; ++batchIdx) {
        int startPage = batchIdx * BATCH_SIZE;
        int endPage = qMin(startPage + BATCH_SIZE - 1, m_pageCount - 1);

        QVector<int> batch;
        batch.reserve(endPage - startPage + 1);
        for (int i = startPage; i <= endPage; ++i) {
            batch.append(i);
        }

        batches.append(batch);
    }

    qDebug() << "MediumDocStrategy: Created" << batches.size()
             << "background batches (batch size:" << BATCH_SIZE << ")";

    return batches;
}

QVector<int> MediumDocStrategy::handleVisibleChange(const QSet<int>& visiblePages) const
{
    // 中文档使用批次加载，不需要实时响应可见区变化
    // 只在初始加载时响应
    Q_UNUSED(visiblePages);
    return {};
}

// ========== LargeDocStrategy ==========

LargeDocStrategy::LargeDocStrategy(int pageCount, QObject* parent)
    : ThumbnailLoadStrategy(pageCount, parent)
{
    qInfo() << "LargeDocStrategy: Initialized for" << pageCount
            << "pages (on-demand paging)";
}

QVector<int> LargeDocStrategy::getInitialLoadPages(const QSet<int>& visiblePages) const
{
    return handleVisibleChange(visiblePages);
}

QVector<QVector<int>> LargeDocStrategy::getBackgroundBatches() const
{
    // 大文档不使用批次加载
    return {};
}

QVector<int> LargeDocStrategy::handleVisibleChange(const QSet<int>& visiblePages) const
{
    if (visiblePages.isEmpty()) {
        return {};
    }

    // 计算窗口范围
    int minPage = *std::min_element(visiblePages.begin(), visiblePages.end());
    int maxPage = *std::max_element(visiblePages.begin(), visiblePages.end());

    int startPage = qMax(0, minPage - PAGE_WINDOW);
    int endPage = qMin(m_pageCount - 1, maxPage + PAGE_WINDOW);

    QVector<int> toLoad;
    for (int i = startPage; i <= endPage; ++i) {
        if (!m_loadedPages.contains(i)) {
            toLoad.append(i);
            m_loadedPages.insert(i);
        }
    }

    if (!toLoad.isEmpty()) {
        qDebug() << "LargeDocStrategy: Loading" << toLoad.size()
        << "pages around visible range [" << minPage << "," << maxPage << "]";
    }

    return toLoad;
}

// ========== StrategyFactory ==========

ThumbnailLoadStrategy* StrategyFactory::createStrategy(int pageCount, QObject* parent)
{
    if (pageCount <= SMALL_DOC_THRESHOLD) {
        return new SmallDocStrategy(pageCount, parent);
    } else if (pageCount <= MEDIUM_DOC_THRESHOLD) {
        return new MediumDocStrategy(pageCount, parent);
    } else {
        return new LargeDocStrategy(pageCount, parent);
    }
}
