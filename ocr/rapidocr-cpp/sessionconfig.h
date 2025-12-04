#ifndef RAPIDOCR_SESSION_CONFIG_H
#define RAPIDOCR_SESSION_CONFIG_H

#include <string>
#include <vector>

namespace RapidOCR {

// 执行提供者类型
enum class ProviderType {
    CPU,
    CUDA,
};

// 提供者配置辅助类
class ProviderConfig {
public:
    ProviderConfig() = default;

    // 添加执行提供者
    void addProvider(ProviderType type, int deviceId = 0);

    // 获取提供者列表
    const std::vector<ProviderType>& getProviders() const { return providers_; }

    // 获取设备ID
    int getDeviceId() const { return deviceId_; }

    // 验证提供者是否可用
    static bool isProviderAvailable(ProviderType type);

    // 获取提供者名称
    static std::string getProviderName(ProviderType type);

private:
    std::vector<ProviderType> providers_;
    int deviceId_ = 0;
};

} // namespace RapidOCR

#endif // RAPIDOCR_SESSION_CONFIG_H
