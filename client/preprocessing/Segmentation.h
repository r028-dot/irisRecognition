#pragma once
#include <opencv2/core.hpp>
#include <opencv2/objdetect.hpp>
#include <string>

namespace iris {

// תוצאת הסגמנטציה – כל מה שנוצא מהתמונה
struct SegmentationResult {
    cv::Rect    faceRegion;     // מלבן הפנים בתמונה המקורית
    cv::Rect    eyeRegion;      // מלבן העין (בתוך faceRegion)
    cv::Vec3f   irisCircle;     // (x, y, radius) של הקשתית בתוך eyeRegion
    cv::Vec3f   pupilCircle;    // (x, y, radius) של האישון
    cv::Mat     irisImage;      // crop של אזור הקשתית בלבד – זה מה שנשלח לשרת
    bool        valid = false;  // האם הצלחנו לחלץ קשתית תקינה?
};

class Segmentation {
public:
    // נתיבי קבצי Haar Cascade – ניתן להוריד מ-OpenCV data/haarcascades/
    explicit Segmentation(const std::string& faceCascadePath,
                          const std::string& eyeCascadePath);

    // הפונקציה הראשית: מקבלת תמונה מלאה, מחזירה SegmentationResult
    SegmentationResult process(const cv::Mat& fullImage) const;

private:
    // שלב 1: מוצא את הפנים בתמונה המלאה
    cv::Rect detectFace(const cv::Mat& grayImage) const;

    // שלב 2: מוצא עיניים בתוך אזור הפנים
    cv::Rect detectEye(const cv::Mat& faceROI) const;

    // שלב 3: מוצא את גבולות הקשתית בתוך אזור העין
    bool detectIris(const cv::Mat& eyeROI,
                    cv::Vec3f&     outIris,
                    cv::Vec3f&     outPupil) const;

    cv::CascadeClassifier m_faceCascade;
    cv::CascadeClassifier m_eyeCascade;
};

} // namespace iris
