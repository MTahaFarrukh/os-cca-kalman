#include "logger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#define NOGDI
#endif
#include <windows.h>
#if defined(ERROR)
#undef ERROR
#endif
#else
#include <cerrno>
#include <cstring>
#endif

Logger::Logger() {
    // Try to open a default log file if the directory exists or can be created
    std::error_code ec;
    std::filesystem::create_directories("results", ec);
    setLogFile("results/os_cca.log");
}

Logger::~Logger() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

bool Logger::setLogFile(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_.close();
    }

    filepath_ = filepath;
    std::error_code ec;
    std::filesystem::path p(filepath);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path(), ec);
    }

    file_.open(filepath, std::ios::out | std::ios::app);
    return file_.is_open();
}

std::string Logger::getTimestampString() const {
    const auto now = std::chrono::system_clock::now();
    const auto time_val = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()) % 1000;

    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &time_val);
#else
    localtime_r(&time_val, &tm_buf);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

const char* Logger::levelToString(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
        default:              return "LOG";
    }
}

void Logger::log(LogLevel level, const std::string& message) {
    const std::string timestamp = getTimestampString();
    const char* lvlStr = levelToString(level);

    std::ostringstream line;
    line << "[" << timestamp << "] [" << lvlStr << "] " << message;
    const std::string outStr = line.str();

    std::lock_guard<std::mutex> lock(mutex_);
    // Write to stderr
    std::cerr << outStr << "\n";
    std::cerr.flush();

    // Write to file if open
    if (file_.is_open()) {
        file_ << outStr << "\n";
        file_.flush();
    }
}

void Logger::info(const std::string& message) {
    log(LogLevel::INFO, message);
}

void Logger::warn(const std::string& message) {
    log(LogLevel::WARN, message);
}

void Logger::error(const std::string& message) {
    log(LogLevel::ERROR, message);
}

void Logger::logSysError(const char* name) {
    const char* safe_name = (name != nullptr) ? name : "unknown";

#ifdef _WIN32
    const DWORD err = GetLastError();
    LPSTR msgBuf = nullptr;
    const DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&msgBuf),
        0,
        NULL
    );

    std::string detail;
    if (size > 0 && msgBuf != nullptr) {
        detail = msgBuf;
        while (!detail.empty() && (detail.back() == '\r' || detail.back() == '\n' || detail.back() == ' ')) {
            detail.pop_back();
        }
        LocalFree(msgBuf);
    } else {
        detail = "Unknown system error";
    }

    error(std::string("Win32 API '") + safe_name + "' failed: error " +
          std::to_string(err) + " (" + detail + ")");
#else
    const int err = errno;
    char buf[256];
    const char* detail = nullptr;
#if defined(__GLIBC__) && defined(_GNU_SOURCE)
    detail = strerror_r(err, buf, sizeof(buf));
#else
    if (strerror_r(err, buf, sizeof(buf)) == 0) {
        detail = buf;
    } else {
        detail = "Unknown errno";
    }
#endif
    error(std::string("System call '") + safe_name + "' failed: errno " +
          std::to_string(err) + " (" + (detail ? detail : "") + ")");
#endif
}

void logInfo(const std::string& message) {
    Logger::instance().info(message);
}

void logError(const std::string& message) {
    Logger::instance().error(message);
}

void logSysError(const char* name) {
    Logger::instance().logSysError(name);
}
