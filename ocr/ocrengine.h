// ocrengine.h
#ifndef OCRENGINE_H
#define OCRENGINE_H

#include <QObject>
#include <QImage>
#include <QString>
#include <memory>
#include "rapidocr-cpp/rapidocr.h"

// OCR引擎状态
enum class OCREngineState {
    Uninitialized,  // 未初始化
    Loading,        // 加载中
    Ready,          // 就绪
    Processing,     // 处理中
    Error           // 错误
};

// OCR识别结果
struct OCRResult {
    bool success = false;           // 是否成功
    QString text;                   // 识别的文本
    float confidence = 0.0f;        // 置信度
    QString error;                  // 错误信息

    // 详细结果 (可选)
    std::vector<std::vector<cv::Point2f>> boxes;  // 文本框
    std::vector<std::string> texts;                // 各区域文本
    std::vector<float> scores;                     // 各区域置信度
    float elapsedTime = 0.0f;                      // 耗时(秒)
};

class OCREngine : public QObject
{
    Q_OBJECT

public:
    explicit OCREngine(QObject* parent = nullptr);
    ~OCREngine() override;

    // 初始化
    bool initializeSync(const QString& modelDir);
    bool initializeAsync(const QString& modelDir);

    // 识别
    OCRResult recognize(const QImage& image);
    OCRResult recognizeDetailed(const QImage& image);  // 返回详细结果

    // 参数设置
    void setTextScore(float score);
    void setUseDet(bool use);
    void setUseCls(bool use);
    void setUseRec(bool use);
    void setReturnWordBox(bool enable);

    // 状态查询
    OCREngineState state() const { return m_state; }
    QString lastError() const { return m_lastError; }
    bool isReady() const { return m_state == OCREngineState::Ready; }

signals:
    void initialized(bool success, const QString& error);
    void stateChanged(OCREngineState state);
    void recognitionCompleted(const OCRResult& result);

private:
    bool initializeInternal(const QString& modelDir);
    void setState(OCREngineState state);
    void setError(const QString& error);

    // 将RapidOCROutput转换为OCRResult
    OCRResult convertToOCRResult(const RapidOCR::RapidOCROutput& output);

private:
    std::unique_ptr<RapidOCR::RapidOCR> m_rapidOCR;
    OCREngineState m_state;
    QString m_lastError;
    QString m_modelDir;

    // 配置参数
    float m_textScore = 0.5f;
    bool m_useDet = true;
    bool m_useCls = true;
    bool m_useRec = true;
    bool m_returnWordBox = false;
};

#endif // OCRENGINE_H
