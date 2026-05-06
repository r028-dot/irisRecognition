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

    // 2. Segmentation on the RAW image — CLAHE must not be applied before
    //    segmentation (it can shift local pupil intensity and destabilise centre).
    IrisRegion region;
    if (!segmentIris(raw, region))
        throw std::runtime_error("FeatureExtractor: iris segmentation failed");

    // 3. Rubber-sheet normalisation on the RAW image first
    cv::Mat occMask;
    cv::Mat normalized = m_norm.normalize(raw, region, occMask);

    // 4. Apply CLAHE to the NORMALISED STRIP (not to the original image).
    //    Applying CLAHE after normalisation ensures the tile grid is always
    //    aligned the same way relative to the iris strip regardless of where
    //    the iris appears in the source image.  This removes the per-image
    //    inconsistency that arises when CLAHE tiles straddle the iris at
    //    different offsets in different captures.
    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(2.0, cv::Size(16, 8));
    clahe->apply(normalized, normalized);

    // 5. Feature extraction: iterate over 8 horizontal bands of the normalised strip
    IrisCode code;
    const int stripH   = normalized.rows;   // 64
    const int stripW   = normalized.cols;   // 512

    // ── Feature extraction ────────────────────────────────────────────────────
    // Layout: 4 radial bands × 4 frequencies × 128 bits/call = 2048 bits total.
    // Each applyLogGabor call produces 64 real-sign + 64 imag-sign = 128 bits,
    // sampled every 8 strip columns (stride 8 → 64 angular positions, 5.625°/pos).
    // Nyquist for stride-8 sampling = 1/(2×8) = 0.0625 cycles/sample.
    // Centre frequencies {0.010, 0.020, 0.030, 0.042} are all below this limit,
    // avoiding aliasing that corrupted repeatability with the old stride-16 code.
    const int numBands = 4;
    const int bandH    = stripH / numBands; // 16 rows per band

    const float freqs[4] = { 0.010f, 0.020f, 0.030f, 0.042f };
    const float bw       = 0.85f;  // narrow BW → steep rolloff → alias-free

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
            bitIdx += stripW / 4;  // 128 bits per call (64 real + 64 imag)
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
//
// The iris strip (512 columns) represents the FULL 360° ring — it is periodic.
// We therefore use a circular DFT (no zero-padding) which correctly wraps
// around at the boundaries, unlike a zero-padded linear convolution that would
// treat the strip as a finite aperiodic signal and introduce edge distortion.
//
// The analytic filter (one-sided spectrum, doubled amplitude at positive
// frequencies) produces complex output whose real part is the even (cosine)
// response and imaginary part is the odd (sine) response.  Encoding both signs
// gives 2 bits per sample (Daugman's 2-bit phase quadrant encoding).
// ─────────────────────────────────────────────────────────────────────────────
void FeatureExtractor::applyLogGabor(const cv::Mat& normalizedRow,
                                     const cv::Mat& maskRow,
                                     int            startBitIdx,
                                     float          centerFreq,
                                     float          bandwidth,
                                     IrisCode&      code) const
{
    const int N = normalizedRow.cols;  // 512

    // ── Circular DFT (no zero-padding) ───────────────────────────────────────
    // The iris strip wraps around at 2π, so circular convolution is correct.
    cv::Mat planes[] = { cv::Mat_<float>(normalizedRow),
                         cv::Mat::zeros(normalizedRow.size(), CV_32F) };
    cv::Mat complexSignal;
    cv::merge(planes, 2, complexSignal);
    cv::dft(complexSignal, complexSignal);  // 512-point circular DFT

    // ── Analytic Log-Gabor filter ─────────────────────────────────────────────
    // Only positive frequencies k=1..N/2-1 are kept (doubled for energy).
    // DC (k=0) and Nyquist (k=N/2) and negative freqs (k=N/2+1..N-1) → 0.
    // f = k/N in cycles-per-sample; compare to centerFreq (also in c/sample).
    const double logBW2 = std::log(static_cast<double>(bandwidth));
    cv::Mat fp[2];
    fp[0] = cv::Mat::zeros(1, N, CV_32F);
    fp[1] = cv::Mat::zeros(1, N, CV_32F);

    for (int k = 1; k < N / 2; ++k) {
        double f = static_cast<double>(k) / N;   // cycles per sample
        double logRatio = std::log(f / static_cast<double>(centerFreq));
        double val = std::exp(-(logRatio * logRatio) / (2.0 * logBW2 * logBW2));
        fp[0].at<float>(0, k) = static_cast<float>(val * 2.0);  // *2 for analytic
    }

    cv::Mat filterComplex;
    cv::merge(fp, 2, filterComplex);

    // ── Filter in frequency domain, then IDFT ────────────────────────────────
    cv::mulSpectrums(complexSignal, filterComplex, complexSignal, 0);
    cv::idft(complexSignal, complexSignal, cv::DFT_SCALE);

    cv::Mat ch[2];
    cv::split(complexSignal, ch);  // ch[0]=real, ch[1]=imag

    // ── Encode 128 bits: 64 real-sign + 64 imaginary-sign ─────────────────────
    // Sample every 8th column → 64 angular positions (5.625° each).
    // Stride 8 gives Nyquist = 0.0625 c/s, above all four centre frequencies.
    // Only mark mask bit if the sample is not occluded (maskRow > 127).
    const int halfBits = N / 8;  // 64  (stride = 8)
    for (int i = 0; i < halfBits; ++i) {
        const int sampleIdx = i * 8;
        const bool valid    = (maskRow.at<float>(0, sampleIdx) > 127.f);

        // Real sign
        {
            int globalBit = startBitIdx + i;
            int byteIdx   = globalBit / 8;
            int bitOff    = 7 - (globalBit % 8);
            if (byteIdx < 256) {
                if (ch[0].at<float>(0, sampleIdx) >= 0.f)
                    code.bits[byteIdx] |= uint8_t(1u << bitOff);
                if (valid)
                    code.mask[byteIdx] |= uint8_t(1u << bitOff);
            }
        }
        // Imaginary sign
        {
            int globalBit = startBitIdx + halfBits + i;
            int byteIdx   = globalBit / 8;
            int bitOff    = 7 - (globalBit % 8);
            if (byteIdx < 256) {
                if (ch[1].at<float>(0, sampleIdx) >= 0.f)
                    code.bits[byteIdx] |= uint8_t(1u << bitOff);
                if (valid)
                    code.mask[byteIdx] |= uint8_t(1u << bitOff);
            }
        }
    }
}
