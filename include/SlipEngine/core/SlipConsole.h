#pragma once

#ifndef SLIP_CONSOLE_H
#define SLIP_CONSOLE_H

#include "SlipEngine/core/SlipExport.h"
#include <string>
#include <vector>
#include <mutex>

enum class LogLevel {
    Info,
    Warning,
    Error,
    Success,
    Trace
};

struct SlipLogColor {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;

    constexpr SlipLogColor() = default;
    constexpr SlipLogColor(float _r, float _g, float _b, float _a = 1.0f) : r(_r), g(_g), b(_b), a(_a) {}
};

struct LogEntry {
    std::string text;
    SlipLogColor color;
    LogLevel level = LogLevel::Info;
    std::string timestamp;
};

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

class SLIPENGINE_API SlipConsole {
public:
    static std::vector<LogEntry> s_Logs;
    static bool s_AutoScroll;
    static bool s_ScrollToBottom;
    static std::mutex s_Mutex;

    // Métodos principales de limpieza y registro
    static void Clear();

    static void Log(const std::string& message, LogLevel level = LogLevel::Info);
    static void Log(const std::string& message, const SlipLogColor& color, LogLevel level = LogLevel::Info);

    static void LogInfo(const std::string& message);
    static void LogWarning(const std::string& message);
    static void LogError(const std::string& message);
    static void LogSuccess(const std::string& message);
    static void LogTrace(const std::string& message);

    // Formateo estilo printf
    static void LogFormat(LogLevel level, const char* fmt, ...);

    // Colores según nivel de log
    static SlipLogColor GetColorForLevel(LogLevel level);

    // Conteo de mensajes según nivel
    static size_t GetCount(LogLevel level);
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#endif // SLIP_CONSOLE_H
