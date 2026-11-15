#pragma once

namespace fdve {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };

// Process-wide log level for this program's own messages. The modem applies
// the same level to the FreeDV backend's logger.
void setLogLevel(LogLevel level);
LogLevel logLevel();

// Returns false when the name is not one of "debug", "info", "warn", "error".
bool parseLogLevel(const char* name, LogLevel& out);

void logMessage(LogLevel level, const char* format, ...) __attribute__((format(printf, 2, 3)));

} // namespace fdve

#define FDVE_DEBUG(...) ::fdve::logMessage(::fdve::LogLevel::Debug, __VA_ARGS__)
#define FDVE_INFO(...) ::fdve::logMessage(::fdve::LogLevel::Info, __VA_ARGS__)
#define FDVE_WARN(...) ::fdve::logMessage(::fdve::LogLevel::Warn, __VA_ARGS__)
#define FDVE_ERROR(...) ::fdve::logMessage(::fdve::LogLevel::Error, __VA_ARGS__)
