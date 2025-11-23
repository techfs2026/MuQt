#include "pdfdocumenttab.h"
#include "pdfdocumentsession.h"
#include "pdfdocumentstate.h"
#include "navigationpanel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QProgressBar>
#include <QSplitter>
#include <QFileInfo>
#include <QTimer>
#include <QMessageBox>

PDFDocumentTab::PDFDocumentTab(QWidget* parent)
    : QWidget(parent)
    , m_session(nullptr)
    , m_pageWidget(nullptr)
    , m_navigationPanel(nullptr)
    , m_searchWidget(nullptr)
    , m_splitter(nullptr)
    , m_scrollArea(nullptr)
    , m_textPreloadProgress(nullptr)
{
    setupUI();
    setupConnections();
}

PDFDocumentTab::~PDFDocumentTab()
{
    if (m_session) {
        m_session->closeDocument();
    }
}

// ==================== UI设置 ====================

void PDFDocumentTab::setupUI()
{
    // 创建主布局
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 创建Session
    m_session = new PDFDocumentSession(this);

    // 创建导航面板
    m_navigationPanel = new NavigationPanel(m_session, this);
    m_navigationPanel->setVisible(false);

    // 创建滚动区域
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(false);
    m_scrollArea->setAlignment(Qt::AlignCenter);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    // 创建页面显示组件
    m_pageWidget = new PDFPageWidget(m_session, this);
    m_scrollArea->setWidget(m_pageWidget);

    // 初始化时调整大小
    QTimer::singleShot(0, this, [this]() {
        const PDFDocumentState* state = m_session->state();
        if (!state->isDocumentLoaded() && m_scrollArea &&
            m_scrollArea->viewport() && m_pageWidget) {
            QSize viewportSize = m_scrollArea->viewport()->size();
            m_pageWidget->resize(viewportSize);
        }
    });

    // 创建搜索工具栏
    m_searchWidget = new SearchWidget(m_session,
                                      m_pageWidget, this);
    m_searchWidget->setVisible(false);

    // 创建进度条
    m_textPreloadProgress = new QProgressBar(this);
    m_textPreloadProgress->setMaximumWidth(200);
    m_textPreloadProgress->setMaximumHeight(20);
    m_textPreloadProgress->setVisible(false);
    m_textPreloadProgress->setTextVisible(true);
    m_textPreloadProgress->setAlignment(Qt::AlignCenter);

    // 组装主布局
    mainLayout->addWidget(m_searchWidget);
    mainLayout->addWidget(m_scrollArea, 1);
    mainLayout->addWidget(m_textPreloadProgress);

    // 设置样式
    m_scrollArea->setStyleSheet("QScrollArea { background-color: #F0F0F0; border: none; }");
}

void PDFDocumentTab::setupConnections()
{
    // ========== Session状态变化信号 ==========

    // 文档加载状态变化
    connect(m_session, &PDFDocumentSession::documentLoadedChanged,
            this, [this](bool loaded, const QString& path, int pageCount) {
                if (loaded) {
                    onDocumentLoaded(path, pageCount);
                } else {
                    m_filePath.clear();
                    m_navigationPanel->clear();
                    m_pageWidget->refresh();
                    emit documentClosed();
                }
            });

    connect(m_session, &PDFDocumentSession::documentError,
            this, &PDFDocumentTab::documentError);

    // 页面变化
    connect(m_session, &PDFDocumentSession::currentPageChanged,
            this, &PDFDocumentTab::onPageChanged);

    // 缩放变化
    connect(m_session, &PDFDocumentSession::currentZoomChanged,
            this, [this](double zoom) {
                m_pageWidget->onZoomChanged(zoom);
                emit zoomChanged(zoom);
            });

    connect(m_session, &PDFDocumentSession::currentZoomModeChanged,
            this, [this](ZoomMode mode) {
                m_pageWidget->setZoomMode(mode);
            });

    // 显示模式变化
    connect(m_session, &PDFDocumentSession::currentDisplayModeChanged,
            this, [this](PageDisplayMode mode) {
                updateScrollBarPolicy();
                emit displayModeChanged(mode);
            });

    connect(m_session, &PDFDocumentSession::continuousScrollChanged,
            this, [this](bool continuous) {
                updateScrollBarPolicy();
                m_pageWidget->renderCurrentPage();
                emit continuousScrollChanged(continuous);
            });

    // 页面位置计算完成(连续滚动模式)
    connect(m_session, &PDFDocumentSession::pagePositionsChanged,
            this, [this](const QVector<int>& positions, const QVector<int>& heights) {
                QSize targetSize = m_pageWidget->sizeHint();
                m_pageWidget->resize(targetSize);

                QTimer::singleShot(0, this, [this]() {
                    m_pageWidget->refreshVisiblePages();
                });
            });

    // ========== 滚动条信号 ==========

    connect(m_scrollArea->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int value) {
                const PDFDocumentState* state = m_session->state();
                if (state->isContinuousScroll()) {
                    m_session->updateCurrentPageFromScroll(value);
                    m_pageWidget->updateCurrentPageFromScroll(value);
                }
            });

    // ========== 搜索相关 ==========

    connect(m_searchWidget, &SearchWidget::closeRequested,
            this, &PDFDocumentTab::hideSearchBar);

    connect(m_session, &PDFDocumentSession::searchCompleted,
            this, &PDFDocumentTab::searchCompleted);

    // ========== 文本选择 ==========

    connect(m_session, &PDFDocumentSession::textSelectionChanged,
            this, &PDFDocumentTab::textSelectionChanged);

    // ========== 链接跳转 ==========

    connect(m_session, &PDFDocumentSession::internalLinkRequested,
            this, [this](int targetPage) {
                m_session->goToPage(targetPage);
            });

    // ========== 文本预加载 ==========

    connect(m_session, &PDFDocumentSession::textPreloadProgress,
            this, &PDFDocumentTab::onTextPreloadProgress);

    connect(m_session, &PDFDocumentSession::textPreloadCompleted,
            this, &PDFDocumentTab::onTextPreloadCompleted);

    connect(m_session, &PDFDocumentSession::textPreloadCancelled,
            this, [this]() {
                if (m_textPreloadProgress) {
                    m_textPreloadProgress->setVisible(false);
                }
            });

    // ========== 导航面板 ==========

    connect(m_navigationPanel, &NavigationPanel::pageJumpRequested,
            this, [this](int pageIndex) {
                m_session->goToPage(pageIndex);
            });

    // 缩略图加载
    connect(m_session, &PDFDocumentSession::thumbnailLoadStarted,
            this, [this](int totalPages) {
                // 可以显示进度提示
            });
}

// ==================== 文档操作 ====================

bool PDFDocumentTab::loadDocument(const QString& filePath, QString* errorMessage)
{
    if (!m_session->loadDocument(filePath, errorMessage)) {
        return false;
    }

    m_filePath = filePath;
    return true;
}

void PDFDocumentTab::closeDocument()
{
    m_session->closeDocument();
}

bool PDFDocumentTab::isDocumentLoaded() const
{
    return m_session && m_session->state()->isDocumentLoaded();
}

QString PDFDocumentTab::documentPath() const
{
    return m_filePath;
}

QString PDFDocumentTab::documentTitle() const
{
    if (m_filePath.isEmpty()) {
        return tr("New Tab");
    }
    return QFileInfo(m_filePath).fileName();
}

// ==================== 导航操作 ====================

void PDFDocumentTab::previousPage()
{
    m_session->previousPage();
}

void PDFDocumentTab::nextPage()
{
    m_session->nextPage();
}

void PDFDocumentTab::firstPage()
{
    m_session->firstPage();
}

void PDFDocumentTab::lastPage()
{
    m_session->lastPage();
}

void PDFDocumentTab::goToPage(int pageIndex)
{
    m_session->goToPage(pageIndex);
}

// ==================== 缩放操作 ====================

void PDFDocumentTab::zoomIn()
{
    m_session->zoomIn();
}

void PDFDocumentTab::zoomOut()
{
    m_session->zoomOut();
}

void PDFDocumentTab::actualSize()
{
    m_session->actualSize();
}

void PDFDocumentTab::fitPage()
{
    m_session->fitPage();
    updateScrollBarPolicy();
}

void PDFDocumentTab::fitWidth()
{
    m_session->fitWidth();
    updateScrollBarPolicy();
}

void PDFDocumentTab::setZoom(double zoom)
{
    m_session->setZoom(zoom);
}

// ==================== 视图操作 ====================

void PDFDocumentTab::setDisplayMode(PageDisplayMode mode)
{
    m_session->setDisplayMode(mode);
}

void PDFDocumentTab::setContinuousScroll(bool continuous)
{
    m_session->setContinuousScroll(continuous);
}

// ==================== 搜索操作 ====================

void PDFDocumentTab::showSearchBar()
{
    // 检查是否为文本PDF
    if (!m_session->state()->isTextPDF()) {
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

void PDFDocumentTab::hideSearchBar()
{
    m_searchWidget->hide();
    m_session->cancelSearch();
    m_pageWidget->update();
    m_pageWidget->setFocus();
}

bool PDFDocumentTab::isSearchBarVisible() const
{
    return m_searchWidget && m_searchWidget->isVisible();
}

// ==================== 文本操作 ====================

void PDFDocumentTab::copySelectedText()
{
    if (m_session->state()->hasTextSelection()) {
        m_session->copySelectedText();
    }
}

void PDFDocumentTab::selectAll()
{
    if (m_session->state()->isDocumentLoaded()) {
        m_session->selectAll(m_session->state()->currentPage());
    }
}

// ==================== 链接操作 ====================

void PDFDocumentTab::setLinksVisible(bool visible)
{
    m_session->setLinksVisible(visible);
    m_pageWidget->update();
}

bool PDFDocumentTab::linksVisible() const
{
    return m_session->state()->linksVisible();
}

// ==================== 状态查询 ====================

int PDFDocumentTab::currentPage() const
{
    return m_session->state()->currentPage();
}

int PDFDocumentTab::pageCount() const
{
    return m_session->state()->pageCount();
}

double PDFDocumentTab::zoom() const
{
    return m_session->state()->currentZoom();
}

ZoomMode PDFDocumentTab::zoomMode() const
{
    return m_session->state()->currentZoomMode();
}

PageDisplayMode PDFDocumentTab::displayMode() const
{
    return m_session->state()->currentDisplayMode();
}

bool PDFDocumentTab::isContinuousScroll() const
{
    return m_session->state()->isContinuousScroll();
}

bool PDFDocumentTab::hasTextSelection() const
{
    return m_session->state()->hasTextSelection();
}

bool PDFDocumentTab::isTextPDF() const
{
    return m_session->state()->isTextPDF();
}

// ==================== 私有槽函数 ====================

void PDFDocumentTab::onDocumentLoaded(const QString& filePath, int pageCount)
{
    m_filePath = filePath;

    if (m_navigationPanel) {
        m_navigationPanel->loadDocument(pageCount);

        if (!m_session->contentHandler()->isThumbnailLoading()) {
            m_session->startLoadThumbnails(120);
        }
    }

    if (m_session->state()->isTextPDF()) {
        m_session->textCache()->startPreload();
    }

    // 初始化缩放
    QTimer::singleShot(100, this, [this]() {
        const PDFDocumentState* state = m_session->state();
        if (state->isDocumentLoaded()) {
            ZoomMode mode = state->currentZoomMode();
            if (mode == ZoomMode::FitWidth || mode == ZoomMode::FitPage) {
                QSize viewportSize = m_scrollArea->viewport()->size();
                m_session->updateZoom(viewportSize);
            }
        }
    });

    emit documentLoaded(filePath, pageCount);
}

void PDFDocumentTab::onPageChanged(int pageIndex)
{
    // 更新导航面板高亮
    if (m_navigationPanel) {
        m_navigationPanel->updateCurrentPage(pageIndex);
    }

    m_pageWidget->onPageChanged(pageIndex);

    emit pageChanged(pageIndex);
}

void PDFDocumentTab::onTextPreloadProgress(int current, int total)
{
    if (m_textPreloadProgress) {
        m_textPreloadProgress->setVisible(true);
        m_textPreloadProgress->setMaximum(total);
        m_textPreloadProgress->setValue(current);
        m_textPreloadProgress->setFormat(QString("%1/%2").arg(current).arg(total));
    }

    emit textPreloadProgress(current, total);
}

void PDFDocumentTab::onTextPreloadCompleted()
{
    if (m_textPreloadProgress) {
        m_textPreloadProgress->setVisible(false);
    }

    emit textPreloadCompleted();
}

// ==================== 私有方法 ====================

void PDFDocumentTab::updateScrollBarPolicy()
{
    const PDFDocumentState* state = m_session->state();

    if (!state->isDocumentLoaded()) {
        return;
    }

    bool continuous = state->isContinuousScroll();
    ZoomMode zoomMode = state->currentZoomMode();

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
