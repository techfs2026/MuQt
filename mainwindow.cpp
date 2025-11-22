#include "mainwindow.h"
#include "pdfdocumenttab.h"
#include "appconfig.h"

#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QLabel>
#include <QSpinBox>
#include <QComboBox>
#include <QTabWidget>
#include <QTabBar>
#include <QApplication>
#include <QFileInfo>
#include <QCloseEvent>
#include <QDockWidget>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_tabWidget(nullptr)
    , m_navigationDock(nullptr)
    , m_toolBar(nullptr)
    , m_pageSpinBox(nullptr)
    , m_zoomComboBox(nullptr)
    , m_statusLabel(nullptr)
    , m_pageLabel(nullptr)
    , m_zoomLabel(nullptr)
{
    setWindowTitle(tr("JoPDF"));
    resize(AppConfig::instance().defaultWindowSize());

    // 创建标签页容器
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    m_tabWidget->setDocumentMode(true);
    m_tabWidget->setUsesScrollButtons(true);
    m_tabWidget->tabBar()->setExpanding(false);

    setCentralWidget(m_tabWidget);

    m_navigationDock = new QDockWidget(tr("Navigation"), this);
    m_navigationDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_navigationDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::LeftDockWidgetArea, m_navigationDock);
    m_navigationDock->setVisible(false);

    // 创建UI组件
    createMenuBar();
    createToolBar();
    createStatusBar();
    setupConnections();

    // 初始状态
    updateUIState();

    // 配置防抖定时器
    m_resizeDebounceTimer.setSingleShot(true);
    m_resizeDebounceTimer.setInterval(AppConfig::instance().resizeDebounceDelay());

    // 应用全局样式
    applyModernStyle();
}

MainWindow::~MainWindow()
{
    // 关闭所有标签页
    while (m_tabWidget->count() > 0) {
        closeTab(0);
    }
}

// ========== 文件操作 ==========

void MainWindow::openFile()
{
    QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Open PDF File"),
        QString(),
        tr("PDF Files (*.pdf);;All Files (*.*)")
        );

    if (filePath.isEmpty()) {
        return;
    }

    PDFDocumentTab* tab = currentTab();

    // ✅ 如果没有标签页或当前标签页已加载,创建新标签页
    if (!tab || tab->isDocumentLoaded()) {
        tab = createNewTab();
    }

    QString errorMsg;
    if (!tab->loadDocument(filePath, &errorMsg)) {
        QMessageBox::critical(this, tr("Error"),
                              tr("Failed to open PDF file:\n%1\n\nError: %2")
                                  .arg(filePath).arg(errorMsg));

        // 如果加载失败,清理标签页
        if (m_tabWidget->count() > 1) {
            int index = m_tabWidget->indexOf(tab);
            closeTab(index);
        }
    }
}

void MainWindow::openFileInNewTab()
{
    QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Open PDF File in New Tab"),
        QString(),
        tr("PDF Files (*.pdf);;All Files (*.*)")
        );

    if (filePath.isEmpty()) {
        return;
    }

    PDFDocumentTab* newTab = createNewTab();

    QString errorMsg;
    if (!newTab->loadDocument(filePath, &errorMsg)) {
        QMessageBox::critical(this, tr("Error"),
                              tr("Failed to open PDF file:\n%1\n\nError: %2")
                                  .arg(filePath).arg(errorMsg));

        // 如果加载失败且这是唯一的标签页,保留它
        // 否则关闭这个失败的标签页
        if (m_tabWidget->count() > 1) {
            int index = m_tabWidget->indexOf(newTab);
            closeTab(index);
        }
    }
}

void MainWindow::closeCurrentTab()
{
    int index = m_tabWidget->currentIndex();
    if (index >= 0) {
        closeTab(index);
    }
}

void MainWindow::closeTab(int index)
{
    if (index < 0 || index >= m_tabWidget->count()) {
        return;
    }

    PDFDocumentTab* tab = qobject_cast<PDFDocumentTab*>(m_tabWidget->widget(index));
    if (!tab) {
        return;
    }

    // ✅ 直接关闭最后一个标签页
    disconnectTabSignals(tab);
    m_tabWidget->removeTab(index);
    tab->deleteLater();

    // 如果没有标签页了,更新UI
    if (m_tabWidget->count() == 0) {
        updateUIState();
    }
}

void MainWindow::quit()
{
    QApplication::quit();
}

// ========== 标签页管理 ==========

PDFDocumentTab* MainWindow::currentTab() const
{
    return qobject_cast<PDFDocumentTab*>(m_tabWidget->currentWidget());
}

PDFDocumentTab* MainWindow::createNewTab()
{
    PDFDocumentTab* tab = new PDFDocumentTab(this);

    int index = m_tabWidget->addTab(tab, tr("New Tab"));
    m_tabWidget->setCurrentIndex(index);

    // 连接信号
    connectTabSignals(tab);

    return tab;
}

void MainWindow::connectTabSignals(PDFDocumentTab* tab)
{
    if (!tab) return;

    // 文档生命周期
    connect(tab, &PDFDocumentTab::documentLoaded,
            this, &MainWindow::onCurrentTabDocumentLoaded);

    connect(tab, &PDFDocumentTab::documentClosed,
            this, &MainWindow::onCurrentTabDocumentClosed);

    // 视图状态变化
    connect(tab, &PDFDocumentTab::pageChanged,
            this, &MainWindow::onCurrentTabPageChanged);

    connect(tab, &PDFDocumentTab::zoomChanged,
            this, &MainWindow::onCurrentTabZoomChanged);

    connect(tab, &PDFDocumentTab::displayModeChanged,
            this, &MainWindow::onCurrentTabDisplayModeChanged);

    connect(tab, &PDFDocumentTab::continuousScrollChanged,
            this, &MainWindow::onCurrentTabContinuousScrollChanged);

    // 文本选择
    connect(tab, &PDFDocumentTab::textSelectionChanged,
            this, &MainWindow::onCurrentTabTextSelectionChanged);

    // 搜索
    connect(tab, &PDFDocumentTab::searchCompleted,
            this, &MainWindow::onCurrentTabSearchCompleted);
}

void MainWindow::disconnectTabSignals(PDFDocumentTab* tab)
{
    if (!tab) return;
    disconnect(tab, nullptr, this, nullptr);
}

void MainWindow::onTabChanged(int index)
{
    Q_UNUSED(index);

    PDFDocumentTab* tab = currentTab();

    if (tab && tab->isDocumentLoaded() && tab->navigationPanel()) {
        m_navigationDock->setWidget(tab->navigationPanel());
        m_showNavigationAction->setChecked(m_navigationDock->isVisible());

        QTimer::singleShot(0, [tab]() {
            ZoomMode mode = tab->zoomMode();
            if (mode == ZoomMode::FitWidth || mode == ZoomMode::FitPage) {
                tab->pageWidget()->updateZoom();
            }
        });
    } else {
        // 无文档或无 tab，隐藏导航面板
        m_navigationDock->setWidget(nullptr);
        m_navigationDock->setVisible(false);
        m_showNavigationAction->setChecked(false);
    }

    updateUIState();
    updateWindowTitle();
}

void MainWindow::onTabCloseRequested(int index)
{
    closeTab(index);
}

void MainWindow::updateTabTitle(int index)
{
    PDFDocumentTab* tab = qobject_cast<PDFDocumentTab*>(m_tabWidget->widget(index));
    if (tab) {
        m_tabWidget->setTabText(index, tab->documentTitle());
        m_tabWidget->setTabToolTip(index, tab->documentPath());
    }
}

// ========== 页面导航 ==========

void MainWindow::previousPage()
{
    if (PDFDocumentTab* tab = currentTab()) {
        tab->previousPage();
    }
}

void MainWindow::nextPage()
{
    if (PDFDocumentTab* tab = currentTab()) {
        tab->nextPage();
    }
}

void MainWindow::firstPage()
{
    if (PDFDocumentTab* tab = currentTab()) {
        tab->firstPage();
    }
}

void MainWindow::lastPage()
{
    if (PDFDocumentTab* tab = currentTab()) {
        tab->lastPage();
    }
}

void MainWindow::goToPage(int page)
{
    if (PDFDocumentTab* tab = currentTab()) {
        // SpinBox是1-based,内部是0-based
        tab->goToPage(page - 1);
    }
}

// ========== 缩放操作 ==========

void MainWindow::zoomIn()
{
    if (PDFDocumentTab* tab = currentTab()) {
        tab->zoomIn();
    }
}

void MainWindow::zoomOut()
{
    if (PDFDocumentTab* tab = currentTab()) {
        tab->zoomOut();
    }
}

void MainWindow::actualSize()
{
    if (PDFDocumentTab* tab = currentTab()) {
        tab->actualSize();
    }
}

void MainWindow::fitPage()
{
    if (PDFDocumentTab* tab = currentTab()) {
        tab->fitPage();
    }
}

void MainWindow::fitWidth()
{
    if (PDFDocumentTab* tab = currentTab()) {
        tab->fitWidth();
    }
}

void MainWindow::onZoomComboChanged(const QString& text)
{
    QString cleaned = text;
    cleaned.remove('%').remove(' ');
    bool ok;
    double zoom = cleaned.toDouble(&ok) / 100.0;

    if (ok && zoom > 0) {
        if (PDFDocumentTab* tab = currentTab()) {
            tab->setZoom(zoom);
        }
    }
}

// ========== 视图操作 ==========

void MainWindow::togglePageMode(PageDisplayMode mode)
{
    if (PDFDocumentTab* tab = currentTab()) {
        tab->setDisplayMode(mode);
    }
}

void MainWindow::toggleContinuousScroll()
{
    if (PDFDocumentTab* tab = currentTab()) {
        bool continuous = !tab->isContinuousScroll();
        tab->setContinuousScroll(continuous);
    }
}

void MainWindow::toggleNavigationPanel()
{
    PDFDocumentTab* tab = currentTab();
    if (!tab || !tab->isDocumentLoaded()) {
        return;
    }

    bool visible = !m_navigationDock->isVisible();

    if (visible) {
        if (m_navigationDock->widget() != tab->navigationPanel()) {
            m_navigationDock->setWidget(tab->navigationPanel());
        }
    }

    m_navigationDock->setVisible(visible);
    m_showNavigationAction->setChecked(visible);

    QTimer::singleShot(0, this, [tab]() {
        ZoomMode mode = tab->zoomMode();
        if (mode == ZoomMode::FitWidth || mode == ZoomMode::FitPage) {
            tab->pageWidget()->updateZoom();
        }
    });
}

void MainWindow::toggleLinksVisible()
{
    bool visible = m_showLinksAction->isChecked();
    if (PDFDocumentTab* tab = currentTab()) {
        tab->setLinksVisible(visible);
    }
}

// ========== 搜索操作 ==========

void MainWindow::showSearchBar()
{
    if (PDFDocumentTab* tab = currentTab()) {
        tab->showSearchBar();
    }
}

void MainWindow::findNext()
{
    if (PDFDocumentTab* tab = currentTab()) {
        if (tab->searchWidget()) {
            tab->searchWidget()->findNext();
        }
    }
}

void MainWindow::findPrevious()
{
    if (PDFDocumentTab* tab = currentTab()) {
        if (tab->searchWidget()) {
            tab->searchWidget()->findPrevious();
        }
    }
}

// ========== 文本操作 ==========

void MainWindow::copySelectedText()
{
    if (PDFDocumentTab* tab = currentTab()) {
        tab->copySelectedText();
    }
}

// ========== 事件响应 ==========

void MainWindow::onCurrentTabPageChanged(int pageIndex)
{
    // 检查信号来源是否是当前标签页
    PDFDocumentTab* sender = qobject_cast<PDFDocumentTab*>(QObject::sender());
    if (sender != currentTab()) {
        return;
    }

    updateStatusBar();

    // 更新SpinBox
    if (m_pageSpinBox) {
        m_pageSpinBox->blockSignals(true);
        m_pageSpinBox->setValue(pageIndex + 1);
        m_pageSpinBox->blockSignals(false);
    }

    updateUIState();
}

void MainWindow::onCurrentTabZoomChanged(double zoom)
{
    PDFDocumentTab* sender = qobject_cast<PDFDocumentTab*>(QObject::sender());
    if (sender != currentTab()) {
        return;
    }

    updateStatusBar();

    // 更新ComboBox
    updateZoomCombox(zoom);
}

void MainWindow::updateZoomCombox(double zoom) {
    if (m_zoomComboBox) {
        QString text = QString::number(qRound(zoom * 100)) + "%";
        int index = m_zoomComboBox->findText(text);

        m_zoomComboBox->blockSignals(true);
        if (index >= 0) {
            m_zoomComboBox->setCurrentIndex(index);
        } else {
            m_zoomComboBox->setEditText(text);
        }
        m_zoomComboBox->blockSignals(false);
    }
}

void MainWindow::onCurrentTabDisplayModeChanged(PageDisplayMode mode)
{
    PDFDocumentTab* sender = qobject_cast<PDFDocumentTab*>(QObject::sender());
    if (sender != currentTab()) {
        return;
    }

    m_doublePageAction->setChecked(mode == PageDisplayMode::DoublePage);
    m_singlePageAction->setChecked(mode == PageDisplayMode::SinglePage);
    m_continuousScrollAction->setEnabled(mode == PageDisplayMode::SinglePage);
}

void MainWindow::onCurrentTabContinuousScrollChanged(bool continuous)
{
    PDFDocumentTab* sender = qobject_cast<PDFDocumentTab*>(QObject::sender());
    if (sender != currentTab()) {
        return;
    }

    m_continuousScrollAction->setChecked(continuous);
}

void MainWindow::onCurrentTabTextSelectionChanged()
{
    PDFDocumentTab* sender = qobject_cast<PDFDocumentTab*>(QObject::sender());
    if (sender != currentTab()) {
        return;
    }

    PDFDocumentTab* tab = currentTab();
    if (tab && m_copyAction) {
        m_copyAction->setEnabled(tab->hasTextSelection());
    }

    updateStatusBar();
}

void MainWindow::onCurrentTabDocumentLoaded(const QString& filePath, int pageCount)
{
    Q_UNUSED(filePath);
    Q_UNUSED(pageCount);

    PDFDocumentTab* tab = qobject_cast<PDFDocumentTab*>(QObject::sender());
    if (!tab) {
        return;
    }

    // 更新标签页标题
    int index = m_tabWidget->indexOf(tab);
    if (index >= 0) {
        updateTabTitle(index);
    }

    // 如果是当前标签页，更新UI
    if (tab == currentTab()) {
        updateWindowTitle();
        updateUIState();

        // ✅ 显示导航面板（文档加载后默认显示）
        if (tab->navigationPanel()) {
            m_navigationDock->setWidget(tab->navigationPanel());
            m_navigationDock->setVisible(true);
            m_showNavigationAction->setChecked(true);

        }
    }
}

void MainWindow::onCurrentTabDocumentClosed()
{
    PDFDocumentTab* sender = qobject_cast<PDFDocumentTab*>(QObject::sender());
    if (!sender) {
        return;
    }

    // 更新标签页标题
    int index = m_tabWidget->indexOf(sender);
    if (index >= 0) {
        m_tabWidget->setTabText(index, tr("New Tab"));
        m_tabWidget->setTabToolTip(index, "");
    }

    // 如果是当前标签页,更新UI
    if (sender == currentTab()) {
        updateWindowTitle();
        updateUIState();
    }
}

void MainWindow::onCurrentTabSearchCompleted(const QString& query, int totalMatches)
{
    Q_UNUSED(query);

    PDFDocumentTab* sender = qobject_cast<PDFDocumentTab*>(QObject::sender());
    if (sender != currentTab()) {
        return;
    }

    m_findNextAction->setEnabled(totalMatches > 0);
    m_findPreviousAction->setEnabled(totalMatches > 0);
}

// ========== UI创建 ==========

void MainWindow::createMenuBar()
{
    // 隐藏菜单栏以获得更现代的外观
    menuBar()->setNativeMenuBar(false);

    // 文件菜单
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));

    m_openAction = fileMenu->addAction(tr("&Open..."), this, &MainWindow::openFile);
    m_openAction->setShortcut(QKeySequence::Open);

    m_openInNewTabAction = fileMenu->addAction(tr("Open in &New Tab..."),
                                               this, &MainWindow::openFileInNewTab);
    m_openInNewTabAction->setShortcut(tr("Ctrl+Shift+O"));

    m_closeAction = fileMenu->addAction(tr("&Close"), this, &MainWindow::closeCurrentTab);
    m_closeAction->setShortcut(QKeySequence::Close);

    fileMenu->addSeparator();

    m_quitAction = fileMenu->addAction(tr("&Quit"), this, &MainWindow::quit);
    m_quitAction->setShortcut(QKeySequence::Quit);

    // 编辑菜单
    QMenu* editMenu = menuBar()->addMenu(tr("&Edit"));

    m_copyAction = editMenu->addAction(tr("&Copy"), this, &MainWindow::copySelectedText);
    m_copyAction->setShortcut(QKeySequence::Copy);
    m_copyAction->setEnabled(false);

    editMenu->addSeparator();

    m_findAction = editMenu->addAction(tr("&Find..."), this, &MainWindow::showSearchBar);
    m_findAction->setShortcut(QKeySequence::Find);

    m_findNextAction = editMenu->addAction(tr("Find &Next"), this, &MainWindow::findNext);
    m_findNextAction->setShortcut(QKeySequence::FindNext);
    m_findNextAction->setEnabled(false);

    m_findPreviousAction = editMenu->addAction(tr("Find &Previous"),
                                               this, &MainWindow::findPrevious);
    m_findPreviousAction->setShortcut(QKeySequence::FindPrevious);
    m_findPreviousAction->setEnabled(false);

    // 视图菜单
    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));

    m_zoomInAction = viewMenu->addAction(tr("Zoom &In"), this, &MainWindow::zoomIn);
    m_zoomInAction->setShortcut(QKeySequence::ZoomIn);

    m_zoomOutAction = viewMenu->addAction(tr("Zoom &Out"), this, &MainWindow::zoomOut);
    m_zoomOutAction->setShortcut(QKeySequence::ZoomOut);

    m_actualSizeAction = viewMenu->addAction(tr("&Actual Size"), this, &MainWindow::actualSize);
    m_actualSizeAction->setShortcut(tr("Ctrl+0"));

    viewMenu->addSeparator();

    m_fitPageAction = viewMenu->addAction(tr("Fit &Page"), this, &MainWindow::fitPage);
    m_fitPageAction->setShortcut(tr("Ctrl+1"));

    m_fitWidthAction = viewMenu->addAction(tr("Fit &Width"), this, &MainWindow::fitWidth);
    m_fitWidthAction->setShortcut(tr("Ctrl+2"));

    viewMenu->addSeparator();

    m_singlePageAction = viewMenu->addAction(tr("&Single Page"), this, [this]() {
        togglePageMode(PageDisplayMode::SinglePage);
    });
    m_singlePageAction->setCheckable(true);
    m_singlePageAction->setChecked(true);

    m_doublePageAction = viewMenu->addAction(tr("&Double Page"), this, [this]() {
        togglePageMode(PageDisplayMode::DoublePage);
    });
    m_doublePageAction->setCheckable(true);

    m_continuousScrollAction = viewMenu->addAction(tr("&Continuous Scroll"),
                                                   this, &MainWindow::toggleContinuousScroll);
    m_continuousScrollAction->setCheckable(true);

    viewMenu->addSeparator();

    m_showNavigationAction = viewMenu->addAction(tr("Show &Navigation Panel"),
                                                 this, &MainWindow::toggleNavigationPanel);
    m_showNavigationAction->setCheckable(true);
    m_showNavigationAction->setShortcut(tr("F9"));

    m_showLinksAction = viewMenu->addAction(tr("Show &Links"),
                                            this, &MainWindow::toggleLinksVisible);
    m_showLinksAction->setCheckable(true);
    m_showLinksAction->setChecked(true);

    // 导航菜单
    QMenu* navMenu = menuBar()->addMenu(tr("&Navigation"));

    m_firstPageAction = navMenu->addAction(tr("&First Page"), this, &MainWindow::firstPage);
    m_firstPageAction->setShortcut(tr("Home"));

    m_previousPageAction = navMenu->addAction(tr("&Previous Page"),
                                              this, &MainWindow::previousPage);
    m_previousPageAction->setShortcut(tr("PgUp"));

    m_nextPageAction = navMenu->addAction(tr("&Next Page"), this, &MainWindow::nextPage);
    m_nextPageAction->setShortcut(tr("PgDown"));

    m_lastPageAction = navMenu->addAction(tr("&Last Page"), this, &MainWindow::lastPage);
    m_lastPageAction->setShortcut(tr("End"));
}

void MainWindow::createToolBar()
{
    m_toolBar = addToolBar(tr("Main Toolbar"));
    m_toolBar->setMovable(false);
    m_toolBar->setFloatable(false);
    m_toolBar->setIconSize(QSize(20, 20));
    m_toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_toolBar->setContentsMargins(8, 4, 8, 4);
    m_toolBar->setObjectName("mainToolBar");

    // 导航面板按钮
    QAction* navPanelAction = m_toolBar->addAction(QIcon(":/icons/icons/sidebar.png"),
                                                   tr("Panel"));
    navPanelAction->setToolTip(tr("Navigation Panel (F9)"));
    navPanelAction->setCheckable(true);
    connect(navPanelAction, &QAction::triggered, this, &MainWindow::toggleNavigationPanel);

    m_toolBar->addSeparator();

    // 文件操作
    QAction* openAction = m_toolBar->addAction(QIcon(":/icons/icons/open file.png"),
                                               tr("Open"));
    openAction->setToolTip(tr("Open PDF (Ctrl+O)"));
    connect(openAction, &QAction::triggered, this, &MainWindow::openFile);

    m_toolBar->addSeparator();

    // 导航
    QAction* firstAction = m_toolBar->addAction(QIcon(":/icons/icons/first-arrow.png"),
                                                tr("First"));
    firstAction->setToolTip(tr("First Page (Home)"));
    connect(firstAction, &QAction::triggered, this, &MainWindow::firstPage);

    QAction* prevAction = m_toolBar->addAction(QIcon(":/icons/icons/left-arrow.png"),
                                               tr("Previous"));
    prevAction->setToolTip(tr("Previous Page (PgUp)"));
    connect(prevAction, &QAction::triggered, this, &MainWindow::previousPage);

    // 页码输入
    m_toolBar->addWidget(new QLabel("  "));
    m_pageSpinBox = new QSpinBox(this);
    m_pageSpinBox->setMinimum(1);
    m_pageSpinBox->setMaximum(1);
    m_pageSpinBox->setEnabled(false);
    m_pageSpinBox->setMinimumWidth(70);
    m_pageSpinBox->setMaximumWidth(100);
    m_pageSpinBox->setAlignment(Qt::AlignCenter);
    m_pageSpinBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_pageSpinBox->setObjectName("pageSpinBox");
    connect(m_pageSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::goToPage);
    m_toolBar->addWidget(m_pageSpinBox);

    QAction* nextAction = m_toolBar->addAction(QIcon(":/icons/icons/right-arrow.png"),
                                               tr("Next"));
    nextAction->setToolTip(tr("Next Page (PgDown)"));
    connect(nextAction, &QAction::triggered, this, &MainWindow::nextPage);

    QAction* lastAction = m_toolBar->addAction(QIcon(":/icons/icons/last-arrow.png"),
                                               tr("Last"));
    lastAction->setToolTip(tr("Last Page (End)"));
    connect(lastAction, &QAction::triggered, this, &MainWindow::lastPage);

    m_toolBar->addSeparator();

    // 缩放
    QAction* zoomOutAction = m_toolBar->addAction(QIcon(":/icons/icons/zoom-out.png"),
                                                  tr("Zoom Out"));
    zoomOutAction->setToolTip(tr("Zoom Out (Ctrl+-)"));
    connect(zoomOutAction, &QAction::triggered, this, &MainWindow::zoomOut);

    m_zoomComboBox = new QComboBox(this);
    m_zoomComboBox->setEditable(true);
    m_zoomComboBox->setMinimumWidth(85);
    m_zoomComboBox->setMaximumWidth(100);
    m_zoomComboBox->setObjectName("zoomComboBox");
    m_zoomComboBox->addItems({
        "25%", "50%", "75%", "100%", "125%", "150%", "200%", "300%", "400%"
    });
    m_zoomComboBox->setCurrentText("100%");
    connect(m_zoomComboBox, &QComboBox::currentTextChanged,
            this, &MainWindow::onZoomComboChanged);
    m_toolBar->addWidget(m_zoomComboBox);

    QAction* zoomInAction = m_toolBar->addAction(QIcon(":/icons/icons/zoom-in.png"),
                                                 tr("Zoom In"));
    zoomInAction->setToolTip(tr("Zoom In (Ctrl++)"));
    connect(zoomInAction, &QAction::triggered, this, &MainWindow::zoomIn);

    m_toolBar->addSeparator();

    // 适应选项
    QAction* fitPageAction = m_toolBar->addAction(QIcon(":/icons/icons/fit-to-page.png"),
                                                  tr("Fit Page"));
    fitPageAction->setToolTip(tr("Fit Page (Ctrl+1)"));
    connect(fitPageAction, &QAction::triggered, this, &MainWindow::fitPage);

    QAction* fitWidthAction = m_toolBar->addAction(QIcon(":/icons/icons/fit-to-width.png"),
                                                   tr("Fit Width"));
    fitWidthAction->setToolTip(tr("Fit Width (Ctrl+2)"));
    connect(fitWidthAction, &QAction::triggered, this, &MainWindow::fitWidth);

    // 弹性空间
    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolBar->addWidget(spacer);

    // 搜索按钮
    QAction* searchAction = m_toolBar->addAction(QIcon(":/icons/icons/search.png"),
                                                 tr("Search"));
    searchAction->setToolTip(tr("Search (Ctrl+F)"));
    connect(searchAction, &QAction::triggered, this, &MainWindow::showSearchBar);

    // 保存action引用
    m_firstPageAction = firstAction;
    m_previousPageAction = prevAction;
    m_nextPageAction = nextAction;
    m_lastPageAction = lastAction;
    m_zoomInAction = zoomInAction;
    m_zoomOutAction = zoomOutAction;
    m_fitPageAction = fitPageAction;
    m_fitWidthAction = fitWidthAction;
}

void MainWindow::createStatusBar()
{
    statusBar()->setObjectName("modernStatusBar");
    statusBar()->setSizeGripEnabled(true);

    m_statusLabel = new QLabel(tr("Ready"));
    m_statusLabel->setObjectName("statusLabel");
    statusBar()->addWidget(m_statusLabel, 1);

    m_pageLabel = new QLabel();
    m_pageLabel->setObjectName("pageLabel");
    m_pageLabel->setMinimumWidth(120);
    m_pageLabel->setAlignment(Qt::AlignCenter);
    statusBar()->addPermanentWidget(m_pageLabel);

    m_zoomLabel = new QLabel();
    m_zoomLabel->setObjectName("zoomLabel");
    m_zoomLabel->setMinimumWidth(100);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    statusBar()->addPermanentWidget(m_zoomLabel);

    updateStatusBar();
}

void MainWindow::setupConnections()
{
    // 标签页容器信号
    connect(m_tabWidget, &QTabWidget::currentChanged,
            this, &MainWindow::onTabChanged);

    connect(m_tabWidget, &QTabWidget::tabCloseRequested,
            this, &MainWindow::onTabCloseRequested);

    // 防抖定时器
    connect(&m_resizeDebounceTimer, &QTimer::timeout, this, [this]() {
        PDFDocumentTab* tab = currentTab();
        if (tab && tab->isDocumentLoaded()) {
            ZoomMode mode = tab->zoomMode();
            if (mode == ZoomMode::FitWidth || mode == ZoomMode::FitPage) {
                // 触发重新计算缩放
                tab->pageWidget()->updateZoom();
            }
        }
    });
}

// ========== 状态管理 ==========

void MainWindow::updateUIState()
{
    PDFDocumentTab* tab = currentTab();
    bool hasDocument = tab && tab->isDocumentLoaded();
    int pageCount = hasDocument ? tab->pageCount() : 0;
    int currentPage = hasDocument ? tab->currentPage() : 0;
    double zoom = hasDocument? tab->zoom(): 1.0;
    bool continuousScroll = hasDocument? tab->isContinuousScroll(): false;

    // 文件操作
    m_closeAction->setEnabled(hasDocument);

    if (m_copyAction) {
        bool hasSelection = hasDocument && tab->hasTextSelection();
        m_copyAction->setEnabled(hasDocument && tab->isTextPDF() && hasSelection);
    }

    // 搜索功能
    m_findAction->setEnabled(hasDocument && tab->isTextPDF());

    // 导航操作
    m_firstPageAction->setEnabled(hasDocument && currentPage > 0);
    m_previousPageAction->setEnabled(hasDocument && currentPage > 0);
    m_nextPageAction->setEnabled(hasDocument && currentPage < pageCount - 1);
    m_lastPageAction->setEnabled(hasDocument && currentPage < pageCount - 1);

    // 缩放操作
    m_zoomInAction->setEnabled(hasDocument);
    m_zoomOutAction->setEnabled(hasDocument);
    m_actualSizeAction->setEnabled(hasDocument);
    m_fitPageAction->setEnabled(hasDocument);
    m_fitWidthAction->setEnabled(hasDocument);

    // 视图操作
    m_singlePageAction->setEnabled(hasDocument);
    m_doublePageAction->setEnabled(hasDocument);
    m_continuousScrollAction->setEnabled(hasDocument);
    m_continuousScrollAction->setChecked(continuousScroll);

    // 导航面板
    m_showNavigationAction->setEnabled(hasDocument);
    m_showLinksAction->setEnabled(hasDocument);

    // 工具栏组件
    if (m_pageSpinBox) {
        m_pageSpinBox->setEnabled(hasDocument);
        m_pageSpinBox->setMaximum(qMax(1, pageCount));
        if (hasDocument) {
            m_pageSpinBox->setValue(currentPage + 1);
            m_pageSpinBox->setSuffix(tr(" / %1").arg(pageCount));
        } else {
            m_pageSpinBox->setValue(1);
            m_pageSpinBox->setSuffix("");
        }
    }

    if (m_zoomComboBox) {
        m_zoomComboBox->setEnabled(hasDocument);
        updateZoomCombox(zoom);
    }

    updateStatusBar();
}

void MainWindow::updateWindowTitle()
{
    QString title = tr("JoPDF");

    PDFDocumentTab* tab = currentTab();
    if (tab && tab->isDocumentLoaded()) {
        QString filePath = tab->documentPath();
        if (!filePath.isEmpty()) {
            QFileInfo fileInfo(filePath);
            title = fileInfo.fileName() + " - " + title;
        }
    }

    setWindowTitle(title);
}

void MainWindow::updateStatusBar()
{
    PDFDocumentTab* tab = currentTab();

    if (!tab || !tab->isDocumentLoaded()) {
        m_pageLabel->setText("");
        m_zoomLabel->setText("");
        // ✅ 显示友好提示
        m_statusLabel->setText(tr("No document opened. Press Ctrl+O to open a PDF."));
        return;
    }

    int currentPage = tab->currentPage() + 1;
    int pageCount = tab->pageCount();
    m_pageLabel->setText(tr("📄 %1 / %2").arg(currentPage).arg(pageCount));

    double zoom = tab->zoom();
    QString zoomMode;
    switch (tab->zoomMode()) {
    case ZoomMode::FitPage:
        zoomMode = tr(" (Fit Page)");
        break;
    case ZoomMode::FitWidth:
        zoomMode = tr(" (Fit Width)");
        break;
    default:
        break;
    }
    m_zoomLabel->setText(tr("🔍 %1%%2").arg(qRound(zoom * 100)).arg(zoomMode));

    if (tab->hasTextSelection()) {
        m_statusLabel->setText(tr("Text selected"));
    } else {
        m_statusLabel->setText(tr("Ready"));
    }
}

// ========== 事件处理 ==========

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    m_resizeDebounceTimer.start();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // 可以在这里添加"确认关闭多个标签页"的对话框
    int tabCount = m_tabWidget->count();
    int loadedCount = 0;

    for (int i = 0; i < tabCount; ++i) {
        PDFDocumentTab* tab = qobject_cast<PDFDocumentTab*>(m_tabWidget->widget(i));
        if (tab && tab->isDocumentLoaded()) {
            loadedCount++;
        }
    }

    // 如果有多个已加载的文档,询问用户
    if (loadedCount > 1) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            tr("Close Application"),
            tr("You have %1 documents open. Are you sure you want to close all of them?")
                .arg(loadedCount),
            QMessageBox::Yes | QMessageBox::No
            );

        if (reply == QMessageBox::No) {
            event->ignore();
            return;
        }
    }

    QMainWindow::closeEvent(event);
}

void MainWindow::applyModernStyle()
{
    // PDF Expert 风格的黑白色调全局样式
    QString style = R"(
        /* 主窗口 */
        QMainWindow {
            background-color: #FAFAFA;
        }

        /* 菜单栏 */
        QMenuBar {
            background-color: #FFFFFF;
            border-bottom: 1px solid #E0E0E0;
            padding: 4px;
            font-size: 13px;
        }

        QMenuBar::item {
            background-color: transparent;
            color: #2C2C2C;
            padding: 6px 12px;
            border-radius: 5px;
        }

        QMenuBar::item:selected {
            background-color: #F0F0F0;
        }

        QMenuBar::item:pressed {
            background-color: #E5E5E5;
        }

        /* 菜单 */
        QMenu {
            background-color: #FFFFFF;
            border: 1px solid #D0D0D0;
            border-radius: 6px;
            padding: 4px;
        }

        QMenu::item {
            padding: 7px 30px 7px 14px;
            border-radius: 4px;
            color: #2C2C2C;
            font-size: 13px;
        }

        QMenu::item:selected {
            background-color: #4A4A4A;
            color: #FFFFFF;
        }

        QMenu::separator {
            height: 1px;
            background-color: #E5E5E5;
            margin: 4px 10px;
        }

        /* 工具栏 */
        #mainToolBar {
            background-color: #FFFFFF;
            border: none;
            border-bottom: 1px solid #E0E0E0;
            spacing: 3px;
            padding: 6px 6px;
            min-height: 44px;
            max-height: 44px;
        }

        #mainToolBar QToolButton {
            background-color: transparent;
            border: 1px solid transparent;
            border-radius: 5px;
            padding: 6px;
            color: #2C2C2C;
            min-width: 32px;
            max-width: 32px;
            min-height: 32px;
            max-height: 32px;
        }

        #mainToolBar QToolButton:hover {
            background-color: #F5F5F5;
            border: 1px solid #E0E0E0;
        }

        #mainToolBar QToolButton:pressed {
            background-color: #E8E8E8;
            border: 1px solid #D0D0D0;
        }

        #mainToolBar QToolButton:disabled {
            opacity: 0.4;
        }

        #mainToolBar QToolButton:checked {
            background-color: #4A4A4A;
            border: 1px solid #3A3A3A;
        }

        #mainToolBar::separator {
            background-color: #D5D5D5;
            width: 1px;
            margin: 6px 5px;
        }

        /* 页码输入框 */
        #pageSpinBox {
            background-color: #FFFFFF;
            border: 1px solid #D0D0D0;
            border-radius: 5px;
            padding: 4px 6px;
            color: #2C2C2C;
            font-size: 12px;
            font-weight: 500;
            min-height: 24px;
            max-height: 24px;
            min-width: 80px;
            max-width: 80px;
        }

        #pageSpinBox:focus {
            background-color: #FFFFFF;
            border: 1px solid #4A4A4A;
        }

        #pageSpinBox:disabled {
            background-color: #F8F8F8;
            color: #A0A0A0;
        }

        /* 缩放下拉框 */
        #zoomComboBox {
            background-color: #FFFFFF;
            border: 1px solid #D0D0D0;
            border-radius: 5px;
            padding: 4px 6px;
            color: #2C2C2C;
            font-size: 12px;
            min-height: 24px;
            max-height: 24px;
            min-width: 70px;
            max-width: 70px;
        }

        #zoomComboBox:focus {
            background-color: #FFFFFF;
            border: 1px solid #4A4A4A;
        }

        #zoomComboBox:disabled {
            background-color: #F8F8F8;
            color: #A0A0A0;
        }

        #zoomComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: center right;
            width: 18px;
            border: none;
            background-color: transparent;
            border-top-right-radius: 4px;
            border-bottom-right-radius: 4px;
        }

        #zoomComboBox::drop-down:hover {
            background-color: #F0F0F0;
        }

        #zoomComboBox::down-arrow {
            image: url(:/icons/icons/expand.png);
            width: 10px;
            height: 10px;
        }

        #zoomComboBox QAbstractItemView {
            background-color: #FFFFFF;
            border: 1px solid #D0D0D0;
            border-radius: 6px;
            padding: 4px;
            selection-background-color: #4A4A4A;
            selection-color: #FFFFFF;
        }

        /* 状态栏 */
        #modernStatusBar {
            background-color: #F8F8F8;
            border-top: 1px solid #E0E0E0;
            color: #6B6B6B;
            font-size: 12px;
        }

        #modernStatusBar QLabel {
            color: #6B6B6B;
            padding: 0px 8px;
        }

        #statusLabel {
            color: #3A3A3A;
            font-weight: 500;
        }

        #pageLabel, #zoomLabel {
            background-color: #EFEFEF;
            border: 1px solid #DBDBDB;
            border-radius: 4px;
            padding: 4px 12px;
            color: #2C2C2C;
            font-weight: 500;
        }

        /* 标签页容器 */
        QTabWidget::pane {
            border: none;
            background-color: #FAFAFA;
        }

        QTabBar::tab {
            background-color: #E8E8E8;
            color: #2C2C2C;
            padding: 8px 16px;
            margin-right: 2px;
            border-top-left-radius: 6px;
            border-top-right-radius: 6px;
            min-width: 100px;
            max-width: 200px;
            font-size: 13px;
        }

        QTabBar::tab:selected {
            background-color: #FFFFFF;
            color: #000000;
            font-weight: 500;
        }

        QTabBar::tab:hover:!selected {
            background-color: #F0F0F0;
        }

        QTabBar::close-button {
            image: url(:/icons/icons/close.png);
            subcontrol-position: right;
            margin: 2px;
        }

        QTabBar::close-button:hover {
            background-color: #D0D0D0;
            border-radius: 3px;
        }

        /* 滚动条 */
        QScrollBar:vertical {
            background: #F5F5F5;
            width: 12px;
            margin: 0px;
            border-left: 1px solid #E5E5E5;
        }

        QScrollBar::handle:vertical {
            background: #C0C0C0;
            border-radius: 6px;
            min-height: 30px;
            margin: 2px;
        }

        QScrollBar::handle:vertical:hover {
            background: #A0A0A0;
        }

        QScrollBar::handle:vertical:pressed {
            background: #808080;
        }

        QScrollBar::add-line:vertical,
        QScrollBar::sub-line:vertical {
            height: 0px;
        }

        QScrollBar::add-page:vertical,
        QScrollBar::sub-page:vertical {
            background: transparent;
        }

        QScrollBar:horizontal {
            background: #F5F5F5;
            height: 12px;
            margin: 0px;
            border-top: 1px solid #E5E5E5;
        }

        QScrollBar::handle:horizontal {
            background: #C0C0C0;
            border-radius: 6px;
            min-width: 30px;
            margin: 2px;
        }

        QScrollBar::handle:horizontal:hover {
            background: #A0A0A0;
        }

        QScrollBar::handle:horizontal:pressed {
            background: #808080;
        }

        QScrollBar::add-line:horizontal,
        QScrollBar::sub-line:horizontal {
            width: 0px;
        }

        QScrollBar::add-page:horizontal,
        QScrollBar::sub-page:horizontal {
            background: transparent;
        }

        /* 提示框 */
        QToolTip {
            background-color: #2C2C2C;
            color: #FFFFFF;
            border: 1px solid #1C1C1C;
            border-radius: 4px;
            padding: 4px 8px;
            font-size: 12px;
        }
    )";

    setStyleSheet(style);
}
