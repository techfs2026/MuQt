#include "outlineeditor.h"
#include "outlineitem.h"
#include "perthreadmupdfrenderer.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QDebug>
#include <QMetaObject>
#include <QThread>
#include <QCoreApplication>

OutlineEditor::OutlineEditor(PerThreadMuPDFRenderer* renderer, QObject* parent)
    : QObject(parent)
    , m_renderer(renderer)
    , m_root(nullptr)
    , m_modified(false)
{
}

OutlineEditor::~OutlineEditor()
{
}

void OutlineEditor::setModified(bool modified)
{
    if (m_modified == modified) {
        return;
    }
    m_modified = modified;
    emit unsavedChangesChanged(m_modified);
}

void OutlineEditor::setRoot(OutlineItem* root)
{
    if (!root) {
        qWarning() << "OutlineEditor::setRoot: root is nullptr, creating virtual root";
        m_root = new OutlineItem();
    } else {
        m_root = root;
    }

    setModified(false);
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

    // insertIndex 有效时按位置插入（用于"添加兄弟节点"），否则追加到末尾
    if (insertIndex >= 0 && insertIndex <= parent->childCount()) {
        parent->insertChild(insertIndex, newItem);
    } else {
        parent->addChild(newItem);
    }

    setModified(true);
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

    bool removed = parent->removeChild(item);
    if (!removed) {
        qWarning() << "OutlineEditor: Failed to remove item from parent";
        return false;
    }

    delete item;

    setModified(true);
    emit outlineModified();
    qInfo() << "OutlineEditor: Deleted outline";
    return true;
}

bool OutlineEditor::deleteAllOutlines()
{
    if (!m_root) {
        qWarning() << "OutlineEditor::deleteAllOutlines: No root available";
        return false;
    }

    if (m_root->childCount() == 0) {
        qInfo() << "OutlineEditor::deleteAllOutlines: Already empty";
        return true;
    }

    qInfo() << "OutlineEditor::deleteAllOutlines: Deleting"
            << m_root->childCount() << "root items";

    while (m_root->childCount() > 0) {
        OutlineItem* child = m_root->child(0);
        m_root->removeChild(child);
        delete child;
    }

    setModified(true);
    emit outlineModified();

    qInfo() << "OutlineEditor::deleteAllOutlines: All outlines deleted";

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

    setModified(true);
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

    if (newPageIndex < -1) {
        qWarning() << "OutlineEditor: Invalid page index:" << newPageIndex;
        return false;
    }

    if (m_renderer && newPageIndex >= m_renderer->pageCount()) {
        qWarning() << "OutlineEditor: Page index out of range:" << newPageIndex;
        return false;
    }

    int oldPageIndex = item->pageIndex();

    if (oldPageIndex == newPageIndex) {
        return true;
    }

    item->setPageIndex(newPageIndex);

    setModified(true);
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

    OutlineItem* targetParent = newParent ? newParent : m_root;

    OutlineItem* p = targetParent;
    while (p) {
        if (p == item) {
            qWarning() << "OutlineEditor: Cannot move to descendant";
            return false;
        }
        p = p->parent();
    }

    OutlineItem* oldParent = item->parent();
    if (!oldParent) {
        qWarning() << "OutlineEditor: Item has no parent";
        return false;
    }

    oldParent->removeChild(item);

    if (newIndex < 0 || newIndex > targetParent->childCount())
        newIndex = targetParent->childCount();

    targetParent->insertChild(newIndex, item);

    setModified(true);
    emit outlineModified();

    qInfo() << "OutlineEditor: Moved outline successfully";
    return true;
}

namespace {
inline pdf_obj* create_doc_dict(fz_context* ctx, pdf_document* doc, int initial = 4)
{
    pdf_obj* dict = nullptr;
    fz_try(ctx) {
        dict = pdf_add_object(ctx, doc, pdf_new_dict(ctx, doc, initial));
    }
    fz_catch(ctx) {
        qWarning() << "create_doc_dict: error creating dict:" << fz_caught_message(ctx);
        return nullptr;
    }
    return dict;
}

bool validate_tree(OutlineItem* root, PerThreadMuPDFRenderer* renderer, QString* reason)
{
    if (!root) {
        if (reason) *reason = QCoreApplication::translate("OutlineEditor", "Root node is null");
        return false;
    }
    if (!renderer || !renderer->isDocumentLoaded()) {
        if (reason) *reason = QCoreApplication::translate("OutlineEditor", "No document loaded");
        return false;
    }

    int pageCount = renderer->pageCount();
    QList<OutlineItem*> stack;
    stack.append(root);

    while (!stack.isEmpty()) {
        OutlineItem* item = stack.takeLast();
        if (!item) continue;

        if (item != root) {
            if (item->title().trimmed().isEmpty()) {
                if (reason) *reason = QCoreApplication::translate("OutlineEditor", "Empty title found");
                return false;
            }
            int pg = item->pageIndex();
            if (pg < -1 || pg >= pageCount) {
                if (reason) *reason = QCoreApplication::translate("OutlineEditor", "Page index out of range: %1").arg(pg);
                return false;
            }
        }

        for (int i = item->childCount() - 1; i >= 0; --i) {
            OutlineItem* child = item->child(i);
            if (child) stack.append(child);
        }
    }

    return true;
}

pdf_obj* buildPdfOutlineRecursive(fz_context* ctx, pdf_document* doc,
                                  PerThreadMuPDFRenderer* renderer, OutlineItem* item)
{
    if (!ctx || !doc || !item || !renderer) return nullptr;

    pdf_obj* item_obj = nullptr;
    fz_try(ctx) {
        item_obj = create_doc_dict(ctx, doc, 8);
        if (!item_obj) fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to create item dict");

        pdf_dict_put(ctx, item_obj, PDF_NAME(Title), pdf_new_text_string(ctx, item->title().toUtf8().constData()));

        int pg = item->pageIndex();
        if (pg >= 0 && pg < renderer->pageCount()) {
            pdf_obj* pageObj = pdf_lookup_page_obj(ctx, doc, pg);
            if (pageObj) {
                pdf_obj* dest = pdf_new_array(ctx, doc, 5);

                // 目标页必须以“间接引用”(N G R) 写入 Dest：若把已解析的页字典
                // 直接 inline 进数组，宽容的 MuPDF 仍能跳，但严格的其它阅读器
                // 无法按对象引用匹配到正确的页 → 目录与页面错位。
                int num = pdf_to_num(ctx, pageObj);
                if (num > 0) {
                    pdf_array_push_drop(ctx, dest,
                        pdf_new_indirect(ctx, doc, num, pdf_to_gen(ctx, pageObj)));
                } else {
                    pdf_array_push(ctx, dest, pageObj);  // 兜底：拿不到对象号时维持原样
                }

                // /XYZ 的 top 取页面顶端（MediaBox 上边 y），落在页顶而非页底；
                // top=0 会被严格阅读器理解为页面底部，看着像跳到了下一页。
                float top = 0.0f;
                pdf_obj* mb = pdf_dict_get_inheritable(ctx, pageObj, PDF_NAME(MediaBox));
                if (mb) {
                    fz_rect r = pdf_to_rect(ctx, mb);
                    top = (r.y1 > r.y0) ? r.y1 : r.y0;
                }

                pdf_array_push(ctx, dest, PDF_NAME(XYZ));
                pdf_array_push_real(ctx, dest, 0);     // left
                pdf_array_push_real(ctx, dest, top);   // top（页顶）
                pdf_array_push_real(ctx, dest, 0);     // zoom：0 = 保持当前缩放

                pdf_dict_put(ctx, item_obj, PDF_NAME(Dest), dest);
                pdf_drop_obj(ctx, dest);
            }
        }

        QList<pdf_obj*> children;
        for (int i = 0; i < item->childCount(); ++i) {
            OutlineItem* child = item->child(i);
            if (!child || !child->isValid()) continue;

            pdf_obj* co = buildPdfOutlineRecursive(ctx, doc, renderer, child);
            if (!co) continue;

            pdf_dict_put(ctx, co, PDF_NAME(Parent), item_obj);
            children.append(co);
        }

        if (!children.isEmpty()) {
            pdf_dict_put(ctx, item_obj, PDF_NAME(First), children.first());
            pdf_dict_put(ctx, item_obj, PDF_NAME(Last), children.last());
            pdf_dict_put_int(ctx, item_obj, PDF_NAME(Count), children.size());

            for (int i = 0; i < children.size(); ++i) {
                if (i > 0) pdf_dict_put(ctx, children[i], PDF_NAME(Prev), children[i - 1]);
                if (i < children.size() - 1) pdf_dict_put(ctx, children[i], PDF_NAME(Next), children[i + 1]);
            }

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
}

bool OutlineEditor::saveToDocument(const QString& filePath)
{
    if (!m_renderer || !m_renderer->isDocumentLoaded()) {
        QString msg = QCoreApplication::translate("OutlineEditor", "No document loaded");
        qWarning() << "OutlineEditor:" << msg;
        emit saveCompleted(false, msg);
        return false;
    }
    if (!m_root) {
        QString msg = QCoreApplication::translate("OutlineEditor", "No outline data");
        qWarning() << "OutlineEditor:" << msg;
        emit saveCompleted(false, msg);
        return false;
    }

    QString reason;
    if (!validate_tree(m_root, m_renderer, &reason)) {
        qWarning() << "OutlineEditor: invalid outline tree:" << reason;
        emit saveCompleted(false, reason);
        return false;
    }

    fz_context* ctx = static_cast<fz_context*>(m_renderer->context());
    fz_document* fzdoc = static_cast<fz_document*>(m_renderer->document());
    if (!ctx || !fzdoc) {
        QString msg = QCoreApplication::translate("OutlineEditor", "Invalid MuPDF context or document");
        qWarning() << "OutlineEditor:" << msg;
        emit saveCompleted(false, msg);
        return false;
    }

    pdf_document* pdfDoc = pdf_document_from_fz_document(ctx, fzdoc);
    if (!pdfDoc) {
        QString msg = QCoreApplication::translate("OutlineEditor", "Document is not a PDF");
        qWarning() << "OutlineEditor:" << msg;
        emit saveCompleted(false, msg);
        return false;
    }

    QString savePath = filePath.isEmpty() ? m_renderer->documentPath() : filePath;
    if (savePath.isEmpty()) {
        QString msg = QCoreApplication::translate("OutlineEditor", "No file path specified");
        qWarning() << "OutlineEditor:" << msg;
        emit saveCompleted(false, msg);
        return false;
    }

    QString backupPath = createBackup(savePath);
    if (!backupPath.isEmpty()) qInfo() << "OutlineEditor: Backup created at:" << backupPath;

    bool success = false;
    QString errorMsg;

    fz_try(ctx) {
        pdf_obj* trailer = pdf_trailer(ctx, pdfDoc);
        pdf_obj* catalog_ref = pdf_dict_get(ctx, trailer, PDF_NAME(Root));
        pdf_obj* catalog = nullptr;
        if (catalog_ref) catalog = pdf_resolve_indirect(ctx, catalog_ref);

        if (!catalog || !pdf_is_dict(ctx, catalog)) {
            fz_throw(ctx, FZ_ERROR_GENERIC, "Invalid PDF catalog");
        }

        pdf_dict_del(ctx, catalog, PDF_NAME(Outlines));

        if (m_root->childCount() > 0) {
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

                    pdf_dict_put(ctx, item_obj, PDF_NAME(Parent), outlines);
                    topItems.append(item_obj);
                }

                if (!topItems.isEmpty()) {
                    pdf_dict_put(ctx, outlines, PDF_NAME(First), topItems.first());
                    pdf_dict_put(ctx, outlines, PDF_NAME(Last),  topItems.last());
                    pdf_dict_put_int(ctx, outlines, PDF_NAME(Count), topItems.size());

                    for (int i = 0; i < topItems.size(); ++i) {
                        if (i > 0) pdf_dict_put(ctx, topItems[i], PDF_NAME(Prev), topItems[i-1]);
                        if (i < topItems.size() - 1) pdf_dict_put(ctx, topItems[i], PDF_NAME(Next), topItems[i+1]);
                    }

                    for (pdf_obj* obj : topItems) pdf_drop_obj(ctx, obj);
                }

                pdf_dict_put(ctx, catalog, PDF_NAME(Outlines), outlines);
            }
            fz_always(ctx) {
                pdf_drop_obj(ctx, outlines);
            }
            fz_catch(ctx) {
                fz_rethrow(ctx);
            }
        }

        pdf_write_options opts = pdf_default_write_options;

        qInfo() << "OutlineEditor: about to save PDF to" << savePath;
        if (pdf_can_be_saved_incrementally(ctx, pdfDoc)) {
            // 正常文档：增量追加，最快且不动原有字节。
            opts.do_incremental = 1;
            opts.do_garbage = 0;
            pdf_save_document(ctx, pdfDoc, savePath.toUtf8().constData(), &opts);
        } else {
            // 被 MuPDF 修复过的文档（增量写出的 xref 偏移与原始字节对不上，
            // 别家阅读器会打不开）：完整重写到临时文件后原子替换原文件。
            opts.do_incremental = 0;
            opts.do_garbage = 1;
            opts.do_clean = 1;
            const QString tmpPath = savePath + QStringLiteral(".owlsave.tmp");
            QFile::remove(tmpPath);
            pdf_save_document(ctx, pdfDoc, tmpPath.toUtf8().constData(), &opts);
            // 原文件已在上面 createBackup() 备份，替换失败也可回滚。
            if (!QFile::remove(savePath) || !QFile::rename(tmpPath, savePath)) {
                QFile::remove(tmpPath);
                fz_throw(ctx, FZ_ERROR_GENERIC, "replace original file failed");
            }
        }

        success = true;
        setModified(false);
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

    item->setParent(nullptr);
    return true;
}

// 备份统一存放目录：AppDataLocation/backups（不再污染原 PDF 同级目录）
static QString backupDirPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/backups";
}

QString OutlineEditor::createBackup(const QString& filePath) const
{
    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        return QString();
    }

    QString dir = backupDirPath();
    if (!QDir().mkpath(dir)) {
        qWarning() << "OutlineEditor: Failed to create backup dir:" << dir;
        return QString();
    }

    QFileInfo fileInfo(filePath);
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    // 用 completeBaseName + suffix，避免 "a.b.pdf" 被错误拆分
    QString backupPath = dir + "/" +
                         fileInfo.completeBaseName() + "_backup_" + timestamp + "." +
                         fileInfo.suffix();

    if (QFile::copy(filePath, backupPath)) {
        return backupPath;
    }

    return QString();
}

void OutlineEditor::cleanupBackups()
{
    // 双保险：① 超过 kMaxAgeDays 天的删除；② 总体积超 kMaxTotalBytes 时从最旧删起
    const int   kMaxAgeDays    = 30;
    const qint64 kMaxTotalBytes = 500LL * 1024 * 1024;  // 500 MB

    QDir dir(backupDirPath());
    if (!dir.exists()) {
        return;
    }

    // QDir::Time：按修改时间排序，最新在前 → 最旧在末尾
    QFileInfoList files = dir.entryInfoList(
        QStringList() << "*_backup_*", QDir::Files, QDir::Time);

    const QDateTime now = QDateTime::currentDateTime();
    QFileInfoList remaining;
    for (const QFileInfo& fi : files) {
        if (fi.lastModified().daysTo(now) > kMaxAgeDays) {
            QFile::remove(fi.absoluteFilePath());
        } else {
            remaining.append(fi);
        }
    }

    qint64 total = 0;
    for (const QFileInfo& fi : remaining) {
        total += fi.size();
    }
    // 从最旧（末尾）开始删，直到总体积低于上限
    for (int i = remaining.size() - 1; i >= 0 && total > kMaxTotalBytes; --i) {
        total -= remaining[i].size();
        QFile::remove(remaining[i].absoluteFilePath());
    }
}
