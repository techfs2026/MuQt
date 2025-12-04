#ifndef RAPIDOCR_TEXT_DETECTOR_H
#define RAPIDOCR_TEXT_DETECTOR_H

#include "ortinfersession.h"
#include <opencv2/opencv.hpp>
#include <vector>

namespace RapidOCR {

// 检测结果输出结构
struct TextDetOutput {
    cv::Mat img;                           // 原始图像
    std::vector<std::vector<cv::Point>> boxes;  // 检测到的文本框
    std::vector<float> scores;             // 每个文本框的置信度分数
    double elapse = 0.0;                   // 耗时（秒）

    // 获取检测到的文本框数量
    size_t size() const {
        return boxes.size();
    }

    bool empty() const {
        return boxes.empty();
    }
};

// 检测器配置
struct DetectorConfig {
    std::string modelPath;                 // 模型路径
    int limitSideLen = 960;                // 限制边长
    std::string limitType = "max";         // 限制类型: "max" 或 "min"
    std::vector<float> mean = {0.485f, 0.456f, 0.406f};  // 归一化均值
    std::vector<float> std = {0.229f, 0.224f, 0.225f};   // 归一化标准差
    float thresh = 0.3f;                   // 二值化阈值
    float boxThresh = 0.5f;                // 文本框置信度阈值
    int maxCandidates = 1000;              // 最大候选框数量
    float unclipRatio = 1.6f;              // 扩展比例
    bool useDilation = true;               // 是否使用膨胀
    std::string scoreMode = "fast";        // 评分模式: "fast" 或 "slow"

    // OnnxRuntime 配置
    int numThreads = 0;
    bool useGpu = false;
    int gpuDeviceId = 0;
};

class TextDetector {
public:
    // 构造函数：接收配置和已创建的推理会话指针
    TextDetector(const DetectorConfig& config, OrtInferSession* session);
    ~TextDetector() = default;

    // 执行文本检测
    TextDetOutput operator()(const cv::Mat& img);

private:
    // 预处理
    cv::Mat preprocess(const cv::Mat& img, cv::Size& outOriSize);

    // 后处理
    void postprocess(const cv::Mat& pred,
                     const cv::Size& oriSize,
                     std::vector<std::vector<cv::Point>>& boxes,
                     std::vector<float>& scores);

    // 从二值图中提取文本框
    void boxesFromBitmap(const cv::Mat& pred,
                         const cv::Mat& bitmap,
                         int destWidth,
                         int destHeight,
                         std::vector<std::vector<cv::Point>>& boxes,
                         std::vector<float>& scores);

    // 获取最小外接矩形
    std::vector<cv::Point2f> getMiniBoxes(const std::vector<cv::Point>& contour, float& minSideLen);

    // 计算文本框得分（快速方法）
    float boxScoreFast(const cv::Mat& bitmap, const std::vector<cv::Point2f>& box);

    // 计算文本框得分（慢速方法）
    float boxScoreSlow(const cv::Mat& bitmap, const std::vector<cv::Point>& contour);

    // 扩展文本框
    std::vector<cv::Point2f> unclip(const std::vector<cv::Point2f>& box);

    // 过滤检测结果
    void filterDetRes(std::vector<std::vector<cv::Point>>& boxes,
                      std::vector<float>& scores,
                      int imgHeight,
                      int imgWidth);

    // 按顺时针方向排序点
    std::vector<cv::Point> orderPointsClockwise(const std::vector<cv::Point2f>& pts);

    // 裁剪检测结果
    void clipDetRes(std::vector<cv::Point>& points, int imgHeight, int imgWidth);

    // 对文本框排序（从上到下，从左到右）
    static std::vector<std::vector<cv::Point>> sortedBoxes(std::vector<std::vector<cv::Point>>& dtBoxes);

    // 根据图像尺寸动态调整limit_side_len
    int getAdaptiveLimitSideLen(int maxWH);

private:
    DetectorConfig config_;
    OrtInferSession* session_;  // 使用指针，不拥有所有权
    cv::Mat dilationKernel_;
};

} // namespace RapidOCR

#endif // RAPIDOCR_TEXT_DETECTOR_H
