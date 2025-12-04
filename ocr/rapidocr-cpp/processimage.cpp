#include "processimage.h"
#include <algorithm>
#include <cmath>

namespace RapidOCR {

cv::Mat ProcessImage::mapBoxesToOriginal(
    const cv::Mat& dtBoxes,
    const OpRecord& opRecord,
    int oriH,
    int oriW
    ) {
    cv::Mat boxes = dtBoxes.clone();

    // 按逆序处理操作记录
    std::vector<std::string> ops;
    for (const auto& [key, _] : opRecord) {
        ops.push_back(key);
    }
    std::reverse(ops.begin(), ops.end());

    for (const std::string& op : ops) {
        const auto& v = opRecord.at(op);

        if (op.find("padding") != std::string::npos) {
            int top = std::any_cast<int>(v.at("top"));
            int left = std::any_cast<int>(v.at("left"));

            // 调整所有点的坐标
            for (int i = 0; i < boxes.rows; ++i) {
                for (int j = 0; j < boxes.cols; ++j) {
                    cv::Vec2f& pt = boxes.at<cv::Vec2f>(i, j);
                    pt[0] -= left;
                    pt[1] -= top;
                }
            }
        } else if (op.find("preprocess") != std::string::npos) {
            float ratioH = std::any_cast<float>(v.at("ratio_h"));
            float ratioW = std::any_cast<float>(v.at("ratio_w"));

            for (int i = 0; i < boxes.rows; ++i) {
                for (int j = 0; j < boxes.cols; ++j) {
                    cv::Vec2f& pt = boxes.at<cv::Vec2f>(i, j);
                    pt[0] *= ratioW;
                    pt[1] *= ratioH;
                }
            }
        }
    }

    // 限制坐标在原始图像范围内
    for (int i = 0; i < boxes.rows; ++i) {
        for (int j = 0; j < boxes.cols; ++j) {
            cv::Vec2f& pt = boxes.at<cv::Vec2f>(i, j);
            pt[0] = std::max(0.0f, std::min(pt[0], static_cast<float>(oriW)));
            pt[1] = std::max(0.0f, std::min(pt[1], static_cast<float>(oriH)));
        }
    }

    return boxes;
}

std::tuple<cv::Mat, OpRecord> ProcessImage::applyVerticalPadding(
    const cv::Mat& img,
    OpRecord opRecord,
    float widthHeightRatio,
    float minHeight
    ) {
    int h = img.rows;
    int w = img.cols;

    bool useLimitRatio = (widthHeightRatio != -1) &&
                         (static_cast<float>(w) / h > widthHeightRatio);

    if (h <= minHeight || useLimitRatio) {
        int paddingH = getPaddingH(h, w, widthHeightRatio, minHeight);
        cv::Mat blockImg = addRoundLetterbox(img, {paddingH, paddingH, 0, 0});

        std::map<std::string, std::any> paddingInfo;
        paddingInfo["top"] = paddingH;
        paddingInfo["left"] = 0;
        opRecord["padding_1"] = paddingInfo;

        return {blockImg, opRecord};
    }

    std::map<std::string, std::any> paddingInfo;
    paddingInfo["top"] = 0;
    paddingInfo["left"] = 0;
    opRecord["padding_1"] = paddingInfo;

    return {img.clone(), opRecord};
}

int ProcessImage::getPaddingH(int h, int w, float widthHeightRatio, float minHeight) {
    float newH = std::max(static_cast<float>(w) / widthHeightRatio, minHeight) * 2;
    int paddingH = static_cast<int>(std::abs(newH - h) / 2);
    return paddingH;
}

cv::Mat ProcessImage::getRotateCropImage(const cv::Mat& img, const cv::Mat& points) {
    // points应该是4x2的矩阵
    cv::Point2f pts[4];
    for (int i = 0; i < 4; ++i) {
        pts[i] = cv::Point2f(points.at<float>(i, 0), points.at<float>(i, 1));
    }

    // 计算裁剪区域的宽度和高度
    float width1 = cv::norm(pts[0] - pts[1]);
    float width2 = cv::norm(pts[2] - pts[3]);
    int imgCropWidth = static_cast<int>(std::max(width1, width2));

    float height1 = cv::norm(pts[0] - pts[3]);
    float height2 = cv::norm(pts[1] - pts[2]);
    int imgCropHeight = static_cast<int>(std::max(height1, height2));

    // 目标点
    cv::Point2f ptsStd[4] = {
        cv::Point2f(0, 0),
        cv::Point2f(imgCropWidth, 0),
        cv::Point2f(imgCropWidth, imgCropHeight),
        cv::Point2f(0, imgCropHeight)
    };

    // 透视变换
    cv::Mat M = cv::getPerspectiveTransform(pts, ptsStd);
    cv::Mat dstImg;
    cv::warpPerspective(img, dstImg, M,
                        cv::Size(imgCropWidth, imgCropHeight),
                        cv::INTER_CUBIC, cv::BORDER_REPLICATE);

    // 如果高度远大于宽度，则旋转90度
    if (dstImg.rows * 1.0f / dstImg.cols >= 1.5f) {
        cv::rotate(dstImg, dstImg, cv::ROTATE_90_COUNTERCLOCKWISE);
    }

    return dstImg;
}

std::tuple<cv::Mat, float, float> ProcessImage::resizeImageWithinBounds(
    const cv::Mat& img,
    float minSideLen,
    float maxSideLen
    ) {
    int h = img.rows;
    int w = img.cols;
    int maxValue = std::max(h, w);

    cv::Mat resizedImg = img.clone();
    float ratioH = 1.0f;
    float ratioW = 1.0f;

    if (maxValue > maxSideLen) {
        auto [tmpImg, tmpRatioH, tmpRatioW] = reduceMaxSide(resizedImg, maxSideLen);
        resizedImg = tmpImg;
        ratioH = tmpRatioH;
        ratioW = tmpRatioW;
    }

    h = resizedImg.rows;
    w = resizedImg.cols;
    int minValue = std::min(h, w);

    if (minValue < minSideLen) {
        auto [tmpImg, tmpRatioH, tmpRatioW] = increaseMinSide(resizedImg, minSideLen);
        resizedImg = tmpImg;
        ratioH = tmpRatioH;
        ratioW = tmpRatioW;
    }

    return {resizedImg, ratioH, ratioW};
}

std::tuple<cv::Mat, float, float> ProcessImage::reduceMaxSide(
    const cv::Mat& img,
    float maxSideLen
    ) {
    int h = img.rows;
    int w = img.cols;

    float ratio = 1.0f;
    if (std::max(h, w) > maxSideLen) {
        if (h > w) {
            ratio = maxSideLen / h;
        } else {
            ratio = maxSideLen / w;
        }
    }

    int resizeH = static_cast<int>(h * ratio);
    int resizeW = static_cast<int>(w * ratio);

    // 调整为32的倍数
    resizeH = static_cast<int>(std::round(resizeH / 32.0) * 32);
    resizeW = static_cast<int>(std::round(resizeW / 32.0) * 32);

    if (resizeW <= 0 || resizeH <= 0) {
        throw ResizeImgError("resize_w or resize_h is less than or equal to 0");
    }

    cv::Mat resizedImg;
    try {
        cv::resize(img, resizedImg, cv::Size(resizeW, resizeH));
    } catch (const cv::Exception& e) {
        throw ResizeImgError(e.what());
    }

    float ratioH = static_cast<float>(h) / resizeH;
    float ratioW = static_cast<float>(w) / resizeW;

    return {resizedImg, ratioH, ratioW};
}

std::tuple<cv::Mat, float, float> ProcessImage::increaseMinSide(
    const cv::Mat& img,
    float minSideLen
    ) {
    int h = img.rows;
    int w = img.cols;

    float ratio = 1.0f;
    if (std::min(h, w) < minSideLen) {
        if (h < w) {
            ratio = minSideLen / h;
        } else {
            ratio = minSideLen / w;
        }
    }

    int resizeH = static_cast<int>(h * ratio);
    int resizeW = static_cast<int>(w * ratio);

    // 调整为32的倍数
    resizeH = static_cast<int>(std::round(resizeH / 32.0) * 32);
    resizeW = static_cast<int>(std::round(resizeW / 32.0) * 32);

    if (resizeW <= 0 || resizeH <= 0) {
        throw ResizeImgError("resize_w or resize_h is less than or equal to 0");
    }

    cv::Mat resizedImg;
    try {
        cv::resize(img, resizedImg, cv::Size(resizeW, resizeH));
    } catch (const cv::Exception& e) {
        throw ResizeImgError(e.what());
    }

    float ratioH = static_cast<float>(h) / resizeH;
    float ratioW = static_cast<float>(w) / resizeW;

    return {resizedImg, ratioH, ratioW};
}

cv::Mat ProcessImage::addRoundLetterbox(
    const cv::Mat& img,
    const std::tuple<int, int, int, int>& paddingTuple
    ) {
    auto [top, bottom, left, right] = paddingTuple;

    cv::Mat paddedImg;
    cv::copyMakeBorder(img, paddedImg, top, bottom, left, right,
                       cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));

    return paddedImg;
}

} // namespace RapidOCR
