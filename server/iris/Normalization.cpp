#include "Normalization.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <vector>
using namespace std;
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

//פונקציה האחראית לדגום פיקסל בתמונה המקורית לפי קואורדינטות קוטביות (זווית ורדיוס) ולחשב את ערכו הממוצע לפי משקלות ליניאריות של הפיקסלים הסמוכים. אם הקואורדינטות מחוץ לתמונה, הפונקציה מחזירה false.
static bool samplePixel(const cv::Mat& src, float sx, float sy,
                         float fW, float fH, uint8_t& outVal)
{
    if (sx < 0.f || sx > fW || sy < 0.f || sy > fH)
        return false;
    int x0 = int(sx), y0 = int(sy);
    int x1 = min(x0 + 1, (int)fW);
    int y1 = min(y0 + 1, (int)fH);
    float fx = sx - x0, fy = sy - y0;
    float v = (1-fx)*(1-fy)*src.at<uint8_t>(y0, x0)
              + fx *(1-fy)*src.at<uint8_t>(y0, x1)
              + (1-fx)* fy *src.at<uint8_t>(y1, x0)
              + fx * fy *src.at<uint8_t>(y1, x1);
    outVal = uint8_t(v + 0.5f);
    return true;
}

// פונקציה פנימית לסינון פיקסלים חריגים לפי MAD (Median Absolute Deviation) לכל שורה בתמונה המנורמלת. הפיקסלים החריגים מסומנים במסכה כלא תקינים (0).
static void applyMadRowMask(const uint8_t* rowPtr, uint8_t* maskPtr, int width)
{
    vector<uint8_t> rowBuf(rowPtr, rowPtr + width);
    std::nth_element(rowBuf.begin(), rowBuf.begin() + width / 2, rowBuf.end());
    uint8_t med = rowBuf[width / 2];
    vector<uint8_t> absDev(width);
    for (int c = 0; c < width; ++c)
        absDev[c] = uint8_t(std::abs(int(rowPtr[c]) - int(med)));
    std::nth_element(absDev.begin(), absDev.begin() + width / 2, absDev.end());
    int mad = absDev[width / 2];
    int thr = max(MAD_MIN_DEVIATION, int(MAD_MULTIPLIER * max(mad, MAD_MIN_MAD)));
    for (int c = 0; c < width; ++c)
        if (int(rowPtr[c]) - int(med) > thr)
            maskPtr[c] = 0;
}

// בנאי המגדיר גודל קבוע לתמונה המנורמלת של הקשתית (Rubber Sheet Model).
Normalization::Normalization(int outWidth, int outHeight)
    : m_width(outWidth), m_height(outHeight)
{}

// ממירה את אזור הקשתית בתמונה לצורה מלבנית בגודל קבוע (Rubber Sheet Model).
CV_EXPORTS cv::Mat Normalization::normalize(const cv::Mat& srcGray,
                                             const IrisRegion& region,
                                             cv::Mat& occMask) const
{
    cv::Mat normalized(m_height, m_width, CV_8UC1, cv::Scalar(0));
    occMask = cv::Mat(m_height, m_width, CV_8UC1, cv::Scalar(255));
    const float fW = float(srcGray.cols - 1), fH = float(srcGray.rows - 1);

    // לכל פיקסל בתמונה המנורמלת, לחשב את המיקום המקביל בתמונה המקורית לפי מודל Rubber Sheet של Daugman.
    for (int row = 0; row < m_height; ++row) {
        float rNorm = (float(row) + 0.5f) / float(m_height);
        // חישוב ממוצע משוקלל לטיפול במצב שבו האישון והקשתית אינם בעלי אותו מרכז
        float cx = region.pupilCenter.x * (1.f - rNorm) + region.irisCenter.x * rNorm;
        float cy = region.pupilCenter.y * (1.f - rNorm) + region.irisCenter.y * rNorm;
        float R  = region.pupilRadius   * (1.f - rNorm) + region.irisRadius   * rNorm;
        uint8_t* normPtr = normalized.ptr<uint8_t>(row);
        uint8_t* maskPtr = occMask.ptr<uint8_t>(row);
        for (int col = 0; col < m_width; ++col) {
            // מעבר מקואורדינטות קוטביות (זווית ורדיוס) לקואורדינטות קרטזיות (X ו-Y)
            double theta = 2.0 * M_PI * col / m_width;
            float srcX = cx + R * float(cos(theta));
            float srcY = cy + R * float(sin(theta));
            
            if (!samplePixel(srcGray, srcX, srcY, fW, fH, normPtr[col]))
                maskPtr[col] = 0;
        }
    }
    // סינון פיקסלים בהירים וכהים מדי מהמסכה
    cv::Mat brightMask, darkMask;
    cv::threshold(normalized, brightMask, NORM_BRIGHT_THRESHOLD, 255, cv::THRESH_BINARY);
    cv::bitwise_and(occMask, ~brightMask, occMask);
    cv::threshold(normalized, darkMask, NORM_DARK_THRESHOLD, 255, cv::THRESH_BINARY_INV);
    cv::bitwise_and(occMask, ~darkMask, occMask);
    // סינון פיקסלים חריגים לפי MAD לכל שורה
    for (int r = 0; r < m_height; ++r)
        applyMadRowMask(normalized.ptr<uint8_t>(r), occMask.ptr<uint8_t>(r), m_width);
    return normalized;
}

