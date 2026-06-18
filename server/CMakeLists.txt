cmake_minimum_required(VERSION 3.20)
project(IrisRecognitionServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Find dependencies ────────────────────────────────────────────────────────
find_package(OpenCV REQUIRED)
find_package(OpenSSL REQUIRED)

# nlohmann/json (header-only; vcpkg or FetchContent)
find_package(nlohmann_json REQUIRED)

# Windows SDK ODBC libraries (sql.h / sqlext.h ship with the Windows SDK)
# Link against odbc32 and odbccp32
set(ODBC_LIBS odbc32 odbccp32)

# ── Source files ─────────────────────────────────────────────────────────────
set(SERVER_SOURCES
    main.cpp
    config/ServerConfig.cpp
    database/DatabaseManager.cpp
    iris/FeatureExtractor.cpp
    iris/IrisMatcher.cpp
    iris/IrisProcessor.cpp
    iris/Normalization.cpp
    network/ConnectionHandler.cpp
    network/TcpServer.cpp
    security/Encryptor.cpp
    security/ReplayGuard.cpp
    service/BiometricService.cpp
    utils/Logger.cpp
    utils/AccessLogger.cpp
    utils/ThreadPool.cpp
)

add_executable(IrisRecognitionServer ${SERVER_SOURCES})

target_include_directories(IrisRecognitionServer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OpenCV_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(IrisRecognitionServer PRIVATE
    ${OpenCV_LIBS}
    OpenSSL::SSL
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    ${ODBC_LIBS}
    ws2_32      # Winsock
)

# Suppress MSVC warnings about unsafe CRT functions
if(MSVC)
    target_compile_definitions(IrisRecognitionServer PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

# ── EnrollTool ───────────────────────────────────────────────────────────────
# Standalone enrollment utility: reads MMU BMP images and enrolls users into DB.
# Note: EnrollTool talks to the DB directly (not over the network), so it does
# NOT need network AES — but DatabaseManager encrypts iris codes at-rest, so it
# DOES need security/Encryptor.cpp (key IRIS_DB_AES_KEY).
set(ENROLL_SOURCES
    tools/enroll_tool.cpp
    config/ServerConfig.cpp
    database/DatabaseManager.cpp
    iris/FeatureExtractor.cpp
    iris/IrisMatcher.cpp
    iris/IrisProcessor.cpp
    iris/Normalization.cpp
    service/BiometricService.cpp
    security/Encryptor.cpp
    utils/Logger.cpp
    utils/AccessLogger.cpp
)

add_executable(EnrollTool ${ENROLL_SOURCES})

target_include_directories(EnrollTool PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OpenCV_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(EnrollTool PRIVATE
    ${OpenCV_LIBS}
    OpenSSL::SSL
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    ${ODBC_LIBS}
)

if(MSVC)
    target_compile_definitions(EnrollTool PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

# ── IrisSimulation ──────────────────────────────────────────────────────────────
# כלי סימולציה מקומי: בוחר אדם מ-CASIA-Iris-Thousand, מדרג תמונות, מצפין
# ושולח לשרת הרץ לאימות. מאפשר בדיקת הצינור המלא ללא חומרת מצלמה.
set(SIMULATION_SOURCES
    tools/iris_simulation.cpp
    config/ServerConfig.cpp
    iris/FeatureExtractor.cpp
    iris/IrisMatcher.cpp
    iris/Normalization.cpp
    utils/Logger.cpp
)

add_executable(IrisSimulation ${SIMULATION_SOURCES})

target_include_directories(IrisSimulation PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OpenCV_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(IrisSimulation PRIVATE
    ${OpenCV_LIBS}
    OpenSSL::SSL
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    ws2_32
    bcrypt
)

if(MSVC)
    target_compile_definitions(IrisSimulation PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    # הבטח תמיכה ב-UTF-8 בקוד (לקריאת מחרוזות עברית)
    target_compile_options(IrisSimulation PRIVATE /utf-8)
endif()

# הערה: ממשק ה-GUI (IrisGateGUI) עבר לפרויקט הלקוח (client/CMakeLists.txt)
# כדי לשמור על הפרדה נכונה — הלקוח לא משתמש בקוד הפנימי של השרת.
