#ifndef ThreadSafeRendererUTIL_H
#define ThreadSafeRendererUTIL_H

#include <QDebug>
#include <QMutex>
#include <memory>

// ========================================
// MuPDF 多线程支持 - 全局锁管理
// ========================================

namespace {
constexpr int FZ_LOCK_MAX = 4;

static std::unique_ptr<QMutex> g_mupdf_locks[FZ_LOCK_MAX];
static bool g_locks_initialized = false;
static QMutex g_init_mutex;

extern "C" {
static void lock_mutex(void* user, int lock_no)
{
    if (lock_no >= 0 && lock_no < FZ_LOCK_MAX && g_mupdf_locks[lock_no]) {
        g_mupdf_locks[lock_no]->lock();
    }
}

static void unlock_mutex(void* user, int lock_no)
{
    if (lock_no >= 0 && lock_no < FZ_LOCK_MAX && g_mupdf_locks[lock_no]) {
        g_mupdf_locks[lock_no]->unlock();
    }
}
}

void initializeMuPDFLocks()
{
    QMutexLocker locker(&g_init_mutex);
    if (g_locks_initialized) return;

    for (int i = 0; i < FZ_LOCK_MAX; ++i) {
        g_mupdf_locks[i] = std::make_unique<QMutex>();
    }

    g_locks_initialized = true;
    qDebug() << "MuPDF locks initialized (auto cleanup with smart pointers)";
}

}


#endif // ThreadSafeRendererUTIL_H
