#include "ImageQualityCheck.h"
#include <opencv2/imgproc.hpp>
using namespace std;

namespace iris {

ImageQualityCheck::ImageQualityCheck(const ClientConfig& config)
    : m_minBrightness(config.minBrightness)
    , m_maxBrightness(config.maxBrightness)
    , m_minSharpness (config.minSharpness)
{}

QualityResult ImageQualityCheck::check(const cv::Mat& image) const {
    QualityResult result;

    // המרה לגווני אפור לצורך חישוב
    cv::Mat gray;
    if (image.channels() > 1)
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    else
        gray = image;

    result.brightness = measureBrightness(gray);
    result.sharpness  = measureSharpness(gray);

    // בדיקת בהירות
    if (result.brightness < m_minBrightness) {
        result.reason = "Image too dark (brightness=" +
                        to_string(result.brightness) + ")";
        return result;
    }
    if (result.brightness > m_maxBrightness) {
        result.reason = "Image too bright (brightness=" +
                        to_string(result.brightness) + ")";
        return result;
    }

    // בדיקת חדות
    if (result.sharpness < m_minSharpness) {
        result.reason = "Image too blurry (sharpness=" +
                        to_string(result.sharpness) + ")";
        return result;
    }

    result.passed = true;
    return result;
}

double ImageQualityCheck::measureBrightness(const cv::Mat& gray) const {
    // cv::mean מחזיר ממוצע ערכי הפיקסלים (0=שחור, 255=לבן)
    return cv::mean(gray)[0];
}

double ImageQualityCheck::measureSharpness(const cv::Mat& gray) const {
    // מחשב Laplacian (נגזרת שנייה) ואז לוקח את השונות שלו.
    // תמונה מטושטשת = שינויים קטנים בין פיקסלים = שונות נמוכה.
    cv::Mat lap;
    cv::Laplacian(gray, lap, CV_64F);

    cv::Scalar mean, stddev;
    cv::meanStdDev(lap, mean, stddev);

    return stddev[0] * stddev[0];  // שונות = סטיית תקן בריבוע
}

} // namespace iris
