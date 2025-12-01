#ifndef PERTHREADMUPDFRENDERER_H
#define PERTHREADMUPDFRENDERER_H

#include <QString>
#include <QImage>
#include <QSizeF>
#include <QVector>
#include <QMutex>
#include "papereffectenhancer.h"
#include "datastructure.h"

extern "C" {
#include <mupdf/fitz.h>
}


/**
 * @brief 线程隔离的MuPDF渲染器
 *
 * 每个渲染器有自己 context 和 document，不共享
 */
class PerThreadMuPDFRenderer
{
public:
    PerThreadMuPDFRenderer();
    explicit PerThreadMuPDFRenderer(const QString& documentPath);
    ~PerThreadMuPDFRenderer();

    // 禁止拷贝
    PerThreadMuPDFRenderer(const PerThreadMuPDFRenderer&) = delete;
    PerThreadMuPDFRenderer& operator=(const PerThreadMuPDFRenderer&) = delete;

    /**
     * @brief 加载 PDF 文档
     * @param filePath 文件路径
     * @param errorMsg 错误信息输出参数
     * @return 成功返回 true
     */
    bool loadDocument(const QString& filePath, QString* errorMsg = nullptr);

    /**
     * @brief 关闭当前文档
     */
    void closeDocument();

    /**
     * @brief 获取当前文档路径
     */
    QString documentPath() const;

    /**
     * @brief 检查文档是否成功加载
     */
    bool isDocumentLoaded() const;

    /**
     * @brief 获取文档总页数
     */
    int pageCount() const;

    /**
     * @brief 获取指定页面的尺寸
     */
    QSizeF pageSize(int pageIndex) const;

    /**
     * @brief 渲染指定页面
     * @param pageIndex 页面索引 (0-based)
     * @param zoom 缩放比例
     * @param rotation 旋转角度 (0, 90, 180, 270)
     * @return 渲染结果
     */
    RenderResult renderPage(int pageIndex, double zoom, int rotation);

    /**
     * @brief 提取页面文本
     * @param pageIndex 页面索引
     * @param outData 输出的文本数据
     * @param errorMsg 错误信息输出参数
     * @return 成功返回 true
     */
    bool extractText(int pageIndex, PageTextData& outData, QString* errorMsg = nullptr);

    /**
     * @brief 检测是否为文本 PDF
     * @param samplePages 采样页数，0 表示全部检查
     * @return 如果采样页面中 30% 以上有文本则返回 true
     */
    bool isTextPDF(int samplePages = 5);

    /**
     * @brief 获取最后的错误信息
     */
    QString getLastError() const;

    void setPaperEffectEnabled(bool enabled);
    bool paperEffectEnabled() const { return m_paperEffectEnabled; }

    fz_context* context() const { return m_context; }
    fz_document* document() const { return m_document; }


private:
    /**
     * @brief 创建 MuPDF context
     * @return 成功返回 true
     */
    bool createContext();

    /**
     * @brief 销毁 MuPDF context
     */
    void destroyContext();

    /**
     * @brief 设置错误信息
     */
    void setLastError(const QString& error) const;

private:
    QString m_documentPath;                     // 文档路径
    fz_context* m_context;                      // MuPDF context (独立实例)
    fz_document* m_document;                    // MuPDF document
    int m_pageCount;                            // 文档页数
    mutable QVector<QSizeF> m_pageSizeCache;    // 页面尺寸缓存
    mutable QString m_lastError;                // 最后的错误信息

    PaperEffectEnhancer m_paperEffectEnhancer;
    bool m_paperEffectEnabled;
};

#endif // PERTHREADMUPDFRENDERER_H
