#ifndef PDFBACKGROUNDTASKHANDLER_H
#define PDFBACKGROUNDTASKHANDLER_H

#include <QObject>
#include <QMetaType>
#include <QtGlobal>

#include <array>
#include <atomic>
#include <cstddef>

// 每个 PDFDocumentSession 持有独立实例，令牌天然不能跨 Tab 使用。
enum class PDFBackgroundTaskType : quint8 {
    TextPreload = 0,
    Search,
    Count
};

struct PDFBackgroundTaskToken {
    PDFBackgroundTaskType type = PDFBackgroundTaskType::TextPreload;
    quint64 documentGeneration = 0;
    quint64 taskGeneration = 0;

    bool isValid() const { return documentGeneration != 0 && taskGeneration != 0; }
};

Q_DECLARE_METATYPE(PDFBackgroundTaskToken)

// 只管理任务的有效期，不实现搜索、预加载等具体业务。
class PDFBackgroundTaskHandler : public QObject
{
    Q_OBJECT

public:
    explicit PDFBackgroundTaskHandler(QObject* parent = nullptr);

    PDFBackgroundTaskToken startTask(PDFBackgroundTaskType type);
    void invalidate(PDFBackgroundTaskType type);
    void invalidateAll();
    bool isCurrent(const PDFBackgroundTaskToken& token) const;

signals:
    void allTasksInvalidated(quint64 documentGeneration);

private:
    static constexpr std::size_t taskIndex(PDFBackgroundTaskType type)
    {
        return static_cast<std::size_t>(type);
    }

    std::atomic<quint64> m_documentGeneration;
    std::array<std::atomic<quint64>, static_cast<std::size_t>(PDFBackgroundTaskType::Count)>
        m_taskGenerations;
};

#endif // PDFBACKGROUNDTASKHANDLER_H
