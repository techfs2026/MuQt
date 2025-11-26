#include "thumbnailmanagerv2.h"
#include "thumbnailcache.h"
#include "mupdfrenderer.h"
#include <QDebug>
#include <QElapsedTimer>

ThumbnailManagerV2::ThumbnailManagerV2(MuPDFRenderer* renderer, QObject* parent)
    : QObject(parent)
    , m_renderer(renderer)
    , m_cache(std::make_unique<ThumbnailCache>())
    , m_threadPool(std::make_unique<QThreadPool>())
    , m_thumbnailWidth(120)
    , m_rotation(0)
    , m_currentBatchIndex(0)
{
    m_threadPool->setMaxThreadCount(2);
    m_threadPool->setExpiryTimeout(30000);

    m_batchTimer = new QTimer(this);
    m_batchTimer->setSingleShot(true);
    m_batchTimer->setInterval(500);  // 每批间隔500ms
    connect(m_batchTimer, &QTimer::timeout, this, &ThumbnailManagerV2::processNextBatch);

    qInfo() << "ThumbnailManagerV2: Initialized with"
            << m_threadPool->maxThreadCount() << "threads";
}

ThumbnailManagerV2::~ThumbnailManagerV2()
{
    clear();
}

// ========== 配置 ==========

void ThumbnailManagerV2::setThumbnailWidth(int width)
{
    if (width >= 80 && width <= 400) {
        m_thumbnailWidth = width;
    }
}

void ThumbnailManagerV2::setRotation(int rotation)
{
    m_rotation = rotation;
}

// ========== 获取缩略图 ==========

QImage ThumbnailManagerV2::getThumbnail(int pageIndex) const
{
    return m_cache->getLowRes(pageIndex);
}

bool ThumbnailManagerV2::hasThumbnail(int pageIndex) const
{
    return m_cache->hasLowRes(pageIndex);
}

// ========== 加载控制 ==========

void ThumbnailManagerV2::startLoading(const QSet<int>& initialVisible)
{
    if (!m_renderer || !m_renderer->isDocumentLoaded()) {
        qWarning() << "ThumbnailManagerV2: No document loaded";
        return;
    }

    int pageCount = m_renderer->pageCount();

    // 创建策略
    m_strategy.reset(StrategyFactory::createStrategy(pageCount, this));

    qInfo() << "ThumbnailManagerV2: Starting load with strategy:"
            << static_cast<int>(m_strategy->type());

    // 获取初始加载页面
    QVector<int> initialPages = m_strategy->getInitialLoadPages(initialVisible);

    if (!initialPages.isEmpty()) {
        if (m_strategy->type() == LoadStrategyType::SMALL_DOC) {
            // 小文档：同步全量加载
            renderPagesSync(initialPages);
            emit allCompleted();
        } else {
            // 中/大文档：同步加载初始可见区
            renderPagesSync(initialPages);

            if (m_strategy->type() == LoadStrategyType::MEDIUM_DOC) {
                // 中文档：设置后台批次
                setupBackgroundBatches();
            }
        }
    }
}

void ThumbnailManagerV2::handleVisibleRangeChanged(const QSet<int>& visiblePages)
{
    if (!m_strategy || m_strategy->type() != LoadStrategyType::LARGE_DOC) {
        return;
    }

    // 大文档：按需加载
    QVector<int> toLoad = m_strategy->handleVisibleChange(visiblePages);

    if (!toLoad.isEmpty()) {
        renderPagesAsync(toLoad, RenderPriority::HIGH);
    }
}

void ThumbnailManagerV2::cancelAllTasks()
{
    QMutexLocker locker(&m_taskMutex);

    for (ThumbnailBatchTask* task : m_activeTasks) {
        if (task) {
            task->abort();
        }
    }

    m_activeTasks.clear();
    m_batchTimer->stop();
    m_currentBatchIndex = 0;
}

void ThumbnailManagerV2::waitForCompletion()
{
    m_threadPool->waitForDone();
}

void ThumbnailManagerV2::clear()
{
    cancelAllTasks();
    waitForCompletion();

    if (m_cache) {
        m_cache->clear();
    }

    m_backgroundBatches.clear();
    m_currentBatchIndex = 0;
}

QString ThumbnailManagerV2::getStatistics() const
{
    return m_cache ? m_cache->getStatistics() : QString();
}

int ThumbnailManagerV2::cachedCount() const
{
    return m_cache ? m_cache->lowResCount() : 0;
}

// ========== 私有方法 ==========

void ThumbnailManagerV2::renderPagesSync(const QVector<int>& pages)
{
    if (!m_renderer || pages.isEmpty()) {
        return;
    }

    QElapsedTimer timer;
    timer.start();

    int rendered = 0;
    for (int pageIndex : pages) {
        if (m_cache->hasLowRes(pageIndex)) {
            continue;
        }

        QSizeF pageSize = m_renderer->pageSize(pageIndex);
        if (pageSize.isEmpty()) {
            continue;
        }

        double zoom = m_thumbnailWidth / pageSize.width();

        MuPDFRenderer::RenderResult result = m_renderer->renderPage(
            pageIndex, zoom, m_rotation);

        if (result.success && !result.image.isNull()) {
            m_cache->setLowRes(pageIndex, result.image);
            emit thumbnailLoaded(pageIndex, result.image, false);
            rendered++;
        }
    }

    qint64 elapsed = timer.elapsed();
    qInfo() << "ThumbnailManagerV2: Sync rendered" << rendered
            << "pages in" << elapsed << "ms"
            << "(" << (rendered > 0 ? elapsed / rendered : 0) << "ms/page)";

    emit loadProgress(rendered, pages.size());
}

void ThumbnailManagerV2::renderPagesAsync(const QVector<int>& pages, RenderPriority priority)
{
    if (!m_renderer || pages.isEmpty()) {
        return;
    }

    // 过滤已缓存的页面
    QVector<int> toRender;
    for (int pageIndex : pages) {
        if (!m_cache->hasLowRes(pageIndex)) {
            toRender.append(pageIndex);
        }
    }

    if (toRender.isEmpty()) {
        return;
    }

    qDebug() << "ThumbnailManagerV2: Async rendering" << toRender.size()
             << "pages (priority:" << static_cast<int>(priority) << ")";

    auto* task = new ThumbnailBatchTask(
        m_renderer->documentPath(),
        m_cache.get(),
        this,
        toRender,
        priority,
        true,  // 低清
        m_thumbnailWidth,
        m_thumbnailWidth,
        m_rotation
        );

    trackTask(task);
    m_threadPool->start(task, static_cast<int>(priority));
}

void ThumbnailManagerV2::setupBackgroundBatches()
{
    if (!m_strategy) {
        return;
    }

    m_backgroundBatches = m_strategy->getBackgroundBatches();
    m_currentBatchIndex = 0;

    if (!m_backgroundBatches.isEmpty()) {
        qInfo() << "ThumbnailManagerV2: Setup" << m_backgroundBatches.size()
        << "background batches";

        // 延迟启动第一批
        QTimer::singleShot(1000, this, &ThumbnailManagerV2::processNextBatch);
    }
}

void ThumbnailManagerV2::processNextBatch()
{
    if (m_currentBatchIndex >= m_backgroundBatches.size()) {
        qInfo() << "ThumbnailManagerV2: All background batches completed";
        emit allCompleted();
        return;
    }

    const QVector<int>& batch = m_backgroundBatches[m_currentBatchIndex];

    qDebug() << "ThumbnailManagerV2: Processing batch"
             << (m_currentBatchIndex + 1) << "/" << m_backgroundBatches.size()
             << "(" << batch.size() << "pages)";

    renderPagesAsync(batch, RenderPriority::LOW);

    emit batchCompleted(m_currentBatchIndex, m_backgroundBatches.size());

    m_currentBatchIndex++;

    // 调度下一批
    if (m_currentBatchIndex < m_backgroundBatches.size()) {
        m_batchTimer->start();
    } else {
        emit allCompleted();
    }
}

void ThumbnailManagerV2::onBatchTaskFinished()
{
    // 任务完成回调（如果需要）
}

void ThumbnailManagerV2::trackTask(ThumbnailBatchTask* task)
{
    QMutexLocker locker(&m_taskMutex);
    m_activeTasks.append(task);
}

void ThumbnailManagerV2::untrackTask(ThumbnailBatchTask* task)
{
    QMutexLocker locker(&m_taskMutex);
    m_activeTasks.removeOne(task);
}
