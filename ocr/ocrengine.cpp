// ocrengine.cpp
#include "ocrengine.h"
#include <QDebug>
#include <QtConcurrent>

OCREngine::OCREngine(QObject* parent)
    : QObject(parent)
    , m_state(OCREngineState::Uninitialized)
{
}

OCREngine::~OCREngine()
{
}

bool OCREngine::initializeAsync(const QString& modelDir)
{
    if (m_state == OCREngineState::Loading || m_state == OCREngineState::Ready) {
        qWarning() << "OCREngine: Already initialized or loading";
        return false;
    }

    setState(OCREngineState::Loading);
    m_modelDir = modelDir;

    QtConcurrent::run([this, modelDir]() {
        bool success = initializeInternal(modelDir);

        QMetaObject::invokeMethod(this, [this, success]() {
            if (success) {
                setState(OCREngineState::Ready);
                emit initialized(true, QString());
            } else {
                setState(OCREngineState::Error);
                emit initialized(false, m_lastError);
            }
        }, Qt::QueuedConnection);
    });

    return true;
}

bool OCREngine::initializeSync(const QString& modelDir)
{
    setState(OCREngineState::Loading);
    m_modelDir = modelDir;

    bool success = initializeInternal(modelDir);

    if (success) {
        setState(OCREngineState::Ready);
    } else {
        setState(OCREngineState::Error);
    }

    return success;
}

bool OCREngine::initializeInternal(const QString& modelDir)
{
    qInfo() << "OCREngine: Starting initialization...";

    try {
        // 创建RapidOCR配置
        RapidOCR::RapidOCRConfig config;
        config.modelDir = modelDir;
        config.textScore = m_textScore;
        config.useDet = m_useDet;
        config.useCls = m_useCls;
        config.useRec = m_useRec;
        config.returnWordBox = m_returnWordBox;

        // 创建RapidOCR实例
        m_rapidOCR = std::make_unique<RapidOCR::RapidOCR>(config);

        // 初始化
        if (!m_rapidOCR->initialize(modelDir)) {
            setError("RapidOCR初始化失败: " + m_rapidOCR->getLastError());
            return false;
        }

        qInfo() << "OCREngine: Initialization successful";
        return true;

    } catch (const std::exception& e) {
        setError(QString("初始化异常: %1").arg(QString::fromStdString(e.what())));
        return false;
    }
}

OCRResult OCREngine::recognize(const QImage& image)
{
    OCRResult result;

    if (m_state != OCREngineState::Ready) {
        result.error = tr("OCR引擎未就绪");
        return result;
    }

    if (image.isNull() || image.width() < 10 || image.height() < 10) {
        result.error = tr("输入图像无效");
        return result;
    }

    try {
        setState(OCREngineState::Processing);

        qDebug() << "OCREngine: Starting recognition...";

        // 执行OCR识别
        RapidOCR::RapidOCROutput output = (*m_rapidOCR)(image);

        // 转换结果
        result = convertToOCRResult(output);

        setState(OCREngineState::Ready);

        if (result.success) {
            qInfo() << "OCREngine: Recognition completed";
            qInfo() << "  Text:" << result.text;
            qInfo() << "  Confidence:" << result.confidence;
        } else {
            result.error = tr("未识别到文本");
        }

        emit recognitionCompleted(result);

    } catch (const std::exception& e) {
        result.error = tr("识别异常: %1").arg(QString::fromStdString(e.what()));
        qWarning() << result.error;
        setState(OCREngineState::Ready);
    }

    return result;
}

OCRResult OCREngine::recognizeDetailed(const QImage& image)
{
    OCRResult result;

    if (m_state != OCREngineState::Ready) {
        result.error = tr("OCR引擎未就绪");
        return result;
    }

    if (image.isNull() || image.width() < 10 || image.height() < 10) {
        result.error = tr("输入图像无效");
        return result;
    }

    try {
        setState(OCREngineState::Processing);

        qDebug() << "OCREngine: Starting detailed recognition...";

        // 执行OCR识别
        RapidOCR::RapidOCROutput output = (*m_rapidOCR)(image);

        // 转换为详细结果
        result = convertToOCRResult(output);

        // 填充详细信息
        if (output.hasValidData()) {
            result.boxes = *output.boxes;
            result.texts = *output.txts;
            result.scores = *output.scores;
            result.elapsedTime = output.getElapse();
        }

        setState(OCREngineState::Ready);

        if (result.success) {
            qInfo() << "OCREngine: Detailed recognition completed";
            qInfo() << "  Detected" << result.texts.size() << "text regions";
            qInfo() << "  Total text:" << result.text;
            qInfo() << "  Elapsed time:" << result.elapsedTime << "seconds";
        } else {
            result.error = tr("未识别到文本");
        }

        emit recognitionCompleted(result);

    } catch (const std::exception& e) {
        result.error = tr("识别异常: %1").arg(QString::fromStdString(e.what()));
        qWarning() << result.error;
        setState(OCREngineState::Ready);
    }

    return result;
}

void OCREngine::setTextScore(float score)
{
    m_textScore = score;
    if (m_rapidOCR) {
        m_rapidOCR->updateParams(
            std::nullopt,
            std::nullopt,
            std::nullopt,
            m_returnWordBox,
            false,
            m_textScore
            );
    }
}

void OCREngine::setUseDet(bool use)
{
    m_useDet = use;
    if (m_rapidOCR) {
        m_rapidOCR->updateParams(
            use,
            std::nullopt,
            std::nullopt
            );
    }
}

void OCREngine::setUseCls(bool use)
{
    m_useCls = use;
    if (m_rapidOCR) {
        m_rapidOCR->updateParams(
            std::nullopt,
            use,
            std::nullopt
            );
    }
}

void OCREngine::setUseRec(bool use)
{
    m_useRec = use;
    if (m_rapidOCR) {
        m_rapidOCR->updateParams(
            std::nullopt,
            std::nullopt,
            use
            );
    }
}

void OCREngine::setReturnWordBox(bool enable)
{
    m_returnWordBox = enable;
    if (m_rapidOCR) {
        m_rapidOCR->updateParams(
            std::nullopt,
            std::nullopt,
            std::nullopt,
            enable
            );
    }
}

void OCREngine::setState(OCREngineState state)
{
    if (m_state != state) {
        m_state = state;
        emit stateChanged(state);
    }
}

void OCREngine::setError(const QString& error)
{
    m_lastError = error;
    qWarning() << "OCREngine error:" << error;
}

OCRResult OCREngine::convertToOCRResult(const RapidOCR::RapidOCROutput& output)
{
    OCRResult result;

    if (!output.hasValidData()) {
        result.success = false;
        return result;
    }

    // 合并所有文本
    QStringList textList;
    float totalScore = 0.0f;
    int validCount = 0;

    for (size_t i = 0; i < output.txts->size(); ++i) {
        QString txt = QString::fromStdString((*output.txts)[i]).trimmed();
        if (!txt.isEmpty()) {
            textList << txt;
            totalScore += (*output.scores)[i];
            validCount++;
        }
    }

    if (validCount > 0) {
        result.success = true;
        result.text = textList.join("\n");
        result.confidence = totalScore / validCount;
    } else {
        result.success = false;
    }

    return result;
}
