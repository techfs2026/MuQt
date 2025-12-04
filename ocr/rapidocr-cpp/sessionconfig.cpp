#include "sessionconfig.h"
#include <algorithm>

namespace RapidOCR {

void ProviderConfig::addProvider(ProviderType type, int deviceId) {
    // 避免重复添加
    auto it = std::find(providers_.begin(), providers_.end(), type);
    if (it == providers_.end()) {
        providers_.push_back(type);
        if (type == ProviderType::CUDA) {
            deviceId_ = deviceId;
        }
    }
}

bool ProviderConfig::isProviderAvailable(ProviderType type) {
    switch (type) {
    case ProviderType::CPU:
        return true;  // CPU总是可用

    case ProviderType::CUDA:
        // TODO: 检测CUDA是否可用
        // 可以通过尝试创建CUDA提供者来检测
        return false;

    default:
        return false;
    }
}

std::string ProviderConfig::getProviderName(ProviderType type) {
    switch (type) {
    case ProviderType::CPU:
        return "CPUExecutionProvider";
    case ProviderType::CUDA:
        return "CUDAExecutionProvider";
    default:
        return "UnknownProvider";
    }
}

} // namespace RapidOCR
