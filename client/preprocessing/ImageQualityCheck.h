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

    // תוצאות בדיקת חיות (Presentation Attack Detection)
    bool   livenessPass   = false;   // האם עמדה בבדיקת חיות
    double textureScore   = 0.0;     // מדד מורכבות מרקם (גבוה יותר = ריאלי יותר)
    bool   hasSpecular    = false;   // האם נמצאה נקודת ניצוץ (specular reflection)
    std::string livenessReason;
};

class ImageQualityCheck {
public:
    explicit ImageQualityCheck(const ClientConfig& config);

    // בדיקת איכות כללית (בהירות + חדות)
    QualityResult check(const cv::Mat& image) const;

    // בדיקת חיות (PAD) — מזהה תמונות מודפסות, מסכי טלפון ועדשות מזויפות.
    // מוסיף תוצאות לתוך result שנמסר ע"י check().
    void checkLiveness(const cv::Mat& image, QualityResult& result) const;

private:
    double measureBrightness(const cv::Mat& gray) const;
    double measureSharpness(const cv::Mat& gray) const;

    // מדד מורכבות מרקם: רקמת עין חיה >> תמונה מודפסת בגרגר הדפסה חלק
    double measureTextureComplexity(const cv::Mat& gray) const;

    // זיהוי ניצוץ אור (specular reflection) — קיים בעין ממשית, נעדר בתמונה
    bool detectSpecularReflection(const cv::Mat& gray) const;

    double m_minBrightness;
    double m_maxBrightness;
    double m_minSharpness;

    // סף מינימלי למדד המרקם שמתחתיו הבקשה נדחית
    static constexpr double MIN_TEXTURE_SCORE = 80.0;
};

} // namespace iris
