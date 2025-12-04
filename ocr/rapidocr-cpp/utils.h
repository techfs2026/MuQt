#ifndef RAPIDOCR_UTILS_H
#define RAPIDOCR_UTILS_H

#include <string>
#include <tuple>
#include <opencv2/opencv.hpp>

namespace RapidOCR {

class Utils {
public:
    // 创建目录
    static void mkdir(const std::string& dirPath);

    // 将四边形转换为矩形边界框 (x_min, y_min, x_max, y_max)
    static std::tuple<float, float, float, float> quadsToRectBBox(const cv::Mat& bbox);

    // 检查文本是否包含中文字符
    static bool hasChineseChar(const std::string& text);

    // 计算文件的SHA256
    static std::string getFileSHA256(const std::string& filePath, int chunkSize = 65536);

    // 保存图像
    static void saveImage(const std::string& savePath, const cv::Mat& img);

    // 检查是否为URL
    static bool isUrl(const std::string& url);
};

} // namespace RapidOCR

#endif // RAPIDOCR_UTILS_H
