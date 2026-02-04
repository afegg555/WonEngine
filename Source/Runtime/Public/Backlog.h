#pragma once
#include "RuntimeExport.h"
#include "Types.h"

#include <string>
#include <cstdio>

#define wonlog_level(str, level, ...) {char text[1024]; snprintf(text, sizeof(text), str, ## __VA_ARGS__); won::backlog::Post(text, level);}
#define wonlog_warning(str, ...) {wonlog_level(str, won::backlog::LogLevel::Warning, ## __VA_ARGS__);}
#define wonlog_error(str, ...) {wonlog_level(str, won::backlog::LogLevel::Error, ## __VA_ARGS__);}
#define wonlog(str, ...) {wonlog_level(str, won::backlog::LogLevel::Default, ## __VA_ARGS__);}

namespace won::backlog
{
    enum class LogLevel
    {
        None,
        Default,
        Warning,
        Error,
    };

    WONENGINE_API void SetEnabled(bool enabled);
    WONENGINE_API bool IsEnabled();

    WONENGINE_API std::string GetText();
    WONENGINE_API void Clear();
    WONENGINE_API void Post(const char* input, LogLevel level = LogLevel::Default);
    WONENGINE_API void Post(const std::string& input, LogLevel level = LogLevel::Default);
    WONENGINE_API void SetLogLevel(LogLevel new_level);
    WONENGINE_API void SetLogFile(const std::string& path);

    struct LogEntry
    {
        std::string text;
        LogLevel level = LogLevel::Default;
    };
}
