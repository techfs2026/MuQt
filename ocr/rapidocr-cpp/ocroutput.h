#ifndef RAPIDOCR_OCR_OUTPUT_H
#define RAPIDOCR_OCR_OUTPUT_H

#include <vector>
#include <string>
#include <tuple>
#include <optional>
#include <opencv2/opencv.hpp>
#include <QJsonArray>
#include <QJsonObject>

namespace RapidOCR {

// 单词结果：(文本, 置信度, 边界框)
using WordResult = std::tuple<std::string, float, std::optional<std::vector<std::vector<int>>>>;

// OCR输出结构
struct RapidOCROutput {
    std::optional<cv::Mat> img;
    std::optional<std::vector<std::vector<cv::Point2f>>> boxes;
    std::optional<std::vector<std::string>> txts;
    std::optional<std::vector<float>> scores;
    std::vector<WordResult> wordResults;
    std::vector<double> elapseList;

    // 构造函数
    RapidOCROutput() = default;

    // 计算总耗时
    double getElapse() const;

    // 获取识别结果数量
    size_t size() const;

    // 转换为JSON格式
    std::optional<QJsonArray> toJson() const;

    // 转换为Markdown格式
    std::string toMarkdown() const;

public:
    // 辅助函数：检查数据是否有效
    bool hasValidData() const;
};

// JSON转换工具类
class ToJSON {
public:
    static std::optional<QJsonArray> to(
        const std::vector<std::vector<cv::Point2f>>& boxes,
        const std::vector<std::string>& txts,
        const std::vector<float>& scores
        );
};

// Markdown转换工具类
class ToMarkdown {
public:
    static std::string to(
        const std::optional<std::vector<std::vector<cv::Point2f>>>& boxes,
        const std::optional<std::vector<std::string>>& txts
        );
};

} // namespace RapidOCR

#endif // RAPIDOCR_OCR_OUTPUT_H
