#include "pdfcontenthandler.h"
#include "mupdfrenderer.h"
#include "outlinemanager.h"

#include "outlineitem.h"
#include "outlineeditor.h"
#include <QDebug>
#include <QFileInfo>
#include <QTimer>

PDFContentHandler::PDFContentHandler(MuPDFRenderer* renderer, QObject* parent)
    : QObject(parent)
    , m_renderer(renderer)
    , m_outlineManager(nullptr)
    , m_thumbnailManager(nullptr)
{
    if (!m_renderer) {
        qWarning() << "PDFContentHandler: renderer is null!";
        return;
    }

    m_outlineManager = std::make_unique<OutlineManager>(m_renderer, this);
    m_thumbnailManager = std::make_unique<ThumbnailManagerV2>(m_renderer, this);
    m_outlineEditor = std::make_unique<OutlineEditor>(m_renderer, this);

    setupConnections();
}

PDFContentHandler::~PDFContentHandler()
{
}

// ========== 文档加载 ==========

bool PDFContentHandler::loadDocument(const QString& filePath, QString* errorMessage)
{
    if (!m_renderer) {
        if (errorMessage) {
            *errorMessage = tr("Renderer not initialized");
        }
        return false;
    }

    if (isDocumentLoaded()) {
        closeDocument();
    }

    QString error;
    if (!m_renderer->loadDocument(filePath, &error)) {
        if (errorMessage) {
            *errorMessage = error;
        }
        emit documentError(error);
        return false;
    }

    int pageCount = m_renderer->pageCount();

    qInfo() << "PDFContentHandler: Document loaded successfully -"
            << QFileInfo(filePath).fileName()
            << "(" << pageCount << "pages)";

    emit documentLoaded(filePath, pageCount);

    return true;
}

void PDFContentHandler::closeDocument()
{
    if (!isDocumentLoaded()) {
        return;
    }

    if (m_renderer) {
        m_renderer->closeDocument();
    }

    clearOutline();
    clearThumbnails();

    qInfo() << "PDFContentHandler: Document closed";

    emit documentClosed();
}

bool PDFContentHandler::isDocumentLoaded() const
{
    return m_renderer && m_renderer->isDocumentLoaded();
}

int PDFContentHandler::pageCount() const
{
    if (!isDocumentLoaded()) {
        return 0;
    }
    return m_renderer->pageCount();
}

// ========== 大纲管理 ==========

bool PDFContentHandler::loadOutline()
{
    if (!isDocumentLoaded()) {
        qWarning() << "PDFContentHandler: Cannot load outline - no document loaded";
        return false;
    }

    if (!m_outlineManager) {
        qWarning() << "PDFContentHandler: Outline manager not initialized";
        return false;
    }

    bool success = m_outlineManager->loadOutline();

    if (success && m_outlineEditor) {
        m_outlineEditor->setRoot(m_outlineManager->root());
    }

    return success;
}

OutlineItem* PDFContentHandler::outlineRoot() const
{
    if (!m_outlineManager) {
        return nullptr;
    }
    return m_outlineManager->root();
}

int PDFContentHandler::outlineItemCount() const
{
    if (!m_outlineManager) {
        return 0;
    }
    return m_outlineManager->totalItemCount();
}

bool PDFContentHandler::hasOutline() const
{
    return outlineItemCount() > 0;
}

void PDFContentHandler::clearOutline()
{
    if (m_outlineManager) {
        m_outlineManager->clear();
    }
}

// ========== 缩略图管理 (新版智能管理器) ==========

void PDFContentHandler::loadThumbnails()
{
    if (!isDocumentLoaded() || !m_thumbnailManager) {
        return;
    }

    int pageCount = m_renderer->pageCount();
    qInfo() << "PDFContentHandler: Starting thumbnail loading for" << pageCount << "pages";

    // 通知UI初始化完成
    emit thumbnailsInitialized(pageCount);
}

void PDFContentHandler::handleVisibleRangeChanged(const QSet<int>& visibleIndices, int margin)
{
    if (!m_thumbnailManager) {
        return;
    }

    // 仅对大文档生效
    m_thumbnailManager->handleVisibleRangeChanged(visibleIndices);
}

void PDFContentHandler::startInitialThumbnailLoad(const QSet<int>& initialVisible)
{
    if (!m_thumbnailManager) {
        return;
    }

    qDebug() << "PDFContentHandler: Starting initial thumbnail load for"
             << initialVisible.size() << "visible pages";

    // 启动智能加载（自动选择策略）
    m_thumbnailManager->startLoading(initialVisible);
}


QImage PDFContentHandler::getThumbnail(int pageIndex, bool preferHighRes) const
{
    if (!m_thumbnailManager) {
        return QImage();
    }
    return m_thumbnailManager->getThumbnail(pageIndex);
}

bool PDFContentHandler::hasThumbnail(int pageIndex) const
{
    if (!m_thumbnailManager) {
        return false;
    }
    return m_thumbnailManager->hasThumbnail(pageIndex);
}

void PDFContentHandler::setThumbnailSize(int lowResWidth, int highResWidth)
{
    if (m_thumbnailManager) {
        m_thumbnailManager->setThumbnailWidth(lowResWidth);  // V2只需要一个宽度
    }
}

void PDFContentHandler::setThumbnailRotation(int rotation)
{
    if (m_thumbnailManager) {
        m_thumbnailManager->setRotation(rotation);
    }
}

void PDFContentHandler::cancelThumbnailTasks()
{
    if (m_thumbnailManager) {
        m_thumbnailManager->cancelAllTasks();
    }
}

void PDFContentHandler::clearThumbnails()
{
    if (m_thumbnailManager) {
        m_thumbnailManager->clear();
    }
}

QString PDFContentHandler::getThumbnailStatistics() const
{
    return m_thumbnailManager ? m_thumbnailManager->getStatistics() : QString();
}

int PDFContentHandler::cachedThumbnailCount() const
{
    return m_thumbnailManager ? m_thumbnailManager->cachedCount() : 0;
}

// ========== 工具方法 ==========

bool PDFContentHandler::isTextPDF(int samplePages) const
{
    if (!isDocumentLoaded()) {
        return false;
    }
    return m_renderer->isTextPDF(samplePages);
}

void PDFContentHandler::reset()
{
    closeDocument();
}

// ========== 私有方法 ==========

void PDFContentHandler::setupConnections()
{
    if (m_outlineManager) {
        connect(m_outlineManager.get(), &OutlineManager::outlineLoaded,
                this, &PDFContentHandler::outlineLoaded);
    }

    if (m_thumbnailManager) {
        connect(m_thumbnailManager.get(), &ThumbnailManagerV2::thumbnailLoaded,  // ← V2
                this, &PDFContentHandler::thumbnailLoaded);

        connect(m_thumbnailManager.get(), &ThumbnailManagerV2::loadProgress,    // ← V2
                this, &PDFContentHandler::thumbnailLoadProgress);
    }

    if (m_outlineEditor) {
        connect(m_outlineEditor.get(), &OutlineEditor::outlineModified,
                this, &PDFContentHandler::outlineModified);
        connect(m_outlineEditor.get(), &OutlineEditor::saveCompleted,
                this, &PDFContentHandler::outlineSaveCompleted);
    }
}

// ========== 大纲编辑 ==========

OutlineItem* PDFContentHandler::addOutlineItem(OutlineItem* parent,
                                               const QString& title,
                                               int pageIndex,
                                               int insertIndex)
{
    if (!m_outlineEditor) {
        return nullptr;
    }
    return m_outlineEditor->addOutline(parent, title, pageIndex, insertIndex);
}

bool PDFContentHandler::deleteOutlineItem(OutlineItem* item)
{
    if (!m_outlineEditor) {
        return false;
    }
    return m_outlineEditor->deleteOutline(item);
}

bool PDFContentHandler::renameOutlineItem(OutlineItem* item, const QString& newTitle)
{
    if (!m_outlineEditor) {
        return false;
    }
    return m_outlineEditor->renameOutline(item, newTitle);
}

bool PDFContentHandler::saveOutlineChanges(const QString& savePath)
{
    if (!m_outlineEditor) {
        return false;
    }
    return m_outlineEditor->saveToDocument(savePath);
}

bool PDFContentHandler::hasUnsavedOutlineChanges() const
{
    return m_outlineEditor ? m_outlineEditor->hasUnsavedChanges() : false;
}
