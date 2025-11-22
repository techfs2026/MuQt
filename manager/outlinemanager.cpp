#include "outlinemanager.h"
#include "mupdfrenderer.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <QDebug>

OutlineManager::OutlineManager(MuPDFRenderer* renderer, QObject* parent)
    : QObject(parent)
    , m_root(nullptr)
    , m_renderer(renderer)
    , m_totalItems(0)
{
}

OutlineManager::~OutlineManager()
{
    clear();
}

bool OutlineManager::loadOutline()
{

    clear();

    if (!m_renderer || !m_renderer->isDocumentLoaded()) {
        qWarning() << "OutlineManager: No document loaded";
        emit outlineLoaded(false, 0);
        return false;
    }

    // 创建根节点
    m_root = new OutlineItem();

    // 获取MuPDF上下文和文档
    fz_context* ctx = static_cast<fz_context*>(m_renderer->context());
    fz_document* doc = static_cast<fz_document*>(m_renderer->document());

    if (!ctx || !doc) {
        emit outlineLoaded(false, 0);
        return false;
    }

    fz_outline* outline = nullptr;

    fz_try(ctx) {
        // 加载文档大纲
        outline = fz_load_outline(ctx, doc);
        if (outline) {
            // 递归构建大纲树
            m_totalItems = buildOutlineTree(outline, m_root);

            qInfo() << "OutlineManager: Loaded" << m_totalItems << "outline items";
        } else {
            qInfo() << "OutlineManager: Document has no outline";
        }
    }
    fz_always(ctx) {
        // 释放大纲
        if (outline) {
            fz_drop_outline(ctx, outline);
        }
    }
    fz_catch(ctx) {
        qWarning() << "OutlineManager: Failed to load outline:"
                   << fz_caught_message(ctx);
        emit outlineLoaded(false, 0);
        return false;
    }

    bool success = m_totalItems > 0;
    emit outlineLoaded(success, m_totalItems);
    return success;
}

void OutlineManager::clear()
{
    if (m_root) {
        delete m_root;
        m_root = nullptr;
    }
    m_totalItems = 0;
}

int OutlineManager::buildOutlineTree(void* fzOutline, OutlineItem* parent)
{
    if (!fzOutline || !parent) {
        return 0;
    }

    fz_outline* outline = static_cast<fz_outline*>(fzOutline);
    int itemCount = 0;

    // 遍历同级节点
    for (fz_outline* node = outline; node; node = node->next) {
        // 获取标题
        QString title = QString::fromUtf8(node->title ? node->title : "");

        // 获取URI（如果是外部链接）
        QString uri;
        if (node->uri) {
            uri = QString::fromUtf8(node->uri);
        }

        // 解析目标页码
        int pageIndex = resolvePageIndex(node);

        // 创建大纲项
        OutlineItem* item = new OutlineItem(title, pageIndex, uri);

        // 递归处理子节点
        if (node->down) {
            itemCount += buildOutlineTree(node->down, item);
        }

        // 添加到父节点
        parent->addChild(item);
        itemCount++;
    }

    return itemCount;
}

int OutlineManager::resolvePageIndex(void* fzOutline)
{
    if (!fzOutline || !m_renderer) {
        return -1;
    }

    fz_outline* outline = static_cast<fz_outline*>(fzOutline);
    fz_context* ctx = static_cast<fz_context*>(m_renderer->context());
    fz_document* doc = static_cast<fz_document*>(m_renderer->document());

    if (!ctx || !doc) {
        return -1;
    }

    int pageIndex = -1;

    fz_try(ctx) {
        // 解析链接目标
        fz_location loc = fz_resolve_link(ctx, doc, outline->uri, nullptr, nullptr);

        // 将location转换为页码
        pageIndex = fz_page_number_from_location(ctx, doc, loc);
    }
    fz_catch(ctx) {
        // 解析失败，可能是外部链接或无效链接
        pageIndex = -1;
    }

    return pageIndex;
}

int OutlineManager::countItems(OutlineItem* item) const
{
    if (!item) {
        return 0;
    }

    int count = 1; // 当前节点

    for (int i = 0; i < item->childCount(); ++i) {
        count += countItems(item->child(i));
    }

    return count;
}
