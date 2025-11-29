#include "outlinemanager.h"
#include "threadsaferenderer.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <QDebug>

OutlineManager::OutlineManager(ThreadSafeRenderer* renderer, QObject* parent)
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
    if (!m_renderer || !m_renderer->isDocumentLoaded()) {
        qWarning() << "OutlineManager: No document loaded";
        return false;
    }

    // 清空旧数据
    clear();

    fz_context* ctx = static_cast<fz_context*>(m_renderer->context());
    fz_document* doc = static_cast<fz_document*>(m_renderer->document());

    if (!ctx || !doc) {
        qWarning() << "OutlineManager: Invalid MuPDF context or document";
        return false;
    }

    fz_outline* outline = nullptr;

    fz_try(ctx) {
        outline = fz_load_outline(ctx, doc);
    }
    fz_catch(ctx) {
        qWarning() << "OutlineManager: Failed to load outline:"
                   << fz_caught_message(ctx);
        outline = nullptr;
    }

    // ✅ 关键修改：无论是否有 outline，都创建虚拟根节点
    // 虚拟根节点：title = 空字符串, pageIndex = -1
    // 这个节点不会显示在 UI 上，只作为容器
    m_root = new OutlineItem();

    int itemCount = 0;

    if (outline) {
        // PDF 有目录：从 MuPDF outline 构建树
        itemCount = buildOutlineTree(outline, m_root);
        fz_drop_outline(ctx, outline);

        qInfo() << "OutlineManager: Loaded outline with" << itemCount << "items";
    } else {
        // PDF 没有目录：root 是空的容器，但不是 nullptr
        // 用户可以向这个空容器添加新的目录项
        qInfo() << "OutlineManager: PDF has no outline, created empty root for editing";
    }

    // ✅ 总是返回 true（即使没有目录项）
    // 因为 root 已经创建成功，可以进行编辑操作
    emit outlineLoaded(true, itemCount);
    return true;
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
