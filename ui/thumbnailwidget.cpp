#include "thumbnailwidget.h"
#include "mupdfrenderer.h"
#include "pdfcontenthandler.h"
#include "thumbnailmanager.h"

#include <QVBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QScrollArea>
#include <QLabel>

// ================================================================
//                       ThumbnailWidget
// ================================================================

ThumbnailWidget::ThumbnailWidget(MuPDFRenderer *renderer, PDFContentHandler* contentHandler, QWidget *parent)
    : QScrollArea(parent)
    , m_renderer(renderer)
    , m_contentHandler(contentHandler)
    , m_container(nullptr)
    , m_layout(nullptr)
    , m_thumbnailWidth(DEFAULT_THUMBNAIL_WIDTH)
    , m_currentPage(-1)
    , m_columnsPerRow(2)
{
    m_container = new QWidget(this);
    m_layout = new QGridLayout(m_container);
    m_layout->setSpacing(THUMBNAIL_SPACING);
    m_layout->setContentsMargins(
        THUMBNAIL_SPACING, THUMBNAIL_SPACING,
        THUMBNAIL_SPACING, THUMBNAIL_SPACING);

    setWidget(m_container);
    setWidgetResizable(true);

    setStyleSheet(R"(
        QScrollArea {
            background-color: #F5F5F5;
            border: none;
        }
    )");

    connect(m_contentHandler, &PDFContentHandler::thumbnailReady,
            this, &ThumbnailWidget::onThumbnailReady);
    connect(m_contentHandler, &PDFContentHandler::thumbnailLoadProgress,
            this, &ThumbnailWidget::onLoadProgress);
    connect(m_contentHandler, &PDFContentHandler::thumbnailLoadCompleted,
            this, &ThumbnailWidget::onLoadCompleted);
}

ThumbnailWidget::~ThumbnailWidget() {
    clear();
}



void ThumbnailWidget::clear()
{
    qDeleteAll(m_thumbnailItems);
    m_thumbnailItems.clear();
    m_currentPage = -1;
}

void ThumbnailWidget::loadThumbnails(int pageCount)
{
    clear();
    if (!m_renderer || !m_contentHandler || pageCount <= 0) return;

    // 计算每行列数
    int availableWidth = viewport()->width() - 2 * THUMBNAIL_SPACING;
    int itemWidth = m_thumbnailWidth + 20;
    m_columnsPerRow = qMax(1, availableWidth / itemWidth);

    // 创建所有缩略图项
    for (int i = 0; i < pageCount; ++i) {
        auto *item = new ThumbnailItem(i, m_thumbnailWidth, m_container);
        connect(item, &ThumbnailItem::clicked, this,
                &ThumbnailWidget::onThumbnailClicked);

        int row = i / m_columnsPerRow;
        int col = i % m_columnsPerRow;
        m_layout->addWidget(item, row, col);

        m_thumbnailItems[i] = item;
    }

    // 通过 PDFContentHandler 启动缩略图加载
    m_contentHandler->startLoadThumbnails(m_thumbnailWidth);
}

void ThumbnailWidget::highlightCurrentPage(int pageIndex)
{
    if (m_currentPage >= 0 && m_thumbnailItems.contains(m_currentPage))
        m_thumbnailItems[m_currentPage]->setHighlight(false);

    m_currentPage = pageIndex;

    if (m_currentPage >= 0 && m_thumbnailItems.contains(m_currentPage)) {
        auto *item = m_thumbnailItems[m_currentPage];
        item->setHighlight(true);
        ensureWidgetVisible(item, 50, 50);
    }
}

void ThumbnailWidget::setThumbnailSize(int width)
{
    if (width < 80 || width > 400) return;
    m_thumbnailWidth = width;

    if (!m_thumbnailItems.isEmpty() && m_renderer && m_contentHandler) {
        m_contentHandler->setThumbnailSize(width);
        loadThumbnails(m_renderer->pageCount());
    }
}

void ThumbnailWidget::onThumbnailClicked(int pageIndex)
{
    emit pageJumpRequested(pageIndex);
}

void ThumbnailWidget::onThumbnailReady(int pageIndex, const QImage& thumbnail)
{
    if (m_thumbnailItems.contains(pageIndex)) {
        m_thumbnailItems[pageIndex]->setThumbnail(thumbnail);
    }
}

void ThumbnailWidget::onLoadProgress(int current, int total)
{
    emit loadProgress(current, total);
}

void ThumbnailWidget::onLoadCompleted()
{
    // 可以在这里添加加载完成后的处理
    qDebug() << "All thumbnails loaded successfully";
}

// ================================================================
//                       ThumbnailItem
// ================================================================

ThumbnailItem::ThumbnailItem(int pageIndex, int width, QWidget *parent)
    : QWidget(parent)
    , m_pageIndex(pageIndex)
    , m_width(width)
    , m_isHighlighted(false)
    , m_isHovered(false)
{
    m_height = static_cast<int>(width * A4_RATIO);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_imageContainer = new QWidget(this);
    auto *containerLayout = new QVBoxLayout(m_imageContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);

    m_imageLabel = new QLabel(m_imageContainer);
    m_imageLabel->setFixedSize(width, m_height);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setScaledContents(false);

    updateStyle();

    QFont loadingFont = m_imageLabel->font();
    loadingFont.setPointSize(9);
    m_imageLabel->setFont(loadingFont);
    m_imageLabel->setText("加载中...");

    containerLayout->addWidget(m_imageLabel);

    auto *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(12);
    shadow->setColor(QColor(0, 0, 0, 40));
    shadow->setOffset(0, 2);
    m_imageContainer->setGraphicsEffect(shadow);

    m_pageLabel = new QLabel(QString("第 %1 页").arg(pageIndex + 1), this);
    m_pageLabel->setAlignment(Qt::AlignCenter);
    QFont font = m_pageLabel->font();
    font.setPointSize(9);
    m_pageLabel->setFont(font);
    m_pageLabel->setStyleSheet("QLabel { color: #666666; }");

    layout->addWidget(m_imageContainer);
    layout->addWidget(m_pageLabel);

    setFixedWidth(width + 16);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
}

void ThumbnailItem::setThumbnail(const QImage &image)
{
    if (image.isNull()) {
        m_imageLabel->setText("加载失败");
        return;
    }

    QImage scaled = image.scaled(m_imageLabel->size(),
                                 Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);

    QPixmap pixmap = QPixmap::fromImage(scaled);
    QPixmap rounded(pixmap.size());
    rounded.fill(Qt::transparent);

    QPainter painter(&rounded);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    QPainterPath path;
    path.addRoundedRect(rounded.rect(), 4, 4);
    painter.setClipPath(path);
    painter.drawPixmap(0, 0, pixmap);

    m_imageLabel->setPixmap(rounded);
    m_imageLabel->setText(QString());
}

void ThumbnailItem::setHighlight(bool highlight)
{
    m_isHighlighted = highlight;
    updateStyle();

    if (highlight)
        m_pageLabel->setStyleSheet("QLabel { color: #2196F3; font-weight: bold; }");
    else
        m_pageLabel->setStyleSheet("QLabel { color: #666666; }");
}

void ThumbnailItem::updateStyle()
{
    QString baseStyle = R"(
        QLabel {
            background-color: white;
            border-radius: 4px;
        }
    )";

    if (m_isHighlighted) {
        m_imageLabel->setStyleSheet(baseStyle +
                                    "QLabel { border: 3px solid #2196F3; }");
    } else if (m_isHovered) {
        m_imageLabel->setStyleSheet(baseStyle +
                                    "QLabel { border: 2px solid #64B5F6; }");
    } else {
        m_imageLabel->setStyleSheet(baseStyle +
                                    "QLabel { border: 1px solid #E0E0E0; }");
    }
}

void ThumbnailItem::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit clicked(m_pageIndex);
    QWidget::mousePressEvent(event);
}

void ThumbnailItem::enterEvent(QEnterEvent *event)
{
    m_isHovered = true;
    updateStyle();

    if (auto *shadow =
        qobject_cast<QGraphicsDropShadowEffect *>(m_imageContainer->graphicsEffect())) {
        shadow->setBlurRadius(16);
        shadow->setColor(QColor(0, 0, 0, 60));
        shadow->setOffset(0, 4);
    }

    QWidget::enterEvent(event);
}

void ThumbnailItem::leaveEvent(QEvent *event)
{
    m_isHovered = false;
    updateStyle();

    if (auto *shadow =
        qobject_cast<QGraphicsDropShadowEffect *>(m_imageContainer->graphicsEffect())) {
        shadow->setBlurRadius(12);
        shadow->setColor(QColor(0, 0, 0, 40));
        shadow->setOffset(0, 2);
    }

    QWidget::leaveEvent(event);
}
