#include "FeatureExtractor.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>
using namespace std;
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


FeatureExtractor::FeatureExtractor(int normWidth, int normHeight)
    : m_norm(normWidth, normHeight)
{}

// תהליך חילוץ המאפיינים (Feature Extraction):
// 1. פענוח התמונה הגולמית והמרתה לגווני אפור (כולל שיפור ניגודיות CLAHE).
// 2. סגמנטציה ואיתור גבולות הקשתית בעזרת התמרת הוף (Hough Circles).
// 3. נרמול גיאומטרי של אזור העין למלבן אחיד (שיטת Rubber-Sheet).
// 4. קידוד ביומטרי באמצעות פילטרים מתמטיים (Log-Gabor) ליצירת קוד בינארי של 2048 ביט.
IrisCode FeatureExtractor::extract(const vector<uint8_t>& imageData) const
{
    //ממיר את הנתונים הבינאריים של התמונה הגולמית למטריצה של OpenCV
    cv::Mat raw = cv::imdecode(
        cv::Mat(1, static_cast<int>(imageData.size()), CV_8UC1,
                const_cast<uint8_t*>(imageData.data())),
        cv::IMREAD_GRAYSCALE);
    if (raw.empty())
        throw runtime_error("FeatureExtractor: failed to decode image");
    {
        cv::Mat reflMask;
        // זיהוי והסרת השתקפויות (Reflections) בתמונה הגולמית
        cv::threshold(raw, reflMask, 235, 255, cv::THRESH_BINARY);
        // הרחבת המסכה של ההשתקפויות כדי לכסות את כל האזורים המושתקפים
        cv::dilate(reflMask, reflMask,
                   cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5)));
        // אם יש אזורים מושתקפים, מבצעים ציור מחדש כדי לשחזר את האזורים הללו בתמונה
        if (cv::countNonZero(reflMask) > 0) {
            cv::Mat inpainted;
            cv::inpaint(raw, reflMask, inpainted, 3, cv::INPAINT_TELEA);
            raw = inpainted;
        }
    }
    // סגמנטציה של הקשתית
    IrisRegion region;
    if (!segmentIris(raw, region))
        throw runtime_error("FeatureExtractor: iris segmentation failed");
    cv::Mat occMask;
    cv::Mat normalized = m_norm.normalize(raw, region, occMask);
    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(2.0, cv::Size(16, 8));
    clahe->apply(normalized, normalized);
    IrisCode code;
    const int stripH   = normalized.rows;  
    const int stripW   = normalized.cols;  
    const int numBands = 4;
    const int bandH    = stripH / numBands; 
    const float freqs[4] = { 0.010f, 0.020f, 0.030f, 0.042f };
    const float bw       = 0.85f;  
    int bitIdx = 0;
    for (int band = 0; band < numBands; ++band) {
        cv::Mat bandStrip = normalized.rowRange(band * bandH,(band + 1) * bandH);
        cv::Mat maskStrip = occMask.rowRange(band * bandH,(band + 1) * bandH);
        cv::Mat rowMean, maskMean;
        cv::reduce(bandStrip, rowMean, 0, cv::REDUCE_AVG, CV_32F);
        cv::reduce(maskStrip, maskMean, 0, cv::REDUCE_AVG, CV_32F);
        for (int f = 0; f < 4; ++f) {
            applyLogGabor(rowMean, maskMean, bitIdx, freqs[f], bw, code);
            bitIdx += stripW / 4;  
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
    const int N = normalizedRow.cols;  
    cv::Mat planes[] = { cv::Mat_<float>(normalizedRow),
                         cv::Mat::zeros(normalizedRow.size(), CV_32F) };
    cv::Mat complexSignal;
    cv::merge(planes, 2, complexSignal);
    cv::dft(complexSignal, complexSignal);  
    const double logBW2 = std::log(static_cast<double>(bandwidth));
    cv::Mat fp[2];
    fp[0] = cv::Mat::zeros(1, N, CV_32F);
    fp[1] = cv::Mat::zeros(1, N, CV_32F);
    for (int k = 1; k < N / 2; ++k) {
        double f = static_cast<double>(k) / N;   
        double logRatio = std::log(f / static_cast<double>(centerFreq));
        double val = std::exp(-(logRatio * logRatio) / (2.0 * logBW2 * logBW2));
        fp[0].at<float>(0, k) = static_cast<float>(val * 2.0);  
    }
    cv::Mat filterComplex;
    cv::merge(fp, 2, filterComplex);
    cv::mulSpectrums(complexSignal, filterComplex, complexSignal, 0);
    cv::idft(complexSignal, complexSignal, cv::DFT_SCALE);
    cv::Mat ch[2];
    cv::split(complexSignal, ch);  
    const int halfBits = N / 8;  
    vector<float> magR(halfBits), magI(halfBits);
    for (int i = 0; i < halfBits; ++i) {
        magR[i] = std::fabs(ch[0].at<float>(0, i * 8));
        magI[i] = std::fabs(ch[1].at<float>(0, i * 8));
    }
    auto medianOf = [](vector<float>& v) {
        std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
        return v[v.size() / 2];
    };
    vector<float> magRcopy = magR, magIcopy = magI;
    const float fragileThR = 0.30f * medianOf(magRcopy);
    const float fragileThI = 0.30f * medianOf(magIcopy);
    for (int i = 0; i < halfBits; ++i) {
        const int sampleIdx = i * 8;
        const bool occOK    = (maskRow.at<float>(0, sampleIdx) > 127.f);
        {
            int globalBit = startBitIdx + i;
            int byteIdx   = globalBit / 8;
            int bitOff    = 7 - (globalBit % 8);
            if (byteIdx < 256) {
                if (ch[0].at<float>(0, sampleIdx) >= 0.f)
                    code.bits[byteIdx] |= uint8_t(1u << bitOff);
                if (occOK && magR[i] >= fragileThR)
                    code.mask[byteIdx] |= uint8_t(1u << bitOff);
            }
        }
        {
            int globalBit = startBitIdx + halfBits + i;
            int byteIdx   = globalBit / 8;
            int bitOff    = 7 - (globalBit % 8);
            if (byteIdx < 256) {
                if (ch[1].at<float>(0, sampleIdx) >= 0.f)
                    code.bits[byteIdx] |= uint8_t(1u << bitOff);
                if (occOK && magI[i] >= fragileThI)
                    code.mask[byteIdx] |= uint8_t(1u << bitOff);
            }
        }
    }
}
