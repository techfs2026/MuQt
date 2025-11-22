#include "pdfcontenthandler.h"
#include "mupdfrenderer.h"
#include "outlinemanager.h"
#include "thumbnailmanager.h"
#include "outlineitem.h"
#include "outlineeditor.h"
#include <QDebug>
#include <QFileInfo>

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

    // 创建子管理器
    m_outlineManager = std::make_unique<OutlineManager>(m_renderer, this);
    m_thumbnailManager = std::make_unique<ThumbnailManager>(m_renderer, this);
    m_outlineEditor = std::make_unique<OutlineEditor>(m_renderer, this);

    // 连接信号
    setupConnections();
}

PDFContentHandler::~PDFContentHandler()
{
    closeDocument();
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

    // 关闭当前文档
    if (isDocumentLoaded()) {
        closeDocument();
    }

    // 加载新文档
    QString error;
    if (!m_renderer->loadDocument(filePath, &error)) {
        if (errorMessage) {
            *errorMessage = error;
        }
        emit documentError(error);
        return false;
    }

    m_currentFilePath = filePath;
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

    // 取消正在进行的操作
    if (m_thumbnailManager) {
        m_thumbnailManager->cancelLoading();
    }

    // 清空数据
    clearOutline();
    clearThumbnails();

    // 关闭文档
    if (m_renderer) {
        m_renderer->closeDocument();
    }

    m_currentFilePath.clear();

    qInfo() << "PDFContentHandler: Document closed";

    emit documentClosed();
}

bool PDFContentHandler::isDocumentLoaded() const
{
    return m_renderer && m_renderer->isDocumentLoaded();
}

QString PDFContentHandler::documentPath() const
{
    return m_currentFilePath;
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

    // 同步到编辑器
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

// ========== 缩略图管理 ==========

void PDFContentHandler::startLoadThumbnails(int thumbnailWidth)
{
    if (!isDocumentLoaded()) {
        qWarning() << "PDFContentHandler: Cannot load thumbnails - no document loaded";
        return;
    }

    if (!m_thumbnailManager) {
        qWarning() << "PDFContentHandler: Thumbnail manager not initialized";
        return;
    }

    m_thumbnailManager->startLoading(pageCount(), thumbnailWidth);
}

void PDFContentHandler::cancelThumbnailLoading()
{
    if (m_thumbnailManager) {
        m_thumbnailManager->cancelLoading();
    }
}

QImage PDFContentHandler::getThumbnail(int pageIndex) const
{
    if (!m_thumbnailManager) {
        return QImage();
    }
    return m_thumbnailManager->getThumbnail(pageIndex);
}

bool PDFContentHandler::isThumbnailLoading() const
{
    if (!m_thumbnailManager) {
        return false;
    }
    return m_thumbnailManager->isLoading();
}

int PDFContentHandler::loadedThumbnailCount() const
{
    if (!m_thumbnailManager) {
        return 0;
    }
    return m_thumbnailManager->loadedCount();
}

void PDFContentHandler::setThumbnailSize(int width)
{
    if (m_thumbnailManager) {
        m_thumbnailManager->setThumbnailWidth(width);
    }
}

void PDFContentHandler::clearThumbnails()
{
    if (m_thumbnailManager) {
        m_thumbnailManager->clear();
    }
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
    // 连接 OutlineManager 信号
    if (m_outlineManager) {
        connect(m_outlineManager.get(), &OutlineManager::outlineLoaded,
                this, &PDFContentHandler::outlineLoaded);
    }

    // 连接 ThumbnailManager 信号
    if (m_thumbnailManager) {
        connect(m_thumbnailManager.get(), &ThumbnailManager::loadStarted,
                this, &PDFContentHandler::thumbnailLoadStarted);

        connect(m_thumbnailManager.get(), &ThumbnailManager::loadProgress,
                this, &PDFContentHandler::thumbnailLoadProgress);

        connect(m_thumbnailManager.get(), &ThumbnailManager::thumbnailReady,
                this, &PDFContentHandler::thumbnailReady);

        connect(m_thumbnailManager.get(), &ThumbnailManager::loadCompleted,
                this, &PDFContentHandler::thumbnailLoadCompleted);

        connect(m_thumbnailManager.get(), &ThumbnailManager::loadCancelled,
                this, &PDFContentHandler::thumbnailLoadCancelled);
    }

    connect(m_outlineEditor.get(), &OutlineEditor::outlineModified,
            this, &PDFContentHandler::outlineModified);
    connect(m_outlineEditor.get(), &OutlineEditor::saveCompleted,
            this, &PDFContentHandler::outlineSaveCompleted);
}


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
