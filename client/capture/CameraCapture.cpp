#include "CameraCapture.h"
#include <stdexcept>

namespace iris {

CameraCapture::CameraCapture(const ClientConfig& config) {
    // פותח את המצלמה לפי האינדקס שב-config (0 = מצלמה ראשונה)
    m_cap.open(config.cameraIndex);

    if (!m_cap.isOpened())
        throw std::runtime_error("Cannot open camera index " +
                                 std::to_string(config.cameraIndex));

    // מגדיר רזולוציה – OpenCV ינסה להגדיר, לא תמיד מובטח
    m_cap.set(cv::CAP_PROP_FRAME_WIDTH,  config.captureWidth);
    m_cap.set(cv::CAP_PROP_FRAME_HEIGHT, config.captureHeight);
}

CameraCapture::~CameraCapture() {
    // משחרר את המצלמה כדי שתוכנות אחרות יוכלו להשתמש בה
    if (m_cap.isOpened())
        m_cap.release();
}

cv::Mat CameraCapture::capture() {
    cv::Mat frame;
    m_cap >> frame;   // קורא פריים אחד מהמצלמה
    return frame;     // אם נכשל – frame יהיה ריק
}

bool CameraCapture::isOpen() const {
    return m_cap.isOpened();
}

} // namespace iris
