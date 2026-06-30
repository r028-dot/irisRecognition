#pragma once
#include <opencv2/opencv.hpp>
#include "../models/IrisCode.h"

// צעד הדגימה של לוג-גבור: כלכמה פיקסלים לוקחים דגימה אחת מתוך שורת הנורמליזציה
// (N/STRIDE דגימות מתוך N פיקסלים בשורה באורך 512 → 64 ביטים לכל רצועה × תדר)
static constexpr int GABOR_SAMPLE_STRIDE = 8;

// הפעלת פילטר לוג-גבור על שורה נרמלת של תמונת קשתית, והמרתה לקוד בינארי סופי (IrisCode).
void applyLogGabor(const cv::Mat& normalizedRow,
                   const cv::Mat& maskRow,
                   int            startBitIdx,
                   float          centerFreq,
                   float          bandwidth,
                   IrisCode&      code);
