#include "ortinfersession.h"
#include <algorithm>
#include <sstream>

namespace RapidOCR {

OrtInferSession::OrtInferSession(const OrtConfig& config)
    : memoryInfo_(Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU))
{
    // 验证模型文件
    verifyModel(config.modelPath);

    // 创建环境
    env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_ERROR, "RapidOCR");

    // 初始化会话选项
    sessionOptions_ = std::make_unique<Ort::SessionOptions>();
    initSessionOptions(config);

    // 初始化执行提供者
    initProviders(config);

    // 创建会话
    try {
#ifdef _WIN32
        std::wstring wModelPath(config.modelPath.begin(), config.modelPath.end());
        session_ = std::make_unique<Ort::Session>(*env_, wModelPath.c_str(), *sessionOptions_);
#else
        session_ = std::make_unique<Ort::Session>(*env_, config.modelPath.c_str(), *sessionOptions_);
#endif
    } catch (const Ort::Exception& e) {
        throw ONNXRuntimeError(std::string("Failed to create ONNX session: ") + e.what());
    }

    // 获取输入输出节点名称
    Ort::AllocatorWithDefaultOptions allocator;

    // 获取输入节点
    size_t numInputNodes = session_->GetInputCount();
    inputNames_.reserve(numInputNodes);
    inputNamesCStr_.reserve(numInputNodes);

    for (size_t i = 0; i < numInputNodes; ++i) {
        auto inputName = session_->GetInputNameAllocated(i, allocator);
        inputNames_.push_back(inputName.get());
        inputNamesCStr_.push_back(inputNames_.back().c_str());
    }

    // 获取输出节点
    size_t numOutputNodes = session_->GetOutputCount();
    outputNames_.reserve(numOutputNodes);
    outputNamesCStr_.reserve(numOutputNodes);

    for (size_t i = 0; i < numOutputNodes; ++i) {
        auto outputName = session_->GetOutputNameAllocated(i, allocator);
        outputNames_.push_back(outputName.get());
        outputNamesCStr_.push_back(outputNames_.back().c_str());
    }

    // 获取自定义元数据
    try {
        Ort::ModelMetadata metadata = session_->GetModelMetadata();
        Ort::AllocatorWithDefaultOptions metaAllocator;

        // 获取所有自定义元数据键
        auto keysAllocated = metadata.GetCustomMetadataMapKeysAllocated(metaAllocator);

        for (size_t i = 0; i < keysAllocated.size(); ++i) {
            std::string key = keysAllocated[i].get();
            auto valueAllocated = metadata.LookupCustomMetadataMapAllocated(key.c_str(), metaAllocator);
            if (valueAllocated) {
                customMetadata_[key] = valueAllocated.get();
            }
        }
    } catch (const Ort::Exception& e) {
        // 某些模型可能没有元数据，忽略错误
    }
}

void OrtInferSession::initSessionOptions(const OrtConfig& config) {
    // 设置日志级别
    sessionOptions_->SetLogSeverityLevel(4);  // ORT_LOGGING_LEVEL_ERROR

    // 设置图优化级别
    sessionOptions_->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    // 设置CPU内存池
    if (config.useCpuMemArena) {
        sessionOptions_->EnableCpuMemArena();
    } else {
        sessionOptions_->DisableCpuMemArena();
    }

    // 设置线程数
    int cpuNums = std::thread::hardware_concurrency();

    if (config.intraOpNumThreads != -1 &&
        config.intraOpNumThreads >= 1 &&
        config.intraOpNumThreads <= cpuNums) {
        sessionOptions_->SetIntraOpNumThreads(config.intraOpNumThreads);
    }

    if (config.interOpNumThreads != -1 &&
        config.interOpNumThreads >= 1 &&
        config.interOpNumThreads <= cpuNums) {
        sessionOptions_->SetInterOpNumThreads(config.interOpNumThreads);
    }
}

void OrtInferSession::initProviders(const OrtConfig& config) {
    if (config.useGpu) {
        // 使用CUDA提供者
        OrtCUDAProviderOptions cudaOptions;
        cudaOptions.device_id = config.gpuDeviceId;

        try {
            sessionOptions_->AppendExecutionProvider_CUDA(cudaOptions);
        } catch (const Ort::Exception& e) {
            // CUDA不可用，回退到CPU
            // LOG: CUDA provider is not available, falling back to CPU
        }
    }

    // CPU提供者总是可用的
    // sessionOptions_->AppendExecutionProvider_CPU();  // 默认已添加
}

cv::Mat OrtInferSession::operator()(const cv::Mat& inputContent) {
    try {
        // 转换为tensor
        Ort::Value inputTensor = matToTensor(inputContent);

        // 执行推理
        auto outputTensors = session_->Run(
            Ort::RunOptions{nullptr},
            inputNamesCStr_.data(),
            &inputTensor,
            inputNamesCStr_.size(),
            outputNamesCStr_.data(),
            outputNamesCStr_.size()
        );

        // 转换输出tensor为cv::Mat
        return tensorToMat(outputTensors[0]);

    } catch (const Ort::Exception& e) {
        std::stringstream ss;
        ss << "ONNX Runtime inference error: " << e.what();
        throw ONNXRuntimeError(ss.str());
    } catch (const std::exception& e) {
        throw ONNXRuntimeError(std::string("Inference error: ") + e.what());
    }
}


std::vector<std::string> OrtInferSession::getInputNames() const {
    return inputNames_;
}

std::vector<std::string> OrtInferSession::getOutputNames() const {
    return outputNames_;
}

std::vector<std::string> OrtInferSession::getCharacterList(const std::string& key) const {
    auto it = customMetadata_.find(key);
    if (it == customMetadata_.end()) {
        return {};
    }

    // 按行分割字符串
    std::vector<std::string> lines;
    std::string value = it->second;
    std::stringstream ss(value);
    std::string line;

    while (std::getline(ss, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }

    return lines;
}

bool OrtInferSession::haveKey(const std::string& key) const {
    return customMetadata_.find(key) != customMetadata_.end();
}

Ort::Value OrtInferSession::matToTensor(const cv::Mat& mat) {
    // 确保Mat是连续的
    cv::Mat continuousMat = mat.isContinuous() ? mat : mat.clone();

    // 获取Mat的尺寸信息
    std::vector<int64_t> inputShape;

    if (continuousMat.dims == 2) {
        // 2D图像: [H, W] -> [1, C, H, W]
        inputShape = {
            1,
            static_cast<int64_t>(continuousMat.channels()),
            static_cast<int64_t>(continuousMat.rows),
            static_cast<int64_t>(continuousMat.cols)
        };
    } else if (continuousMat.dims == 3) {
        // 3D数据: [C, H, W] -> 直接使用
        inputShape.resize(3);
        for (int i = 0; i < 3; ++i) {
            inputShape[i] = continuousMat.size[i];
        }
    } else if (continuousMat.dims == 4) {
        // ✅ 新增：4D数据支持 [N, C, H, W]
        inputShape.resize(4);
        for (int i = 0; i < 4; ++i) {
            inputShape[i] = continuousMat.size[i];
        }
    } else {
        throw ONNXRuntimeError("Unsupported Mat dimensions: " + std::to_string(continuousMat.dims));
    }

    // 计算元素总数
    size_t totalElements = 1;
    for (auto dim : inputShape) {
        totalElements *= dim;
    }

    // 创建tensor
    Ort::Value tensor = Ort::Value::CreateTensor<float>(
        memoryInfo_,
        reinterpret_cast<float*>(continuousMat.data),
        totalElements,
        inputShape.data(),
        inputShape.size()
        );

    return tensor;
}

cv::Mat OrtInferSession::tensorToMat(Ort::Value& tensor) {
    auto tensorInfo = tensor.GetTensorTypeAndShapeInfo();
    auto shape = tensorInfo.GetShape();
    auto elementType = tensorInfo.GetElementType();

    if (elementType != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        throw ONNXRuntimeError("Unsupported tensor element type, expected float");
    }

    float* tensorData = tensor.GetTensorMutableData<float>();

    if (shape.size() == 1) {
        // 1D tensor
        int size = static_cast<int>(shape[0]);
        return cv::Mat(1, size, CV_32F, tensorData).clone();

    } else if (shape.size() == 2) {
        // 2D tensor: [rows, cols]
        int rows = static_cast<int>(shape[0]);
        int cols = static_cast<int>(shape[1]);
        return cv::Mat(rows, cols, CV_32F, tensorData).clone();

    } else if (shape.size() == 3) {
        // 3D tensor: 直接构建3D Mat，不做转换
        int dims[] = {
            static_cast<int>(shape[0]),
            static_cast<int>(shape[1]),
            static_cast<int>(shape[2])
        };
        return cv::Mat(3, dims, CV_32F, tensorData).clone();

    } else if (shape.size() == 4) {
        // 4D tensor: 直接构建4D Mat
        int dims[] = {
            static_cast<int>(shape[0]),
            static_cast<int>(shape[1]),
            static_cast<int>(shape[2]),
            static_cast<int>(shape[3])
        };
        return cv::Mat(4, dims, CV_32F, tensorData).clone();

    } else {
        throw ONNXRuntimeError(
            "Unsupported tensor shape dimensions: " + std::to_string(shape.size())
            );
    }
}

} // namespace RapidOCR
