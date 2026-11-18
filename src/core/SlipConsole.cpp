#include "SlipEngine/core/SlipConsole.h"
#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <cstdarg>

std::vector<LogEntry> SlipConsole::s_Logs;
bool SlipConsole::s_AutoScroll = true;
bool SlipConsole::s_ScrollToBottom = false;
std::mutex SlipConsole::s_Mutex;

namespace {
    std::string GetCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        std::time_t in_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm buf{};
#if defined(_WIN32)
        localtime_s(&buf, &in_time_t);
#else
        localtime_r(&in_time_t, &buf);
#endif
        std::ostringstream ss;
        ss << std::put_time(&buf, "%H:%M:%S");
        return ss.str();
    }

    const char* GetLevelPrefix(LogLevel level) {
        switch (level) {
            case LogLevel::Info:    return "[INFO] ";
            case LogLevel::Warning: return "[WARN] ";
            case LogLevel::Error:   return "[ERROR] ";
            case LogLevel::Success: return "[OK] ";
            case LogLevel::Trace:   return "[TRACE] ";
            default:                return "";
        }
    }
}

SlipLogColor SlipConsole::GetColorForLevel(LogLevel level) {
    switch (level) {
        case LogLevel::Info:    return SlipLogColor(0.9f, 0.9f, 0.9f, 1.0f); // Blanco grisáceo
        case LogLevel::Warning: return SlipLogColor(1.0f, 0.85f, 0.2f, 1.0f); // Amarillo / Ámbar
        case LogLevel::Error:   return SlipLogColor(1.0f, 0.35f, 0.35f, 1.0f); // Rojo
        case LogLevel::Success: return SlipLogColor(0.35f, 0.95f, 0.45f, 1.0f); // Verde
        case LogLevel::Trace:   return SlipLogColor(0.6f, 0.6f, 0.6f, 1.0f); // Gris
        default:                return SlipLogColor(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

void SlipConsole::Clear() {
    std::lock_guard<std::mutex> lock(s_Mutex);
    s_Logs.clear();
}

void SlipConsole::Log(const std::string& message, LogLevel level) {
    Log(message, GetColorForLevel(level), level);
}

void SlipConsole::Log(const std::string& message, const SlipLogColor& color, LogLevel level) {
    std::lock_guard<std::mutex> lock(s_Mutex);

    std::string timestamp = GetCurrentTimestamp();
    std::string formattedText = "[" + timestamp + "] " + GetLevelPrefix(level) + message;

    LogEntry entry;
    entry.text = formattedText;
    entry.color = color;
    entry.level = level;
    entry.timestamp = timestamp;

    s_Logs.push_back(entry);
    s_ScrollToBottom = true;

    // Salida también a consola estándar
    if (level == LogLevel::Error) {
        std::cerr << formattedText << std::endl;
    } else {
        std::cout << formattedText << std::endl;
    }
}

void SlipConsole::LogInfo(const std::string& message) {
    Log(message, LogLevel::Info);
}

void SlipConsole::LogWarning(const std::string& message) {
    Log(message, LogLevel::Warning);
}

void SlipConsole::LogError(const std::string& message) {
    Log(message, LogLevel::Error);
}

void SlipConsole::LogSuccess(const std::string& message) {
    Log(message, LogLevel::Success);
}

void SlipConsole::LogTrace(const std::string& message) {
    Log(message, LogLevel::Trace);
}

void SlipConsole::LogFormat(LogLevel level, const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    Log(std::string(buffer), level);
}

size_t SlipConsole::GetCount(LogLevel level) {
    std::lock_guard<std::mutex> lock(s_Mutex);
    size_t count = 0;
    for (const auto& log : s_Logs) {
        if (log.level == level) {
            count++;
        }
    }
    return count;
}
