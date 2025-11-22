#ifndef TEXTCACHEMANAGER_H
#define TEXTCACHEMANAGER_H

#include <QObject>
#include <QHash>
#include <QVector>
#include <QMutex>
#include <QString>
#include <QRectF>
#include <QChar>
#include <QAtomicInt>
#include <QThreadPool>

#include "mupdfrenderer.h"
#include "datastructure.h"

extern "C" {
#include <mupdf/fitz.h>
}

class PageExtractTask;

class TextCacheManager : public QObject
{
    Q_OBJECT

public:
    explicit TextCacheManager(MuPDFRenderer* renderer, QObject* parent = nullptr);
    ~TextCacheManager();

    // 预加载控制
    void startPreload();
    void cancelPreload();
    bool isPreloading() const;
    int computePreloadProgress() const;

    // 缓存访问
    PageTextData getPageTextData(int pageIndex);
    void addPageTextData(int pageIndex, const PageTextData& data);
    bool contains(int pageIndex) const;

    // 缓存管理
    void clear();
    void setMaxCacheSize(int maxPages);
    int cacheSize() const;

    // 统计信息
    QString getStatistics() const;

signals:
    void preloadProgress(int current, int total);
    void preloadCompleted();
    void preloadCancelled();
    void preloadError(const QString& error);

private slots:
    // 由 PageExtractTask 通过 QMetaObject::invokeMethod 调用（QueuedConnection）
    void handleTaskDone(int pageIndex, PageTextData pageData, bool ok);

private:
    friend class PageExtractTask;

    MuPDFRenderer* m_renderer;

    // 缓存（页索引 -> PageTextData）
    QHash<int, PageTextData> m_cache;
    mutable QMutex m_mutex;

    // 缓存限制（-1 表示无限制）
    int m_maxCacheSize;

    // 预加载状态（原子）
    QAtomicInt m_isPreloading;
    QAtomicInt m_cancelRequested;
    QAtomicInt m_preloadedPages;

    // 剩余待处理任务数（原子，用于判断何时完成）
    QAtomicInt m_remainingTasks;

    // 线程池（使用全局或独立池均可，cpp 中使用 QThreadPool::globalInstance()）
    // 此处保留一个成员以便未来改成独立池（可选）
    QThreadPool m_threadPool;

    // 文档路径副本（线程安全的只读拷贝）
    QString m_documentPath;

    // 统计信息
    qint64 m_hitCount;
    qint64 m_missCount;
};

#endif // TEXTCACHEMANAGER_H
