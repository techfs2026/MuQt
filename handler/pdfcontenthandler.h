#ifndef PDFCONTENTHANDLER_H
#define PDFCONTENTHANDLER_H

#include <QObject>
#include <QString>
#include <QImage>
#include <QVector>
#include <QSet>
#include <memory>

#include "thumbnailmanagerv2.h"

class MuPDFRenderer;
class OutlineManager;
class OutlineItem;
class OutlineEditor;

class PDFContentHandler : public QObject
{
    Q_OBJECT

public:
    explicit PDFContentHandler(MuPDFRenderer* renderer, QObject* parent = nullptr);
    ~PDFContentHandler();

    // 文档加载
    bool loadDocument(const QString& filePath, QString* errorMessage = nullptr);
    void closeDocument();
    bool isDocumentLoaded() const;
    int pageCount() const;

    // 大纲管理
    bool loadOutline();
    OutlineItem* outlineRoot() const;
    int outlineItemCount() const;
    bool hasOutline() const;
    void clearOutline();

    // 缩略图管理
    void loadThumbnails();
    void handleVisibleRangeChanged(const QSet<int>& visibleIndices, int margin);
    void startInitialThumbnailLoad(const QSet<int>& initialVisible);

    QImage getThumbnail(int pageIndex, bool preferHighRes = false) const;
    bool hasThumbnail(int pageIndex) const;
    void setThumbnailSize(int lowResWidth, int highResWidth);
    void setThumbnailRotation(int rotation);
    void cancelThumbnailTasks();
    void clearThumbnails();
    QString getThumbnailStatistics() const;
    int cachedThumbnailCount() const;

    // 大纲编辑
    OutlineItem* addOutlineItem(OutlineItem* parent, const QString& title,
                                int pageIndex, int insertIndex = -1);
    bool deleteOutlineItem(OutlineItem* item);
    bool renameOutlineItem(OutlineItem* item, const QString& newTitle);
    bool saveOutlineChanges(const QString& savePath);
    bool hasUnsavedOutlineChanges() const;

    // 工具方法
    bool isTextPDF(int samplePages = 5) const;
    void reset();

    // 获取子管理器
    OutlineManager* outlineManager() const { return m_outlineManager.get(); }
    ThumbnailManagerV2* thumbnailManager() const { return m_thumbnailManager.get(); }
    OutlineEditor* outlineEditor() const { return m_outlineEditor.get(); }

signals:
    // 文档事件
    void documentLoaded(const QString& filePath, int pageCount);
    void documentClosed();
    void documentError(const QString& error);

    // 大纲事件
    void outlineLoaded(bool success, int itemCount);
    void outlineModified();
    void outlineSaveCompleted(bool success, const QString& errorMsg);

    // 缩略图事件
    void thumbnailsInitialized(int pageCount);
    void thumbnailLoaded(int pageIndex, const QImage& thumbnail, bool isHighRes);
    void thumbnailLoadProgress(int current, int total);

private:
    void setupConnections();
    void startBackgroundLowResRendering();

private:
    MuPDFRenderer* m_renderer;
    std::unique_ptr<OutlineManager> m_outlineManager;
    std::unique_ptr<ThumbnailManagerV2> m_thumbnailManager;
    std::unique_ptr<OutlineEditor> m_outlineEditor;
};

#endif // PDFCONTENTHANDLER_H
