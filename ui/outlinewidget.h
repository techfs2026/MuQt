#ifndef OUTLINEWIDGET_H
#define OUTLINEWIDGET_H

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QMenu>
#include <QPainter>
#include <QElapsedTimer>
#include <QStyledItemDelegate>

#include "pdfcontenthandler.h"
#include "stylemanager.h"

class OutlineItem;
class OutlineEditor;

enum LocalDropIndicator {
    DI_None,
    DI_Above,
    DI_Below,
    DI_Inside
};

// 自绘区域的配色统一从 StyleManager 的 token 取，不写死颜色，也不再维护
// 独立的 m_darkMode 开关 —— StyleManager 已按当前主题返回对应 token，
// 暗色/亮色自动跟随，与 ThemedIcon 的取色方式一致。
class DragOverlayWidget : public QWidget {
public:
    DragOverlayWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_AlwaysStackOnTop);
    }

    struct GhostInfo {
        bool valid = false;
        QRect rect;
        QString text;
        QColor color;
    };

    struct LineInfo {
        bool valid = false;
        QRect lineRect;
        QColor color;
    };

    GhostInfo ghost;
    LineInfo line;

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        if (line.valid) {
            p.setPen(QPen(line.color, 2.5, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(line.lineRect.left(), line.lineRect.top(),
                       line.lineRect.right(), line.lineRect.bottom());

            p.setBrush(line.color);
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(line.lineRect.left(), line.lineRect.top()), 4, 4);
            p.drawEllipse(QPointF(line.lineRect.right(), line.lineRect.bottom()), 4, 4);
        }

        if (ghost.valid) {
            p.setBrush(ghost.color);
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(ghost.rect, 6, 6);

            p.setPen(QColor(ghost.color).lighter(130));
            QFont font = p.font();
            // 字号统一到 token：@font-size-sm
            font.setPixelSize(StyleManager::instance().currentConfig().fontSizeSm);
            p.setFont(font);
            p.drawText(ghost.rect.adjusted(16,0,0,0),
                       Qt::AlignVCenter, ghost.text);
        }
    }
};


class OutlineItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit OutlineItemDelegate(QTreeWidget* treeWidget, QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
        , m_treeWidget(treeWidget)
    {}

    // 折叠三角所占装饰列宽度（三角之后即正文起点）。
    // paint() 与 OutlineWidget::mousePressEvent() 的命中判定共用此值。
    static constexpr int kDecorationWidth = 16;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        StyleManager& sm = StyleManager::instance();

        painter->save();

        // 选中/hover 底色由 delegate 整行通染：从视口左缘(x=0)画到行右端，
        // 盖住左侧缩进/装饰区，避免 Qt 原生高亮从缝隙透出。
        // （原生 ::item:selected / ::branch:selected 背景已在 qss 置为
        //   transparent，二者配合确保底色唯一来源是这里。）
        QRect fillRect = option.rect;
        fillRect.setLeft(0);

        if (option.state & QStyle::State_Selected) {
            painter->fillRect(fillRect, sm.getColor("selected"));
        } else if (option.state & QStyle::State_MouseOver) {
            painter->fillRect(fillRect, sm.getColor("hover"));
        }

        if (!m_treeWidget) {
            painter->restore();
            return;
        }

        QTreeWidgetItem* item = m_treeWidget->itemFromIndex(index);
        if (!item) {
            painter->restore();
            return;
        }

        int indent = m_treeWidget->indentation();
        int depth = 0;
        QTreeWidgetItem* parent = item->parent();
        while (parent) {
            depth++;
            parent = parent->parent();
        }

        int leftMargin = 8 + depth * indent;

        if (item->childCount() > 0) {
            painter->setRenderHint(QPainter::Antialiasing);

            // 折叠三角：常态 = text-secondary，hover = primary
            QColor iconColor = sm.getColor("textSecondary");
            if (option.state & QStyle::State_MouseOver) {
                iconColor = sm.getColor("primary");
            }

            painter->setPen(Qt::NoPen);
            painter->setBrush(iconColor);

            int triangleX = leftMargin + 4;
            int triangleY = option.rect.center().y();

            QPolygonF triangle;
            if (item->isExpanded()) {
                triangle << QPointF(triangleX, triangleY - 2)
                << QPointF(triangleX + 10, triangleY - 2)
                << QPointF(triangleX + 5, triangleY + 4);
            } else {
                triangle << QPointF(triangleX, triangleY - 5)
                << QPointF(triangleX + 7, triangleY)
                << QPointF(triangleX, triangleY + 5);
            }

            painter->drawPolygon(triangle);
            leftMargin += kDecorationWidth;
        } else {
            leftMargin += kDecorationWidth;
        }

        QString fullText = item->text(0);
        QString title = fullText;
        QString pageNum;

        int separatorPos = fullText.indexOf("  •  ");
        if (separatorPos > 0) {
            title = fullText.left(separatorPos);
            pageNum = fullText.mid(separatorPos + 5);
        }

        QFont font = item->font(0);
        // 字号统一到 token：@font-size-sm
        font.setPixelSize(sm.currentConfig().fontSizeSm);
        painter->setFont(font);

        // 文字颜色：选中态 = primary；外部链接 = primary；普通条目 = text-primary。
        // （此前普通条目只要带页码就染主色，导致整树皆蓝，现按语义收敛。）
        QColor textColor;
        if (option.state & QStyle::State_Selected) {
            textColor = sm.getColor("primary");
        } else {
            QVariant uriVar = item->data(0, UriRole);
            bool isExternalLink = uriVar.isValid() && !uriVar.toString().isEmpty();
            textColor = isExternalLink ? sm.getColor("primary")
                                       : sm.getColor("textPrimary");
        }
        painter->setPen(textColor);

        int rightMargin = 8;
        int pageNumWidth = 0;

        if (!pageNum.isEmpty()) {
            QFontMetrics fm(font);
            pageNumWidth = fm.horizontalAdvance(pageNum) + 16;
        }

        QRect titleRect = option.rect.adjusted(leftMargin, 0, -pageNumWidth - rightMargin, 0);
        painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, title);

        if (!pageNum.isEmpty()) {
            // 页码用次要文字色，弱化于标题
            painter->setPen(sm.getColor("textSecondary"));

            QRect pageRect = option.rect.adjusted(option.rect.width() - pageNumWidth - rightMargin,
                                                  0, -rightMargin, 0);
            painter->drawText(pageRect, Qt::AlignRight | Qt::AlignVCenter, pageNum);
        }

        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        if (size.height() < 28) {
            size.setHeight(28);
        }
        return size;
    }

    // 行内重命名：editor 仅编辑标题（不含页码后缀），提交后交由
    // OutlineWidget::commitInlineRename 走 OutlineEditor::renameOutline。
    // 这些方法体定义在 .cpp 中（此处 OutlineWidget 尚为不完整类型）。
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(QWidget* editor, QAbstractItemModel* model,
                      const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const override;

private:
    // UriRole 与 OutlineWidget 中的定义保持一致（Qt::UserRole + 2）
    static constexpr int UriRole = Qt::UserRole + 2;

    QTreeWidget* m_treeWidget;
};

class OutlineWidget : public QTreeWidget
{
    Q_OBJECT

public:
    explicit OutlineWidget(PDFContentHandler* contentHandler, QWidget* parent = nullptr);
    ~OutlineWidget();

    void setContentHandler(PDFContentHandler* contentHandler);
    bool loadOutline();
    void clear();
    // 非活动页签只更新高亮，不接管用户正在浏览的滚动位置。
    void highlightCurrentPage(int pageIndex, bool ensureVisible = false);
    void setEditEnabled(bool enabled) { m_editEnabled = enabled; }
    bool isEditEnabled() const { return m_editEnabled; }
    void expandAll();
    void collapseAll();
    void toggleExpandAll();

    // 行内重命名支持：供 OutlineItemDelegate 回调（m_treeWidget 即本控件）
    QString inlineTitleForIndex(const QModelIndex& index) const;
    void commitInlineRename(const QModelIndex& index, const QString& newTitle);

public slots:
    // 工具栏“+”：选中某项→建其子项，未选中→建根项；页码取当前阅读页，
    // 新建后立即进入行内改名，全程不弹窗。
    void addNewOutlineItem();
    // 工具栏“删除选中”/“删除全部”（均带确认弹窗），与右键菜单共用同一逻辑。
    void onDeleteOutline();
    void onDeleteAllOutlines();
    // 工具栏“保存”：静默存盘（自动建备份），失败才弹错；与右键菜单共用同一逻辑。
    void onSaveToDocument();

signals:
    void pageJumpRequested(int pageIndex);
    void externalLinkRequested(const QString& uri);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void startDrag(Qt::DropActions supportedActions) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private slots:
    void onItemClicked(QTreeWidgetItem* item, int column);
    void onAddChildOutline();
    void onAddSiblingOutline();
    void onEditOutline();

private:
    void setupUI();
    QMenu* createContextMenu(QTreeWidgetItem* item);
    void buildTree(OutlineItem* outlineItem, QTreeWidgetItem* treeItem);
    QTreeWidgetItem* createTreeItem(OutlineItem* outlineItem);
    QTreeWidgetItem* findItemByPage(int pageIndex, QTreeWidgetItem* parent = nullptr);
    QTreeWidgetItem* findItemByOutlineItem(OutlineItem* outlineItem);
    void expandToItem(QTreeWidgetItem* item);
    OutlineItem* getOutlineItem(QTreeWidgetItem* treeItem);
    void setOutlineItem(QTreeWidgetItem* treeItem, OutlineItem* outlineItem);
    void refreshTree();
    int getCurrentPageIndex() const;
    void setItemDefaultColor(QTreeWidgetItem* item);

private:
    PDFContentHandler* m_contentHandler;
    OutlineEditor* m_outlineEditor;
    QTreeWidgetItem* m_currentHighlight;
    bool m_allExpanded;
    bool m_editEnabled;
    int m_currentPageIndex;

    QTreeWidgetItem* m_draggedItem;
    QTreeWidgetItem* m_dropTargetItem;
    LocalDropIndicator m_dropIndicator;

    QElapsedTimer m_hoverTimer;
    QTreeWidgetItem* m_lastHoverItem;

    DragOverlayWidget* m_overlay;
    OutlineItemDelegate* m_itemDelegate;

    static constexpr int PageIndexRole = Qt::UserRole + 1;
    static constexpr int UriRole = Qt::UserRole + 2;
    static constexpr int OutlineItemRole = Qt::UserRole + 3;
};

#endif // OUTLINEWIDGET_H
