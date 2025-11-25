#include "thumbnailwidget.h"
#include "thumbnailmanager.h"
#include "mupdfrenderer.h"

#include <QVBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsDropShadowEffect>
#include <QScrollBar>
#include <QDebug>
#include <QDateTime>
#include <QElapsedTimer>
#include <QPropertyAnimation>

// ================================================================
//                       ThumbnailWidget
// ================================================================

ThumbnailWidget::ThumbnailWidget(PDFDocumentSession* session,
                                 QWidget* parent)
    : QScrollArea(parent)
    , m_renderer(session->renderer())
    , m_thumbnailManager(session->contentHandler()->thumbnailManager())
    , m_container(nullptr)
    , m_layout(nullptr)
    , m_thumbnailWidth(DEFAULT_THUMBNAIL_WIDTH)
    , m_currentPage(-1)
    , m_columnsPerRow(2)
    , m_scrollState(ScrollState::IDLE)
{
    // 创建容器和布局
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

    // 创建定时器
    m_throttleTimer = new QTimer(this);
    m_throttleTimer->setSingleShot(true);
    m_throttleTimer->setInterval(30);  // 30ms 节流
    connect(m_throttleTimer, &QTimer::timeout,
            this, &ThumbnailWidget::onScrollThrottle);

    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(150);  // 150ms 防抖
    connect(m_debounceTimer, &QTimer::timeout,
            this, &ThumbnailWidget::onScrollDebounce);
}

ThumbnailWidget::~ThumbnailWidget()
{
    qDebug() << "ThumbnailWidget: Destructor called";

    // 1. 断开ThumbnailManager的信号（防止收到延迟的thumbnailLoaded信号）
    if (m_thumbnailManager) {
        disconnect(m_thumbnailManager, nullptr, this, nullptr);
    }

    // 2. 停止所有定时器（防止触发槽函数）
    if (m_throttleTimer) {
        m_throttleTimer->stop();
    }
    if (m_debounceTimer) {
        m_debounceTimer->stop();
    }

    // 3. 清理所有缩略图项
    clear();

    qDebug() << "ThumbnailWidget: Destructor finished";
}

void ThumbnailWidget::clear()
{
    if (!m_layout)
        return;

    qDebug() << "ThumbnailWidget::clear() - Start";

    // 1. 停止定时器（防止清理时触发加载）
    if (m_throttleTimer && m_throttleTimer->isActive()) {
        m_throttleTimer->stop();
    }
    if (m_debounceTimer && m_debounceTimer->isActive()) {
        m_debounceTimer->stop();
    }

    // 2. 断开所有ThumbnailItem的信号
    for (auto it = m_thumbnailItems.begin(); it != m_thumbnailItems.end(); ++it) {
        if (it.value()) {
            // 断开与this的连接
            disconnect(it.value(), nullptr, this, nullptr);

            // 断开item内部的所有连接（可选，更彻底）
            it.value()->disconnect();
        }
    }

    // 3. 从布局中移除并标记删除
    while (QLayoutItem* item = m_layout->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            w->deleteLater();  // 使用deleteLater而非delete
        }
        delete item;  // 删除布局项本身
    }

    // 4. 清空存储
    m_thumbnailItems.clear();
    m_itemRects.clear();
    m_scrollHistory.clear();
    m_currentPage = -1;

    // 5. 强制刷新布局
    m_layout->invalidate();

    qDebug() << "ThumbnailWidget::clear() - Finished";
}


void ThumbnailWidget::loadThumbnails(int pageCount)
{
    clear();
    if (!m_renderer || !m_thumbnailManager || pageCount <= 0) {
        qWarning() << "ThumbnailWidget: Invalid parameters for loading";
        return;
    }

    qInfo() << "ThumbnailWidget: Loading" << pageCount << "thumbnail placeholders";

    // 1. 计算布局
    int availableWidth = viewport()->width() - 2 * THUMBNAIL_SPACING;
    int itemWidth = m_thumbnailWidth + 20;
    m_columnsPerRow = qMax(1, availableWidth / itemWidth);

    // 2. 预分配位置数组
    m_itemRects.resize(pageCount);

    // 3. 创建所有缩略图项（只创建占位符）
    for (int i = 0; i < pageCount; ++i) {
        auto* item = new ThumbnailItem(i, m_thumbnailWidth, m_container);
        item->setPlaceholder(tr("Page %1").arg(i + 1));

        connect(item, &ThumbnailItem::clicked,
                this, &ThumbnailWidget::onThumbnailClicked);

        int row = i / m_columnsPerRow;
        int col = i % m_columnsPerRow;
        m_layout->addWidget(item, row, col);

        m_thumbnailItems[i] = item;

        // 记录位置
        m_itemRects[i] = calculateItemRect(row, col);
    }

    qInfo() << "ThumbnailWidget: Created" << pageCount << "placeholder items";

    // 4. 立即同步渲染可见区的低清缩略图
    QSet<int> visibleIndices = getVisibleIndices(0);
    QVector<int> visiblePages = visibleIndices.values().toVector();

    if (!visiblePages.isEmpty()) {
        qDebug() << "ThumbnailWidget: Immediate rendering" << visiblePages.size()
        << "low-res thumbnails";
        m_thumbnailManager->renderLowResImmediate(visiblePages);
    }

    // 5. 异步渲染可见区的高清缩略图
    if (!visiblePages.isEmpty()) {
        m_thumbnailManager->renderHighResAsync(visiblePages, RenderPriority::HIGH);
    }

    // 6. 异步渲染预加载区的高清缩略图
    QSet<int> preloadIndices = getVisibleIndices(800);
    preloadIndices.subtract(visibleIndices);
    QVector<int> preloadPages = preloadIndices.values().toVector();

    if (!preloadPages.isEmpty()) {
        m_thumbnailManager->renderHighResAsync(preloadPages, RenderPriority::MEDIUM);
    }

    // 7. 启动后台全文档低清渲染
    QTimer::singleShot(1000, this, &ThumbnailWidget::startBackgroundLowResRendering);
}

void ThumbnailWidget::calculateItemPositions()
{
    int itemWidth = m_thumbnailWidth + 20;
    int itemHeight = static_cast<int>((m_thumbnailWidth + 20) * A4_RATIO);

    for (int i = 0; i < m_itemRects.size(); ++i) {
        int row = i / m_columnsPerRow;
        int col = i % m_columnsPerRow;

        int x = THUMBNAIL_SPACING + col * (itemWidth + THUMBNAIL_SPACING);
        int y = THUMBNAIL_SPACING + row * (itemHeight + THUMBNAIL_SPACING);

        m_itemRects[i] = QRect(x, y, itemWidth, itemHeight);
    }
}

QRect ThumbnailWidget::calculateItemRect(int row, int col) const
{
    int itemWidth = m_thumbnailWidth + 20;
    int itemHeight = static_cast<int>((m_thumbnailWidth + 20) * A4_RATIO);

    int x = THUMBNAIL_SPACING + col * (itemWidth + THUMBNAIL_SPACING);
    int y = THUMBNAIL_SPACING + row * (itemHeight + THUMBNAIL_SPACING);

    return QRect(x, y, itemWidth, itemHeight);
}

QSet<int> ThumbnailWidget::getVisibleIndices(int margin) const
{
    QSet<int> visible;

    if (m_itemRects.isEmpty()) {
        return visible;
    }

    QRect viewportRect = viewport()->rect();
    int scrollY = verticalScrollBar()->value();

    QRect extendedViewport = viewportRect.adjusted(0, -margin, 0, margin);
    extendedViewport.translate(0, scrollY);

    for (int i = 0; i < m_itemRects.size(); ++i) {
        if (m_itemRects[i].intersects(extendedViewport)) {
            visible.insert(i);
        }
    }

    return visible;
}

ScrollState ThumbnailWidget::detectScrollState()
{
    int currentPos = verticalScrollBar()->value();
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();

    // 记录滚动历史
    m_scrollHistory.enqueue(qMakePair(currentPos, currentTime));

    // 只保留最近 200ms 的历史
    while (!m_scrollHistory.isEmpty() &&
           currentTime - m_scrollHistory.first().second > 200) {
        m_scrollHistory.dequeue();
    }

    if (m_scrollHistory.size() < 2) {
        return ScrollState::IDLE;
    }

    // 计算滚动速度
    auto [firstPos, firstTime] = m_scrollHistory.first();
    int distance = qAbs(currentPos - firstPos);
    qint64 duration = currentTime - firstTime;

    if (duration < 50) {
        return ScrollState::FLING;  // 惯性滑动
    }

    float velocity = distance / (duration / 1000.0f);  // px/s

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

void ThumbnailWidget::loadVisibleThumbnails(ScrollState state)
{
    if (!m_renderer || !m_thumbnailManager) {
        return;
    }

    if (m_itemRects.isEmpty()) {
        calculateItemPositions();
    }

    // 策略1: 总是加载严格可见区（优先高清）
    QSet<int> visibleIndices = getVisibleIndices(0);
    QVector<int> visiblePages = visibleIndices.values().toVector();

    if (!visiblePages.isEmpty()) {
        m_thumbnailManager->renderHighResAsync(visiblePages, RenderPriority::HIGH);
    }

    // 策略2: 根据滚动状态决定是否加载预加载区
    if (state != ScrollState::FLING) {
        int margin = getPreloadMargin(state);
        QSet<int> preloadIndices = getVisibleIndices(margin);
        preloadIndices.subtract(visibleIndices);
        QVector<int> preloadPages = preloadIndices.values().toVector();

        if (!preloadPages.isEmpty()) {
            RenderPriority priority = (state == ScrollState::IDLE)
            ? RenderPriority::MEDIUM
            : RenderPriority::LOW;
            m_thumbnailManager->renderHighResAsync(preloadPages, priority);
        }
    }
}

void ThumbnailWidget::startBackgroundLowResRendering()
{
    if (!m_renderer || !m_thumbnailManager) {
        return;
    }

    int pageCount = m_thumbnailItems.size();
    if (pageCount == 0) {
        return;
    }

    // 生成所有页面索引
    QVector<int> allPages;
    allPages.reserve(pageCount);
    for (int i = 0; i < pageCount; ++i) {
        allPages.append(i);
    }

    qDebug() << "ThumbnailWidget: Starting background low-res rendering for"
             << pageCount << "pages";

    // 异步渲染全文档低清缩略图（最低优先级）
    m_thumbnailManager->renderLowResAsync(allPages);
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

        if (!m_thumbnailItems.isEmpty() && m_renderer && m_thumbnailManager) {
            m_thumbnailManager->setHighResWidth(width);
            loadThumbnails(m_renderer->pageCount());
        }
    }
}

void ThumbnailWidget::scrollContentsBy(int dx, int dy)
{
    QScrollArea::scrollContentsBy(dx, dy);

    // 检测滚动状态
    m_scrollState = detectScrollState();

    // 节流：至少每 30ms 触发一次加载
    if (!m_throttleTimer->isActive()) {
        m_throttleTimer->start();
    }

    // 防抖：150ms 无滚动后触发完整加载
    m_debounceTimer->start();
}

void ThumbnailWidget::onScrollThrottle()
{
    // 节流触发：根据滚动状态加载
    loadVisibleThumbnails(m_scrollState);
}

void ThumbnailWidget::onScrollDebounce()
{
    // 防抖触发：滚动停止，加载所有预加载区
    m_scrollState = ScrollState::IDLE;
    m_scrollHistory.clear();

    qDebug() << "ThumbnailWidget: Scroll stopped, loading preload area";
    loadVisibleThumbnails(ScrollState::IDLE);
}

void ThumbnailWidget::onThumbnailLoaded(int pageIndex, const QImage& thumbnail, bool isHighRes)
{
    if (!m_thumbnailItems.contains(pageIndex)) {
        return;
    }

    ThumbnailItem* item = m_thumbnailItems[pageIndex];
    item->setThumbnail(thumbnail, isHighRes);
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

        // 重新布局
        for (int i = 0; i < m_thumbnailItems.size(); ++i) {
            int row = i / m_columnsPerRow;
            int col = i % m_columnsPerRow;

            QLayoutItem* layoutItem = m_layout->itemAtPosition(row, col);
            if (!layoutItem || layoutItem->widget() != m_thumbnailItems[i]) {
                m_layout->removeWidget(m_thumbnailItems[i]);
                m_layout->addWidget(m_thumbnailItems[i], row, col);
            }
        }

        calculateItemPositions();
        m_throttleTimer->start();
    }
}

void ThumbnailWidget::onThumbnailClicked(int pageIndex)
{
    emit pageJumpRequested(pageIndex);
}

// ================================================================
//                       ThumbnailItem
// ================================================================

ThumbnailItem::ThumbnailItem(int pageIndex, int width, QWidget* parent)
    : QWidget(parent)
    , m_pageIndex(pageIndex)
    , m_width(width)
    , m_hasImage(false)
    , m_isHighRes(false)
    , m_isHighlighted(false)
    , m_isHovered(false)
{
    m_height = static_cast<int>(width * A4_RATIO);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // 图片容器
    m_imageContainer = new QWidget(this);
    auto* containerLayout = new QVBoxLayout(m_imageContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);

    // 图片标签
    m_imageLabel = new QLabel(m_imageContainer);
    m_imageLabel->setFixedSize(width, m_height);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setScaledContents(false);

    updateStyle();

    containerLayout->addWidget(m_imageLabel);

    // 阴影效果
    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(12);
    shadow->setColor(QColor(0, 0, 0, 40));
    shadow->setOffset(0, 2);
    m_imageContainer->setGraphicsEffect(shadow);

    // 页码标签
    m_pageLabel = new QLabel(tr("Page %1").arg(pageIndex + 1), this);
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
    m_isHighRes = false;
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

void ThumbnailItem::setThumbnail(const QImage& image, bool isHighRes)
{
    if (image.isNull()) {
        setError(tr("Load failed"));
        return;
    }

    if (m_isHighRes) {
        // 防止高清图被替换
        return;
    }

    m_hasImage = true;
    m_isHighRes = isHighRes;

    // 缩放图片
    QImage scaled = image.scaled(
        m_imageLabel->size(),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation
        );

    // 创建圆角图片
    QPixmap pixmap = createRoundedPixmap(scaled);
    m_imageLabel->setPixmap(pixmap);
    m_imageLabel->setText(QString());

    updateStyle();

    // 如果是高清版本，添加淡入动画
    if (isHighRes) {
        QPropertyAnimation* animation = new QPropertyAnimation(m_imageLabel, "windowOpacity");
        animation->setDuration(100);
        animation->setStartValue(0.5);
        animation->setEndValue(1.0);
        animation->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

void ThumbnailItem::setError(const QString& error)
{
    m_hasImage = false;
    m_isHighRes = false;
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
