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
    , m_thumbnailWidth(180)
    , m_rotation(0)
    , m_nextBatchIndex(0)
    , m_runningTasks(0)
    , m_isLoadingInProgress(false)
    , m_devicePixelRatio(1.0)
{
    int threadCount = qMax(4, QThread::idealThreadCount() / 3);
    m_threadPool->setMaxThreadCount(threadCount);
    m_threadPool->setExpiryTimeout(30000);

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

QImage ThumbnailManagerV2::getThumbnail(int pageIndex) const
{
    QImage image = m_cache->get(pageIndex);

    if (!image.isNull() && m_devicePixelRatio > 1.0) {
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
    return m_cache->count();
}

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
        strategyName = tr("Small Document (Full Sync)");
        break;
    case LoadStrategyType::MEDIUM_DOC:
        strategyName = tr("Medium Document (Visible Sync + Background Async)");
        break;
    case LoadStrategyType::LARGE_DOC:
        strategyName = tr("Large Document (Initial Async + On-Demand)");
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
        emit loadingStatusChanged(tr("Loading..."));
        renderPagesSync(initialPages);
        emit loadingStatusChanged(tr("Load completed!"));
        m_isLoadingInProgress = false;
        emit allCompleted();

    } else if (m_strategy->type() == LoadStrategyType::MEDIUM_DOC) {
        m_isLoadingInProgress = true;
        emit loadingStatusChanged(tr("Loading visible area..."));
        renderPagesSync(initialPages);
        emit loadingStatusChanged(tr("Background loading..."));
        setupBackgroundBatches();

    } else {
        m_isLoadingInProgress = false;
        emit loadingStatusChanged(tr("Loading..."));
        // 仅大文档首次打开时的可见窗口改为后台任务；分级阈值、后续按需
        // 加载和小/中型文档路径保持原样，避免 400 页以上文档首帧阻塞 UI。
        renderPagesAsync(initialPages, RenderPriority::HIGH);
        emit loadingStatusChanged(tr("Scroll to trigger paged loading"));
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

    m_nextBatchIndex = 0;
    m_runningTasks = 0;

    if (m_threadPool) {
        m_threadPool->clear();
        m_threadPool->waitForDone();
    }
}

void ThumbnailManagerV2::clear()
{
    cancelAllTasks();

    if (m_cache) {
        m_cache->clear();
    }

    m_backgroundBatches.clear();
    m_nextBatchIndex = 0;
    m_runningTasks = 0;
    m_isLoadingInProgress = false;
}

QString ThumbnailManagerV2::getStatistics() const
{
    return m_cache->getStatistics();
}

bool ThumbnailManagerV2::shouldRespondToScroll() const
{
    return !m_isLoadingInProgress;
}


void ThumbnailManagerV2::detectDevicePixelRatio()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (screen) {
        m_devicePixelRatio = screen->devicePixelRatio();

        if (m_devicePixelRatio > 3.0) {
            qInfo() << "ThumbnailManagerV2: Device pixel ratio" << m_devicePixelRatio
                    << "is very high, capping at 3.0";
            m_devicePixelRatio = 3.0;
        }
    } else {
        m_devicePixelRatio = 1.2;
    }
}

int ThumbnailManagerV2::getRenderWidth() const
{
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
    int renderWidth = getRenderWidth();

    for (int pageIndex : pages) {
        if (m_cache->has(pageIndex)) {
            continue;
        }

        QSizeF pageSize = m_renderer->pageSize(pageIndex);
        if (pageSize.isEmpty()) {
            continue;
        }

        double zoom = renderWidth / pageSize.width();

        RenderResult result = m_renderer->renderPage(
            pageIndex, zoom, m_rotation, RenderScene::Thumbnail);

        if (result.success && !result.image.isNull()) {
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

    // HIGH 任务单批最多渲染 10 页；大文档的可见窗口最多可能包含 17 页，
    // 因此按限制拆分，确保窗口内每一页都会被调度。
    const int batchSize = priority == RenderPriority::HIGH ? 10 : toRender.size();
    for (int start = 0; start < toRender.size(); start += batchSize) {
        auto* task = new ThumbnailBatchTask(
            m_renderer->documentPath(),
            m_cache.get(),
            this,
            toRender.mid(start, batchSize),
            priority,
            getRenderWidth(),
            m_rotation,
            m_devicePixelRatio,
            nullptr);

        m_threadPool->start(task, static_cast<int>(priority));
    }
}

void ThumbnailManagerV2::setupBackgroundBatches()
{
    m_backgroundBatches = m_strategy->getBackgroundBatches();
    m_nextBatchIndex = 0;
    m_runningTasks = 0;

    int maxConcurrency = m_threadPool->maxThreadCount();

    for (int i = 0; i < maxConcurrency; i++) {
        processNextBatch();
    }
}

void ThumbnailManagerV2::processNextBatch()
{
    if (m_nextBatchIndex >= m_backgroundBatches.size()) {
        if (m_runningTasks == 0) {
            emit allCompleted();
        }
        return;
    }

    const QVector<int>& batch = m_backgroundBatches[m_nextBatchIndex];

    m_runningTasks++;

    auto callback = [this]() {
        QMetaObject::invokeMethod(this, [this]() {
            emit batchCompleted(m_nextBatchIndex, m_backgroundBatches.size());
            m_runningTasks--;
            processNextBatch();
        });
    };

    m_nextBatchIndex++;

    auto* task = new ThumbnailBatchTask(
        m_renderer->documentPath(),
        m_cache.get(),
        this,
        batch,
        RenderPriority::LOW,
        getRenderWidth(),
        m_rotation,
        m_devicePixelRatio,
        callback
        );

    m_threadPool->start(task);
}
