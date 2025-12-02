#include "ocrengine.h"
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QPainter>
#include <QTransform>
#include <QtConcurrent>

#include <onnxruntime_cxx_api.h>
#include <opencv2/opencv.hpp>

OCREngine::OCREngine(QObject* parent)
    : QObject(parent)
    , m_state(OCREngineState::Uninitialized)
{
}

OCREngine::~OCREngine()
{
    // ONNX会话自动释放
}

bool OCREngine::initializeAsync(const QString& modelDir)
{
    if (m_state == OCREngineState::Loading || m_state == OCREngineState::Ready) {
        qWarning() << "OCREngine: Already initialized or loading";
        return false;
    }

    setState(OCREngineState::Loading);
    m_modelDir = modelDir;

    // 在后台线程异步加载
    QtConcurrent::run([this, modelDir]() {
        bool success = initializeInternal(modelDir);

        // 切换回主线程发信号
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

    // 1. 检查模型文件
    QString detModel = modelDir + "/ch_PP-OCRv5_server_det.onnx";
    QString clsModel = modelDir + "/ch_ppocr_mobile_v2.0_cls_infer.onnx";
    QString recModel = modelDir + "/ch_PP-OCRv5_rec_server_infer.onnx";
    QString charsetFile = modelDir + "/ppocr_keys_v1.txt";

    QStringList missingFiles;
    if (!QFileInfo::exists(detModel)) missingFiles << "检测模型";
    if (!QFileInfo::exists(clsModel)) missingFiles << "分类模型";
    if (!QFileInfo::exists(recModel)) missingFiles << "识别模型";
    if (!QFileInfo::exists(charsetFile)) missingFiles << "字符集文件";

    if (!missingFiles.isEmpty()) {
        setError(tr("缺失文件: %1").arg(missingFiles.join(", ")));
        return false;
    }

    // 2. 加载字符集
    if (!loadCharacterSet(charsetFile)) {
        setError(tr("加载字符集失败"));
        return false;
    }

    try {
        // 3. 初始化ONNX Runtime环境
        m_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "OCREngine");

        // 4. 配置会话选项
        Ort::SessionOptions sessionOptions;
        sessionOptions.SetIntraOpNumThreads(4);  // 使用4个线程
        sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);

        // 5. 加载三个模型
        qInfo() << "OCREngine: Loading detection model...";
        std::wstring detPath = detModel.toStdWString();
        m_detSession = std::make_unique<Ort::Session>(*m_env, detPath.c_str(), sessionOptions);

        qInfo() << "OCREngine: Loading classification model...";
        std::wstring clsPath = clsModel.toStdWString();
        m_clsSession = std::make_unique<Ort::Session>(*m_env, clsPath.c_str(), sessionOptions);

        qInfo() << "OCREngine: Loading recognition model...";
        std::wstring recPath = recModel.toStdWString();
        m_recSession = std::make_unique<Ort::Session>(*m_env, recPath.c_str(), sessionOptions);

        qInfo() << "OCREngine: Initialization successful";
        qInfo() << "  Character set size:" << m_characterSet.size();
        return true;

    } catch (const Ort::Exception& e) {
        setError(tr("ONNX初始化失败: %1").arg(QString::fromStdString(e.what())));
        return false;
    } catch (const std::exception& e) {
        setError(tr("初始化异常: %1").arg(QString::fromStdString(e.what())));
        return false;
    }
}

bool OCREngine::loadCharacterSet(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);

    m_characterSet.clear();
    m_characterSet.append(" ");  // 索引0是CTC的blank

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (!line.isEmpty()) {
            m_characterSet.append(line);
        }
    }

    return m_characterSet.size() > 1;
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
        // 步骤1: 检测文本区域
        qDebug() << "OCREngine: Detecting text regions...";
        QVector<QVector<QPointF>> regions = detectTextRegions(image);

        if (regions.isEmpty()) {
            result.error = tr("未检测到文本");
            return result;
        }

        // 取置信度最高的第一个区域
        QVector<QPointF> firstRegion = regions.first();

        // 步骤2: 裁剪文本区域
        QImage croppedImage = cropTextRegion(image, firstRegion);
        if (croppedImage.isNull()) {
            result.error = tr("裁剪失败");
            return result;
        }

        // 步骤3: 方向分类
        qDebug() << "OCREngine: Classifying orientation...";
        int angle = classifyOrientation(croppedImage);

        if (angle != 0) {
            croppedImage = rotateImage(croppedImage, angle);
        }

        // 步骤4: 识别文字
        qDebug() << "OCREngine: Recognizing text...";
        auto [text, confidence] = recognizeText(croppedImage);

        // 填充结果
        result.text = text;
        result.confidence = confidence;
        result.success = !text.isEmpty();

        qInfo() << "OCREngine: Recognition completed";
        qInfo() << "  Text:" << text;
        qInfo() << "  Confidence:" << confidence;

    } catch (const Ort::Exception& e) {
        result.error = tr("推理错误: %1").arg(QString::fromStdString(e.what()));
        qWarning() << result.error;
    } catch (const std::exception& e) {
        result.error = tr("异常: %1").arg(QString::fromStdString(e.what()));
        qWarning() << result.error;
    }

    return result;
}


QVector<QVector<QPointF>> OCREngine::detectTextRegions(const QImage& image)
{
    /*
     * 检测流程：
     * 1. 图像预处理：调整大小、归一化
     * 2. ONNX推理：DBNet模型输出概率图
     * 3. 后处理：二值化、轮廓检测、过滤
     */

    // 预处理
    int resizedW, resizedH;
    std::vector<float> inputData = preprocessForDet(image, resizedW, resizedH);

    // 构建输入张量
    std::vector<int64_t> inputShape = {1, 3, resizedH, resizedW};
    auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);
    Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
        memoryInfo, inputData.data(), inputData.size(),
        inputShape.data(), inputShape.size()
        );

    // ONNX推理
    const char* inputNames[] = {"x"};
    const char* outputNames[] = {"sigmoid_0.tmp_0"};  // DBNet输出名

    auto outputTensors = m_detSession->Run(
        Ort::RunOptions{nullptr},
        inputNames, &inputTensor, 1,
        outputNames, 1
        );

    // 后处理
    return postprocessDet(outputTensors[0],
                          image.width(), image.height(),
                          resizedW, resizedH);
}

std::vector<float> OCREngine::preprocessForDet(const QImage& image, int& outW, int& outH)
{
    /*
     * DBNet预处理要点：
     * 1. 保持宽高比
     * 2. 长边缩放到目标尺寸（960）
     * 3. 宽高都调整为32的倍数（DBNet要求）
     * 4. RGB归一化：(pixel/255 - mean) / std
     */

    int oriW = image.width();
    int oriH = image.height();

    // 计算缩放比例
    float ratio = (float)DET_TARGET_SIZE / qMax(oriW, oriH);

    // 调整为32的倍数
    outW = qRound(oriW * ratio / 32) * 32;
    outH = qRound(oriH * ratio / 32) * 32;

    // 缩放图像
    QImage scaled = image.scaled(outW, outH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    scaled = scaled.convertToFormat(QImage::Format_RGB888);

    // 归一化到CHW格式
    std::vector<float> data(3 * outH * outW);
    const float mean[3] = {0.485f, 0.456f, 0.406f};  // ImageNet标准
    const float std[3] = {0.229f, 0.224f, 0.225f};

    for (int c = 0; c < 3; ++c) {
        for (int h = 0; h < outH; ++h) {
            for (int w = 0; w < outW; ++w) {
                QRgb pixel = scaled.pixel(w, h);
                float value;

                if (c == 0) value = qRed(pixel) / 255.0f;
                else if (c == 1) value = qGreen(pixel) / 255.0f;
                else value = qBlue(pixel) / 255.0f;

                value = (value - mean[c]) / std[c];

                int index = c * outH * outW + h * outW + w;
                data[index] = value;
            }
        }
    }

    return data;
}

QVector<QVector<QPointF>> OCREngine::postprocessDet(Ort::Value& tensor,
                                                    int oriW, int oriH,
                                                    int resizedW, int resizedH)
{
    /*
     * DBNet后处理步骤：
     * 1. 获取输出概率图（shape: [1, 1, H, W]）
     * 2. 二值化：概率 > 阈值的像素标记为文本
     * 3. 使用OpenCV查找轮廓
     * 4. 计算最小外接矩形
     * 5. 过滤太小的框
     * 6. 坐标转换回原图尺寸并归一化
     */

    QVector<QVector<QPointF>> boxes;

    // 1. 获取输出数据
    auto shape = tensor.GetTensorTypeAndShapeInfo().GetShape();
    float* outputData = tensor.GetTensorMutableData<float>();

    int outputH = shape[2];
    int outputW = shape[3];

    // 2. 二值化
    cv::Mat binaryMat(outputH, outputW, CV_8U);
    for (int y = 0; y < outputH; ++y) {
        for (int x = 0; x < outputW; ++x) {
            int idx = y * outputW + x;
            binaryMat.at<uchar>(y, x) = (outputData[idx] > DET_THRESHOLD) ? 255 : 0;
        }
    }

    // 3. 查找轮廓
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binaryMat, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

    // 4. 处理每个轮廓
    float ratioW = (float)oriW / resizedW;
    float ratioH = (float)oriH / resizedH;

    for (const auto& contour : contours) {
        if (contour.size() < 4) continue;

        // 计算面积，过滤小框
        double area = cv::contourArea(contour);
        if (area < 10) continue;

        // 最小外接矩形
        cv::RotatedRect rect = cv::minAreaRect(contour);

        // 计算置信度（简化）
        float score = area / (rect.size.width * rect.size.height + 1e-5);
        if (score < DET_BOX_THRESHOLD) continue;

        // 获取四个顶点
        cv::Point2f vertices[4];
        rect.points(vertices);

        // 转换为归一化坐标
        QVector<QPointF> box;
        for (int i = 0; i < 4; ++i) {
            float x = (vertices[i].x * ratioW) / oriW;
            float y = (vertices[i].y * ratioH) / oriH;
            box.append(QPointF(x, y));
        }

        boxes.append(box);
    }

    qDebug() << "OCREngine: Detected" << boxes.size() << "text regions";
    return boxes;
}


int OCREngine::classifyOrientation(const QImage& image)
{
    /*
     * 分类流程：
     * 1. 预处理：缩放到192x48
     * 2. ONNX推理：输出[0度, 180度]的概率
     * 3. 后处理：选择概率最高的角度
     */

    // 预处理
    std::vector<float> inputData = preprocessForCls(image);

    // 构建输入张量
    std::vector<int64_t> inputShape = {1, 3, CLS_IMAGE_HEIGHT, CLS_IMAGE_WIDTH};
    auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);
    Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
        memoryInfo, inputData.data(), inputData.size(),
        inputShape.data(), inputShape.size()
        );

    // ONNX推理
    const char* inputNames[] = {"x"};
    const char* outputNames[] = {"softmax_0.tmp_0"};

    auto outputTensors = m_clsSession->Run(
        Ort::RunOptions{nullptr},
        inputNames, &inputTensor, 1,
        outputNames, 1
        );

    // 后处理
    return postprocessCls(outputTensors[0]);
}

std::vector<float> OCREngine::preprocessForCls(const QImage& image)
{
    // 固定缩放到192x48
    QImage scaled = image.scaled(CLS_IMAGE_WIDTH, CLS_IMAGE_HEIGHT,
                                 Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    scaled = scaled.convertToFormat(QImage::Format_RGB888);

    // 归一化
    std::vector<float> data(3 * CLS_IMAGE_HEIGHT * CLS_IMAGE_WIDTH);
    const float mean[3] = {0.5f, 0.5f, 0.5f};
    const float std[3] = {0.5f, 0.5f, 0.5f};

    for (int c = 0; c < 3; ++c) {
        for (int h = 0; h < CLS_IMAGE_HEIGHT; ++h) {
            for (int w = 0; w < CLS_IMAGE_WIDTH; ++w) {
                QRgb pixel = scaled.pixel(w, h);
                float value;

                if (c == 0) value = qRed(pixel) / 255.0f;
                else if (c == 1) value = qGreen(pixel) / 255.0f;
                else value = qBlue(pixel) / 255.0f;

                value = (value - mean[c]) / std[c];

                int index = c * CLS_IMAGE_HEIGHT * CLS_IMAGE_WIDTH +
                            h * CLS_IMAGE_WIDTH + w;
                data[index] = value;
            }
        }
    }

    return data;
}

int OCREngine::postprocessCls(Ort::Value& tensor)
{
    // 输出shape: [1, 2]，分别是0度和180度的概率
    float* outputData = tensor.GetTensorMutableData<float>();

    float prob0 = outputData[0];
    float prob180 = outputData[1];

    // 如果180度概率更高且超过阈值，返回180
    if (prob180 > prob0 && prob180 > CLS_THRESHOLD) {
        qDebug() << "OCREngine: Orientation 180°, confidence:" << prob180;
        return 180;
    }

    qDebug() << "OCREngine: Orientation 0°, confidence:" << prob0;
    return 0;
}



QPair<QString, float> OCREngine::recognizeText(const QImage& image)
{
    /*
     * 识别流程：
     * 1. 预处理：高度固定48，宽度自适应
     * 2. ONNX推理：输出每个时间步的字符概率
     * 3. CTC解码：去除blank和重复，映射到字符
     */

    // 预处理
    int resizedW;
    std::vector<float> inputData = preprocessForRec(image, resizedW);

    // 构建输入张量
    std::vector<int64_t> inputShape = {1, 3, REC_IMAGE_HEIGHT, resizedW};
    auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);
    Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
        memoryInfo, inputData.data(), inputData.size(),
        inputShape.data(), inputShape.size()
        );

    // ONNX推理
    const char* inputNames[] = {"x"};
    const char* outputNames[] = {"softmax_0.tmp_0"};

    auto outputTensors = m_recSession->Run(
        Ort::RunOptions{nullptr},
        inputNames, &inputTensor, 1,
        outputNames, 1
        );

    // 后处理
    return postprocessRec(outputTensors[0]);
}

std::vector<float> OCREngine::preprocessForRec(const QImage& image, int& outW)
{
    /*
     * Rec预处理要点：
     * 1. 高度固定48
     * 2. 宽度按比例缩放
     * 3. 限制最大宽度避免内存溢出
     */

    int oriW = image.width();
    int oriH = image.height();

    float ratio = (float)REC_IMAGE_HEIGHT / oriH;
    outW = qRound(oriW * ratio);

    // 限制最大宽度
    const int MAX_WIDTH = 2000;
    if (outW > MAX_WIDTH) {
        outW = MAX_WIDTH;
    }

    if (outW < 10) outW = 10;  // 最小宽度

    QImage scaled = image.scaled(outW, REC_IMAGE_HEIGHT,
                                 Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    scaled = scaled.convertToFormat(QImage::Format_RGB888);

    // 归一化
    std::vector<float> data(3 * REC_IMAGE_HEIGHT * outW);
    const float mean[3] = {0.5f, 0.5f, 0.5f};
    const float std[3] = {0.5f, 0.5f, 0.5f};

    for (int c = 0; c < 3; ++c) {
        for (int h = 0; h < REC_IMAGE_HEIGHT; ++h) {
            for (int w = 0; w < outW; ++w) {
                QRgb pixel = scaled.pixel(w, h);
                float value;

                if (c == 0) value = qRed(pixel) / 255.0f;
                else if (c == 1) value = qGreen(pixel) / 255.0f;
                else value = qBlue(pixel) / 255.0f;

                value = (value - mean[c]) / std[c];

                int index = c * REC_IMAGE_HEIGHT * outW + h * outW + w;
                data[index] = value;
            }
        }
    }

    return data;
}

QPair<QString, float> OCREngine::postprocessRec(Ort::Value& tensor)
{
    /*
     * CTC解码原理：
     * 1. 模型输出: [batch, time_steps, num_classes]
     * 2. 每个time_step取概率最大的字符索引
     * 3. 去除blank(索引0)和连续重复的字符
     * 4. 通过字符集映射得到最终文本
     *
     * 例子：
     * 输入索引: [0, 5, 5, 0, 10, 10, 0, 3, 0]
     * 处理后:   [   5,      10,       3   ]
     * 映射文字: [   '你',   '好',     '吗' ]
     * 结果: "你好吗"
     */

    // 获取输出shape
    auto shape = tensor.GetTensorTypeAndShapeInfo().GetShape();
    float* outputData = tensor.GetTensorMutableData<float>();

    int timeSteps = shape[1];
    int numClasses = shape[2];

    // 1. 取每个时间步的最大概率索引
    std::vector<int> indices;
    std::vector<float> probs;

    for (int t = 0; t < timeSteps; ++t) {
        float maxProb = -1.0f;
        int maxIndex = 0;

        for (int c = 0; c < numClasses; ++c) {
            float prob = outputData[t * numClasses + c];
            if (prob > maxProb) {
                maxProb = prob;
                maxIndex = c;
            }
        }

        indices.push_back(maxIndex);
        probs.push_back(maxProb);
    }

    // 2. CTC解码
    QString text = ctcDecode(indices);

    // 3. 计算平均置信度
    float avgConf = 0.0f;
    if (!probs.empty()) {
        float sum = 0.0f;
        for (float p : probs) sum += p;
        avgConf = sum / probs.size();
    }

    return {text, avgConf};
}

QString OCREngine::ctcDecode(const std::vector<int>& indices)
{
    QString result;
    int lastIndex = -1;

    for (int idx : indices) {
        // 跳过blank（索引0）
        if (idx == 0) {
            lastIndex = idx;
            continue;
        }

        // 跳过连续重复的字符
        if (idx == lastIndex) {
            continue;
        }

        // 映射到字符
        if (idx > 0 && idx < m_characterSet.size()) {
            result.append(m_characterSet[idx]);
        }

        lastIndex = idx;
    }

    return result;
}


QImage OCREngine::cropTextRegion(const QImage& image, const QVector<QPointF>& points)
{
    if (points.size() != 4) {
        return image;
    }

    // 归一化坐标转图像坐标
    QVector<QPointF> imagePoints;
    for (const QPointF& pt : points) {
        imagePoints.append(QPointF(pt.x() * image.width(), pt.y() * image.height()));
    }

    // 计算最小外接矩形
    qreal minX = imagePoints[0].x(), maxX = imagePoints[0].x();
    qreal minY = imagePoints[0].y(), maxY = imagePoints[0].y();

    for (const QPointF& pt : imagePoints) {
        minX = qMin(minX, pt.x());
        maxX = qMax(maxX, pt.x());
        minY = qMin(minY, pt.y());
        maxY = qMax(maxY, pt.y());
    }

    QRect cropRect(qRound(minX), qRound(minY),
                   qRound(maxX - minX), qRound(maxY - minY));

    cropRect = cropRect.intersected(image.rect());

    if (cropRect.isEmpty()) {
        return image;
    }

    return image.copy(cropRect);
}

QImage OCREngine::rotateImage(const QImage& image, int angle)
{
    if (angle == 0) return image;

    QTransform transform;
    transform.rotate(angle);
    return image.transformed(transform, Qt::SmoothTransformation);
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
