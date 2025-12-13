#include "papereffectenhancer.h"
#include <QMutexLocker>
#include <random>

PaperEffectEnhancer::PaperEffectEnhancer(const AdvancedOptions& opt)
    : m_options(opt)
{
}

PaperEffectEnhancer::~PaperEffectEnhancer()
{
}

QImage PaperEffectEnhancer::enhance(const QImage& input)
{
    if (!m_options.enabled || input.isNull()) {
        return input;
    }

    // 转换为 OpenCV Mat
    cv::Mat img = qImageToCvMat(input);
    if (img.empty()) {
        return input;
    }

    // 1. 创建文字遮罩 (黑色=文字, 白色=背景)
    cv::Mat textMask = createTextMask(img);

    // 2. 应用纸张背景色
    applyPaperBackground(img, textMask);

    // 3. 应用纸张纹理（如果启用）
    if (m_options.enablePaperTexture) {
        applyPaperTexture(img, textMask);
    }

    // 转换回 QImage
    return cvMatToQImage(img);
}

void PaperEffectEnhancer::setOptions(const AdvancedOptions& opt)
{
    m_options = opt;

    // 清除纹理缓存，下次使用时重新生成
    m_cachedTexture = cv::Mat();
}

cv::Mat PaperEffectEnhancer::qImageToCvMat(const QImage& image)
{
    cv::Mat mat;
    switch (image.format()) {
    case QImage::Format_RGB32:
    case QImage::Format_ARGB32:
    case QImage::Format_ARGB32_Premultiplied:
    {
        mat = cv::Mat(image.height(), image.width(), CV_8UC4,
                      const_cast<uchar*>(image.bits()),
                      static_cast<size_t>(image.bytesPerLine()));
        cv::Mat result;
        cv::cvtColor(mat, result, cv::COLOR_RGBA2BGR);
        return result.clone();
    }
    case QImage::Format_RGB888:
    {
        mat = cv::Mat(image.height(), image.width(), CV_8UC3,
                      const_cast<uchar*>(image.bits()),
                      static_cast<size_t>(image.bytesPerLine()));
        cv::Mat result;
        cv::cvtColor(mat, result, cv::COLOR_RGB2BGR);
        return result.clone();
    }
    case QImage::Format_Grayscale8:
    {
        mat = cv::Mat(image.height(), image.width(), CV_8UC1,
                      const_cast<uchar*>(image.bits()),
                      static_cast<size_t>(image.bytesPerLine()));
        return mat.clone();
    }
    default:
    {
        QImage convertedImage = image.convertToFormat(QImage::Format_RGB888);
        return qImageToCvMat(convertedImage);
    }
    }
}

QImage PaperEffectEnhancer::cvMatToQImage(const cv::Mat& mat)
{
    switch (mat.type()) {
    case CV_8UC1:
    {
        QImage image(mat.data, mat.cols, mat.rows,
                     static_cast<int>(mat.step), QImage::Format_Grayscale8);
        return image.copy();
    }
    case CV_8UC3:
    {
        cv::Mat rgb;
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        QImage image(rgb.data, rgb.cols, rgb.rows,
                     static_cast<int>(rgb.step), QImage::Format_RGB888);
        return image.copy();
    }
    case CV_8UC4:
    {
        cv::Mat rgba;
        cv::cvtColor(mat, rgba, cv::COLOR_BGRA2RGBA);
        QImage image(rgba.data, rgba.cols, rgba.rows,
                     static_cast<int>(rgba.step), QImage::Format_ARGB32);
        return image.copy();
    }
    default:
        return QImage();
    }
}

int PaperEffectEnhancer::calculateAdaptiveThreshold(const cv::Mat& gray)
{
    // 计算图像平均亮度
    cv::Scalar meanValue = cv::mean(gray);
    double meanBrightness = meanValue[0];

    // 自适应阈值 = 平均亮度 × 比例
    int adaptiveThreshold = static_cast<int>(meanBrightness * m_options.adaptiveThresholdRatio);

    // 限制在合理范围内 (150-230)
    adaptiveThreshold = std::max(150, std::min(230, adaptiveThreshold));

    return adaptiveThreshold;
}

cv::Mat PaperEffectEnhancer::createTextMask(const cv::Mat& img)
{
    cv::Mat gray;

    // 转换为灰度图
    if (img.channels() == 3) {
        cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = img.clone();
    }


    int finalThreshold = m_options.threshold;
    if (m_options.useAdaptiveThreshold && m_options.threshold == 0) {
        finalThreshold = calculateAdaptiveThreshold(gray);
    }

    // 创建遮罩：亮度低于阈值的是文字(0), 高于阈值的是背景(255)
    cv::Mat mask;
    cv::threshold(gray, mask, finalThreshold, 255, cv::THRESH_BINARY);


    if (m_options.protectTextEdges) {
        cv::Mat edgeMask = detectTextEdges(gray);
        // 有边缘的地方强制标记为文字区域（设为0）
        mask.setTo(0, edgeMask);
    }

    // 轻微腐蚀，避免文字边缘有白色残留
    if (m_options.featherRadius > 0) {
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE,
                                                   cv::Size(3, 3));
        cv::erode(mask, mask, kernel, cv::Point(-1, -1), 1);
    }

    // 羽化边缘，使过渡更自然
    if (m_options.featherRadius > 0) {
        featherMask(mask, m_options.featherRadius);
    }

    return mask;
}

cv::Mat PaperEffectEnhancer::detectTextEdges(const cv::Mat& gray)
{
    cv::Mat edges;

    // 使用Canny边缘检测
    double threshold1 = m_options.edgeThreshold;
    double threshold2 = threshold1 * 2.5;
    cv::Canny(gray, edges, threshold1, threshold2);

    // 轻微膨胀，让边缘区域更连续
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
    cv::dilate(edges, edges, kernel, cv::Point(-1, -1), 1);

    return edges;
}

cv::Mat PaperEffectEnhancer::createProgressiveIntensityMask(const cv::Size& size)
{
    cv::Mat intensityMask(size, CV_32F);

    // 计算中心点
    float centerX = size.width / 2.0f;
    float centerY = size.height / 2.0f;

    // 最大半径（从中心到角落的距离）
    float maxRadius = std::sqrt(centerX * centerX + centerY * centerY);

    // 生成径向渐变
    for (int y = 0; y < size.height; y++) {
        for (int x = 0; x < size.width; x++) {
            // 计算当前点到中心的距离
            float dx = x - centerX;
            float dy = y - centerY;
            float distance = std::sqrt(dx * dx + dy * dy);

            // 归一化距离 (0=中心, 1=边缘)
            float normalizedDistance = distance / maxRadius;

            // 线性插值：中心用 centerIntensity，边缘用 edgeIntensity
            float intensity = m_options.centerIntensity +
                              (m_options.edgeIntensity - m_options.centerIntensity) * normalizedDistance;

            intensityMask.at<float>(y, x) = intensity;
        }
    }

    return intensityMask;
}

void PaperEffectEnhancer::applyPaperBackground(cv::Mat& img, const cv::Mat& textMask)
{
    // 创建纸张背景图
    cv::Mat paperBackground(img.size(), img.type(), m_options.paperColor);

    // 如果是灰度图，转换纸张颜色为灰度
    if (img.channels() == 1) {
        cv::cvtColor(paperBackground, paperBackground, cv::COLOR_BGR2GRAY);
    }

    cv::Mat intensityMask;
    if (m_options.useProgressiveIntensity) {
        intensityMask = createProgressiveIntensityMask(img.size());
    }

    // 归一化遮罩到 0-1 范围
    cv::Mat maskFloat;
    textMask.convertTo(maskFloat, CV_32F, 1.0/255.0);

    // 分通道混合
    if (img.channels() == 3) {
        std::vector<cv::Mat> channels(3);
        std::vector<cv::Mat> bgChannels(3);
        cv::split(img, channels);
        cv::split(paperBackground, bgChannels);

        for (int i = 0; i < 3; i++) {
            channels[i].convertTo(channels[i], CV_32F);
            bgChannels[i].convertTo(bgChannels[i], CV_32F);

            // 根据是否使用渐进式强度，计算混合权重
            cv::Mat blendWeight;
            if (m_options.useProgressiveIntensity) {
                blendWeight = intensityMask.mul(maskFloat);
            } else {
                blendWeight = maskFloat * m_options.colorIntensity;
            }

            // 文字区域(mask=0)保持原图，背景区域(mask=1)使用纸张色
            channels[i] = channels[i].mul(1.0 - blendWeight) + bgChannels[i].mul(blendWeight);
            channels[i].convertTo(channels[i], CV_8U);
        }
        cv::merge(channels, img);
    } else {
        cv::Mat imgFloat, bgFloat;
        img.convertTo(imgFloat, CV_32F);
        paperBackground.convertTo(bgFloat, CV_32F);

        cv::Mat blendWeight;
        if (m_options.useProgressiveIntensity) {
            blendWeight = intensityMask.mul(maskFloat);
        } else {
            blendWeight = maskFloat * m_options.colorIntensity;
        }

        img = imgFloat.mul(1.0 - blendWeight) + bgFloat.mul(blendWeight);
        img.convertTo(img, CV_8U);
    }
}

cv::Mat PaperEffectEnhancer::generatePaperTexture(const cv::Size& size)
{
    // 检查缓存
    if (!m_cachedTexture.empty() && m_cachedTextureSize == size) {
        return m_cachedTexture.clone();
    }

    cv::Mat texture(size, CV_8UC3);

    // 使用随机数生成器
    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<float> dis(0.0f, 10.0f);  // 均值0，标准差10

    // 生成细腻的噪点
    for (int y = 0; y < size.height; y++) {
        for (int x = 0; x < size.width; x++) {
            float noise = dis(gen);
            // 限制噪点范围在 [-20, 20]
            noise = std::max(-20.0f, std::min(20.0f, noise));

            uchar value = cv::saturate_cast<uchar>(noise);
            texture.at<cv::Vec3b>(y, x) = cv::Vec3b(value, value, value);
        }
    }

    // 轻微模糊，让纹理更自然（模拟纸张纤维）
    cv::GaussianBlur(texture, texture, cv::Size(3, 3), 0.5);

    // 缓存纹理
    m_cachedTexture = texture.clone();
    m_cachedTextureSize = size;

    return texture;
}

void PaperEffectEnhancer::applyPaperTexture(cv::Mat& img, const cv::Mat& mask)
{
    // 生成纹理
    cv::Mat texture = generatePaperTexture(img.size());

    // 如果是灰度图，转换纹理为灰度
    if (img.channels() == 1) {
        cv::cvtColor(texture, texture, cv::COLOR_BGR2GRAY);
    }

    // 归一化遮罩（只在背景区域应用纹理）
    cv::Mat maskFloat;
    mask.convertTo(maskFloat, CV_32F, 1.0/255.0);

    // 转换为浮点数进行混合
    cv::Mat imgFloat, textureFloat;
    img.convertTo(imgFloat, CV_32F);
    texture.convertTo(textureFloat, CV_32F);

    // 纹理强度
    float intensity = m_options.textureIntensity;

    if (img.channels() == 3) {
        std::vector<cv::Mat> imgChannels(3);
        std::vector<cv::Mat> texChannels(3);
        cv::split(imgFloat, imgChannels);
        cv::split(textureFloat, texChannels);

        for (int i = 0; i < 3; i++) {
            // 只在背景区域（mask=1）添加纹理
            cv::Mat textureContribution = texChannels[i].mul(maskFloat) * intensity;
            imgChannels[i] = imgChannels[i] + textureContribution;
        }

        cv::merge(imgChannels, imgFloat);
    } else {
        cv::Mat textureContribution = textureFloat.mul(maskFloat) * intensity;
        imgFloat = imgFloat + textureContribution;
    }

    // 转换回8位
    imgFloat.convertTo(img, CV_8U);
}

void PaperEffectEnhancer::featherMask(cv::Mat& mask, int radius)
{
    if (radius <= 0) return;

    // 使用高斯模糊实现羽化效果
    int kernelSize = radius * 2 + 1;
    cv::GaussianBlur(mask, mask, cv::Size(kernelSize, kernelSize),
                     static_cast<double>(radius) / 2.0);
}
