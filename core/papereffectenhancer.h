#ifndef PAPEREFFECTENHANCER_ADVANCED_H
#define PAPEREFFECTENHANCER_ADVANCED_H

#include <opencv2/opencv.hpp>
#include <QImage>
#include <QMutex>

struct AdvancedOptions {
    bool enabled = true;

    // 核心参数：背景纸张颜色 (米黄色)
    cv::Vec3b paperColor = cv::Vec3b(220, 248, 255); // BGR: 米黄色 #FFF8DC

    // 着色强度 (0.0 = 保持原样, 1.0 = 完全替换为纸张色)
    double colorIntensity = 0.7;

    // 文字/背景分离阈值 (0-255, 越高越多区域被认为是背景)
    // 设置为 0 表示启用自适应阈值
    int threshold = 0;  // 0 = 自适应

    // 边缘羽化半径 (避免文字边缘生硬, 0=不羽化)
    int featherRadius = 2;

    // 1. 自适应阈值
    bool useAdaptiveThreshold = true;
    double adaptiveThresholdRatio = 0.85;  // 阈值 = 平均亮度 × 此比例

    // 2. 纸张纹理
    bool enablePaperTexture = true;
    double textureIntensity = 0.03;  // 纹理强度 (0.02-0.05 推荐)

    // 3. 边缘对比度保护
    bool protectTextEdges = true;
    double edgeThreshold = 30.0;  // Canny边缘检测阈值

    // 4. 渐进式着色强度
    bool useProgressiveIntensity = true;
    double centerIntensity = 0.6;   // 中心区域着色强度
    double edgeIntensity = 0.8;     // 边缘区域着色强度

    // 预设颜色选项
    enum PaperPreset {
        WARM_WHITE,      // 暖白色 #FFF8DC
        CREAM,           // 奶油色 #FAEBD7
        LIGHT_YELLOW,    // 浅黄色 #FFFACD
        SEPIA,           // 复古棕 #F4ECD8
        CUSTOM           // 自定义
    };

    void setPaperPreset(PaperPreset preset) {
        switch(preset) {
        case WARM_WHITE:
            paperColor = cv::Vec3b(220, 248, 255); // #FFF8DC
            break;
        case CREAM:
            paperColor = cv::Vec3b(215, 235, 250); // #FAEBD7
            break;
        case LIGHT_YELLOW:
            paperColor = cv::Vec3b(205, 250, 255); // #FFFACD
            break;
        case SEPIA:
            paperColor = cv::Vec3b(216, 236, 244); // #F4ECD8
            break;
        case CUSTOM:
            // 保持当前自定义颜色
            break;
        }
    }
};

class PaperEffectEnhancer
{
public:
    explicit PaperEffectEnhancer(const AdvancedOptions& opt = AdvancedOptions());
    ~PaperEffectEnhancer();

    // 主要处理函数
    QImage enhance(const QImage& input);

    // 设置和获取参数
    void setOptions(const AdvancedOptions& opt);
    AdvancedOptions options() const { return m_options; }

private:
    // 格式转换
    cv::Mat qImageToCvMat(const QImage& image);
    QImage cvMatToQImage(const cv::Mat& mat);

    // 核心处理函数
    cv::Mat createTextMask(const cv::Mat& img);
    void applyPaperBackground(cv::Mat& img, const cv::Mat& textMask);
    void featherMask(cv::Mat& mask, int radius);

    // 1. 自适应阈值计算
    int calculateAdaptiveThreshold(const cv::Mat& gray);

    // 2. 生成纸张纹理
    cv::Mat generatePaperTexture(const cv::Size& size);
    void applyPaperTexture(cv::Mat& img, const cv::Mat& mask);

    // 3. 检测文字边缘
    cv::Mat detectTextEdges(const cv::Mat& gray);

    // 4. 创建渐进式强度遮罩
    cv::Mat createProgressiveIntensityMask(const cv::Size& size);

    AdvancedOptions m_options;

    // 纹理缓存（避免重复生成）
    cv::Mat m_cachedTexture;
    cv::Size m_cachedTextureSize;
};

#endif // PAPEREFFECTENHANCER_ADVANCED_H
