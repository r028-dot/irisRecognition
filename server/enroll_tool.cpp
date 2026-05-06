// ============================================================
//  enroll_tool.cpp
//  Standalone enrollment utility – enrolls a single user into
//  IrisRecognitionDB via IrisProcessor::enroll() + DatabaseManager.
//
//  Usage:
//    EnrollTool.exe --passport <id> --name "<name>" --nationality <nat>
//                   --left <left_eye_image> --right <right_eye_image>
//
//  Example:
//    EnrollTool.exe --passport IL12345678 --name "Israel Israeli"
//                   --nationality Israeli
//                   --left ..\test_images\MMU-Iris-Database\1\left\aeval1.bmp
//                   --right ..\test_images\MMU-Iris-Database\1\right\aevar1.bmp
// ============================================================

#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <memory>
#include <map>
#include <fstream>

#include <windows.h>
#include <fcntl.h>
#include <io.h>

#include <opencv2/imgcodecs.hpp>

#include "config/ServerConfig.h"
#include "database/DatabaseManager.h"
#include "iris/IrisProcessor.h"

namespace fs = std::filesystem;

// ── helpers ──────────────────────────────────────────────────────────────────

// Convert wide string to UTF-8 (DatabaseManager::strToWide expects UTF-8 input)
static std::string wideToUtf8(const std::wstring& ws)
{
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string result(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, result.data(), len, nullptr, nullptr);
    return result;
}

static void printUsage(const wchar_t* exe)
{
    std::wcout << L"Modes:\n\n"
               << L"  ENROLL:\n"
               << L"    " << exe << L" --enroll --passport <id> --name \"<name>\" --nationality <nat>\n"
               << L"               --left <img> --right <img>\n\n"
               << L"  VERIFY:\n"
               << L"    " << exe << L" --verify --passport <id> --eye <left|right> --image <img>\n\n"
               << L"Example verify:\n"
               << L"  EnrollTool.exe --verify --passport IL12345678 --eye left\n"
               << L"                 --image ..\\test_images\\3\\left\\changl1.bmp\n";
}

// Load image from wide path (handles Hebrew/Unicode paths)
static std::vector<uint8_t> loadImageAsBytes(const std::wstring& widePath)
{
    std::ifstream file(widePath, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot open image file");
    std::vector<uint8_t> fileData(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );
    cv::Mat img = cv::imdecode(fileData, cv::IMREAD_GRAYSCALE);
    if (img.empty())
        throw std::runtime_error("Cannot decode image (unsupported format?)");
    std::vector<uint8_t> buf;
    cv::imencode(".png", img, buf);
    return buf;
}

// Parse --key value pairs (or boolean --flag) from wide argv into a map
static std::map<std::wstring, std::wstring> parseArgs(int argc, wchar_t* argv[])
{
    std::map<std::wstring, std::wstring> args;
    for (int i = 1; i < argc; ++i) {
        std::wstring key = argv[i];
        if (key.size() > 2 && key[0] == L'-' && key[1] == L'-') {
            key = key.substr(2);
            // Check if next arg is a value (not another flag)
            if (i + 1 < argc && (argv[i+1][0] != L'-' || argv[i+1][1] != L'-')) {
                args[key] = argv[i + 1];
                ++i;
            } else {
                args[key] = L"1"; // boolean flag
            }
        }
    }
    return args;
}

// ── wmain: Unicode-aware entry point ─────────────────────────────────────────

int wmain(int argc, wchar_t* argv[])
{
    // Enable UTF-16 output on stdout for Hebrew console display.
    // Leave stderr in its default byte mode so OpenCV error messages work.
    _setmode(_fileno(stdout), _O_U16TEXT);

    if (argc < 2 || std::wstring(argv[1]) == L"--help" || std::wstring(argv[1]) == L"-h") {
        printUsage(argv[0]);
        return 0;
    }

    auto args = parseArgs(argc, argv);

    // ── VERIFY mode ──────────────────────────────────────────────────────────
    if (args.count(L"verify")) {
        for (auto& r : std::vector<std::wstring>{L"passport", L"eye", L"image"}) {
            if (!args.count(r)) {
                std::wcerr << L"[ERROR] Missing --" << r << L"\n";
                printUsage(argv[0]); return 1;
            }
        }
        const std::wstring imagePath = args[L"image"];
        const std::string  passport  = wideToUtf8(args[L"passport"]);
        const std::wstring wEye      = args[L"eye"];
        int eye = (wEye == L"right") ? 1 : 0;

        if (!fs::exists(imagePath)) {
            std::wcerr << L"[ERROR] Image not found: " << imagePath << L"\n";
            return 1;
        }

        std::wcout << L"=======================================================\n";
        std::wcout << L"  VERIFY MODE\n";
        std::wcout << L"=======================================================\n";
        std::wcout << L"  Passport : " << args[L"passport"] << L"\n";
        std::wcout << L"  Eye      : " << wEye << L"\n";
        std::wcout << L"  Image    : " << imagePath << L"\n";
        std::wcout << L"-------------------------------------------------------\n";
        std::wcout << L"  Step 1: Loading image...\n";

        try {
            auto imgBytes = loadImageAsBytes(imagePath);
            std::wcout << L"  Step 2: Connecting to DB...\n";
            ServerConfig cfg;
            auto db = std::make_shared<DatabaseManager>(cfg.dbConnectionString);

            std::wcout << L"  Step 3: Running iris pipeline\n";
            std::wcout << L"          (CLAHE → Hough segmentation → Rubber Sheet\n";
            std::wcout << L"           → Log-Gabor → IrisCode 2048-bit)...\n";
            IrisProcessor processor(db);
            AuthResult result = processor.verify(passport, imgBytes, eye);

            std::wcout << L"  Step 4: Hamming distance vs stored IrisCode...\n";
            std::wcout << L"-------------------------------------------------------\n";
            std::wcout << L"  Hamming Distance : " << result.hammingDist << L"\n";
            std::wcout << L"  Threshold        : 0.32\n";

            if (result.status == AuthStatus::MATCH) {
                std::wcout << L"  Result           : ** MATCH ** -> " << result.matchedName.c_str() << L"\n";
            } else if (result.status == AuthStatus::USER_NOT_FOUND) {
                std::wcout << L"  Result           : USER NOT FOUND in DB\n";
            } else {
                std::wcout << L"  Result           : NO MATCH (Hamming too high)\n";
            }
            std::wcout << L"=======================================================\n";
            return (result.status == AuthStatus::MATCH) ? 0 : 2;
        }
        catch (const std::exception& e) {
            fprintf(stderr, "[ERROR] %s\n", e.what());
            return 1;
        }
    }

    // ── ENROLL mode ──────────────────────────────────────────────────────────
    // (original enroll logic — requires --enroll flag OR no mode flag)
    const std::vector<std::wstring> required = { L"passport", L"name", L"nationality", L"left", L"right" };
    for (auto& r : required) {
        if (args.find(r) == args.end()) {
            std::wcerr << L"[ERROR] Missing argument: --" << r << L"\n\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    const std::wstring wPassport    = args[L"passport"];
    const std::wstring wName        = args[L"name"];
    const std::wstring wNationality = args[L"nationality"];
    const std::wstring leftPath     = args[L"left"];
    const std::wstring rightPath    = args[L"right"];

    // Convert to UTF-8 for the server pipeline (DatabaseManager expects UTF-8)
    const std::string passport    = wideToUtf8(wPassport);
    const std::string name        = wideToUtf8(wName);
    const std::string nationality = wideToUtf8(wNationality);

    // Validate image files exist
    if (!fs::exists(leftPath)) {
        std::wcerr << L"[ERROR] Left eye image not found: " << leftPath << L"\n";
        return 1;
    }
    if (!fs::exists(rightPath)) {
        std::wcerr << L"[ERROR] Right eye image not found: " << rightPath << L"\n";
        return 1;
    }

    std::wcout << L"=======================================================\n";
    std::wcout << L"  ENROLLMENT TOOL - IrisRecognitionDB\n";
    std::wcout << L"=======================================================\n";
    std::wcout << L"  Passport   : " << wPassport    << L"\n";
    std::wcout << L"  Name       : " << wName        << L"\n";
    std::wcout << L"  Nationality: " << wNationality << L"\n";
    std::wcout << L"  Left  eye  : " << leftPath     << L"\n";
    std::wcout << L"  Right eye  : " << rightPath    << L"\n";
    std::wcout << L"-------------------------------------------------------\n";

    try {
        ServerConfig cfg;
        auto db = std::make_shared<DatabaseManager>(cfg.dbConnectionString);

        if (db->userExists(passport)) {
            std::wcout << L"[SKIP] Passport " << wPassport << L" is already enrolled.\n";
            return 0;
        }

        auto imgLeft  = loadImageAsBytes(leftPath);
        auto imgRight = loadImageAsBytes(rightPath);

        // Collect optional extra images for left eye (--left2, --left3)
        // and right eye (--right2, --right3).  All templates are stored in
        // a single DB row per eye (IrisCode1 / IrisCode2 / IrisCode3).
        auto collectImages = [&](const wchar_t* key1, const wchar_t* key2,
                                  const std::vector<uint8_t>& primary)
            -> std::vector<std::vector<uint8_t>>
        {
            std::vector<std::vector<uint8_t>> images;
            images.push_back(primary);
            for (auto key : { key1, key2 }) {
                if (!args.count(key)) break;
                const std::wstring path = args.at(key);
                if (!fs::exists(path)) {
                    fprintf(stderr, "[WARN] Image not found, skipped: extra template\n");
                    continue;
                }
                try { images.push_back(loadImageAsBytes(path)); }
                catch (const std::exception& ex) {
                    fprintf(stderr, "[WARN] Could not load extra image: %s\n", ex.what());
                }
            }
            return images;
        };

        auto leftImages  = collectImages(L"left2",  L"left3",  imgLeft);
        auto rightImages = collectImages(L"right2", L"right3", imgRight);

        IrisProcessor processor(db);
        AuthResult result = processor.enroll(passport, name, nationality,
                                              leftImages, rightImages);

        if (result.status == AuthStatus::MATCH) {
            std::wcout << L"[OK] Enrolled successfully! UserID = "
                       << result.matchedUserID << L"\n";
            std::wcout << L"     Left  templates stored: " << leftImages.size()  << L"\n";
            std::wcout << L"     Right templates stored: " << rightImages.size() << L"\n";
        } else {
            std::wcerr << L"[FAIL] " << result.message.c_str() << L"\n";
            return 1;
        }
    }
    catch (const std::exception& e) {
        fprintf(stderr, "[ERROR] %s\n", e.what());
        return 1;
    }

    std::wcout << L"=======================================================\n";
    return 0;
}
