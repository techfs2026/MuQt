#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include "datastructure.h"

class PDFDocumentTab;
class QTabWidget;
class QToolBar;
class QSpinBox;
class QComboBox;
class QLabel;
class QAction;

/**
 * @brief 主窗口 - 多标签页PDF查看器
 *
 * 职责:
 * - 管理多个PDF文档标签页
 * - 全局菜单栏、工具栏、状态栏
 * - 根据当前活动标签页更新UI状态
 * - 转发用户操作到当前标签页
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    // ========== 文件操作 ==========
    void openFile();
    void openFileInNewTab();
    void closeCurrentTab();
    void closeTab(int index);
    void quit();

    // ========== 标签页管理 ==========
    void onTabChanged(int index);
    void onTabCloseRequested(int index);
    void updateTabTitle(int index);

    // ========== 页面导航 ==========
    void previousPage();
    void nextPage();
    void firstPage();
    void lastPage();
    void goToPage(int page);

    // ========== 缩放操作 ==========
    void zoomIn();
    void zoomOut();
    void actualSize();
    void fitPage();
    void fitWidth();
    void onZoomComboChanged(const QString& text);

    // ========== 视图操作 ==========
    void togglePageMode(PageDisplayMode mode);
    void toggleContinuousScroll();
    void toggleNavigationPanel();
    void toggleLinksVisible();

    // ========== 搜索操作 ==========
    void showSearchBar();
    void findNext();
    void findPrevious();

    // ========== 文本操作 ==========
    void copySelectedText();

    // ========== 事件响应 ==========
    void onCurrentTabPageChanged(int pageIndex);
    void onCurrentTabZoomChanged(double zoom);
    void onCurrentTabDisplayModeChanged(PageDisplayMode mode);
    void onCurrentTabContinuousScrollChanged(bool continuous);
    void onCurrentTabTextSelectionChanged();
    void onCurrentTabDocumentLoaded(const QString& filePath, int pageCount);
    void onCurrentTabDocumentClosed();
    void onCurrentTabSearchCompleted(const QString& query, int totalMatches);

private:
    // ========== UI创建 ==========
    void createMenuBar();
    void createToolBar();
    void createStatusBar();
    void setupConnections();

    // ========== 状态管理 ==========
    void updateUIState();
    void updateWindowTitle();
    void updateStatusBar();
    void applyModernStyle();

    void updateZoomCombox(double zoom);

    // ========== 标签页操作 ==========
    PDFDocumentTab* currentTab() const;
    PDFDocumentTab* createNewTab();
    void connectTabSignals(PDFDocumentTab* tab);
    void disconnectTabSignals(PDFDocumentTab* tab);

private:
    // ========== 核心组件 ==========
    QTabWidget* m_tabWidget;
    QDockWidget* m_navigationDock;

    // ========== UI组件 ==========
    QToolBar* m_toolBar;
    QSpinBox* m_pageSpinBox;
    QComboBox* m_zoomComboBox;
    QLabel* m_statusLabel;
    QLabel* m_pageLabel;
    QLabel* m_zoomLabel;

    // ========== Actions ==========
    // 文件菜单
    QAction* m_openAction;
    QAction* m_openInNewTabAction;
    QAction* m_closeAction;
    QAction* m_quitAction;

    // 编辑菜单
    QAction* m_copyAction;
    QAction* m_findAction;
    QAction* m_findNextAction;
    QAction* m_findPreviousAction;

    // 视图菜单
    QAction* m_zoomInAction;
    QAction* m_zoomOutAction;
    QAction* m_actualSizeAction;
    QAction* m_fitPageAction;
    QAction* m_fitWidthAction;
    QAction* m_singlePageAction;
    QAction* m_doublePageAction;
    QAction* m_continuousScrollAction;
    QAction* m_showNavigationAction;
    QAction* m_showLinksAction;

    QActionGroup* m_pageModeGroup;

    // 导航菜单
    QAction* m_firstPageAction;
    QAction* m_previousPageAction;
    QAction* m_nextPageAction;
    QAction* m_lastPageAction;

    // ========== 工具 ==========
    QTimer m_resizeDebounceTimer;
};

#endif // MAINWINDOW_H
