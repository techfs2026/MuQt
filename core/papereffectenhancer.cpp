#include "papereffectenhancer.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QMutexLocker>


static void applyPaperEffectOptimized(cv::Mat& img)
{
    if (img.empty() || img.channels() != 3) return;

    // 全局亮度调节系数（0~1，越小越暗）
    const float globalBrightnessFactor = 0.88f;

    cv::Mat lab;
    cv::cvtColor(img, lab, cv::COLOR_BGR2Lab);

    std::vector<cv::Mat> cs;
    cv::split(lab, cs);
    cv::Mat& L = cs[0]; // 亮度
    cv::Mat& A = cs[1]; // a通道
    cv::Mat& B = cs[2]; // b通道

    for (int y = 0; y < img.rows; ++y) {
        uchar* lptr = L.ptr<uchar>(y);
        uchar* aptr = A.ptr<uchar>(y);
        uchar* bptr = B.ptr<uchar>(y);

        for (int x = 0; x < img.cols; ++x) {
            float lum = lptr[x];

            // ---------- 背景护眼黄色 ----------
            float alpha_bg = 0.0f;
            if (lum < 200.0f) alpha_bg = 0.38f;       // 暗区增强黄
            else if (lum < 230.0f) alpha_bg = 0.20f;  // 中亮区偏黄
            else alpha_bg = 0.08f;                     // 高亮区轻黄

            aptr[x] = cv::saturate_cast<uchar>(aptr[x] + alpha_bg * 5.0f);
            bptr[x] = cv::saturate_cast<uchar>(bptr[x] + alpha_bg * 36.0f);

            // ---------- L通道亮度调整（加入全局亮度系数） ----------
            if (lum < 200) lptr[x] = cv::saturate_cast<uchar>(lptr[x] * 0.78f * globalBrightnessFactor);
            else if (lum < 230) lptr[x] = cv::saturate_cast<uchar>(lptr[x] * 0.85f * globalBrightnessFactor);
            else lptr[x] = cv::saturate_cast<uchar>(lptr[x] * 0.93f * globalBrightnessFactor);

            // ---------- 文字加黑（柔和护眼） ----------
            if (lum < 160.0f) {
                float alpha_text = (160.0f - lum) / 160.0f;
                float darken_factor = 0.10f + 0.12f * alpha_text;
                lptr[x] = cv::saturate_cast<uchar>(lptr[x] * (1.0f - darken_factor));

                bptr[x] = cv::saturate_cast<uchar>(bptr[x] - alpha_text * 2.0f);
            }
        }
    }

    cv::merge(cs, lab);
    cv::cvtColor(lab, img, cv::COLOR_Lab2BGR);
}


PaperEffectEnhancer::PaperEffectEnhancer(const Options& opt)
    : m_options(opt)
{
    m_clahe = cv::createCLAHE(m_options.claheClipLimit, m_options.claheTileGrid);
}

PaperEffectEnhancer::~PaperEffectEnhancer()
{
}

void PaperEffectEnhancer::setOptions(const Options& opt)
{
    QMutexLocker locker(&m_mutex);
    m_options = opt;
    m_clahe->setClipLimit(m_options.claheClipLimit);
    m_clahe->setTilesGridSize(m_options.claheTileGrid);
}

QImage PaperEffectEnhancer::enhance(const QImage& input)
{
    if (input.isNull() || !m_options.enabled) return input;
    QMutexLocker locker(&m_mutex);

    cv::Mat img = qImageToCvMat(input);
    if (img.empty()) return input;

    bool isColor = isColorImage(img);

    try {
        removeShadows(img);
        normalizeColor(img, isColor);
        enhanceContrast(img);
        applyPaperEffectOptimized(img);
        adjustGamma(img, m_options.gamma);
    } catch (...) {
        return input;
    }

    return cvMatToQImage(img);
}

cv::Mat PaperEffectEnhancer::qImageToCvMat(const QImage& image)
{
    if (image.isNull()) {
        return cv::Mat();
    }

    QImage converted;
    if (image.format() == QImage::Format_RGB888) {
        converted = image.copy();
    } else {
        converted = image.convertToFormat(QImage::Format_RGB888);
    }

    if (converted.isNull()) {
        return cv::Mat();
    }

    cv::Mat mat(converted.height(), converted.width(), CV_8UC3);

    for (int y = 0; y < converted.height(); ++y) {
        const uchar* srcLine = converted.scanLine(y);
        uchar* dstLine = mat.ptr<uchar>(y);

        for (int x = 0; x < converted.width(); ++x) {
            dstLine[x * 3 + 0] = srcLine[x * 3 + 2]; // B
            dstLine[x * 3 + 1] = srcLine[x * 3 + 1]; // G
            dstLine[x * 3 + 2] = srcLine[x * 3 + 0]; // R
        }
    }

    return mat;
}

QImage PaperEffectEnhancer::cvMatToQImage(const cv::Mat& mat)
{
    if (mat.empty()) {
        return QImage();
    }

    if (mat.type() == CV_8UC3) {
        QImage image(mat.cols, mat.rows, QImage::Format_RGB888);

        for (int y = 0; y < mat.rows; ++y) {
            const uchar* srcLine = mat.ptr<uchar>(y);
            uchar* dstLine = image.scanLine(y);

            for (int x = 0; x < mat.cols; ++x) {
                dstLine[x * 3 + 0] = srcLine[x * 3 + 2]; // R
                dstLine[x * 3 + 1] = srcLine[x * 3 + 1]; // G
                dstLine[x * 3 + 2] = srcLine[x * 3 + 0]; // B
            }
        }

        return image;
    } else if (mat.type() == CV_8UC1) {
        QImage image(mat.data, mat.cols, mat.rows,
                     static_cast<int>(mat.step),
                     QImage::Format_Grayscale8);
        return image.copy();
    }

    return QImage();
}

void PaperEffectEnhancer::removeShadows(cv::Mat& img)
{
    if (img.empty() || img.rows == 0 || img.cols == 0) {
        return;
    }

    cv::Mat gray;
    if (img.channels() == 3) {
        cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = img.clone();
    }

    int kernelSize = std::max(std::min(img.cols / 25, 61), 21);
    if (kernelSize % 2 == 0) kernelSize++;

    cv::Mat background;
    cv::GaussianBlur(gray, background, cv::Size(kernelSize, kernelSize), 0);

    cv::Mat grayFloat, bgFloat;
    gray.convertTo(grayFloat, CV_32F);
    background.convertTo(bgFloat, CV_32F);

    cv::Mat normalized = cv::Mat::zeros(gray.size(), CV_32F);
    for (int y = 0; y < gray.rows; ++y) {
        const float* grayPtr = grayFloat.ptr<float>(y);
        const float* bgPtr = bgFloat.ptr<float>(y);
        float* normPtr = normalized.ptr<float>(y);

        for (int x = 0; x < gray.cols; ++x) {
            float grayVal = grayPtr[x];
            float bgVal = std::max(bgPtr[x], 1.0f);

            float diff = bgVal - grayVal;
            float ratio = grayVal / bgVal;

            float adjusted;
            if (diff < 25) {
                adjusted = grayVal;
            } else {
                adjusted = ratio * 218.0f + 22.0f;
            }

            normPtr[x] = adjusted;
        }
    }

    cv::Mat result;
    normalized.convertTo(result, CV_8U);

    if (img.channels() == 3) {
        std::vector<cv::Mat> channels;
        cv::split(img, channels);

        for (int i = 0; i < 3; ++i) {
            cv::Mat channelBg;
            cv::GaussianBlur(channels[i], channelBg, cv::Size(kernelSize, kernelSize), 0);

            cv::Mat channelFloat, bgChanFloat;
            channels[i].convertTo(channelFloat, CV_32F);
            channelBg.convertTo(bgChanFloat, CV_32F);

            cv::Mat normChan = cv::Mat::zeros(channels[i].size(), CV_32F);
            for (int y = 0; y < channels[i].rows; ++y) {
                const float* chPtr = channelFloat.ptr<float>(y);
                const float* bgPtr = bgChanFloat.ptr<float>(y);
                float* normPtr = normChan.ptr<float>(y);

                for (int x = 0; x < channels[i].cols; ++x) {
                    float chVal = chPtr[x];
                    float bgVal = std::max(bgPtr[x], 1.0f);

                    float diff = bgVal - chVal;
                    float ratio = chVal / bgVal;

                    float adjusted;
                    if (diff < 25) {
                        adjusted = chVal;
                    } else {
                        float baseAdjust = ratio * 218.0f + 22.0f;

                        if (i == 0) {
                            normPtr[x] = baseAdjust * 1.02f;
                        } else if (i == 1) {
                            normPtr[x] = baseAdjust * 0.99f;
                        } else {
                            normPtr[x] = baseAdjust * 0.95f;
                        }
                        continue;
                    }

                    normPtr[x] = adjusted;
                }
            }

            cv::Mat chanResult;
            normChan.convertTo(chanResult, CV_8U);
            channels[i] = chanResult;
        }

        cv::merge(channels, img);
    } else {
        img = result;
    }
}

void PaperEffectEnhancer::normalizeColor(cv::Mat& img, bool isColor)
{
    if (!isColor || img.channels() != 3 || img.empty()) {
        return;
    }

    cv::Scalar mean = cv::mean(img);
    double avgGray = (mean[0] + mean[1] + mean[2]) / 3.0;

    if (avgGray < 1.0) {
        return;
    }

    std::vector<cv::Mat> channels;
    cv::split(img, channels);

    for (int i = 0; i < 3; ++i) {
        if (mean[i] > 1.0) {
            double scale = avgGray / mean[i];
            scale = std::min(std::max(scale, 0.5), 2.0);
            channels[i].convertTo(channels[i], -1, scale, 0);
        }
    }

    cv::merge(channels, img);
}

void PaperEffectEnhancer::enhanceContrast(cv::Mat& img)
{
    if (img.empty()) {
        return;
    }

    if (img.channels() == 3) {
        cv::Mat lab;
        cv::cvtColor(img, lab, cv::COLOR_BGR2Lab);

        std::vector<cv::Mat> labChannels;
        cv::split(lab, labChannels);

        cv::Mat& L = labChannels[0];

        cv::Mat denoised;
        cv::bilateralFilter(L, denoised, 7, 30, 30);

        cv::Mat enhanced;
        m_clahe->apply(denoised, enhanced);

        cv::Mat blended = cv::Mat::zeros(L.size(), CV_8U);
        for (int y = 0; y < L.rows; ++y) {
            const uchar* origPtr = denoised.ptr<uchar>(y);
            const uchar* enhPtr = enhanced.ptr<uchar>(y);
            uchar* blendPtr = blended.ptr<uchar>(y);

            for (int x = 0; x < L.cols; ++x) {
                int orig = origPtr[x];
                int enh = enhPtr[x];

                if (orig < 65) {
                    int darkened = static_cast<int>(orig * 0.55);
                    blendPtr[x] = cv::saturate_cast<uchar>(darkened);
                } else if (orig < 125) {
                    blendPtr[x] = cv::saturate_cast<uchar>(orig * 0.35 + enh * 0.65);
                } else if (orig < 180) {
                    blendPtr[x] = cv::saturate_cast<uchar>(orig * 0.70 + enh * 0.30);
                } else {
                    blendPtr[x] = orig;
                }
            }
        }

        labChannels[0] = blended;

        cv::merge(labChannels, lab);
        cv::cvtColor(lab, img, cv::COLOR_Lab2BGR);
    } else {
        cv::Mat denoised;
        cv::bilateralFilter(img, denoised, 7, 30, 30);

        cv::Mat enhanced;
        m_clahe->apply(denoised, enhanced);

        cv::Mat blended = cv::Mat::zeros(img.size(), CV_8U);
        for (int y = 0; y < img.rows; ++y) {
            const uchar* origPtr = denoised.ptr<uchar>(y);
            const uchar* enhPtr = enhanced.ptr<uchar>(y);
            uchar* blendPtr = blended.ptr<uchar>(y);

            for (int x = 0; x < img.cols; ++x) {
                int orig = origPtr[x];
                int enh = enhPtr[x];

                if (orig < 65) {
                    int darkened = static_cast<int>(orig * 0.55);
                    blendPtr[x] = cv::saturate_cast<uchar>(darkened);
                } else if (orig < 125) {
                    blendPtr[x] = cv::saturate_cast<uchar>(orig * 0.35 + enh * 0.65);
                } else if (orig < 180) {
                    blendPtr[x] = cv::saturate_cast<uchar>(orig * 0.70 + enh * 0.30);
                } else {
                    blendPtr[x] = orig;
                }
            }
        }

        img = blended;
    }
}

void PaperEffectEnhancer::sharpen(cv::Mat& img, float alphaMax)
{
    if (img.empty()) return;

    cv::Mat gray;
    if (img.channels() == 3)
        cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
    else
        gray = img.clone();

    cv::Mat textMask = gray < 165;
    cv::Mat blurred;
    cv::GaussianBlur(img, blurred, cv::Size(0, 0), 0.8);
    cv::Mat sharpened;
    cv::addWeighted(img, 1.35, blurred, -0.35, 0, sharpened);

    cv::Mat smoothMask;
    cv::GaussianBlur(textMask, smoothMask, cv::Size(7, 7), 2.0); // 更大平滑过渡

    cv::Mat result = img.clone();

    if (img.channels() == 3) {
        for (int y = 0; y < img.rows; ++y) {
            const uchar* sharpPtr = sharpened.ptr<uchar>(y);
            const uchar* origPtr = img.ptr<uchar>(y);
            uchar* resPtr = result.ptr<uchar>(y);
            const uchar* maskPtr = smoothMask.ptr<uchar>(y);

            for (int x = 0; x < img.cols; ++x) {
                float alpha = maskPtr[x] / 255.0f;
                alpha *= alphaMax; // 控制锐化强度

                for (int c = 0; c < 3; ++c) {
                    int idx = x * 3 + c;
                    resPtr[idx] = cv::saturate_cast<uchar>(
                        origPtr[idx] * (1.0f - alpha) +
                        sharpPtr[idx] * alpha
                        );
                }
            }
        }
    } else {
        for (int y = 0; y < img.rows; ++y) {
            const uchar* sharpPtr = sharpened.ptr<uchar>(y);
            const uchar* origPtr = img.ptr<uchar>(y);
            uchar* resPtr = result.ptr<uchar>(y);
            const uchar* maskPtr = smoothMask.ptr<uchar>(y);

            for (int x = 0; x < img.cols; ++x) {
                float alpha = maskPtr[x] / 255.0f;
                alpha *= alphaMax;
                resPtr[x] = cv::saturate_cast<uchar>(
                    origPtr[x] * (1.0f - alpha) +
                    sharpPtr[x] * alpha
                    );
            }
        }
    }

    img = result;
}

void PaperEffectEnhancer::adjustGamma(cv::Mat& img, double gamma)
{
    if (img.empty()) {
        return;
    }

    if (img.channels() == 3) {
        cv::Mat lab;
        cv::cvtColor(img, lab, cv::COLOR_BGR2Lab);

        std::vector<cv::Mat> labChannels;
        cv::split(lab, labChannels);

        cv::Mat& L = labChannels[0];

        cv::Mat lut(1, 256, CV_8U);
        uchar* p = lut.ptr();

        for (int i = 0; i < 256; ++i) {
            double normalized = i / 255.0;
            double adjusted;

            if (normalized < 0.04) {
                adjusted = normalized * 0.25;
            } else if (normalized < 0.35) {
                adjusted = pow(normalized, gamma * 0.82);
            } else if (normalized < 0.68) {
                adjusted = pow(normalized, gamma);
            } else if (normalized < 0.95) {
                adjusted = pow(normalized, gamma * 1.08);
            } else {
                adjusted = 0.95 + (normalized - 0.95) * 1.6;
            }

            p[i] = cv::saturate_cast<uchar>(adjusted * 255.0);
        }

        cv::LUT(L, lut, L);

        cv::merge(labChannels, lab);
        cv::cvtColor(lab, img, cv::COLOR_Lab2BGR);
    } else {
        cv::Mat lut(1, 256, CV_8U);
        uchar* p = lut.ptr();

        for (int i = 0; i < 256; ++i) {
            double normalized = i / 255.0;
            double adjusted;

            if (normalized < 0.04) {
                adjusted = normalized * 0.25;
            } else if (normalized < 0.35) {
                adjusted = pow(normalized, gamma * 0.82);
            } else if (normalized < 0.68) {
                adjusted = pow(normalized, gamma);
            } else if (normalized < 0.95) {
                adjusted = pow(normalized, gamma * 1.08);
            } else {
                adjusted = 0.95 + (normalized - 0.95) * 1.6;
            }

            p[i] = cv::saturate_cast<uchar>(adjusted * 255.0);
        }

        cv::LUT(img, lut, img);
    }
}

bool PaperEffectEnhancer::isColorImage(const cv::Mat& img)
{
    if (img.empty() || img.channels() == 1) {
        return false;
    }

    cv::Scalar mean = cv::mean(img);
    cv::Scalar stddev;
    cv::meanStdDev(img, mean, stddev);

    double channelDiff = std::abs(mean[0] - mean[1]) +
                         std::abs(mean[1] - mean[2]) +
                         std::abs(mean[0] - mean[2]);

    double stddevSum = stddev[0] + stddev[1] + stddev[2];

    return channelDiff > 5.0 || stddevSum > 30.0;
}

void PaperEffectEnhancer::autoAdjustParameters(const cv::Mat& img)
{
    if (img.empty()) {
        return;
    }

    cv::Mat gray;
    if (img.channels() == 3) {
        cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = img.clone();
    }

    cv::Scalar mean, stddev;
    cv::meanStdDev(gray, mean, stddev);

    double contrast = stddev[0];
    double brightness = mean[0];

    if (contrast < 30) {
        m_clahe->setClipLimit(3.5);
        m_options.gamma = 0.78;
    } else if (contrast > 80) {
        m_clahe->setClipLimit(2.6);
        m_options.gamma = 0.86;
    } else {
        m_clahe->setClipLimit(3.0);
        m_options.gamma = 0.82;
    }

    if (brightness < 95) {
        m_options.gamma = std::max(0.74, m_options.gamma - 0.04);
    } else if (brightness > 185) {
        m_options.gamma = std::min(0.88, m_options.gamma + 0.02);
    }
}


