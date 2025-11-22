#include "mainwindow.h"
#include "pdfdocumentsession.h"
#include "pdfpagewidget.h"
#include "navigationpanel.h"
#include "searchwidget.h"
#include "appconfig.h"
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QLabel>
#include <QSpinBox>
#include <QComboBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QApplication>
#include <QFileInfo>
#include <QCloseEvent>
#include <QProgressBar>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_session(nullptr)
    , m_pageWidget(nullptr)
    , m_scrollArea(nullptr)
    , m_toolBar(nullptr)
    , m_pageSpinBox(nullptr)
    , m_zoomComboBox(nullptr)
    , m_statusLabel(nullptr)
    , m_pageLabel(nullptr)
    , m_zoomLabel(nullptr)
    , m_navigationPanel(nullptr)
    , m_showNavigationAction(nullptr)
    , m_showLinksAction(nullptr)
    , m_copyAction(nullptr)
{
    setWindowTitle(tr("Simple PDF Viewer"));

    // 应用初始配置
    applyInitialSettings();

    // 创建Session(核心)
    m_session = new PDFDocumentSession(this);

    // 创建滚动区域
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(false);
    m_scrollArea->setAlignment(Qt::AlignCenter);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    // 创建页面显示组件(依赖Session)
    m_pageWidget = new PDFPageWidget(m_session, this);
    m_scrollArea->setWidget(m_pageWidget);

    QTimer::singleShot(0, this, [this]() {
        if (!m_session->isDocumentLoaded() && m_scrollArea && m_scrollArea->viewport() && m_pageWidget) {
            QSize viewportSize = m_scrollArea->viewport()->size();
            m_pageWidget->resize(viewportSize);
        }
    });

    // 创建搜索工具栏
    m_searchWidget = new SearchWidget(m_session->interactionHandler(), m_pageWidget, parent);

    // 创建进度条
    m_textPreloadProgress = new QProgressBar(this);
    m_textPreloadProgress->setMaximumWidth(200);
    m_textPreloadProgress->setMaximumHeight(20);
    m_textPreloadProgress->setVisible(false);
    m_textPreloadProgress->setTextVisible(true);
    m_textPreloadProgress->setAlignment(Qt::AlignCenter);

    // 设置为中心窗口
    setCentralWidget(m_scrollArea);

    // 创建导航面板(依赖Session)
    m_navigationPanel = new NavigationPanel(m_session, this);
    addDockWidget(Qt::LeftDockWidgetArea, m_navigationPanel);
    m_navigationPanel->show();

    m_navigationPanel->installEventFilter(this);

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

    // 加载上次会话
    loadLastSession();
}

MainWindow::~MainWindow()
{
    saveCurrentSession();
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

    QString errorMsg;
    if (!m_session->loadDocument(filePath, &errorMsg)) {
        QMessageBox::critical(this, tr("Error"),
                              tr("Failed to open PDF file:\n%1\n\nError: %2")
                                  .arg(filePath).arg(errorMsg));
        return;
    }

    // 成功加载,其他操作由信号处理
}

void MainWindow::closeFile()
{
    m_session->closeDocument();
}

void MainWindow::quit()
{
    QApplication::quit();
}

// ========== 页面导航 ==========

void MainWindow::previousPage()
{
    m_pageWidget->previousPage();
}

void MainWindow::nextPage()
{
    m_pageWidget->nextPage();
}

void MainWindow::firstPage()
{
    m_pageWidget->setCurrentPage(0);
}

void MainWindow::lastPage()
{
    if (m_session->isDocumentLoaded()) {
        m_pageWidget->setCurrentPage(m_session->pageCount() - 1);
    }
}

void MainWindow::goToPage(int page)
{
    // SpinBox是1-based,内部是0-based
    m_pageWidget->setCurrentPage(page - 1);
    updateUIState();
}

// ========== 缩放操作 ==========

void MainWindow::zoomIn()
{
    m_pageWidget->zoomIn();
}

void MainWindow::zoomOut()
{
    m_pageWidget->zoomOut();
}

void MainWindow::actualSize()
{
    m_pageWidget->setZoom(AppConfig::DEFAULT_ZOOM);
}

void MainWindow::fitPage()
{
    m_pageWidget->setZoomMode(ZoomMode::FitPage);
    updateScrollBarPolicy();
}

void MainWindow::fitWidth()
{
    m_pageWidget->setZoomMode(ZoomMode::FitWidth);
    updateScrollBarPolicy();
}

// ========== 视图操作 ==========

void MainWindow::togglePageMode(PageDisplayMode mode)
{
    m_pageWidget->setDisplayMode(mode);
}

void MainWindow::toggleContinuousScroll()
{
    bool continuous = !m_pageWidget->isContinuousScroll();
    m_pageWidget->setContinuousScroll(continuous);
    m_continuousScrollAction->setChecked(continuous);

    updateScrollBarPolicy();

    // 触发widget调整
    m_pageWidget->updateGeometry();
}

// ========== 事件响应 ==========

void MainWindow::onPageChanged(int pageIndex)
{
    Q_UNUSED(pageIndex);
    updateStatusBar();

    // 更新SpinBox(阻止信号避免递归)
    if (m_pageSpinBox) {
        m_pageSpinBox->blockSignals(true);
        m_pageSpinBox->setValue(pageIndex + 1);
        m_pageSpinBox->blockSignals(false);
    }

    // 更新导航按钮状态
    updateUIState();

    // 更新导航面板高亮
    if (m_navigationPanel) {
        m_navigationPanel->updateCurrentPage(pageIndex);
    }
}

void MainWindow::onZoomChanged(double zoom)
{
    updateStatusBar();

    // 更新ComboBox
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

// ========== UI创建 ==========

void MainWindow::createMenuBar()
{
    // 隐藏菜单栏以获得更现代的外观
    menuBar()->setNativeMenuBar(false);

    // 文件菜单
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));

    m_openAction = fileMenu->addAction(tr("&Open..."), this, &MainWindow::openFile);
    m_openAction->setShortcut(QKeySequence::Open);

    m_closeAction = fileMenu->addAction(tr("&Close"), this, &MainWindow::closeFile);
    m_closeAction->setShortcut(QKeySequence::Close);

    fileMenu->addSeparator();

    m_quitAction = fileMenu->addAction(tr("&Quit"), this, &MainWindow::quit);
    m_quitAction->setShortcut(QKeySequence::Quit);

    // 编辑菜单
    QMenu* editMenu = menuBar()->addMenu(tr("&Edit"));

    QAction* copyAction = editMenu->addAction(tr("&Copy"), this, &MainWindow::copySelectedText);
    copyAction->setShortcut(QKeySequence::Copy);
    copyAction->setEnabled(false);
    m_copyAction = copyAction;

    editMenu->addSeparator();

    m_findAction = editMenu->addAction(tr("&Find..."), this, &MainWindow::showSearchBar);
    m_findAction->setShortcut(QKeySequence::Find);

    m_findNextAction = editMenu->addAction(tr("Find &Next"),
                                           m_searchWidget, &SearchWidget::findNext);
    m_findNextAction->setShortcut(QKeySequence::FindNext);
    m_findNextAction->setEnabled(false);

    m_findPreviousAction = editMenu->addAction(tr("Find &Previous"),
                                               m_searchWidget, &SearchWidget::findPrevious);
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

    m_previousPageAction = navMenu->addAction(tr("&Previous Page"), this, &MainWindow::previousPage);
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
    QAction* navPanelAction = m_toolBar->addAction(QIcon(":/icons/icons/sidebar.png"), tr("Panel"));
    navPanelAction->setToolTip(tr("Navigation Panel (F9)"));
    navPanelAction->setCheckable(true);
    connect(navPanelAction, &QAction::triggered, this, &MainWindow::toggleNavigationPanel);

    m_toolBar->addSeparator();

    // 文件操作
    QAction* openAction = m_toolBar->addAction(QIcon(":/icons/icons/open file.png"), tr("Open"));
    openAction->setToolTip(tr("Open PDF (Ctrl+O)"));
    connect(openAction, &QAction::triggered, this, &MainWindow::openFile);

    m_toolBar->addSeparator();

    // 导航
    QAction* firstAction = m_toolBar->addAction(QIcon(":/icons/icons/first-arrow.png"), tr("First"));
    firstAction->setToolTip(tr("First Page (Home)"));
    connect(firstAction, &QAction::triggered, this, &MainWindow::firstPage);

    QAction* prevAction = m_toolBar->addAction(QIcon(":/icons/icons/left-arrow.png"), tr("Previous"));
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

    QAction* nextAction = m_toolBar->addAction(QIcon(":/icons/icons/right-arrow.png"), tr("Next"));
    nextAction->setToolTip(tr("Next Page (PgDown)"));
    connect(nextAction, &QAction::triggered, this, &MainWindow::nextPage);

    QAction* lastAction = m_toolBar->addAction(QIcon(":/icons/icons/last-arrow.png"), tr("Last"));
    lastAction->setToolTip(tr("Last Page (End)"));
    connect(lastAction, &QAction::triggered, this, &MainWindow::lastPage);

    m_toolBar->addSeparator();

    // 缩放
    QAction* zoomOutAction = m_toolBar->addAction(QIcon(":/icons/icons/zoom-out.png"), tr("Zoom Out"));
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
    connect(m_zoomComboBox, &QComboBox::currentTextChanged, this, [this](const QString& text) {
        QString cleaned = text;
        cleaned.remove('%').remove(' ');
        bool ok;
        double zoom = cleaned.toDouble(&ok) / 100.0;
        if (ok && zoom > 0) {
            m_pageWidget->setZoom(zoom);
        }
    });
    m_toolBar->addWidget(m_zoomComboBox);

    QAction* zoomInAction = m_toolBar->addAction(QIcon(":/icons/icons/zoom-in.png"), tr("Zoom In"));
    zoomInAction->setToolTip(tr("Zoom In (Ctrl++)"));
    connect(zoomInAction, &QAction::triggered, this, &MainWindow::zoomIn);

    m_toolBar->addSeparator();

    // 适应选项
    QAction* fitPageAction = m_toolBar->addAction(QIcon(":/icons/icons/fit-to-page.png"), tr("Fit Page"));
    fitPageAction->setToolTip(tr("Fit Page (Ctrl+1)"));
    connect(fitPageAction, &QAction::triggered, this, &MainWindow::fitPage);

    QAction* fitWidthAction = m_toolBar->addAction(QIcon(":/icons/icons/fit-to-width.png"), tr("Fit Width"));
    fitWidthAction->setToolTip(tr("Fit Width (Ctrl+2)"));
    connect(fitWidthAction, &QAction::triggered, this, &MainWindow::fitWidth);

    // 弹性空间
    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolBar->addWidget(spacer);

    // 搜索按钮
    QAction* searchAction = m_toolBar->addAction(QIcon(":/icons/icons/search.png"), tr("Search"));
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

    // 进度条
    statusBar()->addPermanentWidget(m_textPreloadProgress);

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
    // 连接Session信号(替代原来的多个组件信号)
    connect(m_session, &PDFDocumentSession::documentLoaded,
            this, [this](const QString& filePath, int pageCount) {
                qInfo() << "Document loaded:" << filePath << "pages:" << pageCount;

                updateWindowTitle();
                updateUIState();

                // 如果导航面板可见,加载缩略图
                if (m_navigationPanel && m_navigationPanel->isVisible()) {
                    m_navigationPanel->loadDocument(pageCount);
                }

                // 如果是文本PDF,开始预加载
                if (m_session->isTextPDF()) {
                    m_session->textCache()->startPreload();
                }

                m_pageWidget->setCurrentPage(0);
            });

    connect(m_session, &PDFDocumentSession::documentClosed,
            this, [this]() {
                qInfo() << "Document closed";
                if (m_navigationPanel && m_navigationPanel->isVisible()) {
                    m_navigationPanel->clear();
                }
                updateWindowTitle();
                updateUIState();
                m_pageWidget->refresh();
            });

    connect(m_session, &PDFDocumentSession::documentError,
            this, [this](const QString& error) {
                QMessageBox::critical(this, tr("Error"),
                                      tr("Failed to load document:\n%1").arg(error));
            });

    // 页面变化
    connect(m_pageWidget, &PDFPageWidget::pageChanged,
            this, &MainWindow::onPageChanged);

    // 缩放变化
    connect(m_pageWidget, &PDFPageWidget::zoomChanged,
            this, &MainWindow::onZoomChanged);

    // 显示模式变化
    connect(m_pageWidget, &PDFPageWidget::displayModeChanged,
            this, [this](PageDisplayMode mode) {
                m_doublePageAction->setChecked(mode == PageDisplayMode::DoublePage);
                m_singlePageAction->setChecked(mode == PageDisplayMode::SinglePage);
                m_continuousScrollAction->setEnabled(mode == PageDisplayMode::SinglePage);
                updateScrollBarPolicy();
            });

    connect(m_pageWidget, &PDFPageWidget::continuousScrollChanged,
            this, [this](bool continuous) {
                updateScrollBarPolicy();
                m_continuousScrollAction->setChecked(continuous);
            });

    // 连接滚动条信号
    connect(m_scrollArea->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int value) {
                if (m_pageWidget->isContinuousScroll()) {
                    m_pageWidget->updateCurrentPageFromScroll(value);
                }
            });

    // 防抖定时器
    connect(&m_resizeDebounceTimer, &QTimer::timeout, this, [this]() {
        if (m_session->isDocumentLoaded()) {
            ZoomMode mode = m_pageWidget->zoomMode();
            if (mode == ZoomMode::FitWidth || mode == ZoomMode::FitPage) {
                m_pageWidget->updateZoom();
            }
        } else {
            if (m_scrollArea && m_scrollArea->viewport() && m_pageWidget) {
                QSize viewportSize = m_scrollArea->viewport()->size();
                m_pageWidget->resize(viewportSize);
                m_pageWidget->update();
            }
        }
    });

    // 搜索相关
    connect(m_searchWidget, &SearchWidget::closeRequested,
            this, &MainWindow::hideSearchBar);

    connect(m_session, &PDFDocumentSession::searchCompleted,
            this, [this](const QString&, int totalMatches) {
                m_findNextAction->setEnabled(totalMatches > 0);
                m_findPreviousAction->setEnabled(totalMatches > 0);
            });

    // 文本选择
    connect(m_session, &PDFDocumentSession::textSelectionChanged,
            this, &MainWindow::onTextSelectionChanged);

    // 链接相关
    connect(m_session, &PDFDocumentSession::internalLinkRequested,
            this, [this](int targetPage) {
                m_pageWidget->setCurrentPage(targetPage);
            });

    connect(m_session, &PDFDocumentSession::textCopied,
            this, [this](int charCount) {
                statusBar()->showMessage(tr("Copied %1 characters to clipboard")
                                             .arg(charCount), 2000);
            });

    // 文本预加载
    connect(m_session, &PDFDocumentSession::textPreloadProgress,
            this, &MainWindow::onTextPreloadProgress);

    connect(m_session, &PDFDocumentSession::textPreloadCompleted,
            this, &MainWindow::onTextPreloadCompleted);

    connect(m_session, &PDFDocumentSession::textPreloadCancelled,
            this, [this]() {
                if (m_textPreloadProgress) {
                    m_textPreloadProgress->setVisible(false);
                }
                statusBar()->showMessage(tr("Text extraction cancelled"), 3000);
            });

    // 导航面板
    connect(m_navigationPanel, &NavigationPanel::pageJumpRequested,
            this, [this](int pageIndex) {
                m_pageWidget->setCurrentPage(pageIndex);
            });

    connect(m_navigationPanel, &NavigationPanel::externalLinkRequested,
            this, [this](const QString& uri) {
                qDebug() << "External link opened:" << uri;
            });

    connect(m_navigationPanel, &QDockWidget::visibilityChanged,
            this, [this](bool visible) {
                m_showNavigationAction->setChecked(visible);
                m_resizeDebounceTimer.start();
            });

    // 缩略图加载
    connect(m_session, &PDFDocumentSession::thumbnailLoadStarted,
            this, [this](int totalPages) {
                statusBar()->showMessage(tr("Loading thumbnails..."));
            });

    connect(m_session, &PDFDocumentSession::thumbnailLoadProgress,
            this, [this](int loaded, int total) {
                statusBar()->showMessage(
                    tr("Loading thumbnails: %1/%2").arg(loaded).arg(total));
            });

    connect(m_session, &PDFDocumentSession::thumbnailLoadCompleted,
            this, [this]() {
                statusBar()->showMessage(tr("Thumbnails loaded"), 2000);
            });
}

// ========== 状态管理 ==========

void MainWindow::updateUIState()
{
    bool hasDocument = m_session->isDocumentLoaded();
    int pageCount = hasDocument ? m_session->pageCount() : 0;
    int currentPage = m_pageWidget->currentPage();

    // 文件操作
    m_closeAction->setEnabled(hasDocument);

    if (m_copyAction) {
        bool hasSelection = m_session->hasTextSelection();
        m_copyAction->setEnabled(hasDocument && m_session->isTextPDF() && hasSelection);
    }

    // 搜索功能
    m_findAction->setEnabled(hasDocument && m_session->isTextPDF());

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
    }

    updateStatusBar();
}

void MainWindow::updateWindowTitle()
{
    QString title = tr("Simple PDF Viewer");

    if (m_session && m_session->isDocumentLoaded()) {
        QString filePath = m_session->documentPath();
        if (!filePath.isEmpty()) {
            QFileInfo fileInfo(filePath);
            title = fileInfo.fileName() + " - " + title;
        }
    }

    setWindowTitle(title);
}

void MainWindow::updateStatusBar()
{
    if (!m_session->isDocumentLoaded()) {
        m_pageLabel->setText("");
        m_zoomLabel->setText("");
        return;
    }

    // 页码信息
    int currentPage = m_pageWidget->currentPage() + 1;
    int pageCount = m_session->pageCount();
    m_pageLabel->setText(tr("📄 %1 / %2").arg(currentPage).arg(pageCount));

    // 缩放信息
    double zoom = m_pageWidget->zoom();
    QString zoomMode;
    switch (m_pageWidget->zoomMode()) {
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
}

void MainWindow::updateScrollBarPolicy()
{
    if (!m_session->isDocumentLoaded()) {
        return;
    }

    bool continuous = m_pageWidget->isContinuousScroll();
    ZoomMode zoomMode = m_pageWidget->zoomMode();

    if (continuous) {
        m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    } else if (zoomMode == ZoomMode::FitPage) {
        m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    } else {
        m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }
}

// ========== 配置管理 ==========

void MainWindow::applyInitialSettings()
{
    QSize windowSize = AppConfig::instance().defaultWindowSize();
    resize(windowSize);
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

        /* 进度条 */
        QProgressBar {
            background-color: #E5E5E5;
            border: 1px solid #D0D0D0;
            border-radius: 10px;
            height: 20px;
            text-align: center;
            color: #2C2C2C;
            font-size: 11px;
            font-weight: 500;
        }

        QProgressBar::chunk {
            background-color: #5A5A5A;
            border-radius: 9px;
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

        /* 停靠窗口 */
        QDockWidget {
            background-color: #FAFAFA;
            border: none;
        }

        QDockWidget::title {
            background-color: #F0F0F0;
            padding: 8px;
            border-bottom: 1px solid #E0E0E0;
            text-align: left;
            font-weight: 600;
            color: #2C2C2C;
        }

        QDockWidget::close-button, QDockWidget::float-button {
            background-color: transparent;
            border: none;
            padding: 3px;
            border-radius: 3px;
        }

        QDockWidget::close-button:hover, QDockWidget::float-button:hover {
            background-color: #E0E0E0;
        }

        QDockWidget::close-button:pressed, QDockWidget::float-button:pressed {
            background-color: #D0D0D0;
        }

        /* 滚动区域 */
        QScrollArea {
            background-color: #F0F0F0;
            border: none;
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

void MainWindow::loadLastSession()
{
}

void MainWindow::saveCurrentSession()
{
}

// ========== 事件处理 ==========

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);

    if (!m_session->isDocumentLoaded()) {
        if (m_scrollArea && m_scrollArea->viewport() && m_pageWidget) {
            QSize viewportSize = m_scrollArea->viewport()->size();
            m_pageWidget->resize(viewportSize);
        }
    } else {
        m_resizeDebounceTimer.start();
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveCurrentSession();
    QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_navigationPanel && event->type() == QEvent::Resize) {
        m_resizeDebounceTimer.start();
        return false;
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::showSearchBar()
{
    // 检查是否为文本PDF
    if (!m_session->isTextPDF()) {
        QMessageBox::information(this, tr("Search Unavailable"),
                                 tr("This PDF is a scanned document and does not contain searchable text.\n\n"
                                    "To search this document, you would need to use OCR (Optical Character Recognition)."));
        return;
    }

    // 检查文本是否还在预加载中
    if (m_session->textCache()->isPreloading()) {
        int progress = m_session->textCache()->computePreloadProgress();

        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            tr("Text Extraction in Progress"),
            tr("Text extraction is in progress (%1%).\n\n"
               "You can search now, but only extracted pages will be searchable.\n\n"
               "Continue with search?").arg(progress),
            QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::No) {
            return;
        }
    }

    m_searchWidget->showAndFocus();
}

void MainWindow::hideSearchBar()
{
    m_searchWidget->hide();
    m_session->interactionHandler()->clearSearchResults();
    m_pageWidget->update();
    m_pageWidget->setFocus();
}

void MainWindow::onTextPreloadProgress(int current, int total)
{
    if (m_textPreloadProgress) {
        m_textPreloadProgress->setVisible(true);
        m_textPreloadProgress->setMaximum(total);
        m_textPreloadProgress->setValue(current);
        m_textPreloadProgress->setFormat(QString("%1/%2").arg(current).arg(total));

        statusBar()->showMessage(
            tr("📄 Extracting text: %1/%2 pages").arg(current).arg(total));
    }
}

void MainWindow::onTextPreloadCompleted()
{
    if (m_textPreloadProgress) {
        m_textPreloadProgress->setVisible(false);
    }

    statusBar()->showMessage(tr("✅ Text extraction completed. Search is ready."), 3000);
    m_statusLabel->setText(tr("📄 Loaded: %1 - Search ready")
                               .arg(QFileInfo(m_session->documentPath()).fileName()));

    if (AppConfig::instance().debugMode() && m_session->textCache()) {
        qDebug() << "Text preload completed:"
                 << m_session->textCache()->getStatistics();
    }
}

void MainWindow::toggleNavigationPanel()
{
    if (!m_navigationPanel) {
        return;
    }

    bool visible = !m_navigationPanel->isVisible();
    m_navigationPanel->setVisible(visible);

    // 如果显示面板且文档已加载,加载缩略图
    if (visible && m_session->isDocumentLoaded()) {
        if (!m_session->contentHandler()->isThumbnailLoading() &&
            m_session->contentHandler()->loadedThumbnailCount() == 0) {
            m_session->startLoadThumbnails(120);
        }
    }
}

void MainWindow::toggleLinksVisible()
{
    bool visible = m_showLinksAction->isChecked();
    m_session->setLinksVisible(visible);
    m_pageWidget->update();
}

void MainWindow::copySelectedText()
{
    if (m_session->hasTextSelection()) {
        m_session->copySelectedText();
    }
}

void MainWindow::onTextSelectionChanged()
{
    bool hasSelection = m_session->hasTextSelection();

    if (m_copyAction) {
        m_copyAction->setEnabled(hasSelection);
    }

    if (hasSelection) {
        int charCount = m_session->selectedText().length();
        m_statusLabel->setText(tr("Selected: %1 characters").arg(charCount));
    } else {
        updateStatusBar();
    }
}
