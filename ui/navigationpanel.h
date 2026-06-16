#ifndef NAVIGATIONPANEL_H
#define NAVIGATIONPANEL_H

#include <QWidget>
#include <QTabWidget>
#include <QLabel>
#include <QProgressBar>
#include <QHash>
#include <QList>
#include <QSet>
#include <QMetaObject>

class PDFDocumentSession;
class OutlineWidget;
class ThumbnailWidget;
class AnnotationWidget;
class QToolButton;

class NavigationPanel : public QWidget
{
    Q_OBJECT

public:
    // 单例面板：构造时不绑定文档，由 attachSession 在切换文档时挂载
    explicit NavigationPanel(QWidget* parent = nullptr);
    ~NavigationPanel();

    // 挂载/卸载当前文档：断开旧 session 连接、重建数据、恢复 per-doc 视图状态
    void attachSession(PDFDocumentSession* session);
    void detachSession();
    PDFDocumentSession* session() const { return m_session; }

    void clear();
    void updateCurrentPage(int pageIndex);

    // 工具栏「批注」入口：切到批注 Tab 并激活钢笔。
    void showAnnotationTab();

signals:
    void pageJumpRequested(int pageIndex);
    void externalLinkRequested(const QString& uri);

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onTabChanged(int index);

private:
    void setupUI();
    void setupConnections();

    void loadDocument(int pageCount);

    // per-document 视图状态：子页选中(目录/缩略图) + 缩略图滚动位置
    void saveViewState();
    void restoreViewState();

    PDFDocumentSession* m_session;
    QTabWidget* m_tabWidget;
    OutlineWidget* m_outlineWidget;
    ThumbnailWidget* m_thumbnailWidget;
    AnnotationWidget* m_annotationWidget;
    int m_annotationTabIndex = -1;
    QToolButton* m_addOutlineBtn;
    QToolButton* m_deleteOutlineBtn;
    QToolButton* m_saveOutlineBtn;
    QToolButton* m_expandAllBtn;
    QToolButton* m_collapseAllBtn;

    QLabel* m_thumbnailStatusLabel;
    QProgressBar* m_thumbnailProgressBar;

    // 与当前 session 相关的连接，detach 时统一断开
    QList<QMetaObject::Connection> m_sessionConns;

    struct NavViewState {
        int subTabIndex = -1;   // -1 表示未记忆，按是否有目录决定默认页
        int thumbScroll = 0;
    };
    QHash<PDFDocumentSession*, NavViewState> m_viewStates;

    // 已挂接 destroyed 清理信号的 session（避免重复 connect）
    QSet<PDFDocumentSession*> m_trackedSessions;
};

#endif // NAVIGATIONPANEL_H
