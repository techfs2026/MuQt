#ifndef RAPIDOCR_ORT_INFER_SESSION_H
#define RAPIDOCR_ORT_INFER_SESSION_H

#include "infersession.h"
#include <onnxruntime_cxx_api.h>
#include <memory>
#include <stdexcept>

namespace RapidOCR {

class ONNXRuntimeError : public std::runtime_error {
public:
    explicit ONNXRuntimeError(const std::string& msg)
        : std::runtime_error(msg) {}
};

// ONNXRuntime配置结构
struct OrtConfig {
    std::string modelPath;
    int intraOpNumThreads = -1;
    int interOpNumThreads = -1;
    bool useGpu = false;
    int gpuDeviceId = 0;
    bool useCpuMemArena = false;

    // 添加构造函数
    OrtConfig() = default;

    OrtConfig(const std::string& path,
              int intraThreads = 4,
              int interThreads = 1,
              bool gpu = false,
              int gpuId = 0,
              bool cpuArena = false)
        : modelPath(path)
        , intraOpNumThreads(intraThreads)
        , interOpNumThreads(interThreads)
        , useGpu(gpu)
        , gpuDeviceId(gpuId)
        , useCpuMemArena(cpuArena)
    {}
};

class OrtInferSession : public InferSession {
public:
    // 使用配置构造
    explicit OrtInferSession(const OrtConfig& config);

    // 析构函数
    ~OrtInferSession() override = default;

    // 执行推理
    cv::Mat operator()(const cv::Mat& inputContent) override;

    // 获取输入节点名称
    std::vector<std::string> getInputNames() const override;

    // 获取输出节点名称
    std::vector<std::string> getOutputNames() const override;

    // 获取字符列表（用于识别器）
    std::vector<std::string> getCharacterList(const std::string& key = "character") const override;

    // 检查是否有指定的key
    bool haveKey(const std::string& key = "character") const override;

private:
    // 初始化会话选项
    void initSessionOptions(const OrtConfig& config);

    // 初始化执行提供者（CPU/GPU）
    void initProviders(const OrtConfig& config);

    // cv::Mat转换为ONNX tensor
    Ort::Value matToTensor(const cv::Mat& mat);

    // ONNX tensor转换为cv::Mat
    cv::Mat tensorToMat(Ort::Value& tensor);

private:
    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::Session> session_;
    std::unique_ptr<Ort::SessionOptions> sessionOptions_;
    Ort::MemoryInfo memoryInfo_;

    std::vector<std::string> inputNames_;
    std::vector<std::string> outputNames_;
    std::vector<const char*> inputNamesCStr_;
    std::vector<const char*> outputNamesCStr_;

    std::map<std::string, std::string> customMetadata_;
};

} // namespace RapidOCR

#endif // RAPIDOCR_ORT_INFER_SESSION_H
