#ifndef OS_CCA_LOGGER_HPP
#define OS_CCA_LOGGER_HPP

#include <string>
#include <mutex>
#include <fstream>
#include <iostream>

#if defined(ERROR)
#undef ERROR
#endif

enum class LogLevel {
    INFO,
    WARN,
    ERROR,
    Info = INFO,
    Warn = WARN,
    Error = ERROR
};

/**
 * @brief Thread-safe Logger writing timestamped lines to a log file and stderr.
 */
class Logger {
public:
    static Logger& instance();

    /**
     * @brief Configure the destination log file path.
     * @param filepath Path to the log file.
     * @return true if opened successfully, false otherwise.
     */
    bool setLogFile(const std::string& filepath);

    /**
     * @brief Log a message with a specific severity level.
     * @param level LogLevel (INFO, WARN, ERROR).
     * @param message Text message to log.
     */
    void log(LogLevel level, const std::string& message);

    void info(const std::string& message);
    void warn(const std::string& message);
    void error(const std::string& message);

    /**
     * @brief Platform-aware system error logger.
     * Logs errno + strerror on Linux; GetLastError + FormatMessage on Windows.
     * @param name Name of the failed system call or API function.
     */
    void logSysError(const char* name);

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::string getTimestampString() const;
    static const char* levelToString(LogLevel level) noexcept;

    std::mutex mutex_;
    std::ofstream file_;
    std::string filepath_;
};

// Convenience free helper functions matching requirements
void logInfo(const std::string& message);
void logError(const std::string& message);
void logSysError(const char* name);

#endif // OS_CCA_LOGGER_HPP
