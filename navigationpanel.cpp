#include "navigationpanel.h"
#include "outlinewidget.h"
#include "thumbnailwidget.h"
#include "pdfdocumentsession.h"
#include "pdfcontenthandler.h"
#include "outlineeditor.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QToolBar>
#include <QToolButton>
#include <QLabel>
#include <QDebug>

NavigationPanel::NavigationPanel(PDFDocumentSession* session, QWidget* parent)
    : QWidget(parent)
    , m_session(session)
    , m_tabWidget(nullptr)
    , m_outlineWidget(nullptr)
    , m_thumbnailWidget(nullptr)
    , m_expandAllBtn(nullptr)
    , m_collapseAllBtn(nullptr)
{
    if (!m_session) {
        qCritical() << "NavigationPanel: session is null!";
        return;
    }

    setupUI();
    setupConnections();
}

NavigationPanel::~NavigationPanel()
{
}

void NavigationPanel::loadDocument(int pageCount)
{
    clear();

    if (!m_session || pageCount <= 0) {
        return;
    }

    // 加载大纲
    bool hasOutline = m_session->loadOutline();
    if (hasOutline) {
        m_outlineWidget->loadOutline();
        qInfo() << "NavigationPanel: Outline loaded";
    } else {
        qInfo() << "NavigationPanel: No outline available";
    }

    // 加载缩略图
    m_thumbnailWidget->loadThumbnails(pageCount);

    // 默认显示缩略图标签页
    if (hasOutline) {
        m_tabWidget->setCurrentIndex(0); // 大纲
    } else {
        m_tabWidget->setCurrentIndex(1); // 缩略图
    }
}

void NavigationPanel::clear()
{
    m_outlineWidget->clear();
    m_thumbnailWidget->clear();
}

void NavigationPanel::updateCurrentPage(int pageIndex)
{
    // 更新大纲高亮
    if (m_outlineWidget) {
        m_outlineWidget->highlightCurrentPage(pageIndex);
    }

    // 更新缩略图高亮
    if (m_thumbnailWidget) {
        m_thumbnailWidget->highlightCurrentPage(pageIndex);
    }
}

void NavigationPanel::setThumbnail(int pageIndex, const QImage& thumbnail)
{
    if (m_thumbnailWidget) {
        // m_thumbnailWidget->setThumbnail(pageIndex, thumbnail);
    }
}

void NavigationPanel::setupUI()
{
    // 创建主容器
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 创建标签页组件
    m_tabWidget = new QTabWidget(this);
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

    // 展开全部按钮
    m_expandAllBtn = new QToolButton(this);
    m_expandAllBtn->setIcon(QIcon(":/icons/icons/expand.png"));
    m_expandAllBtn->setToolTip(tr("展开全部"));
    m_expandAllBtn->setObjectName("outlineToolButton");
    m_expandAllBtn->setFixedSize(28, 28);
    m_expandAllBtn->setIconSize(QSize(14, 14));

    // 折叠全部按钮
    m_collapseAllBtn = new QToolButton(this);
    m_collapseAllBtn->setIcon(QIcon(":/icons/icons/fold.png"));
    m_collapseAllBtn->setToolTip(tr("折叠全部"));
    m_collapseAllBtn->setObjectName("outlineToolButton");
    m_collapseAllBtn->setFixedSize(28, 28);
    m_collapseAllBtn->setIconSize(QSize(20, 20));

    toolbarLayout->addStretch();
    toolbarLayout->addWidget(m_expandAllBtn);
    toolbarLayout->addWidget(m_collapseAllBtn);

    // 创建大纲视图(使用Session的ContentHandler)
    m_outlineWidget = new OutlineWidget(m_session->contentHandler(), this);
    m_outlineWidget->setMinimumWidth(0);
    m_outlineWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_outlineWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_outlineWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    outlineLayout->addWidget(outlineToolbar);
    outlineLayout->addWidget(m_outlineWidget, 1);

    outlineTab->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_tabWidget->addTab(outlineTab, tr("目录"));

    // ========== 缩略图标签页 ==========
    m_thumbnailWidget = new ThumbnailWidget(m_session->renderer(),
                                            m_session->contentHandler(),
                                            this);
    m_thumbnailWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_thumbnailWidget->setMinimumWidth(0);

    m_tabWidget->addTab(m_thumbnailWidget, tr("缩略图"));

    // 添加到主布局
    mainLayout->addWidget(m_tabWidget, 1);

    // 应用样式
    QString style = R"(
        /* 标签页样式 */
        #navigationTabWidget {
            background-color: #FFFFFF;
            border: none;
        }

        #navigationTabWidget::pane {
            border: none;
            background-color: #FFFFFF;
        }

        #navigationTabWidget QTabBar {
            background-color: #FFFFFF;
            border-bottom: 1px solid #E8E8E8;
        }

        #navigationTabWidget QTabBar::tab {
            background-color: transparent;
            color: #6B6B6B;
            padding: 10px 20px;
            border: none;
            border-bottom: 2px solid transparent;
            font-size: 13px;
            font-weight: 500;
            min-width: 60px;
        }

        #navigationTabWidget QTabBar::tab:selected {
            color: #007AFF;
            border-bottom: 2px solid #007AFF;
        }

        #navigationTabWidget QTabBar::tab:hover:!selected {
            color: #000000;
        }

        /* 工具栏样式 */
        #outlineToolbar {
            background-color: #FAFAFA;
            border-bottom: 1px solid #E8E8E8;
        }

        /* 工具按钮样式 */
        #outlineToolButton {
            background-color: transparent;
            border: 1px solid #D1D1D6;
            border-radius: 6px;
            color: #3A3A3C;
            font-size: 14px;
            padding: 0px;
        }

        #outlineToolButton:hover {
            background-color: #E8E8E8;
            border-color: #007AFF;
            color: #007AFF;
        }

        #outlineToolButton:pressed {
            background-color: #D1D1D6;
        }
    )";

    setStyleSheet(style);

    setMinimumWidth(180);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
}

void NavigationPanel::setupConnections()
{
    // ========== OutlineWidget信号 ==========

    // 大纲跳转信号
    connect(m_outlineWidget, &OutlineWidget::pageJumpRequested,
            this, &NavigationPanel::pageJumpRequested);

    // 大纲外部链接信号
    connect(m_outlineWidget, &OutlineWidget::externalLinkRequested,
            this, [this](const QString& uri) {
                // 打开外部链接
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

    // 大纲项修改信号
    connect(m_session->contentHandler(), &PDFContentHandler::outlineModified,
            this, &NavigationPanel::outlineModified);

    // 连接展开/折叠按钮
    connect(m_expandAllBtn, &QToolButton::clicked,
            m_outlineWidget, &OutlineWidget::expandAll);
    connect(m_collapseAllBtn, &QToolButton::clicked,
            m_outlineWidget, &OutlineWidget::collapseAll);

    // ========== ThumbnailWidget信号 ==========

    // 缩略图跳转信号
    connect(m_thumbnailWidget, &ThumbnailWidget::pageJumpRequested,
            this, &NavigationPanel::pageJumpRequested);

    // 缩略图加载进度(可选显示)
    connect(m_thumbnailWidget, &ThumbnailWidget::loadProgress,
            this, [](int current, int total) {
                if(current == total) {
                    qDebug() << "Thumbnail loading progress done:" << current << "/" << total;
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

        // 缩略图相关信号
        connect(m_session, &PDFDocumentSession::thumbnailLoadStarted,
                this, [this](int totalPages) {
                    // 可以显示进度提示
                });

        connect(m_session, &PDFDocumentSession::thumbnailLoadProgress,
                this, [this](int loaded, int total) {
                    // 更新进度
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
