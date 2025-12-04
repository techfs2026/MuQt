#ifndef RAPIDOCR_TEXT_RECOGNIZER_H
#define RAPIDOCR_TEXT_RECOGNIZER_H

#include "ortinfersession.h"
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <memory>

namespace RapidOCR {

// 词语类型枚举
enum class WordType {
    CN,      // 中文
    EN,      // 英文
    NUM,     // 数字
    EN_NUM   // 英文和数字
};

// 词语信息
struct WordInfo {
    std::vector<std::vector<std::string>> words;      // 词语列表
    std::vector<std::vector<int>> wordCols;           // 词语列位置
    std::vector<WordType> wordTypes;                   // 词语类型
    float lineTxtLen = 0.0f;                          // 行文本长度
    std::vector<float> confs;                         // 置信度列表

    std::vector<std::vector<cv::Point>> wordBoxes;    // 词语边界框列表
};

// 识别结果输出结构
struct TextRecOutput {
    std::vector<cv::Mat> imgs;                        // 输入图像列表
    std::vector<std::string> txts;                    // 识别文本列表
    std::vector<float> scores;                        // 置信度列表
    std::vector<WordInfo> wordResults;                // 词语级别结果
    double elapse = 0.0;                              // 耗时（秒）

    size_t size() const {
        return txts.size();
    }

    bool empty() const {
        return txts.empty();
    }
};

// 识别器配置
struct RecognizerConfig {
    std::string modelPath;                            // 模型路径
    std::string keysPath;                             // 字符字典路径（可选）
    int recBatchNum = 6;                              // 批处理大小
    std::vector<int> recImageShape = {3, 48, 320};   // 输入图像形状 [C, H, W]

    // OnnxRuntime 配置
    int numThreads = 0;
    bool useGpu = false;
    int gpuDeviceId = 0;
};

// CTC标签解码器
class CTCLabelDecode {
public:
    CTCLabelDecode(const std::vector<std::string>& character);
    CTCLabelDecode(const std::string& characterPath);

    // 解码预测结果
    std::pair<std::vector<std::pair<std::string, float>>, std::vector<WordInfo>>
    operator()(const cv::Mat& preds,
               bool returnWordBox = false,
               const std::vector<float>& whRatioList = {},
               float maxWhRatio = 1.0f);

    // 读取字符文件
    static std::vector<std::string> readCharacterFile(const std::string& path);

private:
    // 获取字符列表
    std::vector<std::string> getCharacter(const std::vector<std::string>& character);
    std::vector<std::string> getCharacter(const std::string& characterPath);



    // 插入特殊字符
    static void insertSpecialChar(std::vector<std::string>& charList,
                                  const std::string& specialChar,
                                  int loc = -1);

    // 解码
    std::pair<std::vector<std::pair<std::string, float>>, std::vector<WordInfo>>
    decode(const cv::Mat& textIndex,
           const cv::Mat& textProb,
           bool returnWordBox,
           const std::vector<float>& whRatioList,
           float maxWhRatio,
           bool removeDuplicate = true);

    // 获取词语信息
    static WordInfo getWordInfo(const std::string& text,
                                const std::vector<bool>& selection);

    // 获取忽略的token
    static std::vector<int> getIgnoredTokens();

private:
    std::vector<std::string> character_;
    std::map<std::string, int> dict_;
};

class TextRecognizer {
public:
    TextRecognizer(const RecognizerConfig& config, OrtInferSession* session);
    ~TextRecognizer() = default;

    // 执行文本识别
    TextRecOutput operator()(const std::vector<cv::Mat>& imgList,
                             bool returnWordBox = false);
    TextRecOutput operator()(const cv::Mat& img,
                             bool returnWordBox = false);

private:
    // 调整图像大小并归一化
    cv::Mat resizeNormImg(const cv::Mat& img, float maxWhRatio);

    // 获取字符字典
    std::vector<std::string> getCharacterDict();

private:
    RecognizerConfig config_;
    OrtInferSession* session_;
    std::unique_ptr<CTCLabelDecode> postprocessOp_;
};

} // namespace RapidOCR

#endif // RAPIDOCR_TEXT_RECOGNIZER_H
