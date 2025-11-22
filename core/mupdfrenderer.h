#ifndef MUPDFRENDERER_H
#define MUPDFRENDERER_H

#include <QString>
#include <QVector>
#include <QSizeF>
#include <QImage>

#include "datastructure.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

class MuPDFRenderer
{
public:
    struct RenderResult {
        bool success = false;
        QImage image;
        QString errorMessage;
    };

    MuPDFRenderer();
    ~MuPDFRenderer();

    // 主实例用于 UI 线程：打开/关闭文档、获取页数与页尺寸、渲染大图
    bool loadDocument(const QString& filePath, QString* errorMsg = nullptr);
    void closeDocument();

    QString documentPath() const;
    bool isDocumentLoaded() const;
    int pageCount() const;
    QSizeF pageSize(int pageIndex) const;
    QVector<QSizeF> pageSizes(int startPage = 0, int endPage = -1) const;

    // 渲染单页（单线程调用时安全）
    RenderResult renderPage(int pageIndex, double zoom, int rotation = 0);

    bool extractText(int pageIndex, PageTextData& outData, QString* errorMsg);

    // 辅助检查：是否为文本型 PDF（单线程调用）
    bool isTextPDF(int samplePages = 3);
    QString getLastError() const;

    QString currentFilePath() const {return m_currentFilePath;}

    fz_context* context() {return m_context;}
    fz_document* document() {return m_document;}

private:
    // NOT thread-safe: keep these per-instance and only used by the thread that owns this instance.
    fz_context* m_context;
    fz_document* m_document;
    int m_pageCount;
    QVector<QSizeF> m_pageSizeCache;
    QString m_currentFilePath;
    mutable QString m_lastError;

    // helpers
    void setLastError(const QString& error) const;
};

#endif // MUPDFRENDERER_H
