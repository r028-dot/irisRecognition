#pragma once
#include <opencv2/core.hpp>
#include <string>
#include "../config/ClientConfig.h"
using namespace std;
namespace iris {

struct QualityResult {
    bool   passed     = false;
    double brightness = 0.0;
    double sharpness  = 0.0;
    std::string reason;

    // תוצאות בדיקת חיות (Presentation Attack Detection)
    bool livenessPass = false;   
    double textureScore = 0.0;  
    bool hasSpecular = false; 
    string livenessReason;
};

class ImageQualityCheck {
public:
    explicit ImageQualityCheck(const ClientConfig& config);

    // בדיקת איכות כללית (בהירות + חדות)
    QualityResult check(const cv::Mat& image) const;

    // בדיקת חיות (PAD) — מזהה תמונות מודפסות, מסכי טלפון ועדשות מזויפות.
    // מוסיף תוצאות לתוך result שנמסר ע"י check().
    // 
    // הערה: בדיקה זו דורשת מצלמה חיה עם תאורה מבוקרת ורצף תמונות.
    // בסימולציה עם תמונות CASIA סטטיות, הבדיקה מושבתת אוטומטית.
    // במערכת ייצור אמיתית, יש להפעיל שיטות PAD מתקדמות:
    //   - Eye blinking detection (זיהוי מצמוצים)
    //   - Pupil light reflex (תגובת אישון לשינוי תאורה)
    //   - 3D depth analysis (ניתוח עומק)
    void checkLiveness(const cv::Mat& image, QualityResult& result) const;

    // ── שיטות PAD מתקדמות (לשימוש עתידי עם מצלמה חיה) ────────────────────────

    // זיהוי מצמוצים — דורש רצף של 90-120 פריימים (3-4 שניות ב-30fps)
    // עין חיה: 15-20 מצמוצים לדקה (ממוצע 1 מצמוץ כל 3-4 שניות)
    // תמונה/מסך: 0 מצמוצים
    bool detectEyeBlink(const std::vector<cv::Mat>& frameSequence) const;

    // תגובת אישון לאור — דורש 2 תמונות: לפני ואחרי שינוי תאורה
    // אישון חי מתכווץ ב-30-50% תוך 0.2-0.5 שניות כשהאור מתחזק
    // תמונה/מסך: אין שינוי בגודל האישון
    bool checkPupilLightReflex(const cv::Mat& beforeLight, 
                                const cv::Mat& afterLight) const;

    // ניתוח עומק 3D — דורש מצלמת עומק (סטריאו או structured light)
    // עין אמיתית: קרנית בולטת 0.5-1.0 מ"מ מעל פני האף
    // תמונה מודפסת: שטוחה לגמרי (הפרש עומק = 0)
    bool check3DDepth(const cv::Mat& depthMap) const;

    // Multi-spectral analysis — דורש מצלמת NIR (Near-Infrared 700-900nm)
    // רקמת עין חיה סופגת NIR אחרת מניר/מסך LCD
    // Correlation בין תמונת RGB לתמונת NIR: עין חיה > 0.85, ניר < 0.6
    bool checkMultiSpectral(const cv::Mat& rgbImage, 
                            const cv::Mat& nirImage) const;

    // אתגר-תגובה (Challenge-Response) — בקשה מהמשתמש לבצע פעולה אקראית
    // לדוגמה: "הסתכל שמאלה", "הטה ראש ימינה"
    // בוט/תמונה לא יכולים להגיב לפקודה דינמית
    bool checkChallengeResponse(const std::vector<cv::Mat>& responseFrames,
                                const std::string& challenge) const;

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

} 
