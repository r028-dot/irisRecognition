cmake_minimum_required(VERSION 3.20)
project(IrisRecognitionClient LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── חיפוש ספריות ──────────────────────────────────────────────────────────
find_package(OpenCV REQUIRED)
find_package(OpenSSL REQUIRED)

# ── קבצי מקור ──────────────────────────────────────────────────────────────
set(SOURCES
    main.cpp
    capture/CameraCapture.cpp
    preprocessing/ImageQualityCheck.cpp
    preprocessing/Segmentation.cpp
    security/Encryptor.cpp
    network/ServerClient.cpp
    ui/Display.cpp
)

# ── יצירת הקובץ הרצה ──────────────────────────────────────────────────────
add_executable(IrisClient ${SOURCES})

# ── קבצי כותרת ────────────────────────────────────────────────────────────
target_include_directories(IrisClient PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

# ── קישור ספריות ──────────────────────────────────────────────────────────
target_link_libraries(IrisClient PRIVATE
    ${OpenCV_LIBS}
    OpenSSL::SSL
    OpenSSL::Crypto
    ws2_32       # Winsock (Windows)
)
