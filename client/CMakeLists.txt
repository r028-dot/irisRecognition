cmake_minimum_required(VERSION 3.20)
project(IrisRecognitionClient LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── חיפוש ספריות ──────────────────────────────────────────────────────────
find_package(OpenCV REQUIRED)
find_package(OpenSSL REQUIRED)

# ── IrisGateGUI — ממשק גרפי לסימולציית שער ─────────────────────────────────
set(GUI_SOURCES
    tools/iris_gate_gui.cpp
    security/Encryptor.cpp
    network/ServerClient.cpp
    preprocessing/ImageQualityCheck.cpp   # קוד בדיקת האיכות המעודכן שלך
)

add_executable(IrisGateGUI WIN32 ${GUI_SOURCES})

target_include_directories(IrisGateGUI PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OpenCV_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(IrisGateGUI PRIVATE
    ${OpenCV_LIBS}
    OpenSSL::SSL
    OpenSSL::Crypto
    ws2_32
    bcrypt
    comctl32
    gdiplus
)

if(MSVC)
    target_compile_definitions(IrisGateGUI PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _HAS_STD_BYTE=0
    )
    # אין NOMINMAX — GDI+ דורש min/max של Windows
    target_compile_options(IrisGateGUI PRIVATE /utf-8)
endif()

# ── IrisAdminGUI — ממשק מנהל: רישום + לוגים ────────────────────────────────
set(ADMIN_SOURCES
    tools/iris_admin_gui.cpp
    security/Encryptor.cpp
    network/ServerClient.cpp
)

add_executable(IrisAdminGUI WIN32 ${ADMIN_SOURCES})

target_include_directories(IrisAdminGUI PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OpenCV_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(IrisAdminGUI PRIVATE
    ${OpenCV_LIBS}
    OpenSSL::SSL
    OpenSSL::Crypto
    ws2_32
    bcrypt
    comctl32
    gdiplus
    shell32
    ole32
)

if(MSVC)
    target_compile_definitions(IrisAdminGUI PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _HAS_STD_BYTE=0
    )
    target_compile_options(IrisAdminGUI PRIVATE /utf-8)
endif()