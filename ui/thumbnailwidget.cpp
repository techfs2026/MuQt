#include "thumbnailwidget.h"
#include "thumbnailmanagerv2.h"
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsDropShadowEffect>
#include <QScrollBar>
#include <QDebug>
#include <QDateTime>

ThumbnailWidget::ThumbnailWidget(QWidget* parent)
    : QScrollArea(parent)
    , m_container(nullptr)
    , m_layout(nullptr)
    , m_thumbnailWidth(DEFAULT_THUMBNAIL_WIDTH)
    , m_currentPage(-1)
    , m_columnsPerRow(2)
    , m_scrollState(ScrollState::IDLE)
    , m_manager(nullptr)
{
    m_container = new QWidget(this);
    m_layout = new QGridLayout(m_container);
    m_layout->setSpacing(THUMBNAIL_SPACING);
    m_layout->setContentsMargins(
        THUMBNAIL_SPACING, THUMBNAIL_SPACING,
        THUMBNAIL_SPACING, THUMBNAIL_SPACING
        );

    setWidget(m_container);
    setWidgetResizable(true);

    setStyleSheet(R"(
        QScrollArea {
            background-color: #F5F5F5;
            border: none;
        }
    )");

    m_throttleTimer = new QTimer(this);
    m_throttleTimer->setSingleShot(true);
    m_throttleTimer->setInterval(30);
    connect(m_throttleTimer, &QTimer::timeout,
            this, &ThumbnailWidget::onScrollThrottle);

    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(150);
    connect(m_debounceTimer, &QTimer::timeout,
            this, &ThumbnailWidget::onScrollDebounce);
}

ThumbnailWidget::~ThumbnailWidget()
{
    qDebug() << "ThumbnailWidget: Destructor called";
    clear();
    qDebug() << "ThumbnailWidget: Destructor finished";
}

void ThumbnailWidget::setThumbnailManager(ThumbnailManagerV2* manager)
{
    m_manager = manager;
}

bool ThumbnailWidget::isLargeLoadMode() {
    return m_manager->thumbnailLoadStrategy()->type() == LoadStrategyType::LARGE_DOC;
}

void ThumbnailWidget::initializeThumbnails(int pageCount)
{
    clear();

    if (pageCount <= 0) {
        qWarning() << "ThumbnailWidget: Invalid page count:" << pageCount;
        return;
    }

    qInfo() << "ThumbnailWidget: Initializing" << pageCount << "thumbnail placeholders";

    int availableWidth = viewport()->width() - 2 * THUMBNAIL_SPACING;
    int itemWidth = m_thumbnailWidth + 20;
    m_columnsPerRow = qMax(1, availableWidth / itemWidth);

    qDebug() << "ThumbnailWidget: viewport width =" << viewport()->width()
             << ", availableWidth =" << availableWidth
             << ", itemWidth =" << itemWidth
             << ", columns =" << m_columnsPerRow;

    for (int i = 0; i < pageCount; ++i) {
        auto* item = new ThumbnailItem(i, m_thumbnailWidth, m_container);
        item->setPlaceholder(tr("第%1页").arg(i + 1));

        connect(item, &ThumbnailItem::clicked,
                this, &ThumbnailWidget::onThumbnailClicked);

        int row = i / m_columnsPerRow;
        int col = i % m_columnsPerRow;
        m_layout->addWidget(item, row, col);

        m_thumbnailItems[i] = item;
    }

    qInfo() << "ThumbnailWidget: Created" << pageCount << "placeholder items";

    // 延迟发送初始可见信号
    QTimer::singleShot(100, this, [this]() {
        // 强制布局立即计算
        m_container->adjustSize();
        m_layout->activate();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

        QSet<int> initialVisible = getVisibleIndices(0);

        qDebug() << "ThumbnailWidget: Initial visible count =" << initialVisible.size();
        if (initialVisible.isEmpty()) {
            qWarning() << "ThumbnailWidget: No initial visible items found!";
            // 打印前几个widget的geometry用于调试
            for (int i = 0; i < qMin(5, m_thumbnailItems.size()); ++i) {
                if (m_thumbnailItems.contains(i)) {
                    qDebug() << "  Widget" << i << "geometry:"
                             << m_thumbnailItems[i]->geometry();
                }
            }
        }

        emit initialVisibleReady(initialVisible);
    });
}

void ThumbnailWidget::clear()
{
    if (!m_layout)
        return;

    qDebug() << "ThumbnailWidget::clear() - Start";

    if (m_throttleTimer && m_throttleTimer->isActive()) {
        m_throttleTimer->stop();
    }
    if (m_debounceTimer && m_debounceTimer->isActive()) {
        m_debounceTimer->stop();
    }

    while (QLayoutItem* item = m_layout->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }

    m_thumbnailItems.clear();
    m_scrollHistory.clear();
    m_currentPage = -1;

    m_layout->invalidate();

    qDebug() << "ThumbnailWidget::clear() - Finished";
}

void ThumbnailWidget::highlightCurrentPage(int pageIndex)
{
    if (m_currentPage >= 0 && m_thumbnailItems.contains(m_currentPage)) {
        m_thumbnailItems[m_currentPage]->setHighlight(false);
    }

    m_currentPage = pageIndex;

    if (m_currentPage >= 0 && m_thumbnailItems.contains(m_currentPage)) {
        auto* item = m_thumbnailItems[m_currentPage];
        item->setHighlight(true);
        ensureWidgetVisible(item, 50, 50);
    }
}

void ThumbnailWidget::setThumbnailSize(int width)
{
    if (width < 80 || width > 400) {
        qWarning() << "ThumbnailWidget: Invalid thumbnail width:" << width;
        return;
    }

    if (m_thumbnailWidth != width) {
        m_thumbnailWidth = width;
    }
}

void ThumbnailWidget::onThumbnailLoaded(int pageIndex, const QImage& thumbnail)
{
    if (!m_thumbnailItems.contains(pageIndex)) {
        return;
    }

    ThumbnailItem* item = m_thumbnailItems[pageIndex];
    item->setThumbnail(thumbnail);
}

void ThumbnailWidget::scrollContentsBy(int dx, int dy)
{
    QScrollArea::scrollContentsBy(dx, dy);

    if(!isLargeLoadMode()) {
        m_scrollHistory.clear();
        return;
    }

    m_scrollState = detectScrollState();

    if (!m_throttleTimer->isActive()) {
        m_throttleTimer->start();
    }

    m_debounceTimer->start();
}

void ThumbnailWidget::resizeEvent(QResizeEvent* event)
{
    QScrollArea::resizeEvent(event);

    int availableWidth = viewport()->width() - 2 * THUMBNAIL_SPACING;
    int itemWidth = m_thumbnailWidth + 20;
    int newColumns = qMax(1, availableWidth / itemWidth);

    if (newColumns != m_columnsPerRow) {
        m_columnsPerRow = newColumns;

        qDebug() << "ThumbnailWidget: Columns changed to" << m_columnsPerRow;

        for (int i = 0; i < m_thumbnailItems.size(); ++i) {
            int row = i / m_columnsPerRow;
            int col = i % m_columnsPerRow;

            QLayoutItem* layoutItem = m_layout->itemAtPosition(row, col);
            if (!layoutItem || layoutItem->widget() != m_thumbnailItems[i]) {
                m_layout->removeWidget(m_thumbnailItems[i]);
                m_layout->addWidget(m_thumbnailItems[i], row, col);
            }
        }

        if(isLargeLoadMode()) {
            m_throttleTimer->start();
        }
    }
}

void ThumbnailWidget::onScrollThrottle()
{
    if(!isLargeLoadMode()) {
        return;
    }

    // 只有在应该响应滚动时才通知
    if (m_manager && m_manager->shouldRespondToScroll()) {
        notifyVisibleRange();
    }
}

void ThumbnailWidget::onScrollDebounce()
{
    if(!isLargeLoadMode()) {
        return;
    }

    m_scrollState = ScrollState::IDLE;
    m_scrollHistory.clear();

    qDebug() << "ThumbnailWidget: Scroll stopped";

    // 检查当前Tab是否可见
    if (!isVisible()) {
        qDebug() << "ThumbnailWidget: Not visible, ignoring scroll stop";
        return;
    }

    // 检查是否应该响应滚动停止(避免批次加载期间触发)
    if (m_manager && !m_manager->shouldRespondToScroll()) {
        qDebug() << "ThumbnailWidget: Ignoring scroll stop during batch loading";
        return;
    }

    // 检查可见区域是否有未加载的占位页
    QSet<int> unloadedVisible = getUnloadedVisiblePages();

    qDebug() << "onScrollDebounce:" << unloadedVisible;

    if (!unloadedVisible.isEmpty()) {
        qInfo() << "ThumbnailWidget: Found" << unloadedVisible.size()
        << "unloaded visible pages after scroll stop, triggering sync load";

        emit syncLoadRequested(unloadedVisible);
    }
}

void ThumbnailWidget::onThumbnailClicked(int pageIndex)
{
    emit pageJumpRequested(pageIndex);
}

// ========== 可见性判断方法 ==========

QSet<int> ThumbnailWidget::getVisibleIndices(int margin) const
{
    QSet<int> visible;
    if (m_thumbnailItems.isEmpty())
        return visible;

    // 以 viewport 为基准的可见区域，加一点上下 margin 作为预加载区域
    QRect visibleRect = viewport()->rect();
    qDebug() << "getVisibleIndices visibleRect 1:" << visibleRect;
    visibleRect.adjust(0, -margin, 0, margin);
    qDebug() << "getVisibleIndices visibleRect 2:" << visibleRect;

    for (auto it = m_thumbnailItems.constBegin(); it != m_thumbnailItems.constEnd(); ++it) {
        int index = it.key();
        ThumbnailItem* item = it.value();
        if (!item)
            continue;

        // 把 item 的矩形转换到 viewport 坐标系
        QPoint topLeft = item->mapTo(viewport(), QPoint(0, 0));
        QRect itemRect(topLeft, item->size());

        if (itemRect.intersects(visibleRect)) {
            visible.insert(index);
        }
    }

    qDebug() << "ThumbnailWidget::getVisibleIndices" << visible;
    return visible;
}

QSet<int> ThumbnailWidget::getUnloadedVisiblePages() const
{
    QSet<int> unloaded;

    // 检查当前Tab是否可见
    if (!isVisible()) {
        return unloaded;
    }

    if (m_thumbnailItems.isEmpty()) {
        return unloaded;
    }

    // 获取严格可见区域(不带margin)
    QSet<int> visible = getVisibleIndices(0);

    // 检查哪些页面还是占位符
    for (int pageIndex : visible) {
        if (m_thumbnailItems.contains(pageIndex)) {
            ThumbnailItem* item = m_thumbnailItems[pageIndex];
            qDebug() << "ThumbnailWidget: getUnloadedVisiblePages" << pageIndex << "hasImage=" << item->hasImage();
            if (!item->hasImage()) {
                unloaded.insert(pageIndex);
            }
        }
    }

    return unloaded;
}

void ThumbnailWidget::notifyVisibleRange()
{
    int margin = getPreloadMargin(m_scrollState);
    QSet<int> visible = getVisibleIndices(margin);

    emit visibleRangeChanged(visible, margin);
}

// ========== 辅助方法 ==========

ScrollState ThumbnailWidget::detectScrollState()
{
    int currentPos = verticalScrollBar()->value();
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();

    m_scrollHistory.enqueue(qMakePair(currentPos, currentTime));

    while (!m_scrollHistory.isEmpty() &&
           currentTime - m_scrollHistory.first().second > 200) {
        m_scrollHistory.dequeue();
    }

    if (m_scrollHistory.size() < 2) {
        return ScrollState::IDLE;
    }

    auto [firstPos, firstTime] = m_scrollHistory.first();
    int distance = qAbs(currentPos - firstPos);
    qint64 duration = currentTime - firstTime;

    if (duration < 50) {
        return ScrollState::FLING;
    }

    float velocity = distance / (duration / 1000.0f);

    if (velocity < 500) {
        return ScrollState::IDLE;
    } else if (velocity < 1000) {
        return ScrollState::SLOW_SCROLL;
    } else if (velocity < 3000) {
        return ScrollState::FAST_SCROLL;
    } else {
        return ScrollState::FLING;
    }
}

int ThumbnailWidget::getPreloadMargin(ScrollState state) const
{
    switch (state) {
    case ScrollState::IDLE:
        return 1200;
    case ScrollState::SLOW_SCROLL:
        return 800;
    case ScrollState::FAST_SCROLL:
        return 400;
    case ScrollState::FLING:
        return 0;
    }
    return 800;
}

// ================================================================
//                       ThumbnailItem
// ================================================================

ThumbnailItem::ThumbnailItem(int pageIndex, int width, QWidget* parent)
    : QWidget(parent)
    , m_pageIndex(pageIndex)
    , m_width(width)
    , m_hasImage(false)
    , m_isHighlighted(false)
    , m_isHovered(false)
{
    m_height = static_cast<int>(width * ThumbnailWidget::A4_RATIO);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_imageContainer = new QWidget(this);
    auto* containerLayout = new QVBoxLayout(m_imageContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);

    m_imageLabel = new QLabel(m_imageContainer);
    m_imageLabel->setFixedSize(width, m_height);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setScaledContents(false);

    updateStyle();

    containerLayout->addWidget(m_imageLabel);

    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(12);
    shadow->setColor(QColor(0, 0, 0, 40));
    shadow->setOffset(0, 2);
    m_imageContainer->setGraphicsEffect(shadow);

    m_pageLabel = new QLabel(tr("第%1页").arg(pageIndex + 1), this);
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

void ThumbnailItem::setPlaceholder(const QString& text)
{
    m_hasImage = false;
    m_imageLabel->setText(text);
    m_imageLabel->setStyleSheet(
        "QLabel { "
        "    background-color: white; "
        "    border: 1px solid #E0E0E0; "
        "    border-radius: 4px; "
        "    color: #999999; "
        "}"
        );

    QFont font = m_imageLabel->font();
    font.setPointSize(9);
    m_imageLabel->setFont(font);
}

void ThumbnailItem::setThumbnail(const QImage& image)
{
    if (image.isNull()) {
        setError(tr("Load failed"));
        return;
    }

    m_hasImage = true;

    QImage scaled = image.scaled(
        m_imageLabel->size(),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation
        );

    QPixmap pixmap = createRoundedPixmap(scaled);
    m_imageLabel->setPixmap(pixmap);
    m_imageLabel->setText(QString());

    updateStyle();
}

void ThumbnailItem::setError(const QString& error)
{
    m_hasImage = false;
    m_imageLabel->setText(error);
    m_imageLabel->setStyleSheet(
        "QLabel { "
        "    background-color: white; "
        "    border: 1px solid #E0E0E0; "
        "    border-radius: 4px; "
        "    color: #F44336; "
        "}"
        );

    QFont font = m_imageLabel->font();
    font.setPointSize(8);
    m_imageLabel->setFont(font);
}

void ThumbnailItem::setHighlight(bool highlight)
{
    m_isHighlighted = highlight;
    updateStyle();

    if (highlight) {
        m_pageLabel->setStyleSheet("QLabel { color: #2196F3; font-weight: bold; }");
    } else {
        m_pageLabel->setStyleSheet("QLabel { color: #666666; }");
    }
}

void ThumbnailItem::updateStyle()
{
    if (!m_hasImage) {
        return;
    }

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

QPixmap ThumbnailItem::createRoundedPixmap(const QImage& image)
{
    QPixmap pixmap = QPixmap::fromImage(image);
    QPixmap rounded(pixmap.size());
    rounded.fill(Qt::transparent);

    QPainter painter(&rounded);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    QPainterPath path;
    path.addRoundedRect(rounded.rect(), 4, 4);
    painter.setClipPath(path);
    painter.drawPixmap(0, 0, pixmap);

    return rounded;
}

void ThumbnailItem::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked(m_pageIndex);
    }
    QWidget::mousePressEvent(event);
}

void ThumbnailItem::enterEvent(QEnterEvent* event)
{
    m_isHovered = true;
    updateStyle();

    if (auto* shadow = qobject_cast<QGraphicsDropShadowEffect*>(
            m_imageContainer->graphicsEffect())) {
        shadow->setBlurRadius(16);
        shadow->setColor(QColor(0, 0, 0, 60));
        shadow->setOffset(0, 4);
    }

    QWidget::enterEvent(event);
}

void ThumbnailItem::leaveEvent(QEvent* event)
{
    m_isHovered = false;
    updateStyle();

    if (auto* shadow = qobject_cast<QGraphicsDropShadowEffect*>(
            m_imageContainer->graphicsEffect())) {
        shadow->setBlurRadius(12);
        shadow->setColor(QColor(0, 0, 0, 40));
        shadow->setOffset(0, 2);
    }

    QWidget::leaveEvent(event);
}
