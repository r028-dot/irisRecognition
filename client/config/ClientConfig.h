#pragma once
using namespace std;

namespace iris {

// כל ההגדרות של הלקוח במקום אחד.
// אם רוצים לשנות כתובת שרת / נתיב / פורט — משנים כאן בלבד.
struct ClientConfig {

    // ── תקשורת עם השרת ─────────────────────────────────────
    string serverHost = "127.0.0.1";   // כתובת IP של השרת
    int    serverPort = 9000;           // פורט TCP

    // ── קבצי Haar Cascade לזיהוי פנים ועיניים ──────────────
    string faceCascadePath = "C:/iris_demo/haarcascade_frontalface_default.xml";
    string eyeCascadePath  = "C:/iris_demo/haarcascade_eye.xml";

    // ── מצלמה ───────────────────────────────────────────────
    int cameraIndex = 0;          // 0 = מצלמה ראשונה במחשב
    int captureWidth  = 1280;     // רזולוציית צילום (פיקסלים)
    int captureHeight = 720;

    // ── איכות תמונה ─────────────────────────────────────────
    double minBrightness = 40.0;  // בהירות מינימלית (0–255)
    double maxBrightness = 220.0; // בהירות מקסימלית
    double minSharpness  = 100.0; // חדות מינימלית (ערך Laplacian)
};

} // namespace iris
