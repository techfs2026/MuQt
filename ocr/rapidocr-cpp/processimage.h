#ifndef RAPIDOCR_PROCESS_IMAGE_H
#define RAPIDOCR_PROCESS_IMAGE_H

#include <opencv2/opencv.hpp>
#include <map>
#include <string>
#include <any>
#include <tuple>
#include <stdexcept>

namespace RapidOCR {

class ResizeImgError : public std::runtime_error {
public:
    explicit ResizeImgError(const std::string& msg = "")
        : std::runtime_error(msg.empty() ? "Resize image error" : msg) {}
};

// 操作记录类型
using OpRecord = std::map<std::string, std::map<std::string, std::any>>;

class ProcessImage {
public:
    // 将检测框映射回原始图像坐标
    static cv::Mat mapBoxesToOriginal(
        const cv::Mat& dtBoxes,
        const OpRecord& opRecord,
        int oriH,
        int oriW
        );

    // 应用垂直padding
    static std::tuple<cv::Mat, OpRecord> applyVerticalPadding(
        const cv::Mat& img,
        OpRecord opRecord,
        float widthHeightRatio,
        float minHeight
        );

    // 计算padding高度
    static int getPaddingH(int h, int w, float widthHeightRatio, float minHeight);

    // 获取旋转裁剪的图像
    static cv::Mat getRotateCropImage(const cv::Mat& img, const cv::Mat& points);

    // 在边界内调整图像大小
    static std::tuple<cv::Mat, float, float> resizeImageWithinBounds(
        const cv::Mat& img,
        float minSideLen,
        float maxSideLen
        );

    // 缩小最大边
    static std::tuple<cv::Mat, float, float> reduceMaxSide(
        const cv::Mat& img,
        float maxSideLen = 2000.0f
        );

    // 增大最小边
    static std::tuple<cv::Mat, float, float> increaseMinSide(
        const cv::Mat& img,
        float minSideLen = 30.0f
        );

    // 添加圆角letterbox（padding）
    static cv::Mat addRoundLetterbox(
        const cv::Mat& img,
        const std::tuple<int, int, int, int>& paddingTuple
        );
};

} // namespace RapidOCR

#endif // RAPIDOCR_PROCESS_IMAGE_H
