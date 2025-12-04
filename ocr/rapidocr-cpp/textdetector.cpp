#include "textdetector.h"
#include <chrono>
#include <algorithm>
#include "clipper1/clipper.hpp"

namespace RapidOCR {

TextDetector::TextDetector(const DetectorConfig& config, OrtInferSession* session)
    : config_(config), session_(session)
{
    if (!session_) {
        throw std::invalid_argument("OrtInferSession pointer cannot be null");
    }

    // 创建膨胀核
    if (config_.useDilation) {
        dilationKernel_ = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2, 2));
    }
}

TextDetOutput TextDetector::operator()(const cv::Mat& img) {
    auto startTime = std::chrono::high_resolution_clock::now();

    TextDetOutput output;

    if (img.empty()) {
        throw std::runtime_error("Input image is empty");
    }

    // 保存原始图像
    output.img = img.clone();

    // 预处理
    cv::Size oriSize;
    cv::Mat preprocessedImg = preprocess(img, oriSize);

    if (preprocessedImg.empty()) {
        return output;
    }

    // 推理
    cv::Mat preds = (*session_)(preprocessedImg);

    // 后处理
    postprocess(preds, oriSize, output.boxes, output.scores);

    if (output.boxes.empty()) {
        return output;
    }

    // 排序文本框
    output.boxes = sortedBoxes(output.boxes);

    // 计算耗时
    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = endTime - startTime;
    output.elapse = elapsed.count();

    return output;
}

cv::Mat TextDetector::preprocess(const cv::Mat& img, cv::Size& outOriSize) {
    outOriSize = img.size();

    // 动态调整limit_side_len
    int maxWH = std::max(img.rows, img.cols);
    int limitSideLen = getAdaptiveLimitSideLen(maxWH);

    // 计算缩放比例
    float ratio = 1.0f;
    int h = img.rows;
    int w = img.cols;

    if (config_.limitType == "max") {
        if (maxWH > limitSideLen) {
            ratio = static_cast<float>(limitSideLen) / static_cast<float>(maxWH);
        }
    } else { // "min"
        int minWH = std::min(h, w);
        if (minWH < limitSideLen) {
            ratio = static_cast<float>(limitSideLen) / static_cast<float>(minWH);
        }
    }

    // 计算调整后的尺寸（必须是32的倍数）
    int resizeH = static_cast<int>(h * ratio);
    int resizeW = static_cast<int>(w * ratio);

    resizeH = static_cast<int>(std::round(resizeH / 32.0f) * 32);
    resizeW = static_cast<int>(std::round(resizeW / 32.0f) * 32);

    if (resizeH <= 0 || resizeW <= 0) {
        return cv::Mat();
    }

    // 调整大小
    cv::Mat resizedImg;
    cv::resize(img, resizedImg, cv::Size(resizeW, resizeH));

    // 归一化
    resizedImg.convertTo(resizedImg, CV_32F, 1.0 / 255.0);

    // 减均值除标准差
    cv::Mat meanMat(1, 1, CV_32FC3, cv::Scalar(config_.mean[0], config_.mean[1], config_.mean[2]));
    cv::Mat stdMat(1, 1, CV_32FC3, cv::Scalar(config_.std[0], config_.std[1], config_.std[2]));

    cv::subtract(resizedImg, meanMat, resizedImg);
    cv::divide(resizedImg, stdMat, resizedImg);

    // 转换为 NCHW 格式 (OnnxRuntime 输入格式)
    std::vector<cv::Mat> channels;
    cv::split(resizedImg, channels);

    // 创建输出Mat [1, 3, H, W]
    int dims[] = {1, 3, resizeH, resizeW};
    cv::Mat blob(4, dims, CV_32F);

    for (int c = 0; c < 3; ++c) {
        cv::Mat channelMat(resizeH, resizeW, CV_32F,
                          blob.ptr<float>() + c * resizeH * resizeW);
        channels[c].copyTo(channelMat);
    }

    return blob;
}

void TextDetector::postprocess(const cv::Mat& pred,
                               const cv::Size& oriSize,
                               std::vector<std::vector<cv::Point>>& boxes,
                               std::vector<float>& scores) {
    int srcH = oriSize.height;
    int srcW = oriSize.width;

    // 提取预测结果 (假设输出为 [1, 1, H, W])
    cv::Mat probMap;
    if (pred.dims == 4) {
        // 提取 [0, 0, :, :]
        // 需要计算偏移量到 [0, 0, :, :] 的位置
        int h = pred.size[2];
        int w = pred.size[3];

        // 创建2D Mat指向4D Mat中的数据（需要使用非const指针）
        probMap = cv::Mat(h, w, CV_32F, const_cast<float*>(pred.ptr<float>()));
    } else if (pred.dims == 3) {
        // 如果是 [1, H, W]
        int h = pred.size[1];
        int w = pred.size[2];
        probMap = cv::Mat(h, w, CV_32F, const_cast<float*>(pred.ptr<float>()));
    } else if (pred.dims == 2) {
        probMap = pred;
    } else {
        return;
    }

    // 二值化
    cv::Mat bitmap;
    cv::threshold(probMap, bitmap, config_.thresh, 255, cv::THRESH_BINARY);
    bitmap.convertTo(bitmap, CV_8U);

    // 膨胀
    cv::Mat mask = bitmap.clone();
    if (config_.useDilation && !dilationKernel_.empty()) {
        cv::dilate(mask, mask, dilationKernel_);
    }

    // 从二值图中提取文本框
    boxesFromBitmap(probMap, mask, srcW, srcH, boxes, scores);

    // 过滤结果
    filterDetRes(boxes, scores, srcH, srcW);
}

void TextDetector::boxesFromBitmap(const cv::Mat& pred,
                                   const cv::Mat& bitmap,
                                   int destWidth,
                                   int destHeight,
                                   std::vector<std::vector<cv::Point>>& boxes,
                                   std::vector<float>& scores) {
    int height = bitmap.rows;
    int width = bitmap.cols;

    // 查找轮廓
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(bitmap, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

    int numContours = std::min(static_cast<int>(contours.size()), config_.maxCandidates);

    for (int i = 0; i < numContours; ++i) {
        const auto& contour = contours[i];

        // 获取最小外接矩形
        float minSideLen;
        std::vector<cv::Point2f> boxPoints = getMiniBoxes(contour, minSideLen);

        if (minSideLen < 3) {
            continue;
        }

        // 计算得分
        float score;
        if (config_.scoreMode == "fast") {
            score = boxScoreFast(pred, boxPoints);
        } else {
            score = boxScoreSlow(pred, contour);
        }

        if (score < config_.boxThresh) {
            continue;
        }

        // 扩展文本框
        std::vector<cv::Point2f> expandedBox = unclip(boxPoints);
        std::vector<cv::Point2f> expandedBoxPoints = getMiniBoxes(
            std::vector<cv::Point>(expandedBox.begin(), expandedBox.end()),
            minSideLen
        );

        if (minSideLen < 3 + 2) {
            continue;
        }

        // 缩放到原始图像尺寸
        std::vector<cv::Point> finalBox;
        for (const auto& pt : expandedBoxPoints) {
            cv::Point scaledPt;
            scaledPt.x = std::clamp(
                static_cast<int>(std::round(pt.x / width * destWidth)),
                0, destWidth - 1
            );
            scaledPt.y = std::clamp(
                static_cast<int>(std::round(pt.y / height * destHeight)),
                0, destHeight - 1
            );
            finalBox.push_back(scaledPt);
        }

        boxes.push_back(finalBox);
        scores.push_back(score);
    }
}

std::vector<cv::Point2f> TextDetector::getMiniBoxes(const std::vector<cv::Point>& contour,
                                                     float& minSideLen) {
    cv::RotatedRect rect = cv::minAreaRect(contour);
    cv::Point2f vertices[4];
    rect.points(vertices);

    // 排序点：按x坐标排序
    std::vector<cv::Point2f> points(vertices, vertices + 4);
    std::sort(points.begin(), points.end(), [](const cv::Point2f& a, const cv::Point2f& b) {
        return a.x < b.x;
    });

    // 确定四个角点的顺序
    int index1 = 0, index2 = 1, index3 = 2, index4 = 3;

    if (points[1].y > points[0].y) {
        index1 = 0;
        index4 = 1;
    } else {
        index1 = 1;
        index4 = 0;
    }

    if (points[3].y > points[2].y) {
        index2 = 2;
        index3 = 3;
    } else {
        index2 = 3;
        index3 = 2;
    }

    std::vector<cv::Point2f> box = {
        points[index1], points[index2], points[index3], points[index4]
    };

    minSideLen = std::min(rect.size.width, rect.size.height);
    return box;
}

float TextDetector::boxScoreFast(const cv::Mat& bitmap, const std::vector<cv::Point2f>& box) {
    int h = bitmap.rows;
    int w = bitmap.cols;

    std::vector<cv::Point2f> boxCopy = box;

    int xmin = std::clamp(static_cast<int>(std::floor(
                              std::min_element(boxCopy.begin(), boxCopy.end(),
                                               [](const cv::Point2f& a, const cv::Point2f& b) { return a.x < b.x; })->x
                              )), 0, w - 1);

    int xmax = std::clamp(static_cast<int>(std::ceil(
                              std::max_element(boxCopy.begin(), boxCopy.end(),
                                               [](const cv::Point2f& a, const cv::Point2f& b) { return a.x < b.x; })->x
                              )), 0, w - 1);

    int ymin = std::clamp(static_cast<int>(std::floor(
                              std::min_element(boxCopy.begin(), boxCopy.end(),
                                               [](const cv::Point2f& a, const cv::Point2f& b) { return a.y < b.y; })->y
                              )), 0, h - 1);

    int ymax = std::clamp(static_cast<int>(std::ceil(
                              std::max_element(boxCopy.begin(), boxCopy.end(),
                                               [](const cv::Point2f& a, const cv::Point2f& b) { return a.y < b.y; })->y
                              )), 0, h - 1);

    cv::Mat mask = cv::Mat::zeros(ymax - ymin + 1, xmax - xmin + 1, CV_8U);

    // 调整box坐标
    for (auto& pt : boxCopy) {
        pt.x -= xmin;
        pt.y -= ymin;
    }

    std::vector<std::vector<cv::Point>> contours = {
        std::vector<cv::Point>(boxCopy.begin(), boxCopy.end())
    };
    cv::fillPoly(mask, contours, cv::Scalar(1));

    cv::Mat roi = bitmap(cv::Rect(xmin, ymin, xmax - xmin + 1, ymax - ymin + 1));
    return cv::mean(roi, mask)[0];
}

float TextDetector::boxScoreSlow(const cv::Mat& bitmap, const std::vector<cv::Point>& contour) {
    int h = bitmap.rows;
    int w = bitmap.cols;

    std::vector<cv::Point> contourCopy = contour;

    int xmin = std::clamp(
        std::min_element(contourCopy.begin(), contourCopy.end(),
        [](const cv::Point& a, const cv::Point& b) { return a.x < b.x; })->x,
        0, w - 1
    );

    int xmax = std::clamp(
        std::max_element(contourCopy.begin(), contourCopy.end(),
        [](const cv::Point& a, const cv::Point& b) { return a.x < b.x; })->x,
        0, w - 1
    );

    int ymin = std::clamp(
        std::min_element(contourCopy.begin(), contourCopy.end(),
        [](const cv::Point& a, const cv::Point& b) { return a.y < b.y; })->y,
        0, h - 1
    );

    int ymax = std::clamp(
        std::max_element(contourCopy.begin(), contourCopy.end(),
        [](const cv::Point& a, const cv::Point& b) { return a.y < b.y; })->y,
        0, h - 1
    );

    cv::Mat mask = cv::Mat::zeros(ymax - ymin + 1, xmax - xmin + 1, CV_8U);

    for (auto& pt : contourCopy) {
        pt.x -= xmin;
        pt.y -= ymin;
    }

    std::vector<std::vector<cv::Point>> contours = {contourCopy};
    cv::fillPoly(mask, contours, cv::Scalar(1));

    cv::Mat roi = bitmap(cv::Rect(xmin, ymin, xmax - xmin + 1, ymax - ymin + 1));
    return cv::mean(roi, mask)[0];
}

std::vector<cv::Point2f> TextDetector::unclip(const std::vector<cv::Point2f>& box) {
    using namespace ClipperLib;

    // 计算多边形面积和周长
    double area = cv::contourArea(box);
    double length = cv::arcLength(box, true);

    if (length < 1e-6) {
        return box;
    }

    double distance = area * config_.unclipRatio / length;

    // 转换为 Clipper 的整数坐标（Clipper 使用整数以保证数值稳定性）
    // 缩放因子，通常使用 1000 或更大的值以保持精度
    const double SCALE = 1000.0;

    Path path;
    for (const auto& pt : box) {
        path.push_back(IntPoint(
            static_cast<cInt>(pt.x * SCALE),
            static_cast<cInt>(pt.y * SCALE)
            ));
    }

    // 使用 ClipperOffset 进行多边形偏移
    ClipperOffset clipperOffset;
    clipperOffset.AddPath(path, jtRound, etClosedPolygon);

    Paths solution;
    clipperOffset.Execute(solution, distance * SCALE);

    // 转换回 cv::Point2f
    std::vector<cv::Point2f> result;
    if (!solution.empty() && !solution[0].empty()) {
        for (const auto& pt : solution[0]) {
            result.push_back(cv::Point2f(
                static_cast<float>(pt.X / SCALE),
                static_cast<float>(pt.Y / SCALE)
                ));
        }
    } else {
        // 如果 Clipper 失败，返回原始框
        result = box;
    }

    return result;
}

void TextDetector::filterDetRes(std::vector<std::vector<cv::Point>>& boxes,
                                std::vector<float>& scores,
                                int imgHeight,
                                int imgWidth) {
    std::vector<std::vector<cv::Point>> newBoxes;
    std::vector<float> newScores;

    for (size_t i = 0; i < boxes.size(); ++i) {
        // 按顺时针排序点
        std::vector<cv::Point2f> floatBox(boxes[i].begin(), boxes[i].end());
        std::vector<cv::Point> orderedBox = orderPointsClockwise(floatBox);

        // 裁剪
        clipDetRes(orderedBox, imgHeight, imgWidth);

        // 计算宽高
        int rectWidth = static_cast<int>(cv::norm(orderedBox[0] - orderedBox[1]));
        int rectHeight = static_cast<int>(cv::norm(orderedBox[0] - orderedBox[3]));

        if (rectWidth <= 3 || rectHeight <= 3) {
            continue;
        }

        newBoxes.push_back(orderedBox);
        newScores.push_back(scores[i]);
    }

    boxes = newBoxes;
    scores = newScores;
}

std::vector<cv::Point> TextDetector::orderPointsClockwise(const std::vector<cv::Point2f>& pts) {
    // 按x坐标排序
    std::vector<cv::Point2f> sorted = pts;
    std::sort(sorted.begin(), sorted.end(), [](const cv::Point2f& a, const cv::Point2f& b) {
        return a.x < b.x;
    });

    // 左侧两个点
    std::vector<cv::Point2f> leftMost = {sorted[0], sorted[1]};
    std::sort(leftMost.begin(), leftMost.end(), [](const cv::Point2f& a, const cv::Point2f& b) {
        return a.y < b.y;
    });

    // 右侧两个点
    std::vector<cv::Point2f> rightMost = {sorted[2], sorted[3]};
    std::sort(rightMost.begin(), rightMost.end(), [](const cv::Point2f& a, const cv::Point2f& b) {
        return a.y < b.y;
    });

    // 顺时针顺序：左上、右上、右下、左下
    std::vector<cv::Point> result = {
        cv::Point(static_cast<int>(leftMost[0].x), static_cast<int>(leftMost[0].y)),   // 左上
        cv::Point(static_cast<int>(rightMost[0].x), static_cast<int>(rightMost[0].y)), // 右上
        cv::Point(static_cast<int>(rightMost[1].x), static_cast<int>(rightMost[1].y)), // 右下
        cv::Point(static_cast<int>(leftMost[1].x), static_cast<int>(leftMost[1].y))    // 左下
    };

    return result;
}

void TextDetector::clipDetRes(std::vector<cv::Point>& points, int imgHeight, int imgWidth) {
    for (auto& pt : points) {
        pt.x = std::clamp(pt.x, 0, imgWidth - 1);
        pt.y = std::clamp(pt.y, 0, imgHeight - 1);
    }
}

std::vector<std::vector<cv::Point>> TextDetector::sortedBoxes(
    std::vector<std::vector<cv::Point>>& dtBoxes) {

    // 按照第一个点的y坐标，然后x坐标排序
    std::sort(dtBoxes.begin(), dtBoxes.end(),
        [](const std::vector<cv::Point>& a, const std::vector<cv::Point>& b) {
            if (std::abs(a[0].y - b[0].y) < 10) {
                return a[0].x < b[0].x;
            }
            return a[0].y < b[0].y;
        }
    );

    // 冒泡排序微调：同一行的文本框按x排序
    int numBoxes = dtBoxes.size();
    for (int i = 0; i < numBoxes - 1; ++i) {
        for (int j = i; j >= 0; --j) {
            if (std::abs(dtBoxes[j + 1][0].y - dtBoxes[j][0].y) < 10 &&
                dtBoxes[j + 1][0].x < dtBoxes[j][0].x) {
                std::swap(dtBoxes[j], dtBoxes[j + 1]);
            } else {
                break;
            }
        }
    }

    return dtBoxes;
}

int TextDetector::getAdaptiveLimitSideLen(int maxWH) {
    if (config_.limitType == "min") {
        return config_.limitSideLen;
    } else if (maxWH < 960) {
        return 960;
    } else if (maxWH < 1500) {
        return 1500;
    } else {
        return 2000;
    }
}

} // namespace RapidOCR
