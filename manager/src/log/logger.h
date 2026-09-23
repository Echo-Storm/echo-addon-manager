#pragma once
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace eam {

enum class LogLevel : uint8_t { Trace, Debug, Info, Warn, Error };

struct LogEntry {
    LogLevel level;
    std::string source;
    std::string message;
    uint64_t timestamp; // ms since epoch
};

class Logger {
public:
    static Logger& Instance();

    void Init(const std::wstring& logFilePath);
    void Shutdown();

    void Log(LogLevel level, const char* source, const char* fmt, ...);
    void LogV(LogLevel level, const char* source, const char* fmt, va_list args);

    // For UI consumption - returns a snapshot
    std::vector<LogEntry> GetEntries(LogLevel minLevel = LogLevel::Trace) const;
    void Clear();
    void Flush();

    // Messages below this level are dropped before they are formatted (default Info).
    void SetMinLevel(LogLevel level) { m_minLevel.store((uint8_t)level, std::memory_order_relaxed); }
    LogLevel MinLevel() const { return (LogLevel)m_minLevel.load(std::memory_order_relaxed); }

    // Bumped whenever the entry list changes, so the UI can skip rebuilding an unchanged snapshot.
    uint64_t Revision() const { return m_revision.load(std::memory_order_relaxed); }

    static const char* LevelToString(LogLevel level);

private:
    Logger() = default;
    ~Logger();

    static constexpr size_t MAX_ENTRIES = 10000;

    // Repeats of one Trace/Debug/Info message shape (same source, same text up to the first digit)
    // are capped per window, so a per-frame log line cannot flood the file or the render thread.
    struct Throttle { uint64_t windowStartMs = 0; uint32_t count = 0; uint32_t suppressed = 0; };

    void OpenFile();
    void RotateIfNeeded();

    mutable std::mutex m_mutex;
    std::deque<LogEntry> m_entries;
    std::ofstream m_file;
    std::wstring m_path;
    uint64_t m_fileBytes = 0;
    uint64_t m_lastFlushMs = 0;
    std::unordered_map<std::string, Throttle> m_throttle;
    std::atomic<uint8_t> m_minLevel{ (uint8_t)LogLevel::Info };
    std::atomic<uint64_t> m_revision{ 0 };
    bool m_initialized = false;
};

// Logs a backtrace (module+offset) to the log file when the process aborts, terminates on an
// uncaught C++ exception, or a CRT function receives an invalid parameter. Call after Init().
void InstallCrashLogging();

// How many invalid CRT parameter events have been seen so far (all threads). Lets a caller tell
// whether a call into an addon tripped one.
uint32_t InvalidParameterCount();

// Convenience macros
#define LOG_TRACE(src, fmt, ...) ::eam::Logger::Instance().Log(::eam::LogLevel::Trace, src, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(src, fmt, ...) ::eam::Logger::Instance().Log(::eam::LogLevel::Debug, src, fmt, ##__VA_ARGS__)
#define LOG_INFO(src, fmt, ...)  ::eam::Logger::Instance().Log(::eam::LogLevel::Info, src, fmt, ##__VA_ARGS__)
#define LOG_WARN(src, fmt, ...)  ::eam::Logger::Instance().Log(::eam::LogLevel::Warn, src, fmt, ##__VA_ARGS__)
#define LOG_ERROR(src, fmt, ...) ::eam::Logger::Instance().Log(::eam::LogLevel::Error, src, fmt, ##__VA_ARGS__)

} // namespace eam
