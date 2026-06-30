#include "FeatureExtractor.h"
#include "LogGabor.h"
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
    , m_normWidth(normWidth)
    , m_normHeight(normHeight)
{}


// שלבי קו הייצור:
// 1. שיפור תמונה: הפיכה לשחור-לבן והדגשת פרטים.
// 2.IDO3D איתור (סגמנטציה): מציאת גבולות האישון והקשתית לפי אלגוריתם דאגמן.
// 3. נרמול: מתיחת העין העגולה למלבן ישר בגודל קבוע.
// 4. קידוד: הפיכת הפיקסלים במלבן לקוד בינארי סופי (אפסים ואחדות).
 
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
        cv::threshold(raw, reflMask, REFLECTION_THRESHOLD, 255, cv::THRESH_BINARY);
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
    // נרמול גיאומטרי של אזור הקשתית למלבן אחיד (Rubber-Sheet)
    cv::Mat occMask;
    cv::Mat normalized = m_norm.normalize(raw, region, occMask);
    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(CLAHE_CLIP_LIMIT, cv::Size(CLAHE_TILE_W, CLAHE_TILE_H));
    clahe->apply(normalized, normalized);
    IrisCode code;
    const int stripH = normalized.rows;  
    const int stripW = normalized.cols;  
    const int bandH = stripH / NUM_GABOR_BANDS;
    int bitIdx = 0;
    // חלוקה של המלבן למספר רצועות אופקיות, חישוב ממוצע לכל רצועה, והחלת פילטר לוג-גבור על כל רצועה
    for (int band = 0; band < NUM_GABOR_BANDS; ++band) {
        cv::Mat bandStrip = normalized.rowRange(band * bandH,(band + 1) * bandH);
        cv::Mat maskStrip = occMask.rowRange(band * bandH,(band + 1) * bandH);
        cv::Mat rowMean, maskMean;
        cv::reduce(bandStrip, rowMean, 0, cv::REDUCE_AVG, CV_32F);
        cv::reduce(maskStrip, maskMean, 0, cv::REDUCE_AVG, CV_32F);
        for (int f = 0; f < NUM_GABOR_BANDS; ++f) {
            ::applyLogGabor(rowMean, maskMean, bitIdx, GABOR_FREQS[f], GABOR_BANDWIDTH, code);
            bitIdx += stripW / 4;  
        }
    }
    return code;
}

