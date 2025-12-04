#include "calrecboxes.h"
#include "utils.h"
#include <algorithm>
#include <cmath>
#include <numeric>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace RapidOCR {

TextRecOutput CalRecBoxes::operator()(const std::vector<cv::Mat>& imgs,
                                      const std::vector<std::vector<cv::Point>>& dtBoxes,
                                      TextRecOutput& recRes,
                                      bool returnSingleCharBox) {

    std::vector<WordInfo> updatedWordResults;
    updatedWordResults.reserve(imgs.size());

    for (size_t idx = 0; idx < imgs.size(); ++idx) {
        const auto& img = imgs[idx];
        const auto& box = dtBoxes[idx];

        // 检查有效性
        if (idx >= recRes.txts.size() || img.empty() || idx >= recRes.wordResults.size()) {
            updatedWordResults.push_back(WordInfo());
            continue;
        }

        int h = img.rows;
        int w = img.cols;
        std::vector<cv::Point> imgBox = {{0, 0}, {w, 0}, {w, h}, {0, h}};

        // 计算OCR词语框
        auto [wordBoxContentList, wordBoxList, confList] = calOcrWordBox(
            recRes.txts[idx],
            imgBox,
            recRes.wordResults[idx],
            returnSingleCharBox
            );

        // 调整框重叠
        wordBoxList = adjustBoxOverlap(wordBoxList);

        // 获取方向
        Direction direction = getBoxDirection(box);

        // 反向旋转
        wordBoxList = reverseRotateCropImage(box, wordBoxList, direction);

        // 更新WordInfo - 保留原有信息，添加边界框
        WordInfo updatedInfo = recRes.wordResults[idx];
        updatedInfo.wordBoxes = wordBoxList;  // 存储计算出的边界框

        updatedWordResults.push_back(updatedInfo);
    }

    // 更新结果
    recRes.wordResults = updatedWordResults;

    return recRes;
}

Direction CalRecBoxes::getBoxDirection(const std::vector<cv::Point>& box) {
    if (box.size() < 4) {
        return Direction::HORIZONTAL;
    }

    // 计算边长
    std::vector<float> edgeLengths = {
        static_cast<float>(cv::norm(box[0] - box[1])),  // 上边
        static_cast<float>(cv::norm(box[1] - box[2])),  // 右边
        static_cast<float>(cv::norm(box[2] - box[3])),  // 下边
        static_cast<float>(cv::norm(box[3] - box[0]))   // 左边
    };

    // 宽和高取对边的最大距离
    float width = std::max(edgeLengths[0], edgeLengths[2]);
    float height = std::max(edgeLengths[1], edgeLengths[3]);

    if (width < 1e-6f) {
        return Direction::VERTICAL;
    }

    float aspectRatio = std::round(height / width * 100.0f) / 100.0f;
    return aspectRatio >= 1.5f ? Direction::VERTICAL : Direction::HORIZONTAL;
}

std::tuple<std::vector<std::string>, std::vector<std::vector<cv::Point>>, std::vector<float>>
CalRecBoxes::calOcrWordBox(const std::string& recTxt,
                           const std::vector<cv::Point>& bbox,
                           const WordInfo& wordInfo,
                           bool returnSingleCharBox) {

    std::vector<std::string> wordContents;
    std::vector<std::vector<cv::Point>> wordBoxes;
    std::vector<float> confList;

    if (recTxt.empty() || wordInfo.lineTxtLen == 0) {
        return {wordContents, wordBoxes, confList};
    }

    // 转换bbox为矩形坐标
    cv::Mat bboxMat(4, 2, CV_32F);
    for (int i = 0; i < 4; ++i) {
        bboxMat.at<float>(i, 0) = bbox[i].x;
        bboxMat.at<float>(i, 1) = bbox[i].y;
    }
    bboxMat = bboxMat.reshape(1, 1);
    bboxMat = bboxMat.reshape(2, 4);

    auto bboxPoints = Utils::quadsToRectBBox(bboxMat.reshape(2, 1));
    auto [x0, y0, x1, y1] = bboxPoints;

    float avgColWidth = (x1 - x0) / wordInfo.lineTxtLen;

    // 检查是否全是英文数字
    bool isAllEnNum = true;
    for (const auto& type : wordInfo.wordTypes) {
        if (type != WordType::EN_NUM) {
            isAllEnNum = false;
            break;
        }
    }

    // 处理词语和列
    std::vector<std::vector<int>> lineCols;
    std::vector<float> charWidths;

    for (size_t i = 0; i < wordInfo.words.size(); ++i) {
        const auto& word = wordInfo.words[i];
        const auto& wordCol = wordInfo.wordCols[i];

        if (isAllEnNum && !returnSingleCharBox) {
            // 英文单词级别
            lineCols.push_back(wordCol);
            std::string wordContent;
            for (const auto& ch : word) {
                wordContent += ch;
            }
            wordContents.push_back(wordContent);
        } else {
            // 单字符级别
            for (const auto& ch : word) {
                wordContents.push_back(ch);
            }
            for (const auto& col : wordCol) {
                lineCols.push_back({col});
            }
        }

        // 计算字符宽度
        if (wordCol.size() > 1) {
            float avgWidth = calcAvgCharWidth(wordCol, avgColWidth);
            charWidths.push_back(avgWidth);
        }
    }

    // 计算平均字符宽度
    float avgCharWidth = calcAllCharAvgWidth(charWidths, x0, x1, recTxt.length());

    // 计算框
    if (isAllEnNum && !returnSingleCharBox) {
        wordBoxes = calcEnNumBox(lineCols, avgCharWidth, avgColWidth, bboxPoints);
    } else {
        for (const auto& cols : lineCols) {
            auto boxes = calcBox(cols, avgCharWidth, avgColWidth, bboxPoints);
            if (!boxes.empty()) {
                wordBoxes.push_back(boxes[0]);
            }
        }
    }

    confList = wordInfo.confs;

    return {wordContents, wordBoxes, confList};
}

std::vector<std::vector<cv::Point>> CalRecBoxes::calcEnNumBox(
    const std::vector<std::vector<int>>& lineCols,
    float avgCharWidth,
    float avgColWidth,
    const std::tuple<float, float, float, float>& bboxPoints) {

    std::vector<std::vector<cv::Point>> results;

    for (const auto& oneCol : lineCols) {
        auto curWordCell = calcBox(oneCol, avgCharWidth, avgColWidth, bboxPoints);

        if (curWordCell.empty()) {
            continue;
        }

        // 将多个字符框合并为一个单词框
        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float maxY = std::numeric_limits<float>::lowest();

        for (const auto& cell : curWordCell) {
            for (const auto& pt : cell) {
                minX = std::min(minX, static_cast<float>(pt.x));
                minY = std::min(minY, static_cast<float>(pt.y));
                maxX = std::max(maxX, static_cast<float>(pt.x));
                maxY = std::max(maxY, static_cast<float>(pt.y));
            }
        }

        std::vector<cv::Point> wordBox = {
            cv::Point(static_cast<int>(minX), static_cast<int>(minY)),
            cv::Point(static_cast<int>(maxX), static_cast<int>(minY)),
            cv::Point(static_cast<int>(maxX), static_cast<int>(maxY)),
            cv::Point(static_cast<int>(minX), static_cast<int>(maxY))
        };
        results.push_back(wordBox);
    }

    return results;
}

std::vector<std::vector<cv::Point>> CalRecBoxes::calcBox(
    const std::vector<int>& lineCols,
    float avgCharWidth,
    float avgColWidth,
    const std::tuple<float, float, float, float>& bboxPoints) {

    auto [x0, y0, x1, y1] = bboxPoints;

    std::vector<std::vector<cv::Point>> results;

    for (int colIdx : lineCols) {
        // 将中心点定位在列的中间位置
        float centerX = (colIdx + 0.5f) * avgColWidth;

        // 计算字符单元格的左右边界
        int charX0 = std::max(static_cast<int>(centerX - avgCharWidth / 2.0f), 0) + static_cast<int>(x0);
        int charX1 = std::min(static_cast<int>(centerX + avgCharWidth / 2.0f), static_cast<int>(x1 - x0)) + static_cast<int>(x0);

        std::vector<cv::Point> cell = {
            cv::Point(charX0, static_cast<int>(y0)),
            cv::Point(charX1, static_cast<int>(y0)),
            cv::Point(charX1, static_cast<int>(y1)),
            cv::Point(charX0, static_cast<int>(y1))
        };
        results.push_back(cell);
    }

    // 按x坐标排序
    std::sort(results.begin(), results.end(),
              [](const std::vector<cv::Point>& a, const std::vector<cv::Point>& b) {
                  return a[0].x < b[0].x;
              }
              );

    return results;
}

float CalRecBoxes::calcAvgCharWidth(const std::vector<int>& wordCol, float eachColWidth) {
    if (wordCol.size() <= 1) {
        return eachColWidth;
    }

    float charTotalLength = (wordCol.back() - wordCol.front()) * eachColWidth;
    return charTotalLength / (wordCol.size() - 1);
}

float CalRecBoxes::calcAllCharAvgWidth(const std::vector<float>& widthList,
                                       float bboxX0,
                                       float bboxX1,
                                       int txtLen) {
    if (txtLen == 0) {
        return 0.0f;
    }

    if (!widthList.empty()) {
        return std::accumulate(widthList.begin(), widthList.end(), 0.0f) / widthList.size();
    }

    return (bboxX1 - bboxX0) / txtLen;
}

std::vector<std::vector<cv::Point>> CalRecBoxes::adjustBoxOverlap(
    std::vector<std::vector<cv::Point>>& wordBoxList) {

    // 调整bbox有重叠的地方
    for (size_t i = 0; i < wordBoxList.size() - 1; ++i) {
        auto& cur = wordBoxList[i];
        auto& nxt = wordBoxList[i + 1];

        if (cur[1].x > nxt[0].x) {  // 有交集
            float distance = std::abs(static_cast<float>(cur[1].x - nxt[0].x));
            float half = distance / 2.0f;

            cur[1].x -= static_cast<int>(half);
            cur[2].x -= static_cast<int>(half);
            nxt[0].x += static_cast<int>(distance - half);
            nxt[3].x += static_cast<int>(distance - half);
        }
    }

    return wordBoxList;
}

std::vector<std::vector<cv::Point>> CalRecBoxes::reverseRotateCropImage(
    const std::vector<cv::Point>& bboxPoints,
    const std::vector<std::vector<cv::Point>>& wordPointsList,
    Direction direction) {

    // 复制bbox点
    std::vector<cv::Point2f> bbox;
    for (const auto& pt : bboxPoints) {
        bbox.push_back(cv::Point2f(pt.x, pt.y));
    }

    // 计算最小值
    float left = std::min({bbox[0].x, bbox[1].x, bbox[2].x, bbox[3].x});
    float top = std::min({bbox[0].y, bbox[1].y, bbox[2].y, bbox[3].y});

    for (auto& pt : bbox) {
        pt.x -= left;
        pt.y -= top;
    }

    int imgCropWidth = static_cast<int>(cv::norm(bbox[0] - bbox[1]));
    int imgCropHeight = static_cast<int>(cv::norm(bbox[0] - bbox[3]));

    // 标准点
    std::vector<cv::Point2f> ptsStd = {
        cv::Point2f(0, 0),
        cv::Point2f(imgCropWidth, 0),
        cv::Point2f(imgCropWidth, imgCropHeight),
        cv::Point2f(0, imgCropHeight)
    };

    // 计算透视变换矩阵
    cv::Mat M = cv::getPerspectiveTransform(bbox, ptsStd);
    cv::Mat IM;
    cv::invert(M, IM);

    std::vector<std::vector<cv::Point>> newWordPointsList;

    for (const auto& wordPoints : wordPointsList) {
        std::vector<cv::Point> newWordPoints;

        for (const auto& point : wordPoints) {
            cv::Point2f newPoint(point.x, point.y);

            // 如果是垂直方向，先旋转
            if (direction == Direction::VERTICAL) {
                auto [rx, ry] = sRotate(
                    -M_PI / 2.0f,
                    newPoint.x,
                    newPoint.y,
                    0, 0
                    );
                newPoint.x = rx + imgCropWidth;
                newPoint.y = ry;
            }

            // 应用逆透视变换
            cv::Mat p = (cv::Mat_<float>(3, 1) << newPoint.x, newPoint.y, 1.0f);
            cv::Mat result = IM * p;

            float x = result.at<float>(0, 0) / result.at<float>(2, 0);
            float y = result.at<float>(1, 0) / result.at<float>(2, 0);

            newWordPoints.push_back(cv::Point(
                static_cast<int>(x + left),
                static_cast<int>(y + top)
                ));
        }

        newWordPoints = orderPoints(newWordPoints);
        newWordPointsList.push_back(newWordPoints);
    }

    return newWordPointsList;
}

std::pair<float, float> CalRecBoxes::sRotate(float angle,
                                             float valuex,
                                             float valuey,
                                             float pointx,
                                             float pointy) {
    float sRotatex = (valuex - pointx) * std::cos(angle) +
                     (valuey - pointy) * std::sin(angle) + pointx;
    float sRotatey = (valuey - pointy) * std::cos(angle) -
                     (valuex - pointx) * std::sin(angle) + pointy;
    return {sRotatex, sRotatey};
}

std::vector<cv::Point> CalRecBoxes::orderPoints(const std::vector<cv::Point>& oriBox) {
    if (oriBox.size() < 4) {
        return oriBox;
    }

    std::vector<cv::Point> box = oriBox;

    // 计算中心点
    float centerX = 0, centerY = 0;
    for (const auto& pt : box) {
        centerX += pt.x;
        centerY += pt.y;
    }
    centerX /= box.size();
    centerY /= box.size();

    // 找到四个角点
    cv::Point p1, p2, p3, p4;

    // 检查是否有点横坐标相等或纵坐标相等
    bool hasEqualX = false, hasEqualY = false;
    for (size_t i = 0; i < box.size(); ++i) {
        if (std::abs(box[i].x - centerX) < 1e-6) hasEqualX = true;
        if (std::abs(box[i].y - centerY) < 1e-6) hasEqualY = true;
    }

    if (hasEqualX && hasEqualY) {
        // 菱形情况
        auto minX = std::min_element(box.begin(), box.end(),
                                     [](const cv::Point& a, const cv::Point& b) { return a.x < b.x; });
        auto minY = std::min_element(box.begin(), box.end(),
                                     [](const cv::Point& a, const cv::Point& b) { return a.y < b.y; });
        auto maxX = std::max_element(box.begin(), box.end(),
                                     [](const cv::Point& a, const cv::Point& b) { return a.x < b.x; });
        auto maxY = std::max_element(box.begin(), box.end(),
                                     [](const cv::Point& a, const cv::Point& b) { return a.y < b.y; });

        p1 = *minX;
        p2 = *minY;
        p3 = *maxX;
        p4 = *maxY;
    } else {
        // 一般情况：先左右再上下
        std::vector<cv::Point> leftPoints, rightPoints;
        for (const auto& pt : box) {
            if (pt.x < centerX) {
                leftPoints.push_back(pt);
            } else {
                rightPoints.push_back(pt);
            }
        }

        if (leftPoints.size() >= 2 && rightPoints.size() >= 2) {
            std::sort(leftPoints.begin(), leftPoints.end(),
                      [](const cv::Point& a, const cv::Point& b) { return a.y < b.y; });
            std::sort(rightPoints.begin(), rightPoints.end(),
                      [](const cv::Point& a, const cv::Point& b) { return a.y < b.y; });

            p1 = leftPoints[0];
            p4 = leftPoints.back();
            p2 = rightPoints[0];
            p3 = rightPoints.back();
        } else {
            // 退化情况，直接返回
            return box;
        }
    }

    return {p1, p2, p3, p4};
}

} // namespace RapidOCR
