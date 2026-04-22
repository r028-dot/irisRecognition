#include "Logger.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

Logger& Logger::instance()
{
    static Logger inst;
    return inst;
}

Logger::~Logger()
{
    if (m_file.is_open())
        m_file.close();
}

void Logger::init(const std::string& filePath)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file.is_open()) m_file.close();
    m_file.open(filePath, std::ios::app);
}

void Logger::log(LogLevel level, const std::string& msg)
{
    std::string line = "[" + timestamp() + "] [" + levelStr(level) + "] " + msg;
    std::lock_guard<std::mutex> lock(m_mutex);
    std::cout << line << '\n';
    if (m_file.is_open())
        m_file << line << '\n';
}

std::string Logger::levelStr(LogLevel level)
{
    switch (level) {
        case LogLevel::LVL_DEBUG:   return "DEBUG";
        case LogLevel::LVL_INFO:    return "INFO ";
        case LogLevel::LVL_WARNING: return "WARN ";
        case LogLevel::LVL_ERROR:   return "ERROR";
    }
    return "?????";
}

std::string Logger::timestamp()
{
    auto now   = std::chrono::system_clock::now();
    auto time  = std::chrono::system_clock::to_time_t(now);
    auto ms    = std::chrono::duration_cast<std::chrono::milliseconds>(
                     now.time_since_epoch()) % 1000;
    std::ostringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}
