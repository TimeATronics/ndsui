#include "screens/Screen.h"

#include <ctime>

#include "core/Settings.h"
#include "util/Platform.h"

namespace ndsui {

std::string clockString() {
  time_t now = time(nullptr);
  struct tm tmv;
  localtime_r(&now, &tmv);
  char buf[24];
  // 24h by default; the Homepage setting can switch to 12h with AM/PM
  int fmtIdx = Settings::instance().getIntDefault("clock_24h", 0);
  if (fmtIdx == 1) {
    int hr = tmv.tm_hour % 12;
    if (hr == 0) hr = 12;
    snprintf(buf, sizeof(buf), "%d:%02d %s", hr, tmv.tm_min,
             tmv.tm_hour < 12 ? "AM" : "PM");
  } else {
    strftime(buf, sizeof(buf), "%H:%M", &tmv);
  }
  return buf;
}

std::string batteryString() {
  int pct = batteryPercent();
  if (pct < 0) return "--";
  // note: the top bar draws the pill itself (bolt icon when charging), this
  // string is only used by the legacy screens
  return fmt("%d%%", pct);
}

}  // namespace ndsui
