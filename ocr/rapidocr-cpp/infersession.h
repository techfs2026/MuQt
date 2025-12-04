#ifndef RAPIDOCR_INFER_SESSION_H
#define RAPIDOCR_INFER_SESSION_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <opencv2/opencv.hpp>

namespace RapidOCR {

// 推理会话基类（抽象类）
class InferSession {
public:
    virtual ~InferSession() = default;

    // 执行推理
    virtual cv::Mat operator()(const cv::Mat& inputContent) = 0;

    // 获取输入节点名称
    virtual std::vector<std::string> getInputNames() const = 0;

    // 获取输出节点名称
    virtual std::vector<std::string> getOutputNames() const = 0;

    // 获取字符列表（用于识别器）
    virtual std::vector<std::string> getCharacterList(const std::string& key = "character") const = 0;

    // 检查是否有指定的key
    virtual bool haveKey(const std::string& key = "character") const = 0;

protected:
    // 验证模型文件是否存在
    static void verifyModel(const std::string& modelPath);
};

} // namespace RapidOCR

#endif // RAPIDOCR_INFER_SESSION_H
