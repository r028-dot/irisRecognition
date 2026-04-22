#include "config/ClientConfig.h"
#include "capture/CameraCapture.h"
#include "preprocessing/ImageQualityCheck.h"
#include "preprocessing/Segmentation.h"
#include "security/Encryptor.h"
#include "network/ServerClient.h"
#include "ui/Display.h"
#include <opencv2/imgcodecs.hpp>
#include <iostream>
#include <vector>
using namespace iris;
using namespace std;

int main() {
    // ── 1. טעינת הגדרות ────────────────────────────────
    ClientConfig config;

    // ── 2. אתחול רכיבים ────────────────────────────────
    CameraCapture    camera(config);
    ImageQualityCheck quality(config);
    Segmentation     segmentation(config.faceCascadePath,
                                        config.eyeCascadePath);
    ServerClient     server(config);
    Display          display("Iris Recognition");

    // מפתח + IV לדוגמה  בפרויקט אמיתי יטענו מקובץ מאובטח
    vector<uint8_t> key(32, 0xAB);
    vector<uint8_t> iv (16, 0xCD);
    Encryptor encryptor(key, iv);

    cout << "[Iris Client] Ready. Press ESC to quit.\n";

    // ── 3. לולאה ראשית ─────────────────────────────────
    while (true) {
        // שלב א: צלם פריים
        cv::Mat frame = camera.capture();
        if (frame.empty()) continue;

        // שלב ב: בדוק איכות תמונה
        QualityResult q = quality.check(frame);
        if (!q.passed) {
            display.showMessage("Low quality: " + q.reason);
            if (display.waitKey(30) == 27) break;
            continue;
        }

        // שלב ג: חלץ קשתית
        SegmentationResult seg = segmentation.process(frame);
        if (!seg.valid) {
            display.showMessage("No iris detected");
            if (display.waitKey(30) == 27) break;
            continue;
        }

        // שלב ד: המר תמונה לבתים
        vector<uint8_t> imgBytes;
        cv::imencode(".png", seg.irisImage, imgBytes);

        // שלב ה: הצפן
        vector<uint8_t> encrypted = encryptor.encrypt(imgBytes);

        // שלב ו: שלח לשרת וקבל תשובה
        AuthResponse resp = server.sendIrisImage(encrypted);

        // שלב ז: הצג תוצאה
        string statusText = (resp.status == AuthStatus::AUTHORIZED)
                                 ? "AUTHORIZED" : "DENIED";
        display.show(frame, statusText);

        if (display.waitKey(0) == 27) break;  // ESC = יציאה
    }

    return 0;
}
