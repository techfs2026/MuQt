#include "navigationpanel.h"
#include "outlinewidget.h"
#include "thumbnailwidget.h"
#include "pdfdocumentsession.h"
#include "pdfcontenthandler.h"
#include "outlineeditor.h"
#include "thumbnailmanagerv2.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QToolButton>
#include <QTimer>
#include <QDebug>
#include <QFile>

class NoRotateTabBar : public QTabBar
{
public:
    NoRotateTabBar(QWidget* parent = nullptr) : QTabBar(parent) {}

    QSize tabSizeHint(int index) const override
    {
        Q_UNUSED(index);
        return QSize(36, 50);  // 宽度36，高度50（可调整）
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        for (int i = 0; i < count(); i++) {
            QStyleOptionTab opt;
            initStyleOption(&opt, i);

            QRect rect = tabRect(i);

            // 绘制背景
            if (opt.state & QStyle::State_Selected) {
                painter.fillRect(rect, QColor(255, 255, 255));
                // 左侧指示线
                painter.fillRect(rect.left(), rect.top(), 2, rect.height(), QColor(44, 44, 46));
            } else if (opt.state & QStyle::State_MouseOver) {
                painter.fillRect(rect, QColor(245, 245, 243));
            }

            // 绘制文字（不旋转）
            painter.save();
            painter.setPen(opt.state & QStyle::State_Selected ? QColor(28, 28, 30) : QColor(107, 107, 105));

            QFont font = painter.font();
            font.setPixelSize(10);
            font.setWeight(opt.state & QStyle::State_Selected ? QFont::DemiBold : QFont::Normal);
            painter.setFont(font);

            // 直接绘制文字，使用换行符会自动换行
            painter.drawText(rect, Qt::AlignCenter, tabText(i));
            painter.restore();
        }
    }
};

class CustomTabWidget : public QTabWidget
{
public:
    CustomTabWidget(QWidget* parent = nullptr) : QTabWidget(parent)
    {
        // 在构造函数中设置自定义 TabBar
        setTabBar(new NoRotateTabBar(this));
    }
};

NavigationPanel::NavigationPanel(PDFDocumentSession* session, QWidget* parent)
    : QWidget(parent)
    , m_session(session)
    , m_tabWidget(nullptr)
    , m_outlineWidget(nullptr)
    , m_thumbnailWidget(nullptr)
    , m_expandAllBtn(nullptr)
    , m_collapseAllBtn(nullptr)
    , m_thumbnailStatusLabel(nullptr)
    , m_thumbnailProgressBar(nullptr)
{
    if (!m_session) {
        qCritical() << "NavigationPanel: session is null!";
        return;
    }

    setupUI();
    setupConnections();

    applyModernStyle();
}

NavigationPanel::~NavigationPanel()
{
    qDebug() << "NavigationPanel: Destructor called";
    clear();
    qDebug() << "NavigationPanel: Destructor finished";
}

void NavigationPanel::loadDocument(int pageCount)
{
    clear();

    if (!m_session || pageCount <= 0) {
        return;
    }

    qInfo() << "NavigationPanel: Loading document with" << pageCount << "pages";

    // 通过Session加载大纲
    bool hasOutline = m_session->loadOutline();
    if (hasOutline) {
        qInfo() << "NavigationPanel: Outline available";
    } else {
        qInfo() << "NavigationPanel: No outline available";
    }

    // 通过Session加载缩略图
    m_session->loadThumbnails();

    // 默认显示标签页
    if (hasOutline) {
        m_tabWidget->setCurrentIndex(0); // 大纲
    } else {
        m_tabWidget->setCurrentIndex(1); // 缩略图
    }
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
        m_thumbnailStatusLabel->setText(tr("Ready"));
    }
    if (m_thumbnailProgressBar) {
        m_thumbnailProgressBar->setVisible(false);
    }
}

void NavigationPanel::onTabChanged(int index) {
    updateCurrentPage(m_session->state()->currentPage());

    if (index == 1 && m_thumbnailWidget) {  // 1 = 缩略图标签页
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
    if (m_outlineWidget) {
        m_outlineWidget->highlightCurrentPage(pageIndex);
    }

    if (m_thumbnailWidget) {
        m_thumbnailWidget->highlightCurrentPage(pageIndex);
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
    m_tabWidget->setMinimumWidth(180);
    m_tabWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    // ========== 大纲标签页 ==========
    QWidget* outlineTab = new QWidget(this);
    QVBoxLayout* outlineLayout = new QVBoxLayout(outlineTab);
    outlineLayout->setContentsMargins(0, 0, 0, 0);
    outlineLayout->setSpacing(0);

    // 大纲工具栏
    QWidget* outlineToolbar = new QWidget(this);
    outlineToolbar->setObjectName("outlineToolbar");
    outlineToolbar->setFixedHeight(44);
    outlineToolbar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    QHBoxLayout* toolbarLayout = new QHBoxLayout(outlineToolbar);
    toolbarLayout->setContentsMargins(12, 8, 12, 8);
    toolbarLayout->setSpacing(8);

    m_expandAllBtn = new QToolButton(this);
    m_expandAllBtn->setIcon(QIcon(":/icons/icons/expand.png"));
    m_expandAllBtn->setToolTip(tr("展开全部"));
    m_expandAllBtn->setObjectName("outlineToolButton");
    m_expandAllBtn->setFixedSize(28, 28);
    m_expandAllBtn->setIconSize(QSize(14, 14));

    m_collapseAllBtn = new QToolButton(this);
    m_collapseAllBtn->setIcon(QIcon(":/icons/icons/fold.png"));
    m_collapseAllBtn->setToolTip(tr("折叠全部"));
    m_collapseAllBtn->setObjectName("outlineToolButton");
    m_collapseAllBtn->setFixedSize(28, 28);
    m_collapseAllBtn->setIconSize(QSize(20, 20));

    toolbarLayout->addStretch();
    toolbarLayout->addWidget(m_expandAllBtn);
    toolbarLayout->addWidget(m_collapseAllBtn);

    m_outlineWidget = new OutlineWidget(m_session->contentHandler(), this);
    m_outlineWidget->setMinimumWidth(0);
    m_outlineWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_outlineWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_outlineWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    outlineLayout->addWidget(outlineToolbar);
    outlineLayout->addWidget(m_outlineWidget, 1);

    outlineTab->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // ========== 缩略图标签页 ==========
    QWidget* thumbnailTab = new QWidget(this);
    QVBoxLayout* thumbnailLayout = new QVBoxLayout(thumbnailTab);
    thumbnailLayout->setContentsMargins(0, 0, 0, 0);
    thumbnailLayout->setSpacing(0);

    m_thumbnailWidget = new ThumbnailWidget(this);
    m_thumbnailWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_thumbnailWidget->setMinimumWidth(0);

    // 缩略图状态栏
    QWidget* statusBar = new QWidget(this);
    statusBar->setObjectName("thumbnailStatusBar");
    statusBar->setFixedHeight(32);

    QHBoxLayout* statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(12, 4, 12, 4);
    statusLayout->setSpacing(8);

    m_thumbnailStatusLabel = new QLabel(tr("Ready"), this);
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

    m_tabWidget->addTab(outlineTab, tr("目\n录"));
    m_tabWidget->addTab(thumbnailTab, tr("缩\n略\n图"));

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
    // ========== OutlineWidget信号 ==========
    connect(m_outlineWidget, &OutlineWidget::pageJumpRequested,
            this, &NavigationPanel::pageJumpRequested);

    connect(m_outlineWidget, &OutlineWidget::externalLinkRequested,
            this, [this](const QString& uri) {
                QUrl url(uri);
                if (url.isValid()) {
                    if (!QDesktopServices::openUrl(url)) {
                        QMessageBox::warning(this, tr("Open Link Failed"),
                                             tr("Failed to open link:\n%1").arg(uri));
                    }
                } else {
                    QMessageBox::warning(this, tr("Invalid Link"),
                                         tr("Invalid link URI:\n%1").arg(uri));
                }
                emit externalLinkRequested(uri);
            });

    if (m_session->contentHandler()) {
        connect(m_session->contentHandler(), &PDFContentHandler::outlineModified,
                this, &NavigationPanel::outlineModified);
    }

    connect(m_expandAllBtn, &QToolButton::clicked,
            m_outlineWidget, &OutlineWidget::expandAll);
    connect(m_collapseAllBtn, &QToolButton::clicked,
            m_outlineWidget, &OutlineWidget::collapseAll);

    // ========== ThumbnailWidget信号 ==========
    if (m_session && m_session->contentHandler() &&
        m_session->contentHandler()->thumbnailManager()) {
        m_thumbnailWidget->setThumbnailManager(
            m_session->contentHandler()->thumbnailManager()
            );
    }
    connect(m_thumbnailWidget, &ThumbnailWidget::pageJumpRequested,
            this, &NavigationPanel::pageJumpRequested);

    // 监听ThumbnailWidget的可见区域变化，通知ContentHandler加载
    connect(m_thumbnailWidget, &ThumbnailWidget::visibleRangeChanged,
            this, [this](const QSet<int>& visibleIndices, int margin) {
                if (m_session && m_session->contentHandler()) {
                    m_session->contentHandler()->handleVisibleRangeChanged(visibleIndices, margin);
                }
            });

    // 连接慢速滚动信号（仅大文档生效）
    connect(m_thumbnailWidget, &ThumbnailWidget::slowScrollDetected,
            this, [this](const QSet<int>& visiblePages) {
                if (m_session && m_session->contentHandler()) {
                    qDebug() << "NavigationPanel: Slow scroll detected, loading"
                             << visiblePages.size() << "visible pages";
                    m_session->contentHandler()->handleVisibleRangeChanged(visiblePages, 0);
                }
            });

    // 保留滚动停止时的同步加载连接
    connect(m_thumbnailWidget, &ThumbnailWidget::syncLoadRequested,
            this, [this](const QSet<int>& unloadedVisible) {
                if (m_session && m_session->contentHandler()) {
                    qDebug() << "NavigationPanel: Requesting sync load for"
                             << unloadedVisible.size() << "unloaded pages";
                    m_session->contentHandler()->syncLoadUnloadedPages(unloadedVisible);
                }
            });

    connect(m_thumbnailWidget, &ThumbnailWidget::initialVisibleReady,
            this, [this](const QSet<int>& initialVisible) {
                if (m_session && m_session->contentHandler()) {
                    m_session->contentHandler()->startInitialThumbnailLoad(initialVisible);
                }
            });

    // ========== Session信号连接 ==========
    if (m_session) {
        // 大纲加载完成
        connect(m_session, &PDFDocumentSession::outlineLoaded,
                this, [this](bool success, int itemCount) {
                    if (success && m_outlineWidget) {
                        m_outlineWidget->loadOutline();
                        qInfo() << "NavigationPanel: Outline loaded with" << itemCount << "items";
                    }
                });

        // 缩略图初始化完成 - UI创建占位符
        connect(m_session->contentHandler(), &PDFContentHandler::thumbnailsInitialized,
                this, [this](int pageCount) {
                    qInfo() << "NavigationPanel: Initializing" << pageCount << "thumbnail placeholders";
                    m_thumbnailWidget->initializeThumbnails(pageCount);
                });

        // 缩略图加载完成 - UI更新图片
        connect(m_session, &PDFDocumentSession::thumbnailLoaded,
                this, [this](int pageIndex, const QImage& thumbnail) {
                    m_thumbnailWidget->onThumbnailLoaded(pageIndex, thumbnail);
                });

        // 编辑器保存完成信号
        OutlineEditor* editor = m_session->outlineEditor();
        if (editor) {
            connect(editor, &OutlineEditor::saveCompleted,
                    this, [this](bool success, const QString& errorMsg) {
                        if (success) {
                            qInfo() << "NavigationPanel: Outline saved successfully";
                        } else {
                            qWarning() << "NavigationPanel: Failed to save outline:" << errorMsg;
                        }
                    });
        }

        // ========== ThumbnailManager进度信号 ==========
        if (m_session->contentHandler() && m_session->contentHandler()->thumbnailManager()) {
            ThumbnailManagerV2* manager = m_session->contentHandler()->thumbnailManager();

            // 加载开始
            connect(manager, &ThumbnailManagerV2::loadingStarted,
                    this, [this](int totalPages, const QString& strategy) {
                        qInfo() << "Thumbnail loading started:" << strategy << "for" << totalPages << "pages";
                        m_thumbnailStatusLabel->setText(tr("Initializing..."));
                    });

            // 状态变化
            connect(manager, &ThumbnailManagerV2::loadingStatusChanged,
                    this, [this](const QString& status) {
                        m_thumbnailStatusLabel->setText(status);
                    });

            // 批次进度（中文档）
            connect(manager, &ThumbnailManagerV2::batchCompleted,
                    this, [this](int current, int total) {
                        m_thumbnailProgressBar->setVisible(true);
                        m_thumbnailProgressBar->setMaximum(total);
                        m_thumbnailProgressBar->setValue(current);
                        m_thumbnailProgressBar->setFormat(QString("%1/%2").arg(current).arg(total));
                    });

            // 全部完成
            connect(manager, &ThumbnailManagerV2::allCompleted,
                    this, [this]() {
                        m_thumbnailStatusLabel->setText(tr("加载完毕"));
                        m_thumbnailProgressBar->setVisible(false);

                        // 3秒后恢复默认状态
                        QTimer::singleShot(3000, this, [this]() {
                            if (m_thumbnailStatusLabel) {
                                m_thumbnailStatusLabel->setText(tr("Ready"));
                            }
                        });
                    });

            // 加载进度（用于同步加载阶段）
            connect(manager, &ThumbnailManagerV2::loadProgress,
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
    }
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

void NavigationPanel::applyModernStyle()
{
    QFile styleFile(":/styles/resources/styles/navigation.qss");
    if (styleFile.open(QFile::ReadOnly)) {
        QString style = QLatin1String(styleFile.readAll());
        setStyleSheet(style);
        styleFile.close();
    }
}
