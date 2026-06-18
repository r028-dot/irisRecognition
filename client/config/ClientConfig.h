#pragma once
#include <string>

namespace iris {

// כל ההגדרות של הלקוח במקום אחד.
// אם רוצים לשנות כתובת שרת / נתיב / פורט — משנים כאן בלבד.
struct ClientConfig {

    // ── תקשורת עם השרת ─────────────────────────────────────
    std::string serverHost = "127.0.0.1";   // כתובת IP של השרת
    int         serverPort = 9000;          // פורט TCP

    // ── מצלמה ───────────────────────────────────────────────
    int cameraIndex   = 0;        // 0 = מצלמה ראשונה במחשב
    int captureWidth  = 1280;     // רזולוציית צילום (פיקסלים)
    int captureHeight = 720;

    // ── איכות תמונה ─────────────────────────────────────────
    double minBrightness = 40.0;  // בהירות מינימלית (0–255)
    double maxBrightness = 220.0; // בהירות מקסימלית
    double minSharpness  = 100.0; // חדות מינימלית (ערך Laplacian)
};

} // namespace iris
