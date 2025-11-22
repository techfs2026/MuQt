#ifndef OUTLINEWIDGET_H
#define OUTLINEWIDGET_H

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QMenu>
#include <QPainter>
#include <QElapsedTimer>
#include <QStyledItemDelegate>

#include "pdfcontenthandler.h"

class OutlineItem;
class OutlineEditor;

enum LocalDropIndicator {
    DI_None,
    DI_Above,
    DI_Below,
    DI_Inside
};

/**
 * @brief 拖拽覆盖层组件 - PDF Expert 风格
 */
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
            font.setPointSize(10);
            p.setFont(font);
            p.drawText(ghost.rect.adjusted(16,0,0,0),
                       Qt::AlignVCenter, ghost.text);
        }
    }
};

/**
 * @brief 自定义代理，用于绘制展开/折叠图标和自定义布局
 */
class OutlineItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit OutlineItemDelegate(QTreeWidget* treeWidget, QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
        , m_treeWidget(treeWidget)
        , m_darkMode(false)
    {}

    void setDarkMode(bool dark) { m_darkMode = dark; }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        painter->save();

        // 绘制背景（选中、悬停等状态）
        if (option.state & QStyle::State_Selected) {
            painter->fillRect(option.rect, m_darkMode ? QColor("#0A4B7F") : QColor("#E3F2FD"));
        } else if (option.state & QStyle::State_MouseOver) {
            painter->fillRect(option.rect, m_darkMode ? QColor("#2C2C2E") : QColor("#F2F2F7"));
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

        // 计算深度和缩进
        int indent = m_treeWidget->indentation();
        int depth = 0;
        QTreeWidgetItem* parent = item->parent();
        while (parent) {
            depth++;
            parent = parent->parent();
        }

        int leftMargin = 8 + depth * indent;

        // 如果有子项，绘制展开/折叠三角形
        if (item->childCount() > 0) {
            painter->setRenderHint(QPainter::Antialiasing);

            // 三角形颜色 - 更明显
            QColor iconColor = m_darkMode ? QColor("#AEAEB2") : QColor("#8E8E93");
            if (option.state & QStyle::State_MouseOver) {
                iconColor = m_darkMode ? QColor("#0A84FF") : QColor("#007AFF");
            }

            painter->setPen(Qt::NoPen);
            painter->setBrush(iconColor);

            int triangleX = leftMargin + 4;
            int triangleY = option.rect.center().y();

            QPolygonF triangle;
            if (item->isExpanded()) {
                // 向下的三角形 ▼ - 稍大一些
                triangle << QPointF(triangleX, triangleY - 2)
                         << QPointF(triangleX + 10, triangleY - 2)
                         << QPointF(triangleX + 5, triangleY + 4);
            } else {
                // 向右的三角形 ▶ - 稍大一些
                triangle << QPointF(triangleX, triangleY - 5)
                         << QPointF(triangleX + 7, triangleY)
                         << QPointF(triangleX, triangleY + 5);
            }

            painter->drawPolygon(triangle);
            leftMargin += 20; // 为三角形留出空间
        } else {
            leftMargin += 20; // 保持对齐
        }

        // 获取文本和页码
        QString fullText = item->text(0);
        QString title = fullText;
        QString pageNum;

        // 分离标题和页码
        int separatorPos = fullText.indexOf("  •  ");
        if (separatorPos > 0) {
            title = fullText.left(separatorPos);
            pageNum = fullText.mid(separatorPos + 5);
        }

        // 设置字体
        QFont font = item->font(0);
        font.setPointSize(10); // 缩小字体
        painter->setFont(font);

        // 设置文字颜色
        QColor textColor;
        if (option.state & QStyle::State_Selected) {
            textColor = m_darkMode ? QColor("#0A84FF") : QColor("#007AFF");
        } else {
            // 检查是否有页码链接
            QVariant pageVar = item->data(0, Qt::UserRole + 1);
            if (pageVar.isValid()) {
                textColor = m_darkMode ? QColor("#0A84FF") : QColor("#007AFF");
            } else {
                textColor = m_darkMode ? QColor("#EBEBF5") : QColor("#1C1C1E");
            }
        }
        painter->setPen(textColor);

        // 计算可用宽度
        int rightMargin = 8;
        int pageNumWidth = 0;

        if (!pageNum.isEmpty()) {
            QFontMetrics fm(font);
            pageNumWidth = fm.horizontalAdvance(pageNum) + 16; // 页码宽度 + 间距
        }

        // 绘制标题（左对齐）
        QRect titleRect = option.rect.adjusted(leftMargin, 0, -pageNumWidth - rightMargin, 0);
        painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, title);

        // 绘制页码（右对齐，颜色稍浅）
        if (!pageNum.isEmpty()) {
            QColor pageColor = textColor;
            pageColor.setAlpha(180); // 稍微透明
            painter->setPen(pageColor);

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
        if (size.height() < 28) { // 稍微减小行高
            size.setHeight(28);
        }
        return size;
    }

private:
    QTreeWidget* m_treeWidget;
    bool m_darkMode;
};

/**
 * @brief PDF大纲树形视图组件 (PDF Expert风格)
 */
class OutlineWidget : public QTreeWidget
{
    Q_OBJECT

public:
    explicit OutlineWidget(PDFContentHandler* contentHandler, QWidget* parent = nullptr);
    ~OutlineWidget();

    void setOutlineEditor(OutlineEditor* editor);
    bool loadOutline();
    void clear();
    void highlightCurrentPage(int pageIndex);
    void setDarkMode(bool dark);
    bool isDarkMode() const { return m_darkMode; }
    void setEditEnabled(bool enabled) { m_editEnabled = enabled; }
    bool isEditEnabled() const { return m_editEnabled; }
    void expandAll();
    void collapseAll();
    void toggleExpandAll();

signals:
    void pageJumpRequested(int pageIndex);
    void externalLinkRequested(const QString& uri);
    void outlineModified();

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void startDrag(Qt::DropActions supportedActions) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private slots:
    void onItemClicked(QTreeWidgetItem* item, int column);
    void onAddChildOutline();
    void onAddSiblingOutline();
    void onEditOutline();
    void onDeleteOutline();
    void onSaveToDocument();

private:
    void setupUI();
    void applyStyleSheet();
    QMenu* createContextMenu(QTreeWidgetItem* item);
    void buildTree(OutlineItem* outlineItem, QTreeWidgetItem* treeItem);
    QTreeWidgetItem* createTreeItem(OutlineItem* outlineItem);
    QTreeWidgetItem* findItemByPage(int pageIndex, QTreeWidgetItem* parent = nullptr);
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
    bool m_darkMode;
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
