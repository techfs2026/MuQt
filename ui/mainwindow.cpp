#include "mainwindow.h"
#include "pdfdocumenttab.h"
#include "dictionaryconnector.h"
#include "ocrstatusindicator.h"
#include "ocrmanager.h"
#include "chinesetokenizer.h"
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
#include <QActionGroup>

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
    , m_ocrInitialized(false)
{
    setWindowTitle(tr("MuQt"));
    resize(AppConfig::instance().defaultWindowSize());

    // 创建标签页容器
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    m_tabWidget->setDocumentMode(true);
    m_tabWidget->setUsesScrollButtons(true);
    m_tabWidget->tabBar()->setExpanding(false);

    setCentralWidget(m_tabWidget);

    m_navigationDock = new QDockWidget(tr("导航"), this);
    m_navigationDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_navigationDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    addDockWidget(Qt::LeftDockWidgetArea, m_navigationDock);
    m_navigationDock->setVisible(false);

    // 创建UI组件
    createActions();
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

    // 检查GoldenDict是否可用
    if (!DictionaryConnector::instance().isGoldenDictAvailable()) {
        qWarning() << "GoldenDict not found, lookup feature will not work";
    }
}

MainWindow::~MainWindow()
{
    // 关闭所有标签页
    while (m_tabWidget->count() > 0) {
        closeTab(0);
    }
}


void MainWindow::openFile()
{
    QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("打开PDF文件"),
        QString(),
        tr("PDF Files (*.pdf);;All Files (*.*)")
        );

    if (filePath.isEmpty()) {
        return;
    }

    PDFDocumentTab* tab = currentTab();

    // 如果没有标签页或当前标签页已加载,创建新标签页
    if (!tab || tab->isDocumentLoaded()) {
        tab = createNewTab();
    }

    QString errorMsg;
    if (!tab->loadDocument(filePath, &errorMsg)) {
        QMessageBox::critical(this, tr("错误"),
                              tr("打开失败:\n%1\n\n错误: %2")
                                  .arg(filePath).arg(errorMsg));

        // 如果加载失败,清理标签页
        if (m_tabWidget->count() > 1) {
            int index = m_tabWidget->indexOf(tab);
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

    disconnectTabSignals(tab);

    if (tab == currentTab() && m_navigationDock) {
        m_navigationDock->setWidget(nullptr);
        m_navigationDock->setVisible(false);
    }

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

    if (tab && tab->isDocumentLoaded()) {
        // 切换到已加载文档的标签页

        if (tab->navigationPanel()) {
            m_navigationDock->setWidget(tab->navigationPanel());

            bool shouldShow = m_showNavigationAction->isChecked();
            m_navigationDock->setVisible(shouldShow);
            m_navPanelAction->setChecked(shouldShow);
        }

        bool canEnhance = !tab->isTextPDF();
        m_paperEffectAction->setEnabled(canEnhance);
        m_paperEffectAction->setChecked(canEnhance && tab->paperEffectEnabled());
        if (tab->isTextPDF()) {
            m_paperEffectAction->setToolTip(tr("纸质书印刷效果增强（仅适用于扫描版 PDF）"));
        } else {
            m_paperEffectAction->setToolTip(tr("纸质书印刷效果增强"));
        }

        bool isScannedPDF = !tab->isTextPDF();
        bool ocrReady = (OCRManager::instance().engineState() == OCREngineState::Ready);

        if (m_ocrHoverAction) {
            m_ocrHoverAction->setEnabled(isScannedPDF && ocrReady);
            m_ocrHoverAction->setChecked(OCRManager::instance().isOCRHoverEnabled());
        }
    } else {
        // 无文档或无 tab,隐藏导航面板
        m_navigationDock->setWidget(nullptr);
        m_navigationDock->setVisible(false);
        m_showNavigationAction->setChecked(false);
        m_navPanelAction->setChecked(false);

        if (m_ocrHoverAction) {
            m_ocrHoverAction->setEnabled(false);
            m_ocrHoverAction->setChecked(false);
        }
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
        QString fullTitle = tab->documentTitle();
        QString displayTitle = fullTitle;

        // 截断过长的文件名
        const int maxLength = 20; // 可调整的最大长度
        if (displayTitle.length() > maxLength) {
            // 保留文件扩展名
            QFileInfo fileInfo(fullTitle);
            QString baseName = fileInfo.completeBaseName(); // 不含扩展名的文件名
            QString extension = fileInfo.suffix(); // 扩展名

            if (!extension.isEmpty()) {
                // 计算可用于基础文件名的长度（预留扩展名和省略号的空间）
                int availableLength = maxLength - extension.length() - 4; // "..." + "."

                if (baseName.length() > availableLength) {
                    baseName = baseName.left(availableLength);
                    displayTitle = baseName + "..." + "." + extension;
                } else {
                    displayTitle = fullTitle;
                }
            } else {
                // 没有扩展名的情况
                displayTitle = displayTitle.left(maxLength - 3) + "...";
            }
        }

        m_tabWidget->setTabText(index, displayTitle);
        m_tabWidget->setTabToolTip(index, tab->documentPath()); // 完整路径显示在tooltip
    }
}


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

    // 切换可见性
    bool visible = !m_navigationDock->isVisible();

    // 如果要显示,先确保设置了正确的widget
    if (visible && tab->navigationPanel()) {
        m_navigationDock->setWidget(tab->navigationPanel());
    }

    m_navigationDock->setVisible(visible);
    m_navPanelAction->setChecked(visible);
    m_showNavigationAction->setChecked(visible);

    // 延迟更新缩放
    QTimer::singleShot(0, this, [tab]() {
        ZoomMode mode = tab->zoomMode();
        if (mode == ZoomMode::FitWidth || mode == ZoomMode::FitPage) {
            QSize viewportSize = tab->getViewportSize();
            tab->updateZoom(viewportSize);
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


void MainWindow::showSearchBar()
{
    if (PDFDocumentTab* tab = currentTab()) {
        tab->showSearchBar();
    }
}

void MainWindow::findNext()
{
    if (PDFDocumentTab* tab = currentTab()) {
        tab->findNext();
    }
}

void MainWindow::findPrevious()
{
    if (PDFDocumentTab* tab = currentTab()) {
        tab->findPrevious();
    }
}

// ========== 文本操作 ==========

void MainWindow::copySelectedText()
{
    if (PDFDocumentTab* tab = currentTab()) {
        tab->copySelectedText();
    }
}


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

    // 同步缩放模式按钮状态
    updateUIState();
}

void MainWindow::updateZoomCombox(double zoom)
{
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

    updateUIState();
}

void MainWindow::onCurrentTabContinuousScrollChanged(bool continuous)
{
    PDFDocumentTab* sender = qobject_cast<PDFDocumentTab*>(QObject::sender());
    if (sender != currentTab()) {
        return;
    }

    updateUIState();
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

    // 如果是当前标签页,更新UI
    if (tab == currentTab()) {
        updateWindowTitle();
        updateUIState();

        // 设置导航面板
        if (tab->isDocumentLoaded() && tab->navigationPanel()) {
            m_navigationDock->setWidget(tab->navigationPanel());

            // 文档加载时默认显示导航面板
            m_navigationDock->setVisible(true);
            m_showNavigationAction->setChecked(true);
            m_navPanelAction->setChecked(true);
        }

        bool canEnhance = !tab->isTextPDF();
        m_paperEffectAction->setEnabled(canEnhance);

        // 如果是文本 PDF，确保增强功能关闭
        if (tab->isTextPDF()) {
            m_paperEffectAction->setChecked(false);
        }

        // 更新OCR按钮状态
        bool isScannedPDF = !tab->isTextPDF();
        bool ocrReady = (OCRManager::instance().engineState() == OCREngineState::Ready);

        if (m_ocrHoverAction) {
            m_ocrHoverAction->setEnabled(isScannedPDF && ocrReady);

            if (!isScannedPDF) {
                m_ocrHoverAction->setToolTip(
                    tr("OCR悬停取词\n(当前是文本PDF，不需要OCR)"));
                m_ocrHoverAction->setChecked(false);
            } else if (!ocrReady) {
                m_ocrHoverAction->setToolTip(
                    tr("OCR悬停取词\n(OCR引擎未就绪)"));
            } else {
                m_ocrHoverAction->setToolTip(
                    tr("启用OCR悬停取词功能\n(仅扫描版PDF)"));
            }
        }
    }
    // 如果不是当前标签页,但该标签页的文档已加载
    // 不做任何操作,等待用户切换到该标签页时再更新导航面板
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

void MainWindow::createActions()
{

    m_openAction = new QAction(QIcon(":icons/resources/icons/open-file.png"),
                               tr("打开"), this);
    m_openAction->setShortcut(QKeySequence::Open);
    m_openAction->setToolTip(tr("打开文件 (Ctrl+O)"));
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openFile);

    m_closeAction = new QAction(tr("关闭"), this);
    m_closeAction->setShortcut(QKeySequence::Close);
    connect(m_closeAction, &QAction::triggered, this, &MainWindow::closeCurrentTab);

    m_quitAction = new QAction(tr("退出"), this);
    m_quitAction->setShortcut(QKeySequence::Quit);
    connect(m_quitAction, &QAction::triggered, this, &MainWindow::quit);


    m_copyAction = new QAction(tr("复制"), this);
    m_copyAction->setShortcut(QKeySequence::Copy);
    m_copyAction->setEnabled(false);
    connect(m_copyAction, &QAction::triggered, this, &MainWindow::copySelectedText);

    m_findAction = new QAction(QIcon(":icons/resources/icons/search.png"),
                               tr("查找"), this);
    m_findAction->setShortcut(QKeySequence::Find);
    m_findAction->setToolTip(tr("搜索 (Ctrl+F)"));
    connect(m_findAction, &QAction::triggered, this, &MainWindow::showSearchBar);

    m_findNextAction = new QAction(tr("查找下一个"), this);
    m_findNextAction->setShortcut(QKeySequence::FindNext);
    m_findNextAction->setEnabled(false);
    connect(m_findNextAction, &QAction::triggered, this, &MainWindow::findNext);

    m_findPreviousAction = new QAction(tr("查找上一个"), this);
    m_findPreviousAction->setShortcut(QKeySequence::FindPrevious);
    m_findPreviousAction->setEnabled(false);
    connect(m_findPreviousAction, &QAction::triggered, this, &MainWindow::findPrevious);


    m_firstPageAction = new QAction(QIcon(":icons/resources/icons/first-arrow.png"),
                                    tr("首页"), this);
    m_firstPageAction->setToolTip(tr("首页 (Home)"));
    connect(m_firstPageAction, &QAction::triggered, this, &MainWindow::firstPage);

    m_previousPageAction = new QAction(QIcon(":icons/resources/icons/left-arrow.png"),
                                       tr("上一页"), this);
    m_previousPageAction->setToolTip(tr("上一页 (PgUp)"));
    connect(m_previousPageAction, &QAction::triggered, this, &MainWindow::previousPage);

    m_nextPageAction = new QAction(QIcon(":icons/resources/icons/right-arrow.png"),
                                   tr("下一页"), this);
    m_nextPageAction->setToolTip(tr("下一页 (PgDown)"));
    connect(m_nextPageAction, &QAction::triggered, this, &MainWindow::nextPage);

    m_lastPageAction = new QAction(QIcon(":icons/resources/icons/last-arrow.png"),
                                   tr("尾页"), this);
    m_lastPageAction->setToolTip(tr("尾页 (End)"));
    connect(m_lastPageAction, &QAction::triggered, this, &MainWindow::lastPage);

    m_zoomInAction = new QAction(QIcon(":icons/resources/icons/zoom-in.png"),
                                 tr("放大"), this);
    m_zoomInAction->setShortcut(QKeySequence::ZoomIn);
    m_zoomInAction->setToolTip(tr("放大 (Ctrl++)"));
    connect(m_zoomInAction, &QAction::triggered, this, &MainWindow::zoomIn);

    m_zoomOutAction = new QAction(QIcon(":icons/resources/icons/zoom-out.png"),
                                  tr("缩小"), this);
    m_zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    m_zoomOutAction->setToolTip(tr("缩小 (Ctrl+-)"));
    connect(m_zoomOutAction, &QAction::triggered, this, &MainWindow::zoomOut);

    m_fitPageAction = new QAction(QIcon(":icons/resources/icons/fit-to-page.png"),
                                  tr("适应页面"), this);
    m_fitPageAction->setShortcut(tr("Ctrl+1"));
    m_fitPageAction->setToolTip(tr("适应页面 (Ctrl+1)"));
    m_fitPageAction->setCheckable(true);
    connect(m_fitPageAction, &QAction::triggered, this, &MainWindow::fitPage);

    m_fitWidthAction = new QAction(QIcon(":icons/resources/icons/fit-to-width.png"),
                                   tr("适应宽度"), this);
    m_fitWidthAction->setShortcut(tr("Ctrl+2"));
    m_fitWidthAction->setToolTip(tr("适应宽度 (Ctrl+2)"));
    m_fitWidthAction->setCheckable(true);
    connect(m_fitWidthAction, &QAction::triggered, this, &MainWindow::fitWidth);


    m_pageModeGroup = new QActionGroup(this);
    m_pageModeGroup->setExclusive(true);

    m_singlePageAction = new QAction(QIcon(":icons/resources/icons/single-page-mode.png"),
                                     tr("单页"), this);
    m_singlePageAction->setCheckable(true);
    m_singlePageAction->setChecked(true);
    m_pageModeGroup->addAction(m_singlePageAction);
    connect(m_singlePageAction, &QAction::triggered, this, [this]() {
        togglePageMode(PageDisplayMode::SinglePage);
    });

    m_doublePageAction = new QAction(QIcon(":icons/resources/icons/double-page-mode.png"),
                                     tr("双页"), this);
    m_doublePageAction->setCheckable(true);
    m_pageModeGroup->addAction(m_doublePageAction);
    connect(m_doublePageAction, &QAction::triggered, this, [this]() {
        togglePageMode(PageDisplayMode::DoublePage);
    });

    m_continuousScrollAction = new QAction(QIcon(":icons/resources/icons/continuous-mode.png"),
                                           tr("连续滚动"), this);
    m_continuousScrollAction->setCheckable(true);
    m_continuousScrollAction->setChecked(true);
    connect(m_continuousScrollAction, &QAction::triggered,
            this, &MainWindow::toggleContinuousScroll);


    m_navPanelAction = new QAction(QIcon(":icons/resources/icons/sidebar.png"),
                                   tr("导航面板"), this);
    m_navPanelAction->setToolTip(tr("显示导航栏 (F9)"));
    m_navPanelAction->setCheckable(true);
    connect(m_navPanelAction, &QAction::triggered,
            this, &MainWindow::toggleNavigationPanel);

    m_showNavigationAction = m_navPanelAction;  // 菜单和工具栏共用同一个
    m_showNavigationAction->setShortcut(tr("F9"));

    m_showLinksAction = new QAction(tr("显示链接边框"), this);
    m_showLinksAction->setCheckable(true);
    m_showLinksAction->setChecked(true);
    connect(m_showLinksAction, &QAction::triggered,
            this, &MainWindow::toggleLinksVisible);


    m_paperEffectAction = new QAction(QIcon(":icons/resources/icons/paper-effect.png"),
                                      tr("纸质增强"), this);
    m_paperEffectAction->setToolTip(tr("魔法！护眼纸质感效果增强"));
    m_paperEffectAction->setCheckable(true);
    m_paperEffectAction->setChecked(false);
    connect(m_paperEffectAction, &QAction::triggered,
            this, &MainWindow::togglePaperEffect);

    m_ocrHoverAction = new QAction(QIcon(":icons/resources/icons/ocr.png"),
                                   tr("OCR取词"), this);
    m_ocrHoverAction->setShortcut(QKeySequence(tr("Ctrl+Shift+O")));
    m_ocrHoverAction->setToolTip(tr("启用OCR取词模式 (Ctrl+Shift+O)\n"
                                    "启用后按 Ctrl+Q 触发识别\n"
                                    "(仅扫描版PDF)"));
    m_ocrHoverAction->setCheckable(true);
    m_ocrHoverAction->setChecked(false);
    m_ocrHoverAction->setEnabled(false);
    connect(m_ocrHoverAction, &QAction::triggered,
            this, &MainWindow::toggleOCRHover);
}

void MainWindow::createMenuBar()
{
    menuBar()->setNativeMenuBar(false);

    // 文件菜单
    QMenu* fileMenu = menuBar()->addMenu(tr("&文件"));
    fileMenu->addAction(m_openAction);
    fileMenu->addAction(m_closeAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_quitAction);

    // 编辑菜单
    QMenu* editMenu = menuBar()->addMenu(tr("&编辑"));
    editMenu->addAction(m_copyAction);
    editMenu->addSeparator();
    editMenu->addAction(m_findAction);
    editMenu->addAction(m_findNextAction);
    editMenu->addAction(m_findPreviousAction);

    // 视图菜单
    QMenu* viewMenu = menuBar()->addMenu(tr("&视图"));
    viewMenu->addAction(m_zoomInAction);
    viewMenu->addAction(m_zoomOutAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_fitPageAction);
    viewMenu->addAction(m_fitWidthAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_singlePageAction);
    viewMenu->addAction(m_doublePageAction);
    viewMenu->addAction(m_continuousScrollAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_showNavigationAction);
    viewMenu->addAction(m_showLinksAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_ocrHoverAction);
}

void MainWindow::createToolBar()
{
    m_toolBar = addToolBar(tr(""));
    m_toolBar->setMovable(false);
    m_toolBar->setFloatable(false);
    m_toolBar->setIconSize(QSize(20, 20));
    m_toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_toolBar->setContentsMargins(0, 0, 0, 0);
    m_toolBar->setObjectName("mainToolBar");

    // 导航面板
    m_toolBar->addAction(m_navPanelAction);
    m_toolBar->addSeparator();

    // 文件操作
    m_toolBar->addAction(m_openAction);
    m_toolBar->addSeparator();

    // 页面导航
    m_toolBar->addAction(m_firstPageAction);
    m_toolBar->addAction(m_previousPageAction);

    // 页码输入框
    m_pageSpinBox = new QSpinBox(this);
    m_pageSpinBox->setMinimum(1);
    m_pageSpinBox->setMaximum(1);
    m_pageSpinBox->setEnabled(false);
    m_pageSpinBox->setAlignment(Qt::AlignCenter);
    m_pageSpinBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_pageSpinBox->setObjectName("pageSpinBox");
    connect(m_pageSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::goToPage);
    m_toolBar->addWidget(m_pageSpinBox);

    m_toolBar->addAction(m_nextPageAction);
    m_toolBar->addAction(m_lastPageAction);
    m_toolBar->addSeparator();

    // 缩放操作
    m_toolBar->addAction(m_zoomOutAction);

    m_zoomComboBox = new QComboBox(this);
    m_zoomComboBox->setEditable(true);
    m_zoomComboBox->setObjectName("zoomComboBox");
    m_zoomComboBox->addItems({
        "25%", "50%", "75%", "100%", "125%", "150%", "200%", "300%", "400%"
    });
    m_zoomComboBox->setCurrentText("100%");
    connect(m_zoomComboBox, &QComboBox::currentTextChanged,
            this, &MainWindow::onZoomComboChanged);
    m_toolBar->addWidget(m_zoomComboBox);

    m_toolBar->addAction(m_zoomInAction);
    m_toolBar->addSeparator();

    // 缩放模式
    m_toolBar->addAction(m_fitPageAction);
    m_toolBar->addAction(m_fitWidthAction);
    m_toolBar->addSeparator();

    // 页面模式
    m_toolBar->addAction(m_singlePageAction);
    m_toolBar->addAction(m_doublePageAction);
    m_toolBar->addAction(m_continuousScrollAction);
    m_toolBar->addSeparator();

    // 特殊功能
    m_toolBar->addAction(m_paperEffectAction);
    m_toolBar->addAction(m_ocrHoverAction);

    // 弹性空间
    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolBar->addWidget(spacer);

    // 搜索
    m_toolBar->addAction(m_findAction);
}

void MainWindow::createStatusBar()
{
    statusBar()->setObjectName("modernStatusBar");
    statusBar()->setSizeGripEnabled(true);

    m_statusLabel = new QLabel(tr(""));
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

    // 在右下角添加OCR状态指示器
    m_ocrIndicator = new OCRStatusIndicator(this);
    statusBar()->addPermanentWidget(m_ocrIndicator);

    // 双击指示器可以查看详细信息
    connect(m_ocrIndicator, &OCRStatusIndicator::doubleClicked,
            this, [this]() {
                QString message;
                OCREngineState state = OCRManager::instance().engineState();

                switch (state) {
                case OCREngineState::Uninitialized:
                    message = tr("OCR功能未初始化\n\n"
                                 "首次使用时会自动加载模型，请稍候。");
                    break;
                case OCREngineState::Loading:
                    message = tr("OCR模型正在加载中...\n\n"
                                 "这可能需要几秒钟时间。");
                    break;
                case OCREngineState::Ready:
                    message = tr("OCR功能已就绪\n\n"
                                 "您可以在扫描版PDF上使用悬停取词功能。");
                    break;
                case OCREngineState::Error:
                    message = tr("OCR初始化失败\n\n"
                                 "错误: %1\n\n"
                                 "请检查模型文件是否存在。")
                                  .arg(OCRManager::instance().lastError());
                    break;
                }

                QMessageBox::information(this, tr("OCR状态"), message);
            });

    // 连接OCR状态变化
    connect(&OCRManager::instance(), &OCRManager::engineStateChanged,
            this, &MainWindow::onOCREngineStateChanged);

    connect(&OCRManager::instance(), &OCRManager::ocrHoverEnabledChanged,
            this, &MainWindow::onOCRHoverEnabledChanged);

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
                QSize viewportSize = tab->getViewportSize();
                tab->updateZoom(viewportSize);
            }
        }
    });
}


void MainWindow::updateUIState()
{
    PDFDocumentTab* tab = currentTab();
    bool hasDocument = tab && tab->isDocumentLoaded();
    int pageCount = hasDocument ? tab->pageCount() : 0;
    int currentPage = hasDocument ? tab->currentPage() : 0;
    double zoom = hasDocument ? tab->zoom() : 1.0;
    bool continuousScroll = hasDocument ? tab->isContinuousScroll() : true;
    PageDisplayMode displayMode = hasDocument ? tab->displayMode() : PageDisplayMode::SinglePage;
    ZoomMode zoomMode = hasDocument ? tab->zoomMode() : ZoomMode::FitWidth;
    bool canEnhance = hasDocument && !tab->isTextPDF();
    bool canOCR = hasDocument && !tab->isTextPDF();

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
    m_fitPageAction->setEnabled(hasDocument);
    m_fitWidthAction->setEnabled(hasDocument);

    // 同步缩放模式按钮状态
    m_fitPageAction->setEnabled(hasDocument && zoomMode != ZoomMode::FitPage);
    m_fitPageAction->setChecked(hasDocument && zoomMode == ZoomMode::FitPage);
    m_fitWidthAction->setEnabled(hasDocument && zoomMode != ZoomMode::FitWidth);
    m_fitWidthAction->setChecked(hasDocument && zoomMode == ZoomMode::FitWidth);

    // 视图操作 - 菜单
    m_singlePageAction->setEnabled(hasDocument);
    m_doublePageAction->setEnabled(hasDocument);
    m_continuousScrollAction->setEnabled(hasDocument && displayMode == PageDisplayMode::SinglePage);

    m_singlePageAction->setChecked(hasDocument && displayMode == PageDisplayMode::SinglePage);
    m_doublePageAction->setChecked(hasDocument && displayMode == PageDisplayMode::DoublePage);
    m_continuousScrollAction->setChecked(hasDocument && continuousScroll);

    // 纸质增强按钮
    m_paperEffectAction->setEnabled(canEnhance);
    // 修改图标或样式以提示不可用原因
    if (hasDocument && !canEnhance) {
        m_paperEffectAction->setToolTip(
            tr("纸质书印刷效果增强\n"
               "（当前是原生文本 PDF，此功能不适用）")
            );
    } else if (canEnhance) {
        m_paperEffectAction->setToolTip(tr("纸质书印刷效果增强"));
    } else {
        m_paperEffectAction->setToolTip(tr("纸质书印刷效果增强（需要打开文档）"));
    }
    if (hasDocument) {
        m_paperEffectAction->setChecked(tab->paperEffectEnabled());

        // 如果切换到文本 PDF，自动禁用增强
        if (tab->isTextPDF() && tab->paperEffectEnabled()) {
            tab->setPaperEffectEnabled(false);
        }
    }

    // OCR 悬停取词按钮
    bool ocrCanEnable = hasDocument && !tab->isTextPDF();
    bool ocrReady = (OCRManager::instance().engineState() == OCREngineState::Ready);
    m_ocrHoverAction->setEnabled(ocrCanEnable);

    // 更新工具提示
    if (!hasDocument) {
        m_ocrHoverAction->setToolTip(tr("OCR取词 (Ctrl+Shift+O)\n(需要打开文档)"));
    } else if (!ocrCanEnable) {
        m_ocrHoverAction->setToolTip(tr("OCR取词 (Ctrl+Shift+O)\n(当前是文本PDF,不需要OCR)"));
    } else if (!ocrReady) {
        OCREngineState state = OCRManager::instance().engineState();
        if (state == OCREngineState::Loading) {
            m_ocrHoverAction->setToolTip(tr("OCR取词 (Ctrl+Shift+O)\n(OCR引擎加载中...)"));
        } else if (state == OCREngineState::Error) {
            m_ocrHoverAction->setToolTip(tr("OCR取词 (Ctrl+Shift+O)\n(OCR引擎初始化失败)"));
        } else {
            m_ocrHoverAction->setToolTip(tr("OCR取词 (Ctrl+Shift+O)\n(OCR引擎未就绪)"));
        }
    } else {
        m_ocrHoverAction->setToolTip(tr("启用OCR取词模式 (Ctrl+Shift+O)\n"
                                        "启用后按 Ctrl+Q 触发识别\n"
                                        "(仅扫描版PDF)"));
    }

    // 同步勾选状态
    if (hasDocument) {
        m_ocrHoverAction->setChecked(OCRManager::instance().isOCRHoverEnabled());  // 修改
    } else {
        m_ocrHoverAction->setChecked(false);
    }

    // 导航面板
    m_showNavigationAction->setEnabled(hasDocument);
    m_showLinksAction->setEnabled(hasDocument);

    // 工具栏组件
    m_navPanelAction->setEnabled(hasDocument);
    m_navPanelAction->setChecked(m_navigationDock->isVisible());

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
    QString title = tr("MuQt");

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
        m_statusLabel->setText(tr("请打开PDF文件查看"));
        return;
    }

    int currentPage = tab->currentPage() + 1;
    int pageCount = tab->pageCount();
    m_pageLabel->setText(tr("📄 %1 / %2").arg(currentPage).arg(pageCount));

    double zoom = tab->zoom();
    QString zoomMode;
    switch (tab->zoomMode()) {
    case ZoomMode::FitPage:
        zoomMode = tr(" (适合页面)");
        break;
    case ZoomMode::FitWidth:
        zoomMode = tr(" (适合宽度)");
        break;
    default:
        break;
    }
    m_zoomLabel->setText(tr("🔍 %1%%2").arg(qRound(zoom * 100)).arg(zoomMode));

    if (tab->hasTextSelection()) {
        m_statusLabel->setText(tr("文本已选择"));
    } else {
        m_statusLabel->setText(tr(""));
    }
}

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
    QFile styleFile(":styles/resources/styles/main.qss");
    if (styleFile.open(QFile::ReadOnly)) {
        QString style = QLatin1String(styleFile.readAll());
        setStyleSheet(style);
        styleFile.close();
    }
}

void MainWindow::togglePaperEffect()
{
    PDFDocumentTab* tab = currentTab();
    if (!tab || !tab->isDocumentLoaded()) {
        return;
    }

    // 添加文本 PDF 检查
    if (tab->isTextPDF()) {
        QMessageBox::information(this, tr("功能不可用"),
                                 tr("纸质增强效果仅适用于扫描版 PDF。\n"
                                    "当前文档是原生文本 PDF，不需要此功能。"));
        m_paperEffectAction->setChecked(false);
        return;
    }

    bool enabled = m_paperEffectAction->isChecked();
    tab->setPaperEffectEnabled(enabled);
}

void MainWindow::toggleOCRHover()
{
    bool enabled = m_ocrHoverAction->isChecked();

    PDFDocumentTab* tab = currentTab();
    if (!tab || !tab->isDocumentLoaded()) {
        m_ocrHoverAction->setChecked(false);
        return;
    }

    // 只对扫描版PDF可用
    if (tab->isTextPDF()) {
        QMessageBox::information(this, tr("功能不可用"),
                                 tr("OCR取词仅适用于扫描版PDF。\n"
                                    "当前文档是原生文本PDF,请直接选择文字。"));
        m_ocrHoverAction->setChecked(false);
        return;
    }

    // 首次启用时初始化OCR
    if (enabled && !m_ocrInitialized) {
        initializeOCRManager();
    }

    // 等待OCR就绪
    if (enabled && !OCRManager::instance().isReady()) {
        OCREngineState state = OCRManager::instance().engineState();

        if (state == OCREngineState::Loading) {
            QMessageBox::information(this, tr("请稍候"),
                                     tr("OCR模型正在加载中,请稍候...\n\n"
                                        "加载完成后会自动启用取词功能。"));
            m_ocrHoverAction->setChecked(false);
            return;
        } else if (state == OCREngineState::Error) {
            QMessageBox::critical(this, tr("OCR初始化失败"),
                                  tr("OCR引擎初始化失败:\n%1\n\n"
                                     "请检查模型文件是否存在于 models 目录。")
                                      .arg(OCRManager::instance().lastError()));
            m_ocrHoverAction->setChecked(false);
            return;
        }
    }

    // 使用全局OCRManager设置状态 (修改)
    OCRManager::instance().setOCRHoverEnabled(enabled);

    // 显示使用提示
    if (enabled) {
        QMessageBox::information(this, tr("OCR取词已启用"),
                                 tr("OCR悬停取词已启用!\n\n"
                                    "使用方法:\n"
                                    "1. 将鼠标移动到要识别的文字位置\n"
                                    "2. 按下 Ctrl+Q 快捷键触发识别\n"
                                    "3. 识别结果会在浮窗中显示\n"
                                    "4. 点击浮窗可查询词典\n"
                                    "5. 再次点击工具栏按钮可关闭OCR\n\n"
                                    "提示: 可在状态栏查看OCR引擎状态"));
    }
}


void MainWindow::initializeOCRManager()
{
    if (m_ocrInitialized) {
        return;
    }

    QString modelDir = AppConfig::instance().ocrModelDir();
    QString dictDir = AppConfig::instance().jiebaDictDir();

    qInfo() << "MainWindow: Initializing OCR with model dir:" << modelDir;
    qInfo() << "MainWindow: Initializing Jieba with dict dir:" << dictDir;

    // 初始化分词器
    if (!ChineseTokenizer::instance().isInitialized()) {
        bool jiebaOk = ChineseTokenizer::instance().initialize(dictDir);
        if (!jiebaOk) {
            qWarning() << "Failed to initialize Jieba:"
                       << ChineseTokenizer::instance().lastError();
            QMessageBox::warning(this, tr("分词器初始化失败"),
                                 tr("中文分词功能初始化失败:\n%1\n\nOCR识别将使用全部文本。")
                                     .arg(ChineseTokenizer::instance().lastError()));
        }
    }

    // 初始化OCR
    bool started = OCRManager::instance().initialize(modelDir);

    if (started) {
        m_ocrInitialized = true;
        qInfo() << "MainWindow: OCR initialization started";
    } else {
        qWarning() << "MainWindow: Failed to start OCR initialization";
    }
}

void MainWindow::onOCREngineStateChanged(OCREngineState state)
{
    // 更新状态指示器
    if (m_ocrIndicator) {
        m_ocrIndicator->setState(state);
    }

    // 根据状态更新按钮可用性
    PDFDocumentTab* tab = currentTab();
    bool canEnable = (state == OCREngineState::Ready) &&
                     tab &&
                     tab->isDocumentLoaded() &&
                     !tab->isTextPDF();

    if (m_ocrHoverAction) {
        m_ocrHoverAction->setEnabled(canEnable);
    }

    // 如果初始化失败，显示错误
    if (state == OCREngineState::Error) {
        QString error = OCRManager::instance().lastError();
        qWarning() << "OCR initialization failed:" << error;

        // 取消勾选
        if (m_ocrHoverAction) {
            m_ocrHoverAction->setChecked(false);
        }
    }

    // 如果初始化成功且用户已经尝试启用，自动启用 (修改)
    if (state == OCREngineState::Ready &&
        m_ocrHoverAction &&
        m_ocrHoverAction->isChecked()) {

        OCRManager::instance().setOCRHoverEnabled(true);
    }
}

void MainWindow::onOCRHoverEnabledChanged(bool enabled)
{
    // 同步UI状态
    if (m_ocrHoverAction) {
        m_ocrHoverAction->setChecked(enabled);
    }

    // 通知所有Tab更新
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        PDFDocumentTab* tab = qobject_cast<PDFDocumentTab*>(m_tabWidget->widget(i));
        if (tab && tab->isDocumentLoaded() && !tab->isTextPDF()) {
            tab->updateOCRHoverState();
        }
    }

    qInfo() << "OCR hover state changed to:" << enabled;
}
