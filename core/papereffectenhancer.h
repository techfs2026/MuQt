#ifndef PAPEREFFECTENHANCER_H
#define PAPEREFFECTENHANCER_H

#include <opencv2/opencv.hpp>
#include <QImage>
#include <QMutex>

struct Options {
    bool enabled = true;
    double gamma = 0.82;
    double sharpAmount = 1.7;
    double sharpSubtract = 0.7;
    double claheClipLimit = 3.0;
    cv::Size claheTileGrid = cv::Size(8, 8);
    bool autoAdjust = true;
};

class PaperEffectEnhancer
{
public:
    explicit PaperEffectEnhancer(const Options& opt = Options());
    ~PaperEffectEnhancer();

    QImage enhance(const QImage& input);

    void setOptions(const Options& opt);
    Options options() const { return m_options; }

private:
    cv::Mat qImageToCvMat(const QImage& image);
    QImage cvMatToQImage(const cv::Mat& mat);

    void removeShadows(cv::Mat& img);
    void normalizeColor(cv::Mat& img, bool isColor);
    void enhanceContrast(cv::Mat& img);
    void sharpen(cv::Mat& img, float alphaMax);
    void adjustGamma(cv::Mat& img, double gamma);

    bool isColorImage(const cv::Mat& img);
    void autoAdjustParameters(const cv::Mat& img);

    Options m_options;
    cv::Ptr<cv::CLAHE> m_clahe;
    QMutex m_mutex;
};

#endif // PAPEREFFECTENHANCER_H
