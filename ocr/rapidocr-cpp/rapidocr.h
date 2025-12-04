#ifndef RAPIDOCR_RAPIDOCR_H
#define RAPIDOCR_RAPIDOCR_H

#include "textdetector.h"
#include "textclassifier.h"
#include "textrecognizer.h"
#include "calrecboxes.h"
#include "loadimage.h"
#include "processimage.h"
#include "ocroutput.h"
#include <QImage>
#include <QString>
#include <memory>
#include <optional>

namespace RapidOCR {

// RapidOCR配置
struct RapidOCRConfig {
    // 全局配置
    float textScore = 0.5f;
    bool useDet = true;
    bool useCls = true;
    bool useRec = true;

    float minHeight = 30.0f;
    float widthHeightRatio = 8.0f;
    float maxSideLen = 2000.0f;
    float minSideLen = 30.0f;

    bool returnWordBox = false;
    bool returnSingleCharBox = false;

    QString modelDir;  // 模型目录

    // 检测器配置
    DetectorConfig detConfig;

    // 分类器配置
    ClassifierConfig clsConfig;

    // 识别器配置
    RecognizerConfig recConfig;
};

// RapidOCR 主类
class RapidOCR {
public:
    RapidOCR();
    explicit RapidOCR(const RapidOCRConfig& config);
    ~RapidOCR();

    // 初始化
    bool initialize(const QString& modelDir);
    bool isInitialized() const { return initialized_; }

    // 执行OCR识别
    RapidOCROutput operator()(const QImage& img);
    RapidOCROutput operator()(const cv::Mat& img);
    RapidOCROutput operator()(const std::string& imgPath);

    // 更新参数
    void updateParams(
        std::optional<bool> useDet = std::nullopt,
        std::optional<bool> useCls = std::nullopt,
        std::optional<bool> useRec = std::nullopt,
        bool returnWordBox = false,
        bool returnSingleCharBox = false,
        float textScore = 0.5f,
        float boxThresh = 0.5f,
        float unclipRatio = 1.6f
        );

    // 获取错误信息
    QString getLastError() const { return lastError_; }

private:
    // 初始化内部实现
    bool initializeInternal(const QString& modelDir);

    // 预处理图像
    std::tuple<cv::Mat, OpRecord> preprocessImg(const cv::Mat& oriImg);

    // 运行OCR步骤
    std::tuple<TextDetOutput, TextClsOutput, TextRecOutput, std::vector<cv::Mat>>
    runOcrSteps(const cv::Mat& img, const OpRecord& opRecord);

    // 检测并裁剪
    std::tuple<std::vector<cv::Mat>, TextDetOutput>
    detectAndCrop(const cv::Mat& img, OpRecord& opRecord);

    // 裁剪文本区域
    std::vector<cv::Mat> cropTextRegions(const cv::Mat& img,
                                         const std::vector<std::vector<cv::Point>>& boxes);

    // 分类和旋转
    std::tuple<std::vector<cv::Mat>, TextClsOutput>
    clsAndRotate(const std::vector<cv::Mat>& imgList);

    // 识别文本
    TextRecOutput recognizeText(const std::vector<cv::Mat>& imgList);

    // 构建最终输出
    RapidOCROutput buildFinalOutput(
        const cv::Mat& oriImg,
        TextDetOutput& detRes,
        TextClsOutput& clsRes,
        TextRecOutput& recRes,
        const std::vector<cv::Mat>& croppedImgList,
        const OpRecord& opRecord
        );

    // 计算词语框
    std::vector<WordInfo> calcWordBoxes(
        const std::vector<cv::Mat>& imgs,
        const std::vector<std::vector<cv::Point>>& dtBoxes,
        TextRecOutput& recRes,
        const OpRecord& opRecord,
        int rawH,
        int rawW
        );

    // 按文本置信度过滤
    RapidOCROutput filterByTextScore(RapidOCROutput& ocrRes);

    // 设置错误信息
    void setError(const QString& error);

private:
    RapidOCRConfig config_;

    // 推理会话
    std::unique_ptr<OrtInferSession> detSession_;
    std::unique_ptr<OrtInferSession> clsSession_;
    std::unique_ptr<OrtInferSession> recSession_;

    // 检测器、分类器、识别器
    std::unique_ptr<TextDetector> textDet_;
    std::unique_ptr<TextClassifier> textCls_;
    std::unique_ptr<TextRecognizer> textRec_;

    // 辅助工具
    LoadImage loadImg_;
    CalRecBoxes calRecBoxes_;

    bool initialized_ = false;
    QString lastError_;
};

// 异常类
class RapidOCRException : public std::runtime_error {
public:
    explicit RapidOCRException(const std::string& message)
        : std::runtime_error(message) {}
};

} // namespace RapidOCR

#endif // RAPIDOCR_RAPIDOCR_H
