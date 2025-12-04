#ifndef RAPIDOCR_CALC_REC_BOXES_H
#define RAPIDOCR_CALC_REC_BOXES_H

#include "textrecognizer.h"
#include <opencv2/opencv.hpp>
#include <vector>

namespace RapidOCR {

// 文本框方向枚举
enum class Direction {
    HORIZONTAL,  // 水平
    VERTICAL     // 垂直
};

// 计算识别框的类
// 用于计算识别文字的汉字单字和英文单词的坐标框
class CalRecBoxes {
public:
    CalRecBoxes() = default;
    ~CalRecBoxes() = default;

    // 计算词语级别的边界框
    // imgs: 裁剪后的文本区域图像列表
    // dtBoxes: 检测框坐标
    // recRes: 识别结果
    // returnSingleCharBox: 是否返回单字符框（对于英文）
    TextRecOutput operator()(const std::vector<cv::Mat>& imgs,
                             const std::vector<std::vector<cv::Point>>& dtBoxes,
                             TextRecOutput& recRes,
                             bool returnSingleCharBox = false);

private:
    // 获取文本框的方向
    static Direction getBoxDirection(const std::vector<cv::Point>& box);

    // 计算OCR词语框
    std::tuple<std::vector<std::string>, std::vector<std::vector<cv::Point>>, std::vector<float>>
    calOcrWordBox(const std::string& recTxt,
                  const std::vector<cv::Point>& bbox,
                  const WordInfo& wordInfo,
                  bool returnSingleCharBox);

    // 计算英文数字框
    std::vector<std::vector<cv::Point>> calcEnNumBox(
        const std::vector<std::vector<int>>& lineCols,
        float avgCharWidth,
        float avgColWidth,
        const std::tuple<float, float, float, float>& bboxPoints);

    // 计算框
    static std::vector<std::vector<cv::Point>> calcBox(
        const std::vector<int>& lineCols,
        float avgCharWidth,
        float avgColWidth,
        const std::tuple<float, float, float, float>& bboxPoints);

    // 计算平均字符宽度
    static float calcAvgCharWidth(const std::vector<int>& wordCol, float eachColWidth);

    // 计算所有字符的平均宽度
    static float calcAllCharAvgWidth(const std::vector<float>& widthList,
                                     float bboxX0,
                                     float bboxX1,
                                     int txtLen);

    // 调整框重叠
    static std::vector<std::vector<cv::Point>> adjustBoxOverlap(
        std::vector<std::vector<cv::Point>>& wordBoxList);

    // 反向旋转裁剪图像
    std::vector<std::vector<cv::Point>> reverseRotateCropImage(
        const std::vector<cv::Point>& bboxPoints,
        const std::vector<std::vector<cv::Point>>& wordPointsList,
        Direction direction);

    // 旋转点
    static std::pair<float, float> sRotate(float angle,
                                           float valuex,
                                           float valuey,
                                           float pointx,
                                           float pointy);

    // 排序点
    static std::vector<cv::Point> orderPoints(const std::vector<cv::Point>& oriBox);
};

} // namespace RapidOCR

#endif // RAPIDOCR_CALC_REC_BOXES_H
