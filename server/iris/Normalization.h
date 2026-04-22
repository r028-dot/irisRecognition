#pragma once
#include <opencv2/opencv.hpp>

// Localization result produced by Segmentation or received from the client.
struct IrisRegion {
    cv::Point2f pupilCenter;
    float       pupilRadius  = 0.f;
    cv::Point2f irisCenter;
    float       irisRadius   = 0.f;
};

// Implements Daugman's Rubber Sheet Model.
// Maps the annular iris region to a fixed-size rectangular image.
class Normalization {
public:
    // outWidth  ≈ 512 (angular resolution)
    // outHeight ≈  64 (radial resolution)
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
