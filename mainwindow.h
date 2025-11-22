#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>

// Forward declarations
class PDFDocumentSession;  // 新增:替代所有零散组件
class PDFPageWidget;
class NavigationPanel;
class SearchWidget;
class QScrollArea;
class QToolBar;
class QSpinBox;
class QComboBox;
class QLabel;
class QProgressBar;
class QAction;

enum class PageDisplayMode;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    // 文件操作
    void openFile();
    void closeFile();
    void quit();

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

    // 视图操作
    void togglePageMode(PageDisplayMode mode);
    void toggleContinuousScroll();

    // 事件响应
    void onPageChanged(int pageIndex);
    void onZoomChanged(double zoom);

    // 搜索
    void showSearchBar();
    void hideSearchBar();

    // 文本预加载
    void onTextPreloadProgress(int current, int total);
    void onTextPreloadCompleted();

    // 导航面板
    void toggleNavigationPanel();

    // 链接
    void toggleLinksVisible();

    // 文本选择
    void copySelectedText();
    void onTextSelectionChanged();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void createMenuBar();
    void createToolBar();
    void createStatusBar();
    void setupConnections();

    void updateUIState();
    void updateWindowTitle();
    void updateStatusBar();
    void updateScrollBarPolicy();

    void applyInitialSettings();
    void applyModernStyle();
    void loadLastSession();
    void saveCurrentSession();

private:
    // 核心组件(替代原来的多个组件)
    PDFDocumentSession* m_session;  // 唯一的核心组件

    // UI组件
    PDFPageWidget* m_pageWidget;
    QScrollArea* m_scrollArea;
    SearchWidget* m_searchWidget;
    NavigationPanel* m_navigationPanel;

    // 工具栏组件
    QToolBar* m_toolBar;
    QSpinBox* m_pageSpinBox;
    QComboBox* m_zoomComboBox;

    // 状态栏组件
    QLabel* m_statusLabel;
    QLabel* m_pageLabel;
    QLabel* m_zoomLabel;
    QProgressBar* m_textPreloadProgress;

    // 菜单/工具栏动作
    QAction* m_openAction;
    QAction* m_closeAction;
    QAction* m_quitAction;
    QAction* m_copyAction;
    QAction* m_findAction;
    QAction* m_findNextAction;
    QAction* m_findPreviousAction;
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
    QAction* m_firstPageAction;
    QAction* m_previousPageAction;
    QAction* m_nextPageAction;
    QAction* m_lastPageAction;

    // 防抖定时器
    QTimer m_resizeDebounceTimer;
};

#endif // MAINWINDOW_H
