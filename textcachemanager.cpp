#include "textcachemanager.h"
#include <QDebug>
#include <QMutexLocker>
#include <QCoreApplication>
#include <QThreadPool>
#include <QRunnable>
#include <QMetaObject>
#include <QAtomicInteger>
#include <QElapsedTimer>

// MuPDF headers
extern "C" {
#include <mupdf/fitz.h>
}

// ---------- PageExtractTask (QRunnable) ----------
class PageExtractTask : public QRunnable
{
public:
    PageExtractTask(TextCacheManager* manager,
                    const QString& pdfPath,
                    int pageIndex)
        : m_manager(manager)
        , m_pdfPath(pdfPath)
        , m_pageIndex(pageIndex)
    {
        setAutoDelete(true);
    }

    void run() override
    {
        if (!m_manager) return;

        // 检查取消
        if (m_manager->m_cancelRequested.loadAcquire()) {
            // 报告为未成功，但仍需要让 manager 递减 remainingTasks
            QMetaObject::invokeMethod(m_manager, "handleTaskDone",
                                      Qt::QueuedConnection,
                                      Q_ARG(int, m_pageIndex),
                                      Q_ARG(PageTextData, PageTextData()),
                                      Q_ARG(bool, false));
            return;
        }

        // 创建线程 local context
        fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
        if (!ctx) {
            QMetaObject::invokeMethod(m_manager, "handleTaskDone",
                                      Qt::QueuedConnection,
                                      Q_ARG(int, m_pageIndex),
                                      Q_ARG(PageTextData, PageTextData()),
                                      Q_ARG(bool, false));
            return;
        }

        fz_try(ctx) {
            fz_register_document_handlers(ctx);
        } fz_catch(ctx) {
            fz_drop_context(ctx);
            QMetaObject::invokeMethod(m_manager, "handleTaskDone",
                                      Qt::QueuedConnection,
                                      Q_ARG(int, m_pageIndex),
                                      Q_ARG(PageTextData, PageTextData()),
                                      Q_ARG(bool, false));
            return;
        }

        fz_document* doc = nullptr;
        fz_try(ctx) {
            doc = fz_open_document(ctx, m_pdfPath.toUtf8().constData());
            if (!doc) {
                fz_drop_context(ctx);
                QMetaObject::invokeMethod(m_manager, "handleTaskDone",
                                          Qt::QueuedConnection,
                                          Q_ARG(int, m_pageIndex),
                                          Q_ARG(PageTextData, PageTextData()),
                                          Q_ARG(bool, false));
                return;
            }
        } fz_catch(ctx) {
            if (doc) fz_drop_document(ctx, doc);
            fz_drop_context(ctx);
            QMetaObject::invokeMethod(m_manager, "handleTaskDone",
                                      Qt::QueuedConnection,
                                      Q_ARG(int, m_pageIndex),
                                      Q_ARG(PageTextData, PageTextData()),
                                      Q_ARG(bool, false));
            return;
        }

        PageTextData pageData;
        pageData.pageIndex = m_pageIndex;

        fz_stext_page* stext = nullptr;

        fz_try(ctx) {
            // Load page
            fz_page* page = fz_load_page(ctx, doc, m_pageIndex);
            fz_rect bound = fz_bound_page(ctx, page);

            stext = fz_new_stext_page(ctx, bound);

            fz_stext_options options;
            memset(&options, 0, sizeof(options));
            fz_device* dev = fz_new_stext_device(ctx, stext, &options);
            fz_run_page(ctx, page, dev, fz_identity, nullptr);
            fz_close_device(ctx, dev);
            fz_drop_device(ctx, dev);

            // 遍历结构化文本，转换到 Qt 类型
            for (fz_stext_block* block = stext->first_block; block; block = block->next) {
                if (block->type != FZ_STEXT_BLOCK_TEXT) continue;

                TextBlock tb;
                tb.bbox = QRectF(block->bbox.x0, block->bbox.y0,
                                 block->bbox.x1 - block->bbox.x0,
                                 block->bbox.y1 - block->bbox.y0);

                for (fz_stext_line* line = block->u.t.first_line; line; line = line->next) {
                    TextLine tl;
                    tl.bbox = QRectF(line->bbox.x0, line->bbox.y0,
                                     line->bbox.x1 - line->bbox.x0,
                                     line->bbox.y1 - line->bbox.y0);

                    for (fz_stext_char* ch = line->first_char; ch; ch = ch->next) {
                        TextChar tc;
                        tc.character = QChar(ch->c);
                        fz_quad q = ch->quad;
                        qreal minX = qMin(qMin(q.ul.x, q.ur.x), qMin(q.ll.x, q.lr.x));
                        qreal maxX = qMax(qMax(q.ul.x, q.ur.x), qMax(q.ll.x, q.lr.x));
                        qreal minY = qMin(qMin(q.ul.y, q.ur.y), qMin(q.ll.y, q.lr.y));
                        qreal maxY = qMax(qMax(q.ul.y, q.ur.y), qMax(q.ll.y, q.lr.y));
                        tc.bbox = QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
                        tl.chars.append(tc);
                        pageData.fullText.append(tc.character);
                    }
                    tb.lines.append(tl);
                    pageData.fullText.append('\n');
                }
                pageData.blocks.append(tb);
                pageData.fullText.append("\n\n");
            }

            if (stext) { fz_drop_stext_page(ctx, stext); stext = nullptr; }
            fz_drop_page(ctx, page);
        } fz_catch(ctx) {
            if (stext) { fz_drop_stext_page(ctx, stext); stext = nullptr; }
            qWarning() << "PageExtractTask: failed to extract page" << m_pageIndex;
            // 仍然将错误报告回去（ok = false）
            fz_drop_document(ctx, doc);
            fz_drop_context(ctx);
            QMetaObject::invokeMethod(m_manager, "handleTaskDone",
                                      Qt::QueuedConnection,
                                      Q_ARG(int, m_pageIndex),
                                      Q_ARG(PageTextData, PageTextData()),
                                      Q_ARG(bool, false));
            return;
        }

        // 释放资源
        fz_drop_document(ctx, doc);
        fz_drop_context(ctx);

        // 检查是否在提取后被取消
        if (m_manager->m_cancelRequested.loadAcquire()) {
            QMetaObject::invokeMethod(m_manager, "handleTaskDone",
                                      Qt::QueuedConnection,
                                      Q_ARG(int, m_pageIndex),
                                      Q_ARG(PageTextData, PageTextData()),
                                      Q_ARG(bool, false));
            return;
        }

        // 成功，将数据传回 manager（主线程接收）
        QMetaObject::invokeMethod(m_manager, "handleTaskDone",
                                  Qt::QueuedConnection,
                                  Q_ARG(int, m_pageIndex),
                                  Q_ARG(PageTextData, pageData),
                                  Q_ARG(bool, true));
    }

private:
    TextCacheManager* m_manager;
    QString m_pdfPath;
    int m_pageIndex;
};

// ---------- TextCacheManager impl ----------
TextCacheManager::TextCacheManager(MuPDFRenderer* renderer, QObject* parent)
    : QObject(parent)
    , m_renderer(renderer)
    , m_maxCacheSize(-1)
    , m_isPreloading(0)
    , m_cancelRequested(0)
    , m_preloadedPages(0)
    , m_hitCount(0)
    , m_missCount(0)
{
}

TextCacheManager::~TextCacheManager()
{
    cancelPreload();
    m_threadPool.waitForDone(3000);
    clear();
}

void TextCacheManager::startPreload()
{
    if (!m_renderer) {
        emit preloadError(QStringLiteral("No renderer assigned"));
        return;
    }

    // 读取并缓存 pdfPath（线程安全：documentPath() 是只读 QString）
    QString pdfPath;
    try {
        pdfPath = m_renderer->documentPath();
    } catch (...) {
        emit preloadError(QStringLiteral("MuPDFRenderer::documentPath() not available"));
        return;
    }

    if (pdfPath.isEmpty()) {
        emit preloadError(QStringLiteral("Renderer returned empty document path"));
        return;
    }

    // 如果已有在跑的预加载，先请求取消并短等
    if (m_isPreloading.loadAcquire()) {
        cancelPreload();
        for (int i = 0; i < 30 && m_isPreloading.loadAcquire(); ++i) {
            QThread::msleep(50);
            QCoreApplication::processEvents();
        }
    }

    int pageCount = m_renderer->pageCount();

    // 设置并发状态
    m_isPreloading.storeRelease(1);
    m_cancelRequested.storeRelease(0);
    m_preloadedPages.storeRelease(0);
    m_remainingTasks.storeRelease(pageCount);

    m_threadPool.setMaxThreadCount(QThread::idealThreadCount() / 2);

    // 创建并提交任务（这里不使用 priorityPages；按页序列提交）
    for (int i = 0; i < pageCount; ++i) {
        // 如果缓存里已有，直接减少 remainingTasks 并更新进度（避免提交任务）
        {
            QMutexLocker locker(&m_mutex);
            if (m_cache.contains(i)) {
                // 认为这是已加载的一页
                m_preloadedPages.ref(); // 增加计数（atomic）
                m_remainingTasks.fetchAndSubRelaxed(1);
                emit preloadProgress(m_preloadedPages.loadAcquire(), pageCount);
                continue;
            }
        }

        PageExtractTask* task = new PageExtractTask(this, pdfPath, i);
        m_threadPool.start(task);
    }

    // Edge case: 如果所有页都已在缓存，直接完成
    if (m_remainingTasks.loadAcquire() <= 0) {
        m_isPreloading.storeRelease(0);
        emit preloadCompleted();
    }
}

void TextCacheManager::cancelPreload()
{
    if (!m_isPreloading.loadAcquire()) return;
    m_cancelRequested.storeRelease(1);
    qDebug() << "TextCacheManager: cancel requested";
}

bool TextCacheManager::isPreloading() const
{
    return m_isPreloading.loadAcquire() != 0;
}

int TextCacheManager::computePreloadProgress() const
{
    if (!m_renderer) return 0;
    return m_preloadedPages.loadAcquire();
}

PageTextData TextCacheManager::getPageTextData(int pageIndex)
{
    QMutexLocker locker(&m_mutex);
    if (m_cache.contains(pageIndex)) {
        ++m_hitCount;
        return m_cache.value(pageIndex);
    }
    ++m_missCount;
    return PageTextData();
}

void TextCacheManager::addPageTextData(int pageIndex, const PageTextData& data)
{
    QMutexLocker locker(&m_mutex);
    if (m_maxCacheSize > 0 && m_cache.size() >= m_maxCacheSize) {
        if (!m_cache.isEmpty()) {
            auto it = m_cache.begin();
            m_cache.erase(it);
        }
    }
    m_cache.insert(pageIndex, data);
}

bool TextCacheManager::contains(int pageIndex) const
{
    QMutexLocker locker(&m_mutex);
    return m_cache.contains(pageIndex);
}

void TextCacheManager::clear()
{
    QMutexLocker locker(&m_mutex);
    if (m_isPreloading.loadAcquire()) {
        qWarning() << "TextCacheManager::clear() called while preload active!";
    }
    m_cache.clear();
    m_hitCount = 0;
    m_missCount = 0;
}

void TextCacheManager::setMaxCacheSize(int maxPages)
{
    QMutexLocker locker(&m_mutex);
    m_maxCacheSize = maxPages;
}

int TextCacheManager::cacheSize() const
{
    QMutexLocker locker(&m_mutex);
    return m_cache.size();
}

QString TextCacheManager::getStatistics() const
{
    QMutexLocker locker(&m_mutex);
    qint64 total = m_hitCount + m_missCount;
    double hitRate = (total > 0) ? (m_hitCount * 100.0 / total) : 0.0;
    return QString("TextCache: %1 pages, Hit Rate: %2%, Hits: %3, Misses: %4")
        .arg(m_cache.size())
        .arg(hitRate, 0, 'f', 1)
        .arg(m_hitCount)
        .arg(m_missCount);
}

// 这个槽由 PageExtractTask 通过 QueuedConnection 调用（在主线程）
void TextCacheManager::handleTaskDone(int pageIndex, PageTextData pageData, bool ok)
{
    // 无论 ok 与否，都需要把 remainingTasks-- 并在所有任务完成时结束 preload
    int remaining = m_remainingTasks.fetchAndSubRelaxed(1) - 1; // fetchAndSub returns old value; adjust
    Q_UNUSED(remaining);

    if (ok) {
        // 把 pageData 写入主缓存（带锁）
        {
            QMutexLocker locker(&m_mutex);
            if (m_maxCacheSize > 0 && m_cache.size() >= m_maxCacheSize) {
                if (!m_cache.isEmpty()) {
                    auto it = m_cache.begin();
                    m_cache.erase(it);
                }
            }
            m_cache.insert(pageIndex, pageData);
        }
        m_preloadedPages.ref(); // 增加已加载计数
        // 发进度（loadedCount）
        // pageCount 信息可以通过外部调用保存的 pageCount 再计算百分比；按你要求我们只发 loadedCount
        // 为方便使用，这里我们尝试读取 pageCount:  loaded + remaining = total
        int loaded = m_preloadedPages.loadAcquire();
        int total = loaded + qMax(0, m_remainingTasks.loadAcquire());
        emit preloadProgress(loaded, total);
    } else {
        // 未成功（被取消或解析出错），也发 progress，但不增加 loadedCount
        int loaded = m_preloadedPages.loadAcquire();
        int total = loaded + qMax(0, m_remainingTasks.loadAcquire());
        emit preloadProgress(loaded, total);
    }

    // 如果所有任务都处理完了，结束状态
    if (m_remainingTasks.loadAcquire() <= 0) {
        // 如果是用户请求了取消，则发 cancelled，否则发 completed
        if (m_cancelRequested.loadAcquire()) {
            m_isPreloading.storeRelease(0);
            emit preloadCancelled();
        } else {
            m_isPreloading.storeRelease(0);
            emit preloadCompleted();
        }
    }
}
