#include "FeatureExtractor.h"
#include <cmath>
#include <stdexcept>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
FeatureExtractor::FeatureExtractor(int normWidth, int normHeight)
    : m_norm(normWidth, normHeight)
{}

// ─────────────────────────────────────────────────────────────────────────────
// extract
//
// Full pipeline:
//  1. Decode raw bytes → cv::Mat (BGR or grayscale)
//  2. Convert to grayscale + CLAHE equalisation
//  3. Segment iris (Hough circles) → IrisRegion
//  4. Rubber-sheet normalisation → 64×512 strip + occlusion mask
//  5. For each of 8 angular rows sample 4 frequency bands (Log-Gabor)
//     Each band contributes 64 bits → total 8×4×64 = 2048 bits
// ─────────────────────────────────────────────────────────────────────────────
IrisCode FeatureExtractor::extract(const std::vector<uint8_t>& imageData) const
{
    // 1. Decode
    cv::Mat raw = cv::imdecode(
        cv::Mat(1, static_cast<int>(imageData.size()), CV_8UC1,
                const_cast<uint8_t*>(imageData.data())),
        cv::IMREAD_GRAYSCALE);

    if (raw.empty())
        throw std::runtime_error("FeatureExtractor: failed to decode image");

    // 2. Enhance contrast with CLAHE
    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(2.0, cv::Size(8, 8));
    cv::Mat enhanced;
    clahe->apply(raw, enhanced);

    // 3. Segmentation
    IrisRegion region;
    if (!segmentIris(enhanced, region))
        throw std::runtime_error("FeatureExtractor: iris segmentation failed");

    // 4. Normalise
    cv::Mat occMask;
    cv::Mat normalized = m_norm.normalize(enhanced, region, occMask);

    // 5. Feature extraction: iterate over 8 horizontal bands of the normalised strip
    IrisCode code;
    const int stripH   = normalized.rows;   // 64
    const int stripW   = normalized.cols;   // 512
    const int numBands = 8;
    const int bandH    = stripH / numBands; // 8 rows per band

    // Four Log-Gabor centre frequencies (relative to Nyquist)
    const float freqs[4]  = { 0.1f, 0.2f, 0.3f, 0.4f };
    const float bw        = 0.5f;  // bandwidth (octaves)

    int bitIdx = 0;
    for (int band = 0; band < numBands; ++band) {
        // Average the rows of this band
        cv::Mat bandStrip = normalized.rowRange(band * bandH,
                                                (band + 1) * bandH);
        cv::Mat maskStrip = occMask.rowRange(band * bandH,
                                             (band + 1) * bandH);

        cv::Mat rowMean, maskMean;
        cv::reduce(bandStrip, rowMean, 0, cv::REDUCE_AVG, CV_32F);
        cv::reduce(maskStrip, maskMean, 0, cv::REDUCE_AVG, CV_32F);

        for (int f = 0; f < 4; ++f) {
            applyLogGabor(rowMean, maskMean, bitIdx, freqs[f], bw, code);
            bitIdx += stripW / 8;  // 64 bits per freq band per band
        }
    }

    return code;
}

// ─────────────────────────────────────────────────────────────────────────────
// applyLogGabor
//
// Convolves a 1-D signal (one row of the normalised strip) with a complex
// Log-Gabor filter in the frequency domain.  Phase is quantised to 2 bits
// (real/imaginary sign), contributing 1 bit per sample to the IrisCode.
//
// Log-Gabor transfer function:
//   H(f) = exp( -[log(f/f0)]^2 / (2*[log(bw)]^2) )
// ─────────────────────────────────────────────────────────────────────────────
void FeatureExtractor::applyLogGabor(const cv::Mat& normalizedRow,
                                     const cv::Mat& maskRow,
                                     int            startBitIdx,
                                     float          centerFreq,
                                     float          bandwidth,
                                     IrisCode&      code) const
{
    const int N = normalizedRow.cols;  // 512

    // Build DFT of signal
    cv::Mat padded;
    cv::copyMakeBorder(normalizedRow, padded,
                       0, 0, 0, N,   // right-pad with zeros to 2N for circular convolution
                       cv::BORDER_CONSTANT, 0);
    cv::Mat planes[] = { cv::Mat_<float>(padded), cv::Mat::zeros(padded.size(), CV_32F) };
    cv::Mat complexSignal;
    cv::merge(planes, 2, complexSignal);
    cv::dft(complexSignal, complexSignal);

    // Build Log-Gabor filter in frequency domain
    const double logBW2 = std::log(static_cast<double>(bandwidth));
    cv::Mat filterPlanes[2];
    filterPlanes[0] = cv::Mat::zeros(1, 2 * N, CV_32F);
    filterPlanes[1] = cv::Mat::zeros(1, 2 * N, CV_32F);

    for (int k = 1; k < 2 * N; ++k) {
        double f = static_cast<double>(k) / (2 * N);
        if (f < 1e-9) continue;
        double logRatio = std::log(f / static_cast<double>(centerFreq));
        double val      = std::exp(-(logRatio * logRatio) / (2.0 * logBW2 * logBW2));
        filterPlanes[0].at<float>(0, k) = static_cast<float>(val);
    }

    cv::Mat filterComplex;
    cv::merge(filterPlanes, 2, filterComplex);

    // Multiply in frequency domain
    cv::mulSpectrums(complexSignal, filterComplex, complexSignal, 0);

    // Inverse DFT → complex response
    cv::idft(complexSignal, complexSignal, cv::DFT_SCALE);

    // Phase quantisation: extract first N samples
    cv::Mat splitResult[2];
    cv::split(complexSignal, splitResult);

    int bitsPerBand = N / 8;   // 64 bits spread across the IrisCode bytes
    for (int i = 0; i < bitsPerBand; ++i) {
        int sampleIdx = i * 8;   // sub-sample: take every 8th pixel
        if (sampleIdx >= N) break;

        float realPart = splitResult[0].at<float>(0, sampleIdx);
        float maskVal  = maskRow.at<float>(0, sampleIdx);

        int globalBit = startBitIdx + i;  // global bit index in the 2048-bit array
        int byteIdx   = globalBit / 8;
        int bitOff    = 7 - (globalBit % 8);

        if (byteIdx >= 256) break;

        // Phase bit: 1 if real part >= 0
        if (realPart >= 0.f)
            code.bits[byteIdx] |= static_cast<uint8_t>(1u << bitOff);

        // Validity mask: pixel is valid if mask > 127 (not occluded)
        if (maskVal > 127.f)
            code.mask[byteIdx] |= static_cast<uint8_t>(1u << bitOff);
    }
}
