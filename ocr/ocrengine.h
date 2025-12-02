#ifndef OCRENGINE_H
#define OCRENGINE_H

#include <QObject>
#include <QImage>
#include <QString>
#include <memory>

#include "datastructure.h"

namespace Ort {
class Env;
class Session;
class SessionOptions;
class Value;
}

/**
 * @brief OCR引擎 - 封装PaddleOCR的ONNX模型
 *
 * 职责：
 * 1. 加载和管理ONNX模型（det, cls, rec）
 * 2. 图像预处理
 * 3. 模型推理
 * 4. 结果后处理
 */
class OCREngine : public QObject
{
    Q_OBJECT

public:
    explicit OCREngine(QObject* parent = nullptr);
    ~OCREngine();

    /**
     * @brief 初始化OCR引擎（异步）
     * @param modelDir 模型目录
     * @return 是否开始初始化
     */
    bool initializeAsync(const QString& modelDir);

    /**
     * @brief 同步初始化（仅供测试）
     */
    bool initializeSync(const QString& modelDir);

    /**
     * @brief 获取引擎状态
     */
    OCREngineState state() const { return m_state; }

    /**
     * @brief 识别图像中的文字
     * @param image 输入图像（悬停区域截图）
     * @return 识别结果
     */
    OCRResult recognize(const QImage& image);

    /**
     * @brief 获取最后的错误信息
     */
    QString lastError() const { return m_lastError; }

signals:
    /**
     * @brief 初始化完成
     * @param success 是否成功
     * @param error 错误信息（如果失败）
     */
    void initialized(bool success, const QString& error);

    /**
     * @brief 状态改变
     */
    void stateChanged(OCREngineState state);

private:
    // 初始化内部实现
    bool initializeInternal(const QString& modelDir);

    // 三个推理步骤
    QVector<QVector<QPointF>> detectTextRegions(const QImage& image);
    int classifyOrientation(const QImage& image);
    QPair<QString, float> recognizeText(const QImage& image);

    // 图像预处理
    std::vector<float> preprocessForDet(const QImage& image, int& outW, int& outH);
    std::vector<float> preprocessForCls(const QImage& image);
    std::vector<float> preprocessForRec(const QImage& image, int& outW);

    // 后处理
    QVector<QVector<QPointF>> postprocessDet(Ort::Value& tensor,
                                             int oriW, int oriH,
                                             int resizedW, int resizedH);
    int postprocessCls(Ort::Value& tensor);
    QPair<QString, float> postprocessRec(Ort::Value& tensor);

    // 辅助函数
    QImage cropTextRegion(const QImage& image, const QVector<QPointF>& points);
    QImage rotateImage(const QImage& image, int angle);
    QString ctcDecode(const std::vector<int>& indices);
    bool loadCharacterSet(const QString& filePath);
    void setState(OCREngineState state);
    void setError(const QString& error);

private:
    OCREngineState m_state;
    QString m_lastError;
    QString m_modelDir;

    // ONNX Runtime
    std::unique_ptr<Ort::Env> m_env;
    std::unique_ptr<Ort::Session> m_detSession;
    std::unique_ptr<Ort::Session> m_clsSession;
    std::unique_ptr<Ort::Session> m_recSession;

    // 字符集
    QStringList m_characterSet;

    // 模型参数
    static constexpr int DET_TARGET_SIZE = 960;
    static constexpr float DET_THRESHOLD = 0.3f;
    static constexpr float DET_BOX_THRESHOLD = 0.6f;

    static constexpr int CLS_IMAGE_HEIGHT = 48;
    static constexpr int CLS_IMAGE_WIDTH = 192;
    static constexpr float CLS_THRESHOLD = 0.9f;

    static constexpr int REC_IMAGE_HEIGHT = 48;
};

#endif // OCRENGINE_H
