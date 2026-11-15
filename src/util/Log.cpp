#include "util/Log.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace fdve {

namespace {
std::atomic<int> g_level{static_cast<int>(LogLevel::Info)};
const char* const kNames[] = {"DEBUG", "INFO", "WARN", "ERROR"};
} // namespace

void setLogLevel(LogLevel level)
{
    g_level.store(static_cast<int>(level));
}

LogLevel logLevel()
{
    return static_cast<LogLevel>(g_level.load());
}

bool parseLogLevel(const char* name, LogLevel& out)
{
    const char* const names[] = {"debug", "info", "warn", "error"};
    for (int i = 0; i < 4; i++)
    {
        if (std::strcmp(name, names[i]) == 0)
        {
            out = static_cast<LogLevel>(i);
            return true;
        }
    }
    return false;
}

const char* level_name(LogLevel level)
{
    const char* const names[] = {"debug", "info", "warn", "error"};
    return names[static_cast<int>(level)];
}

void logMessage(LogLevel level, const char* format, ...)
{
    if (static_cast<int>(level) < g_level.load()) return;
    char text[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    std::fprintf(stderr, "%-5s %s\n", kNames[static_cast<int>(level)], text);
}

} // namespace fdve
