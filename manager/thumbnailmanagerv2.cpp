#include "thumbnailmanagerv2.h"
#include "thumbnailcache.h"
#include "perthreadmupdfrenderer.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QScreen>

ThumbnailManagerV2::ThumbnailManagerV2(PerThreadMuPDFRenderer* renderer, QObject* parent)
    : QObject(parent)
    , m_renderer(renderer)
    , m_cache(std::make_unique<ThumbnailCache>())
    , m_threadPool(std::make_unique<QThreadPool>())
    , m_thumbnailWidth(180)  // 提高默认宽度：120 → 180
    , m_rotation(0)
    , m_currentBatchIndex(0)
    , m_isLoadingInProgress(false)
    , m_devicePixelRatio(1.0)
{
    int threadCount = qMax(4, QThread::idealThreadCount() / 3);
    m_threadPool->setMaxThreadCount(threadCount);
    m_threadPool->setExpiryTimeout(30000);

    m_batchTimer = new QTimer(this);
    m_batchTimer->setSingleShot(true);
    m_batchTimer->setInterval(200);
    connect(m_batchTimer, &QTimer::timeout, this, &ThumbnailManagerV2::processNextBatch);

    // 检测设备像素比
    detectDevicePixelRatio();

    qInfo() << "ThumbnailManagerV2: Initialized with"
            << m_threadPool->maxThreadCount() << "threads"
            << "| Display width:" << m_thumbnailWidth
            << "| Device pixel ratio:" << m_devicePixelRatio
            << "| Render width:" << getRenderWidth();
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
        qInfo() << "ThumbnailManagerV2: Thumbnail width set to" << width
                << "| Render width:" << getRenderWidth();
    }
}

void ThumbnailManagerV2::setRotation(int rotation)
{
    m_rotation = rotation;
}

// ========== 获取缩略图 ==========

QImage ThumbnailManagerV2::getThumbnail(int pageIndex) const
{
    QImage image = m_cache->get(pageIndex);

    if (!image.isNull() && m_devicePixelRatio > 1.0) {
        // 设置设备像素比，让 Qt 自动处理高 DPI 显示
        image.setDevicePixelRatio(m_devicePixelRatio);
    }

    return image;
}

bool ThumbnailManagerV2::hasThumbnail(int pageIndex) const
{
    return m_cache->has(pageIndex);
}

int ThumbnailManagerV2::cachedCount() const
{
    return m_cache ? m_cache->count() : 0;
}

// ========== 加载控制 ==========

void ThumbnailManagerV2::startLoading(const QSet<int>& initialVisible)
{
    if (!m_renderer || !m_renderer->isDocumentLoaded()) {
        qWarning() << "ThumbnailManagerV2: No document loaded";
        return;
    }

    int pageCount = m_renderer->pageCount();
    m_strategy.reset(StrategyFactory::createStrategy(pageCount, this));

    QString strategyName;
    switch (m_strategy->type()) {
    case LoadStrategyType::SMALL_DOC:
        strategyName = "Small Document (Full Sync)";
        break;
    case LoadStrategyType::MEDIUM_DOC:
        strategyName = "Medium Document (Visible Sync + Background Async)";
        break;
    case LoadStrategyType::LARGE_DOC:
        strategyName = "Large Document (On-Demand Sync Only)";
        break;
    }

    qInfo() << "ThumbnailManagerV2: Starting load with strategy:" << strategyName
            << "| Render width:" << getRenderWidth() << "px";
    emit loadingStarted(pageCount, strategyName);

    QVector<int> initialPages = m_strategy->getInitialLoadPages(initialVisible);

    if (initialPages.isEmpty()) {
        qDebug() << "startLoading Medium:" << initialVisible << initialPages;
        return;
    }

    if (m_strategy->type() == LoadStrategyType::SMALL_DOC) {
        m_isLoadingInProgress = true;
        emit loadingStatusChanged(tr("加载中..."));
        renderPagesSync(initialPages);
        emit loadingStatusChanged(tr("加载完毕！"));
        m_isLoadingInProgress = false;
        emit allCompleted();

    } else if (m_strategy->type() == LoadStrategyType::MEDIUM_DOC) {
        m_isLoadingInProgress = true;
        emit loadingStatusChanged(tr("加载可见区..."));
        renderPagesSync(initialPages);
        emit loadingStatusChanged(tr("后台加载中..."));
        setupBackgroundBatches();

    } else {
        m_isLoadingInProgress = false;
        emit loadingStatusChanged(tr("加载中..."));
        renderPagesSync(initialPages);
        emit loadingStatusChanged(tr("滚动以触发分页加载"));
    }
}

void ThumbnailManagerV2::syncLoadPages(const QVector<int>& pages)
{
    if (!m_renderer || pages.isEmpty()) {
        return;
    }

    if (m_isLoadingInProgress) {
        return;
    }

    QVector<int> toLoad;
    for (int pageIndex : pages) {
        if (!m_cache->has(pageIndex)) {
            toLoad.append(pageIndex);
        }
    }

    if (toLoad.isEmpty()) {
        return;
    }

    qInfo() << "ThumbnailManagerV2: Sync loading" << toLoad.size()
            << "pages (strategy:"
            << (m_strategy ? static_cast<int>(m_strategy->type()) : -1) << ")";

    renderPagesSync(toLoad);
}

void ThumbnailManagerV2::handleSlowScroll(const QSet<int>& visiblePages)
{
    if (!m_renderer || visiblePages.isEmpty()) {
        return;
    }

    if (!m_strategy || m_strategy->type() != LoadStrategyType::LARGE_DOC) {
        return;
    }

    if (m_isLoadingInProgress) {
        return;
    }

    QVector<int> toLoad;
    for (int pageIndex : visiblePages) {
        if (!m_cache->has(pageIndex)) {
            toLoad.append(pageIndex);
        }
    }

    if (toLoad.isEmpty()) {
        return;
    }

    renderPagesSync(toLoad);
}

void ThumbnailManagerV2::cancelAllTasks()
{
    QMutexLocker locker(&m_taskMutex);

    m_batchTimer->stop();
    m_currentBatchIndex = 0;

    // 等待所有任务完成（自动删除）
    if (m_threadPool) {
        m_threadPool->clear();        // 清除还没开始的任务
        m_threadPool->waitForDone();  // 等待所有正在运行的任务完成
    }
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
    m_isLoadingInProgress = false;
}

QString ThumbnailManagerV2::getStatistics() const
{
    QString stats = m_cache ? m_cache->getStatistics() : QString();
    stats += QString("\nDevice Pixel Ratio: %1x").arg(m_devicePixelRatio);
    stats += QString("\nDisplay Width: %1px").arg(m_thumbnailWidth);
    stats += QString("\nRender Width: %1px").arg(getRenderWidth());
    return stats;
}

bool ThumbnailManagerV2::shouldRespondToScroll() const
{
    return !m_isLoadingInProgress;
}

// ========== 私有方法 ==========

void ThumbnailManagerV2::detectDevicePixelRatio()
{
    // 获取主屏幕的设备像素比
    QScreen* screen = QGuiApplication::primaryScreen();
    if (screen) {
        m_devicePixelRatio = screen->devicePixelRatio();

        // 限制最大倍数，避免过大的图片
        if (m_devicePixelRatio > 3.0) {
            qInfo() << "ThumbnailManagerV2: Device pixel ratio" << m_devicePixelRatio
                    << "is very high, capping at 3.0";
            m_devicePixelRatio = 3.0;
        }
    } else {
        m_devicePixelRatio = 1.6;
    }
}

int ThumbnailManagerV2::getRenderWidth() const
{
    // 按设备像素比渲染高分辨率图片
    return static_cast<int>(m_thumbnailWidth * m_devicePixelRatio);
}

void ThumbnailManagerV2::renderPagesSync(const QVector<int>& pages)
{
    if (!m_renderer || pages.isEmpty()) {
        return;
    }

    QElapsedTimer timer;
    timer.start();

    int rendered = 0;
    int total = pages.size();
    int renderWidth = getRenderWidth();  // 使用高DPI渲染宽度

    for (int pageIndex : pages) {
        if (m_cache->has(pageIndex)) {
            continue;
        }

        QSizeF pageSize = m_renderer->pageSize(pageIndex);
        if (pageSize.isEmpty()) {
            continue;
        }

        // 按高DPI宽度计算缩放比例
        double zoom = renderWidth / pageSize.width();

        RenderResult result = m_renderer->renderPage(
            pageIndex, zoom, m_rotation);

        if (result.success && !result.image.isNull()) {
            // 设置图片的设备像素比
            QImage image = result.image;
            image.setDevicePixelRatio(m_devicePixelRatio);

            m_cache->set(pageIndex, image);
            emit thumbnailLoaded(pageIndex, image);
            rendered++;

            if (rendered % 10 == 0 || rendered == total) {
                emit loadProgress(rendered, total);
            }
        }
    }

    qint64 elapsed = timer.elapsed();
    qInfo() << "ThumbnailManagerV2: Sync rendered" << rendered
            << "pages in" << elapsed << "ms"
            << "(" << (rendered > 0 ? elapsed / rendered : 0) << "ms/page)"
            << "at" << renderWidth << "px width";
}

void ThumbnailManagerV2::renderPagesAsync(const QVector<int>& pages, RenderPriority priority)
{
    if (!m_renderer || pages.isEmpty()) {
        return;
    }

    QVector<int> toRender;
    for (int pageIndex : pages) {
        if (!m_cache->has(pageIndex)) {
            toRender.append(pageIndex);
        }
    }

    if (toRender.isEmpty()) {
        return;
    }

    auto* task = new ThumbnailBatchTask(
        m_renderer->documentPath(),
        m_cache.get(),
        this,
        toRender,
        priority,
        getRenderWidth(),  // 使用高DPI渲染宽度
        m_rotation,
        m_devicePixelRatio  // 传递设备像素比
        );

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
        << "background batches for medium document";

        QTimer::singleShot(500, this, &ThumbnailManagerV2::processNextBatch);
    }
}

void ThumbnailManagerV2::processNextBatch()
{
    if (m_currentBatchIndex >= m_backgroundBatches.size()) {
        qInfo() << "ThumbnailManagerV2: All background batches completed";

        m_isLoadingInProgress = false;

        emit loadingStatusChanged(tr("加载完毕"));
        emit allCompleted();
        return;
    }

    const QVector<int>& batch = m_backgroundBatches[m_currentBatchIndex];

    emit loadingStatusChanged(tr("加载中..."));

    renderPagesAsync(batch, RenderPriority::LOW);

    emit batchCompleted(m_currentBatchIndex + 1, m_backgroundBatches.size());

    m_currentBatchIndex++;

    if (m_currentBatchIndex < m_backgroundBatches.size()) {
        m_batchTimer->start();
    } else {
        m_isLoadingInProgress = false;
        emit allCompleted();
    }
}
