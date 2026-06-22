#ifndef TEXTCACHEMANAGER_H
#define TEXTCACHEMANAGER_H

#include <QObject>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QAtomicInt>
#include <QThreadPool>

#include "datastructure.h"
#include "pdfbackgroundtaskhandler.h"

class PerThreadMuPDFRenderer;
class PageExtractTask;

class TextCacheManager : public QObject
{
    Q_OBJECT

public:
    explicit TextCacheManager(PerThreadMuPDFRenderer* renderer,
                              PDFBackgroundTaskHandler* backgroundTaskHandler,
                              QObject* parent = nullptr);
    ~TextCacheManager();

    void startPreload();
    void cancelPreload();
    bool isPreloading() const;
    int computePreloadProgress() const;

    PageTextData getPageTextData(int pageIndex);
    void addPageTextData(int pageIndex, const PageTextData& data);
    bool contains(int pageIndex) const;

    void clear();
    void setMaxCacheSize(int maxPages);
    int cacheSize() const;

    QString getStatistics() const;

signals:
    void preloadProgress(int current, int total);
    void preloadCompleted();
    void preloadCancelled();
    void preloadError(const QString& error);

private slots:
    void handleTaskDone(int pageIndex, PageTextData pageData, bool ok,
                        PDFBackgroundTaskToken taskToken);

private:
    friend class PageExtractTask;

    PerThreadMuPDFRenderer* m_renderer;
    PDFBackgroundTaskHandler* m_backgroundTaskHandler;

    QHash<int, PageTextData> m_cache;
    mutable QMutex m_mutex;

    int m_maxCacheSize;

    QAtomicInt m_isPreloading;
    QAtomicInt m_cancelRequested;
    QAtomicInt m_preloadedPages;
    QAtomicInt m_remainingTasks;
    PDFBackgroundTaskToken m_activeTask;

    QThreadPool m_threadPool;

    qint64 m_hitCount;
    qint64 m_missCount;
};

#endif // TEXTCACHEMANAGER_H
