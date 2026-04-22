#pragma once
#include <string>
#include <fstream>
#include <mutex>

// Use LVL_ prefix to avoid collisions with Windows macros ERROR/DEBUG/WARNING
enum class LogLevel { LVL_DEBUG, LVL_INFO, LVL_WARNING, LVL_ERROR };

// Thread-safe singleton logger.
// Writes to both stdout and a log file (iris_server.log).
class Logger {
public:
    static Logger& instance();

    // Open (or create) log file. Call once at startup.
    void init(const std::string& filePath = "iris_server.log");

    void debug  (const std::string& msg) { log(LogLevel::LVL_DEBUG,   msg); }
    void info   (const std::string& msg) { log(LogLevel::LVL_INFO,    msg); }
    void warning(const std::string& msg) { log(LogLevel::LVL_WARNING, msg); }
    void error  (const std::string& msg) { log(LogLevel::LVL_ERROR,   msg); }

    void log(LogLevel level, const std::string& msg);

private:
    Logger()  = default;
    ~Logger();

    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;

    std::ofstream m_file;
    std::mutex    m_mutex;

    static std::string levelStr(LogLevel level);
    static std::string timestamp();
};
