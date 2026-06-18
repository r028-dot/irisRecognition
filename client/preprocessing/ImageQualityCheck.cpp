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

    // ── בדיקת חיות (Presentation Attack Detection) ──────────────────────────
    checkLiveness(gray, result);
    if (!result.livenessPass) {
        result.reason = "Liveness check failed: " + result.livenessReason;
        return result;
    }

    result.passed = true;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// checkLiveness — PAD: מזהה תמונות מודפסות, מסכי טלפון ועדשות מזויפות.
//
// שיטות זיהוי:
//  1. מורכבות מרקם: עין חיה מציגה תבנית אקראית עשירה; תמונה מודפסת/מסך —
//     חלקה יותר ברמת המיקרו, שונות נמוכה יותר בחלקונים קטנים.
//  2. ניצוץ אור (specular reflection): נקודת אור בהירה על פני האישון/הקשתית
//     קיימת בעין ממשית ונעדרת בתמונה מודפסת.
// ─────────────────────────────────────────────────────────────────────────────
void ImageQualityCheck::checkLiveness(const cv::Mat& image, QualityResult& result) const
{
    // ודא שמדובר בתמונת גווני אפור
    cv::Mat gray;
    if (image.channels() > 1)
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    else
        gray = image.clone();

    result.textureScore = measureTextureComplexity(gray);
    result.hasSpecular  = detectSpecularReflection(gray);

    // תנאי עבור: מרקם ריאלי + ניצוץ אור
    if (result.textureScore < MIN_TEXTURE_SCORE) {
        result.livenessPass   = false;
        result.livenessReason = "Low texture complexity — possible printed photo or screen "
                                "(score=" + to_string(result.textureScore) + ")";
        return;
    }
    if (!result.hasSpecular) {
        result.livenessPass   = false;
        result.livenessReason = "No specular reflection detected — possible printed/flat surface";
        return;
    }

    result.livenessPass   = true;
    result.livenessReason = "Liveness confirmed";
}

// ─────────────────────────────────────────────────────────────────────────────
// measureTextureComplexity
// מחשב ממוצע שונות מקומית בחלקונים 8×8 על פני התמונה.
// עין חיה → שונות גבוהה בגלל רקמת הקשתית.
// תמונה מודפסת/מסך → שונות נמוכה בגלל גרגר הדפסה חלק.
// ─────────────────────────────────────────────────────────────────────────────
double ImageQualityCheck::measureTextureComplexity(const cv::Mat& gray) const
{
    const int blockSize = 8;
    double totalVar = 0.0;
    int    count    = 0;

    for (int y = 0; y + blockSize <= gray.rows; y += blockSize) {
        for (int x = 0; x + blockSize <= gray.cols; x += blockSize) {
            cv::Mat block = gray(cv::Rect(x, y, blockSize, blockSize));
            cv::Scalar mean, stddev;
            cv::meanStdDev(block, mean, stddev);
            totalVar += stddev[0] * stddev[0];  // שונות = סטיית תקן בריבוע
            ++count;
        }
    }
    return count > 0 ? totalVar / count : 0.0;
}

// ─────────────────────────────────────────────────────────────────────────────
// detectSpecularReflection
// בעין חיה, פנס הצילום יוצר נקודת ניצוץ בהירה מאוד על הקרנית.
// תמונה מודפסת/מסך לא תייצר ניצוץ מרוכז בעל עוצמה כזו.
// ─────────────────────────────────────────────────────────────────────────────
bool ImageQualityCheck::detectSpecularReflection(const cv::Mat& gray) const
{
    // ספית ניצוץ: פיקסלים בהירים מאוד (>240 מתוך 255)
    const uint8_t SPEC_THRESH  = 240;
    // שטח מינימלי של אזור ניצוץ: לפחות 2 פיקסלים אבל לא יותר מ-0.5% מהתמונה
    const int     MIN_AREA     = 2;
    const int     MAX_AREA     = static_cast<int>(gray.rows * gray.cols * 0.005);

    cv::Mat mask;
    cv::threshold(gray, mask, SPEC_THRESH, 255, cv::THRESH_BINARY);

    // מצא רכיבים מחוברים (connected components) באזורי הניצוץ
    cv::Mat labels, stats, centroids;
    int numLabels = cv::connectedComponentsWithStats(mask, labels, stats, centroids);

    for (int i = 1; i < numLabels; ++i) {
        int area = stats.at<int>(i, cv::CC_STAT_AREA);
        if (area >= MIN_AREA && area <= MAX_AREA)
            return true;  // נמצא ניצוץ בגודל סביר — סימן לעין חיה
    }
    return false;
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
