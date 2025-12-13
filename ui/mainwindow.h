#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QPushButton>
#include "ocrengine.h"
#include "datastructure.h"

class QTabWidget;
class QToolBar;
class QDockWidget;
class QSpinBox;
class QComboBox;
class QLabel;
class QActionGroup;
class PDFDocumentTab;
class OCRStatusIndicator;

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
    // 文件操作
    void openFile();
    void closeCurrentTab();
    void quit();

    // 标签页管理
    void onTabChanged(int index);
    void onTabCloseRequested(int index);

    // 页面导航
    void previousPage();
    void nextPage();
    void firstPage();
    void lastPage();
    void goToPage(int page);

    // 缩放操作
    void zoomIn();
    void zoomOut();
    void actualSize();
    void fitPage();
    void fitWidth();
    void onZoomComboChanged(const QString& text);

    // 视图操作
    void togglePageMode(PageDisplayMode mode);
    void toggleContinuousScroll();
    void toggleNavigationPanel();
    void toggleLinksVisible();

    // 搜索操作
    void showSearchBar();
    void findNext();
    void findPrevious();

    // 文本操作
    void copySelectedText();

    // 当前Tab事件响应
    void onCurrentTabPageChanged(int pageIndex);
    void onCurrentTabZoomChanged(double zoom);
    void onCurrentTabDisplayModeChanged(PageDisplayMode mode);
    void onCurrentTabContinuousScrollChanged(bool continuous);
    void onCurrentTabTextSelectionChanged();
    void onCurrentTabDocumentLoaded(const QString& filePath, int pageCount);
    void onCurrentTabSearchCompleted(const QString& query, int totalMatches);

    void togglePaperEffect();

    void toggleOCRHover();
    void onOCREngineStateChanged(OCREngineState state);
    void onOCRHoverEnabledChanged(bool enabled);
    void triggerOCRAtCurrentPosition();

private:
    void createMenuBar();
    void createToolBar();
    void createStatusBar();
    void createActions();
    void setupConnections();

    // 状态管理
    void updateUIState();
    void updateWindowTitle();
    void updateStatusBar();
    void updateZoomCombox(double zoom);

    // 标签页管理
    PDFDocumentTab* currentTab() const;
    PDFDocumentTab* createNewTab();
    void connectTabSignals(PDFDocumentTab* tab);
    void disconnectTabSignals(PDFDocumentTab* tab);
    void closeTab(int index);
    void updateTabTitle(int index);

    void initOCREngine();
    void shutdownOCREngine();
    QString getEngineStateText(OCREngineState state) const;

private:

    // UI组件
    QTabWidget* m_tabWidget;
    QDockWidget* m_navigationDock;
    QToolBar* m_toolBar;
    QSpinBox* m_pageSpinBox;
    QComboBox* m_zoomComboBox;

    // 状态栏
    QLabel* m_statusLabel;
    QLabel* m_pageLabel;
    QLabel* m_zoomLabel;

    // 菜单Actions
    QAction* m_openAction;
    QAction* m_closeAction;
    QAction* m_quitAction;

    QAction* m_copyAction;
    QAction* m_findAction;
    QAction* m_findNextAction;
    QAction* m_findPreviousAction;

    QAction* m_zoomInAction;
    QAction* m_zoomOutAction;
    QAction* m_fitPageAction;
    QAction* m_fitWidthAction;

    QActionGroup* m_pageModeGroup;
    QAction* m_singlePageAction;
    QAction* m_doublePageAction;
    QAction* m_continuousScrollAction;

    QAction* m_showNavigationAction;
    QAction* m_showLinksAction;

    QAction* m_firstPageAction;
    QAction* m_previousPageAction;
    QAction* m_nextPageAction;
    QAction* m_lastPageAction;

    // 工具栏Actions（用于状态同步）
    QAction* m_navPanelAction;

    QAction* m_paperEffectAction;

    // 防抖定时器
    QTimer m_resizeDebounceTimer;

    // OCR相关
    QAction* m_ocrHoverAction;           // 工具栏的OCR按钮
    OCRStatusIndicator* m_ocrIndicator;  // 状态栏的指示器
    bool m_ocrInitialized;
};

#endif // MAINWINDOW_H
