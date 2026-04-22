#include "ServerConfig.h"
#include <fstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

ServerConfig ServerConfig::loadFromFile(const std::string& jsonPath)
{
    std::ifstream file(jsonPath);
    if (!file.is_open())
        throw std::runtime_error("ServerConfig: cannot open '" + jsonPath + "'");

    nlohmann::json j;
    file >> j;

    ServerConfig cfg;

    if (j.contains("port"))             cfg.port             = j["port"].get<int>();
    if (j.contains("numWorkers"))        cfg.numWorkers        = j["numWorkers"].get<int>();
    if (j.contains("hammingThreshold"))  cfg.hammingThreshold  = j["hammingThreshold"].get<double>();
    if (j.contains("normalizedWidth"))   cfg.normalizedWidth   = j["normalizedWidth"].get<int>();
    if (j.contains("normalizedHeight"))  cfg.normalizedHeight  = j["normalizedHeight"].get<int>();

    if (j.contains("dbConnectionString")) {
        std::string s = j["dbConnectionString"].get<std::string>();
        cfg.dbConnectionString = std::wstring(s.begin(), s.end());
    }

    return cfg;
}
