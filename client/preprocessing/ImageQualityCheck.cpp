#include "ImageQualityCheck.h"
#include <opencv2/imgproc.hpp>
using namespace std;

namespace iris {
ImageQualityCheck::ImageQualityCheck(const ClientConfig& config)
    : m_minBrightness(config.minBrightness)
    , m_maxBrightness(config.maxBrightness)
    , m_minSharpness (config.minSharpness)
{}

//פעולה האחראית על בדיקת איכות התמונה
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

    //  בדיקת חיות (Presentation Attack Detection) 
    checkLiveness(gray, result);
    if (!result.livenessPass) {
        result.reason = "Liveness check failed: " + result.livenessReason;
        return result;
    }

    result.passed = true;
    return result;
}

// checkLiveness — PAD: מזהה תמונות מודפסות, מסכי טלפון ועדשות מזויפות.
//
// שיטות זיהוי:
//  1. מורכבות מרקם: עין חיה מציגה תבנית אקראית עשירה; תמונה מודפסת/מסך —
//     חלקה יותר ברמת המיקרו, שונות נמוכה יותר בחלקונים קטנים.
//  2. ניצוץ אור (specular reflection): נקודת אור בהירה על פני האישון/הקשתית
//     קיימת בעין ממשית ונעדרת בתמונה מודפסת.
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


// מחשב ממוצע שונות מקומית בחלקונים 8×8 על פני התמונה.
// עין חיה → שונות גבוהה בגלל רקמת הקשתית.
// תמונה מודפסת/מסך → שונות נמוכה בגלל גרגר הדפסה חלק.
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


// בעין חיה, פנס הצילום יוצר נקודת ניצוץ בהירה מאוד על הקרנית.
// תמונה מודפסת/מסך לא תייצר ניצוץ מרוכז בעל עוצמה כזו.
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

// מדד בהירות ממוצע (0=שחור, 255=לבן)
double ImageQualityCheck::measureBrightness(const cv::Mat& gray) const {
    return cv::mean(gray)[0];
}

// מדד חדות על פי שונות של Laplacian.
// תמונה חדה = שינויים גדולים בין פיקסלים = שונות גבוהה.
double ImageQualityCheck::measureSharpness(const cv::Mat& gray) const {
    cv::Mat lap;
    cv::Laplacian(gray, lap, CV_64F);
    cv::Scalar mean, stddev;
    cv::meanStdDev(lap, mean, stddev);
    return stddev[0] * stddev[0];  // שונות = סטיית תקן בריבוע
}

// שיטות PAD מתקדמות — לשימוש עתידי עם מצלמה חיה
// הערה: שיטות אלו לא מופעלות בסימולציה כי הן דורשות מצלמה חיה.

// זיהוי מצמוצים — דורש רצף של 90-120 פריימים (3-4 שניות ב-30fps)
bool ImageQualityCheck::detectEyeBlink(const std::vector<cv::Mat>& frameSequence) const
{
    if (frameSequence.size() < 30) return false; 
    int blinkCount = 0;
    return blinkCount >= 1 && blinkCount <= 3;
}

// זיהוי שינויי אישון בתגובה לאור חזק (Pupil Light Reflex)
bool ImageQualityCheck::checkPupilLightReflex(const cv::Mat& beforeLight, 
                                               const cv::Mat& afterLight) const
{
    cv::Mat grayBefore, grayAfter;
    if (beforeLight.channels() > 1)
        cv::cvtColor(beforeLight, grayBefore, cv::COLOR_BGR2GRAY);
    else
        grayBefore = beforeLight;
    
    if (afterLight.channels() > 1)
        cv::cvtColor(afterLight, grayAfter, cv::COLOR_BGR2GRAY);
    else
        grayAfter = afterLight;
    
    // מימוש מפושט: בודק שינוי במרכז התמונה (אזור האישון)
    cv::Rect centerROI(grayBefore.cols/3, grayBefore.rows/3, 
                       grayBefore.cols/3, grayBefore.rows/3);
    double meanBefore = cv::mean(grayBefore(centerROI))[0];
    double meanAfter = cv::mean(grayAfter(centerROI))[0];
    // אישון חי אמור להיות כהה יותר כשיש אור חזק (התכווצות)
    double changePct = abs(meanBefore - meanAfter) / meanBefore;
    // במימוש אמיתי: cv::HoughCircles לזיהוי אישון מדויק
    return changePct >= 0.15 && changePct <= 0.6;
}

// ניתוח עומק 3D — דורש מצלמת עומק (סטריאו או structured light)
bool ImageQualityCheck::check3DDepth(const cv::Mat& depthMap) const
{
    if (depthMap.empty() || depthMap.type() != CV_16UC1)
        return false;
    
    // חפש אזור העין (מרכז התמונה)
    cv::Rect eyeROI(depthMap.cols/3, depthMap.rows/3, 
                    depthMap.cols/3, depthMap.rows/3);
    cv::Mat eyeDepth = depthMap(eyeROI);
    
    double minDepth, maxDepth;
    cv::minMaxLoc(eyeDepth, &minDepth, &maxDepth);
    
    // עין אמיתית: קרנית בולטת 0.5-1.5mm מעל פני האף
    // תמונה שטוחה: הפרש עומק קרוב ל-0
    double depthRange = maxDepth - minDepth;
    
    return depthRange >= 0.5 && depthRange <= 3.0;  // ב-mm
}

// Multi-spectral analysis — דורש מצלמת NIR (Near-Infrared 700-900nm)
// רקמת עין חיה סופגת NIR אחרת מניר/מסך LCD
bool ImageQualityCheck::checkMultiSpectral(const cv::Mat& rgbImage, 
                                            const cv::Mat& nirImage) const
{
    if (rgbImage.size() != nirImage.size())
        return false;
    
    cv::Mat grayRGB;
    cv::cvtColor(rgbImage, grayRGB, cv::COLOR_BGR2GRAY);
    
    // חשב קורלציה בין שתי התמונות
    cv::Mat correlation;
    cv::matchTemplate(grayRGB, nirImage, correlation, cv::TM_CCORR_NORMED);
    
    double minVal, maxVal;
    cv::minMaxLoc(correlation, &minVal, &maxVal);
    
    // עין חיה: correlation > 0.85
    // נייר/מסך: correlation < 0.6
    return maxVal > 0.85;
}

// Challenge-response liveness — מחייב תגובה של המשתמש
bool ImageQualityCheck::checkChallengeResponse(const std::vector<cv::Mat>& responseFrames,
                                                const std::string& challenge) const
{
    if (responseFrames.size() < 10) return false;  // לפחות 10 פריימים
    if (challenge == "look_left") {
        // TODO: בדוק שהאישון נע שמאלה
        return true; 
    }
    else if (challenge == "tilt_head") {
        // TODO: בדוק שינוי בזווית ראש (pose estimation)
        return true; 
    }
    return false;

} 

}
