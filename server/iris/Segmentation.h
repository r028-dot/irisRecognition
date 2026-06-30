#pragma once
#include <opencv2/opencv.hpp>

// ─── קבועי סגמנטציה (Daugman IDO fallback thresholds) ────────────────────────
// מרחק מקסימלי (פיקסלים) שבו מרכז האישון יכול לנוע בין ה-coarse לה-IDO estimate
static constexpr float MAX_PUPIL_CENTER_SHIFT_PX = 15.0f;// מקדמי גרעין Gaussian בגודל 5 לחלקות עקומת ה-IDO (משקלות 1-4-6-4-1)
constexpr double GW5[5] = {1.0, 4.0, 6.0, 4.0, 1.0};//משקלות גרעין Gaussian בגודל 5 לחלקות עקומת ה-IDO (משקלות 1-4-6-4-1)
static constexpr float MAX_IRIS_CENTER_OFFSET_PX = 20.0f;// מקסימום סטייה בין מרכז האישון למרכז הקשתית (פיקסלים) לפני שמצמידים את מרכז הקשתית למרכז האישון

// תיאור אזור הקשתית בתמונה: מרכז ורדיוס האישון, מרכז ורדיוס הקשתית.
struct IrisRegion {
    cv::Point2f pupilCenter;
    float pupilRadius = 0.f;
    cv::Point2f irisCenter;
    float irisRadius = 0.f;
};

bool segmentIris(const cv::Mat& grayEye, IrisRegion& outRegion);
