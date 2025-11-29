#ifndef THREADSAFERENDERER_H
#define THREADSAFERENDERER_H

#include <QString>
#include <QImage>
#include <QSizeF>
#include <QVector>
#include <QMutex>
#include "datastructure.h"

extern "C" {
#include <mupdf/fitz.h>
}

struct RenderResult {
    bool success = false;
    QImage image;
    QString errorMessage;
};

/**
 * @brief 线程安全的 PDF 渲染器
 *
 * 每次 loadDocument 时创建新的 context 和 document
 * closeDocument 时销毁它们
 * 支持多种渲染和文本提取功能
 */
class ThreadSafeRenderer
{
public:
    /**
     * @brief 默认构造函数
     */
    ThreadSafeRenderer();

    /**
     * @brief 构造函数 - 创建渲染器并加载文档
     * @param documentPath PDF 文档路径
     */
    explicit ThreadSafeRenderer(const QString& documentPath);

    /**
     * @brief 析构函数 - 自动清理资源
     */
    ~ThreadSafeRenderer();

    // 禁止拷贝
    ThreadSafeRenderer(const ThreadSafeRenderer&) = delete;
    ThreadSafeRenderer& operator=(const ThreadSafeRenderer&) = delete;

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
     * @brief 获取多个页面的尺寸
     */
    QVector<QSizeF> pageSizes(int startPage = 0, int endPage = -1) const;

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

    /**
     * @brief 获取 MuPDF context（仅用于调试）
     */
    fz_context* context() const { return m_context; }

    /**
     * @brief 获取 MuPDF document（仅用于调试）
     */
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
    mutable QMutex m_mutex;                     // 保护并发访问
};

#endif // THREADSAFERENDERER_H
