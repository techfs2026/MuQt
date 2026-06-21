#include "outlinemanager.h"
#include "perthreadmupdfrenderer.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <QDebug>

OutlineManager::OutlineManager(PerThreadMuPDFRenderer* renderer, QObject* parent)
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

    m_root = new OutlineItem();

    int itemCount = 0;

    if (outline) {
        itemCount = buildOutlineTree(outline, m_root);
        fz_drop_outline(ctx, outline);

        qInfo() << "OutlineManager: Loaded outline with" << itemCount << "items";
    } else {
        qInfo() << "OutlineManager: PDF has no outline, created empty root for editing";
    }

    m_totalItems = itemCount;

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

    for (fz_outline* node = outline; node; node = node->next) {
        QString title = QString::fromUtf8(node->title ? node->title : "");

        QString uri;
        if (node->uri) {
            uri = QString::fromUtf8(node->uri);
        }

        int pageIndex = resolvePageIndex(node);

        OutlineItem* item = new OutlineItem(title, pageIndex, uri);

        if (node->down) {
            itemCount += buildOutlineTree(node->down, item);
        }

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
        fz_location loc = fz_resolve_link(ctx, doc, outline->uri, nullptr, nullptr);

        pageIndex = fz_page_number_from_location(ctx, doc, loc);
    }
    fz_catch(ctx) {
        pageIndex = -1;
    }

    return pageIndex;
}

int OutlineManager::countItems(OutlineItem* item) const
{
    if (!item) {
        return 0;
    }

    int count = 1;

    for (int i = 0; i < item->childCount(); ++i) {
        count += countItems(item->child(i));
    }

    return count;
}
