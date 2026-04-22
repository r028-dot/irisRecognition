#pragma once
#include <opencv2/core.hpp>
#include <string>
#include "../config/ClientConfig.h"

namespace iris {

struct QualityResult {
    bool   passed     = false;
    double brightness = 0.0;
    double sharpness  = 0.0;
    std::string reason;
};

class ImageQualityCheck {
public:
    explicit ImageQualityCheck(const ClientConfig& config);
    QualityResult check(const cv::Mat& image) const;
private:
    double measureBrightness(const cv::Mat& gray) const;
    double measureSharpness(const cv::Mat& gray) const;
    double m_minBrightness;
    double m_maxBrightness;
    double m_minSharpness;
};

} // namespace iris
