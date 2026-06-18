
#include "config/ClientConfig.h"
#include "capture/CameraCapture.h"
#include "preprocessing/ImageQualityCheck.h"
#include "security/Encryptor.h"
#include "network/ServerClient.h"
#include "ui/Display.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>

using namespace iris;

// ─────────────────────────────────────────────────────────────────
// Helper: tile multiple probe images into one canvas for display
// ─────────────────────────────────────────────────────────────────
static cv::Mat tileImages(const std::vector<cv::Mat>& imgs)
{
    if (imgs.empty()) return cv::Mat();
    const int targetH = 320;
    std::vector<cv::Mat> resized;
    int totalW = 0;
    for (const auto& img : imgs) {
        cv::Mat r;
        double scale = static_cast<double>(targetH) / img.rows;
        cv::resize(img, r, cv::Size(), scale, scale);
        if (r.channels() == 1) cv::cvtColor(r, r, cv::COLOR_GRAY2BGR);
        resized.push_back(r);
        totalW += r.cols;
    }
    cv::Mat canvas(targetH, totalW + 10 * (int)imgs.size(), CV_8UC3,
                   cv::Scalar(20, 20, 20));
    int x = 5;
    for (size_t i = 0; i < resized.size(); ++i) {
        resized[i].copyTo(canvas(cv::Rect(x, 0, resized[i].cols, targetH)));
        cv::putText(canvas, "Probe " + std::to_string(i + 1),
                    cv::Point(x + 10, 30), cv::FONT_HERSHEY_SIMPLEX,
                    0.7, cv::Scalar(0, 255, 255), 2);
        x += resized[i].cols + 10;
    }
    return canvas;
}

// ─────────────────────────────────────────────────────────────────
//  Read whole file into a byte vector
// ─────────────────────────────────────────────────────────────────
static std::vector<std::uint8_t> readFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("cannot open file: " + path);
    auto size = f.tellg();
    f.seekg(0);
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(size));
    f.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

// ─────────────────────────────────────────────────────────────────
//  Demo mode entry point
// ─────────────────────────────────────────────────────────────────
static int runDemo(const ClientConfig& config,
                   const std::string&  passport,
                   const std::string&  gateName,
                   std::uint8_t        eye,
                   const std::vector<std::string>& imagePaths,
                   bool                autoMode)
{
    if (imagePaths.empty() || imagePaths.size() > 3) {
        std::cerr << "[Demo] need 1..3 --image paths\n";
        return 1;
    }

    Encryptor    encryptor;
    ServerClient server(config);
    Display      display("Iris Recognition - DEMO");

    // ── 1. Load + display the probe images ─────────────────────────
    std::vector<cv::Mat>             previews;
    std::vector<std::vector<uint8_t>> rawBytes;
    for (const auto& p : imagePaths) {
        cv::Mat img = cv::imread(p, cv::IMREAD_UNCHANGED);
        if (img.empty()) {
            std::cerr << "[Demo] failed to read: " << p << "\n";
            return 1;
        }
        previews.push_back(img);
        rawBytes.push_back(readFile(p));   // send original file bytes
    }

    cv::Mat tiled = tileImages(previews);
    display.show(tiled, "ID=" + passport +
                        "  Gate=" + gateName +
                        "  Eye=" + (eye == 0 ? "L" : "R") +
                        "  Probes=" + std::to_string(imagePaths.size()) +
                        "   [SPACE = send to server, ESC = quit]");
    if (!autoMode) {
        int key = display.waitKey(0);
        if (key == 27) return 0;
    } else {
        display.waitKey(500);
    }

    // ── 2. Encrypt all probes with one shared IV ───────────────────
    std::uint8_t iv[16];
    std::vector<std::vector<std::uint8_t>> ciphers;
    ciphers.reserve(rawBytes.size());

    auto firstCipher = encryptor.encrypt(rawBytes[0], iv);   // generates IV
    ciphers.push_back(std::move(firstCipher));
    for (std::size_t i = 1; i < rawBytes.size(); ++i) {
        std::uint8_t throwAwayIv[16];
        // Encrypt remaining probes with the SAME IV as the first (server
        // uses one IV for all probes in a single VerifyRequest).
        auto c = encryptor.encryptWithIV(rawBytes[i], iv);
        ciphers.push_back(std::move(c));
        (void)throwAwayIv;
    }

    display.show(tiled, "Sending to server...");
    display.waitKey(1);

    // ── 3. Send and receive ────────────────────────────────────────
    AuthResponse resp = server.verifyMulti(passport, gateName, eye, iv, ciphers);

    // ── 4. Show result with colored overlay ────────────────────────
    cv::Mat result = tiled.clone();
    cv::Scalar color;
    std::string banner;
    switch (resp.status) {
        case AuthStatus::AUTHORIZED:
            color  = cv::Scalar(0, 200, 0);
            banner = "AUTHORIZED  -  " + resp.matchedName +
                     "   HD=" + cv::format("%.3f", resp.hammingDist);
            if (!resp.flightNumber.empty())
                banner += "   Flight=" + resp.flightNumber;
            if (!resp.seatNumber.empty())
                banner += "   Seat=" + resp.seatNumber;
            break;
        case AuthStatus::DENIED:
            color  = cv::Scalar(0, 0, 220);
            banner = "DENIED   " + resp.message;
            if (resp.hammingDist < 1.0)
                banner += "   HD=" + cv::format("%.3f", resp.hammingDist);
            break;
        default:
            color  = cv::Scalar(0, 165, 255);
            banner = "ERROR: " + resp.message;
            break;
    }
    cv::rectangle(result, cv::Rect(0, 0, result.cols, 60), color, cv::FILLED);
    cv::putText(result, banner, cv::Point(15, 40),
                cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(255, 255, 255), 2);

    std::cout << "[Demo] " << banner << "\n";
    display.show(result, "[ESC to close]");
    if (!autoMode) {
        while (display.waitKey(0) != 27) {}
    } else {
        display.waitKey(1500);
    }

    return (resp.status == AuthStatus::AUTHORIZED) ? 0 : 2;
}

// ─────────────────────────────────────────────────────────────────
//  Camera mode (original behaviour)
// ─────────────────────────────────────────────────────────────────
static int runCamera(const ClientConfig& /*config*/,
                     const std::string&  /*passport*/,
                     const std::string&  /*gateName*/,
                     std::uint8_t        /*eye*/)
{
    // מצב מצלמה חי אינו נתמך עוד — אימות dual-eye מחייב שימוש ב-iris_gate_gui.exe
    std::cerr << "[IrisClient] Live camera mode requires iris_gate_gui.exe (dual-eye verification).\n";
    return 1;
}

// ─────────────────────────────────────────────────────────────────
//  main — argument parsing
// ─────────────────────────────────────────────────────────────────
int main(int argc, char* argv[])
{
    ClientConfig             config;
    bool                     demoMode = false;
    bool                     autoMode = false;
    std::string              passport = "IL12345678";
    std::string              gateName = "A1";
    std::uint8_t             eye      = 0;
    std::vector<std::string> imagePaths;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--demo")                                  demoMode = true;
        else if (a == "--auto")                             autoMode = true;
        else if (a == "--passport" && i + 1 < argc)         passport = argv[++i];
        else if (a == "--gate"     && i + 1 < argc)         gateName = argv[++i];
        else if (a == "--eye"      && i + 1 < argc) {
            std::string v = argv[++i];
            eye = (v == "right" || v == "r" || v == "1") ? 1 : 0;
        }
        else if (a == "--image"    && i + 1 < argc)         imagePaths.push_back(argv[++i]);
        else if (a == "--host"     && i + 1 < argc)         config.serverHost = argv[++i];
        else if (a == "--port"     && i + 1 < argc)         config.serverPort = std::stoi(argv[++i]);
        else if (a == "--help" || a == "-h") {
            std::cout <<
                            "Usage: IrisClient.exe [--demo] [--passport X] [--gate G] [--eye left|right]\n"
                            "                      [--image path]... [--host H] [--port P]\n"
              "  default          : live camera mode\n"
                            "  --demo           : load 1..3 probe images from disk and verify\n";
            return 0;
        }
        else if (i == 1 && a[0] != '-') passport = a;          // backward-compat
        else if (i == 2 && (a == "left" || a == "right"))
            eye = (a == "right") ? 1 : 0;
    }

    try {
        return demoMode
            ? runDemo  (config, passport, gateName, eye, imagePaths, autoMode)
            : runCamera(config, passport, gateName, eye);
    }
    catch (const std::exception& e) {
        std::cerr << "[Iris Client] Fatal: " << e.what() << "\n";
        return 1;
    }
}
