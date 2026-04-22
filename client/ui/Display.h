#pragma once
#include <opencv2/core.hpp>
#include <string>
using namespace std;

namespace iris {

class Display {
public:
    explicit Display(const string& windowName = "Iris Recognition");
    ~Display();

    // מציג תמונה עם הודעת סטטוס
    void show(const cv::Mat& image, const string& status) const;

    // מציג הודעה בלי תמונה (מסך שחור + טקסט)
    void showMessage(const string& message) const;

    // ממתין ללחיצת מקש. מחזיר את קוד המקש (27=ESC)
    int waitKey(int ms = 1) const;

private:
    string m_windowName;
};

} // namespace iris
