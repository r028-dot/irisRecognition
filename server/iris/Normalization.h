#pragma once
#include <opencv2/opencv.hpp>
using namespace std;

// תיאור אזור הקשתית בתמונה: מרכז ורדיוס האישון, מרכז ורדיוס הקשתית.
struct IrisRegion {
    cv::Point2f pupilCenter;
    float pupilRadius = 0.f;
    cv::Point2f irisCenter;
    float irisRadius = 0.f;
};

// מיישם את מודל Rubber Sheet של Daugman.
// ממפה את אזור הקשתית הטבעתית לתמונה מלבנית בגודל קבוע.
class Normalization {
public:
    // outWidth  ≈ 512 (רזולוציה זוויתית)
    // outHeight ≈  64 (רזולוציה רדיאלית)
    Normalization(int outWidth = 512, int outHeight = 64);

    // Unwrap the iris from srcGray into a normalizedWidth x normalizedHeight CV_8UC1 image.
    // occlusionMask output: same size, 255 = valid pixel, 0 = eyelid / eyelash occlusion.
    cv::Mat normalize(const cv::Mat& srcGray,
                      const IrisRegion& region,
                      cv::Mat& occlusionMask) const;

private:
    int m_width;
    int m_height;
};

// Segment the iris in a grayscale eye image using Hough circles.
// Returns false if segmentation fails.
bool segmentIris(const cv::Mat& grayEye, IrisRegion& outRegion);
