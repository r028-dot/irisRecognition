#include "Display.h"
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

namespace iris {

// ── בנאי: יוצר חלון בשם שניתן ────────────────────────────────────────────
Display::Display(const std::string& windowName)
    : m_windowName(windowName)
{
    cv::namedWindow(m_windowName, cv::WINDOW_NORMAL);
    cv::resizeWindow(m_windowName, 800, 500);
}

// ── מפרק: סוגר את החלון ───────────────────────────────────────────────────
Display::~Display()
{
    cv::destroyWindow(m_windowName);
}

// ── show ──────────────────────────────────────────────────────────────────
// מציג תמונה עם פס סטטוס בתחתית:
//   ירוק  = AUTHORIZED
//   אדום  = כל תוצאה אחרת
void Display::show(const cv::Mat& image, const std::string& status) const
{
    // גובה פס הסטטוס
    const int barHeight = 50;

    cv::Mat canvas(image.rows + barHeight, image.cols, CV_8UC3);

    // מעתיק את התמונה לחלק העליון (ממיר ל-BGR אם צריך)
    cv::Mat top = canvas(cv::Rect(0, 0, image.cols, image.rows));
    if (image.channels() == 1)
        cv::cvtColor(image, top, cv::COLOR_GRAY2BGR);
    else
        image.copyTo(top);

    // פס סטטוס
    cv::Mat bar = canvas(cv::Rect(0, image.rows, image.cols, barHeight));
    bool authorized = (status == "AUTHORIZED");
    bar.setTo(authorized ? cv::Scalar(0, 160, 0) : cv::Scalar(0, 0, 180));

    cv::putText(bar, status,
                cv::Point(12, 34),
                cv::FONT_HERSHEY_DUPLEX, 1.0,
                cv::Scalar(255, 255, 255), 2, cv::LINE_AA);

    cv::imshow(m_windowName, canvas);
}

// ── showMessage ───────────────────────────────────────────────────────────
// מציג מסך שחור עם הודעת טקסט — לשגיאות ואיכות נמוכה
void Display::showMessage(const std::string& message) const
{
    cv::Mat canvas(200, 640, CV_8UC3, cv::Scalar(30, 30, 30));

    cv::putText(canvas, message,
                cv::Point(20, 110),
                cv::FONT_HERSHEY_SIMPLEX, 0.75,
                cv::Scalar(200, 200, 200), 2, cv::LINE_AA);

    cv::imshow(m_windowName, canvas);
}

// ── waitKey ───────────────────────────────────────────────────────────────
// ממתין ms מילישניות ומחזיר קוד מקש (27 = ESC)
int Display::waitKey(int ms) const
{
    return cv::waitKey(ms);
}

} // namespace iris
