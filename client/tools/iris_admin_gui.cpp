
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
#include <shlobj.h>       
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>

#include "config/ClientConfig.h"
#include "security/Encryptor.h"
#include "network/ServerClient.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")

namespace fs = std::filesystem;
using namespace iris;

// ─── צבעים ───────────────────────────────────────────────────────────────────
static const COLORREF C_BG      = RGB( 10,  20,  42);
static const COLORREF C_PANEL   = RGB( 16,  30,  60);
static const COLORREF C_EDIT_BG = RGB( 13,  26,  53);
static const COLORREF C_ACCENT  = RGB( 24, 103, 214);
static const COLORREF C_TAB_ACT = RGB( 20,  80, 180);
static const COLORREF C_TAB_IN  = RGB( 14,  26,  54);
static const COLORREF C_TEXT1   = RGB(230, 240, 255);
static const COLORREF C_TEXT2   = RGB(140, 168, 210);
static const COLORREF C_TEXT3   = RGB( 80, 110, 158);
static const COLORREF C_GREEN   = RGB( 44, 200,  82);
static const COLORREF C_RED     = RGB(225,  52,  48);
static const COLORREF C_YELLOW  = RGB(255, 200,  40);
static const COLORREF C_DIVIDER = RGB( 28,  54, 100);
static const COLORREF C_ROW_ALT = RGB( 14,  28,  58);
static const COLORREF C_ROW_NOR = RGB( 10,  22,  46);

// ─── ממדים ───────────────────────────────────────────────────────────────────
static constexpr int WIN_W    = 900;
static constexpr int WIN_H    = 680;
static constexpr int HDR_H    = 72;
static constexpr int TAB_H    = 40;
static constexpr int PAD      = 20;
static constexpr int FIELD_H  = 32;

// ─── מזהי פקדים ──────────────────────────────────────────────────────────────
#define IDC_TAB_ENROLL    10
#define IDC_TAB_LOGS      11
#define IDC_EDIT_CASIA    20
#define IDC_EDIT_NAME     21
#define IDC_EDIT_NAT      22
#define IDC_EDIT_ID       23
#define IDC_BTN_BROWSE    24
#define IDC_BTN_ENROLL    25
#define IDC_LBL_PREVIEW   26
#define IDC_BTN_REFRESH   30
#define IDC_LIST_ACCESS   31
#define IDC_LIST_CHANGES  32
#define IDC_LBL_ACCESS    33
#define IDC_LBL_CHANGES   34

// ─── הודעות thread → UI ──────────────────────────────────────────────────────
#define WM_ENROLL_DONE    (WM_APP + 10)
#define WM_ENROLL_PROG    (WM_APP + 11)

// ─── פרמטרי enumerate תמונות ──────────────────────────────────────────────────
static constexpr int ENROLL_SKIP  = 0;   // רישום: לא מדלגים — משתמשים ב-3 הראשונות
static constexpr int ENROLL_BEST  = 3;   // 3 הכי חדות מתוך הראשונות

// ─── מצב גלובלי ─────────────────────────────────────────────────────────────
enum class Tab { Enroll, Logs };
enum class EnrollState { Idle, Working, Done, Error };

static HWND         g_hwnd       = nullptr;
static Tab          g_tab        = Tab::Enroll;
static EnrollState  g_eState     = EnrollState::Idle;
static std::wstring g_statusMsg  = L"";
static std::wstring g_casiaDir;   // נתיב תיקיית CASIA שנבחרה

// פאנל Enroll
static HWND g_editCasia  = nullptr;
static HWND g_editID     = nullptr;  // תעודת זהות ישראלית
static HWND g_editName   = nullptr;
static HWND g_editNat    = nullptr;
static HWND g_btnBrowse  = nullptr;
static HWND g_btnEnroll  = nullptr;
static HWND g_lblPreview = nullptr;  // תצוגת 3 תמונות נבחרות

// פאנל Logs
static HWND g_listAccess   = nullptr;
static HWND g_listChanges  = nullptr;
static HWND g_btnRefresh   = nullptr;

// GDI
static HBRUSH g_brBg    = nullptr;
static HBRUSH g_brPanel = nullptr;
static HBRUSH g_brEdit  = nullptr;
static HFONT  g_fTitle  = nullptr;
static HFONT  g_fTab    = nullptr;
static HFONT  g_fLabel  = nullptr;
static HFONT  g_fInput  = nullptr;
static HFONT  g_fMono   = nullptr;

// GDI+
static ULONG_PTR g_gdipT = 0;

// ─── עזרה: המרות ──────────────────────────────────────────────────────────────
static std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n-1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
static std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n-1, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}
static std::wstring getEdit(HWND h) {
    wchar_t buf[512]={};
    GetWindowTextW(h, buf, 512);
    return buf;
}

// ─── GDI helpers ──────────────────────────────────────────────────────────────
static HFONT makeFont(int pt, int w, const wchar_t* f) {
    int dpi = GetDeviceCaps(GetDC(nullptr), LOGPIXELSY);
    return CreateFontW(-MulDiv(pt,dpi,72),0,0,0,w,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_SWISS,f);
}
static void createGDI() {
    g_brBg    = CreateSolidBrush(C_BG);
    g_brPanel = CreateSolidBrush(C_PANEL);
    g_brEdit  = CreateSolidBrush(C_EDIT_BG);
    g_fTitle  = makeFont(18, FW_BOLD,     L"Segoe UI");
    g_fTab    = makeFont(11, FW_SEMIBOLD, L"Segoe UI");
    g_fLabel  = makeFont(10, FW_SEMIBOLD, L"Segoe UI");
    g_fInput  = makeFont(11, FW_NORMAL,   L"Segoe UI");
    g_fMono   = makeFont( 9, FW_NORMAL,   L"Consolas");
}
static void destroyGDI() {
    DeleteObject(g_brBg); DeleteObject(g_brPanel); DeleteObject(g_brEdit);
    DeleteObject(g_fTitle); DeleteObject(g_fTab); DeleteObject(g_fLabel);
    DeleteObject(g_fInput); DeleteObject(g_fMono);
}

static void drawTxt(HDC dc, const std::wstring& s, RECT rc,
                    HFONT f, COLORREF c, UINT fl=DT_LEFT|DT_VCENTER|DT_SINGLELINE) {
    HFONT of=(HFONT)SelectObject(dc,f);
    SetTextColor(dc,c); SetBkMode(dc,TRANSPARENT);
    DrawTextW(dc,s.c_str(),-1,&rc,fl);
    SelectObject(dc,of);
}
static void fillRound(HDC dc, int x,int y,int w,int h,int r,COLORREF c) {
    HBRUSH br=CreateSolidBrush(c); HBRUSH ob=(HBRUSH)SelectObject(dc,br);
    BeginPath(dc);
    MoveToEx(dc,x+r,y,nullptr); LineTo(dc,x+w-r,y); AngleArc(dc,x+w-r,y+r,r,270,90);
    LineTo(dc,x+w,y+h-r); AngleArc(dc,x+w-r,y+h-r,r,0,90);
    LineTo(dc,x+r,y+h); AngleArc(dc,x+r,y+h-r,r,90,90);
    LineTo(dc,x,y+r); AngleArc(dc,x+r,y+r,r,180,90);
    EndPath(dc); FillPath(dc);
    SelectObject(dc,ob); DeleteObject(br);
}

// ─── ציור כותרת ──────────────────────────────────────────────────────────────
static void drawHeader(HDC dc) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::LinearGradientBrush lgb(
        Gdiplus::Point(0,0), Gdiplus::Point(0,HDR_H),
        Gdiplus::Color(255,10,22,50), Gdiplus::Color(255,14,28,58));
    g.FillRectangle(&lgb, 0, 0, WIN_W, HDR_H);

    // קו תחתי
    Gdiplus::SolidBrush lineB(Gdiplus::Color(255,30,100,210));
    g.FillRectangle(&lineB, 0, HDR_H-2, WIN_W, 2);

    // כותרת
    Gdiplus::FontFamily ff(L"Segoe UI");
    Gdiplus::Font ftB(&ff,22,Gdiplus::FontStyleBold,Gdiplus::UnitPixel);
    Gdiplus::Font ftR(&ff,22,Gdiplus::FontStyleBold,Gdiplus::UnitPixel);
    Gdiplus::SolidBrush bW(Gdiplus::Color(255,230,240,255));
    Gdiplus::SolidBrush bA(Gdiplus::Color(255,50,145,255));
    g.DrawString(L"iris",   -1, &ftB, Gdiplus::PointF(18,(float)(HDR_H-36)/2), &bW);
    Gdiplus::RectF bb; g.MeasureString(L"iris",-1,&ftB,Gdiplus::PointF(0,0),&bb);
    g.DrawString(L"Pass",   -1, &ftR, Gdiplus::PointF(18+bb.Width,(float)(HDR_H-36)/2), &bA);

    Gdiplus::Font fs2(&ff,10,Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
    Gdiplus::SolidBrush bs(Gdiplus::Color(180,140,170,215));
    g.DrawString(L"Admin Panel — ניהול משתמשים ולוגים",
        -1, &fs2, Gdiplus::PointF(18,(float)(HDR_H-36)/2+32), &bs);

    // תג "ADMIN"
    fillRound(dc, WIN_W-100, (HDR_H-28)/2, 84, 28, 6, RGB(40,10,80));
    RECT rA={WIN_W-100,(HDR_H-28)/2,WIN_W-16,(HDR_H+28)/2};
    drawTxt(dc, L"⚙ ADMIN", rA, g_fTab, RGB(200,130,255), DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}

// ─── ציור טאבים ──────────────────────────────────────────────────────────────
static void drawTabs(HDC dc) {
    int y = HDR_H, tw = WIN_W/2;
    struct { const wchar_t* lbl; Tab t; } tabs[] = {
        {L"📋  רישום משתמש", Tab::Enroll},
        {L"📊  לוגי מערכת",  Tab::Logs}
    };
    for (int i = 0; i < 2; ++i) {
        bool act = (g_tab == tabs[i].t);
        COLORREF bg = act ? C_TAB_ACT : C_TAB_IN;
        RECT r = {i*tw, y, (i+1)*tw, y+TAB_H};
        HBRUSH br = CreateSolidBrush(bg);
        FillRect(dc, &r, br); DeleteObject(br);
        drawTxt(dc, tabs[i].lbl, r, g_fTab,
            act ? C_TEXT1 : C_TEXT3, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if (act) {
            HPEN pen = CreatePen(PS_SOLID, 3, C_ACCENT);
            HPEN op = (HPEN)SelectObject(dc, pen);
            MoveToEx(dc, i*tw, y+TAB_H-1, nullptr);
            LineTo(dc, (i+1)*tw, y+TAB_H-1);
            SelectObject(dc, op); DeleteObject(pen);
        }
    }
}

// ─── Sharpness ──────────────────────────────────────────────────────────────
static double computeSharpness(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)), {});
    cv::Mat gray = cv::imdecode(raw, cv::IMREAD_GRAYSCALE);
    if (gray.empty()) return 0.0;
    cv::Mat lap; cv::Laplacian(gray, lap, CV_64F);
    cv::Scalar mu, sigma; cv::meanStdDev(lap, mu, sigma);
    return sigma[0]*sigma[0];
}
static std::vector<uint8_t> loadAsPng(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)), {});
    cv::Mat gray = cv::imdecode(raw, cv::IMREAD_GRAYSCALE);
    if (gray.empty()) return {};
    std::vector<uint8_t> png; cv::imencode(".png", gray, png);
    return png;
}

struct ScoredImg { fs::path path; double sharp=0; std::vector<uint8_t> png; };

// בחירת 3 הכי חדות — מדלגת על 3 ראשונות (enrollment)
static std::vector<ScoredImg> pickBest(const fs::path& dir) {
    std::vector<fs::path> files;
    for (auto& e : fs::directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(),ext.end(),ext.begin(),::tolower);
        if (ext==".jpg"||ext==".jpeg"||ext==".png"||ext==".bmp")
            files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    // דילוג על תמונות הרישום
    if ((int)files.size() > ENROLL_SKIP)
        files.erase(files.begin(), files.begin()+ENROLL_SKIP);

    std::vector<ScoredImg> scored;
    for (auto& f : files) {
        auto png = loadAsPng(f);
        if (!png.empty()) scored.push_back({f, computeSharpness(f), std::move(png)});
    }
    std::sort(scored.begin(), scored.end(),
        [](const ScoredImg& a, const ScoredImg& b){ return a.sharp > b.sharp; });
    if ((int)scored.size() > ENROLL_BEST) scored.resize(ENROLL_BEST);
    return scored;
}

// ─── מציאת CASIA root ────────────────────────────────────────────────────────
static fs::path findCasiaRoot() {
    const char* env = std::getenv("IRIS_CASIA_ROOT");
    if (env && *env && fs::exists(env)) return fs::canonical(env);
    wchar_t exePath[MAX_PATH]={};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    fs::path exeDir = fs::path(exePath).parent_path();
    for (const auto& c : {
        exeDir/"../../../../test_images/CASIA-Iris-Thousand/CASIA-Iris-Thousand",
        exeDir/"../../../test_images/CASIA-Iris-Thousand/CASIA-Iris-Thousand",
        exeDir/"../../test_images/CASIA-Iris-Thousand/CASIA-Iris-Thousand",
    }) if (fs::exists(c)) return fs::canonical(c);
    return {};
}

// ─── מציאת תיקיית לוגים ──────────────────────────────────────────────────────
static fs::path findLogsDir() {
    const char* env = std::getenv("IRIS_LOGS_DIR");
    if (env && *env && fs::exists(env)) return env;
    wchar_t exePath[MAX_PATH]={};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    fs::path exeDir = fs::path(exePath).parent_path();
    // השרת יוצר logs/ בתיקייה שלו (Release/logs/)
    // הלקוח רץ גם כן מ-Release, לכן logs/ נמצא לידנו
    for (const auto& c : {
        exeDir/"logs",
        exeDir/"../../../server/build/Release/logs",  // client/build/Release → project root → server
        exeDir/"../../server/build/Release/logs",
        exeDir/"../server/build/Release/logs",
    }) if (fs::exists(c)) return fs::canonical(c);
    return {};
}

// ─── טעינת CSV ל-ListView ────────────────────────────────────────────────────
static void loadCsvToList(HWND list, const fs::path& csvPath) {
    ListView_DeleteAllItems(list);

    // פתיחה כ-narrow (הקובץ נכתב כ-UTF-8/ASCII)
    std::ifstream f(csvPath);
    if (!f.is_open()) {
        LVITEMW item={};
        item.mask = LVIF_TEXT;
        item.iItem = 0; item.iSubItem = 0;
        std::wstring msg = L"קובץ לוג לא נמצא: " + csvPath.wstring();
        item.pszText = const_cast<LPWSTR>(msg.c_str());
        ListView_InsertItem(list, &item);
        return;
    }

    std::string line;
    bool first = true;
    int row = 0;
    while (std::getline(f, line)) {
        if (first) { first=false; continue; } // דלג כותרת
        // הסרת \r (Windows line endings)
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        // פיצול ל-CSV fields
        std::vector<std::wstring> cols;
        std::stringstream ss(line);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            cols.push_back(std::wstring(tok.begin(), tok.end()));
        }

        LVITEMW item={};
        item.mask = LVIF_TEXT;
        item.iItem = row;
        item.iSubItem = 0;
        if (!cols.empty()) {
            item.pszText = const_cast<LPWSTR>(cols[0].c_str());
            ListView_InsertItem(list, &item);
        }
        for (int c = 1; c < (int)cols.size(); ++c)
            ListView_SetItemText(list, row, c, const_cast<LPWSTR>(cols[c].c_str()));
        ++row;
    }
}

// ─── setup ListView ───────────────────────────────────────────────────────────
static void setupAccessList(HWND list) {
    ListView_SetExtendedListViewStyle(list,
        LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);

    // עמודות: Timestamp, PassengerID, Gate, Eye, HammingDist, Success, Notes
    const struct { const wchar_t* hdr; int w; } cols[] = {
        {L"Timestamp",    160}, {L"Passenger",  90},
        {L"Gate",          50}, {L"Eye",         40},
        {L"HD",            60}, {L"OK",          40},
        {L"Notes",        220}
    };
    for (int i=0; i<7; ++i) {
        LVCOLUMNW lvc={};
        lvc.mask = LVCF_TEXT|LVCF_WIDTH;
        lvc.cx = cols[i].w;
        lvc.pszText = const_cast<LPWSTR>(cols[i].hdr);
        ListView_InsertColumn(list, i, &lvc);
    }
}
static void setupChangesList(HWND list) {
    ListView_SetExtendedListViewStyle(list,
        LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);

    const struct { const wchar_t* hdr; int w; } cols[] = {
        {L"Timestamp", 160}, {L"Action", 80},
        {L"Passenger",  90}, {L"Details", 360}
    };
    for (int i=0; i<4; ++i) {
        LVCOLUMNW lvc={};
        lvc.mask = LVCF_TEXT|LVCF_WIDTH;
        lvc.cx = cols[i].w;
        lvc.pszText = const_cast<LPWSTR>(cols[i].hdr);
        ListView_InsertColumn(list, i, &lvc);
    }
}

// ─── עדכון תצוגת תמונות נבחרות ──────────────────────────────────────────────
static void updatePreview(const fs::path& casiaDir) {
    if (!g_lblPreview) return;
    if (casiaDir.empty() || !fs::exists(casiaDir)) {
        SetWindowTextW(g_lblPreview, L"");
        return;
    }
    fs::path leftDir  = casiaDir / "L";
    fs::path rightDir = casiaDir / "R";
    if (!fs::exists(leftDir) || !fs::exists(rightDir)) {
        SetWindowTextW(g_lblPreview, L"שגיאה: חסרות תיקיות L/R");
        return;
    }
    auto lBest = pickBest(leftDir);
    auto rBest = pickBest(rightDir);

    std::wstring txt = L"תמונות שנבחרו (לפי חדות Laplacian):\r\n";
    txt += L"עין שמאל (L):   ";
    for (auto& s : lBest) txt += s.path.filename().wstring() + L"  ";
    txt += L"\r\nעין ימין (R):   ";
    for (auto& s : rBest) txt += s.path.filename().wstring() + L"  ";
    SetWindowTextW(g_lblPreview, txt.c_str());
}

// ─── thread רישום ────────────────────────────────────────────────────────────
struct EnrollParams {
    HWND hwnd;
    fs::path casiaDir;
    std::string passID, fullName, nationality;
    ClientConfig cfg;
};

static DWORD WINAPI EnrollWorker(LPVOID p) {
    EnrollParams* ep = (EnrollParams*)p;
    bool ok = false;
    std::wstring msg;

    try {
        // בחירת תמונות
        auto lBest = pickBest(ep->casiaDir / "L");
        auto rBest = pickBest(ep->casiaDir / "R");
        if (lBest.size()<3 || rBest.size()<3)
            throw std::runtime_error("נדרשות לפחות 6 תמונות (3 לכל עין) לאחר דילוג");

        // הצפנה
        Encryptor enc;
        uint8_t iv[16]={};
        std::vector<std::vector<uint8_t>> encL, encR;
        encL.push_back(enc.encrypt(lBest[0].png, iv));
        for (int i=1;i<3;++i) encL.push_back(enc.encryptWithIV(lBest[i].png, iv));
        for (int i=0;i<3;++i) encR.push_back(enc.encryptWithIV(rBest[i].png, iv));

        // שליחה לשרת
        ServerClient sc(ep->cfg);
        EnrollResult res = sc.enroll(ep->passID, ep->fullName, ep->nationality, iv, encL, encR);
        ok = res.success;
        msg = toWide(res.message);
        if (ok) msg = L"✓ נרשם בהצלחה! UserID=" + std::to_wstring(res.newUserID) + L"\n" + msg;
    } catch (const std::exception& e) {
        msg = toWide(std::string(e.what()));
    }

    delete ep;
    std::wstring* pmsg = new std::wstring(msg);
    PostMessageW(p ? nullptr : nullptr, 0, 0, 0); // dummy
    PostMessageW(((EnrollParams*)p == nullptr ? g_hwnd : g_hwnd),
                 WM_ENROLL_DONE, ok?1:0, (LPARAM)(new std::wstring(msg)));
    return 0;
}

// תיקון: העברת hwnd דרך struct
static DWORD WINAPI EnrollWorker2(LPVOID param) {
    EnrollParams* ep = (EnrollParams*)param;
    HWND hwnd = ep->hwnd;
    bool ok = false;
    std::wstring* pmsg = new std::wstring(L"");

    try {
        auto lBest = pickBest(ep->casiaDir / "L");
        auto rBest = pickBest(ep->casiaDir / "R");
        if ((int)lBest.size()<ENROLL_BEST || (int)rBest.size()<ENROLL_BEST)
            throw std::runtime_error("נדרשות לפחות " +
                std::to_string(ENROLL_SKIP + ENROLL_BEST) +
                " תמונות לכל עין");

        Encryptor enc;
        uint8_t iv[16]={};
        std::vector<std::vector<uint8_t>> encL, encR;
        encL.push_back(enc.encrypt(lBest[0].png, iv));
        for (int i=1;i<ENROLL_BEST;++i) encL.push_back(enc.encryptWithIV(lBest[i].png,iv));
        for (int i=0;i<ENROLL_BEST;++i) encR.push_back(enc.encryptWithIV(rBest[i].png,iv));

        ServerClient sc(ep->cfg);
        EnrollResult res = sc.enroll(ep->passID, ep->fullName, ep->nationality, iv, encL, encR);
        ok = res.success;
        *pmsg = toWide(res.message);
        if (ok) {
            *pmsg = L"✓ נרשם בהצלחה! UserID=" + std::to_wstring(res.newUserID) + L"\n" + *pmsg;
            // כתיבת שמות תמונות הרישום — כדי ש-Gate GUI ידלג עליהן
            auto writeEnrolled = [](const fs::path& dir, const std::vector<ScoredImg>& imgs) {
                std::ofstream ef(dir / "enrolled.txt", std::ios::trunc);
                for (auto& img : imgs) ef << img.path.filename().string() << '\n';
            };
            writeEnrolled(ep->casiaDir / "L", lBest);
            writeEnrolled(ep->casiaDir / "R", rBest);
        }
    } catch (const std::exception& e) {
        *pmsg = toWide(std::string(e.what()));
    }

    delete ep;
    PostMessageW(hwnd, WM_ENROLL_DONE, ok?1:0, (LPARAM)pmsg);
    return 0;
}

// ─── show/hide פאנלים ──────────────────────────────────────────────────────────
static void showPanel(Tab t) {
    bool enroll = (t == Tab::Enroll);
    // Enroll widgets
        for (HWND h : {g_editCasia, g_editID, g_editName, g_editNat, g_btnBrowse, g_btnEnroll, g_lblPreview})
        if (h) ShowWindow(h, enroll ? SW_SHOW : SW_HIDE);
    // Logs widgets
    for (HWND h : {g_listAccess, g_listChanges, g_btnRefresh})
        if (h) ShowWindow(h, enroll ? SW_HIDE : SW_SHOW);
}

// ─── WndProc ─────────────────────────────────────────────────────────────────
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_CREATE: {
        createGDI();
        HINSTANCE hi = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
        int cw = WIN_W - PAD*2;
        int y  = HDR_H + TAB_H + PAD;

        auto mkLabel = [&](const wchar_t* t, int ly) {
            HWND h = CreateWindowExW(0,L"STATIC",t,
                WS_CHILD|WS_VISIBLE|SS_LEFT,
                PAD,ly,cw,20,hwnd,nullptr,hi,nullptr);
            SendMessageW(h,WM_SETFONT,(WPARAM)g_fLabel,TRUE);
        };
        auto mkEdit = [&](int id, const wchar_t* ph, int ly, int lh=FIELD_H) -> HWND {
            HWND h = CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",
                WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,
                PAD,ly,cw,lh,hwnd,(HMENU)(UINT_PTR)id,hi,nullptr);
            SendMessageW(h,WM_SETFONT,(WPARAM)g_fInput,TRUE);
            SendMessageW(h,EM_SETCUEBANNER,TRUE,(LPARAM)ph);
            return h;
        };
        auto mkBtn = [&](int id, const wchar_t* t, int bx,int by,int bw,int bh) -> HWND {
            HWND h = CreateWindowExW(0,L"BUTTON",t,
                WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
                bx,by,bw,bh,hwnd,(HMENU)(UINT_PTR)id,hi,nullptr);
            SendMessageW(h,WM_SETFONT,(WPARAM)g_fLabel,TRUE);
            return h;
        };

        // ── Enroll panel ──────────────────────────────────────────────────────
        mkLabel(L"מספר אדם מ-CASIA (000–999)", y);
        int casiaEditW = cw - 120;
        g_editCasia = mkEdit(IDC_EDIT_CASIA, L"לדוגמה: 042", y+22, FIELD_H);
        // שנה רוחב ל-casiaEditW
        SetWindowPos(g_editCasia, nullptr, PAD, y+22, casiaEditW, FIELD_H, SWP_NOZORDER);
        g_btnBrowse = mkBtn(IDC_BTN_BROWSE, L"📁 עיון", PAD+casiaEditW+8, y+22, 104, FIELD_H);

        mkLabel(L"תעודת זהות ישראלית (9 ספרות)", y+68);
        g_editID = mkEdit(IDC_EDIT_ID, L"לדוגמה: 036622330", y+90);
        SendMessageW(g_editID, EM_SETLIMITTEXT, 9, 0);

        mkLabel(L"שם מלא", y+136);
        g_editName = mkEdit(IDC_EDIT_NAME, L"לדוגמה: ישראל ישראלי", y+158);

        mkLabel(L"לאום (Nationality)", y+204);
        g_editNat = mkEdit(IDC_EDIT_NAT, L"לדוגמה: Israeli", y+226);

        // תצוגה מקדימה
        g_lblPreview = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_READONLY|WS_VSCROLL,
            PAD, y+272, cw, 72, hwnd, (HMENU)(UINT_PTR)IDC_LBL_PREVIEW, hi, nullptr);
        SendMessageW(g_lblPreview, WM_SETFONT, (WPARAM)g_fMono, TRUE);

        g_btnEnroll = mkBtn(IDC_BTN_ENROLL, L"✅  רשום משתמש", (WIN_W-200)/2, y+360, 200, 42);

        // ── Logs panel ────────────────────────────────────────────────────────
        int listY = HDR_H + TAB_H + PAD;
        int halfH = (WIN_H - listY - PAD*3 - 38) / 2;

        // כותרות Labels
        HWND lblA = CreateWindowExW(0,L"STATIC",L"📋 לוג גישות (access_log.csv)",
            WS_CHILD|SS_LEFT, PAD,listY,cw,20,hwnd,(HMENU)(UINT_PTR)IDC_LBL_ACCESS,hi,nullptr);
        SendMessageW(lblA,WM_SETFONT,(WPARAM)g_fLabel,TRUE);
        ShowWindow(lblA, SW_HIDE);

        g_listAccess = CreateWindowExW(WS_EX_CLIENTEDGE,WC_LISTVIEWW,L"",
            WS_CHILD|WS_VSCROLL|LVS_REPORT|LVS_SHOWSELALWAYS,
            PAD,listY+22,cw,halfH,hwnd,(HMENU)(UINT_PTR)IDC_LIST_ACCESS,hi,nullptr);
        setupAccessList(g_listAccess);
        ShowWindow(g_listAccess, SW_HIDE);

        HWND lblC = CreateWindowExW(0,L"STATIC",L"🔄 לוג שינויים (changes_log.csv)",
            WS_CHILD|SS_LEFT, PAD,listY+22+halfH+PAD,cw,20,
            hwnd,(HMENU)(UINT_PTR)IDC_LBL_CHANGES,hi,nullptr);
        SendMessageW(lblC,WM_SETFONT,(WPARAM)g_fLabel,TRUE);
        ShowWindow(lblC, SW_HIDE);

        g_listChanges = CreateWindowExW(WS_EX_CLIENTEDGE,WC_LISTVIEWW,L"",
            WS_CHILD|WS_VSCROLL|LVS_REPORT|LVS_SHOWSELALWAYS,
            PAD,listY+22+halfH+PAD+22,cw,halfH,
            hwnd,(HMENU)(UINT_PTR)IDC_LIST_CHANGES,hi,nullptr);
        setupChangesList(g_listChanges);
        ShowWindow(g_listChanges, SW_HIDE);

        g_btnRefresh = mkBtn(IDC_BTN_REFRESH, L"🔄 רענן לוגים",
            (WIN_W-160)/2, WIN_H-PAD-38, 160, 38);
        ShowWindow(g_btnRefresh, SW_HIDE);

        // שמירת hwnd Labels ב-window data
        SetWindowLongPtrW(lblA, GWLP_USERDATA, (LPARAM)lblA);
        SetWindowLongPtrW(lblC, GWLP_USERDATA, (LPARAM)lblC);

        showPanel(Tab::Enroll);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hScreen = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HDC hMem = CreateCompatibleDC(hScreen);
        HBITMAP bm = CreateCompatibleBitmap(hScreen, rc.right, rc.bottom);
        HBITMAP ob = (HBITMAP)SelectObject(hMem, bm);

        FillRect(hMem, &rc, g_brBg);
        drawHeader(hMem);
        drawTabs(hMem);

        // שורת סטטוס תחתונה
        if (!g_statusMsg.empty()) {
            COLORREF sc = (g_eState==EnrollState::Done) ? C_GREEN :
                          (g_eState==EnrollState::Error) ? C_RED : C_TEXT2;
            RECT rs={PAD, WIN_H-PAD-22, WIN_W-PAD, WIN_H-PAD};
            drawTxt(hMem, g_statusMsg, rs, g_fLabel, sc, DT_LEFT|DT_SINGLELINE);
        }

        BitBlt(hScreen,0,0,rc.right,rc.bottom,hMem,0,0,SRCCOPY);
        SelectObject(hMem,ob); DeleteObject(bm); DeleteDC(hMem);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND: return 1;

    case WM_CTLCOLORSTATIC: {
        HWND src = (HWND)lp;
        HDC hdc = (HDC)wp;
        // label תת-כותרת
        SetTextColor(hdc, C_TEXT2);
        SetBkColor(hdc, C_BG);
        return (LRESULT)g_brBg;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc=(HDC)wp;
        SetTextColor(hdc, C_TEXT1); SetBkColor(hdc, C_EDIT_BG);
        return (LRESULT)g_brEdit;
    }
    case WM_CTLCOLORLISTBOX: {
        HDC hdc=(HDC)wp;
        SetTextColor(hdc,C_TEXT1); SetBkColor(hdc,C_EDIT_BG);
        return (LRESULT)g_brEdit;
    }

    case WM_COMMAND: {
        int id = LOWORD(wp);

        if (id == IDC_TAB_ENROLL || id == IDC_TAB_LOGS) {
            // טאב הוחלף — ראה WM_LBUTTONDOWN
            return 0;
        }

        if (id == IDC_BTN_BROWSE) {
            // בחירת תיקיית אדם ב-CASIA
            fs::path casiaRoot = findCasiaRoot();
            BROWSEINFOW bi={};
            bi.hwndOwner = hwnd;
            bi.lpszTitle = L"בחר תיקיית אדם מ-CASIA (לדוגמה: .../042)";
            bi.ulFlags   = BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
            if (!casiaRoot.empty()) {
                // הגדר תיקיית התחלה
                bi.lParam = (LPARAM)casiaRoot.c_str();
            }
            LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                wchar_t path[MAX_PATH]={};
                SHGetPathFromIDListW(pidl, path);
                CoTaskMemFree(pidl);
                g_casiaDir = path;
                // חלץ מספר אדם מהתיקייה (שם הספרייה האחרון)
                fs::path chosen(path);
                std::wstring folderName = chosen.filename().wstring();
                SetWindowTextW(g_editCasia, folderName.c_str());
                updatePreview(chosen);
            }
            return 0;
        }

        if (id == IDC_BTN_ENROLL) {
            if (g_eState == EnrollState::Working) return 0;

            std::wstring casiaW = getEdit(g_editCasia);
            std::wstring idW    = getEdit(g_editID);
            std::wstring nameW  = getEdit(g_editName);
            std::wstring natW   = getEdit(g_editNat);

            if (casiaW.empty()) {
                MessageBoxW(hwnd,L"נא להזין מספר אדם מ-CASIA (000–999) או לבחור תיקייה",
                    L"שגיאת קלט",MB_ICONWARNING); return 0;
            }

            // ── ולידציה של תעודת זהות ישראלית (אלגוריתם לון) ──
            {
                std::string idStr = toUtf8(idW);
                bool valid = false;
                if (idStr.size() == 9 &&
                    std::all_of(idStr.begin(), idStr.end(), ::isdigit)) {
                    int sum = 0;
                    for (int i = 0; i < 9; ++i) {
                        int d = (idStr[i] - '0') * (i % 2 == 0 ? 1 : 2);
                        sum += (d > 9) ? d - 9 : d;
                    }
                    valid = (sum % 10 == 0);
                }
                if (!valid) {
                    MessageBoxW(hwnd,
                        L"תעודת הזהות אינה תקינה.\nנא להזין מספר זהות ישראלי של 9 ספרות תקין.",
                        L"שגיאת תעודת זהות", MB_ICONWARNING);
                    return 0;
                }
            }

            if (nameW.size() < 2) {
                MessageBoxW(hwnd,L"נא להזין שם מלא",L"שגיאת קלט",MB_ICONWARNING); return 0;
            }
            if (!std::getenv("IRIS_AES_KEY")||!*std::getenv("IRIS_AES_KEY")) {
                MessageBoxW(hwnd,L"IRIS_AES_KEY לא מוגדר",L"שגיאה",MB_ICONERROR); return 0;
            }

            // בניית נתיב CASIA
            fs::path cDir;
            if (!g_casiaDir.empty()) {
                cDir = g_casiaDir;
            } else {
                fs::path root = findCasiaRoot();
                if (root.empty()) {
                    MessageBoxW(hwnd,L"לא נמצאה תיקיית CASIA-Iris-Thousand\nהגדר IRIS_CASIA_ROOT",
                        L"שגיאה",MB_ICONERROR); return 0;
                }
                cDir = root / casiaW;
            }
            if (!fs::exists(cDir)) {
                MessageBoxW(hwnd,(L"תיקייה לא קיימת:\n"+cDir.wstring()).c_str(),
                    L"שגיאה",MB_ICONERROR); return 0;
            }

            // PassengerID = תעודת הזהות שהוזנה
            std::string passID = toUtf8(idW);

            ClientConfig cfg;
            const char* h=std::getenv("IRIS_SERVER_HOST"); if(h&&*h) cfg.serverHost=h;
            const char* po=std::getenv("IRIS_SERVER_PORT"); if(po&&*po) cfg.serverPort=std::stoi(po);

            g_eState    = EnrollState::Working;
            g_statusMsg = L"⏳ מעבד תמונות ורושם...";
            EnableWindow(g_btnEnroll, FALSE);
            InvalidateRect(hwnd, nullptr, FALSE);

            EnrollParams* ep = new EnrollParams{
                hwnd, cDir, passID,
                toUtf8(nameW),
                toUtf8(natW.empty() ? L"Unknown" : natW),
                cfg
            };
            HANDLE ht = CreateThread(nullptr,0,EnrollWorker2,ep,0,nullptr);
            if (ht) CloseHandle(ht);
            else {
                delete ep;
                g_eState = EnrollState::Idle;
                EnableWindow(g_btnEnroll, TRUE);
            }
            return 0;
        }

        if (id == IDC_BTN_REFRESH) {
            fs::path logsDir = findLogsDir();
            if (logsDir.empty()) {
                g_statusMsg = L"⚠ תיקיית logs לא נמצאה. הגדר IRIS_LOGS_DIR.";
                InvalidateRect(hwnd,nullptr,FALSE);
                return 0;
            }
            loadCsvToList(g_listAccess,   logsDir/"access_log.csv");
            loadCsvToList(g_listChanges,  logsDir/"changes_log.csv");
            g_statusMsg = L"✓ לוגים עודכנו: " + logsDir.wstring();
            InvalidateRect(hwnd,nullptr,FALSE);
            return 0;
        }

        return 0;
    }

    case WM_LBUTTONDOWN: {
        int x=LOWORD(lp), y=HIWORD(lp);
        int tabY = HDR_H;
        if (y >= tabY && y < tabY+TAB_H) {
            Tab newTab = (x < WIN_W/2) ? Tab::Enroll : Tab::Logs;
            if (newTab != g_tab) {
                g_tab = newTab;
                showPanel(g_tab);
                if (g_tab == Tab::Logs) {
                    // רענן לוגים אוטומטית
                    PostMessageW(hwnd, WM_COMMAND, IDC_BTN_REFRESH, 0);
                }
                InvalidateRect(hwnd, nullptr, TRUE);
            }
        }
        return 0;
    }

    case WM_ENROLL_DONE: {
        bool ok = (wp == 1);
        std::wstring* pmsg = (std::wstring*)lp;
        g_eState    = ok ? EnrollState::Done : EnrollState::Error;
        g_statusMsg = pmsg ? *pmsg : L"";
        delete pmsg;
        EnableWindow(g_btnEnroll, TRUE);
        InvalidateRect(hwnd, nullptr, FALSE);
        if (ok)
            MessageBoxW(hwnd, g_statusMsg.c_str(), L"רישום הצליח ✓", MB_ICONINFORMATION);
        else
            MessageBoxW(hwnd, g_statusMsg.c_str(), L"שגיאת רישום ✗", MB_ICONERROR);
        g_statusMsg = ok ? L"✓ נרשם בהצלחה" : L"✗ שגיאת רישום";
        return 0;
    }

    case WM_DESTROY:
        destroyGDI();
        Gdiplus::GdiplusShutdown(g_gdipT);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ─── WinMain ─────────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    SetProcessDPIAware();

    Gdiplus::GdiplusStartupInput gsi;
    Gdiplus::GdiplusStartup(&g_gdipT, &gsi, nullptr);

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_WIN95_CLASSES|ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&icc);

    CoInitialize(nullptr);  // נדרש ל-SHBrowseForFolder

    WNDCLASSEXW wcx{sizeof(wcx)};
    wcx.style         = CS_HREDRAW|CS_VREDRAW;
    wcx.lpfnWndProc   = WndProc;
    wcx.hInstance     = hInst;
    wcx.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wcx.hbrBackground = nullptr;
    wcx.lpszClassName = L"IrisAdminGUI";
    wcx.hIcon         = LoadIconW(nullptr, IDI_SHIELD);
    wcx.hIconSm       = wcx.hIcon;
    RegisterClassExW(&wcx);

    RECT adj={0,0,WIN_W,WIN_H};
    AdjustWindowRect(&adj, WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX, FALSE);
    int sw=GetSystemMetrics(SM_CXSCREEN), sh=GetSystemMetrics(SM_CYSCREEN);
    int rw=adj.right-adj.left, rh=adj.bottom-adj.top;

    g_hwnd = CreateWindowExW(0, L"IrisAdminGUI",
        L"IrisPass — ממשק מנהל",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
        (sw-rw)/2, (sh-rh)/2, rw, rh,
        nullptr, nullptr, hInst, nullptr);

    if (!g_hwnd) return -1;
    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    CoUninitialize();
    return (int)message.wParam;
}
