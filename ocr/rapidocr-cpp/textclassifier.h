#ifndef RAPIDOCR_TEXT_CLASSIFIER_H
#define RAPIDOCR_TEXT_CLASSIFIER_H

#include "ortinfersession.h"
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>

namespace RapidOCR {

// 分类结果输出结构
struct TextClsOutput {
    std::vector<cv::Mat> imgList;                          // 处理后的图像列表（可能旋转）
    std::vector<std::pair<std::string, float>> clsRes;     // 分类结果：(标签, 置信度)
    double elapse = 0.0;                                   // 耗时（秒）

    size_t size() const {
        return imgList.size();
    }

    bool empty() const {
        return imgList.empty();
    }
};

// 分类器配置
struct ClassifierConfig {
    std::string modelPath;                                 // 模型路径
    std::vector<int> clsImageShape = {3, 48, 192};        // 输入图像形状 [C, H, W]
    int clsBatchNum = 6;                                   // 批处理大小
    float clsThresh = 0.9f;                                // 旋转阈值
    std::vector<std::string> labelList = {"0", "180"};    // 标签列表

    // OnnxRuntime 配置
    int numThreads = 0;
    bool useGpu = false;
    int gpuDeviceId = 0;
};

class TextClassifier {
public:
    TextClassifier(const ClassifierConfig& config, OrtInferSession* session);
    ~TextClassifier() = default;

    // 执行文本方向分类
    // 输入可以是单张图像或图像列表
    TextClsOutput operator()(const std::vector<cv::Mat>& imgList);
    TextClsOutput operator()(const cv::Mat& img);

private:
    // 调整图像大小并归一化
    cv::Mat resizeNormImg(const cv::Mat& img);

    // 后处理：从预测结果中提取标签和置信度
    std::vector<std::pair<std::string, float>> postprocess(const cv::Mat& preds);

private:
    ClassifierConfig config_;
    OrtInferSession* session_;
};

} // namespace RapidOCR

#endif // RAPIDOCR_TEXT_CLASSIFIER_H
