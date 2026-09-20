// Minimal logging.
#pragma once

#include <cstdarg>
#include <cstdio>

namespace ndsui {

enum class LogLevel { Debug = 0, Info, Warn, Error };

void logSetLevel(LogLevel level);
void logWrite(LogLevel level, const char* fmt, ...);

}  // namespace ndsui

#define LOG_DEBUG(...) ::ndsui::logWrite(::ndsui::LogLevel::Debug, __VA_ARGS__)
#define LOG_INFO(...) ::ndsui::logWrite(::ndsui::LogLevel::Info, __VA_ARGS__)
#define LOG_WARN(...) ::ndsui::logWrite(::ndsui::LogLevel::Warn, __VA_ARGS__)
#define LOG_ERROR(...) ::ndsui::logWrite(::ndsui::LogLevel::Error, __VA_ARGS__)
