#ifndef RAPIDOCR_LOAD_IMAGE_H
#define RAPIDOCR_LOAD_IMAGE_H

#include <string>
#include <opencv2/opencv.hpp>
#include <QImage>
#include <stdexcept>

namespace RapidOCR {

class LoadImageError : public std::runtime_error {
public:
    explicit LoadImageError(const std::string& msg) : std::runtime_error(msg) {}
};

class LoadImage {
public:
    LoadImage() = default;

    // 主入口：支持从QImage、文件路径、URL、cv::Mat加载
    cv::Mat operator()(const QImage& img);
    cv::Mat operator()(const std::string& path);
    cv::Mat operator()(const cv::Mat& img);

private:
    // 从QImage转换为cv::Mat
    cv::Mat qImageToMat(const QImage& qimg);

    // 从文件或URL加载图像
    cv::Mat loadFromPath(const std::string& path);

    // 处理EXIF方向信息
    QImage exifTranspose(const QImage& img);

    // 转换图像通道数为3通道BGR
    cv::Mat convertImg(const cv::Mat& img, bool isFromQImage);

    // 2通道转3通道 (gray + alpha -> BGR)
    cv::Mat cvtTwoToThree(const cv::Mat& img);

    // 4通道转3通道 (RGBA -> BGR)
    cv::Mat cvtFourToThree(const cv::Mat& img);

    // 验证文件是否存在
    void verifyExist(const std::string& filePath);
};

} // namespace RapidOCR

#endif // RAPIDOCR_LOAD_IMAGE_H
