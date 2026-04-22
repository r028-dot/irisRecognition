#pragma once
#include <string>

// All runtime-configurable parameters for the server.
// Loaded from config/config.json at startup.
struct ServerConfig {
    // Network
    int         port        = 9000;
    int         numWorkers  = 8;

    // Iris matching
    double      hammingThreshold  = 0.32;
    int         normalizedWidth   = 512;
    int         normalizedHeight  = 64;

    // Database (ction string)
    std::wstring dbConnectionString =
        L"DRIVER={ODBC Driver 17 for SQL Server};"
        L"SERVER=.\\SQLEXPRESS;"
        L"DATABASE=IrisRecognitionDB;"
        L"Trusted_Connection=yes;";

    // Loads all fields from a JSON file.  Throws std::runtime_error on failure.
    static ServerConfig loadFromFile(const std::string& jsonPath);
};
