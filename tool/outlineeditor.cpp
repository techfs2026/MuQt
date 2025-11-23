// outlineeditor.cpp
#include "outlineeditor.h"
#include "outlineitem.h"
#include "mupdfrenderer.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QDebug>
#include <QMetaObject>
#include <QThread>
#include <functional>

OutlineEditor::OutlineEditor(MuPDFRenderer* renderer, QObject* parent)
    : QObject(parent)
    , m_renderer(renderer)
    , m_root(nullptr)
    , m_modified(false)
{
}

OutlineEditor::~OutlineEditor()
{
}

void OutlineEditor::setRoot(OutlineItem* root)
{
    if (!root) {
        qWarning() << "OutlineEditor::setRoot: root is nullptr, creating virtual root";
        m_root = new OutlineItem();
    } else {
        m_root = root;
    }

    m_modified = false;
}

OutlineItem* OutlineEditor::addOutline(OutlineItem* parentItem,
                                       const QString& title,
                                       int pageIndex,
                                       int insertIndex)
{
    if (!m_renderer || !m_renderer->isDocumentLoaded()) {
        qWarning() << "OutlineEditor: No document loaded";
        return nullptr;
    }

    if (!validateOutline(title, pageIndex)) {
        qWarning() << "OutlineEditor: Invalid outline parameters";
        return nullptr;
    }

    if (!m_root) {
        qWarning() << "OutlineEditor: No root, creating virtual root";
        m_root = new OutlineItem();
    }

    OutlineItem* parent = parentItem ? parentItem : m_root;
    if (!parent) {
        qWarning() << "OutlineEditor: No valid parent node";
        return nullptr;
    }

    OutlineItem* newItem = new OutlineItem(title, pageIndex);

    // 如果需要插入到某个位置，先简单处理（OutlineItem 目前无批量操作）
    if (insertIndex >= 0 && insertIndex < parent->childCount()) {
        // 简化：目前 OutlineItem 没有 insert API，先 append
        parent->addChild(newItem);
    } else {
        parent->addChild(newItem);
    }

    m_modified = true;
    emit outlineModified();

    qInfo() << "OutlineEditor: Added outline:" << title << "at page" << (pageIndex + 1);
    return newItem;
}

bool OutlineEditor::deleteOutline(OutlineItem* item)
{
    if (!item || !m_root) return false;
    if (item == m_root) {
        qWarning() << "OutlineEditor: Cannot delete root node";
        return false;
    }

    OutlineItem* parent = item->parent();
    if (!parent) {
        qWarning() << "OutlineEditor: Item has no parent";
        return false;
    }

    // 若 OutlineItem 提供 removeChild，调用它；否则做兼容处理
    bool removed = parent->removeChild(item);
    if (!removed) {
        qWarning() << "OutlineEditor: Failed to remove item from parent";
        return false;
    }

    delete item;

    m_modified = true;
    emit outlineModified();
    qInfo() << "OutlineEditor: Deleted outline";
    return true;
}

bool OutlineEditor::renameOutline(OutlineItem* item, const QString& newTitle)
{
    if (!item || newTitle.isEmpty()) return false;
    if (item == m_root) {
        qWarning() << "OutlineEditor: Cannot rename root node";
        return false;
    }

    QString oldTitle = item->title();
    item->setTitle(newTitle);

    m_modified = true;
    emit outlineModified();

    qInfo() << "OutlineEditor: Renamed outline from" << oldTitle << "to" << newTitle;
    return true;
}

bool OutlineEditor::updatePageIndex(OutlineItem* item, int newPageIndex)
{
    if (!item) return false;

    if (item == m_root) {
        qWarning() << "OutlineEditor: Cannot update root node page index";
        return false;
    }

    // 验证页码有效性
    if (newPageIndex < -1) {
        qWarning() << "OutlineEditor: Invalid page index:" << newPageIndex;
        return false;
    }

    if (m_renderer && newPageIndex >= m_renderer->pageCount()) {
        qWarning() << "OutlineEditor: Page index out of range:" << newPageIndex;
        return false;
    }

    int oldPageIndex = item->pageIndex();

    // 如果页码没有变化，直接返回
    if (oldPageIndex == newPageIndex) {
        return true;
    }

    // 更新页码
    item->setPageIndex(newPageIndex);

    // 设置修改标记并发出信号
    m_modified = true;
    emit outlineModified();

    qInfo() << "OutlineEditor: Updated page index from" << (oldPageIndex + 1)
            << "to" << (newPageIndex + 1);

    return true;
}

bool OutlineEditor::moveOutline(OutlineItem* item,
                                OutlineItem* newParent,
                                int newIndex)
{
    if (!item || !m_root)
        return false;

    if (item == m_root) {
        qWarning() << "OutlineEditor: Cannot move root node";
        return false;
    }

    // 目标父节点
    OutlineItem* targetParent = newParent ? newParent : m_root;

    // ===== 1. 防止移动到自己的子节点 =====
    OutlineItem* p = targetParent;
    while (p) {
        if (p == item) {
            qWarning() << "OutlineEditor: Cannot move to descendant";
            return false;
        }
        p = p->parent();
    }

    // ===== 2. 从旧父节点移除（必须真实移除 child） =====
    OutlineItem* oldParent = item->parent();
    if (!oldParent) {
        qWarning() << "OutlineEditor: Item has no parent";
        return false;
    }

    oldParent->removeChild(item);   // ← 真正移除 child

    // ===== 3. 在 newParent 中按 newIndex 插入 =====
    if (newIndex < 0 || newIndex > targetParent->childCount())
        newIndex = targetParent->childCount();

    targetParent->insertChild(newIndex, item);

    m_modified = true;
    emit outlineModified();

    qInfo() << "OutlineEditor: Moved outline successfully";
    return true;
}


// ---------------------------
// Internal helpers (file-local)
// ---------------------------

namespace {
// Create a document-owned dict (indirect object). Uses pdf_add_new_dict if available.
inline pdf_obj* create_doc_dict(fz_context* ctx, pdf_document* doc, int initial = 4)
{
    // pdf_add_new_dict is common in MuPDF; if not available in your build,
    // you will need to replace with appropriate equivalent.
    return pdf_add_new_dict(ctx, doc, initial);
}

inline pdf_obj* create_doc_array(fz_context* ctx, pdf_document* doc, int initial = 4)
{
    return pdf_add_new_array(ctx, doc, initial);
}

// Validate OutlineItem tree for obvious issues (empty title, invalid page index)
bool validate_tree(OutlineItem* node, MuPDFRenderer* renderer, QString* reason = nullptr)
{
    if (!node) return true;
    for (int i = 0; i < node->childCount(); ++i) {
        OutlineItem* c = node->child(i);
        if (!c) {
            if (reason) *reason = QStringLiteral("Null child pointer");
            return false;
        }
        if (c->title().trimmed().isEmpty()) {
            if (reason) *reason = QStringLiteral("Empty title");
            return false;
        }
        if (c->pageIndex() < -1) {
            if (reason) *reason = QStringLiteral("Invalid pageIndex");
            return false;
        }
        if (renderer && c->pageIndex() >= 0 && c->pageIndex() >= renderer->pageCount()) {
            if (reason) *reason = QStringLiteral("pageIndex out of range");
            return false;
        }
        if (!validate_tree(c, renderer, reason)) return false;
    }
    return true;
}

// Build a document-owned outline item recursively.
// Returns a pdf_obj* that is an indirect (document-owned) object.
// Caller must drop the returned local reference when appropriate (we do so in callers).
pdf_obj* buildPdfOutlineRecursive(fz_context* ctx, pdf_document* pdfDoc,
                                  MuPDFRenderer* renderer, OutlineItem* item)
{
    if (!item || !item->isValid()) return nullptr;

    pdf_obj* item_obj = create_doc_dict(ctx, pdfDoc, 8);
    if (!item_obj) {
        qWarning() << "buildPdfOutlineRecursive: failed to create item dict";
        return nullptr;
    }

    fz_try(ctx) {
        // Title
        QByteArray titleBytes = item->title().toUtf8();
        pdf_dict_put_text_string(ctx, item_obj, PDF_NAME(Title), titleBytes.constData());

        // Dest: page target (if any)
        if (item->pageIndex() >= 0) {
            if (renderer && item->pageIndex() >= renderer->pageCount()) {
                qWarning() << "buildPdfOutlineRecursive: pageIndex out of range" << item->pageIndex();
            } else {
                pdf_obj* page_ref = pdf_lookup_page_obj(ctx, pdfDoc, item->pageIndex());
                if (page_ref) {
                    pdf_obj* dest = create_doc_array(ctx, pdfDoc, 5);
                    if (!dest) {
                        qWarning() << "buildPdfOutlineRecursive: failed to create dest array";
                    } else {
                        fz_try(ctx) {
                            pdf_array_push(ctx, dest, page_ref);
                            pdf_array_push(ctx, dest, PDF_NAME(XYZ));
                            pdf_array_push(ctx, dest, PDF_NULL);
                            pdf_array_push(ctx, dest, PDF_NULL);
                            pdf_array_push(ctx, dest, PDF_NULL);
                            pdf_dict_put(ctx, item_obj, PDF_NAME(Dest), dest);
                        }
                        fz_always(ctx) {
                            pdf_drop_obj(ctx, dest); // drop local ref; doc owns it
                        }
                        fz_catch(ctx) {
                            fz_rethrow(ctx);
                        }
                    }
                } else {
                    qWarning() << "buildPdfOutlineRecursive: page_ref null for index" << item->pageIndex();
                }
            }
        }

        // Children
        QList<pdf_obj*> children;
        for (int i = 0; i < item->childCount(); ++i) {
            OutlineItem* ch = item->child(i);
            if (!ch || !ch->isValid()) continue;

            pdf_obj* child_obj = buildPdfOutlineRecursive(ctx, pdfDoc, renderer, ch);
            if (!child_obj) continue;

            // link parent
            pdf_dict_put(ctx, child_obj, PDF_NAME(Parent), item_obj);
            children.append(child_obj);
        }

        // If children exist, set First/Last/Count and Prev/Next
        if (!children.isEmpty()) {
            pdf_dict_put(ctx, item_obj, PDF_NAME(First), children.first());
            pdf_dict_put(ctx, item_obj, PDF_NAME(Last), children.last());
            pdf_dict_put_int(ctx, item_obj, PDF_NAME(Count), children.size());

            for (int i = 0; i < children.size(); ++i) {
                if (i > 0) pdf_dict_put(ctx, children[i], PDF_NAME(Prev), children[i-1]);
                if (i < children.size() - 1) pdf_dict_put(ctx, children[i], PDF_NAME(Next), children[i+1]);
            }

            // drop local refs to children (document owns them)
            for (pdf_obj* co : children) pdf_drop_obj(ctx, co);
        }
    }
    fz_catch(ctx) {
        qWarning() << "buildPdfOutlineRecursive: caught mu error:" << fz_caught_message(ctx);
        if (item_obj) pdf_drop_obj(ctx, item_obj);
        return nullptr;
    }

    return item_obj;
}
} // namespace

// The public saveToDocument - mid-level refactor: single entry point, robustly implemented.
// This method ensures:
//  - validation of outline data,
//  - creation of document-owned outlines and items,
//  - correct Parent/First/Last/Prev/Next linking,
//  - safe use of pdf_add_new_* and proper pdf_drop_obj management,
//  - incremental save with error capture.
bool OutlineEditor::saveToDocument(const QString& filePath)
{
    // Quick preconditions
    if (!m_renderer || !m_renderer->isDocumentLoaded()) {
        QString msg = "No document loaded";
        qWarning() << "OutlineEditor:" << msg;
        emit saveCompleted(false, msg);
        return false;
    }
    if (!m_root) {
        QString msg = "No outline data";
        qWarning() << "OutlineEditor:" << msg;
        emit saveCompleted(false, msg);
        return false;
    }

    // Validate data tree
    QString reason;
    if (!validate_tree(m_root, m_renderer, &reason)) {
        qWarning() << "OutlineEditor: invalid outline tree:" << reason;
        emit saveCompleted(false, reason);
        return false;
    }

    // Access MuPDF context & document
    fz_context* ctx = static_cast<fz_context*>(m_renderer->context());
    fz_document* fzdoc = static_cast<fz_document*>(m_renderer->document());
    if (!ctx || !fzdoc) {
        QString msg = "Invalid MuPDF context or document";
        qWarning() << "OutlineEditor:" << msg;
        emit saveCompleted(false, msg);
        return false;
    }

    pdf_document* pdfDoc = pdf_document_from_fz_document(ctx, fzdoc);
    if (!pdfDoc) {
        QString msg = "Document is not a PDF";
        qWarning() << "OutlineEditor:" << msg;
        emit saveCompleted(false, msg);
        return false;
    }

    QString savePath = filePath.isEmpty() ? m_renderer->currentFilePath() : filePath;
    if (savePath.isEmpty()) {
        QString msg = "No file path specified";
        qWarning() << "OutlineEditor:" << msg;
        emit saveCompleted(false, msg);
        return false;
    }

    // Backup
    QString backupPath = createBackup(savePath);
    if (!backupPath.isEmpty()) qInfo() << "OutlineEditor: Backup created at:" << backupPath;

    bool success = false;
    QString errorMsg;

    // All MuPDF operations must be inside try/catch
    fz_try(ctx) {
        // Resolve catalog (Root) and validate
        pdf_obj* trailer = pdf_trailer(ctx, pdfDoc);
        pdf_obj* catalog_ref = pdf_dict_get(ctx, trailer, PDF_NAME(Root));
        pdf_obj* catalog = nullptr;
        if (catalog_ref) catalog = pdf_resolve_indirect(ctx, catalog_ref);

        if (!catalog || !pdf_is_dict(ctx, catalog)) {
            fz_throw(ctx, FZ_ERROR_GENERIC, "Invalid PDF catalog");
        }

        // Remove existing Outlines
        pdf_dict_del(ctx, catalog, PDF_NAME(Outlines));

        // Only build outlines if root has children
        if (m_root->childCount() > 0) {
            // Create outlines dict as document-owned indirect object
            pdf_obj* outlines = create_doc_dict(ctx, pdfDoc, 4);
            if (!outlines) fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to create outlines dict");

            fz_try(ctx) {
                pdf_dict_put(ctx, outlines, PDF_NAME(Type), PDF_NAME(Outlines));

                QList<pdf_obj*> topItems;
                for (int i = 0; i < m_root->childCount(); ++i) {
                    OutlineItem* child = m_root->child(i);
                    if (!child || !child->isValid()) continue;

                    pdf_obj* item_obj = buildPdfOutlineRecursive(ctx, pdfDoc, m_renderer, child);
                    if (!item_obj) {
                        qWarning() << "OutlineEditor: buildPdfOutlineRecursive returned null for top child" << i;
                        continue;
                    }

                    // link Parent -> outlines
                    pdf_dict_put(ctx, item_obj, PDF_NAME(Parent), outlines);
                    topItems.append(item_obj);
                }

                // Link first/last/count and prev/next
                if (!topItems.isEmpty()) {
                    pdf_dict_put(ctx, outlines, PDF_NAME(First), topItems.first());
                    pdf_dict_put(ctx, outlines, PDF_NAME(Last),  topItems.last());
                    pdf_dict_put_int(ctx, outlines, PDF_NAME(Count), topItems.size());

                    for (int i = 0; i < topItems.size(); ++i) {
                        if (i > 0) pdf_dict_put(ctx, topItems[i], PDF_NAME(Prev), topItems[i-1]);
                        if (i < topItems.size() - 1) pdf_dict_put(ctx, topItems[i], PDF_NAME(Next), topItems[i+1]);
                    }

                    // drop local refs to topItems (document/catalog owns them now)
                    for (pdf_obj* obj : topItems) pdf_drop_obj(ctx, obj);
                }

                // Attach outlines to catalog (catalog keeps a reference)
                pdf_dict_put(ctx, catalog, PDF_NAME(Outlines), outlines);
            }
            fz_always(ctx) {
                // Drop local ref; catalog/document retains ref
                pdf_drop_obj(ctx, outlines);
            }
            fz_catch(ctx) {
                fz_rethrow(ctx);
            }
        }

        // Save (incremental)
        pdf_write_options opts = pdf_default_write_options;
        opts.do_incremental = 1;
        opts.do_garbage = 0;

        QByteArray pathBytes = savePath.toUtf8();
        qInfo() << "OutlineEditor: about to save PDF to" << savePath;
        pdf_save_document(ctx, pdfDoc, pathBytes.constData(), &opts);

        success = true;
        m_modified = false;
        qInfo() << "OutlineEditor: saveToDocument completed successfully";
    }
    fz_catch(ctx) {
        errorMsg = QString::fromUtf8(fz_caught_message(ctx));
        qWarning() << "OutlineEditor: saveToDocument failed:" << errorMsg;
        success = false;
    }

    emit saveCompleted(success, errorMsg);
    return success;
}

// Backwards-compatible wrapper: reuse existing signature for external callers.
// This uses the same internal helper to produce a document-owned item.
void* OutlineEditor::createPdfOutline(void* ctx_ptr, void* doc_ptr, OutlineItem* item)
{
    if (!ctx_ptr || !doc_ptr || !item) return nullptr;
    fz_context* ctx = static_cast<fz_context*>(ctx_ptr);
    pdf_document* doc = static_cast<pdf_document*>(doc_ptr);

    // call the recursive builder - it returns a pdf_obj* (document-owned)
    pdf_obj* obj = buildPdfOutlineRecursive(ctx, doc, m_renderer, item);
    return static_cast<void*>(obj);
}

void* OutlineEditor::buildPdfOutlineTree(void* ctx_ptr, void* doc_ptr,
                                         OutlineItem* item, void* parent_ptr)
{
    // Kept for compatibility; call createPdfOutline wrapper
    return createPdfOutline(ctx_ptr, doc_ptr, item);
}

bool OutlineEditor::validateOutline(const QString& title, int pageIndex) const
{
    if (title.trimmed().isEmpty()) {
        qWarning() << "OutlineEditor: Empty title";
        return false;
    }

    if (pageIndex < -1) {
        qWarning() << "OutlineEditor: Invalid page index:" << pageIndex;
        return false;
    }

    if (m_renderer && pageIndex >= m_renderer->pageCount()) {
        qWarning() << "OutlineEditor: Page index out of range:" << pageIndex;
        return false;
    }

    return true;
}

int OutlineEditor::findItemIndex(OutlineItem* item) const
{
    if (!item || !item->parent()) {
        return -1;
    }

    OutlineItem* parent = item->parent();
    for (int i = 0; i < parent->childCount(); ++i) {
        if (parent->child(i) == item) {
            return i;
        }
    }

    return -1;
}

bool OutlineEditor::removeFromParent(OutlineItem* item)
{
    if (!item) return false;
    OutlineItem* parent = item->parent();
    if (!parent) return false;

    bool removed = parent->removeChild(item);
    if (removed) {
        item->setParent(nullptr);
        return true;
    }

    // Fallback: clear parent pointer (best-effort)
    item->setParent(nullptr);
    return true;
}

QString OutlineEditor::createBackup(const QString& filePath) const
{
    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        return QString();
    }

    QFileInfo fileInfo(filePath);
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString backupPath = fileInfo.absolutePath() + "/" +
                         fileInfo.baseName() + "_backup_" + timestamp + "." +
                         fileInfo.completeSuffix();

    if (QFile::copy(filePath, backupPath)) {
        return backupPath;
    }

    return QString();
}
