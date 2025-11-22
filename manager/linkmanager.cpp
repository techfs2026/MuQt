#include "linkmanager.h"
#include "mupdfrenderer.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <QDebug>

LinkManager::LinkManager(MuPDFRenderer* renderer, QObject* parent)
    : QObject(parent)
    , m_renderer(renderer)
{
}

LinkManager::~LinkManager()
{
    clear();
}

QVector<PDFLink> LinkManager::loadPageLinks(int pageIndex)
{
    // 检查缓存
    if (m_cachedLinks.contains(pageIndex)) {
        return m_cachedLinks[pageIndex];
    }

    QVector<PDFLink> links;

    if (!m_renderer || !m_renderer->isDocumentLoaded()) {
        return links;
    }

    fz_context* ctx = static_cast<fz_context*>(m_renderer->context());
    fz_document* doc = static_cast<fz_document*>(m_renderer->document());

    if (!ctx || !doc) {
        return links;
    }

    fz_page* page = nullptr;

    fz_try(ctx) {
        // 加载页面
        page = fz_load_page(ctx, doc, pageIndex);

        // 获取链接列表
        fz_link* link = fz_load_links(ctx, page);

        // 遍历链接
        for (fz_link* current = link; current; current = current->next) {
            PDFLink pdfLink;

            // 获取链接区域
            fz_rect rect = current->rect;
            pdfLink.rect = QRectF(rect.x0, rect.y0,
                                  rect.x1 - rect.x0,
                                  rect.y1 - rect.y0);

            // 获取URI
            if (current->uri) {
                pdfLink.uri = QString::fromUtf8(current->uri);
            }

            // 解析目标页码
            pdfLink.targetPage = resolveLinkTarget(current);

            links.append(pdfLink);
        }

        // 释放链接列表
        fz_drop_link(ctx, link);
    }
    fz_always(ctx) {
        if (page) {
            fz_drop_page(ctx, page);
        }
    }
    fz_catch(ctx) {
        qWarning() << "LinkManager: Failed to load links for page" << pageIndex
                   << ":" << fz_caught_message(ctx);
    }

    // 缓存结果
    m_cachedLinks[pageIndex] = links;

    if (!links.isEmpty()) {
        qDebug() << "LinkManager: Found" << links.size() << "links on page" << pageIndex;
    }

    return links;
}

const PDFLink* LinkManager::hitTestLink(int pageIndex, const QPointF& pos, double zoom)
{
    QVector<PDFLink> links = loadPageLinks(pageIndex);

    if (links.isEmpty()) {
        return nullptr;
    }

    // ✅ 修复：pos 已经是屏幕坐标（像素），需要转换为页面坐标（点）
    // 屏幕坐标 / zoom = 页面坐标
    QPointF pagePos = pos / zoom;

    // 查找包含该点的链接
    for (const PDFLink& link : links) {
        if (link.rect.contains(pagePos)) {
            // 返回缓存中的链接指针
            int index = &link - links.constData();
            return &m_cachedLinks[pageIndex][index];
        }
    }

    return nullptr;
}

void LinkManager::clear()
{
    m_cachedLinks.clear();
}

int LinkManager::resolveLinkTarget(void* fzLink)
{
    if (!fzLink || !m_renderer) {
        return -1;
    }

    fz_link* link = static_cast<fz_link*>(fzLink);
    fz_context* ctx = static_cast<fz_context*>(m_renderer->context());
    fz_document* doc = static_cast<fz_document*>(m_renderer->document());

    if (!ctx || !doc || !link->uri) {
        return -1;
    }

    int pageIndex = -1;

    fz_try(ctx) {
        // 解析链接目标
        fz_location loc = fz_resolve_link(ctx, doc, link->uri, nullptr, nullptr);

        // 将location转换为页码
        pageIndex = fz_page_number_from_location(ctx, doc, loc);
    }
    fz_catch(ctx) {
        // 解析失败，可能是外部链接
        pageIndex = -1;
    }

    return pageIndex;
}
