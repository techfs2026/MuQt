#include "pdfbackgroundtaskhandler.h"

PDFBackgroundTaskHandler::PDFBackgroundTaskHandler(QObject* parent)
    : QObject(parent)
    , m_documentGeneration(1)
{
    for (auto& generation : m_taskGenerations) {
        generation.store(0, std::memory_order_relaxed);
    }
    qRegisterMetaType<PDFBackgroundTaskToken>("PDFBackgroundTaskToken");
}

PDFBackgroundTaskToken PDFBackgroundTaskHandler::startTask(PDFBackgroundTaskType type)
{
    const std::size_t index = taskIndex(type);
    const quint64 taskGeneration =
        m_taskGenerations[index].fetch_add(1, std::memory_order_acq_rel) + 1;
    return { type, m_documentGeneration.load(std::memory_order_acquire), taskGeneration };
}

void PDFBackgroundTaskHandler::invalidate(PDFBackgroundTaskType type)
{
    m_taskGenerations[taskIndex(type)].fetch_add(1, std::memory_order_acq_rel);
}

void PDFBackgroundTaskHandler::invalidateAll()
{
    const quint64 generation =
        m_documentGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    emit allTasksInvalidated(generation);
}

bool PDFBackgroundTaskHandler::isCurrent(const PDFBackgroundTaskToken& token) const
{
    if (!token.isValid() || token.type == PDFBackgroundTaskType::Count) {
        return false;
    }

    return token.documentGeneration == m_documentGeneration.load(std::memory_order_acquire) &&
           token.taskGeneration ==
               m_taskGenerations[taskIndex(token.type)].load(std::memory_order_acquire);
}
