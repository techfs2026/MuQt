#include "textclassifier.h"
#include <chrono>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace RapidOCR {

TextClassifier::TextClassifier(const ClassifierConfig& config, OrtInferSession* session)
    : config_(config), session_(session)
{
    if (!session_) {
        throw std::invalid_argument("OrtInferSession pointer cannot be null");
    }
}

TextClsOutput TextClassifier::operator()(const cv::Mat& img) {
    return (*this)(std::vector<cv::Mat>{img});
}

TextClsOutput TextClassifier::operator()(const std::vector<cv::Mat>& imgList) {
    auto startTime = std::chrono::high_resolution_clock::now();

    TextClsOutput output;

    if (imgList.empty()) {
        return output;
    }

    // 深拷贝图像列表
    std::vector<cv::Mat> imgListCopy;
    for (const auto& img : imgList) {
        imgListCopy.push_back(img.clone());
    }

    // 计算所有文本条的宽高比
    std::vector<float> widthList;
    widthList.reserve(imgListCopy.size());
    for (const auto& img : imgListCopy) {
        float ratio = static_cast<float>(img.cols) / static_cast<float>(img.rows);
        widthList.push_back(ratio);
    }

    // 按宽高比排序可以加速分类过程
    std::vector<size_t> indices(imgListCopy.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::sort(indices.begin(), indices.end(),
        [&widthList](size_t i1, size_t i2) {
            return widthList[i1] < widthList[i2];
        }
    );

    size_t imgNum = imgListCopy.size();
    std::vector<std::pair<std::string, float>> clsRes(imgNum, {"", 0.0f});

    int batchNum = config_.clsBatchNum;

    // 分批处理
    for (size_t begImgNo = 0; begImgNo < imgNum; begImgNo += batchNum) {
        size_t endImgNo = std::min(imgNum, begImgNo + batchNum);

        // 准备批次数据
        std::vector<cv::Mat> normImgBatch;
        for (size_t ino = begImgNo; ino < endImgNo; ++ino) {
            cv::Mat normImg = resizeNormImg(imgListCopy[indices[ino]]);
            normImgBatch.push_back(normImg);
        }

        // 拼接成一个大的Mat [N, C, H, W]
        int imgC = config_.clsImageShape[0];
        int imgH = config_.clsImageShape[1];
        int imgW = config_.clsImageShape[2];
        int batchSize = normImgBatch.size();

        // 创建批次Mat
        std::vector<int> dims = {batchSize, imgC, imgH, imgW};
        cv::Mat batchMat(dims.size(), dims.data(), CV_32F);

        // 复制数据
        for (size_t i = 0; i < normImgBatch.size(); ++i) {
            size_t offset = i * imgC * imgH * imgW;
            float* dstPtr = batchMat.ptr<float>() + offset;
            const float* srcPtr = normImgBatch[i].ptr<float>();
            std::memcpy(dstPtr, srcPtr, imgC * imgH * imgW * sizeof(float));
        }

        // 推理
        cv::Mat probOut = (*session_)(batchMat);

        // 后处理
        std::vector<std::pair<std::string, float>> clsResult = postprocess(probOut);

        // 保存结果并旋转图像（如果需要）
        for (size_t rno = 0; rno < clsResult.size(); ++rno) {
            const auto& [label, score] = clsResult[rno];
            size_t originalIdx = indices[begImgNo + rno];
            clsRes[originalIdx] = {label, score};

            // 如果是180度且置信度高于阈值，旋转图像
            if (label.find("180") != std::string::npos && score > config_.clsThresh) {
                cv::rotate(imgListCopy[originalIdx], imgListCopy[originalIdx],
                          cv::ROTATE_180);
            }
        }
    }

    // 填充输出
    output.imgList = imgListCopy;
    output.clsRes = clsRes;

    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = endTime - startTime;
    output.elapse = elapsed.count();

    return output;
}

cv::Mat TextClassifier::resizeNormImg(const cv::Mat& img) {
    int imgC = config_.clsImageShape[0];
    int imgH = config_.clsImageShape[1];
    int imgW = config_.clsImageShape[2];

    int h = img.rows;
    int w = img.cols;
    float ratio = static_cast<float>(w) / static_cast<float>(h);

    int resizedW;
    if (std::ceil(imgH * ratio) > imgW) {
        resizedW = imgW;
    } else {
        resizedW = static_cast<int>(std::ceil(imgH * ratio));
    }

    // 调整大小
    cv::Mat resizedImage;
    cv::resize(img, resizedImage, cv::Size(resizedW, imgH));

    // 转换为float
    resizedImage.convertTo(resizedImage, CV_32F);

    // 转换为CHW格式并归一化
    cv::Mat normalizedImg;
    if (imgC == 1) {
        // 灰度图
        resizedImage = resizedImage / 255.0f;
        normalizedImg = resizedImage;
    } else {
        // 彩色图：转换为CHW格式
        resizedImage = resizedImage / 255.0f;

        std::vector<cv::Mat> channels;
        cv::split(resizedImage, channels);

        // 创建CHW格式的Mat
        normalizedImg = cv::Mat(imgC * imgH, resizedW, CV_32F);
        for (int c = 0; c < imgC; ++c) {
            cv::Mat roi = normalizedImg(cv::Rect(0, c * imgH, resizedW, imgH));
            channels[c].copyTo(roi);
        }
    }

    // 归一化：减0.5除以0.5
    normalizedImg = (normalizedImg - 0.5f) / 0.5f;

    // 创建padding后的图像
    cv::Mat paddingImg = cv::Mat::zeros(imgC * imgH, imgW, CV_32F);
    normalizedImg.copyTo(paddingImg(cv::Rect(0, 0, resizedW, imgC * imgH)));

    // 重新整形为 [C, H, W] 的连续内存布局
    cv::Mat result(std::vector<int>{imgC, imgH, imgW}, CV_32F);

    for (int c = 0; c < imgC; ++c) {
        cv::Mat srcChannel = paddingImg(cv::Rect(0, c * imgH, imgW, imgH));
        cv::Mat dstChannel(imgH, imgW, CV_32F,
                          result.ptr<float>() + c * imgH * imgW);
        srcChannel.copyTo(dstChannel);
    }

    return result;
}

std::vector<std::pair<std::string, float>> TextClassifier::postprocess(const cv::Mat& preds) {
    // preds 的形状应该是 [N, num_classes]
    std::vector<std::pair<std::string, float>> results;

    if (preds.empty() || preds.dims < 2) {
        return results;
    }

    int batchSize = preds.size[0];
    int numClasses = preds.size[1];

    results.reserve(batchSize);

    for (int i = 0; i < batchSize; ++i) {
        // 找到最大值的索引
        const float* rowPtr = preds.ptr<float>(i);
        int maxIdx = 0;
        float maxVal = rowPtr[0];

        for (int j = 1; j < numClasses; ++j) {
            if (rowPtr[j] > maxVal) {
                maxVal = rowPtr[j];
                maxIdx = j;
            }
        }

        // 获取标签
        std::string label;
        if (maxIdx >= 0 && maxIdx < static_cast<int>(config_.labelList.size())) {
            label = config_.labelList[maxIdx];
        } else {
            label = std::to_string(maxIdx);
        }

        results.emplace_back(label, maxVal);
    }

    return results;
}

} // namespace RapidOCR
