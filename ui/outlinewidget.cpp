#include "outlinewidget.h"
#include "outlineeditor.h"
#include "outlineitem.h"
#include "outlinedialog.h"
#include "stylemanager.h"
#include "themedicon.h"

#include <QHeaderView>
#include <QContextMenuEvent>
#include <QMessageBox>
#include <QDrag>
#include <QMimeData>
#include <QDebug>
#include <QTimer>
#include <QFile>
#include <QPainter>
#include <QAbstractButton>
#include <QElapsedTimer>
#include <QStyledItemDelegate>
#include <QLineEdit>
#include <QPointer>


OutlineWidget::OutlineWidget(PDFContentHandler* contentHandler, QWidget* parent)
    : QTreeWidget(parent)
    , m_contentHandler(nullptr)
    , m_outlineEditor(nullptr)
    , m_currentHighlight(nullptr)
    , m_allExpanded(false)
    , m_editEnabled(true)
    , m_currentPageIndex(0)
    , m_draggedItem(nullptr)
{
    setupUI();


    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(false);
    setDragDropMode(QAbstractItemView::DragDrop);


    connect(this, &QTreeWidget::itemClicked,
            this, &OutlineWidget::onItemClicked);

    // contentHandler 可为空：面板单例化后由 setContentHandler 在切换文档时重新绑定
    setContentHandler(contentHandler);
}

OutlineWidget::~OutlineWidget()
{
}

void OutlineWidget::setContentHandler(PDFContentHandler* contentHandler)
{
    if (m_contentHandler == contentHandler) {
        return;
    }

    // 断开旧 contentHandler 的所有 →this 连接（含 outlineModified）
    if (m_contentHandler) {
        disconnect(m_contentHandler, nullptr, this, nullptr);
    }

    m_contentHandler = contentHandler;
    m_outlineEditor = contentHandler ? contentHandler->outlineEditor() : nullptr;

    // 清空旧文档的目录树
    QTreeWidget::clear();
    m_currentHighlight = nullptr;
    m_currentPageIndex = 0;

    if (m_contentHandler) {
        connect(m_contentHandler, &PDFContentHandler::outlineModified,
                this, &OutlineWidget::refreshTree);
    }
}

void OutlineWidget::setupUI()
{
    // objectName 供 outlinewidget.qss 选择器命中
    setObjectName("outlineWidget");

    setColumnCount(1);
    setHeaderHidden(true);


    setAlternatingRowColors(false);
    setAnimated(true);
    setIndentation(6);   // 每级缩进；paint 与命中判定均从 indentation() 取此值
    setIconSize(QSize(16, 16));
    setMouseTracking(true);
    // 双击用于“行内改名”，故不再让双击切换展开（展开仍由点击折叠三角触发）；
    // 编辑只允许显式入口（双击/“+”后），不开放其它自动触发，避免误进编辑态。
    setExpandsOnDoubleClick(false);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setUniformRowHeights(false);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setFrameShape(QFrame::NoFrame);
    // 轨道由 NavigationPanel 放在页签最外层，使其覆盖顶部工具栏。
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setContextMenuPolicy(Qt::DefaultContextMenu);

    // 隐藏默认分支箭头的样式已迁移到 outlinewidget.qss（铁律：页面不写 setStyleSheet）

    m_overlay = new DragOverlayWidget(viewport());
    m_overlay->resize(viewport()->size());
    m_overlay->show();


    m_itemDelegate = new OutlineItemDelegate(this, this);
    setItemDelegate(m_itemDelegate);
}

void OutlineWidget::mousePressEvent(QMouseEvent* event)
{
    QTreeWidgetItem* item = itemAt(event->pos());

    // 点击空白处（没有命中任何条目）即取消当前选中，避免误把后续“+”当成加子项
    if (!item) {
        clearSelection();
        setCurrentItem(nullptr);
        QTreeWidget::mousePressEvent(event);
        return;
    }

    if (item && item->childCount() > 0) {
        QRect rect = visualItemRect(item);
        int indent = indentation();


        int depth = 0;
        QTreeWidgetItem* parent = item->parent();
        while (parent) {
            depth++;
            parent = parent->parent();
        }


        int leftMargin = 8 + depth * indent;

        // 点击装饰列（折叠三角所在区域）即切换展开/折叠
        if (event->pos().x() >= leftMargin &&
            event->pos().x() < leftMargin + OutlineItemDelegate::kDecorationWidth) {
            item->setExpanded(!item->isExpanded());
            event->accept();
            viewport()->update();
            return;
        }
    }


    QTreeWidget::mousePressEvent(event);
}

void OutlineWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    // 双击标题即就地改名（不弹窗）；点空白处保持默认行为
    QTreeWidgetItem* item = itemAt(event->pos());
    if (m_editEnabled && item) {
        setCurrentItem(item);
        editItem(item, 0);
        event->accept();
        return;
    }
    QTreeWidget::mouseDoubleClickEvent(event);
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


    OutlineItem* root = m_contentHandler->outlineRoot();
    if (!root) {
        qWarning() << "OutlineWidget::loadOutline: No root available";
        return false;
    }

    if (root->childCount() == 0) {
        qInfo() << "OutlineWidget::loadOutline: Outline is empty (no items yet)";
        return true;
    }


    buildTree(root, nullptr);


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

void OutlineWidget::highlightCurrentPage(int pageIndex, bool ensureVisible)
{
    m_currentPageIndex = pageIndex;


    if (m_currentHighlight) {
        QFont font = m_currentHighlight->font(0);
        font.setBold(false);
        m_currentHighlight->setFont(0, font);
    }


    QTreeWidgetItem* item = findItemByPage(pageIndex);
    if (item) {
        QFont font = item->font(0);
        font.setBold(true);
        item->setFont(0, font);

        m_currentHighlight = item;

        if (ensureVisible) {
            expandToItem(item);
            scrollToItem(item, QAbstractItemView::PositionAtCenter);
        }
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

    if (item) {
        // 图标颜色随主题：用 text-secondary 作为菜单图标常态色
        const QColor iconColor = StyleManager::instance().getColor("textSecondary");

        QAction* editAction = menu->addAction(
            ThemedIcon::colored("edit", iconColor), tr("Edit"));
        connect(editAction, &QAction::triggered,
                this, &OutlineWidget::onEditOutline);

        QAction* addChildAction = menu->addAction(
            ThemedIcon::colored("add-child", iconColor), tr("Add Child"));
        connect(addChildAction, &QAction::triggered,
                this, &OutlineWidget::onAddChildOutline);

        QAction* addSiblingAction = menu->addAction(
            ThemedIcon::colored("add-sibling", iconColor), tr("Add Sibling"));
        connect(addSiblingAction, &QAction::triggered,
                this, &OutlineWidget::onAddSiblingOutline);

        menu->addSeparator();

        QAction* deleteAction = menu->addAction(
            ThemedIcon::colored("trash", iconColor), tr("Delete"));
        deleteAction->setShortcut(QKeySequence::Delete);
        connect(deleteAction, &QAction::triggered,
                this, &OutlineWidget::onDeleteOutline);
    } else {
        const QColor iconColor = StyleManager::instance().getColor("textSecondary");

        QAction* addAction = menu->addAction(
            ThemedIcon::colored("plus", iconColor), tr("Add Outline Item"));
        connect(addAction, &QAction::triggered,
                this, &OutlineWidget::onAddChildOutline);
    }

    menu->addSeparator();

    QAction* saveAction = menu->addAction(
        ThemedIcon::colored("save", StyleManager::instance().getColor("textSecondary")),
        tr("Save to PDF"));
    saveAction->setEnabled(m_outlineEditor && m_outlineEditor->hasUnsavedChanges());
    connect(saveAction, &QAction::triggered,
            this, &OutlineWidget::onSaveToDocument);

    if (topLevelItemCount() > 0) {
        menu->addSeparator();

        // 危险操作：图标用 error 色提示
        QAction* deleteAllAction = menu->addAction(
            ThemedIcon::colored("trash", StyleManager::instance().getColor("error")),
            tr("Delete All"));
        deleteAllAction->setToolTip(tr("Delete all outline items"));

        QFont font = deleteAllAction->font();
        font.setBold(true);
        deleteAllAction->setFont(font);

        connect(deleteAllAction, &QAction::triggered,
                this, &OutlineWidget::onDeleteAllOutlines);
    }

    return menu;
}

void OutlineWidget::onItemClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column);

    if (!item) {
        return;
    }


    QVariant pageVar = item->data(0, PageIndexRole);
    if (pageVar.isValid()) {
        int pageIndex = pageVar.toInt();
        if (pageIndex >= 0) {
            emit pageJumpRequested(pageIndex);
            return;
        }
    }


    QVariant uriVar = item->data(0, UriRole);
    if (uriVar.isValid()) {
        QString uri = uriVar.toString();
        if (!uri.isEmpty()) {
            emit externalLinkRequested(uri);
            return;
        }
    }
}

void OutlineWidget::addNewOutlineItem()
{
    if (!m_outlineEditor) {
        return;
    }

    // 页码取当前阅读页，并夹到合法范围内
    int pageIndex = m_currentPageIndex;
    int maxPage = m_contentHandler ? m_contentHandler->pageCount() : 1;
    if (pageIndex < 0) pageIndex = 0;
    if (pageIndex >= maxPage) pageIndex = maxPage - 1;

    // 选中某项→作为其子项；未选中→作为根项
    OutlineItem* parentItem = nullptr;
    QList<QTreeWidgetItem*> sel = selectedItems();
    if (!sel.isEmpty()) {
        parentItem = getOutlineItem(sel.first());
    } else {
        parentItem = m_contentHandler ? m_contentHandler->outlineRoot() : nullptr;
    }

    // addOutline 要求标题非空，先给占位名，随即进入行内改名让用户覆盖
    OutlineItem* newItem = m_outlineEditor->addOutline(
        parentItem, tr("New Outline"), pageIndex);
    if (!newItem) {
        return;
    }

    // addOutline 已同步触发 refreshTree 整树重建；按指针回找新建的树项，
    // 展开其父、选中、滚动到位，并立即开启行内编辑（占位名处于全选态）
    QTreeWidgetItem* treeItem = findItemByOutlineItem(newItem);
    if (treeItem) {
        if (treeItem->parent()) {
            treeItem->parent()->setExpanded(true);
        }
        setCurrentItem(treeItem);
        scrollToItem(treeItem);
        editItem(treeItem, 0);
    }
}

void OutlineWidget::onAddChildOutline()
{
    if (!m_outlineEditor) {
        return;
    }

    int maxPage = m_contentHandler ? m_contentHandler->pageCount() : 1;

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

        // 成功后不再弹窗打扰：未保存状态由 tab 标题标记与侧栏保存按钮表达
        if (newItem) {
            clearSelection();
            setCurrentItem(nullptr);
        } else {
            QMessageBox::warning(this, tr("Failed"),
                                 tr("Failed to add outline item!"));
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

    int maxPage = m_contentHandler ? m_contentHandler->pageCount() : 1;

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

        // 在选中项之后插入，作为它的同级兄弟
        int insertIndex = -1;
        if (parentItem && currentOutlineItem) {
            insertIndex = parentItem->indexOf(currentOutlineItem);
            if (insertIndex >= 0) {
                insertIndex += 1;
            }
        }

        OutlineItem* newItem = m_outlineEditor->addOutline(
            parentItem, title, pageIndex, insertIndex);

        if (!newItem) {
            QMessageBox::warning(this, tr("Failed"),
                                 tr("Failed to add outline item!"));
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
        QMessageBox::warning(this, tr("Hint"), tr("Please select an outline item to edit first!"));
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

    int maxPage = m_contentHandler ? m_contentHandler->pageCount() : 1;

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

    }
}

void OutlineWidget::onDeleteOutline()
{
    if (!m_outlineEditor) {
        return;
    }

    QList<QTreeWidgetItem*> selectedItems = this->selectedItems();

    if (selectedItems.isEmpty()) {
        QMessageBox::warning(this, tr("Hint"), tr("Please select an outline item to delete first!"));
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

    QString message = tr("Are you sure you want to delete outline item \"%1\"?").arg(title);
    if (childCount > 0) {
        message += tr("\n\nThis item contains %1 sub-items, which will also be deleted!").arg(childCount);
    }

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, tr("Confirm Delete"), message,
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        if (!m_outlineEditor->deleteOutline(outlineItem)) {
            QMessageBox::warning(this, tr("Failed"),
                                 tr("Failed to delete outline item!"));
        }
    }
}

void OutlineWidget::onDeleteAllOutlines()
{
    if (!m_outlineEditor) {
        QMessageBox::warning(this, tr("Error"),
                             tr("Outline editor not initialized!"));
        return;
    }


    if (topLevelItemCount() == 0) {
        return;
    }


    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("Confirm Delete"));
    msgBox.setText(tr("Are you sure you want to delete all outline items?"));
    msgBox.setInformativeText(tr("This will delete %1 outline items and all their sub-items, and cannot be undone!")
                                  .arg(m_contentHandler->outlineItemCount()));
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);


    msgBox.button(QMessageBox::Yes)->setText(tr("Delete"));
    msgBox.button(QMessageBox::No)->setText(tr("Cancel"));

    if (msgBox.exec() == QMessageBox::Yes) {

        bool success = m_outlineEditor->deleteAllOutlines();

        if (success) {

            clear();
            m_currentHighlight = nullptr;

            qInfo() << "OutlineWidget: All outlines deleted successfully";




        } else {
            QMessageBox::critical(this, tr("Error"),
                                  tr("Failed to delete all outline items!\nPlease check if locked or other errors."));
        }
    }
}

void OutlineWidget::onSaveToDocument()
{
    if (!m_outlineEditor) {
        return;
    }

    if (!m_outlineEditor->hasUnsavedChanges()) {
        return;
    }

    // 静默保存：成功不打扰（自动建备份），失败才弹错。
    // 备份目录化与定期清理见 OutlineEditor::createBackup / cleanupBackups。
    bool success = m_outlineEditor->saveToDocument();

    if (success) {
        if (m_contentHandler) {
            m_contentHandler->loadOutline();
        }
    } else {
        QMessageBox::critical(this, tr("Failed"),
                              tr("Save failed! Please check file permissions and disk space."));
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
        title = tr("[Untitled]");
    }


    if (outlineItem->pageIndex() >= 0) {
        QString pageNum = QString::number(outlineItem->pageIndex() + 1);
        title = QString("%1  •  %2").arg(title, pageNum);
    }

    item->setText(0, title);

    // 允许行内重命名（双击触发）；编辑器仅改标题，页码由 delegate 保留
    item->setFlags(item->flags() | Qt::ItemIsEditable);

    // 字号统一到 token：@font-size-sm（实际着色与字号由 delegate 统一控制，
    // 这里设置仅作为 fallback，不写死字面值）
    QFont font = item->font(0);
    font.setPixelSize(StyleManager::instance().currentConfig().fontSizeSm);
    item->setFont(0, font);

    item->setSizeHint(0, QSize(0, 28));


    if (outlineItem->pageIndex() >= 0) {
        item->setData(0, PageIndexRole, outlineItem->pageIndex());
        QString tooltip = tr("Page %1").arg(outlineItem->pageIndex() + 1);
        item->setToolTip(0, tooltip);
    }


    if (outlineItem->isExternalLink()) {
        item->setData(0, UriRole, outlineItem->uri());

        QFont linkFont = item->font(0);
        linkFont.setUnderline(true);
        item->setFont(0, linkFont);

        QString tooltip = tr("External link: %1").arg(outlineItem->uri());
        item->setToolTip(0, tooltip);
    }

    return item;
}

QTreeWidgetItem* OutlineWidget::findItemByPage(int pageIndex, QTreeWidgetItem* parent)
{
    Q_UNUSED(parent);
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

QTreeWidgetItem* OutlineWidget::findItemByOutlineItem(OutlineItem* outlineItem)
{
    if (!outlineItem) {
        return nullptr;
    }

    QTreeWidgetItemIterator it(this);
    while (*it) {
        if (getOutlineItem(*it) == outlineItem) {
            return *it;
        }
        ++it;
    }
    return nullptr;
}

QString OutlineWidget::inlineTitleForIndex(const QModelIndex& index) const
{
    QTreeWidgetItem* item = itemFromIndex(index);
    if (!item) {
        return QString();
    }
    // 回到数据源取“纯标题”（不含 "  •  页码" 后缀）；新建占位项也走这里
    QVariant data = item->data(0, OutlineItemRole);
    OutlineItem* oi = data.value<OutlineItem*>();
    return oi ? oi->title() : QString();
}

void OutlineWidget::commitInlineRename(const QModelIndex& index, const QString& newTitle)
{
    if (!m_outlineEditor) {
        return;
    }

    QTreeWidgetItem* item = itemFromIndex(index);
    if (!item) {
        return;
    }

    OutlineItem* oi = getOutlineItem(item);
    if (!oi) {
        return;
    }

    QString title = newTitle.trimmed();
    if (title.isEmpty() || title == oi->title()) {
        // 空标题不接受（OutlineEditor 也会拒绝）；未改动则跳过，避免无谓重建
        return;
    }

    // renameOutline 会触发 outlineModified→refreshTree（整树 clear 重建）。
    // 此刻仍在 delegate::setModelData 内、编辑器正关闭，若同步重建模型会令
    // 正在使用的索引/编辑器悬空 → 延后到下一拍执行。OutlineItem* 跨重建仍有效
    // （重建的只是 QTreeWidgetItem，数据源对象不动）；用 QPointer 防文档已关闭。
    QPointer<OutlineEditor> editor = m_outlineEditor;
    QMetaObject::invokeMethod(this, [editor, oi, title]() {
        if (editor) {
            editor->renameOutline(oi, title);
        }
    }, Qt::QueuedConnection);
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


    loadOutline();


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

    // 颜色由 delegate 统一从 token 取，这里不再写死 foreground；
    // 仅复位粗体与背景这类“状态”属性。
    item->setForeground(0, QBrush());

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


    if (item != m_lastHoverItem) {
        m_lastHoverItem = item;
        m_hoverTimer.restart();
    } else if (m_hoverTimer.isValid() && m_hoverTimer.elapsed() > 450) {
        if (item && !item->isExpanded()) {
            item->setExpanded(true);
        }
        m_hoverTimer.invalidate();
    }


    m_dropTargetItem = item;
    m_dropIndicator = DI_None;

    OutlineItem* targetOutline = item ? getOutlineItem(item) : nullptr;

    // 拖拽指示色统一取主题主色
    const QColor accent = StyleManager::instance().getColor("primary");
    QColor ghostFill = accent;
    ghostFill.setAlpha(40);


    if (!item) {
        OutlineItem* root = m_contentHandler->outlineRoot();
        Q_UNUSED(root);

        m_dropIndicator = DI_None;

        // overlay
        m_overlay->line.valid = false;
        m_overlay->ghost.valid = true;
        m_overlay->ghost.rect = QRect(0, viewport()->height() - 32, viewport()->width(), 28);
        m_overlay->ghost.text = m_draggedItem->text(0);
        m_overlay->ghost.color = ghostFill;

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
    Q_UNUSED(insertIndex);


    m_overlay->line.valid = false;
    m_overlay->ghost.valid = false;

    if (m_dropIndicator == DI_Above) {
        m_overlay->line.valid = true;
        m_overlay->line.lineRect =
            QRect(rect.left() + 8, rect.top(), rect.width() - 16, 2);
        m_overlay->line.color = accent;
    }
    else if (m_dropIndicator == DI_Below) {
        m_overlay->line.valid = true;
        m_overlay->line.lineRect =
            QRect(rect.left() + 8, rect.bottom() - 1, rect.width() - 16, 2);
        m_overlay->line.color = accent;
    }
    else if (m_dropIndicator == DI_Inside) {
        QRect r = rect.adjusted(6,3,-6,-3);

        QColor insideFill = accent;
        insideFill.setAlpha(50);

        m_overlay->ghost.valid = true;
        m_overlay->ghost.rect = r;
        m_overlay->ghost.text = m_draggedItem->text(0);
        m_overlay->ghost.color = insideFill;
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


    OutlineItem* p = newParent;
    while (p) {
        if (p == draggedOutline) {
            QMessageBox::warning(this, tr("Invalid Operation"),
                                 tr("Cannot move outline item to itself or its children!"));
            event->ignore();
            m_draggedItem = nullptr;
            m_dropTargetItem = nullptr;
            m_dropIndicator = DI_None;
            viewport()->update();
            return;
        }
        p = p->parent();
    }


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
        QMessageBox::warning(this, tr("Failed"), tr("Failed to move outline item!"));
    }


    m_draggedItem = nullptr;
    m_dropTargetItem = nullptr;
    m_dropIndicator = DI_None;
    m_overlay->ghost.valid = false;
    m_overlay->line.valid = false;
    m_overlay->update();
    viewport()->update();
}

// ===== OutlineItemDelegate：行内重命名编辑器 =====
// 这些方法在此定义而非头部内联，因为它们要调用 OutlineWidget 的接口
// （在头部 OutlineWidget 尚为不完整类型）。m_treeWidget 即对应的 OutlineWidget。

QWidget* OutlineItemDelegate::createEditor(QWidget* parent,
                                           const QStyleOptionViewItem& option,
                                           const QModelIndex& index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);
    QLineEdit* editor = new QLineEdit(parent);
    editor->setFrame(false);
    // 字号对齐正文（@font-size-sm），与 delegate 绘制保持一致
    QFont font = editor->font();
    font.setPixelSize(StyleManager::instance().currentConfig().fontSizeSm);
    editor->setFont(font);
    return editor;
}

void OutlineItemDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
    QLineEdit* le = qobject_cast<QLineEdit*>(editor);
    if (!le) {
        return;
    }
    OutlineWidget* ow = qobject_cast<OutlineWidget*>(m_treeWidget);
    QString title = ow ? ow->inlineTitleForIndex(index) : QString();
    le->setText(title);
    le->selectAll();  // 占位名/原标题整体选中，输入即覆盖
}

void OutlineItemDelegate::setModelData(QWidget* editor, QAbstractItemModel* model,
                                       const QModelIndex& index) const
{
    Q_UNUSED(model);
    QLineEdit* le = qobject_cast<QLineEdit*>(editor);
    if (!le) {
        return;
    }
    OutlineWidget* ow = qobject_cast<OutlineWidget*>(m_treeWidget);
    if (ow) {
        // 不直接写回 model 文本，交由 OutlineWidget 走 OutlineEditor::renameOutline，
        // 由 outlineModified→refreshTree 统一重建（标题+页码后缀）
        ow->commitInlineRename(index, le->text());
    }
}

void OutlineItemDelegate::updateEditorGeometry(QWidget* editor,
                                               const QStyleOptionViewItem& option,
                                               const QModelIndex& index) const
{
    // 让编辑框起点对齐 paint() 中的标题起点（跳过缩进+折叠三角装饰区）
    int leftMargin = 8;
    if (m_treeWidget) {
        int indent = m_treeWidget->indentation();
        int depth = 0;
        if (QTreeWidgetItem* item = m_treeWidget->itemFromIndex(index)) {
            for (QTreeWidgetItem* p = item->parent(); p; p = p->parent()) {
                ++depth;
            }
        }
        leftMargin = 8 + depth * indent + kDecorationWidth;
    }
    QRect rect = option.rect;
    rect.setLeft(leftMargin);
    editor->setGeometry(rect);
}

void OutlineWidget::dragLeaveEvent(QDragLeaveEvent* event)
{
    m_overlay->ghost.valid = false;
    m_overlay->line.valid = false;
    m_overlay->update();
    viewport()->update();
    QTreeWidget::dragLeaveEvent(event);
}
