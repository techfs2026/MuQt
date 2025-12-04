#include "loadimage.h"
#include "utils.h"
#include <QFile>
#include <QImageReader>
#include <QEventLoop>
#include <filesystem>

namespace RapidOCR {

cv::Mat LoadImage::operator()(const QImage& img) {
    if (img.isNull()) {
        throw LoadImageError("Input QImage is null");
    }

    cv::Mat mat = qImageToMat(img);
    return convertImg(mat, true);
}

cv::Mat LoadImage::operator()(const std::string& path) {
    cv::Mat mat = loadFromPath(path);
    return convertImg(mat, true);
}

cv::Mat LoadImage::operator()(const cv::Mat& img) {
    if (img.empty()) {
        throw LoadImageError("Input cv::Mat is empty");
    }
    return convertImg(img, false);
}

cv::Mat LoadImage::qImageToMat(const QImage& qimg) {
    QImage image = qimg;

    // 处理EXIF方向
    image = exifTranspose(image);

    // 转换格式
    if (image.format() == QImage::Format_Mono || image.format() == QImage::Format_MonoLSB) {
        image = image.convertToFormat(QImage::Format_Grayscale8);
    } else if (image.format() != QImage::Format_RGB888 &&
               image.format() != QImage::Format_RGBA8888 &&
               image.format() != QImage::Format_Grayscale8) {
        image = image.convertToFormat(QImage::Format_RGB888);
    }

    int height = image.height();
    int width = image.width();

    cv::Mat mat;

    if (image.format() == QImage::Format_Grayscale8) {
        mat = cv::Mat(height, width, CV_8UC1, (void*)image.bits(), image.bytesPerLine()).clone();
    } else if (image.format() == QImage::Format_RGB888) {
        mat = cv::Mat(height, width, CV_8UC3, (void*)image.bits(), image.bytesPerLine()).clone();
    } else if (image.format() == QImage::Format_RGBA8888) {
        mat = cv::Mat(height, width, CV_8UC4, (void*)image.bits(), image.bytesPerLine()).clone();
    }

    return mat;
}

cv::Mat LoadImage::loadFromPath(const std::string& path) {
    // 检查是否为URL
    if (Utils::isUrl(path)) {
        // TODO: 实现从URL下载图像
        // 需要网络请求，这里简化处理
        throw LoadImageError("URL loading not implemented yet");
    }

    // 验证文件存在
    verifyExist(path);

    // 使用QImageReader加载，支持EXIF
    QImageReader reader(QString::fromStdString(path));
    reader.setAutoTransform(true);  // 自动处理EXIF
    QImage qimg = reader.read();

    if (qimg.isNull()) {
        throw LoadImageError("Cannot identify image file: " + path);
    }

    return qImageToMat(qimg);
}

QImage LoadImage::exifTranspose(const QImage& img) {
    // QImageReader with setAutoTransform已经处理了EXIF
    // 这里保留接口以便需要时手动处理
    return img;
}

cv::Mat LoadImage::convertImg(const cv::Mat& img, bool isFromQImage) {
    if (img.empty()) {
        throw LoadImageError("Input image is empty");
    }

    int channels = img.channels();

    // 2维灰度图
    if (img.dims == 2 && channels == 1) {
        cv::Mat bgr;
        cv::cvtColor(img, bgr, cv::COLOR_GRAY2BGR);
        return bgr;
    }

    // 3维图像
    if (img.dims == 2) {
        if (channels == 1) {
            cv::Mat bgr;
            cv::cvtColor(img, bgr, cv::COLOR_GRAY2BGR);
            return bgr;
        } else if (channels == 2) {
            return cvtTwoToThree(img);
        } else if (channels == 3) {
            if (isFromQImage) {
                // QImage的RGB需要转换为BGR
                cv::Mat bgr;
                cv::cvtColor(img, bgr, cv::COLOR_RGB2BGR);
                return bgr;
            }
            return img.clone();
        } else if (channels == 4) {
            return cvtFourToThree(img);
        } else {
            throw LoadImageError("The channel(" + std::to_string(channels) +
                                 ") of the img is not in [1, 2, 3, 4]");
        }
    }

    throw LoadImageError("The ndim(" + std::to_string(img.dims) +
                         ") of the img is not in [2, 3]");
}

cv::Mat LoadImage::cvtTwoToThree(const cv::Mat& img) {
    // gray + alpha -> BGR
    std::vector<cv::Mat> channels;
    cv::split(img, channels);

    cv::Mat img_gray = channels[0];
    cv::Mat img_alpha = channels[1];

    cv::Mat img_bgr;
    cv::cvtColor(img_gray, img_bgr, cv::COLOR_GRAY2BGR);

    cv::Mat not_a;
    cv::bitwise_not(img_alpha, not_a);
    cv::Mat not_a_bgr;
    cv::cvtColor(not_a, not_a_bgr, cv::COLOR_GRAY2BGR);

    cv::Mat new_img;
    cv::bitwise_and(img_bgr, img_bgr, new_img, img_alpha);
    cv::add(new_img, not_a_bgr, new_img);

    return new_img;
}

cv::Mat LoadImage::cvtFourToThree(const cv::Mat& img) {
    // RGBA -> BGR
    std::vector<cv::Mat> channels;
    cv::split(img, channels);

    cv::Mat r = channels[0];
    cv::Mat g = channels[1];
    cv::Mat b = channels[2];
    cv::Mat a = channels[3];

    std::vector<cv::Mat> bgr_channels = {b, g, r};
    cv::Mat new_img;
    cv::merge(bgr_channels, new_img);

    cv::Mat not_a;
    cv::bitwise_not(a, not_a);
    cv::Mat not_a_bgr;
    cv::cvtColor(not_a, not_a_bgr, cv::COLOR_GRAY2BGR);

    cv::Mat result;
    cv::bitwise_and(new_img, new_img, result, a);

    cv::Scalar mean_color = cv::mean(result);
    double mean_val = (mean_color[0] + mean_color[1] + mean_color[2]) / 3.0;

    if (mean_val <= 0.0) {
        cv::add(result, not_a_bgr, result);
    } else {
        cv::bitwise_not(result, result);
    }

    return result;
}

void LoadImage::verifyExist(const std::string& filePath) {
    if (!std::filesystem::exists(filePath)) {
        throw LoadImageError(filePath + " does not exist.");
    }
}

} // namespace RapidOCR
