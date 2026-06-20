#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QPushButton>
#include "iocrengine.h"
#include "datastructure.h"

class QTabWidget;
class QToolBar;
class QDockWidget;
class QSpinBox;
class QComboBox;
class QLabel;
class QToolButton;
class QActionGroup;
class PDFDocumentTab;
class NavigationPanel;
class OCRStatusIndicator;
struct SessionTabState;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void openFile();
    void closeCurrentTab();
    void saveCurrentTab();
    void quit();

    void onTabChanged(int index);
    void onTabCloseRequested(int index);

    void previousPage();
    void nextPage();
    void firstPage();
    void lastPage();
    void goToPage(int page);

    void zoomIn();
    void zoomOut();
    void actualSize();
    void fitPage();
    void fitWidth();
    void onZoomComboChanged(const QString& text);

    void togglePageMode(PageDisplayMode mode);
    void toggleContinuousScroll();
    void toggleNavigationPanel();
    void toggleLinksVisible();

    void showSearchBar();
    void findNext();
    void findPrevious();

    void copySelectedText();

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

    void toggleToolBar();

    // 批注：全局撤销/重做（入口在侧边栏「批注」Tab）
    void undoAnnotation();
    void redoAnnotation();

    void showSettingsDialog();

public slots:
    void openFileFromCommandLine(const QString& filePath);
    void restoreLastSession();

private:
    void createMenuBar();
    void createToolBar();
    void createStatusBar();
    void createActions();
    void setupConnections();

    void updateUIState();
    void updateWindowTitle();
    void updateStatusBar();
    void updateZoomCombox(double zoom);

    QString elideTabTitle(const QString& fullTitle, int maxLength) const;
    void installTabCloseButton(int index);

    PDFDocumentTab* currentTab() const;
    PDFDocumentTab* createNewTab();
    void connectTabSignals(PDFDocumentTab* tab);
    void disconnectTabSignals(PDFDocumentTab* tab);
    void closeTab(int index);
    void updateTabTitle(int index);

    // 关闭/退出前处理未保存的目录修改：返回 true 表示可继续，false 表示用户取消
    bool maybeSaveTab(PDFDocumentTab* tab);

    void initOCREngine();
    void shutdownOCREngine();
    QString getEngineStateText(OCREngineState state) const;

    void saveSession();
    void applyTabViewState(PDFDocumentTab* tab, const SessionTabState& state);


#ifdef Q_OS_WIN
private slots:
    void onManageFileAssociation();
public:
    void checkAndShowFirstRunDialog();
private:
    void handleFileAssociation(bool shouldRegister);
    QStringList getSupportedExtensions() const;
    QString getFileTypeName() const;
#endif

private:
    QTabWidget* m_tabWidget;
    QDockWidget* m_navigationDock;
    NavigationPanel* m_navigationPanel;   // 公共侧边栏（单例），常驻于 dock
    QToolBar* m_toolBar;
    QSpinBox* m_pageSpinBox;
    QComboBox* m_zoomComboBox;

    QLabel* m_statusLabel;

    QAction* m_openAction;
    QAction* m_closeAction;
    QAction* m_saveAction;
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

    QAction* m_navPanelAction;

    QAction* m_paperEffectAction;

    // 批注：全局撤销/重做快捷键（入口在侧边栏「批注」Tab）
    QAction* m_undoAnnotAction;
    QAction* m_redoAnnotAction;

    QAction* m_settingsAction;

    QTimer m_resizeDebounceTimer;

    QAction* m_ocrHoverAction;
    OCRStatusIndicator* m_ocrIndicator;
    bool m_ocrInitialized;

    QLabel* m_welcomeLabel;
    QAction* m_toggleToolBarAction;
};

#endif // MAINWINDOW_H
