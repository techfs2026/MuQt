#include "pdfdocumenttab.h"
#include "pdfdocumentsession.h"
#include "pdfdocumentstate.h"
#include "pdfpagewidget.h"
#include "searchwidget.h"
#include "ocrfloatingwidget.h"
#include "perthreadmupdfrenderer.h"
#include "pagecachemanager.h"
#include "pdfinteractionhandler.h"
#include "pdfcontenthandler.h"
#include "textcachemanager.h"
#include "pdfviewhandler.h"
#include "annotationmanager.h"
#include "annotationpdfio.h"
#include "linkmanager.h"
#include "ocrmanager.h"
#include "chinesetokenizer.h"
#include "dictionaryconnector.h"
#include "appconfig.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QProgressBar>
#include <QSplitter>
#include <QFileInfo>
#include <QTimer>
#include <QMessageBox>
#include <QMenu>
#include <QApplication>
#include <QToolTip>

PDFDocumentTab::PDFDocumentTab(QWidget* parent)
    : QWidget(parent)
    , m_session(nullptr)
    , m_pageWidget(nullptr)
    , m_searchWidget(nullptr)
    , m_scrollArea(nullptr)
    , m_textPreloadProgress(nullptr)
    , m_lastClickTime(0)
    , m_clickCount(0)
    , m_isUserScrolling(false)
    , m_ocrFloatingWidget(nullptr)
{
    setupUI();
    setupConnections();
}

PDFDocumentTab::~PDFDocumentTab()
{
}

void PDFDocumentTab::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);


    m_session = new PDFDocumentSession(this);


    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(false);
    m_scrollArea->setAlignment(Qt::AlignCenter);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setObjectName("pdfScrollArea");


    m_pageWidget = new PDFPageWidget(m_session, this);
    m_pageWidget->setObjectName("pdfPageWidget");
    m_scrollArea->setWidget(m_pageWidget);


    m_searchWidget = new SearchWidget(m_session, this);
    m_searchWidget->setVisible(false);
    m_searchWidget->setObjectName("searchWidget");


    m_textPreloadProgress = new QProgressBar(this);
    m_textPreloadProgress->setMaximumWidth(220);
    m_textPreloadProgress->setMaximumHeight(24);
    m_textPreloadProgress->setVisible(false);
    m_textPreloadProgress->setTextVisible(true);
    m_textPreloadProgress->setAlignment(Qt::AlignCenter);
    m_textPreloadProgress->setObjectName("textPreloadProgress");


    mainLayout->addWidget(m_searchWidget);
    mainLayout->addWidget(m_scrollArea, 1);


    QHBoxLayout* progressLayout = new QHBoxLayout();
    progressLayout->addStretch();
    progressLayout->addWidget(m_textPreloadProgress);
    progressLayout->addStretch();
    progressLayout->setContentsMargins(0, 8, 0, 8);
    mainLayout->addLayout(progressLayout);


    m_ocrFloatingWidget = new OCRFloatingWidget(this);
    m_ocrFloatingWidget->hide();
}


void PDFDocumentTab::setupConnections()
{

    connect(m_session, &PDFDocumentSession::documentLoaded,
            this, [this](const QString& path, int pageCount) {
                onDocumentLoaded(path, pageCount);
            });

    connect(m_session->contentHandler(), &PDFContentHandler::documentError,
            this, &PDFDocumentTab::documentError);

    // 批注脏标志变化 → 复用未保存信号（驱动标签页圆点与 Save 可用态）
    if (AnnotationManager* am = m_session->annotationManager()) {
        connect(am, &AnnotationManager::dirtyChanged,
                this, [this](bool dirty) { emit unsavedChangesChanged(dirty); });
    }

    connect(m_session, &PDFDocumentSession::currentPageChanged,
            this, &PDFDocumentTab::onPageChanged);

    connect(m_session, &PDFDocumentSession::currentZoomChanged,
            this, &PDFDocumentTab::onZoomChanged);

    connect(m_session, &PDFDocumentSession::zoomSettingCompleted,
            this, [this](double zoom, ZoomMode mode) {
                if (zoom < 0) {

                    QSize viewportSize = m_scrollArea->viewport()->size();
                    m_session->viewHandler()->requestUpdateZoom(viewportSize, verticalScrollBarReserve());
                } else {
                    onZoomChanged(zoom);
                }
            });

    connect(m_session, &PDFDocumentSession::currentDisplayModeChanged,
            this, &PDFDocumentTab::onDisplayModeChanged);

    connect(m_session, &PDFDocumentSession::continuousScrollChanged,
            this, &PDFDocumentTab::onContinuousScrollChanged);

    connect(m_session, &PDFDocumentSession::pagePositionsChanged,
            this, &PDFDocumentTab::onPagePositionsChanged);

    connect(m_session, &PDFDocumentSession::currentRotationChanged,
            this, [this](int rotation) {
                renderAndUpdatePages();
            });

    connect(m_session, &PDFDocumentSession::scrollToPositionRequested,
            this, [this](int scrollY) {
                m_scrollArea->verticalScrollBar()->setValue(scrollY);
            });

    connect(m_session, &PDFDocumentSession::requestCurrentScrollPosition,
            this, [this]() {
                int scrollY = m_scrollArea->verticalScrollBar()->value();
                m_session->saveViewportState(scrollY);
            });

    connect(m_session, &PDFDocumentSession::textSelectionChanged,
            this, &PDFDocumentTab::onTextSelectionChanged);

    connect(m_session->interactionHandler(), &PDFInteractionHandler::internalLinkRequested,
            this, [this](int targetPage) {
                m_session->viewHandler()->requestGoToPage(targetPage);
            });

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

    connect(m_session, &PDFDocumentSession::searchCompleted,
            this, &PDFDocumentTab::onSearchCompleted);

    connect(m_pageWidget, &PDFPageWidget::pageClicked,
            this, &PDFDocumentTab::onPageClicked);

    connect(m_pageWidget, &PDFPageWidget::mouseMovedOnPage,
            this, &PDFDocumentTab::onMouseMovedOnPage);

    connect(m_pageWidget, &PDFPageWidget::mouseLeftAllPages,
            this, &PDFDocumentTab::onMouseLeftAllPages);

    connect(m_pageWidget, &PDFPageWidget::textSelectionDragging,
            this, &PDFDocumentTab::onTextSelectionDragging);

    connect(m_pageWidget, &PDFPageWidget::textSelectionEnded,
            this, &PDFDocumentTab::onTextSelectionEnded);

    connect(m_pageWidget, &PDFPageWidget::contextMenuRequested,
            this, &PDFDocumentTab::onContextMenuRequested);

    connect(m_pageWidget, &PDFPageWidget::visibleAreaChanged,
            this, &PDFDocumentTab::onVisibleAreaChanged);


    connect(m_scrollArea->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &PDFDocumentTab::onScrollValueChanged);

    connect(m_searchWidget, &SearchWidget::closeRequested,
            this, &PDFDocumentTab::hideSearchBar);

    connect(m_searchWidget, &SearchWidget::searchResultNavigated,
            this, [this](const SearchResult& result) {
                scrollToSearchResult(result);
                m_pageWidget->update();
            });

    connect(m_session, &PDFDocumentSession::paperEffectChanged,
            this, &PDFDocumentTab::paperEffectChanged);

    connect(m_session->contentHandler(), &PDFContentHandler::unsavedOutlineChangesChanged,
            this, &PDFDocumentTab::unsavedChangesChanged);



    connect(m_pageWidget, &PDFPageWidget::ocrHoverTriggered,
            this, &PDFDocumentTab::onOCRHoverTriggered);


    connect(&OCRManager::instance(), &OCRManager::ocrCompleted,
            this, &PDFDocumentTab::onOCRCompleted);

    connect(&OCRManager::instance(), &OCRManager::ocrFailed,
            this, &PDFDocumentTab::onOCRFailed);


    connect(m_ocrFloatingWidget, &OCRFloatingWidget::lookupRequested,
            this, &PDFDocumentTab::onLookupRequested);
}


bool PDFDocumentTab::loadDocument(const QString& filePath, QString* errorMessage)
{
    if (!m_session->loadDocument(filePath, errorMessage)) {
        return false;
    }
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
    return m_session->state()->documentPath();
}

bool PDFDocumentTab::hasUnsavedChanges() const
{
    if (!m_session) {
        return false;
    }
    if (m_session->contentHandler()->hasUnsavedOutlineChanges()) {
        return true;
    }
    AnnotationManager* am = m_session->annotationManager();
    return am && am->isDirty();
}

bool PDFDocumentTab::saveOutline()
{
    if (!m_session) {
        return false;
    }
    // 仅在目录确有改动时才写目录，避免只改批注时多余重写。
    bool ok = true;
    if (m_session->contentHandler()->hasUnsavedOutlineChanges()) {
        ok = m_session->contentHandler()->saveOutlineChanges(QString());
    }

    // 批注与目录并入同一保存手势（Cmd+S）：有脏批注则一并写回。
    AnnotationManager* am = m_session->annotationManager();
    if (am && am->isDirty()) {
        QString err;
        if (AnnotationPdfIO::save(m_session->renderer(), am, &err)) {
            // 写回后内存 doc 已移除 Ink，清缓存重渲，避免显示残留
            m_session->pageCache()->clear();
            refreshVisiblePages();
        } else {
            qWarning() << "PDFDocumentTab: annotation save failed:" << err;
            ok = false;
        }
    }
    return ok;
}

QString PDFDocumentTab::documentTitle() const
{
    if (documentPath().isEmpty()) {
        return tr("New Tab");
    }
    return QFileInfo(documentPath()).fileName();
}


void PDFDocumentTab::previousPage()
{
    m_session->viewHandler()->requestPreviousPage();
}

void PDFDocumentTab::nextPage()
{
    m_session->viewHandler()->requestNextPage();
}

void PDFDocumentTab::firstPage()
{
    m_session->viewHandler()->requestFirstPage();
}

void PDFDocumentTab::lastPage()
{
    m_session->viewHandler()->requestLastPage();
}

void PDFDocumentTab::goToPage(int pageIndex)
{
    m_session->viewHandler()->requestGoToPage(pageIndex);
}


void PDFDocumentTab::zoomIn()
{
    m_session->viewHandler()->requestZoomIn();
}

void PDFDocumentTab::zoomOut()
{
    m_session->viewHandler()->requestZoomOut();
}

void PDFDocumentTab::actualSize()
{
    m_session->viewHandler()->requestSetZoom(AppConfig::DEFAULT_ZOOM);
}

void PDFDocumentTab::fitPage()
{
    m_session->viewHandler()->requestSetZoomMode(ZoomMode::FitPage);
    updateScrollBarPolicy();
}

void PDFDocumentTab::fitWidth()
{
    m_session->viewHandler()->requestSetZoomMode(ZoomMode::FitWidth);
    updateScrollBarPolicy();
}

void PDFDocumentTab::setZoom(double zoom)
{
    m_session->viewHandler()->requestSetZoom(zoom);
}


void PDFDocumentTab::setDisplayMode(PageDisplayMode mode)
{
    if (mode != m_session->state()->currentDisplayMode()) {
        m_session->viewHandler()->requestSetDisplayMode(mode);
    }
}

void PDFDocumentTab::setContinuousScroll(bool continuous)
{
    m_session->viewHandler()->requestSetContinuousScroll(continuous);
}



void PDFDocumentTab::showSearchBar()
{
    if (!m_session->state()->isTextPDF()) {
        QMessageBox::information(this, tr("Search Unavailable"),
                                 tr("Scanned file contains no text"));
        return;
    }

    if (m_session->textCache()->isPreloading()) {
        int progress = m_session->textCache()->computePreloadProgress();

        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            tr("Extracting text..."),
            tr("Extracting text...(%1%).\n\nYou can only search pages with extracted text.\n\nContinue searching?").arg(progress),
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
    m_session->interactionHandler()->cancelSearch();
    m_session->interactionHandler()->clearSearchResults();   // 清空搜索结果，移除页面高亮标记
    m_pageWidget->clearHighlights();
    m_pageWidget->setFocus();
}

bool PDFDocumentTab::isSearchBarVisible() const
{
    return m_searchWidget && m_searchWidget->isVisible();
}


void PDFDocumentTab::copySelectedText()
{
    if (m_session->state()->hasTextSelection()) {
        m_session->interactionHandler()->copySelectedText();
    }
}

void PDFDocumentTab::selectAll()
{
    if (m_session->state()->isDocumentLoaded()) {
        m_session->interactionHandler()->selectAll(m_session->state()->currentPage());
    }
}


void PDFDocumentTab::setLinksVisible(bool visible)
{
    m_session->interactionHandler()->requestSetLinksVisible(visible);
    m_pageWidget->update();
}

bool PDFDocumentTab::linksVisible() const
{
    return m_session->state()->linksVisible();
}


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

QSize PDFDocumentTab::getViewportSize() const
{
    if (m_scrollArea && m_scrollArea->viewport()) {
        return m_scrollArea->viewport()->size();
    }
    return QSize(800, 600);
}

int PDFDocumentTab::verticalScrollBarReserve() const
{
    if (m_scrollArea && m_scrollArea->verticalScrollBar()) {
        return m_scrollArea->verticalScrollBar()->sizeHint().width();
    }
    return 0;
}

void PDFDocumentTab::updateZoom(const QSize& viewportSize)
{
    if (m_session) {
        m_session->viewHandler()->requestUpdateZoom(viewportSize, verticalScrollBarReserve());
    }
}

void PDFDocumentTab::findNext()
{
    if (m_session) {
        SearchResult result = m_session->interactionHandler()->findNext();
        if (result.isValid()) {
            scrollToSearchResult(result);
            m_pageWidget->update();
        }
    }
}

void PDFDocumentTab::findPrevious()
{
    if (m_session) {
        SearchResult result = m_session->interactionHandler()->findPrevious();
        if (result.isValid()) {
            scrollToSearchResult(result);
            m_pageWidget->update();
        }
    }
}

void PDFDocumentTab::scrollToSearchResult(const SearchResult& result)
{
    if (!m_session || !result.isValid()) {
        return;
    }

    const PDFDocumentState* state = m_session->state();

    // 非连续滚动模式下，先确保切到命中所在页（整页显示，无需页内滚动）
    if (!state->isContinuousScroll()) {
        if (result.pageIndex != state->currentPage()) {
            m_session->viewHandler()->requestGoToPage(result.pageIndex);
        }
        return;
    }

    const QVector<int>& pageYPositions = state->pageYPositions();
    if (result.pageIndex < 0 || result.pageIndex >= pageYPositions.size()) {
        return;
    }

    const double zoom = state->currentZoom();
    const int margin = AppConfig::PAGE_MARGIN;

    // 取该命中所有 quad 的包围盒（一个匹配可能跨多行/多个矩形）
    QRectF bounds = result.quads.first();
    for (const QRectF& quad : result.quads) {
        bounds = bounds.united(quad);
    }

    // 命中词在滚动内容坐标系中的上/下沿（像素）
    const int pageTop = pageYPositions[result.pageIndex] + margin;
    const int matchTop = pageTop + qRound(bounds.top() * zoom);
    const int matchBottom = pageTop + qRound(bounds.bottom() * zoom);

    QScrollBar* vBar = m_scrollArea->verticalScrollBar();
    const int viewTop = vBar->value();
    const int viewportH = m_scrollArea->viewport()->height();

    // 命中相对当前视口顶部的位置
    const int relTop = matchTop - viewTop;
    const int relBottom = matchBottom - viewTop;

    // 仅当命中已完整落在视口的「居中舒适带」内才不动，避免逐个切换时画面抖动；
    // 落在偏上/偏下（含 goToPage 后被晾在页顶）的情况都重新摆位，否则体验割裂。
    const int comfortTop = viewportH / 5;        // 20%
    const int comfortBottom = viewportH * 13 / 20; // 65%
    if (relTop >= comfortTop && relBottom <= comfortBottom) {
        return;
    }

    // 把命中词放到视口约 1/3 高度处，上下都留出上下文
    int targetY = matchTop - viewportH / 3;

    // 标记搜索定位进行中：goToPage 触发的重排会排一个"回到页顶"的延迟滚动，
    // 这里抑制它，并在其之后清除标记，确保本次精确定位是最终生效的滚动。
    m_inSearchScroll = true;
    vBar->setValue(qBound(vBar->minimum(), targetY, vBar->maximum()));
    QTimer::singleShot(0, this, [this]() {
        m_inSearchScroll = false;
    });
}

void PDFDocumentTab::onDocumentLoaded(const QString& filePath, int pageCount)
{
    // 侧边栏已改为公共单例，由 MainWindow 在 documentLoaded 后挂载/刷新

    // 读入已存 Ink 批注到 overlay，并从内存 doc 删除（避免双重绘制）。
    // 在首帧渲染前完成；若读到批注，loadStrokes 会重置脏标志为 false。
    if (AnnotationPdfIO::load(m_session->renderer(), m_session->annotationManager())) {
        m_session->pageCache()->clear();   // 清掉可能已含 Ink 的缓存
    }

    if (m_session->state()->isTextPDF()) {
        m_session->textCache()->startPreload();
    }

    QTimer::singleShot(0, this, [this]() {
        const PDFDocumentState* state = m_session->state();
        if (state->isDocumentLoaded()) {
            ZoomMode mode = state->currentZoomMode();
            if (mode == ZoomMode::FitWidth || mode == ZoomMode::FitPage) {
                QSize viewportSize = m_scrollArea->viewport()->size();
                qDebug() << "onDocumentLoaded 2, viewport:" << viewportSize;
                m_session->viewHandler()->requestUpdateZoom(viewportSize, verticalScrollBarReserve());
            }
        }
    });

    emit documentLoaded(filePath, pageCount);
}

void PDFDocumentTab::onPageChanged(int pageIndex)
{
    // 侧边栏当前页高亮由公共面板监听 session::currentPageChanged 完成

    renderAndUpdatePages();

    emit pageChanged(pageIndex);
}

void PDFDocumentTab::onZoomChanged(double zoom)
{
    renderAndUpdatePages();
    emit zoomChanged(zoom);
}

void PDFDocumentTab::onDisplayModeChanged(PageDisplayMode mode)
{
    updateScrollBarPolicy();
    m_session->textCache()->clear();


    if (m_session->state()->currentZoomMode() != ZoomMode::Custom) {
        QSize viewportSize = m_scrollArea->viewport()->size();
        m_session->viewHandler()->requestUpdateZoom(viewportSize, verticalScrollBarReserve());
    }

    renderAndUpdatePages();
    emit displayModeChanged(mode);
}

void PDFDocumentTab::onContinuousScrollChanged(bool continuous)
{
    updateScrollBarPolicy();


    if (m_session->state()->currentZoomMode() != ZoomMode::Custom) {
        QSize viewportSize = m_scrollArea->viewport()->size();
        m_session->viewHandler()->requestUpdateZoom(viewportSize, verticalScrollBarReserve());
    }

    renderAndUpdatePages();
    emit continuousScrollChanged(continuous);
}

void PDFDocumentTab::onPagePositionsChanged(const QVector<int>& positions, const QVector<int>& heights)
{
    QSize targetSize = m_pageWidget->calculateRequiredSize();
    m_pageWidget->resize(targetSize);

    refreshVisiblePages();

    if (m_session->state()->isContinuousScroll()) {
        if (!m_isUserScrolling) {
            int targetY = -1;


            if (m_session->state()->needRestoreViewport()) {
                targetY = m_session->state()->getRestoredScrollPosition(AppConfig::PAGE_MARGIN);
                m_session->clearViewportRestore();
            } else {

                int currentPage = m_session->state()->currentPage();
                targetY = m_session->viewHandler()->getScrollPositionForPage(currentPage, AppConfig::PAGE_MARGIN);
            }

            if (targetY >= 0) {
                QTimer::singleShot(0, this, [this, targetY]() {
                    // 搜索定位正在进行时，别把滚动拉回页顶，否则会覆盖命中词的精确位置
                    if (m_inSearchScroll) {
                        return;
                    }
                    m_scrollArea->verticalScrollBar()->setValue(targetY);
                });
            }
        }
    }
}

void PDFDocumentTab::onTextSelectionChanged(bool hasSelection)
{
    m_pageWidget->update();
    emit textSelectionChanged();
}

void PDFDocumentTab::onTextPreloadProgress(int current, int total)
{
    if (m_textPreloadProgress) {
        m_textPreloadProgress->setVisible(true);
        m_textPreloadProgress->setMaximum(total);
        m_textPreloadProgress->setValue(current);
        m_textPreloadProgress->setFormat(QString("%1/%2").arg(current).arg(total));
    }
}

void PDFDocumentTab::onTextPreloadCompleted()
{
    if (m_textPreloadProgress) {
        m_textPreloadProgress->setVisible(false);
    }
}

void PDFDocumentTab::onSearchCompleted(const QString& query, int totalMatches)
{
    m_pageWidget->update();
    emit searchCompleted(query, totalMatches);
}

void PDFDocumentTab::onPageClicked(int pageIndex, const QPointF& pagePos, Qt::MouseButton button, Qt::KeyboardModifiers modifiers)
{
    if (button != Qt::LeftButton) return;

    const PDFDocumentState* state = m_session->state();
    double zoom = state->currentZoom();


    if (state->linksVisible()) {
        const PDFLink* link = m_session->interactionHandler()->hitTestLink(pageIndex, pagePos, zoom);
        if (link) {
            m_session->interactionHandler()->handleLinkClick(link);
            return;
        }
    }


    if (state->isTextPDF()) {

        if (modifiers & Qt::ShiftModifier) {
            m_session->interactionHandler()->extendTextSelection(pageIndex, pagePos, zoom);
            return;
        }


        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 timeDiff = now - m_lastClickTime;

        const int doubleClickTime = QApplication::doubleClickInterval();

        if (timeDiff < doubleClickTime && (m_lastClickPos - QPoint(pagePos.x(), pagePos.y())).manhattanLength() < 5) {
            m_clickCount++;
        } else {
            m_clickCount = 1;
        }

        m_lastClickTime = now;
        m_lastClickPos = QPoint(pagePos.x(), pagePos.y());


        if (m_clickCount >= 3) {
            m_session->interactionHandler()->selectLine(pageIndex, pagePos, zoom);
            m_clickCount = 0;
        }

        else if (m_clickCount == 2) {
            m_session->interactionHandler()->selectWord(pageIndex, pagePos, zoom);
        }

        else {
            m_session->interactionHandler()->startTextSelection(pageIndex, pagePos, zoom);
            m_pageWidget->setTextSelectionMode(true);
        }
    }
}

void PDFDocumentTab::onMouseMovedOnPage(int pageIndex, const QPointF& pagePos)
{
    updateCursorForPage(pageIndex, pagePos);
}

void PDFDocumentTab::onMouseLeftAllPages()
{
    m_session->interactionHandler()->clearHoveredLink();
    QToolTip::hideText();
}

void PDFDocumentTab::onTextSelectionDragging(int pageIndex, const QPointF& pagePos)
{
    const PDFDocumentState* state = m_session->state();
    m_session->interactionHandler()->updateTextSelection(pageIndex, pagePos, state->currentZoom());
}

void PDFDocumentTab::onTextSelectionEnded()
{
    m_session->interactionHandler()->endTextSelection();
}

void PDFDocumentTab::onContextMenuRequested(int pageIndex, const QPointF& pagePos, const QPoint& globalPos)
{
    showContextMenu(pageIndex, pagePos, globalPos);
}

void PDFDocumentTab::onVisibleAreaChanged()
{
    refreshVisiblePages();
}

void PDFDocumentTab::onScrollValueChanged(int value)
{
    const PDFDocumentState* state = m_session->state();
    if (state->isContinuousScroll()) {
        m_isUserScrolling = true;

        m_session->updateCurrentPageFromScroll(value, AppConfig::PAGE_MARGIN);
        refreshVisiblePages();

        m_isUserScrolling = false;
    }
}


void PDFDocumentTab::renderAndUpdatePages()
{
    const PDFDocumentState* state = m_session->state();

    if (!state->isDocumentLoaded()) {
        return;
    }

    if (state->isContinuousScroll()) {
        m_session->calculatePagePositions();
    } else {

        QImage img1 = renderPage(state->currentPage());
        QImage img2;

        if (state->currentDisplayMode() == PageDisplayMode::DoublePage) {
            int nextPage = state->currentPage() + 1;
            if (nextPage < state->pageCount()) {
                img2 = renderPage(nextPage);
            }
        }

        m_pageWidget->setDisplayImages(img1, img2);
    }
}

QImage PDFDocumentTab::renderPage(int pageIndex)
{
    if (pageIndex < 0 || pageIndex >= m_session->state()->pageCount()) {
        return QImage();
    }

    const PDFDocumentState* state = m_session->state();
    double zoom = state->currentZoom();
    int rotation = state->currentRotation();
    double dpr = devicePixelRatioF();   // Retina 屏为 2.0，普通屏为 1.0

    PageCacheManager* cache = m_session->pageCache();
    PerThreadMuPDFRenderer* renderer = m_session->renderer();


    if (cache->contains(pageIndex, zoom, rotation, dpr)) {
        return cache->getPage(pageIndex, zoom, rotation, dpr);
    }


    auto result = renderer->renderPage(pageIndex, zoom, rotation, RenderScene::Page, dpr);
    if (result.success) {
        cache->addPage(pageIndex, zoom, rotation, dpr, result.image);
        return result.image;
    }

    return QImage();
}

void PDFDocumentTab::refreshVisiblePages()
{
    const PDFDocumentState* state = m_session->state();

    if (!state->isContinuousScroll()) {
        return;
    }

    if (!m_scrollArea || !m_scrollArea->viewport()) {
        return;
    }

    int scrollY = m_scrollArea->verticalScrollBar()->value();
    QRect visibleRect(0, scrollY, m_scrollArea->viewport()->width(), m_scrollArea->viewport()->height());


    QSet<int> visiblePages = m_session->viewHandler()->getVisiblePages(
        visibleRect,
        AppConfig::instance().preloadMargin(),
        AppConfig::PAGE_MARGIN
        );


    PageCacheManager* cache = m_session->pageCache();
    cache->markVisiblePages(visiblePages);


    double zoom = state->currentZoom();
    int rotation = state->currentRotation();
    double dpr = devicePixelRatioF();

    bool anyRendered = false;
    for (int pageIndex : visiblePages) {
        if (!cache->contains(pageIndex, zoom, rotation, dpr)) {
            renderPage(pageIndex);
            anyRendered = true;
        }
    }

    if (anyRendered) {
        m_pageWidget->update();
    }
}

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

void PDFDocumentTab::updateCursorForPage(int pageIndex, const QPointF& pagePos)
{
    const PDFDocumentState* state = m_session->state();
    double zoom = state->currentZoom();


    if (state->linksVisible()) {
        const PDFLink* link = m_session->interactionHandler()->hitTestLink(pageIndex, pagePos, zoom);
        if (link) {
            m_pageWidget->setCursor(Qt::PointingHandCursor);

            QString tooltip;
            if (link->isInternal()) {
                tooltip = tr("Jump to page %1").arg(link->targetPage + 1);
            } else if (link->isExternal()) {
                tooltip = tr("Open %1").arg(link->uri);
            }
            QToolTip::showText(QCursor::pos(), tooltip, m_pageWidget);
            return;
        }
    }

    QToolTip::hideText();


    if (state->isTextPDF()) {
        m_pageWidget->setCursor(Qt::IBeamCursor);
    } else {
        m_pageWidget->setCursor(Qt::ArrowCursor);
    }
}

void PDFDocumentTab::showContextMenu(int pageIndex, const QPointF& pagePos, const QPoint& globalPos)
{
    const PDFDocumentState* state = m_session->state();

    if (!state->isDocumentLoaded()) {
        return;
    }

    QMenu menu(this);


    if (state->hasTextSelection()) {
        QAction* copyAction = menu.addAction(tr("Copy"));
        copyAction->setShortcut(QKeySequence::Copy);
        connect(copyAction, &QAction::triggered, this, &PDFDocumentTab::copySelectedText);

        QString selectedText = m_session->interactionHandler()
                                   ? m_session->interactionHandler()->getSelectedText().trimmed()
                                   : QString();
        if (!selectedText.isEmpty()) {
            QAction* lookupAction = menu.addAction(tr("Lookup"));
            connect(lookupAction, &QAction::triggered, this, [this, selectedText]() {
                onLookupRequested(selectedText);
            });
        }

        menu.addSeparator();
    }


    if (state->isTextPDF()) {
        if (!state->hasTextSelection()) {
            double zoom = state->currentZoom();

            QAction* selectWordAction = menu.addAction(tr("Select Word"));
            connect(selectWordAction, &QAction::triggered, this, [=]() {
                m_session->interactionHandler()->selectWord(pageIndex, pagePos, zoom);
            });

            QAction* selectLineAction = menu.addAction(tr("Select Line"));
            connect(selectLineAction, &QAction::triggered, this, [=]() {
                m_session->interactionHandler()->selectLine(pageIndex, pagePos, zoom);
            });

            menu.addSeparator();
        }

        QAction* selectAllAction = menu.addAction(tr("Select All"));
        selectAllAction->setShortcut(QKeySequence::SelectAll);
        connect(selectAllAction, &QAction::triggered, this, &PDFDocumentTab::selectAll);
    }

    if (!menu.isEmpty()) {
        menu.exec(globalPos);
    }
}

void PDFDocumentTab::setPaperEffectEnabled(bool enabled)
{
    if (m_session) {
        m_session->setPaperEffectEnabled(enabled);
        renderAndUpdatePages();
        emit paperEffectChanged(enabled);
    }
}

bool PDFDocumentTab::paperEffectEnabled() const
{
    return m_session ? m_session->renderer()->paperEffectEnabled() : false;
}

void PDFDocumentTab::updateOCRHoverState()
{
    bool enabled = OCRManager::instance().isOCRHoverEnabled();


    if (!isDocumentLoaded() || isTextPDF()) {
        enabled = false;
    }


    if (m_pageWidget) {
        m_pageWidget->setOCRHoverEnabled(enabled);
    }


    if (!enabled && m_ocrFloatingWidget) {
        m_ocrFloatingWidget->hideFloating();
    }
}

void PDFDocumentTab::onOCRHoverTriggered(const QImage& image, const QRect& regionRect, const QPoint& lastHoverPos)
{
    if (!OCRManager::instance().isOCRHoverEnabled()) {
        return;
    }


    if (!OCRManager::instance().isReady()) {
        qWarning() << "OCR not ready";


        if (m_ocrFloatingWidget) {
            QRect globalRect = regionRect.translated(m_pageWidget->mapToGlobal(QPoint(0, 0)));
            m_ocrFloatingWidget->showRecognizing(image, globalRect);


            QTimer::singleShot(100, this, [this]() {
                if (m_ocrFloatingWidget) {
                    m_ocrFloatingWidget->updateResult(tr("OCR engine not ready"), 0.0f);
                }
            });
        }
        return;
    }

    m_lastOCRImage = image;
    m_lastOCRRegion = regionRect;
    m_lastHoverPos = lastHoverPos;


    if (m_ocrFloatingWidget) {
        QRect globalRect = regionRect.translated(m_pageWidget->mapToGlobal(QPoint(0, 0)));
        m_ocrFloatingWidget->showRecognizing(image, globalRect);
    }


    OCRManager::instance().requestOCR(image, regionRect, lastHoverPos);
}

void PDFDocumentTab::onOCRCompleted(const QVector<TokenWithPosition>& tokens, const QRect& regionRect, const QPoint& lastHoverPos)
{
    if (!OCRManager::instance().isOCRHoverEnabled()) {
        return;
    }
    if (tokens.isEmpty()) {
        qDebug() << "OCR result empty";
        if (m_ocrFloatingWidget) {
            m_ocrFloatingWidget->updateResult(QString(), 0.0f);
        }
        return;
    }

    qDebug() << "onOCRCompleted TokenWithPosition size:" << tokens.size();

    // token.estimatedRect 是 OCR 输入图（zoom*dpr 物理像素）上的坐标。
    // lastHoverPos / regionRect 都是控件逻辑像素，Retina 下需乘 dpr 才能对齐。
    QPoint posInRegion = lastHoverPos - regionRect.topLeft();
    qreal dpr = m_pageWidget ? m_pageWidget->devicePixelRatioF() : devicePixelRatioF();
    QPoint posInRegionPx(qRound(posInRegion.x() * dpr),
                         qRound(posInRegion.y() * dpr));

    TokenWithPosition closestToken =
        ChineseTokenizer::instance().findClosestToken(tokens, posInRegionPx);

    QString targetWord = closestToken.isValid() ? closestToken.word : QString();
    if (closestToken.isValid()) {
        qInfo() << "Selected word from OCR result:" << targetWord;
    }

    if (m_ocrFloatingWidget) {
        m_ocrFloatingWidget->updateResult(targetWord, closestToken.confidence);
    }
}

void PDFDocumentTab::onOCRFailed(const QString& error)
{
    if (!OCRManager::instance().isOCRHoverEnabled()) {
        return;
    }

    qWarning() << "OCR failed:" << error;


    if (m_ocrFloatingWidget) {
        m_ocrFloatingWidget->updateResult(tr("Recognition failed: %1").arg(error), 0.0f);
    }
}

void PDFDocumentTab::triggerOCRAtCurrentPosition()
{
    if (!m_pageWidget) {
        return;
    }


    m_pageWidget->triggerOCRAtCurrentPosition();
}

void PDFDocumentTab::onLookupRequested(const QString& text)
{
    // 先拷一份：OCR 悬浮卡片用 emit lookupRequested(m_currentText) 直连过来，
    // text 实际是 m_currentText 的引用；下面的 hideFloating() 会 clear() 它，
    // 不先存下来，传给 lookup() 的就成了空串（词典打不开）。
    const QString word = text;

    if (m_ocrFloatingWidget) {
        m_ocrFloatingWidget->hideFloating();
    }

    if (!DictionaryConnector::instance().lookup(word)) {
        QMessageBox::warning(
            this, tr("Lookup Failed"),
            DictionaryConnector::instance().isConfigured()
                ? tr("Failed to launch the external dictionary. Please check the dictionary command in Settings.")
                : tr("Dictionary command is not configured. Please set the external dictionary command in Settings."));
    }
}
