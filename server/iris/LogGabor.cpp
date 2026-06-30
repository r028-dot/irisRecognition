#include "LogGabor.h"
#include <cmath>
#include <vector>
using namespace std;

// חילוץ מאפייני הקשתית בעזרת מסנן Log-Gabor והמרתם לקוד בינארי
void applyLogGabor(const cv::Mat& normalizedRow,
                   const cv::Mat& maskRow,
                   int            startBitIdx,
                   float          centerFreq,
                   float          bandwidth,
                   IrisCode&      code)
{
    const int N = normalizedRow.cols;
    // המרת האות למספרים מרוכבים לצורך מעבר לתחום התדר
    cv::Mat planes[] = { cv::Mat_<float>(normalizedRow),
                         cv::Mat::zeros(normalizedRow.size(), CV_32F) };
    cv::Mat complexSignal;
    cv::merge(planes, 2, complexSignal);
    // מעבר ממרחב הפיקסלים למרחב התדרים
    cv::dft(complexSignal, complexSignal);
    const double logBW2 = std::log(static_cast<double>(bandwidth));
    // יצירת מסנן Log-Gabor המדגיש תדרים סביב התדר הרצוי
    cv::Mat fp[2];
    fp[0] = cv::Mat::zeros(1, N, CV_32F);
    fp[1] = cv::Mat::zeros(1, N, CV_32F);
    for (int k = 1; k < N / 2; ++k) {
        double f = static_cast<double>(k) / N;
        double logRatio = std::log(f / static_cast<double>(centerFreq));
        double val = std::exp(-(logRatio * logRatio) /
                       (2.0 * logBW2 * logBW2));
        fp[0].at<float>(0, k) = static_cast<float>(val * 2.0);
    }
    cv::Mat filterComplex;
    cv::merge(fp, 2, filterComplex);
    // סינון התדרים הרצויים בלבד
    cv::mulSpectrums(complexSignal, filterComplex, complexSignal, 0);
    // חזרה למרחב הפיקסלים לאחר הסינון
    cv::idft(complexSignal, complexSignal, cv::DFT_SCALE);
    cv::Mat ch[2];
    cv::split(complexSignal, ch);
    const int halfBits = N / GABOR_SAMPLE_STRIDE;
    vector<float> magR(halfBits), magI(halfBits);
    // שמירת עוצמת התגובה לצורך זיהוי ביטים לא אמינים
    for (int i = 0; i < halfBits; ++i) {
        magR[i] = std::fabs(ch[0].at<float>(0, i * 8));
        magI[i] = std::fabs(ch[1].at<float>(0, i * 8));
    }
    auto medianOf = [](vector<float>& v) {
        std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
        return v[v.size() / 2];
    };
    vector<float> magRcopy = magR, magIcopy = magI;
    // קביעת סף לזיהוי תגובות חלשות
    const float fragileThR = 0.30f * medianOf(magRcopy);
    const float fragileThI = 0.30f * medianOf(magIcopy);
    for (int i = 0; i < halfBits; ++i) {
        const int sampleIdx = i * GABOR_SAMPLE_STRIDE;
        const bool occOK = (maskRow.at<float>(0, sampleIdx) > 127.f);
        // יצירת ביט מהסימן של החלק הממשי של התגובה
        {
            int globalBit = startBitIdx + i;
            int byteIdx = globalBit / 8;
            int bitOff = 7 - (globalBit % 8);
            if (byteIdx < 256) {
                if (ch[0].at<float>(0, sampleIdx) >= 0.f)
                    code.bits[byteIdx] |= uint8_t(1u << bitOff);
                if (occOK && magR[i] >= fragileThR)
                    code.mask[byteIdx] |= uint8_t(1u << bitOff);
            }
        }
        // יצירת ביט מהסימן של החלק המדומה של התגובה
        {
            int globalBit = startBitIdx + halfBits + i;
            int byteIdx = globalBit / 8;
            int bitOff = 7 - (globalBit % 8);
            if (byteIdx < 256) {
                if (ch[1].at<float>(0, sampleIdx) >= 0.f)
                    code.bits[byteIdx] |= uint8_t(1u << bitOff);
                if (occOK && magI[i] >= fragileThI)
                    code.mask[byteIdx] |= uint8_t(1u << bitOff);
            }
        }
    }
}