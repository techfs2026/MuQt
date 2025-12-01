#include "outlinewidget.h"
#include "outlineeditor.h"
#include "outlineitem.h"
#include "outlinedialog.h"

#include <QHeaderView>
#include <QContextMenuEvent>
#include <QMessageBox>
#include <QDrag>
#include <QMimeData>
#include <QDebug>
#include <QTimer>
#include <QFile>
#include <QPainter>
#include <QElapsedTimer>
#include <QStyledItemDelegate>


OutlineWidget::OutlineWidget(PDFContentHandler* contentHandler, QWidget* parent)
    : QTreeWidget(parent)
    , m_contentHandler(contentHandler)
    , m_outlineEditor(contentHandler->outlineEditor())
    , m_currentHighlight(nullptr)
    , m_allExpanded(false)
    , m_editEnabled(true)
    , m_currentPageIndex(0)
    , m_draggedItem(nullptr)
{
    setupUI();

    // 启用拖拽
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(false);
    setDragDropMode(QAbstractItemView::DragDrop);


    connect(this, &QTreeWidget::itemClicked,
            this, &OutlineWidget::onItemClicked);
    connect(m_contentHandler, &PDFContentHandler::outlineModified,
            this, &OutlineWidget::refreshTree);
}

OutlineWidget::~OutlineWidget()
{
}

void OutlineWidget::setupUI()
{
    // 设置列
    setColumnCount(1);
    setHeaderHidden(true);

    // 设置样式和行为
    setAlternatingRowColors(false);
    setAnimated(true);
    setIndentation(20);  // 确保缩进足够显示图标
    setIconSize(QSize(16, 16));
    setMouseTracking(true);
    setExpandsOnDoubleClick(true); // 保留双击展开作为备用
    setUniformRowHeights(false);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setFrameShape(QFrame::NoFrame);
    setContextMenuPolicy(Qt::DefaultContextMenu);

    setStyleSheet("QTreeView::branch { image: none; }");

    m_overlay = new DragOverlayWidget(viewport());
    m_overlay->resize(viewport()->size());
    m_overlay->show();

    // 设置自定义代理 - 传递 this 指针
    m_itemDelegate = new OutlineItemDelegate(this, this);
    setItemDelegate(m_itemDelegate);
}

void OutlineWidget::mousePressEvent(QMouseEvent* event)
{
    QTreeWidgetItem* item = itemAt(event->pos());
    if (item && item->childCount() > 0) {
        QRect rect = visualItemRect(item);
        int indent = indentation();

        // 计算项目的深度
        int depth = 0;
        QTreeWidgetItem* parent = item->parent();
        while (parent) {
            depth++;
            parent = parent->parent();
        }

        // 图标的 X 坐标范围（与代理中的绘制位置一致）
        int leftMargin = 8 + depth * indent;
        int iconX = leftMargin + 4;
        int iconWidth = 20; // 点击区域宽度

        // 如果点击在图标区域，切换展开状态
        if (event->pos().x() >= iconX &&
            event->pos().x() <= iconX + iconWidth) {
            item->setExpanded(!item->isExpanded());
            event->accept();
            viewport()->update(); // 强制重绘以更新三角形
            return;
        }
    }

    // 否则调用基类处理（选中项目等）
    QTreeWidget::mousePressEvent(event);
}


void OutlineWidget::resizeEvent(QResizeEvent* event)
{
    QTreeWidget::resizeEvent(event);
    if (m_overlay)
        m_overlay->resize(viewport()->size());
}

bool OutlineWidget::loadOutline()
{
    clear();

    if (!m_contentHandler) {
        return false;
    }

    // 通过 PDFContentHandler 获取大纲根节点
    OutlineItem* root = m_contentHandler->outlineRoot();
    if (!root) {
        qWarning() << "OutlineWidget::loadOutline: No root available";
        return false;
    }

    if (root->childCount() == 0) {
        qInfo() << "OutlineWidget::loadOutline: Outline is empty (no items yet)";
        return true;  // 返回 true，表示结构正常，只是没内容
    }

    // 递归构建树
    buildTree(root, nullptr);

    // 默认展开第一层
    expandToDepth(0);

    clearSelection();
    setCurrentItem(nullptr);

    qInfo() << "OutlineWidget: Loaded" << m_contentHandler->outlineItemCount()
            << "outline items";

    return true;
}

void OutlineWidget::clear()
{
    QTreeWidget::clear();
    m_currentHighlight = nullptr;
    m_allExpanded = false;
}

void OutlineWidget::highlightCurrentPage(int pageIndex)
{
    m_currentPageIndex = pageIndex;

    // 清除之前的高亮
    if (m_currentHighlight) {
        QFont font = m_currentHighlight->font(0);
        font.setBold(false);
        m_currentHighlight->setFont(0, font);
    }

    // 查找并高亮新项
    QTreeWidgetItem* item = findItemByPage(pageIndex);
    if (item) {
        QFont font = item->font(0);
        font.setBold(true);
        item->setFont(0, font);

        m_currentHighlight = item;

        expandToItem(item);
        scrollToItem(item, QAbstractItemView::PositionAtCenter);
    } else {
        m_currentHighlight = nullptr;
    }

    viewport()->update();
}

void OutlineWidget::expandAll()
{
    QTreeWidget::expandAll();
    m_allExpanded = true;
}

void OutlineWidget::collapseAll()
{
    QTreeWidget::collapseAll();
    m_allExpanded = false;
}

void OutlineWidget::toggleExpandAll()
{
    if (m_allExpanded) {
        collapseAll();
    } else {
        expandAll();
    }
}

void OutlineWidget::contextMenuEvent(QContextMenuEvent* event)
{
    if (!m_editEnabled) {
        return;
    }

    QTreeWidgetItem* item = itemAt(event->pos());
    QMenu* menu = createContextMenu(item);

    if (menu) {
        menu->exec(event->globalPos());
        delete menu;
    }
}

QMenu* OutlineWidget::createContextMenu(QTreeWidgetItem* item)
{
    QMenu* menu = new QMenu(this);

    // PDF Expert 风格的菜单样式
    menu->setStyleSheet(R"(
        QMenu {
            background-color: #FFFFFF;
            border: 1px solid #D1D1D6;
            border-radius: 8px;
            padding: 6px;
        }

        QMenu::item {
            padding: 8px 32px 8px 16px;
            border-radius: 5px;
            color: #1C1C1E;
            font-size: 13px;
        }

        QMenu::item:selected {
            background-color: #007AFF;
            color: #FFFFFF;
        }

        QMenu::separator {
            height: 1px;
            background-color: #E8E8E8;
            margin: 6px 12px;
        }
    )");

    if (item) {
        // 编辑选中的大纲项
        QAction* editAction = menu->addAction(tr("✏️  编辑"));
        connect(editAction, &QAction::triggered,
                this, &OutlineWidget::onEditOutline);

        QAction* addChildAction = menu->addAction(tr("➕  添加子项"));
        connect(addChildAction, &QAction::triggered,
                this, &OutlineWidget::onAddChildOutline);

        QAction* addSiblingAction = menu->addAction(tr("➕  添加同级项"));
        connect(addSiblingAction, &QAction::triggered,
                this, &OutlineWidget::onAddSiblingOutline);

        menu->addSeparator();

        QAction* deleteAction = menu->addAction(tr("🗑️  删除"));
        deleteAction->setShortcut(QKeySequence::Delete);
        connect(deleteAction, &QAction::triggered,
                this, &OutlineWidget::onDeleteOutline);
    } else {
        // 在根层级添加大纲
        QAction* addAction = menu->addAction(tr("➕  添加目录项"));
        connect(addAction, &QAction::triggered,
                this, &OutlineWidget::onAddChildOutline);
    }

    menu->addSeparator();

    QAction* saveAction = menu->addAction(tr("💾  保存到PDF"));
    saveAction->setEnabled(m_outlineEditor && m_outlineEditor->hasUnsavedChanges());
    connect(saveAction, &QAction::triggered,
            this, &OutlineWidget::onSaveToDocument);

    return menu;
}

void OutlineWidget::onItemClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column);

    if (!item) {
        return;
    }

    // 获取页码
    QVariant pageVar = item->data(0, PageIndexRole);
    if (pageVar.isValid()) {
        int pageIndex = pageVar.toInt();
        if (pageIndex >= 0) {
            emit pageJumpRequested(pageIndex);
            return;
        }
    }

    // 获取外部链接
    QVariant uriVar = item->data(0, UriRole);
    if (uriVar.isValid()) {
        QString uri = uriVar.toString();
        if (!uri.isEmpty()) {
            emit externalLinkRequested(uri);
            return;
        }
    }
}

void OutlineWidget::onAddChildOutline()
{
    if (!m_outlineEditor) {
        return;
    }

    int maxPage = 100;
    if (m_contentHandler && m_contentHandler->outlineRoot()) {
        maxPage = 100;
    }

    OutlineDialog dialog(OutlineDialog::AddMode, maxPage, this);
    dialog.setPageIndex(m_currentPageIndex);

    if (dialog.exec() == QDialog::Accepted) {
        QString title = dialog.title();
        int pageIndex = dialog.pageIndex();

        QList<QTreeWidgetItem*> selectedItems = this->selectedItems();
        OutlineItem* parentItem = nullptr;

        if (!selectedItems.isEmpty()) {
            QTreeWidgetItem* selectedItem = selectedItems.first();
            parentItem = getOutlineItem(selectedItem);
        } else {
            parentItem = m_contentHandler ? m_contentHandler->outlineRoot() : nullptr;
        }

        OutlineItem* newItem = m_outlineEditor->addOutline(
            parentItem, title, pageIndex);

        if (newItem) {
            clearSelection();
            setCurrentItem(nullptr);

            QMessageBox::information(this, tr("成功"),
                                     tr("目录项已添加!\n记得保存到PDF文档。"));
        } else {
            QMessageBox::warning(this, tr("失败"),
                                 tr("添加目录项失败!"));
        }
    }
}

void OutlineWidget::onAddSiblingOutline()
{
    if (!m_outlineEditor) {
        return;
    }

    QList<QTreeWidgetItem*> selectedItems = this->selectedItems();

    if (selectedItems.isEmpty()) {
        onAddChildOutline();
        return;
    }

    QTreeWidgetItem* selectedItem = selectedItems.first();
    if (!selectedItem) {
        onAddChildOutline();
        return;
    }

    int maxPage = 100;

    OutlineDialog dialog(OutlineDialog::AddMode, maxPage, this);
    dialog.setPageIndex(m_currentPageIndex);

    if (dialog.exec() == QDialog::Accepted) {
        QString title = dialog.title();
        int pageIndex = dialog.pageIndex();

        OutlineItem* currentOutlineItem = getOutlineItem(selectedItem);
        OutlineItem* parentItem = currentOutlineItem ?
                                      currentOutlineItem->parent() : nullptr;

        if (!parentItem && m_contentHandler) {
            parentItem = m_contentHandler->outlineRoot();
        }

        OutlineItem* newItem = m_outlineEditor->addOutline(
            parentItem, title, pageIndex);

        if (newItem) {
            QMessageBox::information(this, tr("成功"),
                                     tr("目录项已添加!\n记得保存到PDF文档。"));
        } else {
            QMessageBox::warning(this, tr("失败"),
                                 tr("添加目录项失败!"));
        }
    }
}

void OutlineWidget::onEditOutline()
{
    if (!m_outlineEditor) {
        return;
    }
    QList<QTreeWidgetItem*> selectedItems = this->selectedItems();

    if (selectedItems.isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请先选择要编辑的目录项!"));
        return;
    }

    QTreeWidgetItem* item = selectedItems.first();
    if (!item) {
        return;
    }

    OutlineItem* outlineItem = getOutlineItem(item);
    if (!outlineItem) {
        return;
    }

    int maxPage = 100;

    OutlineDialog dialog(OutlineDialog::EditMode, maxPage, this);
    dialog.setTitle(outlineItem->title());
    dialog.setPageIndex(outlineItem->pageIndex());

    if (dialog.exec() == QDialog::Accepted) {
        QString newTitle = dialog.title();
        int newPageIndex = dialog.pageIndex();

        bool titleChanged = (newTitle != outlineItem->title());
        bool pageChanged = (newPageIndex != outlineItem->pageIndex());

        if (titleChanged) {
            m_outlineEditor->renameOutline(outlineItem, newTitle);
        }

        if (pageChanged) {
            m_outlineEditor->updatePageIndex(outlineItem, newPageIndex);
        }

        if (titleChanged || pageChanged) {
            QMessageBox::information(this, tr("成功"),
                                     tr("目录项已修改!\n记得保存到PDF文档。"));
        }
    }
}

void OutlineWidget::onDeleteOutline()
{
    if (!m_outlineEditor) {
        return;
    }

    QList<QTreeWidgetItem*> selectedItems = this->selectedItems();

    if (selectedItems.isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请先选择要删除的目录项!"));
        return;
    }

    QTreeWidgetItem* item = selectedItems.first();
    if (!item) {
        return;
    }

    OutlineItem* outlineItem = getOutlineItem(item);
    if (!outlineItem) {
        return;
    }

    QString title = outlineItem->title();
    int childCount = outlineItem->childCount();

    QString message = tr("确定要删除目录项 \"%1\" 吗?").arg(title);
    if (childCount > 0) {
        message += tr("\n\n此目录项包含 %1 个子项,将一起删除!").arg(childCount);
    }

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, tr("确认删除"), message,
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        if (m_outlineEditor->deleteOutline(outlineItem)) {
            QMessageBox::information(this, tr("成功"),
                                     tr("目录项已删除!\n记得保存到PDF文档。"));
        } else {
            QMessageBox::warning(this, tr("失败"),
                                 tr("删除目录项失败!"));
        }
    }
}

void OutlineWidget::onSaveToDocument()
{
    if (!m_outlineEditor) {
        return;
    }

    if (!m_outlineEditor->hasUnsavedChanges()) {
        QMessageBox::information(this, tr("提示"),
                                 tr("没有未保存的修改!"));
        return;
    }

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, tr("保存确认"),
        tr("确定要将目录修改保存到PDF文档吗?\n\n"
           "建议在保存前备份原文件!"),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        bool success = m_outlineEditor->saveToDocument();

        if (success) {
            QMessageBox::information(this, tr("成功"),
                                     tr("目录已成功保存到PDF文档!"));

            // 保存后重新加载大纲（通过 PDFContentHandler）
            if (m_contentHandler) {
                m_contentHandler->loadOutline();
            }
        } else {
            QMessageBox::critical(this, tr("失败"),
                                  tr("保存失败!请检查文件权限和磁盘空间。"));
        }
    }
}
void OutlineWidget::buildTree(OutlineItem* outlineItem, QTreeWidgetItem* treeItem)
{
    if (!outlineItem) {
        return;
    }

    for (int i = 0; i < outlineItem->childCount(); ++i) {
        OutlineItem* child = outlineItem->child(i);
        if (!child || !child->isValid()) {
            continue;
        }

        QTreeWidgetItem* childTreeItem = createTreeItem(child);
        setOutlineItem(childTreeItem, child);

        if (treeItem) {
            treeItem->addChild(childTreeItem);
        } else {
            addTopLevelItem(childTreeItem);
        }

        if (child->childCount() > 0) {
            buildTree(child, childTreeItem);
        }
    }
}

QTreeWidgetItem* OutlineWidget::createTreeItem(OutlineItem* outlineItem)
{
    QTreeWidgetItem* item = new QTreeWidgetItem();

    QString title = outlineItem->title();
    if (title.isEmpty()) {
        title = tr("[无标题]");
    }

    // 页码以更优雅的方式显示 - 保持原格式，让代理来分离和右对齐
    if (outlineItem->pageIndex() >= 0) {
        QString pageNum = QString::number(outlineItem->pageIndex() + 1);
        title = QString("%1  •  %2").arg(title, pageNum);
    }

    item->setText(0, title);

    // 字体设置 - 缩小字号
    QFont font = item->font(0);
    font.setPointSize(10);
    item->setFont(0, font);

    item->setSizeHint(0, QSize(0, 28)); // 减小行高

    // 设置页码数据
    if (outlineItem->pageIndex() >= 0) {
        item->setData(0, PageIndexRole, outlineItem->pageIndex());
        QString tooltip = tr("第 %1 页").arg(outlineItem->pageIndex() + 1);
        item->setToolTip(0, tooltip);
    }

    // 外部链接样式
    if (outlineItem->isExternalLink()) {
        item->setData(0, UriRole, outlineItem->uri());

        QFont linkFont = item->font(0);
        linkFont.setUnderline(true);
        item->setFont(0, linkFont);

        QString tooltip = tr("外部链接: %1").arg(outlineItem->uri());
        item->setToolTip(0, tooltip);
    }

    return item;
}

QTreeWidgetItem* OutlineWidget::findItemByPage(int pageIndex, QTreeWidgetItem* parent)
{
    QTreeWidgetItemIterator it(this);

    while (*it) {
        QTreeWidgetItem* item = *it;
        QVariant pageVar = item->data(0, PageIndexRole);

        if (pageVar.isValid() && pageVar.toInt() == pageIndex) {
            return item;
        }

        ++it;
    }

    return nullptr;
}

void OutlineWidget::expandToItem(QTreeWidgetItem* item)
{
    if (!item) {
        return;
    }

    QTreeWidgetItem* parent = item->parent();
    while (parent) {
        parent->setExpanded(true);
        parent = parent->parent();
    }
}

OutlineItem* OutlineWidget::getOutlineItem(QTreeWidgetItem* treeItem)
{
    if (!treeItem) {
        return nullptr;
    }

    QVariant data = treeItem->data(0, OutlineItemRole);
    return data.value<OutlineItem*>();
}

void OutlineWidget::setOutlineItem(QTreeWidgetItem* treeItem, OutlineItem* outlineItem)
{
    if (treeItem && outlineItem) {
        treeItem->setData(0, OutlineItemRole, QVariant::fromValue(outlineItem));
    }
}

void OutlineWidget::refreshTree()
{
    // 保存展开状态
    QSet<int> expandedPages;
    QTreeWidgetItemIterator it(this);
    while (*it) {
        if ((*it)->isExpanded()) {
            QVariant pageVar = (*it)->data(0, PageIndexRole);
            if (pageVar.isValid()) {
                expandedPages.insert(pageVar.toInt());
            }
        }
        ++it;
    }

    // 重新加载
    loadOutline();

    // 恢复展开状态
    QTreeWidgetItemIterator it2(this);
    while (*it2) {
        QVariant pageVar = (*it2)->data(0, PageIndexRole);
        if (pageVar.isValid() && expandedPages.contains(pageVar.toInt())) {
            (*it2)->setExpanded(true);
        }
        ++it2;
    }

    clearSelection();
    setCurrentItem(nullptr);
}

int OutlineWidget::getCurrentPageIndex() const
{
    return m_currentPageIndex;
}

void OutlineWidget::setItemDefaultColor(QTreeWidgetItem* item)
{
    if (!item) {
        return;
    }

    item->setForeground(0, QBrush(QColor("#007AFF")));

    QFont font = item->font(0);
    font.setBold(false);
    item->setFont(0, font);
    item->setBackground(0, QBrush());
}

void OutlineWidget::startDrag(Qt::DropActions supportedActions)
{
    Q_UNUSED(supportedActions);

    if (!m_editEnabled) {
        return;
    }

    m_draggedItem = currentItem();
    if (!m_draggedItem) {
        return;
    }

    OutlineItem* item = getOutlineItem(m_draggedItem);
    if (!item) {
        m_draggedItem = nullptr;
        return;
    }

    qDebug() << "Start dragging:" << item->title();

    QDrag* drag = new QDrag(this);
    QMimeData* mimeData = new QMimeData();

    mimeData->setData("application/x-outline-drag", QByteArray("1"));
    mimeData->setText(m_draggedItem->text(0));

    drag->setMimeData(mimeData);

    Qt::DropAction result = drag->exec(Qt::MoveAction);

    if (result != Qt::MoveAction) {
        qDebug() << "Drag cancelled";
        m_draggedItem = nullptr;
    }
}

void OutlineWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (!m_editEnabled) {
        event->ignore();
        return;
    }

    if (event->mimeData()->hasFormat("application/x-outline-drag")) {
        event->acceptProposedAction();
        qDebug() << "Drag enter accepted";
    } else {
        event->ignore();
        qDebug() << "Drag enter rejected";
    }
}

void OutlineWidget::dragMoveEvent(QDragMoveEvent* event)
{
    if (!m_editEnabled || !event->mimeData()->hasFormat("application/x-outline-drag")) {
        event->ignore();
        return;
    }

    QPoint pos = event->pos();
    QTreeWidgetItem* item = itemAt(pos);

    // 自动展开 hover
    if (item != m_lastHoverItem) {
        m_lastHoverItem = item;
        m_hoverTimer.restart();
    } else if (m_hoverTimer.isValid() && m_hoverTimer.elapsed() > 450) {
        if (item && !item->isExpanded()) {
            item->setExpanded(true);
        }
        m_hoverTimer.invalidate();
    }

    // 计算 drop indicator
    m_dropTargetItem = item;
    m_dropIndicator = DI_None;

    OutlineItem* targetOutline = item ? getOutlineItem(item) : nullptr;

    // 空白区域
    if (!item) {
        OutlineItem* root = m_contentHandler->outlineRoot();

        m_dropIndicator = DI_None;

        // overlay
        m_overlay->line.valid = false;
        m_overlay->ghost.valid = true;
        m_overlay->ghost.rect = QRect(0, viewport()->height() - 32, viewport()->width(), 28);
        m_overlay->ghost.text = m_draggedItem->text(0);
        m_overlay->ghost.color = QColor(0,122,255,40);

        m_overlay->update();
        event->acceptProposedAction();
        return;
    }

    // Above / Below / Inside
    QRect rect = visualItemRect(item);
    int yMid = rect.center().y();
    const int tol = 5;

    if (pos.y() < yMid - tol)
        m_dropIndicator = DI_Above;
    else if (pos.y() > yMid + tol)
        m_dropIndicator = DI_Below;
    else
        m_dropIndicator = DI_Inside;

    // 计算 new parent / insertIndex
    OutlineItem* newParent = nullptr;
    int insertIndex = -1;

    if (m_dropIndicator == DI_Inside) {
        newParent = targetOutline;
        insertIndex = targetOutline->childCount();
    }
    else {
        OutlineItem* parent = targetOutline->parent();
        newParent = parent ? parent : m_contentHandler->outlineRoot();

        int targetIndex = newParent->indexOf(targetOutline);
        if (targetIndex < 0) targetIndex = newParent->childCount();

        insertIndex = (m_dropIndicator == DI_Above) ? targetIndex : targetIndex + 1;
    }

    // 更新 overlay
    m_overlay->line.valid = false;
    m_overlay->ghost.valid = false;

    if (m_dropIndicator == DI_Above) {
        m_overlay->line.valid = true;
        m_overlay->line.lineRect =
            QRect(rect.left() + 8, rect.top(), rect.width() - 16, 2);
        m_overlay->line.color = QColor("#007AFF");
    }
    else if (m_dropIndicator == DI_Below) {
        m_overlay->line.valid = true;
        m_overlay->line.lineRect =
            QRect(rect.left() + 8, rect.bottom() - 1, rect.width() - 16, 2);
        m_overlay->line.color = QColor("#007AFF");
    }
    else if (m_dropIndicator == DI_Inside) {
        QRect r = rect.adjusted(6,3,-6,-3);

        m_overlay->ghost.valid = true;
        m_overlay->ghost.rect = r;
        m_overlay->ghost.text = m_draggedItem->text(0);
        m_overlay->ghost.color = QColor(0,122,255,50);
    }

    m_overlay->update();
    event->acceptProposedAction();
}

void OutlineWidget::dropEvent(QDropEvent* event)
{
    if (!m_editEnabled || !m_outlineEditor) {
        event->ignore();
        qWarning() << "Drop rejected - editing disabled or no editor";
        m_draggedItem = nullptr;
        m_dropTargetItem = nullptr;
        m_dropIndicator = DI_None;
        viewport()->update();
        return;
    }

    if (!event->mimeData()->hasFormat("application/x-outline-drag")) {
        event->ignore();
        qWarning() << "Drop rejected - wrong format";
        m_draggedItem = nullptr;
        m_dropTargetItem = nullptr;
        m_dropIndicator = DI_None;
        viewport()->update();
        return;
    }

    if (!m_draggedItem) {
        event->ignore();
        qWarning() << "Drop rejected - no dragged item";
        m_dropTargetItem = nullptr;
        m_dropIndicator = DI_None;
        viewport()->update();
        return;
    }

    OutlineItem* draggedOutline = getOutlineItem(m_draggedItem);
    if (!draggedOutline) {
        event->ignore();
        qWarning() << "Drop rejected - invalid outline item";
        m_draggedItem = nullptr;
        m_dropTargetItem = nullptr;
        m_dropIndicator = DI_None;
        viewport()->update();
        return;
    }

    // 决定 newParent 与插入索引
    QTreeWidgetItem* targetItem = m_dropTargetItem;
    OutlineItem* targetOutline = targetItem ? getOutlineItem(targetItem) : nullptr;

    OutlineItem* newParent = nullptr;
    int insertIndex = -1;

    if (m_dropIndicator == DI_Inside) {
        newParent = targetOutline ? targetOutline : (m_contentHandler ? m_contentHandler->outlineRoot(): nullptr);
        if (newParent) {
            insertIndex = newParent->childCount();
        }
    } else if (m_dropIndicator == DI_Above || m_dropIndicator == DI_Below) {
        OutlineItem* parentOfTarget = targetOutline ? targetOutline->parent() : nullptr;
        newParent = parentOfTarget ? parentOfTarget : (m_contentHandler ? m_contentHandler->outlineRoot(): nullptr);

        if (newParent && targetOutline) {
            int targetIndex = -1;
            for (int i = 0; i < newParent->childCount(); ++i) {
                if (newParent->child(i) == targetOutline) {
                    targetIndex = i;
                    break;
                }
            }
            if (targetIndex < 0) {
                insertIndex = newParent->childCount();
            } else {
                insertIndex = (m_dropIndicator == DI_Above) ? targetIndex : (targetIndex + 1);
            }
        } else {
            insertIndex = newParent ? newParent->childCount() : -1;
        }
    } else {
        newParent = m_contentHandler ? m_contentHandler->outlineRoot() : nullptr;
        insertIndex = newParent ? newParent->childCount() : -1;
    }

    if (!newParent) {
        event->ignore();
        qWarning() << "Drop rejected - no valid parent";
        m_draggedItem = nullptr;
        m_dropTargetItem = nullptr;
        m_dropIndicator = DI_None;
        viewport()->update();
        return;
    }

    // 防止移动到自身或子项
    OutlineItem* p = newParent;
    while (p) {
        if (p == draggedOutline) {
            QMessageBox::warning(this, tr("无效操作"),
                                 tr("不能将目录项移动到自己或自己的子项下!"));
            event->ignore();
            m_draggedItem = nullptr;
            m_dropTargetItem = nullptr;
            m_dropIndicator = DI_None;
            viewport()->update();
            return;
        }
        p = p->parent();
    }

    // 调整索引
    OutlineItem* oldParent = draggedOutline->parent();
    int oldIndex = -1;
    if (oldParent) {
        for (int i = 0; i < oldParent->childCount(); ++i) {
            if (oldParent->child(i) == draggedOutline) {
                oldIndex = i;
                break;
            }
        }
    }

    if (oldParent == newParent && oldIndex >= 0 && insertIndex >= 0) {
        if (oldIndex < insertIndex) {
            insertIndex -= 1;
            if (insertIndex < 0) insertIndex = 0;
        }
    }

    // 移动操作
    bool ok = false;
    ok = m_outlineEditor->moveOutline(draggedOutline, newParent, insertIndex);

    if (!ok) {
        qWarning() << "moveOutline(parent,index) failed or not available, falling back to moveOutline(item,parent)";
        ok = m_outlineEditor->moveOutline(draggedOutline, newParent);
    }

    if (ok) {
        event->acceptProposedAction();
    } else {
        event->ignore();
        QMessageBox::warning(this, tr("失败"), tr("移动目录项失败!"));
    }

    // 清理状态
    m_draggedItem = nullptr;
    m_dropTargetItem = nullptr;
    m_dropIndicator = DI_None;
    m_overlay->ghost.valid = false;
    m_overlay->line.valid = false;
    m_overlay->update();
    viewport()->update();
}

void OutlineWidget::dragLeaveEvent(QDragLeaveEvent* event)
{
    m_overlay->ghost.valid = false;
    m_overlay->line.valid = false;
    m_overlay->update();
    viewport()->update();
    QTreeWidget::dragLeaveEvent(event);
}
