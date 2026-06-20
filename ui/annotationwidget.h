#ifndef ANNOTATIONWIDGET_H
#define ANNOTATIONWIDGET_H

#include <QWidget>
#include <QColor>

class AnnotationManager;
class PDFAnnotationHandler;
class QListWidget;
class QLabel;
class QToolButton;
class QAbstractButton;
class QButtonGroup;
class QHBoxLayout;
class QAction;
class QIcon;

// 导航栏「批注」Tab：顶部控制条（钢笔/橡皮/粗细/颜色/橡皮大小/撤销/重做/清空）
// + 下方有批注的页列表（点击跳页）。
//
// 该控件是 NavigationPanel 的单例子控件，状态常驻：切换 PDF 时只重绑数据源
// （setManager/setHandler），工具与配置保持不变并重新施加到新文档的 handler，
// 这正是批注工具「全局」的来源。
class AnnotationWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AnnotationWidget(QWidget* parent = nullptr);

    // 绑定/解绑当前文档的数据源与交互（nullptr 表示无文档）。
    void setManager(AnnotationManager* manager);
    void setHandler(PDFAnnotationHandler* handler);

    void setCurrentPage(int pageIndex);

    // 供工具栏「批注」入口调用：激活钢笔。
    void activatePen();

    void refresh();

signals:
    void pageJumpRequested(int pageIndex);

private slots:
    void onPenToggled(bool checked);
    void onEraserToggled(bool checked);
    void choosePenColor();
    void clearCurrentPage();
    void clearAllPages();
    void updateActionState();

private:
    void setupUI();
    void applyConfigToHandler();
    void updateColorSwatch();
    QToolButton* createIconButton(const QString& iconName,
                                  const QString& objectName,
                                  const QString& tooltip,
                                  bool checkable = false);
    // 平铺单选：在 group 里加入一个画着圆点的可勾选按钮。
    // value = 施加到 handler 的实际宽度/半径；dotPx = 圆点显示直径。
    QToolButton* addSwatchButton(QButtonGroup* group, QHBoxLayout* row,
                                 qreal value, qreal dotPx,
                                 const QString& tooltip, bool hollow);
    static QIcon dotIcon(qreal dotPx, const QColor& color, bool hollow);
    qreal currentPenWidth() const;
    qreal currentEraserRadius() const;

    AnnotationManager* m_manager = nullptr;
    PDFAnnotationHandler* m_handler = nullptr;
    QMetaObject::Connection m_managerUndoConn;
    QMetaObject::Connection m_managerChangeConn;

    int m_currentPage = 0;
    QColor m_penColor = QColor(220, 30, 30);

    QToolButton* m_penButton = nullptr;
    QToolButton* m_eraserButton = nullptr;
    QButtonGroup* m_penWidthGroup = nullptr;
    QToolButton* m_colorButton = nullptr;
    QButtonGroup* m_eraserSizeGroup = nullptr;
    QToolButton* m_undoButton = nullptr;
    QToolButton* m_redoButton = nullptr;
    QToolButton* m_clearButton = nullptr;
    QAction*     m_clearPageItem = nullptr;
    QAction*     m_clearAllItem = nullptr;

    QListWidget* m_list = nullptr;
    QLabel* m_emptyHint = nullptr;
};

#endif // ANNOTATIONWIDGET_H
