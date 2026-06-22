#include "navigationpanel.h"
#include "outlinewidget.h"
#include "thumbnailwidget.h"
#include "annotationwidget.h"
#include "pdfdocumentsession.h"
#include "pdfcontenthandler.h"
#include "pdfviewhandler.h"
#include "outlineeditor.h"
#include "thumbnailmanagerv2.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QToolButton>
#include <QTimer>
#include <QDebug>
#include <QFile>
#include <QPainter>
#include <QStyleOptionTab>
#include <QIcon>
#include <QStringList>
#include "stylemanager.h"
#include "themedicon.h"

// 竖排图标 Tab（侧边导航）：图标居中 + 选中态左侧主色条，颜色全部取自 StyleManager token
class IconTabBar : public QTabBar
{
public:
    IconTabBar(QWidget* parent = nullptr) : QTabBar(parent) {}

    QSize tabSizeHint(int index) const override
    {
        Q_UNUSED(index);
        return QSize(44, 48);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        StyleManager& sm = StyleManager::instance();
        const QColor accent    = sm.getColor("primary");
        const QColor surface   = sm.getColor("surface");
        const QColor hoverBg    = sm.getColor("hover");
        const QColor iconActive = sm.getColor("primary");
        const QColor iconIdle   = sm.getColor("textSecondary");

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        for (int i = 0; i < count(); i++) {
            QStyleOptionTab opt;
            initStyleOption(&opt, i);
            QRect rect = tabRect(i);

            const bool selected = opt.state & QStyle::State_Selected;
            const bool hovered  = opt.state & QStyle::State_MouseOver;

            if (selected) {
                painter.fillRect(rect, surface);
                // 左侧 2px 主色强调条
                painter.fillRect(rect.left(), rect.top(), 2, rect.height(), accent);
            } else if (hovered) {
                painter.fillRect(rect, hoverBg);
            }

            // 图标按 index 取名，用 ThemedIcon 渲染对应状态颜色的单色 SVG
            if (i < m_iconNames.size()) {
                const int iconSize = 20;
                QRect iconRect(
                    rect.left() + (rect.width() - iconSize) / 2,
                    rect.top() + (rect.height() - iconSize) / 2,
                    iconSize, iconSize);
                QPixmap pm = ThemedIcon::coloredPixmap(
                    m_iconNames.at(i),
                    selected ? iconActive : iconIdle,
                    iconSize);
                painter.drawPixmap(iconRect, pm);
            }
        }
    }

public:
    void setIconNames(const QStringList& names) { m_iconNames = names; }

private:
    QStringList m_iconNames;
};

class CustomTabWidget : public QTabWidget
{
public:
    CustomTabWidget(QWidget* parent = nullptr) : QTabWidget(parent)
    {
        m_iconBar = new IconTabBar(this);
        setTabBar(m_iconBar);
    }

    void setIconNames(const QStringList& names) { m_iconBar->setIconNames(names); }

private:
    IconTabBar* m_iconBar;
};

NavigationPanel::NavigationPanel(QWidget* parent)
    : QWidget(parent)
    , m_session(nullptr)
    , m_tabWidget(nullptr)
    , m_outlineWidget(nullptr)
    , m_thumbnailWidget(nullptr)
    , m_annotationWidget(nullptr)
    , m_saveOutlineBtn(nullptr)
    , m_expandAllBtn(nullptr)
    , m_collapseAllBtn(nullptr)
    , m_thumbnailStatusLabel(nullptr)
    , m_thumbnailProgressBar(nullptr)
{
    // 公共面板：UI 与子控件常驻，数据源由 attachSession 动态绑定
    setupUI();
    setupConnections();
}

NavigationPanel::~NavigationPanel()
{
    clear();
}

void NavigationPanel::attachSession(PDFDocumentSession* session)
{
    if (m_session == session) {
        return;
    }

    // 切走旧文档：保存其视图状态、断开连接、清空 UI
    detachSession();

    m_session = session;
    if (!m_session) {
        return;
    }

    // session 销毁时清除其残留的视图状态记录（防止指针复用读到旧值）。
    // 注意：Qt::UniqueConnection 不支持 lambda，这里用 m_trackedSessions 自行去重，
    // 该连接随 session 生命周期存在，不放入 m_sessionConns（detach 时不断开）。
    if (!m_trackedSessions.contains(m_session)) {
        m_trackedSessions.insert(m_session);
        connect(m_session, &QObject::destroyed, this, [this](QObject* obj) {
            PDFDocumentSession* s = static_cast<PDFDocumentSession*>(obj);
            m_viewStates.remove(s);
            m_trackedSessions.remove(s);
        });
    }

    PDFContentHandler* ch = m_session->contentHandler();

    // 重绑定子控件数据源
    m_outlineWidget->setContentHandler(ch);
    if (ch && ch->thumbnailManager()) {
        m_thumbnailWidget->setThumbnailManager(ch->thumbnailManager());
    }
    m_annotationWidget->setManager(m_session->annotationManager());
    m_annotationWidget->setHandler(m_session->annotationHandler());
    m_annotationWidget->setCurrentPage(m_session->state()->currentPage());

    // 建立与当前 session 的连接（detach 时统一断开）
    m_sessionConns << connect(this, &NavigationPanel::pageJumpRequested,
                              this, [this](int pageIndex) {
                                  if (m_session) m_session->viewHandler()->requestGoToPage(pageIndex);
                              });

    m_sessionConns << connect(m_session, &PDFDocumentSession::currentPageChanged,
                              this, &NavigationPanel::updateCurrentPage);

    m_sessionConns << connect(ch, &PDFContentHandler::outlineLoaded,
                              this, [this](bool success, int itemCount) {
                                  if (success && m_outlineWidget) {
                                      m_outlineWidget->loadOutline();
                                      qInfo() << "NavigationPanel: Outline loaded with" << itemCount << "items";
                                  }
                              });

    if (ch) {
        m_sessionConns << connect(ch, &PDFContentHandler::thumbnailsInitialized,
                                  this, [this](int pageCount) {
                                      qInfo() << "NavigationPanel: Initializing" << pageCount << "thumbnail placeholders";
                                      m_thumbnailWidget->initializeThumbnails(pageCount);
                                  });
    }

    m_sessionConns << connect(ch, &PDFContentHandler::thumbnailLoaded,
                              this, [this](int pageIndex, const QImage& thumbnail) {
                                  m_thumbnailWidget->onThumbnailLoaded(pageIndex, thumbnail);
                              });

    OutlineEditor* editor = ch->outlineEditor();
    // 保存按钮可用态跟随“未保存改动”：有改动才点亮，存盘后自动熄灭。
    m_saveOutlineBtn->setEnabled(editor && editor->hasUnsavedChanges());
    if (editor) {
        m_sessionConns << connect(editor, &OutlineEditor::unsavedChangesChanged,
                                  m_saveOutlineBtn, &QToolButton::setEnabled);
        m_sessionConns << connect(editor, &OutlineEditor::saveCompleted,
                                  this, [this](bool success, const QString& errorMsg) {
                                      if (success) {
                                          qInfo() << "NavigationPanel: Outline saved successfully";
                                      } else {
                                          qWarning() << "NavigationPanel: Failed to save outline:" << errorMsg;
                                      }
                                  });
    }

    if (ch && ch->thumbnailManager()) {
        ThumbnailManagerV2* manager = ch->thumbnailManager();

        m_sessionConns << connect(manager, &ThumbnailManagerV2::loadingStarted,
                                  this, [this](int totalPages, const QString& strategy) {
                                      qInfo() << "Thumbnail loading started:" << strategy << "for" << totalPages << "pages";
                                      m_thumbnailStatusLabel->setText(tr("Loading started..."));
                                  });

        m_sessionConns << connect(manager, &ThumbnailManagerV2::loadingStatusChanged,
                                  this, [this](const QString& status) {
                                      m_thumbnailStatusLabel->setText(status);
                                  });

        m_sessionConns << connect(manager, &ThumbnailManagerV2::batchCompleted,
                                  this, [this](int current, int total) {
                                      m_thumbnailProgressBar->setVisible(true);
                                      m_thumbnailProgressBar->setMaximum(total);
                                      m_thumbnailProgressBar->setValue(current);
                                      m_thumbnailProgressBar->setFormat(QString("%1/%2").arg(current).arg(total));
                                  });

        m_sessionConns << connect(manager, &ThumbnailManagerV2::allCompleted,
                                  this, [this]() {
                                      m_thumbnailStatusLabel->setText(tr("Loading complete!"));
                                      m_thumbnailProgressBar->setVisible(false);

                                      QTimer::singleShot(3000, this, [this]() {
                                          if (m_thumbnailStatusLabel) {
                                              m_thumbnailStatusLabel->setText(tr("Loaded successfully!"));
                                          }
                                      });
                                  });

        m_sessionConns << connect(manager, &ThumbnailManagerV2::loadProgress,
                                  this, [this](int current, int total) {
                                      if (total > 0) {
                                          int percentage = current * 100 / total;
                                          m_thumbnailStatusLabel->setText(
                                              tr("Loading: %1/%2 (%3%)")
                                                  .arg(current)
                                                  .arg(total)
                                                  .arg(percentage)
                                              );
                                      }
                                  });
    }

    // 文档已加载则立即重建数据并恢复视图状态
    if (m_session->state()->isDocumentLoaded()) {
        loadDocument(m_session->state()->pageCount());
        updateCurrentPage(m_session->state()->currentPage());
        restoreViewState();
    }
}

void NavigationPanel::detachSession()
{
    if (m_session) {
        saveViewState();
    }

    for (const QMetaObject::Connection& conn : m_sessionConns) {
        disconnect(conn);
    }
    m_sessionConns.clear();

    clear();
    if (m_saveOutlineBtn) {
        m_saveOutlineBtn->setEnabled(false);
    }
    m_outlineWidget->setContentHandler(nullptr);
    m_thumbnailWidget->setThumbnailManager(nullptr);
    m_annotationWidget->setManager(nullptr);
    m_annotationWidget->setHandler(nullptr);

    m_session = nullptr;
}

void NavigationPanel::saveViewState()
{
    if (!m_session) {
        return;
    }
    NavViewState st;
    st.subTabIndex = m_tabWidget->currentIndex();
    if (m_thumbnailWidget && m_thumbnailWidget->verticalScrollBar()) {
        st.thumbScroll = m_thumbnailWidget->verticalScrollBar()->value();
    }
    m_viewStates[m_session] = st;
}

void NavigationPanel::restoreViewState()
{
    if (!m_session) {
        return;
    }

    NavViewState st = m_viewStates.value(m_session);

    // 打开或切回文档时始终按 PDF 是否含目录决定默认页签，不能被上一次
    // 用户停留的侧边栏页签覆盖。
    const bool hasOutline = m_session->contentHandler() &&
                            m_session->contentHandler()->hasOutline();
    const int subTab = hasOutline ? 0 : 1;
    m_tabWidget->setCurrentIndex(subTab);

    // 缩略图滚动位置需等占位重建/布局完成后再恢复
    const int thumbScroll = st.thumbScroll;
    QTimer::singleShot(150, this, [this, thumbScroll]() {
        if (m_thumbnailWidget && m_thumbnailWidget->verticalScrollBar()) {
            m_thumbnailWidget->verticalScrollBar()->setValue(thumbScroll);
        }
    });
}

void NavigationPanel::loadDocument(int pageCount)
{
    if (!m_session || pageCount <= 0) {
        return;
    }

    qInfo() << "NavigationPanel: Loading document with" << pageCount << "pages";

    PDFContentHandler* ch = m_session->contentHandler();

    // 关键：区分「首次加载」与「切回」。
    // OutlineManager::loadOutline() 每次都会 delete 整棵树并重新从 PDF 解析，
    // 会丢掉未保存的目录编辑。因此切回已解析过目录的文档时，只用内存中的树
    // 重建侧栏 widget，绝不重新解析。
    bool outlineAlreadyParsed = ch && ch->outlineRoot() != nullptr;
    if (outlineAlreadyParsed) {
        m_outlineWidget->loadOutline();
    } else {
        // 首次：解析 PDF 目录（emit outlineLoaded → 触发 widget 重建）
        ch->loadOutline();
    }

    // emit thumbnailsInitialized → initializeThumbnails（含已缓存缩略图回填）
    ch->loadThumbnails();
}

void NavigationPanel::clear()
{
    if (m_outlineWidget) {
        m_outlineWidget->clear();
    }
    if (m_thumbnailWidget) {
        m_thumbnailWidget->clear();
    }
    if (m_thumbnailStatusLabel) {
        m_thumbnailStatusLabel->setText(QString());
    }
    if (m_thumbnailProgressBar) {
        m_thumbnailProgressBar->setVisible(false);
    }
}

void NavigationPanel::onTabChanged(int index) {
    if (m_session) {
        updateCurrentPage(m_session->state()->currentPage());
    }

    if (index == 1 && m_thumbnailWidget) {
        QTimer::singleShot(50, this, [this]() {
            if (m_thumbnailWidget) {
                QSet<int> unloadedVisible = m_thumbnailWidget->getUnloadedVisiblePages();
                if (!unloadedVisible.isEmpty()) {
                    qInfo() << "NavigationPanel: Tab switched, found" << unloadedVisible.size() << "unloaded visible pages";
                    if (m_session && m_session->contentHandler()) {
                        m_session->contentHandler()->syncLoadUnloadedPages(unloadedVisible);
                    }
                }
            }
        });
    }
}

void NavigationPanel::updateCurrentPage(int pageIndex)
{
    // 仅当前可见页签可为了当前页自动滚动。此前每次页码变化都会同时调用
    // 目录的 scrollToItem 和缩略图的 ensureWidgetVisible，导致用户在一个页签
    // 手动浏览时，另外两个页签的滚动位置也被悄悄改写。
    const int activeTab = m_tabWidget ? m_tabWidget->currentIndex() : -1;

    if (m_outlineWidget) {
        m_outlineWidget->highlightCurrentPage(pageIndex, activeTab == 0);
    }

    if (m_thumbnailWidget) {
        m_thumbnailWidget->highlightCurrentPage(pageIndex, activeTab == 1);
    }

    if (m_annotationWidget) {
        m_annotationWidget->setCurrentPage(pageIndex);
    }
}

void NavigationPanel::showAnnotationTab()
{
    if (m_annotationTabIndex >= 0) {
        m_tabWidget->setCurrentIndex(m_annotationTabIndex);
    }
    if (m_annotationWidget) {
        m_annotationWidget->activatePen();
    }
}

void NavigationPanel::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_tabWidget = new CustomTabWidget(this);
    m_tabWidget->setObjectName("navigationTabWidget");
    m_tabWidget->setDocumentMode(true);
    m_tabWidget->setMinimumWidth(200);
    m_tabWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    QWidget* outlineTab = new QWidget(this);
    QHBoxLayout* outlineRootLayout = new QHBoxLayout(outlineTab);
    outlineRootLayout->setContentsMargins(0, 0, 0, 0);
    outlineRootLayout->setSpacing(0);
    QWidget* outlineContent = new QWidget(outlineTab);
    QVBoxLayout* outlineLayout = new QVBoxLayout(outlineContent);
    outlineLayout->setContentsMargins(0, 0, 0, 0);
    outlineLayout->setSpacing(0);

    QWidget* outlineToolbar = new QWidget(this);
    outlineToolbar->setObjectName("outlineToolbar");
    outlineToolbar->setFixedHeight(44);
    outlineToolbar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    QHBoxLayout* toolbarLayout = new QHBoxLayout(outlineToolbar);
    toolbarLayout->setContentsMargins(12, 8, 12, 8);
    toolbarLayout->setSpacing(8);

    m_addOutlineBtn = new QToolButton(this);
    m_addOutlineBtn->setIcon(QIcon(":icons/resources/icons/plus.svg"));
    m_addOutlineBtn->setToolTip(tr("Add Outline Item (at current page)"));
    m_addOutlineBtn->setObjectName("outlineToolButton");
    m_addOutlineBtn->setFixedSize(28, 28);
    m_addOutlineBtn->setIconSize(QSize(16, 16));

    m_deleteOutlineBtn = new QToolButton(this);
    m_deleteOutlineBtn->setIcon(QIcon(":icons/resources/icons/trash.svg"));
    m_deleteOutlineBtn->setToolTip(tr("Delete Selected Outline Item"));
    m_deleteOutlineBtn->setObjectName("outlineToolButton");
    m_deleteOutlineBtn->setFixedSize(28, 28);
    m_deleteOutlineBtn->setIconSize(QSize(16, 16));

    // 保存：目录的主操作。常态禁用，仅当有未保存改动时点亮（见 attachSession）。
    // “删除全部”这类危险操作只保留在右键菜单，避免常驻工具栏误触。
    m_saveOutlineBtn = new QToolButton(this);
    m_saveOutlineBtn->setIcon(QIcon(":icons/resources/icons/save.svg"));
    m_saveOutlineBtn->setToolTip(tr("Save Outline to PDF"));
    m_saveOutlineBtn->setObjectName("outlineToolButton");
    m_saveOutlineBtn->setFixedSize(28, 28);
    m_saveOutlineBtn->setIconSize(QSize(16, 16));
    m_saveOutlineBtn->setEnabled(false);

    m_expandAllBtn = new QToolButton(this);
    m_expandAllBtn->setIcon(QIcon(":icons/resources/icons/expand.svg"));
    m_expandAllBtn->setToolTip(tr("Expand All"));
    m_expandAllBtn->setObjectName("outlineToolButton");
    m_expandAllBtn->setFixedSize(28, 28);
    m_expandAllBtn->setIconSize(QSize(16, 16));

    m_collapseAllBtn = new QToolButton(this);
    m_collapseAllBtn->setIcon(QIcon(":icons/resources/icons/fold.svg"));
    m_collapseAllBtn->setToolTip(tr("Collapse All"));
    m_collapseAllBtn->setObjectName("outlineToolButton");
    m_collapseAllBtn->setFixedSize(28, 28);
    m_collapseAllBtn->setIconSize(QSize(16, 16));

    toolbarLayout->addWidget(m_addOutlineBtn);
    toolbarLayout->addWidget(m_deleteOutlineBtn);
    toolbarLayout->addWidget(m_saveOutlineBtn);
    toolbarLayout->addStretch();
    toolbarLayout->addWidget(m_expandAllBtn);
    toolbarLayout->addWidget(m_collapseAllBtn);

    outlineLayout->addWidget(outlineToolbar);

    // 数据源延迟绑定（attachSession 时 setContentHandler）
    m_outlineWidget = new OutlineWidget(nullptr, this);
    m_outlineWidget->setObjectName("outlineWidget");
    m_outlineWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    outlineLayout->addWidget(m_outlineWidget, 1);
    outlineRootLayout->addWidget(outlineContent, 1);
    m_outlineScrollBar = new QScrollBar(Qt::Vertical, outlineTab);
    m_outlineScrollBar->setObjectName("outlinePanelScrollBar");
    m_outlineScrollBar->setFixedWidth(10);
    // 与批注面板一致：始终保留右侧 10px 的布局区域，但无溢出时不绘制滑块。
    m_outlineScrollBar->setEnabled(false);
    outlineRootLayout->addWidget(m_outlineScrollBar);
    outlineTab->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QWidget* thumbnailTab = new QWidget(this);
    QVBoxLayout* thumbnailLayout = new QVBoxLayout(thumbnailTab);
    thumbnailLayout->setContentsMargins(0, 0, 0, 0);
    thumbnailLayout->setSpacing(0);

    m_thumbnailWidget = new ThumbnailWidget(this);
    m_thumbnailWidget->setObjectName("thumbnailWidget");
    m_thumbnailWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QWidget* statusBar = new QWidget(this);
    statusBar->setObjectName("thumbnailStatusBar");
    statusBar->setFixedHeight(32);

    QHBoxLayout* statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(12, 4, 12, 4);
    statusLayout->setSpacing(8);

    m_thumbnailStatusLabel = new QLabel(QString(), this);
    m_thumbnailStatusLabel->setObjectName("thumbnailStatusLabel");
    QFont statusFont = m_thumbnailStatusLabel->font();
    statusFont.setPointSize(9);
    m_thumbnailStatusLabel->setFont(statusFont);

    m_thumbnailProgressBar = new QProgressBar(this);
    m_thumbnailProgressBar->setObjectName("thumbnailProgressBar");
    m_thumbnailProgressBar->setMaximumWidth(150);
    m_thumbnailProgressBar->setMaximumHeight(18);
    m_thumbnailProgressBar->setTextVisible(true);
    m_thumbnailProgressBar->setVisible(false);

    statusLayout->addWidget(m_thumbnailStatusLabel, 1);
    statusLayout->addWidget(m_thumbnailProgressBar);

    thumbnailLayout->addWidget(m_thumbnailWidget, 1);
    thumbnailLayout->addWidget(statusBar);

    thumbnailTab->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // 批注 Tab
    QWidget* annotationTab = new QWidget(this);
    QHBoxLayout* annotationRootLayout = new QHBoxLayout(annotationTab);
    annotationRootLayout->setContentsMargins(0, 0, 0, 0);
    annotationRootLayout->setSpacing(0);
    QWidget* annotationContent = new QWidget(annotationTab);
    QVBoxLayout* annotationLayout = new QVBoxLayout(annotationContent);
    annotationLayout->setContentsMargins(0, 0, 0, 0);
    annotationLayout->setSpacing(0);

    m_annotationWidget = new AnnotationWidget(this);
    m_annotationWidget->setObjectName("annotationWidget");
    m_annotationWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    annotationLayout->addWidget(m_annotationWidget, 1);
    annotationRootLayout->addWidget(annotationContent, 1);
    m_annotationScrollBar = new QScrollBar(Qt::Vertical, annotationTab);
    m_annotationScrollBar->setObjectName("annotationPanelScrollBar");
    m_annotationScrollBar->setFixedWidth(10);
    m_annotationScrollBar->setEnabled(false);
    annotationRootLayout->addWidget(m_annotationScrollBar);
    annotationTab->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto mirrorScrollBar = [](QScrollBar* source, QScrollBar* target) {
        QObject::connect(source, &QScrollBar::rangeChanged, target,
                         [target](int minimum, int maximum) { target->setRange(minimum, maximum); });
        QObject::connect(source, &QScrollBar::valueChanged, target, &QScrollBar::setValue);
        QObject::connect(target, &QScrollBar::valueChanged, source, &QScrollBar::setValue);
        target->setRange(source->minimum(), source->maximum());
        target->setPageStep(source->pageStep());
        target->setSingleStep(source->singleStep());
    };
    mirrorScrollBar(m_outlineWidget->verticalScrollBar(), m_outlineScrollBar);
    auto updateOutlineScrollBarState = [this](int minimum, int maximum) {
        m_outlineScrollBar->setEnabled(maximum > minimum);
    };
    connect(m_outlineWidget->verticalScrollBar(), &QScrollBar::rangeChanged,
            this, updateOutlineScrollBarState);
    updateOutlineScrollBarState(m_outlineWidget->verticalScrollBar()->minimum(),
                                m_outlineWidget->verticalScrollBar()->maximum());
    mirrorScrollBar(m_annotationWidget->listScrollBar(), m_annotationScrollBar);
    connect(m_annotationWidget, &AnnotationWidget::listEmptyChanged, this,
            [this](bool empty) { m_annotationScrollBar->setEnabled(!empty); });

    // Tab 用图标 + tooltip 表达（规避竖排文字与国际化问题）
    // 图标由 IconTabBar 自绘，名称在此注册，颜色跟随主题
    int outlineIdx = m_tabWidget->addTab(outlineTab, QString());
    int thumbIdx   = m_tabWidget->addTab(thumbnailTab, QString());
    int annotIdx   = m_tabWidget->addTab(annotationTab, QString());
    m_annotationTabIndex = annotIdx;
    m_tabWidget->setTabToolTip(outlineIdx, tr("Outline"));
    m_tabWidget->setTabToolTip(thumbIdx, tr("Thumbnails"));
    m_tabWidget->setTabToolTip(annotIdx, tr("Annotations"));
    static_cast<CustomTabWidget*>(m_tabWidget)->setIconNames({"tab-outline", "tab-thumbnail", "edit"});

    m_tabWidget->setTabPosition(QTabWidget::West);
    m_tabWidget->setUsesScrollButtons(false);

    mainLayout->addWidget(m_tabWidget, 1);

    connect(m_tabWidget, &QTabWidget::currentChanged,
            this, &NavigationPanel::onTabChanged);

    setMinimumWidth(180);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
}

void NavigationPanel::setupConnections()
{
    // 仅建立与常驻子控件相关的永久连接；
    // 与具体 session/contentHandler/manager 相关的连接在 attachSession 中建立、
    // detachSession 中断开。下方 lambda 内对 m_session 运行时判空即可。

    connect(m_outlineWidget, &OutlineWidget::pageJumpRequested,
            this, &NavigationPanel::pageJumpRequested);

    connect(m_outlineWidget, &OutlineWidget::externalLinkRequested,
            this, [this](const QString& uri) {
                QUrl url(uri);
                if (url.isValid()) {
                    if (!QDesktopServices::openUrl(url)) {
                        QMessageBox::warning(this, tr("Failed to Open Link"),
                                             tr("Failed to open link:\n%1").arg(uri));
                    }
                } else {
                    QMessageBox::warning(this, tr("Invalid Link"),
                                         tr("Invalid link:\n%1").arg(uri));
                }
                emit externalLinkRequested(uri);
            });

    connect(m_addOutlineBtn, &QToolButton::clicked,
            m_outlineWidget, &OutlineWidget::addNewOutlineItem);
    connect(m_deleteOutlineBtn, &QToolButton::clicked,
            m_outlineWidget, &OutlineWidget::onDeleteOutline);
    connect(m_saveOutlineBtn, &QToolButton::clicked,
            m_outlineWidget, &OutlineWidget::onSaveToDocument);
    connect(m_expandAllBtn, &QToolButton::clicked,
            m_outlineWidget, &OutlineWidget::expandAll);
    connect(m_collapseAllBtn, &QToolButton::clicked,
            m_outlineWidget, &OutlineWidget::collapseAll);

    connect(m_thumbnailWidget, &ThumbnailWidget::pageJumpRequested,
            this, &NavigationPanel::pageJumpRequested);

    connect(m_annotationWidget, &AnnotationWidget::pageJumpRequested,
            this, &NavigationPanel::pageJumpRequested);

    connect(m_thumbnailWidget, &ThumbnailWidget::visibleRangeChanged,
            this, [this](const QSet<int>& visibleIndices, int margin) {
                if (m_session && m_session->contentHandler()) {
                    m_session->contentHandler()->handleVisibleRangeChanged(visibleIndices, margin);
                }
            });

    connect(m_thumbnailWidget, &ThumbnailWidget::slowScrollDetected,
            this, [this](const QSet<int>& visiblePages) {
                if (m_session && m_session->contentHandler()) {
                    m_session->contentHandler()->handleVisibleRangeChanged(visiblePages, 0);
                }
            });

    connect(m_thumbnailWidget, &ThumbnailWidget::syncLoadRequested,
            this, [this](const QSet<int>& unloadedVisible) {
                if (m_session && m_session->contentHandler()) {
                    m_session->contentHandler()->syncLoadUnloadedPages(unloadedVisible);
                }
            });

    connect(m_thumbnailWidget, &ThumbnailWidget::initialVisibleReady,
            this, [this](const QSet<int>& initialVisible) {
                if (m_session && m_session->contentHandler()) {
                    m_session->contentHandler()->startInitialThumbnailLoad(initialVisible);
                }
            });
}

void NavigationPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    updateGeometry();

    if (m_tabWidget) {
        m_tabWidget->updateGeometry();
    }

    if (m_outlineWidget) {
        m_outlineWidget->updateGeometry();
        m_outlineWidget->viewport()->update();
    }
}
