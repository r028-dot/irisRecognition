#pragma once
#include <opencv2/videoio.hpp>
#include "../config/ClientConfig.h"

namespace iris {

// אחראי על פתיחת המצלמה וצילום פריים יחיד.
// משתמש ב-OpenCV VideoCapture מאחורי הקלעים.
class CameraCapture {
public:
    // פותח את המצלמה לפי ההגדרות שב-config (index, רזולוציה)
    explicit CameraCapture(const ClientConfig& config);

    // סוגר את המצלמה בצורה נקייה
    ~CameraCapture();

    // מצלם פריים אחד ומחזיר אותו כ-cv::Mat (BGR)
    // אם הצילום נכשל – מחזיר Mat ריק
    cv::Mat capture();

    // האם המצלמה פתוחה ומוכנה?
    bool isOpen() const;

private:
    cv::VideoCapture m_cap;   // אובייקט OpenCV שמנהל את המצלמה
};

} // namespace iris
