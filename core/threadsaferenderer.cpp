#include "threadsaferenderer.h"
#include <QDebug>
#include <QThread>
#include <cstring>

// ========================================
// ThreadSafeRenderer 实现
// 每次 load 时创建 context 和 document
// 每次 close 时销毁它们
// ========================================

ThreadSafeRenderer::ThreadSafeRenderer()
    : m_context(nullptr)
    , m_document(nullptr)
    , m_pageCount(0)
{
    qInfo() << "ThreadSafeRenderer: Created (context will be created on load)"
            << "Thread:" << QThread::currentThreadId();
}

ThreadSafeRenderer::ThreadSafeRenderer(const QString& documentPath)
    : m_documentPath(documentPath)
    , m_context(nullptr)
    , m_document(nullptr)
    , m_pageCount(0)
{
    qDebug() << "ThreadSafeRenderer: Creating for" << documentPath
             << "Thread:" << QThread::currentThreadId();

    // 初始化 context
    if (!createContext()) {
        qCritical() << "ThreadSafeRenderer: Failed to initialize context";
        return;
    }

    // 加载文档
    QByteArray pathUtf8 = m_documentPath.toUtf8();

    fz_try(m_context) {
        m_document = fz_open_document(m_context, pathUtf8.constData());
        m_pageCount = fz_count_pages(m_context, m_document);

        // 初始化页面尺寸缓存
        m_pageSizeCache.resize(m_pageCount);
        for (int i = 0; i < m_pageCount; ++i) {
            m_pageSizeCache[i] = QSizeF();
        }

        qInfo() << "ThreadSafeRenderer: Successfully initialized with"
                << m_pageCount << "pages"
                << "Thread:" << QThread::currentThreadId();
    }
    fz_catch(m_context) {
        QString err = QString("Failed to open document: %1")
        .arg(fz_caught_message(m_context));
        setLastError(err);
        qCritical() << "ThreadSafeRenderer:" << err;

        m_document = nullptr;
        m_pageCount = 0;
        destroyContext();
    }
}

ThreadSafeRenderer::~ThreadSafeRenderer()
{
    QMutexLocker locker(&m_mutex);

    qInfo() << "ThreadSafeRenderer: Starting destruction"
            << "Thread:" << QThread::currentThreadId();

    // 确保文档已关闭
    if (isDocumentLoaded()) {
        // 关闭文档
        if (m_document && m_context) {
            fz_drop_document(m_context, m_document);
            m_document = nullptr;
        }

        m_pageCount = 0;
        m_pageSizeCache.clear();
        m_documentPath.clear();
    }

    // 销毁 context
    if (m_context) {
        fz_drop_context(m_context);
        m_context = nullptr;
    }

    qInfo() << "ThreadSafeRenderer: Destroyed"
            << "Thread:" << QThread::currentThreadId();
}

bool ThreadSafeRenderer::createContext()
{
    if (m_context) {
        qWarning() << "ThreadSafeRenderer: Context already exists";
        return true;
    }

    qDebug() << "ThreadSafeRenderer: Creating new context";

    // 创建新的 context (不使用锁，因为每个实例独立)
    m_context = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);

    if (!m_context) {
        setLastError("Failed to create MuPDF context");
        qCritical() << "ThreadSafeRenderer: Failed to create context";
        return false;
    }

    // 注册文档处理器
    fz_try(m_context) {
        fz_register_document_handlers(m_context);
    }
    fz_catch(m_context) {
        QString err = QString("Failed to register document handlers: %1")
        .arg(fz_caught_message(m_context));
        setLastError(err);
        qCritical() << "ThreadSafeRenderer:" << err;

        // 创建失败，清理 context
        fz_drop_context(m_context);
        m_context = nullptr;
        return false;
    }

    qDebug() << "ThreadSafeRenderer: Context created successfully";
    return true;
}

void ThreadSafeRenderer::destroyContext()
{
    if (!m_context) {
        return;
    }

    qDebug() << "ThreadSafeRenderer: Destroying context";

    // 销毁 context
    fz_drop_context(m_context);
    m_context = nullptr;

    qDebug() << "ThreadSafeRenderer: Context destroyed";
}

bool ThreadSafeRenderer::loadDocument(const QString& filePath, QString* errorMsg)
{
    QMutexLocker locker(&m_mutex);

    qInfo() << "ThreadSafeRenderer: Loading document:" << filePath;

    // 1. 先关闭已有文档
    if (isDocumentLoaded()) {
        qDebug() << "ThreadSafeRenderer: Closing existing document";

        // 关闭文档
        if (m_document && m_context) {
            fz_drop_document(m_context, m_document);
            m_document = nullptr;
        }

        // 清理状态
        m_pageCount = 0;
        m_pageSizeCache.clear();
        m_documentPath.clear();

        // 销毁 context
        destroyContext();
    }

    // 2. 创建新的 context
    if (!createContext()) {
        QString err = "Failed to create context";
        setLastError(err);
        if (errorMsg) *errorMsg = err;
        return false;
    }

    // 3. 打开文档
    QByteArray pathUtf8 = filePath.toUtf8();

    fz_try(m_context) {
        m_document = fz_open_document(m_context, pathUtf8.constData());
        m_pageCount = fz_count_pages(m_context, m_document);

        // 初始化页面尺寸缓存
        m_pageSizeCache.resize(m_pageCount);
        for (int i = 0; i < m_pageCount; ++i) {
            m_pageSizeCache[i] = QSizeF();
        }

        m_documentPath = filePath;

        qInfo() << "ThreadSafeRenderer: Document loaded successfully -"
                << m_pageCount << "pages";
    }
    fz_catch(m_context) {
        QString err = QString("Failed to open document: %1")
        .arg(fz_caught_message(m_context));
        setLastError(err);
        if (errorMsg) *errorMsg = err;

        qCritical() << "ThreadSafeRenderer:" << err;

        // 打开失败，清理资源
        m_document = nullptr;
        m_pageCount = 0;
        m_pageSizeCache.clear();
        m_documentPath.clear();

        // 销毁 context
        destroyContext();

        return false;
    }

    return true;
}

void ThreadSafeRenderer::closeDocument()
{
    QMutexLocker locker(&m_mutex);

    if (!m_document && !m_context) {
        return;
    }

    qInfo() << "ThreadSafeRenderer: Closing document";

    // 1. 关闭文档
    if (m_document && m_context) {
        qDebug() << "ThreadSafeRenderer: Dropping document";
        fz_drop_document(m_context, m_document);
        m_document = nullptr;
    }

    // 2. 清理状态
    m_pageCount = 0;
    m_pageSizeCache.clear();
    m_documentPath.clear();

    // 3. 销毁 context
    destroyContext();

    qInfo() << "ThreadSafeRenderer: Document closed";
}

QString ThreadSafeRenderer::documentPath() const
{
    QMutexLocker locker(&m_mutex);
    return m_documentPath;
}

bool ThreadSafeRenderer::isDocumentLoaded() const
{
    return m_document != nullptr && m_context != nullptr;
}

int ThreadSafeRenderer::pageCount() const
{
    return m_pageCount;
}

QSizeF ThreadSafeRenderer::pageSize(int pageIndex) const
{
    QMutexLocker locker(&m_mutex);

    if (!isDocumentLoaded() || pageIndex < 0 || pageIndex >= m_pageCount) {
        return QSizeF();
    }

    // 检查缓存
    if (!m_pageSizeCache[pageIndex].isEmpty()) {
        return m_pageSizeCache[pageIndex];
    }

    QSizeF size;

    fz_try(m_context) {
        fz_page* page = fz_load_page(m_context, m_document, pageIndex);
        fz_rect bounds = fz_bound_page(m_context, page);
        size.setWidth(bounds.x1 - bounds.x0);
        size.setHeight(bounds.y1 - bounds.y0);
        fz_drop_page(m_context, page);

        // 缓存结果
        m_pageSizeCache[pageIndex] = size;
    }
    fz_catch(m_context) {
        QString err = QString("Failed to get page size for page %1: %2")
        .arg(pageIndex)
            .arg(fz_caught_message(m_context));
        setLastError(err);
        qWarning() << "ThreadSafeRenderer:" << err;
    }

    return size;
}

QVector<QSizeF> ThreadSafeRenderer::pageSizes(int startPage, int endPage) const
{
    QMutexLocker locker(&m_mutex);

    QVector<QSizeF> sizes;

    if (!isDocumentLoaded()) {
        return sizes;
    }

    int st = qMax(0, startPage);
    int en = (endPage < 0) ? m_pageCount : qMin(m_pageCount, endPage);

    sizes.reserve(en - st);
    for (int i = st; i < en; ++i) {
        // 临时解锁以调用 pageSize
        m_mutex.unlock();
        sizes.append(pageSize(i));
        m_mutex.lock();
    }

    return sizes;
}

static fz_matrix calculateMatrixForMuPDF(double zoom, int rotation)
{
    fz_matrix matrix = fz_scale(zoom, zoom);
    int normalized = rotation % 360;
    if (normalized < 0) normalized += 360;
    if (normalized != 0) {
        matrix = fz_concat(matrix, fz_rotate(normalized));
    }
    return matrix;
}

static QImage pixmapToQImage(fz_context* ctx, fz_pixmap* pixmap)
{
    if (!pixmap) return QImage();

    int width = fz_pixmap_width(ctx, pixmap);
    int height = fz_pixmap_height(ctx, pixmap);
    int stride = fz_pixmap_stride(ctx, pixmap);
    unsigned char* samples = fz_pixmap_samples(ctx, pixmap);

    QImage image(width, height, QImage::Format_RGB888);
    for (int y = 0; y < height; ++y) {
        unsigned char* src = samples + y * stride;
        unsigned char* dest = image.scanLine(y);
        memcpy(dest, src, width * 3);
    }

    return image;
}

RenderResult ThreadSafeRenderer::renderPage(int pageIndex, double zoom, int rotation)
{
    QMutexLocker locker(&m_mutex);

    RenderResult result;

    if (!isDocumentLoaded()) {
        result.errorMessage = "No document loaded";
        return result;
    }

    if (pageIndex < 0 || pageIndex >= m_pageCount) {
        result.errorMessage = QString("Invalid page index %1").arg(pageIndex);
        return result;
    }

    fz_try(m_context) {
        // 加载页面
        fz_page* page = fz_load_page(m_context, m_document, pageIndex);

        // 计算变换矩阵
        fz_matrix matrix = calculateMatrixForMuPDF(zoom, rotation);

        // 计算边界
        fz_rect bounds = fz_bound_page(m_context, page);
        bounds = fz_transform_rect(bounds, matrix);

        // 创建 pixmap
        fz_pixmap* pixmap = fz_new_pixmap_with_bbox(
            m_context,
            fz_device_rgb(m_context),
            fz_round_rect(bounds),
            nullptr,
            0
            );
        fz_clear_pixmap_with_value(m_context, pixmap, 0xff);

        // 渲染
        fz_device* device = fz_new_draw_device(m_context, fz_identity, pixmap);
        fz_run_page(m_context, page, device, matrix, nullptr);

        // 转换为 QImage
        result.image = pixmapToQImage(m_context, pixmap);
        result.success = true;

        // 清理
        fz_close_device(m_context, device);
        fz_drop_device(m_context, device);
        fz_drop_pixmap(m_context, pixmap);
        fz_drop_page(m_context, page);
    }
    fz_catch(m_context) {
        QString err = QString("Failed to render page %1: %2")
        .arg(pageIndex)
            .arg(fz_caught_message(m_context));
        setLastError(err);
        result.errorMessage = err;
        qWarning() << "ThreadSafeRenderer:" << err;
    }

    return result;
}

bool ThreadSafeRenderer::extractText(int pageIndex, PageTextData& outData, QString* errorMsg)
{
    QMutexLocker locker(&m_mutex);

    if (!isDocumentLoaded()) {
        if (errorMsg) *errorMsg = "Document not loaded";
        return false;
    }

    if (pageIndex < 0 || pageIndex >= m_pageCount) {
        if (errorMsg) *errorMsg = QString("Invalid page index %1").arg(pageIndex);
        return false;
    }

    outData = PageTextData();
    outData.pageIndex = pageIndex;

    fz_stext_page* stext = nullptr;
    fz_page* page = nullptr;

    fz_try(m_context) {
        // 加载页面
        page = fz_load_page(m_context, m_document, pageIndex);

        // 获取原始边界
        fz_rect bound = fz_bound_page(m_context, page);

        // 创建 stext_page
        stext = fz_new_stext_page(m_context, bound);

        // 设置选项
        fz_stext_options opts;
        memset(&opts, 0, sizeof(opts));
        opts.flags = 0;

        // 创建设备
        fz_device* dev = fz_new_stext_device(m_context, stext, &opts);

        // 使用单位矩阵
        fz_run_page(m_context, page, dev, fz_identity, nullptr);

        fz_close_device(m_context, dev);
        fz_drop_device(m_context, dev);

        // 提取文本
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
                    outData.fullText.append(tc.character);
                }

                tb.lines.append(tl);
                outData.fullText.append('\n');
            }

            outData.blocks.append(tb);
            outData.fullText.append("\n\n");
        }

        // 清理
        if (stext) fz_drop_stext_page(m_context, stext);
        if (page) fz_drop_page(m_context, page);
    }
    fz_catch(m_context) {
        // 清理
        if (stext) fz_drop_stext_page(m_context, stext);
        if (page) fz_drop_page(m_context, page);

        if (errorMsg) {
            *errorMsg = QString("Failed to extract text on page %1: %2")
            .arg(pageIndex)
                .arg(fz_caught_message(m_context));
        }
        return false;
    }

    return true;
}

bool ThreadSafeRenderer::isTextPDF(int samplePages)
{
    QMutexLocker locker(&m_mutex);

    if (!isDocumentLoaded() || m_pageCount == 0) {
        return false;
    }

    int pagesToCheck = samplePages;
    if (pagesToCheck <= 0 || pagesToCheck > m_pageCount) {
        pagesToCheck = m_pageCount;
    }

    int textPageCount = 0;

    for (int i = 0; i < pagesToCheck; ++i) {
        bool hasText = false;

        fz_try(m_context) {
            fz_page* page = fz_load_page(m_context, m_document, i);
            fz_stext_page* stext = fz_new_stext_page(m_context, fz_bound_page(m_context, page));
            fz_stext_options options = {0};
            fz_device* device = fz_new_stext_device(m_context, stext, &options);
            fz_run_page(m_context, page, device, fz_identity, nullptr);
            fz_close_device(m_context, device);
            fz_drop_device(m_context, device);

            // 检查是否有文本
            for (fz_stext_block* block = stext->first_block; block; block = block->next) {
                if (block->type == FZ_STEXT_BLOCK_TEXT) {
                    for (fz_stext_line* line = block->u.t.first_line; line; line = line->next) {
                        for (fz_stext_char* ch = line->first_char; ch; ch = ch->next) {
                            if (ch->c > 32) {
                                hasText = true;
                                break;
                            }
                        }
                        if (hasText) break;
                    }
                    if (hasText) break;
                }
            }

            fz_drop_stext_page(m_context, stext);
            fz_drop_page(m_context, page);
        }
        fz_catch(m_context) {
            hasText = false;
        }

        if (hasText) {
            textPageCount++;
        }
    }

    double ratio = static_cast<double>(textPageCount) / pagesToCheck;
    return ratio >= 0.3;
}

QString ThreadSafeRenderer::getLastError() const
{
    QMutexLocker locker(&m_mutex);
    return m_lastError;
}

void ThreadSafeRenderer::setLastError(const QString& error) const
{
    m_lastError = error;
}
