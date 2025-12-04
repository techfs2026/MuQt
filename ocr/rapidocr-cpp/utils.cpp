#include "utils.h"
#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QUrl>
#include <filesystem>
#include <algorithm>

namespace RapidOCR {

void Utils::mkdir(const std::string& dirPath) {
    QDir dir;
    dir.mkpath(QString::fromStdString(dirPath));
}

std::tuple<float, float, float, float> Utils::quadsToRectBBox(const cv::Mat& bbox) {
    // 支持多种输入格式
    // 1. shape (N, 4, 2) - 3维
    // 2. shape (4, 2) - 2维
    // 3. shape (1, 4, 2) - 3维但N=1

    float x_min = std::numeric_limits<float>::max();
    float y_min = std::numeric_limits<float>::max();
    float x_max = std::numeric_limits<float>::lowest();
    float y_max = std::numeric_limits<float>::lowest();

    if (bbox.dims == 3) {
        // 3维情况 (N, 4, 2)
        if (bbox.size[1] != 4 || bbox.size[2] != 2) {
            throw std::invalid_argument("bbox shape must be (N, 4, 2)");
        }

        int N = bbox.size[0];
        for (int i = 0; i < N; ++i) {
            for (int j = 0; j < 4; ++j) {
                float x = bbox.at<float>(i, j, 0);
                float y = bbox.at<float>(i, j, 1);
                x_min = std::min(x_min, x);
                y_min = std::min(y_min, y);
                x_max = std::max(x_max, x);
                y_max = std::max(y_max, y);
            }
        }
    } else if (bbox.dims == 2) {
        // 2维情况 (4, 2)
        if (bbox.rows != 4 || bbox.cols != 2) {
            throw std::invalid_argument("bbox shape must be (4, 2)");
        }

        for (int i = 0; i < 4; ++i) {
            float x = bbox.at<float>(i, 0);
            float y = bbox.at<float>(i, 1);
            x_min = std::min(x_min, x);
            y_min = std::min(y_min, y);
            x_max = std::max(x_max, x);
            y_max = std::max(y_max, y);
        }
    } else {
        throw std::invalid_argument("bbox dims must be 2 or 3");
    }

    return {x_min, y_min, x_max, y_max};
}

bool Utils::hasChineseChar(const std::string& text) {
    QString qstr = QString::fromStdString(text);
    for (const QChar& ch : qstr) {
        if (ch >= QChar(0x4E00) && ch <= QChar(0x9FFF)) {
            return true;
        }
    }
    return false;
}

std::string Utils::getFileSHA256(const std::string& filePath, int chunkSize) {
    QFile file(QString::fromStdString(filePath));
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("Cannot open file: " + filePath);
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);

    while (!file.atEnd()) {
        QByteArray chunk = file.read(chunkSize);
        hash.addData(chunk);
    }

    file.close();
    return hash.result().toHex().toStdString();
}

void Utils::saveImage(const std::string& savePath, const cv::Mat& img) {
    // 确保目录存在
    std::filesystem::path path(savePath);
    if (path.has_parent_path()) {
        mkdir(path.parent_path().string());
    }

    // OpenCV保存图像
    if (!cv::imwrite(savePath, img)) {
        throw std::runtime_error("Failed to save image: " + savePath);
    }
}

bool Utils::isUrl(const std::string& url) {
    QUrl qurl(QString::fromStdString(url));
    return qurl.isValid() && !qurl.scheme().isEmpty() && !qurl.host().isEmpty();
}

} // namespace RapidOCR
