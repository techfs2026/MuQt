#include "rapidocr.h"
#include <QFileInfo>
#include <QDebug>
#include <algorithm>

namespace RapidOCR {

RapidOCR::RapidOCR()
    : initialized_(false)
{
    // 默认配置
}

RapidOCR::RapidOCR(const RapidOCRConfig& config)
    : config_(config)
    , initialized_(false)
{
}

RapidOCR::~RapidOCR()
{
}

bool RapidOCR::initialize(const QString& modelDir)
{
    return initializeInternal(modelDir);
}

bool RapidOCR::initializeInternal(const QString& modelDir)
{
    qInfo() << "RapidOCR: Starting initialization...";

    config_.modelDir = modelDir;

    // 检查模型文件
    QString detModelPath = modelDir + "/ch_PP-OCRv5_server_det.onnx";
    QString clsModelPath = modelDir + "/ch_ppocr_mobile_v2.0_cls_infer.onnx";
    QString recModelPath = modelDir + "/ch_PP-OCRv5_rec_server_infer.onnx";
    QString keysPath = modelDir + "/ppocrv5_dict.txt";

    QStringList missingFiles;
    if (!QFileInfo::exists(detModelPath)) missingFiles << "检测模型";
    if (!QFileInfo::exists(clsModelPath)) missingFiles << "分类模型";
    if (!QFileInfo::exists(recModelPath)) missingFiles << "识别模型";
    if (!QFileInfo::exists(keysPath)) missingFiles << "字符集文件";

    if (!missingFiles.isEmpty()) {
        setError("缺少文件: " + missingFiles.join(", "));
        return false;
    }

    try {
        // 创建ORT配置
        OrtConfig detOrtConfig(detModelPath.toStdString(), 4, 1, false, 0, false);
        OrtConfig clsOrtConfig(clsModelPath.toStdString(), 4, 1, false, 0, false);
        OrtConfig recOrtConfig(recModelPath.toStdString(), 4, 1, false, 0, false);

        // 创建推理会话
        qInfo() << "RapidOCR: Creating detection session...";
        detSession_ = std::make_unique<OrtInferSession>(detOrtConfig);

        qInfo() << "RapidOCR: Creating classification session...";
        clsSession_ = std::make_unique<OrtInferSession>(clsOrtConfig);

        qInfo() << "RapidOCR: Creating recognition session...";
        recSession_ = std::make_unique<OrtInferSession>(recOrtConfig);

        // 配置各模块
        config_.detConfig.modelPath = detModelPath.toStdString();
        config_.detConfig.limitSideLen = 960;
        config_.detConfig.thresh = 0.3f;
        config_.detConfig.boxThresh = 0.5f;
        config_.detConfig.unclipRatio = 1.6f;
        config_.detConfig.useDilation = true;
        config_.detConfig.scoreMode = "fast";

        config_.clsConfig.modelPath = clsModelPath.toStdString();
        config_.clsConfig.clsImageShape = {3, 48, 192};
        config_.clsConfig.clsBatchNum = 6;
        config_.clsConfig.clsThresh = 0.9f;

        config_.recConfig.modelPath = recModelPath.toStdString();
        config_.recConfig.keysPath = keysPath.toStdString();
        config_.recConfig.recImageShape = {3, 48, 320};
        config_.recConfig.recBatchNum = 6;

        // 创建检测器、分类器、识别器
        qInfo() << "RapidOCR: Creating text detector...";
        textDet_ = std::make_unique<TextDetector>(config_.detConfig, detSession_.get());

        qInfo() << "RapidOCR: Creating text classifier...";
        textCls_ = std::make_unique<TextClassifier>(config_.clsConfig, clsSession_.get());

        qInfo() << "RapidOCR: Creating text recognizer...";
        textRec_ = std::make_unique<TextRecognizer>(config_.recConfig, recSession_.get());

        initialized_ = true;
        qInfo() << "RapidOCR: Initialization successful";
        return true;

    } catch (const std::exception& e) {
        setError(QString("初始化失败: %1").arg(QString::fromStdString(e.what())));
        return false;
    }
}

RapidOCROutput RapidOCR::operator()(const QImage& img)
{
    if (!initialized_) {
        setError("RapidOCR未初始化");
        return RapidOCROutput();
    }

    try {
        cv::Mat oriImg = loadImg_(img);
        return (*this)(oriImg);
    } catch (const std::exception& e) {
        setError(QString("加载图像失败: %1").arg(QString::fromStdString(e.what())));
        return RapidOCROutput();
    }
}

RapidOCROutput RapidOCR::operator()(const cv::Mat& oriImg)
{
    if (!initialized_) {
        setError("RapidOCR未初始化");
        return RapidOCROutput();
    }

    if (oriImg.empty()) {
        setError("输入图像为空");
        return RapidOCROutput();
    }

    try {
        // 预处理图像
        auto [img, opRecord] = preprocessImg(oriImg);

        // 运行OCR步骤
        auto [detRes, clsRes, recRes, croppedImgList] = runOcrSteps(img, opRecord);

        // 构建最终输出
        return buildFinalOutput(oriImg, detRes, clsRes, recRes, croppedImgList, opRecord);

    } catch (const std::exception& e) {
        setError(QString("OCR识别失败: %1").arg(QString::fromStdString(e.what())));
        return RapidOCROutput();
    }
}

RapidOCROutput RapidOCR::operator()(const std::string& imgPath)
{
    if (!initialized_) {
        setError("RapidOCR未初始化");
        return RapidOCROutput();
    }

    try {
        cv::Mat oriImg = loadImg_(imgPath);
        return (*this)(oriImg);
    } catch (const std::exception& e) {
        setError(QString("加载图像失败: %1").arg(QString::fromStdString(e.what())));
        return RapidOCROutput();
    }
}

void RapidOCR::updateParams(
    std::optional<bool> useDet,
    std::optional<bool> useCls,
    std::optional<bool> useRec,
    bool returnWordBox,
    bool returnSingleCharBox,
    float textScore,
    float boxThresh,
    float unclipRatio)
{
    if (useDet.has_value()) config_.useDet = *useDet;
    if (useCls.has_value()) config_.useCls = *useCls;
    if (useRec.has_value()) config_.useRec = *useRec;

    config_.returnWordBox = returnWordBox;
    config_.returnSingleCharBox = returnSingleCharBox;
    config_.textScore = textScore;

    if (textDet_) {
        config_.detConfig.boxThresh = boxThresh;
        config_.detConfig.unclipRatio = unclipRatio;
        // 重新创建检测器以应用新配置
        textDet_ = std::make_unique<TextDetector>(config_.detConfig, detSession_.get());
    }
}

std::tuple<cv::Mat, OpRecord> RapidOCR::preprocessImg(const cv::Mat& oriImg)
{
    OpRecord opRecord;

    auto [img, ratioH, ratioW] = ProcessImage::resizeImageWithinBounds(
        oriImg,
        config_.minSideLen,
        config_.maxSideLen
    );

    std::map<std::string, std::any> preprocessInfo;
    preprocessInfo["ratio_h"] = ratioH;
    preprocessInfo["ratio_w"] = ratioW;
    opRecord["preprocess"] = preprocessInfo;

    return {img, opRecord};
}

std::tuple<TextDetOutput, TextClsOutput, TextRecOutput, std::vector<cv::Mat>>
RapidOCR::runOcrSteps(const cv::Mat& img, const OpRecord& opRecord)
{
    TextDetOutput detRes;
    TextClsOutput clsRes;
    TextRecOutput recRes;
    std::vector<cv::Mat> croppedImgList;

    // 检测步骤
    if (config_.useDet) {
        try {
            OpRecord mutableOpRecord = opRecord;
            auto [cropped, det] = detectAndCrop(img, mutableOpRecord);
            croppedImgList = cropped;
            detRes = det;
        } catch (const RapidOCRException& e) {
            qWarning() << "Detection failed:" << e.what();
            return {TextDetOutput(), TextClsOutput(), TextRecOutput(), {}};
        }
    } else {
        croppedImgList.push_back(img.clone());
    }

    // 分类步骤
    std::vector<cv::Mat> clsImgList;
    if (config_.useCls) {
        try {
            auto [cls, clsResult] = clsAndRotate(croppedImgList);
            clsImgList = cls;
            clsRes = clsResult;
        } catch (const RapidOCRException& e) {
            qWarning() << "Classification failed:" << e.what();
            return {detRes, TextClsOutput(), TextRecOutput(), croppedImgList};
        }
    } else {
        clsImgList = croppedImgList;
    }

    // 识别步骤
    if (config_.useRec) {
        try {
            recRes = recognizeText(clsImgList);
        } catch (const RapidOCRException& e) {
            qWarning() << "Recognition failed:" << e.what();
            return {detRes, clsRes, TextRecOutput(), croppedImgList};
        }
    }

    return {detRes, clsRes, recRes, croppedImgList};
}

std::tuple<std::vector<cv::Mat>, TextDetOutput>
RapidOCR::detectAndCrop(const cv::Mat& img, OpRecord& opRecord)
{
    // 应用垂直padding
    auto [paddedImg, updatedOpRecord] = ProcessImage::applyVerticalPadding(
        img,
        opRecord,
        config_.widthHeightRatio,
        config_.minHeight
    );
    opRecord = updatedOpRecord;

    // 执行检测
    TextDetOutput detRes = (*textDet_)(paddedImg);

    if (detRes.boxes.empty()) {
        throw RapidOCRException("检测结果为空");
    }

    // 裁剪文本区域
    std::vector<cv::Mat> imgCropList = cropTextRegions(paddedImg, detRes.boxes);

    return {imgCropList, detRes};
}

std::vector<cv::Mat> RapidOCR::cropTextRegions(
    const cv::Mat& img,
    const std::vector<std::vector<cv::Point>>& boxes)
{
    std::vector<cv::Mat> imgCropList;
    imgCropList.reserve(boxes.size());

    for (const auto& box : boxes) {
        // 转换为cv::Mat格式
        cv::Mat boxMat(4, 2, CV_32F);
        for (int i = 0; i < 4; ++i) {
            boxMat.at<float>(i, 0) = box[i].x;
            boxMat.at<float>(i, 1) = box[i].y;
        }

        cv::Mat imgCrop = ProcessImage::getRotateCropImage(img, boxMat);
        if (!imgCrop.empty()) {
            imgCropList.push_back(imgCrop);
        }
    }

    return imgCropList;
}

std::tuple<std::vector<cv::Mat>, TextClsOutput>
RapidOCR::clsAndRotate(const std::vector<cv::Mat>& imgList)
{
    TextClsOutput clsRes = (*textCls_)(imgList);

    if (clsRes.imgList.empty()) {
        throw RapidOCRException("分类结果为空");
    }

    return {clsRes.imgList, clsRes};
}

TextRecOutput RapidOCR::recognizeText(const std::vector<cv::Mat>& imgList)
{
    TextRecOutput recRes = (*textRec_)(imgList, config_.returnWordBox);

    if (recRes.txts.empty()) {
        throw RapidOCRException("识别结果为空");
    }

    return recRes;
}

RapidOCROutput RapidOCR::buildFinalOutput(
    const cv::Mat& oriImg,
    TextDetOutput& detRes,
    TextClsOutput& clsRes,
    TextRecOutput& recRes,
    const std::vector<cv::Mat>& croppedImgList,
    const OpRecord& opRecord)
{
    int oriH = oriImg.rows;
    int oriW = oriImg.cols;

    // 将检测框映射回原始图像坐标
    if (!detRes.boxes.empty()) {
        cv::Mat boxesMat(detRes.boxes.size(), 4, CV_32FC2);
        for (size_t i = 0; i < detRes.boxes.size(); ++i) {
            for (size_t j = 0; j < 4; ++j) {
                boxesMat.at<cv::Vec2f>(i, j)[0] = detRes.boxes[i][j].x;
                boxesMat.at<cv::Vec2f>(i, j)[1] = detRes.boxes[i][j].y;
            }
        }

        cv::Mat mappedBoxes = ProcessImage::mapBoxesToOriginal(
            boxesMat, opRecord, oriH, oriW
        );

        // 转换回vector格式
        std::vector<std::vector<cv::Point>> newBoxes;
        for (int i = 0; i < mappedBoxes.rows; ++i) {
            std::vector<cv::Point> box;
            for (int j = 0; j < 4; ++j) {
                cv::Vec2f pt = mappedBoxes.at<cv::Vec2f>(i, j);
                box.push_back(cv::Point(pt[0], pt[1]));
            }
            newBoxes.push_back(box);
        }
        detRes.boxes = newBoxes;
    }

    // 过滤空文本结果
    if (!recRes.txts.empty() && !detRes.boxes.empty() && !detRes.scores.empty()) {
        std::vector<size_t> emptyIds;
        for (size_t i = 0; i < recRes.txts.size(); ++i) {
            QString txt = QString::fromStdString(recRes.txts[i]).trimmed();
            if (txt.isEmpty()) {
                emptyIds.push_back(i);
            }
        }

        if (!emptyIds.empty()) {
            std::vector<std::vector<cv::Point>> newBoxes;
            std::vector<float> newScores;
            std::vector<std::string> newTxts;
            std::vector<WordInfo> newWordResults;

            for (size_t i = 0; i < detRes.boxes.size(); ++i) {
                if (std::find(emptyIds.begin(), emptyIds.end(), i) == emptyIds.end()) {
                    newBoxes.push_back(detRes.boxes[i]);
                    newScores.push_back(detRes.scores[i]);
                    newTxts.push_back(recRes.txts[i]);
                    if (i < recRes.wordResults.size()) {
                        newWordResults.push_back(recRes.wordResults[i]);
                    }
                }
            }

            detRes.boxes = newBoxes;
            detRes.scores = newScores;
            recRes.txts = newTxts;
            recRes.wordResults = newWordResults;
        }
    }

    // 仅分类结果
    if (detRes.boxes.empty() && recRes.txts.empty() && !clsRes.clsRes.empty()) {
        // TODO: 返回分类结果
        return RapidOCROutput();
    }

    // 无有效输出
    if (detRes.boxes.empty() && recRes.txts.empty()) {
        return RapidOCROutput();
    }

    // 仅识别结果(无检测)
    if (detRes.boxes.empty() && !recRes.txts.empty()) {
        // TODO: 返回识别结果
        return RapidOCROutput();
    }

    // 仅检测结果(无识别)
    if (!detRes.boxes.empty() && recRes.txts.empty()) {
        // TODO: 返回检测结果
        return RapidOCROutput();
    }

    // 计算词语框
    if (config_.returnWordBox && !detRes.boxes.empty() && !recRes.wordResults.empty()) {
        recRes.wordResults = calcWordBoxes(
            croppedImgList,
            detRes.boxes,
            recRes,
            opRecord,
            oriH,
            oriW
        );
    }

    // 转换为cv::Point2f格式用于输出
    std::vector<std::vector<cv::Point2f>> boxes2f;
    for (const auto& box : detRes.boxes) {
        std::vector<cv::Point2f> box2f;
        for (const auto& pt : box) {
            box2f.push_back(cv::Point2f(pt.x, pt.y));
        }
        boxes2f.push_back(box2f);
    }

    // 构建输出
    RapidOCROutput output;
    output.boxes = boxes2f;
    output.txts = recRes.txts;
    output.scores = recRes.scores;
    output.elapseList = {detRes.elapse, clsRes.elapse, recRes.elapse};

    // 按文本置信度过滤
    output = filterByTextScore(output);

    return output.size() > 0 ? output : RapidOCROutput();
}

std::vector<WordInfo> RapidOCR::calcWordBoxes(
    const std::vector<cv::Mat>& imgs,
    const std::vector<std::vector<cv::Point>>& dtBoxes,
    TextRecOutput& recRes,
    const OpRecord& opRecord,
    int rawH,
    int rawW)
{
    // 调用CalRecBoxes计算词语框（在裁剪后图像坐标系中）
    TextRecOutput updatedRecRes = calRecBoxes_(imgs, dtBoxes, recRes, config_.returnSingleCharBox);

    // 将词语框映射回原始图像坐标
    std::vector<WordInfo> originWords;
    originWords.reserve(updatedRecRes.wordResults.size());

    for (const auto& wordInfo : updatedRecRes.wordResults) {
        WordInfo mappedWordInfo = wordInfo;

        // 如果没有词语框，直接添加
        if (wordInfo.wordBoxes.empty()) {
            originWords.push_back(mappedWordInfo);
            continue;
        }

        // 映射每个词语的边界框到原始坐标
        std::vector<std::vector<cv::Point>> mappedWordBoxes;
        mappedWordBoxes.reserve(wordInfo.wordBoxes.size());

        for (const auto& wordBox : wordInfo.wordBoxes) {
            if (wordBox.empty() || wordBox.size() != 4) {
                mappedWordBoxes.push_back(wordBox);
                continue;
            }

            // 转换为cv::Mat格式进行映射
            // 创建一个 1x4 的 CV_32FC2 矩阵（每个点有x,y两个坐标）
            cv::Mat boxMat(1, 4, CV_32FC2);
            for (size_t i = 0; i < 4; ++i) {
                boxMat.at<cv::Vec2f>(0, i)[0] = static_cast<float>(wordBox[i].x);
                boxMat.at<cv::Vec2f>(0, i)[1] = static_cast<float>(wordBox[i].y);
            }

            // 映射到原始坐标
            cv::Mat mappedBoxMat;
            try {
                mappedBoxMat = ProcessImage::mapBoxesToOriginal(
                    boxMat, opRecord, rawH, rawW
                    );
            } catch (const std::exception& e) {
                qWarning() << "Failed to map word box:" << e.what();
                mappedWordBoxes.push_back(wordBox);
                continue;
            }

            // 转换回vector<cv::Point>
            std::vector<cv::Point> mappedBox;
            for (int i = 0; i < mappedBoxMat.cols; ++i) {
                cv::Vec2f pt = mappedBoxMat.at<cv::Vec2f>(0, i);

                // 四舍五入并限制在图像范围内
                int x = std::clamp(
                    static_cast<int>(std::round(pt[0])),
                    0,
                    rawW - 1
                    );
                int y = std::clamp(
                    static_cast<int>(std::round(pt[1])),
                    0,
                    rawH - 1
                    );

                mappedBox.push_back(cv::Point(x, y));
            }

            mappedWordBoxes.push_back(mappedBox);
        }

        mappedWordInfo.wordBoxes = mappedWordBoxes;
        originWords.push_back(mappedWordInfo);
    }

    return originWords;
}

RapidOCROutput RapidOCR::filterByTextScore(RapidOCROutput& ocrRes)
{
    if (!ocrRes.hasValidData()) {
        return ocrRes;
    }

    std::vector<std::vector<cv::Point2f>> filterBoxes;
    std::vector<std::string> filterTxts;
    std::vector<float> filterScores;

    for (size_t i = 0; i < ocrRes.boxes->size(); ++i) {
        if ((*ocrRes.scores)[i] < config_.textScore) {
            continue;
        }

        filterBoxes.push_back((*ocrRes.boxes)[i]);
        filterTxts.push_back((*ocrRes.txts)[i]);
        filterScores.push_back((*ocrRes.scores)[i]);
    }

    RapidOCROutput filtered;
    if (!filterBoxes.empty()) {
        filtered.boxes = filterBoxes;
        filtered.txts = filterTxts;
        filtered.scores = filterScores;
        filtered.elapseList = ocrRes.elapseList;
    }

    return filtered;
}

void RapidOCR::setError(const QString& error)
{
    lastError_ = error;
    qWarning() << "RapidOCR error:" << error;
}

} // namespace RapidOCR
