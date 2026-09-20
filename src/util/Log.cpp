#include "util/Log.h"

#include <cstring>

namespace ndsui {

namespace {
LogLevel g_level = LogLevel::Info;

const char* tag(LogLevel l) {
  switch (l) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warn: return "WARN";
    case LogLevel::Error: return "ERROR";
  }
  return "?";
}
}  // namespace

void logSetLevel(LogLevel level) { g_level = level; }

void logWrite(LogLevel level, const char* fmt, ...) {
  if ((int)level < (int)g_level) return;
  char msg[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  fprintf(stderr, "[%s] %s\n", tag(level), msg);
  fflush(stderr);
}

}  // namespace ndsui
