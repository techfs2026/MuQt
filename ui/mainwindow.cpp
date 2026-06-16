#include "mainwindow.h"
#include "themedicon.h"
#include "pdfdocumenttab.h"
#include "pdfdocumentsession.h"
#include "pdfannotationhandler.h"
#include "annotationmanager.h"
#include "navigationpanel.h"
#include "dictionaryconnector.h"
#include "ocrstatusindicator.h"
#include "ocrmanager.h"
#include "chinesetokenizer.h"
#include "appconfig.h"
#include "settingsdialog.h"

#ifdef Q_OS_WIN
#include "fileassociationmanager.h"
#include "firstrundialog.h"
#endif

#include <QMenuBar>
#include <QHBoxLayout>
#include <QMimeData>
#include <QToolBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QLabel>
#include <QSpinBox>
#include <QComboBox>
#include <QTabWidget>
#include <QTabBar>
#include <QMouseEvent>
#include <QToolButton>
#include <QColorDialog>
#include <QPixmap>
#include <QIcon>
#include <QApplication>
#include <QFileInfo>
#include <QCloseEvent>
#include <QDockWidget>
#include <QActionGroup>
#include <QShortcut>
#include <QPointer>
#include <QScreen>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_tabWidget(nullptr)
    , m_navigationDock(nullptr)
    , m_navigationPanel(nullptr)
    , m_toolBar(nullptr)
    , m_pageSpinBox(nullptr)
    , m_zoomComboBox(nullptr)
    , m_statusLabel(nullptr)
    , m_ocrInitialized(false)
{
    setWindowTitle(tr("OwlPDF"));

    // 优先恢复上次窗口几何（大小+位置+最大化状态）；
    // 无记录（首次运行）则用默认尺寸，并在主屏可用区域居中。
    const QByteArray savedGeometry = AppConfig::instance().windowGeometry();
    if (!savedGeometry.isEmpty()) {
        restoreGeometry(savedGeometry);
    } else {
        resize(AppConfig::instance().defaultWindowSize());
        if (QScreen* screen = QGuiApplication::primaryScreen()) {
            const QRect avail = screen->availableGeometry();
            const QSize sz = size();
            move(avail.x() + (avail.width()  - sz.width())  / 2,
                 avail.y() + (avail.height() - sz.height()) / 2);
        }
    }

    m_tabWidget = new QTabWidget(this);
    // 关闭按钮改用自定义 ThemedIcon 按钮（见 installTabCloseButton），
    // 因此不启用 Qt 自动关闭按钮，避免重复出现两个关闭图标。
    m_tabWidget->setTabsClosable(false);
    m_tabWidget->setMovable(true);
    m_tabWidget->setDocumentMode(true);
    m_tabWidget->setUsesScrollButtons(true);
    m_tabWidget->tabBar()->setExpanding(false);
    // 鼠标中键（滚轮）点击 tab 即关闭该 tab
    m_tabWidget->tabBar()->installEventFilter(this);

    setCentralWidget(m_tabWidget);

    m_navigationDock = new QDockWidget(tr("Navigation"), this);
    m_navigationDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_navigationDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    addDockWidget(Qt::LeftDockWidgetArea, m_navigationDock);

    // 公共侧边栏：单例常驻于 dock，切换文档时通过 attachSession 重绑定数据
    m_navigationPanel = new NavigationPanel(this);
    m_navigationPanel->setObjectName("navigationPanel");
    m_navigationDock->setWidget(m_navigationPanel);
    m_navigationDock->setVisible(false);

    m_welcomeLabel = new QLabel(this);
    m_welcomeLabel->setAlignment(Qt::AlignCenter);
    m_welcomeLabel->setWordWrap(true);
    m_welcomeLabel->setObjectName("welcomeLabel");
    m_welcomeLabel->raise();

    QString welcomeText = tr(
        "<div style='color: #666;'>"
        "<p style='font-size: 18px; font-weight: bold; text-align: center; margin-bottom: 18px;'>No PDF File Opened</p>"
        "<table align='center' cellspacing='6'><tr><td style='font-size: 14px;'>"
        "• Drag and drop PDF files here<br>"
        "• Press <b>Ctrl+O</b> to open file dialog<br>"
        "• Use <b>File</b> menu to browse documents<br>"
        "• Press <b>F11</b> to toggle toolbar"
        "</td></tr></table>"
        "</div>"
        );
    m_welcomeLabel->setText(welcomeText);
    m_welcomeLabel->setVisible(true);

    createActions();
    createMenuBar();
    createToolBar();
    createStatusBar();
    setupConnections();

    updateUIState();

    m_resizeDebounceTimer.setSingleShot(true);
    m_resizeDebounceTimer.setInterval(AppConfig::instance().resizeDebounceDelay());

    m_navigationDock->installEventFilter(this);

    setAcceptDrops(true);

    if (!DictionaryConnector::instance().isConfigured()) {
        qWarning() << "Dictionary command not configured, lookup feature will not work";
    }
}

MainWindow::~MainWindow()
{
    // 未保存询问已在 closeEvent 统一处理；析构只做无交互清理。
    // 不能走 closeTab()——它会再调 maybeSaveTab，用户在 closeEvent 选了
    // "放弃"后脏标志仍在，这里会重复弹第二次保存对话框。
    while (m_tabWidget->count() > 0) {
        QWidget* w = m_tabWidget->widget(0);
        m_tabWidget->removeTab(0);
        delete w;
    }

    if (m_ocrInitialized) {
        OCRManager::instance().shutdown();
    }
}

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

    if (!tab || tab->isDocumentLoaded()) {
        tab = createNewTab();
    }

    QString errorMsg;
    if (!tab->loadDocument(filePath, &errorMsg)) {
        QMessageBox::critical(this, tr("Error"),
                              tr("Failed to open:\n%1\n\nError: %2")
                                  .arg(filePath).arg(errorMsg));

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

void MainWindow::saveCurrentTab()
{
    // Cmd+S：把当前文档的目录修改写回 PDF。无改动则静默忽略；
    // 成功不打扰（标签页圆点会自动消失），仅失败时弹错。
    PDFDocumentTab* tab = currentTab();
    if (!tab || !tab->hasUnsavedChanges()) {
        return;
    }
    if (!tab->saveOutline()) {
        QMessageBox::critical(this, tr("Save Failed"),
                              tr("Failed to save outline.\n"
                                 "Please check file permissions and disk space."));
    }
}

bool MainWindow::maybeSaveTab(PDFDocumentTab* tab)
{
    if (!tab || !tab->hasUnsavedChanges()) {
        return true;
    }

    QMessageBox box(this);
    box.setWindowTitle(tr("Unsaved Changes"));
    box.setText(tr("\"%1\" has unsaved changes.").arg(tab->documentTitle()));
    box.setInformativeText(tr("Do you want to save them to the PDF?"));
    box.setIcon(QMessageBox::Warning);
    box.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Save);

    int ret = box.exec();
    if (ret == QMessageBox::Cancel) {
        return false;
    }
    if (ret == QMessageBox::Save) {
        if (!tab->saveOutline()) {
            // 保存失败时不继续关闭，避免静默丢失修改
            QMessageBox::critical(this, tr("Save Failed"),
                                  tr("Failed to save outline.\nPlease check file permissions and disk space."));
            return false;
        }
    }
    return true;
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

    // 有未保存的目录修改时先询问
    if (!maybeSaveTab(tab)) {
        return;
    }

    disconnectTabSignals(tab);

    // 关闭的是当前文档：先卸载公共侧边栏，避免悬挂到即将销毁的 session
    // （removeTab 后若仍有其它 tab，会触发 onTabChanged 重新挂载）
    if (tab == currentTab() && m_navigationPanel) {
        m_navigationPanel->detachSession();
        m_navigationDock->setVisible(false);
    }

    m_tabWidget->removeTab(index);
    tab->deleteLater();

    updateUIState();
}

void MainWindow::quit()
{
    // 走 close() 而非 QApplication::quit()：让 closeEvent 统一处理
    // 未保存询问/多文档确认/会话保存，避免绕过这些逻辑直接退出。
    close();
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

    installTabCloseButton(index);

    connectTabSignals(tab);

    return tab;
}

// 给指定 tab 安装自定义关闭按钮：图标用 close-file.svg，经 ThemedIcon
// 染色跟随主题。点击时按 tab 当前所在 index 关闭（不用闭包捕获 index，
// 因为 tab 可拖动/插入会使 index 变化，运行时再查实际位置）。
void MainWindow::installTabCloseButton(int index)
{
    QToolButton* closeBtn = new QToolButton;   // 注意：父对象先不设，交给 wrapper
    closeBtn->setObjectName("tabCloseButton");
    closeBtn->setIcon(ThemedIcon::toolButton("close-file", 12));
    closeBtn->setIconSize(QSize(12, 12));
    closeBtn->setFixedSize(20, 20);
    closeBtn->setAutoRaise(false);
    closeBtn->setCursor(Qt::ArrowCursor);
    closeBtn->setToolTip(tr("Close Tab"));
    closeBtn->setFocusPolicy(Qt::NoFocus);

    // 外层透明容器：右侧留出间隔，把按钮从 tab 右边框往里推
    QWidget* wrapper = new QWidget(m_tabWidget);
    wrapper->setObjectName("tabCloseButtonWrapper");
    auto* lay = new QHBoxLayout(wrapper);
    lay->setContentsMargins(0, 0, 6, 0);   // 右 6px 留白 = 按钮整体左移 6px
    lay->setSpacing(0);
    lay->addWidget(closeBtn);

    QWidget* tabPage = m_tabWidget->widget(index);
    connect(closeBtn, &QToolButton::clicked, this, [this, tabPage]() {
        int idx = m_tabWidget->indexOf(tabPage);
        if (idx >= 0) {
            closeTab(idx);
        }
    });

    m_tabWidget->tabBar()->setTabButton(index, QTabBar::RightSide, wrapper);
}

void MainWindow::connectTabSignals(PDFDocumentTab* tab)
{
    if (!tab) return;

    connect(tab, &PDFDocumentTab::documentLoaded,
            this, &MainWindow::onCurrentTabDocumentLoaded);

    connect(tab, &PDFDocumentTab::pageChanged,
            this, &MainWindow::onCurrentTabPageChanged);

    connect(tab, &PDFDocumentTab::zoomChanged,
            this, &MainWindow::onCurrentTabZoomChanged);

    connect(tab, &PDFDocumentTab::displayModeChanged,
            this, &MainWindow::onCurrentTabDisplayModeChanged);

    connect(tab, &PDFDocumentTab::continuousScrollChanged,
            this, &MainWindow::onCurrentTabContinuousScrollChanged);

    connect(tab, &PDFDocumentTab::textSelectionChanged,
            this, &MainWindow::onCurrentTabTextSelectionChanged);

    // 未保存状态变化：刷新该 tab 标题标记，当前 tab 还要刷新窗口标题
    connect(tab, &PDFDocumentTab::unsavedChangesChanged,
            this, [this, tab](bool) {
                int idx = m_tabWidget->indexOf(tab);
                if (idx >= 0) {
                    updateTabTitle(idx);
                }
                if (tab == currentTab()) {
                    updateWindowTitle();
                    updateUIState();  // 同步刷新 Save 动作可用态
                }
            });

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
        // 公共侧边栏重绑定到当前文档；显示/折叠遵循全局意图，不随切换重置
        m_navigationPanel->attachSession(tab->session());

        bool shouldShow = m_showNavigationAction->isChecked();
        m_navigationDock->setVisible(shouldShow);
        m_navPanelAction->setChecked(shouldShow);

        bool canEnhance = !tab->isTextPDF();
        m_paperEffectAction->setEnabled(canEnhance);
        m_paperEffectAction->setChecked(canEnhance && tab->paperEffectEnabled());
        if (tab->isTextPDF()) {
            m_paperEffectAction->setToolTip(tr("Paper texture enhancement (scanned PDFs only)"));
        } else {
            m_paperEffectAction->setToolTip(tr("Paper texture enhancement"));
        }
    } else {
        // 无文档：卸载侧边栏数据并隐藏（保留用户的显隐意图，不改 m_showNavigationAction）
        m_navigationPanel->detachSession();
        m_navigationDock->setVisible(false);
        m_navPanelAction->setChecked(false);
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

        const int maxLength = 20;
        QString text = elideTabTitle(fullTitle, maxLength);
        // 未保存的目录修改：标题尾部加圆点标记
        if (tab->hasUnsavedChanges()) {
            text += QStringLiteral("  •");
        }
        m_tabWidget->setTabText(index, text);
        m_tabWidget->setTabToolTip(index, tab->documentPath());
    }
}

// 截断策略：扩展名（后缀）始终完整保留；对主文件名做“前 x + … + 后 x”，
// 头尾保留、中间用省略号，x 由可用长度动态均分（前段多 1 个，更自然）。
// 总长不超过 maxLength。若全名本就不超限，原样返回。
QString MainWindow::elideTabTitle(const QString& fullTitle, int maxLength) const
{
    if (fullTitle.length() <= maxLength) {
        return fullTitle;
    }

    const QChar ellipsis(0x2026); // '…'

    QFileInfo fileInfo(fullTitle);
    QString baseName = fileInfo.completeBaseName();
    QString extension = fileInfo.suffix();
    QString suffix = extension.isEmpty() ? QString() : ("." + extension);

    // 主文件名可用字符数 = 总上限 - 后缀长度 - 省略号(1)
    int budget = maxLength - suffix.length() - 1;

    // 后缀本身就把预算吃光（极端的超长扩展名）：退化为对全名首段截断
    if (budget < 2) {
        return fullTitle.left(qMax(1, maxLength - 1)) + ellipsis;
    }

    // 若主文件名其实不需要省略（含后缀后仍 <= 上限），原样返回
    if (baseName.length() + suffix.length() <= maxLength) {
        return fullTitle;
    }

    // 动态均分：前段 = ceil(budget/2)，后段 = floor(budget/2)
    int frontLen = (budget + 1) / 2;
    int backLen  = budget - frontLen;

    QString front = baseName.left(frontLen);
    QString back  = backLen > 0 ? baseName.right(backLen) : QString();

    return front + ellipsis + back + suffix;
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

    bool visible = !m_navigationDock->isVisible();

    m_navigationDock->setVisible(visible);
    m_navPanelAction->setChecked(visible);
    m_showNavigationAction->setChecked(visible);

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

void MainWindow::copySelectedText()
{
    if (PDFDocumentTab* tab = currentTab()) {
        tab->copySelectedText();
    }
}

void MainWindow::onCurrentTabPageChanged(int pageIndex)
{
    PDFDocumentTab* sender = qobject_cast<PDFDocumentTab*>(QObject::sender());
    if (sender != currentTab()) {
        return;
    }

    updateStatusBar();

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
    updateZoomCombox(zoom);
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

    int index = m_tabWidget->indexOf(tab);
    if (index >= 0) {
        updateTabTitle(index);
    }

    if (tab == currentTab()) {
        updateWindowTitle();
        updateUIState();

        if (tab->isDocumentLoaded()) {
            // 文档首次加载完成：挂载公共侧边栏并自动展开
            m_navigationPanel->attachSession(tab->session());

            m_navigationDock->setVisible(true);
            m_showNavigationAction->setChecked(true);
            m_navPanelAction->setChecked(true);
        }

        bool canEnhance = !tab->isTextPDF();
        m_paperEffectAction->setEnabled(canEnhance);

        if (tab->isTextPDF()) {
            m_paperEffectAction->setChecked(false);
        }

        tab->updateOCRHoverState();
    }

    updateUIState();
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
    m_openAction = new QAction(ThemedIcon::toolButton("open-file"),
                               tr("Open"), this);
    m_openAction->setShortcut(QKeySequence::Open);
    m_openAction->setToolTip(tr("Open File (Ctrl+O)"));
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openFile);

    m_closeAction = new QAction(ThemedIcon::toolButton("close-file"),
                                tr("Close"), this);
    m_closeAction->setShortcut(QKeySequence::Close);
    connect(m_closeAction, &QAction::triggered, this, &MainWindow::closeCurrentTab);

    // 保存目录修改到 PDF：并入标准 Cmd+S，与“保存文档”同一手势
    m_saveAction = new QAction(tr("Save"), this);
    m_saveAction->setShortcut(QKeySequence::Save);
    m_saveAction->setToolTip(tr("Save outline changes to PDF (Ctrl+S)"));
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::saveCurrentTab);

    m_quitAction = new QAction(tr("Quit"), this);
    m_quitAction->setShortcut(QKeySequence::Quit);
    connect(m_quitAction, &QAction::triggered, this, &MainWindow::quit);

    m_settingsAction = new QAction(tr("Settings..."), this);
    m_settingsAction->setShortcut(QKeySequence::Preferences);
    m_settingsAction->setMenuRole(QAction::PreferencesRole);
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::showSettingsDialog);

    m_copyAction = new QAction(tr("Copy"), this);
    m_copyAction->setShortcut(QKeySequence::Copy);
    m_copyAction->setEnabled(false);
    connect(m_copyAction, &QAction::triggered, this, &MainWindow::copySelectedText);

    m_findAction = new QAction(ThemedIcon::toolButton("search"),
                               tr("Find"), this);
    m_findAction->setShortcut(QKeySequence::Find);
    m_findAction->setToolTip(tr("Search (Ctrl+F)"));
    connect(m_findAction, &QAction::triggered, this, &MainWindow::showSearchBar);

    m_findNextAction = new QAction(tr("Find Next"), this);
    m_findNextAction->setShortcut(QKeySequence::FindNext);
    m_findNextAction->setEnabled(false);
    connect(m_findNextAction, &QAction::triggered, this, &MainWindow::findNext);

    m_findPreviousAction = new QAction(tr("Find Previous"), this);
    m_findPreviousAction->setShortcut(QKeySequence::FindPrevious);
    m_findPreviousAction->setEnabled(false);
    connect(m_findPreviousAction, &QAction::triggered, this, &MainWindow::findPrevious);

    m_firstPageAction = new QAction(ThemedIcon::toolButton("first-arrow"),
                                    tr("First Page"), this);
    m_firstPageAction->setToolTip(tr("First Page (Home)"));
    connect(m_firstPageAction, &QAction::triggered, this, &MainWindow::firstPage);

    m_previousPageAction = new QAction(ThemedIcon::toolButton("left-arrow"),
                                       tr("Previous Page"), this);
    m_previousPageAction->setToolTip(tr("Previous Page (PgUp)"));
    connect(m_previousPageAction, &QAction::triggered, this, &MainWindow::previousPage);

    m_nextPageAction = new QAction(ThemedIcon::toolButton("right-arrow"),
                                   tr("Next Page"), this);
    m_nextPageAction->setToolTip(tr("Next Page (PgDown)"));
    connect(m_nextPageAction, &QAction::triggered, this, &MainWindow::nextPage);

    m_lastPageAction = new QAction(ThemedIcon::toolButton("last-arrow"),
                                   tr("Last Page"), this);
    m_lastPageAction->setToolTip(tr("Last Page (End)"));
    connect(m_lastPageAction, &QAction::triggered, this, &MainWindow::lastPage);

    m_zoomInAction = new QAction(ThemedIcon::toolButton("zoom-in"),
                                 tr("Zoom In"), this);
    m_zoomInAction->setShortcut(QKeySequence::ZoomIn);
    m_zoomInAction->setToolTip(tr("Zoom In (Ctrl++)"));
    connect(m_zoomInAction, &QAction::triggered, this, &MainWindow::zoomIn);

    m_zoomOutAction = new QAction(ThemedIcon::toolButton("zoom-out"),
                                  tr("Zoom Out"), this);
    m_zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    m_zoomOutAction->setToolTip(tr("Zoom Out (Ctrl+-)"));
    connect(m_zoomOutAction, &QAction::triggered, this, &MainWindow::zoomOut);

    m_fitPageAction = new QAction(ThemedIcon::toolButton("fit-to-page"),
                                  tr("Fit Page"), this);
    m_fitPageAction->setShortcut(tr("Ctrl+1"));
    m_fitPageAction->setToolTip(tr("Fit Page (Ctrl+1)"));
    m_fitPageAction->setCheckable(true);
    connect(m_fitPageAction, &QAction::triggered, this, &MainWindow::fitPage);

    m_fitWidthAction = new QAction(ThemedIcon::toolButton("fit-to-width"),
                                   tr("Fit Width"), this);
    m_fitWidthAction->setShortcut(tr("Ctrl+2"));
    m_fitWidthAction->setToolTip(tr("Fit Width (Ctrl+2)"));
    m_fitWidthAction->setCheckable(true);
    connect(m_fitWidthAction, &QAction::triggered, this, &MainWindow::fitWidth);

    m_pageModeGroup = new QActionGroup(this);
    m_pageModeGroup->setExclusive(true);

    m_singlePageAction = new QAction(ThemedIcon::toolButton("single-page-mode"),
                                     tr("Single Page"), this);
    m_singlePageAction->setCheckable(true);
    m_singlePageAction->setChecked(true);
    m_pageModeGroup->addAction(m_singlePageAction);
    connect(m_singlePageAction, &QAction::triggered, this, [this]() {
        togglePageMode(PageDisplayMode::SinglePage);
    });

    m_doublePageAction = new QAction(ThemedIcon::toolButton("double-page-mode"),
                                     tr("Double Page"), this);
    m_doublePageAction->setCheckable(true);
    m_pageModeGroup->addAction(m_doublePageAction);
    connect(m_doublePageAction, &QAction::triggered, this, [this]() {
        togglePageMode(PageDisplayMode::DoublePage);
    });

    m_continuousScrollAction = new QAction(ThemedIcon::toolButton("continuous-mode"),
                                           tr("Continuous Scroll"), this);
    m_continuousScrollAction->setCheckable(true);
    m_continuousScrollAction->setChecked(true);
    connect(m_continuousScrollAction, &QAction::triggered,
            this, &MainWindow::toggleContinuousScroll);

    m_navPanelAction = new QAction(ThemedIcon::toolButton("sidebar"),
                                   tr("Navigation Panel"), this);
    m_navPanelAction->setToolTip(tr("Show Navigation Panel (F9)"));
    m_navPanelAction->setCheckable(true);
    connect(m_navPanelAction, &QAction::triggered,
            this, &MainWindow::toggleNavigationPanel);

    m_showNavigationAction = m_navPanelAction;
    m_showNavigationAction->setShortcut(tr("F9"));

    m_showLinksAction = new QAction(tr("Show Link Borders"), this);
    m_showLinksAction->setCheckable(true);
    m_showLinksAction->setChecked(true);
    connect(m_showLinksAction, &QAction::triggered,
            this, &MainWindow::toggleLinksVisible);

    m_paperEffectAction = new QAction(ThemedIcon::toolButton("paper-effect"),
                                      tr("Paper Enhancement"), this);
    m_paperEffectAction->setToolTip(tr("Eye-protective paper texture enhancement"));
    m_paperEffectAction->setCheckable(true);
    m_paperEffectAction->setChecked(false);
    connect(m_paperEffectAction, &QAction::triggered,
            this, &MainWindow::togglePaperEffect);

    m_ocrHoverAction = new QAction(ThemedIcon::toolButton("ocr"),
                                   tr("OCR Lookup"), this);
    m_ocrHoverAction->setShortcut(QKeySequence(tr("Ctrl+Shift+O")));
    m_ocrHoverAction->setToolTip(tr("Enable OCR hover mode (Ctrl+Shift+O)\n"
                                    "Press Ctrl+Q to trigger recognition\n"
                                    "(Scanned PDFs only)"));
    m_ocrHoverAction->setCheckable(true);
    m_ocrHoverAction->setChecked(false);
    m_ocrHoverAction->setEnabled(false);
    connect(m_ocrHoverAction, &QAction::triggered,
            this, &MainWindow::toggleOCRHover);

    m_toggleToolBarAction = new QAction(tr("Toggle Toolbar"), this);
    m_toggleToolBarAction->setShortcut(tr("F11"));
    connect(m_toggleToolBarAction, &QAction::triggered, this, &MainWindow::toggleToolBar);

    // —— 批注 ——
    // 工具栏单入口：展开侧边栏并切到「批注」Tab（控件在 Tab 顶部）
    m_annotationAction = new QAction(ThemedIcon::toolButton("pen"), tr("Annotate"), this);
    m_annotationAction->setToolTip(tr("Annotate — pen & eraser (in the side panel)"));
    connect(m_annotationAction, &QAction::triggered, this, &MainWindow::showAnnotations);

    // 全局撤销/重做快捷键（作用于当前文档），无工具栏按钮，仅快捷键
    m_undoAnnotAction = new QAction(tr("Undo Annotation"), this);
    m_undoAnnotAction->setShortcut(QKeySequence::Undo);   // Ctrl+Z
    connect(m_undoAnnotAction, &QAction::triggered, this, &MainWindow::undoAnnotation);
    addAction(m_undoAnnotAction);

    m_redoAnnotAction = new QAction(tr("Redo Annotation"), this);
    m_redoAnnotAction->setShortcut(QKeySequence::Redo);   // Ctrl+Y / Ctrl+Shift+Z
    connect(m_redoAnnotAction, &QAction::triggered, this, &MainWindow::redoAnnotation);
    addAction(m_redoAnnotAction);
}

void MainWindow::createMenuBar()
{
#ifdef Q_OS_MACOS
    menuBar()->setNativeMenuBar(true);
#else
    menuBar()->setNativeMenuBar(false);
#endif

    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(m_openAction);
    fileMenu->addAction(m_saveAction);
    fileMenu->addAction(m_closeAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_quitAction);

    QMenu* editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(m_copyAction);
    editMenu->addSeparator();
    editMenu->addAction(m_findAction);
    editMenu->addAction(m_findNextAction);
    editMenu->addAction(m_findPreviousAction);

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
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
    viewMenu->addSeparator();
    viewMenu->addAction(m_toggleToolBarAction);

    // 设置项：macOS 经 PreferencesRole 自动归入应用菜单（Cmd+,），
    // 其它平台放进 Tools 菜单。
#ifdef Q_OS_MACOS
    fileMenu->addAction(m_settingsAction);
#else
    QMenu* toolsMenu = menuBar()->addMenu(tr("&Tools"));
    toolsMenu->addAction(m_settingsAction);
#ifdef Q_OS_WIN
    QAction* manageAssociationAction = new QAction(tr("File Association Settings..."), this);
    connect(manageAssociationAction, &QAction::triggered, this, &MainWindow::onManageFileAssociation);
    toolsMenu->addAction(manageAssociationAction);
#endif
#endif
}

void MainWindow::createToolBar()
{
    m_toolBar = addToolBar(QString());
    m_toolBar->setMovable(false);
    m_toolBar->setFloatable(false);
    m_toolBar->setIconSize(QSize(20, 20));
    m_toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_toolBar->setContentsMargins(0, 0, 0, 0);
    m_toolBar->setObjectName("mainToolBar");

    m_toolBar->addAction(m_navPanelAction);
    m_toolBar->addSeparator();

    m_toolBar->addAction(m_openAction);
    m_toolBar->addSeparator();

    m_toolBar->addAction(m_firstPageAction);
    m_toolBar->addAction(m_previousPageAction);

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

    m_toolBar->addAction(m_fitPageAction);
    m_toolBar->addAction(m_fitWidthAction);
    m_toolBar->addSeparator();

    m_toolBar->addAction(m_singlePageAction);
    m_toolBar->addAction(m_doublePageAction);
    m_toolBar->addAction(m_continuousScrollAction);
    m_toolBar->addSeparator();

    m_toolBar->addAction(m_paperEffectAction);
    m_toolBar->addAction(m_ocrHoverAction);
    m_toolBar->addSeparator();

    // 批注：单入口图标（控件都在侧边栏「批注」Tab）
    m_toolBar->addAction(m_annotationAction);
    m_toolBar->addSeparator();

    m_toolBar->addAction(m_findAction);
}

// ============================================================
// 批注：单入口 + 全局撤销/重做
// ============================================================

void MainWindow::showAnnotations()
{
    PDFDocumentTab* tab = currentTab();
    if (!tab || !tab->isDocumentLoaded()) {
        return;
    }
    // 确保侧边栏可见并切到「批注」Tab、激活钢笔
    m_navigationDock->setVisible(true);
    m_showNavigationAction->setChecked(true);
    m_navPanelAction->setChecked(true);
    m_navigationPanel->showAnnotationTab();
}

void MainWindow::undoAnnotation()
{
    PDFDocumentTab* tab = currentTab();
    if (tab) {
        if (AnnotationManager* am = tab->session()->annotationManager()) {
            am->undo();
        }
    }
}

void MainWindow::redoAnnotation()
{
    PDFDocumentTab* tab = currentTab();
    if (tab) {
        if (AnnotationManager* am = tab->session()->annotationManager()) {
            am->redo();
        }
    }
}

void MainWindow::createStatusBar()
{
    statusBar()->setObjectName("modernStatusBar");
    statusBar()->setSizeGripEnabled(true);

    m_statusLabel = new QLabel();
    m_statusLabel->setObjectName("statusLabel");
    statusBar()->addWidget(m_statusLabel, 1);

    m_ocrIndicator = new OCRStatusIndicator(this);
    statusBar()->addPermanentWidget(m_ocrIndicator);
    m_ocrIndicator->setState(OCREngineState::Uninitialized);

    connect(m_ocrIndicator, &OCRStatusIndicator::engineStartRequested,
            this, &MainWindow::initOCREngine);
    connect(m_ocrIndicator, &OCRStatusIndicator::engineStopRequested,
            this, &MainWindow::shutdownOCREngine);
    connect(&OCRManager::instance(), &OCRManager::engineStateChanged,
            this, &MainWindow::onOCREngineStateChanged);

    connect(&OCRManager::instance(), &OCRManager::ocrHoverEnabledChanged,
            this, &MainWindow::onOCRHoverEnabledChanged);

    updateStatusBar();
}

void MainWindow::setupConnections()
{
    connect(m_tabWidget, &QTabWidget::currentChanged,
            this, &MainWindow::onTabChanged);

    connect(m_tabWidget, &QTabWidget::tabCloseRequested,
            this, &MainWindow::onTabCloseRequested);

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

#ifdef Q_OS_MAC
    QKeySequence seq(Qt::META | Qt::Key_Q);
#else
    QKeySequence seq(Qt::CTRL | Qt::Key_Q);
#endif

    QShortcut* ocrShortcut = new QShortcut(seq, this);
    connect(ocrShortcut, &QShortcut::activated, this, &MainWindow::triggerOCRAtCurrentPosition);
}

void MainWindow::triggerOCRAtCurrentPosition()
{
    if (!OCRManager::instance().isOCRHoverEnabled()) {
        qDebug() << "OCR hover not enabled globally";
        return;
    }

    PDFDocumentTab* tab = currentTab();
    if (!tab || !tab->isDocumentLoaded()) {
        qDebug() << "No document loaded";
        return;
    }

    tab->triggerOCRAtCurrentPosition();
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

    if (m_welcomeLabel) {
        m_welcomeLabel->setVisible(!hasDocument && m_tabWidget->count() <= 1);
    }

    m_closeAction->setEnabled(hasDocument);

    // 保存仅在有未保存的目录修改时可用
    m_saveAction->setEnabled(hasDocument && tab->hasUnsavedChanges());

    if (m_copyAction) {
        bool hasSelection = hasDocument && tab->hasTextSelection();
        m_copyAction->setEnabled(hasDocument && tab->isTextPDF() && hasSelection);
    }

    m_findAction->setEnabled(hasDocument && tab->isTextPDF());

    m_annotationAction->setEnabled(hasDocument);
    m_undoAnnotAction->setEnabled(hasDocument);
    m_redoAnnotAction->setEnabled(hasDocument);

    m_firstPageAction->setEnabled(hasDocument && currentPage > 0);
    m_previousPageAction->setEnabled(hasDocument && currentPage > 0);
    m_nextPageAction->setEnabled(hasDocument && currentPage < pageCount - 1);
    m_lastPageAction->setEnabled(hasDocument && currentPage < pageCount - 1);

    m_zoomInAction->setEnabled(hasDocument);
    m_zoomOutAction->setEnabled(hasDocument);
    m_fitPageAction->setEnabled(hasDocument);
    m_fitWidthAction->setEnabled(hasDocument);

    m_fitPageAction->setEnabled(hasDocument && zoomMode != ZoomMode::FitPage);
    m_fitPageAction->setChecked(hasDocument && zoomMode == ZoomMode::FitPage);
    m_fitWidthAction->setEnabled(hasDocument && zoomMode != ZoomMode::FitWidth);
    m_fitWidthAction->setChecked(hasDocument && zoomMode == ZoomMode::FitWidth);

    m_singlePageAction->setEnabled(hasDocument);
    m_doublePageAction->setEnabled(hasDocument);
    m_continuousScrollAction->setEnabled(hasDocument && displayMode == PageDisplayMode::SinglePage);

    m_singlePageAction->setChecked(hasDocument && displayMode == PageDisplayMode::SinglePage);
    m_doublePageAction->setChecked(hasDocument && displayMode == PageDisplayMode::DoublePage);
    m_continuousScrollAction->setChecked(hasDocument && continuousScroll);

    m_paperEffectAction->setEnabled(canEnhance);
    if (hasDocument && !canEnhance) {
        m_paperEffectAction->setToolTip(
            tr("Paper texture enhancement\n(Current is native text PDF, not applicable)"));
    } else if (canEnhance) {
        m_paperEffectAction->setToolTip(tr("Paper texture enhancement"));
    } else {
        m_paperEffectAction->setToolTip(tr("Paper texture enhancement (Open document first)"));
    }
    if (hasDocument) {
        m_paperEffectAction->setChecked(tab->paperEffectEnabled());

        if (tab->isTextPDF() && tab->paperEffectEnabled()) {
            tab->setPaperEffectEnabled(false);
        }
    }

    if (m_ocrHoverAction) {
        OCREngineState engineState = OCRManager::instance().engineState();
        bool ocrReady = (engineState == OCREngineState::Ready);

        QString tooltip;
        bool shouldEnable = false;

        if (!m_ocrInitialized) {
            tooltip = tr("Enable OCR hover (Ctrl+Shift+O)\n"
                         "Please start OCR engine in status bar first");
            shouldEnable = false;
        } else if (engineState == OCREngineState::Loading) {
            tooltip = tr("Enable OCR hover (Ctrl+Shift+O)\n"
                         "OCR engine loading, please wait...");
            shouldEnable = false;
        } else if (engineState == OCREngineState::Error) {
            tooltip = tr("Enable OCR hover (Ctrl+Shift+O)\n"
                         "OCR engine initialization failed");
            shouldEnable = false;
        } else if (ocrReady) {
            shouldEnable = true;
            if (!hasDocument) {
                tooltip = tr("Enable OCR hover (Ctrl+Shift+O)\n"
                             "Press Ctrl+Q to trigger\n"
                             "Open document first");
            } else if (tab->isTextPDF()) {
                tooltip = tr("Enable OCR hover (Ctrl+Shift+O)\n"
                             "Press Ctrl+Q to trigger\n"
                             "Current is text PDF, OCR not needed");
            } else {
                tooltip = tr("Enable OCR hover (Ctrl+Shift+O)\n"
                             "Press Ctrl+Q to trigger\n"
                             "Click to enable OCR hover feature");
            }
        }

        m_ocrHoverAction->setEnabled(shouldEnable);
        m_ocrHoverAction->setToolTip(tooltip);

        bool shouldCheck = ocrReady && OCRManager::instance().isOCRHoverEnabled();
        m_ocrHoverAction->setChecked(shouldCheck);
    }

    m_showNavigationAction->setEnabled(hasDocument);
    m_showLinksAction->setEnabled(hasDocument);

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
    QString title = tr("OwlPDF");

    PDFDocumentTab* tab = currentTab();
    if (tab && tab->isDocumentLoaded()) {
        QString filePath = tab->documentPath();
        if (!filePath.isEmpty()) {
            QFileInfo fileInfo(filePath);
            title = fileInfo.fileName() + " - " + title;
        }
    }

    // 当前文档有未保存的目录修改：窗口标题前加圆点
    if (tab && tab->hasUnsavedChanges()) {
        title = QStringLiteral("• ") + title;
    }

    setWindowTitle(title);
}

void MainWindow::updateStatusBar()
{
    PDFDocumentTab* tab = currentTab();

    if (!tab || !tab->isDocumentLoaded()) {
        m_statusLabel->setText(tr("Please open a PDF file"));
        return;
    }

    if (tab->hasTextSelection()) {
        m_statusLabel->setText(tr("Text selected"));
    } else {
        m_statusLabel->setText(QString());
    }
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);

    if (m_welcomeLabel) {
        QRect centralRect = centralWidget()->geometry();
        m_welcomeLabel->setGeometry(centralRect);
    }

    m_resizeDebounceTimer.start();
}


bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_navigationDock && event->type() == QEvent::Resize) {
        m_resizeDebounceTimer.start();
    }

    // 鼠标中键点击 tab 关闭对应文档
    if (watched == m_tabWidget->tabBar()
        && event->type() == QEvent::MouseButtonRelease) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::MiddleButton) {
            int index = m_tabWidget->tabBar()->tabAt(me->pos());
            if (index >= 0) {
                onTabCloseRequested(index);
                return true;
            }
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // 先逐个处理未保存的目录修改（切到对应 tab 让用户看清是哪个文档）
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        PDFDocumentTab* tab = qobject_cast<PDFDocumentTab*>(m_tabWidget->widget(i));
        if (tab && tab->hasUnsavedChanges()) {
            m_tabWidget->setCurrentIndex(i);
            if (!maybeSaveTab(tab)) {
                event->ignore();
                return;
            }
        }
    }

    int tabCount = m_tabWidget->count();
    int loadedCount = 0;

    for (int i = 0; i < tabCount; ++i) {
        PDFDocumentTab* tab = qobject_cast<PDFDocumentTab*>(m_tabWidget->widget(i));
        if (tab && tab->isDocumentLoaded()) {
            loadedCount++;
        }
    }

    if (loadedCount > 1) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            tr("Close Application"),
            tr("You have %n document(s) open. Are you sure you want to close all of them?", "", loadedCount),
            QMessageBox::Yes | QMessageBox::No
            );

        if (reply == QMessageBox::No) {
            event->ignore();
            return;
        }
    }

    saveSession();

    QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        bool hasPdf = false;
        for (const QUrl& url : event->mimeData()->urls()) {
            QString filePath = url.toLocalFile();
            if (filePath.endsWith(".pdf", Qt::CaseInsensitive)) {
                hasPdf = true;
                break;
            }
        }

        if (hasPdf) {
            event->acceptProposedAction();
            return;
        }
    }

    event->ignore();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const QMimeData* mimeData = event->mimeData();

    if (!mimeData->hasUrls()) {
        event->ignore();
        return;
    }

    QList<QUrl> urls = mimeData->urls();
    int successCount = 0;
    int failCount = 0;

    for (const QUrl& url : urls) {
        QString filePath = url.toLocalFile();

        if (!filePath.endsWith(".pdf", Qt::CaseInsensitive)) {
            continue;
        }

        QFileInfo fileInfo(filePath);
        if (!fileInfo.exists()) {
            failCount++;
            qWarning() << "File does not exist:" << filePath;
            continue;
        }

        PDFDocumentTab* tab = currentTab();
        if (!tab || tab->isDocumentLoaded()) {
            tab = createNewTab();
        }

        QString errorMsg;
        if (tab->loadDocument(filePath, &errorMsg)) {
            successCount++;

            int index = m_tabWidget->indexOf(tab);
            if (index >= 0) {
                updateTabTitle(index);
            }
        } else {
            failCount++;
            qWarning() << "Failed to load:" << filePath << "Error:" << errorMsg;

            if (m_tabWidget->count() > 1) {
                int index = m_tabWidget->indexOf(tab);
                closeTab(index);
            }
        }
    }

    if (successCount > 0) {
        QString message;
        if (failCount > 0) {
            message = tr("Opened %1 file(s), failed %2").arg(successCount).arg(failCount);
        } else {
            message = tr("Successfully opened %n file(s)", "", successCount);
        }
        statusBar()->showMessage(message, 3000);
    } else if (failCount > 0) {
        statusBar()->showMessage(tr("Failed to open files"), 3000);
    }

    event->acceptProposedAction();
}

void MainWindow::togglePaperEffect()
{
    PDFDocumentTab* tab = currentTab();
    if (!tab || !tab->isDocumentLoaded()) {
        return;
    }

    if (tab->isTextPDF()) {
        QMessageBox::information(this, tr("Feature Not Available"),
                                 tr("Paper enhancement is only for scanned PDFs.\n"
                                    "Current document is native text PDF and does not need this feature."));
        m_paperEffectAction->setChecked(false);
        return;
    }

    bool enabled = m_paperEffectAction->isChecked();
    tab->setPaperEffectEnabled(enabled);
}

QString MainWindow::getEngineStateText(OCREngineState state) const
{
    switch (state) {
    case OCREngineState::Uninitialized:
        return tr("Uninitialized");
    case OCREngineState::Loading:
        return tr("Loading");
    case OCREngineState::Ready:
        return tr("Ready");
    case OCREngineState::Error:
        return tr("Error");
    default:
        return tr("Unknown State");
    }
}

void MainWindow::toggleOCRHover()
{
    bool wantEnable = m_ocrHoverAction->isChecked();

    OCREngineState state = OCRManager::instance().engineState();

    if (wantEnable) {
        if (!m_ocrInitialized) {
            QMessageBox::information(this, tr("OCR Feature"),
                                     tr("Please start the OCR engine in the status bar first!\n\n"
                                        "Click the [OCR Engine] button on the right side of the status bar to start the engine."));
            m_ocrHoverAction->setChecked(false);
            return;
        }

        if (state == OCREngineState::Loading) {
            QMessageBox::information(this, tr("OCR Feature"),
                                     tr("OCR engine is loading...\n\n"
                                        "Please wait for the engine to finish loading (status indicator turns green) before enabling."));
            m_ocrHoverAction->setChecked(false);
            return;
        }

        if (state == OCREngineState::Error) {
            QMessageBox::warning(this, tr("OCR Feature"),
                                 tr("OCR engine initialization failed!\n\n"
                                    "Error message: %1\n\n"
                                    "Please try:\n"
                                    "1. Restart OCR engine\n"
                                    "2. Check model file integrity\n"
                                    "3. View logs for detailed error info")
                                     .arg(OCRManager::instance().lastError()));
            m_ocrHoverAction->setChecked(false);
            return;
        }

        if (state != OCREngineState::Ready) {
            QMessageBox::information(this, tr("OCR Feature"),
                                     tr("OCR engine not ready yet, cannot enable feature.\n\n"
                                        "Current state: %1")
                                         .arg(getEngineStateText(state)));
            m_ocrHoverAction->setChecked(false);
            return;
        }

        OCRManager::instance().setOCRHoverEnabled(true);

        QMessageBox::information(this, tr("OCR Hover Enabled"),
                                 tr("OCR hover lookup enabled!\n\n"
                                    "How to use:\n"
                                    "1. Move mouse to text position to recognize\n"
                                    "2. Press Ctrl+Q shortcut to trigger recognition\n"
                                    "3. Recognition result displays in popup\n"
                                    "4. Click popup to query dictionary\n"
                                    "5. Click toolbar button again to disable OCR\n\n"
                                    "Tip: View OCR engine status in status bar"));
    } else {
        OCRManager::instance().setOCRHoverEnabled(false);
    }
}

void MainWindow::initOCREngine()
{
    if (m_ocrInitialized) {
        qInfo() << "OCR engine already initialized";
        return;
    }

    QString modelDir = AppConfig::instance().ocrModelDir();
    QString dictDir = AppConfig::instance().jiebaDictDir();

    qInfo() << "MainWindow: Starting OCR engine...";
    qInfo() << "Model directory:" << modelDir;
    qInfo() << "Dictionary directory:" << dictDir;

    m_ocrIndicator->setEngineRunning(true);
    m_ocrIndicator->setState(OCREngineState::Loading);

    if (!ChineseTokenizer::instance().isInitialized()) {
        bool jiebaOk = ChineseTokenizer::instance().initialize(dictDir);
        if (!jiebaOk) {
            qWarning() << "Tokenizer initialization failed:"
                       << ChineseTokenizer::instance().lastError();
            QMessageBox::warning(this, tr("Tokenizer Initialization Failed"),
                                 tr("Chinese word segmentation initialization failed:\n%1\n\nOCR will use full text.")
                                     .arg(ChineseTokenizer::instance().lastError()));
        }
    }

    bool started = OCRManager::instance().initialize(modelDir);

    if (started) {
        m_ocrInitialized = true;
        qInfo() << "OCR engine starting...";

        statusBar()->showMessage(tr("OCR engine loading in background..."), 3000);
    } else {
        qWarning() << "OCR engine start failed";

        m_ocrIndicator->setEngineRunning(false);
        m_ocrIndicator->setState(OCREngineState::Error);

        QMessageBox::critical(this, tr("OCR Engine Start Failed"),
                              tr("Unable to start OCR engine, please check:\n"
                                 "1. Model files exist\n"
                                 "2. Model path configuration is correct\n"
                                 "3. System resources are sufficient\n\n"
                                 "Model directory: %1").arg(modelDir));

        m_ocrInitialized = false;
    }

    updateUIState();
}

void MainWindow::shutdownOCREngine()
{
    if (!m_ocrInitialized) {
        return;
    }

    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        tr("Stop OCR Engine"),
        tr("Are you sure you want to stop the OCR engine?\n\nOCR hover feature will be disabled."),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
        );

    if (reply != QMessageBox::Yes) {
        return;
    }

    if (OCRManager::instance().isOCRHoverEnabled()) {
        OCRManager::instance().setOCRHoverEnabled(false);
        if (m_ocrHoverAction) {
            m_ocrHoverAction->setChecked(false);
        }
    }

    OCRManager::instance().shutdown();
    m_ocrInitialized = false;

    m_ocrIndicator->setEngineRunning(false);
    m_ocrIndicator->setState(OCREngineState::Uninitialized);

    qInfo() << "OCR engine stopped";
    statusBar()->showMessage(tr("OCR engine stopped"), 2000);

    updateUIState();
}

void MainWindow::onOCREngineStateChanged(OCREngineState state)
{
    if (m_ocrIndicator) {
        m_ocrIndicator->setState(state);

        if (state == OCREngineState::Uninitialized) {
            m_ocrIndicator->setEngineRunning(false);
        } else {
            m_ocrIndicator->setEngineRunning(true);
        }
    }

    if (state == OCREngineState::Ready) {
        statusBar()->showMessage(tr("OCR engine ready, enable OCR hover in toolbar"), 3000);
    } else if (state == OCREngineState::Error) {
        statusBar()->showMessage(tr("OCR engine initialization failed"), 5000);
    }

    updateUIState();
}

void MainWindow::onOCRHoverEnabledChanged(bool enabled)
{
    updateUIState();

    for (int i = 0; i < m_tabWidget->count(); ++i) {
        PDFDocumentTab* tab = qobject_cast<PDFDocumentTab*>(m_tabWidget->widget(i));
        if (tab && tab->isDocumentLoaded() && !tab->isTextPDF()) {
            tab->updateOCRHoverState();
        }
    }

    qInfo() << "OCR hover state changed to:" << enabled;
}

#ifdef Q_OS_WIN

void MainWindow::checkAndShowFirstRunDialog()
{
    if (!AppConfig::instance().hasAskedFileAssociation()) {
        FirstRunDialog dialog(this);

        if (dialog.exec() == QDialog::Accepted) {
            bool shouldRegister = dialog.shouldRegisterFileAssociation();
            bool dontAskAgain = dialog.shouldNotAskAgain();

            if (dontAskAgain) {
                AppConfig::instance().setHasAskedFileAssociation(true);
                AppConfig::instance().setFileAssociationUserChoice(shouldRegister);
            }

            if (shouldRegister) {
                handleFileAssociation(true);
            }
        }
    }
}

void MainWindow::handleFileAssociation(bool shouldRegister)
{
    QStringList extensions = getSupportedExtensions();
    QString fileTypeName = getFileTypeName();

    bool success = false;

    if (shouldRegister) {
        success = FileAssociationManager::instance().registerMultipleTypes(
            extensions,
            fileTypeName,
            tr("PDF Document")
            );

        if (success) {
            statusBar()->showMessage(tr("File association has been set"), 3000);
        } else {
            QMessageBox::warning(this, tr("Warning"),
                                 tr("Failed to set file association. Please check permissions."));
        }
    } else {
        success = FileAssociationManager::instance().unregisterMultipleTypes(
            extensions,
            fileTypeName
            );

        if (success) {
            statusBar()->showMessage(tr("File association has been removed"), 3000);
        }
    }
}

void MainWindow::onManageFileAssociation()
{
    QStringList extensions = getSupportedExtensions();
    QString fileTypeName = getFileTypeName();

    bool isRegistered = FileAssociationManager::instance().isAnyRegistered(extensions, fileTypeName);

    QString message;
    if (isRegistered) {
        message = tr("Currently associated file types: %1\n\nDo you want to remove file association?")
        .arg(extensions.join(", "));
    } else {
        message = tr("Do you want to associate file types: %1\n\n"
                     "After association, you can open files by double-clicking.")
                      .arg(extensions.join(", "));
    }

    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        tr("File Association Settings"),
        message,
        QMessageBox::Yes | QMessageBox::No
        );

    if (reply == QMessageBox::Yes) {
        handleFileAssociation(!isRegistered);
    }
}

QStringList MainWindow::getSupportedExtensions() const
{
    return QStringList() << "pdf";
}

QString MainWindow::getFileTypeName() const
{
    return "OwlPDF.PDFDocument";
}

#endif

void MainWindow::openFileFromCommandLine(const QString& filePath)
{
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        QMessageBox::warning(this, tr("Error"),
                             tr("File does not exist: %1").arg(filePath));
        return;
    }

    PDFDocumentTab* tab = currentTab();

    if (!tab || tab->isDocumentLoaded()) {
        tab = createNewTab();
    }

    if (tab && tab->loadDocument(filePath)) {
        int index = m_tabWidget->indexOf(tab);
        updateTabTitle(index);  // 统一走截断逻辑（xxx…xxx.pdf），勿用原始全名

        statusBar()->showMessage(tr("Opened: %1").arg(filePath), 3000);
    }
}

void MainWindow::restoreLastSession()
{
    // 已有文档（命令行 / macOS 双击已打开文件）则放弃恢复
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        PDFDocumentTab* t = qobject_cast<PDFDocumentTab*>(m_tabWidget->widget(i));
        if (t && t->isDocumentLoaded()) {
            return;
        }
    }

    SessionState session = AppConfig::instance().loadSession();
    if (session.tabs.isEmpty()) {
        return;
    }

    PDFDocumentTab* activeTab = nullptr;
    for (int i = 0; i < session.tabs.size(); ++i) {
        const SessionTabState& st = session.tabs.at(i);
        if (!QFileInfo::exists(st.filePath)) {
            continue;   // 文件已被移动/删除
        }

        PDFDocumentTab* tab = currentTab();
        if (!tab || tab->isDocumentLoaded()) {
            tab = createNewTab();
        }
        if (!tab->loadDocument(st.filePath)) {
            continue;
        }

        int index = m_tabWidget->indexOf(tab);
        updateTabTitle(index);  // 统一走截断逻辑（xxx…xxx.pdf），勿用原始全名

        applyTabViewState(tab, st);

        if (i == session.activeIndex) {
            activeTab = tab;
        }
    }

    if (m_tabWidget->count() == 0) {
        return;   // 会话里的文件已全部失效
    }

    if (!activeTab) {
        activeTab = qobject_cast<PDFDocumentTab*>(m_tabWidget->widget(0));
    }

    m_navPanelAction->setChecked(session.showNavigation);
    m_tabWidget->setCurrentIndex(m_tabWidget->indexOf(activeTab));
    onTabChanged(m_tabWidget->currentIndex());

    statusBar()->showMessage(
        tr("Restored %n document(s) from last session", "", m_tabWidget->count()), 3000);
}

void MainWindow::saveSession()
{
    QVector<SessionTabState> tabs;
    int activeIndex = 0;
    PDFDocumentTab* active = currentTab();

    for (int i = 0; i < m_tabWidget->count(); ++i) {
        PDFDocumentTab* tab = qobject_cast<PDFDocumentTab*>(m_tabWidget->widget(i));
        if (!tab || !tab->isDocumentLoaded()) {
            continue;
        }

        SessionTabState st;
        st.filePath         = tab->documentPath();
        st.page             = tab->currentPage();
        st.zoomMode         = static_cast<int>(tab->zoomMode());
        st.zoom             = tab->zoom();
        st.displayMode      = static_cast<int>(tab->displayMode());
        st.continuousScroll = tab->isContinuousScroll();

        if (tab == active) {
            activeIndex = tabs.size();
        }
        tabs.append(st);
    }

    AppConfig::instance().saveSession(tabs, activeIndex, m_navPanelAction->isChecked());
    AppConfig::instance().setWindowGeometry(saveGeometry());
}

void MainWindow::applyTabViewState(PDFDocumentTab* tab, const SessionTabState& state)
{
    tab->setDisplayMode(static_cast<PageDisplayMode>(state.displayMode));
    tab->setContinuousScroll(state.continuousScroll);

    // 缩放与跳页延后到视口尺寸就绪后再套用（适应宽/高依赖视口尺寸）
    const ZoomMode zoomMode = static_cast<ZoomMode>(state.zoomMode);
    const double zoom = state.zoom;
    const int page = state.page;
    QPointer<PDFDocumentTab> guard(tab);
    QTimer::singleShot(0, this, [guard, zoomMode, zoom, page]() {
        if (!guard) {
            return;
        }
        if (zoomMode == ZoomMode::FitWidth) {
            guard->fitWidth();
        } else if (zoomMode == ZoomMode::FitPage) {
            guard->fitPage();
        } else {
            guard->setZoom(zoom);
        }
        guard->goToPage(page);
    });
}

void MainWindow::showSettingsDialog()
{
    SettingsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        // 窗口缩放防抖延时由 MainWindow 持有，立即生效
        m_resizeDebounceTimer.setInterval(AppConfig::instance().resizeDebounceDelay());
    }
}



void MainWindow::toggleToolBar()
{
    if (m_toolBar) {
        bool isVisible = m_toolBar->isVisible();
        m_toolBar->setVisible(!isVisible);

        if (!isVisible) {
            statusBar()->showMessage(tr("Toolbar shown (Press F11 to hide)"), 2000);
        } else {
            statusBar()->showMessage(tr("Toolbar hidden (Press F11 to show)"), 2000);
        }
    }
}
