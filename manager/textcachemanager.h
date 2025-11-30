#ifndef TEXTCACHEMANAGER_H
#define TEXTCACHEMANAGER_H

#include <QObject>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QAtomicInt>
#include <QThreadPool>

#include "datastructure.h"

class PerThreadMuPDFRenderer;
class PageExtractTask;

/**
 * @brief 文本缓存管理器
 *
 * 负责管理页面文本数据的缓存和异步预加载
 * 不直接接触 MuPDF API，所有渲染工作委托给 PerThreadMuPDFRenderer
 */
class TextCacheManager : public QObject
{
    Q_OBJECT

public:
    explicit TextCacheManager(PerThreadMuPDFRenderer* renderer, QObject* parent = nullptr);
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
    // 由 PageExtractTask 通过 QMetaObject::invokeMethod 调用
    void handleTaskDone(int pageIndex, PageTextData pageData, bool ok);

private:
    friend class PageExtractTask;

    PerThreadMuPDFRenderer* m_renderer;

    // 缓存（页索引 -> PageTextData）
    QHash<int, PageTextData> m_cache;
    mutable QMutex m_mutex;

    // 缓存限制（-1 表示无限制）
    int m_maxCacheSize;

    // 预加载状态（原子）
    QAtomicInt m_isPreloading;
    QAtomicInt m_cancelRequested;
    QAtomicInt m_preloadedPages;
    QAtomicInt m_remainingTasks;

    // 线程池
    QThreadPool m_threadPool;

    // 统计信息
    qint64 m_hitCount;
    qint64 m_missCount;
};

#endif // TEXTCACHEMANAGER_H
