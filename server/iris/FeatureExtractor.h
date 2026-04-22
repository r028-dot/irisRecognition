#pragma once
#include <vector>
#include <cstdint>
#include <opencv2/opencv.hpp>
#include "../models/IrisCode.h"
#include "Normalization.h"

// Extracts a 2048-bit IrisCode from a raw eye image.
// Pipeline:  decode → grayscale → segmentation → rubber-sheet normalization
//            → 2D Log-Gabor bank → phase quantization → IrisCode
class FeatureExtractor {
public:
    FeatureExtractor(int normWidth = 512, int normHeight = 64);

    // imageData: raw bytes of an eye image (JPEG / PNG / BMP encoded)
    IrisCode extract(const std::vector<uint8_t>& imageData) const;

private:
    Normalization m_norm;

    // Apply a 1D Log-Gabor filter along the angular axis of the normalised strip.
    // Returns phase-quantised bits and validity mask for one frequency band.
    void applyLogGabor(const cv::Mat& normalizedRow,
                       const cv::Mat& maskRow,
                       int            startBitIdx,
                       float          centerFreq,
                       float          bandwidth,
                       IrisCode&      code) const;
};
