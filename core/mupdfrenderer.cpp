#include "mupdfrenderer.h"
#include <QDebug>
#include <QMutex>
#include <cstring>
#include <memory>

// ========================================
// MuPDF 多线程支持 - 全局锁管理
// ========================================

namespace {
constexpr int FZ_LOCK_MAX = 4;

static std::unique_ptr<QMutex> g_mupdf_locks[FZ_LOCK_MAX];
static bool g_locks_initialized = false;
static QMutex g_init_mutex;

extern "C" {
static void lock_mutex(void* user, int lock_no)
{
    if (lock_no >= 0 && lock_no < FZ_LOCK_MAX && g_mupdf_locks[lock_no]) {
        g_mupdf_locks[lock_no]->lock();
    }
}

static void unlock_mutex(void* user, int lock_no)
{
    if (lock_no >= 0 && lock_no < FZ_LOCK_MAX && g_mupdf_locks[lock_no]) {
        g_mupdf_locks[lock_no]->unlock();
    }
}
}

void initializeMuPDFLocks()
{
    QMutexLocker locker(&g_init_mutex);
    if (g_locks_initialized) return;

    for (int i = 0; i < FZ_LOCK_MAX; ++i) {
        g_mupdf_locks[i] = std::make_unique<QMutex>();
    }

    g_locks_initialized = true;
    qDebug() << "MuPDF locks initialized (auto cleanup with smart pointers)";
}

}

// ========================================
// MuPDFRenderer 实现
// ========================================

MuPDFRenderer::MuPDFRenderer()
    : m_context(nullptr)
    , m_document(nullptr)
    , m_pageCount(0)
{
    initializeMuPDFLocks();

    fz_locks_context locks;
    locks.user = nullptr;
    locks.lock = lock_mutex;
    locks.unlock = unlock_mutex;

    m_context = fz_new_context(nullptr, &locks, FZ_STORE_DEFAULT);

    if (!m_context) {
        setLastError("Failed to create MuPDF context");
        qCritical() << "MuPDFRenderer: Failed to create context";
        return;
    }

    fz_try(m_context) {
        fz_register_document_handlers(m_context);
    }
    fz_catch(m_context) {
        setLastError(QString("Failed to register document handlers: %1")
                         .arg(fz_caught_message(m_context)));
    }
}

MuPDFRenderer::~MuPDFRenderer()
{
    closeDocument();
    if (m_context) {
        fz_drop_context(m_context);
        m_context = nullptr;
    }
}

bool MuPDFRenderer::loadDocument(const QString& filePath, QString* errorMsg)
{
    if (!m_context) {
        QString err("Invalid MuPDF context");
        setLastError(err);
        if (errorMsg) *errorMsg = err;
        return false;
    }

    closeDocument();

    QByteArray pathUtf8 = filePath.toUtf8();

    fz_try(m_context) {
        m_document = fz_open_document(m_context, pathUtf8.constData());
        m_pageCount = fz_count_pages(m_context, m_document);
        m_pageSizeCache.resize(m_pageCount);
        for (int i = 0; i < m_pageCount; ++i) m_pageSizeCache[i] = QSizeF();
        m_currentFilePath = filePath;
        qInfo() << "MuPDFRenderer: Loaded document:" << filePath << "pages:" << m_pageCount;
    }
    fz_catch(m_context) {
        QString err = QString("Failed to open document: %1").arg(fz_caught_message(m_context));
        setLastError(err);
        if (errorMsg) *errorMsg = err;
        m_document = nullptr;
        m_pageCount = 0;
        m_pageSizeCache.clear();
        m_currentFilePath.clear();
        return false;
    }

    return true;
}

void MuPDFRenderer::closeDocument()
{
    if (m_document && m_context) {
        fz_drop_document(m_context, m_document);
        m_document = nullptr;
        m_pageCount = 0;
        m_pageSizeCache.clear();
        m_currentFilePath.clear();
    }
}

QString MuPDFRenderer::documentPath() const {
    return m_currentFilePath;
}

bool MuPDFRenderer::isDocumentLoaded() const
{
    return m_document != nullptr;
}

int MuPDFRenderer::pageCount() const
{
    return m_pageCount;
}

QSizeF MuPDFRenderer::pageSize(int pageIndex) const
{
    if (!m_document || pageIndex < 0 || pageIndex >= m_pageCount) {
        return QSizeF();
    }

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
        const_cast<QVector<QSizeF>&>(m_pageSizeCache)[pageIndex] = size;
    }
    fz_catch(m_context) {
        setLastError(QString("Failed to get page size for %1: %2")
                         .arg(pageIndex).arg(fz_caught_message(m_context)));
    }
    return size;
}

QVector<QSizeF> MuPDFRenderer::pageSizes(int startPage, int endPage) const
{
    QVector<QSizeF> sizes;
    if (!isDocumentLoaded()) return sizes;
    int st = qMax(0, startPage);
    int en = (endPage < 0) ? m_pageCount : qMin(m_pageCount, endPage);
    sizes.reserve(en - st);
    for (int i = st; i < en; ++i) sizes.append(pageSize(i));
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

MuPDFRenderer::RenderResult MuPDFRenderer::renderPage(int pageIndex, double zoom, int rotation)
{
    RenderResult result;
    if (!m_document) {
        result.errorMessage = "No document loaded";
        return result;
    }
    if (pageIndex < 0 || pageIndex >= m_pageCount) {
        result.errorMessage = QString("Invalid page index %1").arg(pageIndex);
        return result;
    }

    fz_try(m_context) {
        fz_page* page = fz_load_page(m_context, m_document, pageIndex);
        fz_matrix matrix = calculateMatrixForMuPDF(zoom, rotation);
        fz_rect bounds = fz_bound_page(m_context, page);
        bounds = fz_transform_rect(bounds, matrix);

        fz_pixmap* pixmap = fz_new_pixmap_with_bbox(m_context, fz_device_rgb(m_context),
                                                    fz_round_rect(bounds), nullptr, 0);
        fz_clear_pixmap_with_value(m_context, pixmap, 0xff);

        fz_device* device = fz_new_draw_device(m_context, fz_identity, pixmap);
        fz_run_page(m_context, page, device, matrix, nullptr);

        result.image = pixmapToQImage(m_context, pixmap);
        result.success = true;

        fz_close_device(m_context, device);
        fz_drop_device(m_context, device);
        fz_drop_pixmap(m_context, pixmap);
        fz_drop_page(m_context, page);
    }
    fz_catch(m_context) {
        QString err = QString("Failed to render page %1: %2")
        .arg(pageIndex).arg(fz_caught_message(m_context));
        setLastError(err);
        result.errorMessage = err;
    }

    return result;
}

bool MuPDFRenderer::extractText(int pageIndex, PageTextData& outData, QString* errorMsg)
{
    if (!m_document) {
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
        // ✅ 关键修复1: 先加载页面,获取原始边界
        page = fz_load_page(m_context, m_document, pageIndex);

        // ✅ 关键修复2: 使用 fz_bound_page 获取原始边界,不受任何变换影响
        fz_rect bound = fz_bound_page(m_context, page);

        // ✅ 关键修复3: 创建 stext_page 时使用原始边界
        stext = fz_new_stext_page(m_context, bound);

        // ✅ 关键修复4: 确保选项为0
        fz_stext_options opts;
        memset(&opts, 0, sizeof(opts));
        opts.flags = 0;

        fz_device* dev = fz_new_stext_device(m_context, stext, &opts);

        // ✅ 关键修复5: 使用单位矩阵,保证坐标系一致
        fz_run_page(m_context, page, dev, fz_identity, nullptr);

        fz_close_device(m_context, dev);
        fz_drop_device(m_context, dev);

        // ✅ 关键修复6: 提取文本时直接使用原始坐标
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

        if (stext) fz_drop_stext_page(m_context, stext);
        if (page) fz_drop_page(m_context, page);
    }
    fz_catch(m_context) {
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

bool MuPDFRenderer::isTextPDF(int samplePages)
{
    if (!m_document || m_pageCount == 0) return false;

    int pagesToCheck = samplePages;
    if (pagesToCheck <= 0 || pagesToCheck > m_pageCount) pagesToCheck = m_pageCount;

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
        if (hasText) textPageCount++;
    }

    double ratio = static_cast<double>(textPageCount) / pagesToCheck;
    return ratio >= 0.3;
}

void MuPDFRenderer::setLastError(const QString& error) const
{
    m_lastError = error;
}

QString MuPDFRenderer::getLastError() const
{
    return m_lastError;
}
