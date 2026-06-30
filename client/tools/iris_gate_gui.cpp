
#define WIN32_LEAN_AND_MEAN
#define _HAS_STD_BYTE 0    
#ifndef UNICODE
#  define UNICODE
#endif
#ifndef _UNICODE
#  define _UNICODE
#endif
#include <windows.h>
#include <objidl.h>           
#include <windowsx.h>
#include <commctrl.h>
#include <gdiplus.h>
#ifdef min
#  undef min
#endif
#ifdef max
#  undef max
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <cstring>
#include <cstdlib>

#include "config/ClientConfig.h"
#include "security/Encryptor.h"
#include "network/ServerClient.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")

namespace fs = std::filesystem;
using namespace iris;
using namespace std;
//  פלטת צבעים 
static const COLORREF C_BG      = RGB( 10,  20,  42);
static const COLORREF C_PANEL   = RGB( 16,  30,  60);
static const COLORREF C_EDIT_BG = RGB( 13,  26,  53);
static const COLORREF C_ACCENT  = RGB( 24, 103, 214);
static const COLORREF C_TEXT1   = RGB(230, 240, 255);
static const COLORREF C_TEXT2   = RGB(140, 168, 210);
static const COLORREF C_TEXT3   = RGB( 80, 110, 158);
static const COLORREF C_GREEN   = RGB( 44, 200,  82);
static const COLORREF C_RED     = RGB(225,  52,  48);
static const COLORREF C_PROG_BG = RGB( 22,  44,  88);
static const COLORREF C_DIVIDER = RGB( 28,  54, 100);

//  מזהי פקדים 
#define IDC_EDIT_PASSID  101
#define IDC_EDIT_GATE    102
#define IDC_EDIT_CASIA   103
#define IDC_BTN_SCAN     104
#define IDC_TIMER_DOTS   200

//  הודעות thread → UI 
#define WM_SCAN_PROGRESS (WM_APP + 1)
#define WM_SCAN_DONE     (WM_APP + 2)

//  קבועים 
static constexpr int ENROLL_COUNT  = 3;  // תמונות שנרשמו — מדלגים עליהן
static constexpr int VERIFY_COUNT  = 3;  // תמונות לאימות
static constexpr int WIN_W         = 500;
static constexpr int WIN_H         = 720;
static constexpr int HEADER_H      = 158;
static constexpr int FORM_PAD      = 28;
static constexpr int FIELD_H       = 34;

//  מצב גלובלי 
enum class ScanState { Idle, Scanning, Match, NoMatch, Error };

struct ScanResult {
    bool match = false;
    bool commError  = false;
    double leftHD = 1.0;
    double rightHD = 1.0;
    wstring name;
    wstring flightNumber;
    wstring seatNumber;
    wstring gate;
    wstring errorMsg;
    wstring serverMsg;
};

static HWND g_hwnd       = nullptr;
static HWND g_editPassID = nullptr;
static HWND g_editGate   = nullptr;
static HWND g_editCasia  = nullptr;
static HWND g_btnScan    = nullptr;
static ScanState g_state      = ScanState::Idle;
static int g_progress   = 0;
static wstring g_statusMsg  = L"הכנס פרטים ולחץ התחל סריקה";
static ScanResult g_result;
static int g_dotCount   = 0;
static bool g_btnHover   = false;

// משאבים GDI
static HBRUSH g_brBg    = nullptr;
static HBRUSH g_brPanel = nullptr;
static HBRUSH g_brEdit  = nullptr;
static HFONT  g_fTitle  = nullptr;
static HFONT  g_fLabel  = nullptr;
static HFONT  g_fInput  = nullptr;
static HFONT  g_fResult = nullptr;
static HFONT  g_fDetail = nullptr;

// GDI+
static Gdiplus::Bitmap* g_logo  = nullptr;
static ULONG_PTR         g_gdipT = 0;

//  המרות מחרוזת 
static wstring toWide(const string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    wstring w(n - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
static string toUtf8(const wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    string s(n - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}
static wstring getEdit(HWND h) {
    wchar_t buf[512] = {};
    GetWindowTextW(h, buf, 512);
    return buf;
}

// מחשב חדות תמונה (Laplacian variance)
static double computeSharpness(const fs::path& imgPath) {
    std::ifstream f(imgPath, std::ios::binary);
    vector<uint8_t> raw(
        (std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>());
    cv::Mat gray = cv::imdecode(raw, cv::IMREAD_GRAYSCALE);
    if (gray.empty()) return 0.0;
    cv::Mat lap;
    cv::Laplacian(gray, lap, CV_64F);
    cv::Scalar mu, sigma;
    cv::meanStdDev(lap, mu, sigma);
    return sigma[0] * sigma[0];
}

// טוען תמונה לבתים PNG (לצורך שליחה)
static vector<uint8_t> loadAsPng(const fs::path& imgPath) {
    std::ifstream f(imgPath, std::ios::binary);
    vector<uint8_t> raw(
        (std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>());
    cv::Mat gray = cv::imdecode(raw, cv::IMREAD_GRAYSCALE);
    if (gray.empty()) return {};
    vector<uint8_t> png;
    cv::imencode(".png", gray, png);
    return png;
}

//מחלקה לייצוג תמונה עם ציון חדות, נתיב וקובץ PNG
struct ScoredImage {
    fs::path path;
    double sharpness = 0.0;
    vector<uint8_t> pngBytes;
};

// בוחר VERIFY_COUNT תמונות:
//   1. ממיין לפי שם קובץ
//   2. מדלג על תמונות הרישום: קורא enrolled.txt אם קיים, אחרת ENROLL_COUNT הראשונות
//   3. מחשב חדות לשאריות
//   4. מחזיר VERIFY_COUNT בעלי החדות הגבוהה ביותר
static vector<ScoredImage> pickBestImages(
    const fs::path& dir,
    const wstring& eyeLabel,
    HWND progressHwnd,
    int progressBase)
{
    // איסוף קבצי תמונה
    vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp")
            files.push_back(e.path());
    }

    // מיון אלפבתי
    std::sort(files.begin(), files.end());

    // דילוג על תמונות הרישום
    fs::path enrolledTxt = dir / "enrolled.txt";
    if (fs::exists(enrolledTxt)) {
        // קריאת שמות הקבצים שנרשמו מ-enrolled.txt (נוצר ע"י Admin GUI)
        std::unordered_set<string> enrolledSet;
        std::ifstream ef(enrolledTxt);
        string fname;
        while (std::getline(ef, fname)) {
            if (!fname.empty() && fname.back() == '\r') fname.pop_back();
            if (!fname.empty()) enrolledSet.insert(fname);
        }
        files.erase(std::remove_if(files.begin(), files.end(),
            [&enrolledSet](const fs::path& p) {
                return enrolledSet.count(p.filename().string()) > 0;
            }), files.end());
    } else {
        // מצב מורשת (EnrollTool): מדלג על ENROLL_COUNT הראשונות אלפביתית
        if ((int)files.size() > ENROLL_COUNT)
            files.erase(files.begin(), files.begin() + ENROLL_COUNT);
    }

    // דירוג לפי חדות
    vector<ScoredImage> scored;
    int total = (int)files.size();
    for (int i = 0; i < total; ++i) {
        // שלח עדכון התקדמות לחלון
        if (progressHwnd) {
            int pct = progressBase + (i * 35 / (total > 0 ? total : 1));
            wstring* msg = new wstring(L"מנתח עין " + eyeLabel + L"...");
            PostMessageW(progressHwnd, WM_SCAN_PROGRESS, (WPARAM)pct, (LPARAM)msg);
        }
        ScoredImage si;
        si.path      = files[i];
        si.sharpness = computeSharpness(files[i]);
        si.pngBytes  = loadAsPng(files[i]);
        if (!si.pngBytes.empty())
            scored.push_back(std::move(si));
    }

    // מיון יורד לפי חדות
    std::sort(scored.begin(), scored.end(),
        [](const ScoredImage& a, const ScoredImage& b) {
            return a.sharpness > b.sharpness;
        });

    // החזר עד VERIFY_COUNT
    if ((int)scored.size() > VERIFY_COUNT)
        scored.resize(VERIFY_COUNT);

    return scored;
}

//  מציאת תיקיית CASIA
static fs::path findCasiaRoot() {
    // תחפש env var קודם
    const char* envPath = std::getenv("IRIS_CASIA_ROOT");
    if (envPath && *envPath && fs::exists(envPath)) return fs::canonical(envPath);

    // נתיבים יחסיים מהתיקייה של ה-EXE
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    fs::path exeDir = fs::path(exePath).parent_path();

    const std::vector<fs::path> cands = {
        exeDir / ".." / ".." / ".." / ".." / "test_images" / "CASIA-Iris-Thousand" / "CASIA-Iris-Thousand",
        exeDir / ".." / ".." / ".." / "test_images" / "CASIA-Iris-Thousand" / "CASIA-Iris-Thousand",
        exeDir / ".." / ".." / "test_images" / "CASIA-Iris-Thousand" / "CASIA-Iris-Thousand",
        exeDir / "test_images" / "CASIA-Iris-Thousand" / "CASIA-Iris-Thousand",
    };
    for (const auto& p : cands)
        if (fs::exists(p) && fs::is_directory(p)) return fs::canonical(p);
    return {};
}

//פרמטרים ל-thread הסריקה
struct ScanParams {
    HWND hwnd;
    string passengerID;
    string gateName;
    string casiaID;
    ClientConfig cfg;
};

static DWORD WINAPI ScanWorker(LPVOID param) {
    ScanParams* p = (ScanParams*)param;
    HWND hwnd = p->hwnd;
    ScanResult* res = new ScanResult();

    try {
        // מציאת תיקיית CASIA-Iris-Thousand 
        fs::path casiaRoot = findCasiaRoot();
        if (casiaRoot.empty())
            throw runtime_error("לא נמצאה תיקיית CASIA-Iris-Thousand.\n"
                "הגדר: $env:IRIS_CASIA_ROOT = \"<נתיב לתיקייה>\"");

        fs::path personDir = casiaRoot / p->casiaID;
        if (!fs::exists(personDir))
            throw runtime_error("אדם " + p->casiaID + " לא קיים ב-CASIA");

        fs::path leftDir  = personDir / "L";
        fs::path rightDir = personDir / "R";
        if (!fs::exists(leftDir) || !fs::exists(rightDir))
            throw runtime_error("חסרות תיקיות L/R עבור " + p->casiaID);

        //  בחירת תמונות — חדות בלבד 
        auto leftBest  = pickBestImages(leftDir,  L"שמאל", hwnd, 5);
        auto rightBest = pickBestImages(rightDir, L"ימין",  hwnd, 42);

        if (leftBest.empty() || rightBest.empty())
            throw runtime_error("לא נמצאו תמונות ראויות לאחר דילוג על תמונות הרישום");
        if ((int)leftBest.size() != VERIFY_COUNT || (int)rightBest.size() != VERIFY_COUNT)
            throw runtime_error("נדרשות בדיוק 3 תמונות לכל עין לצורך אימות בשער");

        //  הצפנה (iris::Encryptor מ-client/security) 
        {
            wstring* msg = new wstring(L"מצפין תמונות (AES-256-CBC)...");
            PostMessageW(hwnd, WM_SCAN_PROGRESS, 80, (LPARAM)msg);
        }
        Encryptor enc;  // קורא IRIS_AES_KEY מ-env

        // IV אקראי אחד משותף לכל התמונות בסשן זה
        uint8_t iv[16] = {};
        vector<vector<uint8_t>> encLeft, encRight;

        // מצפין עין שמאל עם IV — קורא generate IV מהתמונה הראשונה
        encLeft.push_back(enc.encrypt(leftBest[0].pngBytes, iv)); // iv מאותחל כאן
        for (int i = 1; i < (int)leftBest.size(); ++i)
            encLeft.push_back(enc.encryptWithIV(leftBest[i].pngBytes, iv));

        for (int i = 0; i < (int)rightBest.size(); ++i)
            encRight.push_back(enc.encryptWithIV(rightBest[i].pngBytes, iv));

        //  שליחה לשרת (iris::ServerClient מ-client/network) 
        {
            wstring* msg = new wstring(L"מתחבר לשרת...");
            PostMessageW(hwnd, WM_SCAN_PROGRESS, 86, (LPARAM)msg);
        }
        ServerClient client(p->cfg);
        {
            wstring* msg = new wstring(L"שולח שתי עיניים לשרת (ממוצע HD)...");
            PostMessageW(hwnd, WM_SCAN_PROGRESS, 92, (LPARAM)msg);
        }
        // שולחים שתי עיניים בפנייה אחת — השרת מחשב ממוצע HD ומשווה לסף
        AuthResponse dualRes = client.verify(
            p->passengerID, p->gateName, iv, encLeft, encRight);

        //  עיבוד תשובת השרת 
        if (dualRes.status == AuthStatus::COMM_ERROR) {
            res->commError = true;
            res->errorMsg  = toWide(dualRes.message);
        } else {
            // השרת מחזיר ממוצע HD ב-hammingDist; אנחנו מציגים אותו בשתי השורות
            res->leftHD  = dualRes.hammingDist;   // ממוצע — יוצג בשורת "HD שמאל"
            res->rightHD = dualRes.hammingDist;   // אותו ממוצע
            res->match = (dualRes.status == AuthStatus::AUTHORIZED);
            res->name = toWide(dualRes.matchedName);
            res->flightNumber = toWide(dualRes.flightNumber);
            res->seatNumber = toWide(dualRes.seatNumber);
            res->gate = toWide(p->gateName);
            res->serverMsg = toWide(dualRes.message);
        }

    } catch (const std::exception& e) {
        res->commError = true;
        res->errorMsg  = toWide(std::string(e.what()));
    }

    delete p;
    int code = res->commError ? -1 : (res->match ? 1 : 0);
    PostMessageW(hwnd, WM_SCAN_DONE, (WPARAM)code, (LPARAM)res);
    return 0;
}

//  פונקציות עזר ל-GDI
static HFONT makeFont(int ptSize, int weight, const wchar_t* face) {
    int dpi = GetDeviceCaps(GetDC(nullptr), LOGPIXELSY);
    return CreateFontW(
        -MulDiv(ptSize, dpi, 72), 0, 0, 0,
        weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, face);
}

static void createGDI() {
    g_brBg    = CreateSolidBrush(C_BG);
    g_brPanel = CreateSolidBrush(C_PANEL);
    g_brEdit  = CreateSolidBrush(C_EDIT_BG);
    g_fTitle  = makeFont(26, FW_BOLD,     L"Segoe UI");
    g_fLabel  = makeFont(10, FW_SEMIBOLD, L"Segoe UI");
    g_fInput  = makeFont(12, FW_NORMAL,   L"Segoe UI");
    g_fResult = makeFont(22, FW_BOLD,     L"Segoe UI");
    g_fDetail = makeFont(10, FW_NORMAL,   L"Segoe UI");
}

static void destroyGDI() {
    DeleteObject(g_brBg); DeleteObject(g_brPanel); DeleteObject(g_brEdit);
    DeleteObject(g_fTitle); DeleteObject(g_fLabel); DeleteObject(g_fInput);
    DeleteObject(g_fResult); DeleteObject(g_fDetail);
}


//  ציור 
static void fillRound(HDC dc, int x, int y, int w, int h, int r, COLORREF c) {
    HBRUSH br = CreateSolidBrush(c);
    HBRUSH ob = (HBRUSH)SelectObject(dc, br);
    BeginPath(dc);
    MoveToEx(dc, x+r, y, nullptr);
    LineTo(dc, x+w-r, y); AngleArc(dc, x+w-r, y+r, r, 270, 90);
    LineTo(dc, x+w, y+h-r); AngleArc(dc, x+w-r, y+h-r, r, 0, 90);
    LineTo(dc, x+r, y+h); AngleArc(dc, x+r, y+h-r, r, 90, 90);
    LineTo(dc, x, y+r); AngleArc(dc, x+r, y+r, r, 180, 90);
    EndPath(dc); FillPath(dc);
    SelectObject(dc, ob); DeleteObject(br);
}

static void frameRound(HDC dc, int x, int y, int w, int h, int r, COLORREF c, int t=1) {
    HPEN pen = CreatePen(PS_SOLID, t, c);
    HPEN op  = (HPEN)SelectObject(dc, pen);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    BeginPath(dc);
    MoveToEx(dc, x+r, y, nullptr);
    LineTo(dc, x+w-r, y); AngleArc(dc, x+w-r, y+r, r, 270, 90);
    LineTo(dc, x+w, y+h-r); AngleArc(dc, x+w-r, y+h-r, r, 0, 90);
    LineTo(dc, x+r, y+h); AngleArc(dc, x+r, y+h-r, r, 90, 90);
    LineTo(dc, x, y+r); AngleArc(dc, x+r, y+r, r, 180, 90);
    EndPath(dc); StrokePath(dc);
    SelectObject(dc, op); DeleteObject(pen);
}

static void drawTxt(HDC dc, const std::wstring& s, RECT rc,
                    HFONT f, COLORREF c, UINT flags = DT_LEFT|DT_VCENTER|DT_SINGLELINE) {
    HFONT of = (HFONT)SelectObject(dc, f);
    SetTextColor(dc, c); SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s.c_str(), -1, &rc, flags);
    SelectObject(dc, of);
}

static void drawHeader(HDC dc, int w) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    Gdiplus::LinearGradientBrush lgb(
        Gdiplus::Point(0, 0), Gdiplus::Point(0, HEADER_H),
        Gdiplus::Color(255, 10, 22, 48),
        Gdiplus::Color(255, 16, 32, 65));
    g.FillRectangle(&lgb, 0, 0, w, HEADER_H);

    Gdiplus::SolidBrush line(Gdiplus::Color(255, 30, 100, 210));
    g.FillRectangle(&line, 0, HEADER_H-2, w, 2);

    int lSz = 108, lX = 22, lY = (HEADER_H - lSz) / 2;
    if (g_logo) {
        g.DrawImage(g_logo, lX, lY, lSz, lSz);
    } else {
        Gdiplus::SolidBrush cb(Gdiplus::Color(180, 30, 100, 220));
        g.FillEllipse(&cb, (float)lX, (float)lY, (float)lSz, (float)lSz);
    }

    int tx = lX + lSz + 18, ty = lY + 8;
    {
        Gdiplus::FontFamily ff(L"Segoe UI");
        Gdiplus::Font fi(&ff, 28, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush bw(Gdiplus::Color(255, 230, 240, 255));
        g.DrawString(L"iris", -1, &fi, Gdiplus::PointF((float)tx, (float)ty), &bw);
        Gdiplus::RectF bb;
        g.MeasureString(L"iris", -1, &fi, Gdiplus::PointF(0,0), &bb);
        Gdiplus::Font fp(&ff, 28, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush bl(Gdiplus::Color(255, 50, 145, 255));
        g.DrawString(L"Pass", -1, &fp, Gdiplus::PointF((float)tx + bb.Width, (float)ty), &bl);
    }
    {
        Gdiplus::FontFamily ff(L"Segoe UI");
        Gdiplus::Font fs2(&ff, 10, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush bs(Gdiplus::Color(180, 140, 170, 215));
        g.DrawString(L"מעבר ביומטרי באמצעות קשתית עין בשדות תעופה",
                     -1, &fs2, Gdiplus::PointF((float)tx, (float)(ty+40)), &bs);
    }
}

static void drawProgressBar(HDC dc, int x, int y, int w, int h, int pct) {
    fillRound(dc, x, y, w, h, h/2, C_PROG_BG);
    if (pct > 0) {
        int fw = (h > (int)(w * pct / 100.0)) ? h : (int)(w * pct / 100.0);
        fillRound(dc, x, y, fw, h, h/2, C_ACCENT);
    }
    wchar_t buf[16]; swprintf_s(buf, L"%d%%", pct);
    RECT rp = { x+w+8, y-2, x+w+50, y+h+2 };
    drawTxt(dc, buf, rp, g_fLabel, C_TEXT2, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
}

static void drawResultCard(HDC dc, int x, int y, int w, int h) {
    bool isMatch  = (g_state == ScanState::Match);
    bool isErr    = (g_state == ScanState::Error);
    COLORREF bc   = isMatch ? C_GREEN : (isErr ? RGB(255,180,30) : C_RED);
    COLORREF bg   = isMatch ? RGB(12,42,20) : (isErr ? RGB(40,30,10) : RGB(42,12,12));

    fillRound(dc, x, y, w, h, 10, bg);
    frameRound(dc, x, y, w, h, 10, bc, 2);

    int pad = 22, cy = y + pad;

    std::wstring title;
    COLORREF tc;
    if (isMatch)          { title = L"\u2713  \u05d2\u05d9\u05e9\u05d4 \u05de\u05d0\u05d5\u05e9\u05e8\u05ea";  tc = C_GREEN; }
    else if (!isErr)      { title = L"\u2717  \u05d2\u05d9\u05e9\u05d4 \u05e0\u05d3\u05d7\u05d9\u05ea";  tc = C_RED; }
    else                  { title = L"\u26a0  \u05e9\u05d2\u05d9\u05d0\u05ea \u05ea\u05e7\u05e9\u05d5\u05e8\u05ea"; tc = RGB(255,180,30); }

    RECT rT = { x+pad, cy, x+w-pad, cy+36 };
    drawTxt(dc, title, rT, g_fResult, tc, DT_LEFT|DT_SINGLELINE);
    cy += 44;

    HPEN div = CreatePen(PS_SOLID, 1, bc);
    HPEN op  = (HPEN)SelectObject(dc, div);
    MoveToEx(dc, x+pad, cy, nullptr); LineTo(dc, x+w-pad, cy);
    SelectObject(dc, op); DeleteObject(div);
    cy += 10;

    if (isErr) {
        RECT re = { x+pad, cy, x+w-pad, cy+80 };
        drawTxt(dc, g_result.errorMsg, re, g_fDetail, RGB(255,170,170), DT_LEFT|DT_WORDBREAK);
        return;
    }

    auto row = [&](const std::wstring& lbl, const std::wstring& val, COLORREF vc) {
        RECT rl = { x+pad,      cy, x+pad+120, cy+22 };
        RECT rv = { x+pad+125,  cy, x+w-pad,   cy+22 };
        drawTxt(dc, lbl, rl, g_fLabel,  C_TEXT3, DT_LEFT|DT_SINGLELINE);
        drawTxt(dc, val, rv, g_fDetail, vc,      DT_LEFT|DT_SINGLELINE);
        cy += 24;
    };

    if (!g_result.name.empty())         row(L"שם מזוהה:", g_result.name, C_TEXT1);
    if (!g_result.flightNumber.empty()) row(L"טיסה:", g_result.flightNumber, C_TEXT1);
    if (!g_result.seatNumber.empty())   row(L"מושב:", g_result.seatNumber, C_TEXT1);
    if (!g_result.gate.empty())         row(L"שער:", g_result.gate, C_TEXT1);
    cy += 6;

    COLORREF hdc = isMatch ? C_GREEN : C_RED;
    wchar_t buf[64];
    swprintf_s(buf, L"%.4f", g_result.leftHD);  // leftHD מכיל את ממוצע HD הסופי
    row(L"ממוצע HD:", buf, hdc);

    if (!g_result.serverMsg.empty()) {
        RECT rm = { x+pad, cy, x+w-pad, cy+40 };
        drawTxt(dc, g_result.serverMsg, rm, g_fDetail, C_TEXT2, DT_LEFT|DT_WORDBREAK);
    }
}

//פונקציית חלון — טיפול ביצירה, ציור, לחיצות כפתור, ועדכוני סריקה
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
   // שימוש ב-WM_APP+ כדי לקבל עדכוני התקדמות ותוצאה מ-thread הסריקה
    switch (msg) {
    // יצירת חלון — אתחול GDI, יצירת שדות קלט וכפתור
    case WM_CREATE: {
        createGDI();
        // טעינת לוגו — מחפש בתיקיית ה-EXE וגם בתיקיית Release של השרת (backup)
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        fs::path exeDir = fs::path(exePath).parent_path();
        for (const auto& candidate : {
            exeDir / L"logo.png",
            exeDir / L".." / L".." / L".." / L"server" / L"build" / L"Release" / L"logo.png",
            exeDir / L".." / L"logo.png"
        }) {
            if (fs::exists(candidate)) {
                g_logo = Gdiplus::Bitmap::FromFile(fs::canonical(candidate).c_str());
                break;
            }
        }

        int cx = FORM_PAD, cw = WIN_W - FORM_PAD * 2;
        int fy = HEADER_H + 14;
        HINSTANCE hi = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);

        auto makeLabel = [&](const wchar_t* t, int y) {
            HWND h = CreateWindowExW(0, L"STATIC", t,
                WS_CHILD|WS_VISIBLE|SS_LEFT, cx, y, cw, 20, hwnd, nullptr, hi, nullptr);
            SendMessageW(h, WM_SETFONT, (WPARAM)g_fLabel, TRUE);
        };
        auto makeEdit = [&](int id, const wchar_t* ph, int y) -> HWND {
            HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", ph,
                WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,
                cx, y, cw, FIELD_H, hwnd, (HMENU)(UINT_PTR)id, hi, nullptr);
            SendMessageW(h, WM_SETFONT, (WPARAM)g_fInput, TRUE);
            SendMessageW(h, EM_SETCUEBANNER, TRUE, (LPARAM)ph);
            return h;
        };

        makeLabel(L"תעודת זהות ", fy);
        g_editPassID = makeEdit(IDC_EDIT_PASSID, L"לדוגמה: 123456789", fy+22);

        makeLabel(L"מספר שער (Gate)", fy+74);
        g_editGate   = makeEdit(IDC_EDIT_GATE,   L"לדוגמה: A1 ", fy+96);

        makeLabel(L"מספר אדם מ-CASIA (000–999)", fy+148);
        g_editCasia  = makeEdit(IDC_EDIT_CASIA,  L"לדוגמה: 042", fy+170);

        int bw = 200, bh = 42;
        g_btnScan = CreateWindowExW(0, L"BUTTON", L"התחל סריקה",
            WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
            (WIN_W-bw)/2, fy+222, bw, bh, hwnd, (HMENU)(UINT_PTR)IDC_BTN_SCAN, hi, nullptr);
        return 0;
    }
    //מצב סריקה משתנה כל הזמן — עדכון התקדמות, הודעות, ותוצאה סופית
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hScreen = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);

        HDC hMem   = CreateCompatibleDC(hScreen);
        HBITMAP bm = CreateCompatibleBitmap(hScreen, rc.right, rc.bottom);
        HBITMAP ob = (HBITMAP)SelectObject(hMem, bm);

        FillRect(hMem, &rc, g_brBg);
        drawHeader(hMem, rc.right);

        RECT rPanel = { 0, HEADER_H, rc.right, HEADER_H+280 };
        FillRect(hMem, &rPanel, g_brPanel);

        HPEN div = CreatePen(PS_SOLID, 1, C_DIVIDER);
        HPEN op  = (HPEN)SelectObject(hMem, div);
        MoveToEx(hMem, 0, HEADER_H+280, nullptr); LineTo(hMem, rc.right, HEADER_H+280);
        SelectObject(hMem, op); DeleteObject(div);

        int sy = HEADER_H + 288;
        if (g_state == ScanState::Scanning) {
            std::wstring dots(g_dotCount % 4, L'.');
            RECT rs = { FORM_PAD, sy, rc.right-FORM_PAD, sy+22 };
            drawTxt(hMem, g_statusMsg + dots, rs, g_fLabel, C_TEXT2, DT_LEFT|DT_SINGLELINE);
            drawProgressBar(hMem, FORM_PAD, sy+26, rc.right-FORM_PAD*2-50, 14, g_progress);
        } else if (g_state == ScanState::Idle) {
            RECT rs = { FORM_PAD, sy, rc.right-FORM_PAD, sy+22 };
            drawTxt(hMem, g_statusMsg, rs, g_fLabel, C_TEXT3, DT_LEFT|DT_SINGLELINE);
        }

        bool showCard = (g_state == ScanState::Match ||
                         g_state == ScanState::NoMatch ||
                         g_state == ScanState::Error);
        if (showCard) {
            int cardY = HEADER_H + 286;
            int cardH = rc.bottom - cardY - 12;
            if (cardH > 80)
                drawResultCard(hMem, FORM_PAD, cardY, rc.right-FORM_PAD*2, cardH);
        }

        BitBlt(hScreen, 0, 0, rc.right, rc.bottom, hMem, 0, 0, SRCCOPY);
        SelectObject(hMem, ob); DeleteObject(bm); DeleteDC(hMem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    // כדי למנוע מהחלון להבהב בזמן ציור מחדש
    case WM_ERASEBKGND: return 1;
    // צבעים מותאמים אישית לשדות סטטיים ועריכה
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, C_TEXT2); SetBkColor(hdc, C_PANEL);
        return (LRESULT)g_brPanel;
    }
    // צבע רקע מותאם אישית לשדות עריכה
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, C_TEXT1); SetBkColor(hdc, C_EDIT_BG);
        return (LRESULT)g_brEdit;
    }
    // כפתור סריקה מותאם אישית — צבעים, טקסט, אייקון, ותגובה ללחיצה והחזקת העכבר
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lp;
        if (dis->CtlID != IDC_BTN_SCAN) break;
        bool pressed = (dis->itemState & ODS_SELECTED) != 0;
        bool enabled = (g_state != ScanState::Scanning);
        COLORREF bgC = enabled
            ? (pressed ? RGB(44,140,255) : (g_btnHover ? RGB(34,122,230) : C_ACCENT))
            : RGB(30,45,70);

        Gdiplus::Graphics gg(dis->hDC);
        gg.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        int bw = dis->rcItem.right - dis->rcItem.left;
        int bh = dis->rcItem.bottom - dis->rcItem.top;
        BYTE r2 = GetRValue(bgC), g2 = GetGValue(bgC), b2 = GetBValue(bgC);
        BYTE rT = (r2+20>255) ? 255 : (BYTE)(r2+20);
        BYTE gT = (g2+10>255) ? 255 : (BYTE)(g2+10);
        Gdiplus::Color top(255, rT, gT, b2), bot(255, r2, g2, b2);
        Gdiplus::LinearGradientBrush lbr(Gdiplus::Point(0,0), Gdiplus::Point(0,bh), top, bot);
        Gdiplus::GraphicsPath gp;
        gp.AddArc(0,0,20,20,180,90); gp.AddArc(bw-20,0,20,20,270,90);
        gp.AddArc(bw-20,bh-20,20,20,0,90); gp.AddArc(0,bh-20,20,20,90,90);
        gp.CloseFigure(); gg.FillPath(&lbr, &gp);

        std::wstring btnTxt = enabled ? L"\u25b6  \u05d4\u05ea\u05d7\u05dc \u05e1\u05e8\u05d9\u05e7\u05d4" : L"\u23f3  \u05e1\u05d5\u05e8\u05e7...";
        RECT rBt = dis->rcItem;
        HFONT of = (HFONT)SelectObject(dis->hDC, g_fLabel);
        SetTextColor(dis->hDC, enabled ? RGB(255,255,255) : RGB(120,150,190));
        SetBkMode(dis->hDC, TRANSPARENT);
        DrawTextW(dis->hDC, btnTxt.c_str(), -1, &rBt, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(dis->hDC, of);
        return TRUE;
    }

    // מעקב אחרי תנועת העכבר — שינוי צבע כפתור סריקה בעת ריחוף
    case WM_MOUSEMOVE: {
        POINT pt = { LOWORD(lp), HIWORD(lp) };
        if (g_btnScan) {
            RECT br; GetWindowRect(g_btnScan, &br);
            MapWindowPoints(HWND_DESKTOP, hwnd, (POINT*)&br, 2);
            bool over = PtInRect(&br, pt) != 0;
            if (over != g_btnHover) { g_btnHover = over; InvalidateRect(g_btnScan, nullptr, FALSE); }
        }
        return 0;
    }
    
    //מצב סריקה משתנה — לחיצה על כפתור סריקה, עדכון התקדמות, הודעות, ותוצאה סופית
    case WM_COMMAND: {
        if (LOWORD(wp) != IDC_BTN_SCAN || HIWORD(wp) != BN_CLICKED) return 0;
        if (g_state == ScanState::Scanning) return 0;

        std::wstring passW  = getEdit(g_editPassID);
        std::wstring gateW  = getEdit(g_editGate);
        std::wstring casiaW = getEdit(g_editCasia);

        if (passW.size() < 5) {
            MessageBoxW(hwnd, L"מספר זהות קצר מדי (מינימום 5 תווים)", L"שגיאת קלט", MB_ICONWARNING);
            SetFocus(g_editPassID); return 0;
        }
        {
            const size_t first = gateW.find_first_not_of(L" \t\r\n");
            if (first == std::wstring::npos) {
                MessageBoxW(hwnd, L"יש להזין שער (למשל A1)", L"שגיאת קלט", MB_ICONWARNING);
                SetFocus(g_editGate); return 0;
            }
            const size_t last = gateW.find_last_not_of(L" \t\r\n");
            gateW = gateW.substr(first, last - first + 1);
            if (gateW.size() > 9) {
                MessageBoxW(hwnd, L"שם שער ארוך מדי (מקסימום 9 תווים)", L"שגיאת קלט", MB_ICONWARNING);
                SetFocus(g_editGate); return 0;
            }
        }
        if (casiaW.size() != 3 || !std::all_of(casiaW.begin(), casiaW.end(), ::isdigit)) {
            MessageBoxW(hwnd, L"מספר CASIA חייב להיות בדיוק 3 ספרות (000–999)", L"שגיאת קלט", MB_ICONWARNING);
            SetFocus(g_editCasia); return 0;
        }
        // אימות IRIS_AES_KEY
        if (!std::getenv("IRIS_AES_KEY") || !*std::getenv("IRIS_AES_KEY")) {
            MessageBoxW(hwnd,
                L"משתנה IRIS_AES_KEY לא מוגדר.\n\n"
                L"הגדר בטרמינל לפני הרצה:\n"
                L"$env:IRIS_AES_KEY = \"000102...1e1f\"",
                L"שגיאת הגדרות", MB_ICONERROR);
            return 0;
        }

        // בניית config
        ClientConfig cfg;
        const char* h = std::getenv("IRIS_SERVER_HOST");
        if (h && *h) cfg.serverHost = h;
        const char* po = std::getenv("IRIS_SERVER_PORT");
        if (po && *po) cfg.serverPort = std::stoi(po);

        g_state     = ScanState::Scanning;
        g_progress  = 0;
        g_dotCount  = 0;
        g_statusMsg = L"בוחר תמונות";
        EnableWindow(g_btnScan, FALSE);
        SetTimer(hwnd, IDC_TIMER_DOTS, 400, nullptr);
        InvalidateRect(hwnd, nullptr, FALSE);

        ScanParams* params = new ScanParams{
            hwnd,
            toUtf8(passW),
            toUtf8(gateW),
            toUtf8(casiaW),
            cfg
        };
        // התחלת thread לסריקה כדי לא לחסום את ממשק המשתמש
        HANDLE ht = CreateThread(nullptr, 0, ScanWorker, params, 0, nullptr);
        if (ht) CloseHandle(ht);
        else {
            delete params;
            g_state = ScanState::Idle;
            EnableWindow(g_btnScan, TRUE);
            KillTimer(hwnd, IDC_TIMER_DOTS);
        }
        return 0;
    }
    // עדכון התקדמות הסריקה וההודעות מ-thread הסריקה
    case WM_SCAN_PROGRESS: {
        g_progress = (int)wp;
        std::wstring* msg = (std::wstring*)lp;
        if (msg) { g_statusMsg = *msg; delete msg; }
        RECT r2 = { 0, HEADER_H+280, WIN_W, WIN_H };
        InvalidateRect(hwnd, &r2, FALSE);
        return 0;
    }
    // תוצאת הסריקה מ-thread הסריקה — עדכון התוצאה, מצב סריקה, והצגת כרטיס התוצאה
    case WM_SCAN_DONE: {
        KillTimer(hwnd, IDC_TIMER_DOTS);
        int code = (int)wp;
        ScanResult* r = (ScanResult*)lp;
        if (r) { g_result = *r; delete r; }
        g_state    = (code == 1) ? ScanState::Match
                   : (code == 0) ? ScanState::NoMatch
                                 : ScanState::Error;
        g_progress = 100;
        EnableWindow(g_btnScan, TRUE);
        SetWindowTextW(hwnd, L"IrisPass — סימולציית שער נמל תעופה");
        InvalidateRect(hwnd, nullptr, FALSE);
        SetForegroundWindow(hwnd);
        return 0;
    }
    // עדכון נקודות בסריקה כדי להראות שהסריקה עדיין פעילה, ושינוי הטקסט בכותרת כדי להוסיף נקודות נעות
    case WM_TIMER:
        if (wp == IDC_TIMER_DOTS) {
            ++g_dotCount;
            wchar_t t[64];
            swprintf_s(t, L"IrisPass — סורק%ls", std::wstring(g_dotCount%4, L'.').c_str());
            SetWindowTextW(hwnd, t);
            RECT r2 = { 0, HEADER_H+280, WIN_W, WIN_H };
            InvalidateRect(hwnd, &r2, FALSE);
        }
        return 0;
    // ניקוי משאבים בעת סגירת החלון
    case WM_DESTROY:
        destroyGDI();
        delete g_logo;
        Gdiplus::GdiplusShutdown(g_gdipT);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

//  נקודת כניסה — אתחול GDI+, יצירת חלון, והפעלת לולאת הודעות
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    SetProcessDPIAware();

    Gdiplus::GdiplusStartupInput gsi;
    Gdiplus::GdiplusStartup(&g_gdipT, &gsi, nullptr);

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wcx{ sizeof(wcx) };
    wcx.style         = CS_HREDRAW|CS_VREDRAW;
    wcx.lpfnWndProc   = WndProc;
    wcx.hInstance     = hInst;
    wcx.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wcx.hbrBackground = nullptr;
    wcx.lpszClassName = L"IrisPassGateGUI";
    wcx.hIcon         = LoadIconW(nullptr, IDI_APPLICATION);
    wcx.hIconSm       = wcx.hIcon;
    RegisterClassExW(&wcx);

    RECT adj = { 0, 0, WIN_W, WIN_H };
    AdjustWindowRect(&adj, WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX, FALSE);
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int rw = adj.right-adj.left, rh = adj.bottom-adj.top;

    g_hwnd = CreateWindowExW(
        0, L"IrisPassGateGUI",
        L"IrisPass — סימולציית שער נמל תעופה",
        WS_OVERLAPPEDWINDOW,   // WS_OVERLAPPEDWINDOW כולל WS_THICKFRAME ו-WS_MAXIMIZEBOX
        (sw-rw)/2, (sh-rh)/2, rw, rh,
        nullptr, nullptr, hInst, nullptr);

    if (!g_hwnd) return -1;
    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0)) {
        if (!IsDialogMessageW(g_hwnd, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return (int)message.wParam;
}
