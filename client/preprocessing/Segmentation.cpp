#include "Segmentation.h"
#include <opencv2/imgproc.hpp>
#include <stdexcept>
using namespace std;

namespace iris {

Segmentation::Segmentation(const string& faceCascadePath,
                             const string& eyeCascadePath)
{
    if (!m_faceCascade.load(faceCascadePath))
        throw runtime_error("Cannot load face cascade: " + faceCascadePath);
    if (!m_eyeCascade.load(eyeCascadePath))
        throw runtime_error("Cannot load eye cascade: " + eyeCascadePath);
}

// -------------------------------------------------------
// הפונקציה הראשית
// -------------------------------------------------------
SegmentationResult Segmentation::process(const cv::Mat& fullImage) const {
    SegmentationResult result;

    // המרה לגווני אפור – כל השלבים עובדים על grayscale
    cv::Mat gray;
    if (fullImage.channels() > 1)
        cv::cvtColor(fullImage, gray, cv::COLOR_BGR2GRAY);
    else
        gray = fullImage.clone();

    // === שלב 1: זיהוי פנים ===
    result.faceRegion = detectFace(gray);
    if (result.faceRegion.empty())
        return result;  // לא נמצאו פנים

    cv::Mat faceROI = gray(result.faceRegion);

    // === שלב 2: זיהוי עין בתוך הפנים ===
    result.eyeRegion = detectEye(faceROI);
    if (result.eyeRegion.empty())
        return result;  // לא נמצאה עין

    cv::Mat eyeROI = faceROI(result.eyeRegion);

    // === שלב 3: זיהוי קשתית בתוך העין ===
    if (!detectIris(eyeROI, result.irisCircle, result.pupilCircle))
        return result;  // לא נמצאה קשתית

    // === שלב 4: חיתוך crop של הקשתית בלבד ===
    int cx = static_cast<int>(result.irisCircle[0]);
    int cy = static_cast<int>(result.irisCircle[1]);
    int r  = static_cast<int>(result.irisCircle[2]);

    cv::Rect irisRect(cx - r, cy - r, r * 2, r * 2);
    irisRect &= cv::Rect(0, 0, eyeROI.cols, eyeROI.rows);

    result.irisImage = eyeROI(irisRect).clone();
    result.valid     = true;

    return result;
}

// -------------------------------------------------------
// שלב 1: זיהוי פנים – Haar Cascade
// -------------------------------------------------------
cv::Rect Segmentation::detectFace(const cv::Mat& grayImage) const {
    vector<cv::Rect> faces;

    cv::Mat equalized;
    cv::equalizeHist(grayImage, equalized);

    m_faceCascade.detectMultiScale(equalized, faces, 1.1, 4, 0, cv::Size(80, 80));

    if (faces.empty()) return cv::Rect();

    // בוחרים את הפנים הגדולות ביותר בתמונה
    return *max_element(faces.begin(), faces.end(),
        [](const cv::Rect& a, const cv::Rect& b) {
            return a.area() < b.area();
        });
}

// -------------------------------------------------------
// שלב 2: זיהוי עין – Haar Cascade בחצי העליון של הפנים
// -------------------------------------------------------
cv::Rect Segmentation::detectEye(const cv::Mat& faceROI) const {
    vector<cv::Rect> eyes;

    // מחפשים רק בחצי העליון של הפנים
    cv::Rect upperHalf(0, 0, faceROI.cols, faceROI.rows / 2);
    cv::Mat  upperFace = faceROI(upperHalf);

    m_eyeCascade.detectMultiScale(upperFace, eyes, 1.1, 3, 0, cv::Size(20, 20));

    if (eyes.empty()) return cv::Rect();

    cv::Rect eye = eyes[0];
    eye.y += upperHalf.y;  // תיקון קואורדינטות ביחס לפנים המלאות
    return eye;
}

// -------------------------------------------------------
// שלב 3: זיהוי קשתית ואישון – Hough Circle Transform
// -------------------------------------------------------
bool Segmentation::detectIris(const cv::Mat& eyeROI,
                               cv::Vec3f&     outIris,
                               cv::Vec3f&     outPupil) const
{
    cv::Mat blurred;
    cv::GaussianBlur(eyeROI, blurred, cv::Size(7, 7), 1.5);

    std::vector<cv::Vec3f> circles;
    int rMin = eyeROI.cols / 6;
    int rMax = eyeROI.cols / 2;

    // חיפוש גבול חיצוני של הקשתית
    cv::HoughCircles(blurred, circles, cv::HOUGH_GRADIENT,
                     1, eyeROI.rows / 4, 80, 15, rMin, rMax);

    if (circles.empty()) return false;

    // בוחרים את המעגל הכי קרוב למרכז התמונה
    cv::Point2f center(eyeROI.cols / 2.0f, eyeROI.rows / 2.0f);
    outIris = *std::min_element(circles.begin(), circles.end(),
        [&center](const cv::Vec3f& a, const cv::Vec3f& b) {
            float da = cv::norm(cv::Point2f(a[0], a[1]) - center);
            float db = cv::norm(cv::Point2f(b[0], b[1]) - center);
            return da < db;
        });

    // חיפוש האישון (מעגל קטן יותר בתוך הקשתית)
    circles.clear();
    cv::HoughCircles(blurred, circles, cv::HOUGH_GRADIENT,
                     1, eyeROI.rows / 4, 80, 12,
                     eyeROI.cols / 10,
                     static_cast<int>(outIris[2]) - 3);

    if (!circles.empty())
        outPupil = circles[0];
    else
        outPupil = cv::Vec3f(outIris[0], outIris[1], outIris[2] * 0.4f);

    return true;
}

} // namespace iris
