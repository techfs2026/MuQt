#include "annotationwidget.h"
#include "annotationmanager.h"
#include "pdfannotationhandler.h"
#include "themedicon.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedLayout>
#include <QListWidget>
#include <QScrollBar>
#include <QLabel>
#include <QFrame>
#include <QToolButton>
#include <QButtonGroup>
#include <QAbstractButton>
#include <QPen>
#include <QMenu>
#include <QColorDialog>
#include <QPixmap>
#include <QPainter>
#include <QIcon>
#include <QSignalBlocker>

namespace {
constexpr int kIconSize = 18;
}

AnnotationWidget::AnnotationWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    updateColorSwatch();
    updateActionState();
    refresh();
}

QScrollBar* AnnotationWidget::listScrollBar() const
{
    return m_list ? m_list->verticalScrollBar() : nullptr;
}

QToolButton* AnnotationWidget::createIconButton(const QString& iconName,
                                                const QString& objectName,
                                                const QString& tooltip,
                                                bool checkable)
{
    QToolButton* button = new QToolButton(this);
    button->setObjectName(objectName);
    button->setIcon(ThemedIcon::toolButton(iconName, kIconSize));
    button->setIconSize(QSize(kIconSize, kIconSize));
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(tooltip);
    button->setCheckable(checkable);
    return button;
}

QIcon AnnotationWidget::dotIcon(qreal dotPx, const QColor& color, bool hollow)
{
    QPixmap pm(kIconSize, kIconSize);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal d = qBound<qreal>(3.0, dotPx, kIconSize - 2.0);
        const QRectF r((kIconSize - d) / 2.0, (kIconSize - d) / 2.0, d, d);
        if (hollow) {
            p.setPen(QPen(color, 1.5));
            p.setBrush(Qt::NoBrush);
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(color);
        }
        p.drawEllipse(r);
    }
    return QIcon(pm);
}

QToolButton* AnnotationWidget::addSwatchButton(QButtonGroup* group, QHBoxLayout* row,
                                               qreal value, qreal dotPx,
                                               const QString& tooltip, bool hollow)
{
    QToolButton* button = new QToolButton(this);
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(tooltip);
    button->setIconSize(QSize(kIconSize, kIconSize));
    button->setProperty("annotValue", value);
    button->setProperty("annotDotPx", dotPx);
    // 钢笔圆点用当前笔色（随选色重绘），橡皮用空心灰环表示范围。
    button->setIcon(dotIcon(dotPx, hollow ? QColor(120, 120, 120) : m_penColor, hollow));
    group->addButton(button);
    row->addWidget(button);
    return button;
}

qreal AnnotationWidget::currentPenWidth() const
{
    QAbstractButton* b = m_penWidthGroup->checkedButton();
    return b ? b->property("annotValue").toReal() : 2.0;
}

qreal AnnotationWidget::currentEraserRadius() const
{
    QAbstractButton* b = m_eraserSizeGroup->checkedButton();
    return b ? b->property("annotValue").toReal() : 12.0;
}

void AnnotationWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // —— 顶部控制条：固定 3 行，钢笔配置 / 橡皮配置 / 动作 分行，互不混淆 ——
    QWidget* bar = new QWidget(this);
    bar->setObjectName("annotationToolbar");
    bar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    QVBoxLayout* rows = new QVBoxLayout(bar);
    rows->setContentsMargins(10, 8, 10, 8);
    rows->setSpacing(6);

    // 控件
    m_penButton = createIconButton("pen", "annotPenButton", tr("Pen"), true);
    m_eraserButton = createIconButton("eraser", "annotEraserButton", tr("Eraser"), true);

    m_colorButton = new QToolButton(bar);
    m_colorButton->setObjectName("annotColorButton");
    m_colorButton->setAutoRaise(true);
    m_colorButton->setFocusPolicy(Qt::NoFocus);
    m_colorButton->setCursor(Qt::PointingHandCursor);
    m_colorButton->setToolTip(tr("Pen color"));
    m_colorButton->setIconSize(QSize(kIconSize, kIconSize));

    // 钢笔粗细：平铺单选（圆点直观表示粗细），互斥
    m_penWidthGroup = new QButtonGroup(this);
    m_penWidthGroup->setExclusive(true);

    // 橡皮大小：平铺单选（空心圆点表示橡皮范围），互斥
    m_eraserSizeGroup = new QButtonGroup(this);
    m_eraserSizeGroup->setExclusive(true);

    m_undoButton = createIconButton("undo", "annotUndoButton", tr("Undo (Ctrl+Z)"));
    m_redoButton = createIconButton("redo", "annotRedoButton", tr("Redo (Ctrl+Y)"));

    m_clearButton = createIconButton("trash", "annotClearButton", tr("Clear"));
    m_clearButton->setPopupMode(QToolButton::InstantPopup);
    QMenu* clearMenu = new QMenu(this);
    m_clearPageItem = clearMenu->addAction(tr("Clear current page"));
    m_clearAllItem  = clearMenu->addAction(tr("Clear all pages"));
    m_clearButton->setMenu(clearMenu);

    // 第 1 行：钢笔 + 颜色 + 粗细（平铺）
    QHBoxLayout* penRow = new QHBoxLayout();
    penRow->setSpacing(6);
    penRow->addWidget(m_penButton);
    penRow->addWidget(m_colorButton);
    penRow->addSpacing(6);
    addSwatchButton(m_penWidthGroup, penRow, 1.0,  5.0,  tr("Fine"),   false);
    QToolButton* defaultPen =
        addSwatchButton(m_penWidthGroup, penRow, 2.0,  8.0,  tr("Thin"),   false);
    addSwatchButton(m_penWidthGroup, penRow, 3.5,  12.0, tr("Medium"), false);
    addSwatchButton(m_penWidthGroup, penRow, 6.0,  16.0, tr("Thick"),  false);
    defaultPen->setChecked(true);
    penRow->addStretch();
    rows->addLayout(penRow);

    auto addSeparator = [this, rows]() {
        QFrame* line = new QFrame(this);
        line->setObjectName("annotRowSeparator");
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Plain);
        line->setFixedHeight(1);
        rows->addWidget(line);
    };

    addSeparator();

    // 第 2 行：橡皮 + 大小（平铺）
    QHBoxLayout* eraserRow = new QHBoxLayout();
    eraserRow->setSpacing(6);
    eraserRow->addWidget(m_eraserButton);
    eraserRow->addSpacing(6);
    addSwatchButton(m_eraserSizeGroup, eraserRow, 6.0,  7.0,  tr("Small"),  true);
    QToolButton* defaultEraser =
        addSwatchButton(m_eraserSizeGroup, eraserRow, 12.0, 11.0, tr("Medium"), true);
    addSwatchButton(m_eraserSizeGroup, eraserRow, 24.0, 16.0, tr("Large"),  true);
    defaultEraser->setChecked(true);
    eraserRow->addStretch();
    rows->addLayout(eraserRow);

    addSeparator();

    // 第 3 行：撤销 / 重做 / 清空
    QHBoxLayout* actionRow = new QHBoxLayout();
    actionRow->setSpacing(6);
    actionRow->addWidget(m_undoButton);
    actionRow->addWidget(m_redoButton);
    actionRow->addWidget(m_clearButton);
    actionRow->addStretch();
    rows->addLayout(actionRow);

    mainLayout->addWidget(bar);

    // —— 页列表 ——
    m_emptyHint = new QLabel(tr("No annotations yet"), this);
    m_emptyHint->setObjectName("annotationEmptyHint");
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setWordWrap(true);

    m_list = new QListWidget(this);
    m_list->setObjectName("annotationListWidget");
    m_list->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // 滚动条由 NavigationPanel 放在页签最外层，使轨道覆盖顶部工具栏。
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget* emptyPage = new QWidget(this);
    QHBoxLayout* emptyLayout = new QHBoxLayout(emptyPage);
    emptyLayout->setContentsMargins(0, 0, 0, 0);
    emptyLayout->setSpacing(0);
    emptyLayout->addWidget(m_emptyHint, 1);

    m_contentStack = new QStackedLayout();
    m_contentStack->setContentsMargins(0, 0, 0, 0);
    m_contentStack->setStackingMode(QStackedLayout::StackOne);
    m_contentStack->addWidget(emptyPage);
    m_contentStack->addWidget(m_list);
    mainLayout->addLayout(m_contentStack, 1);

    // —— 连接 ——
    connect(m_penButton, &QToolButton::toggled, this, &AnnotationWidget::onPenToggled);
    connect(m_eraserButton, &QToolButton::toggled, this, &AnnotationWidget::onEraserToggled);
    connect(m_penWidthGroup, &QButtonGroup::buttonToggled, this,
            [this](QAbstractButton* button, bool checked) {
                if (checked && m_handler)
                    m_handler->setPenWidth(button->property("annotValue").toReal());
            });
    connect(m_colorButton, &QToolButton::clicked, this, &AnnotationWidget::choosePenColor);
    connect(m_eraserSizeGroup, &QButtonGroup::buttonToggled, this,
            [this](QAbstractButton* button, bool checked) {
                if (checked && m_handler)
                    m_handler->setEraserRadius(button->property("annotValue").toReal());
            });
    connect(m_undoButton, &QToolButton::clicked, this, [this]() {
        if (m_manager) m_manager->undo();
    });
    connect(m_redoButton, &QToolButton::clicked, this, [this]() {
        if (m_manager) m_manager->redo();
    });
    connect(m_clearPageItem, &QAction::triggered, this, &AnnotationWidget::clearCurrentPage);
    connect(m_clearAllItem, &QAction::triggered, this, &AnnotationWidget::clearAllPages);

    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        if (item) emit pageJumpRequested(item->data(Qt::UserRole).toInt());
    });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (item) emit pageJumpRequested(item->data(Qt::UserRole).toInt());
    });
}

void AnnotationWidget::setManager(AnnotationManager* manager)
{
    if (m_manager == manager) {
        return;
    }
    if (m_managerUndoConn) disconnect(m_managerUndoConn);
    if (m_managerChangeConn) disconnect(m_managerChangeConn);

    m_manager = manager;

    if (m_manager) {
        m_managerUndoConn = connect(m_manager, &AnnotationManager::undoStateChanged,
                                    this, &AnnotationWidget::updateActionState);
        m_managerChangeConn = connect(m_manager, &AnnotationManager::annotationsChanged,
                                      this, [this](int) { refresh(); updateActionState(); });
    }
    refresh();
    updateActionState();
}

void AnnotationWidget::setHandler(PDFAnnotationHandler* handler)
{
    m_handler = handler;
    // 把当前控件状态（工具/配置）施加到新文档的 handler —— 全局工具的核心。
    applyConfigToHandler();
}

void AnnotationWidget::setCurrentPage(int pageIndex)
{
    m_currentPage = pageIndex;
    updateActionState();
}

void AnnotationWidget::activatePen()
{
    if (!m_penButton->isChecked()) {
        m_penButton->setChecked(true);   // 触发 onPenToggled → setTool(Pen)
    } else {
        applyConfigToHandler();
    }
}

void AnnotationWidget::applyConfigToHandler()
{
    if (!m_handler) {
        return;
    }
    m_handler->setPenColor(m_penColor);
    m_handler->setPenWidth(currentPenWidth());
    m_handler->setEraserRadius(currentEraserRadius());

    AnnotTool tool = m_penButton->isChecked()    ? AnnotTool::Pen
                     : m_eraserButton->isChecked() ? AnnotTool::Eraser
                                                   : AnnotTool::None;
    m_handler->setTool(tool);
}

void AnnotationWidget::onPenToggled(bool checked)
{
    if (checked) {
        QSignalBlocker be(m_eraserButton);
        m_eraserButton->setChecked(false);
    }
    if (m_handler) {
        AnnotTool tool = checked ? AnnotTool::Pen
                                 : (m_eraserButton->isChecked() ? AnnotTool::Eraser : AnnotTool::None);
        m_handler->setTool(tool);
    }
}

void AnnotationWidget::onEraserToggled(bool checked)
{
    if (checked) {
        QSignalBlocker bp(m_penButton);
        m_penButton->setChecked(false);
    }
    if (m_handler) {
        AnnotTool tool = checked ? AnnotTool::Eraser
                                 : (m_penButton->isChecked() ? AnnotTool::Pen : AnnotTool::None);
        m_handler->setTool(tool);
    }
}

void AnnotationWidget::choosePenColor()
{
    QColor c = QColorDialog::getColor(m_penColor, this, tr("Pen Color"));
    if (!c.isValid()) {
        return;
    }
    m_penColor = c;
    updateColorSwatch();
    if (m_handler) {
        m_handler->setPenColor(m_penColor);
    }
    if (!m_penButton->isChecked()) {
        m_penButton->setChecked(true);   // 选色通常意味着想继续画
    }
}

void AnnotationWidget::clearCurrentPage()
{
    if (m_manager) {
        m_manager->clearPage(m_currentPage);
    }
}

void AnnotationWidget::clearAllPages()
{
    if (m_manager) {
        m_manager->clearAll();
    }
}

void AnnotationWidget::updateActionState()
{
    const bool hasManager = m_manager != nullptr;
    m_undoButton->setEnabled(hasManager && m_manager->canUndo());
    m_redoButton->setEnabled(hasManager && m_manager->canRedo());
    m_clearButton->setEnabled(hasManager && !m_manager->isEmpty());
    if (m_clearPageItem) {
        m_clearPageItem->setEnabled(hasManager && m_manager->strokeCountForPage(m_currentPage) > 0);
    }
    if (m_clearAllItem) {
        m_clearAllItem->setEnabled(hasManager && !m_manager->isEmpty());
    }
}

void AnnotationWidget::updateColorSwatch()
{
    QPixmap pm(kIconSize, kIconSize);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(0, 0, 0, 60), 1));
        p.setBrush(m_penColor);
        p.drawEllipse(1, 1, kIconSize - 3, kIconSize - 3);
    }
    m_colorButton->setIcon(QIcon(pm));

    // 钢笔粗细圆点跟随笔色重绘
    if (m_penWidthGroup) {
        const auto buttons = m_penWidthGroup->buttons();
        for (QAbstractButton* b : buttons) {
            b->setIcon(dotIcon(b->property("annotDotPx").toReal(), m_penColor, false));
        }
    }
}

void AnnotationWidget::refresh()
{
    m_list->clear();

    if (!m_manager) {
        m_contentStack->setCurrentIndex(0);
        emit listEmptyChanged(true);
        return;
    }

    const QList<int> pages = m_manager->pagesWithAnnotations();
    for (int page : pages) {
        const int count = m_manager->strokeCountForPage(page);
        QListWidgetItem* item = new QListWidgetItem(
            tr("Page %1  ·  %n stroke(s)", nullptr, count).arg(page + 1), m_list);
        item->setData(Qt::UserRole, page);
    }

    const bool empty = pages.isEmpty();
    m_contentStack->setCurrentIndex(empty ? 0 : 1);
    emit listEmptyChanged(empty);
}
