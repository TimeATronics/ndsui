#include "ui/HomeContent.h"

#include <cmath>
#include <cstring>

#include "ui/Theme.h"
#include "util/Platform.h"

namespace ndsui {
namespace ui {

namespace {
const char* kMonthNames[] = {"January", "February", "March", "April",
                             "May", "June", "July", "August", "September",
                             "October", "November", "December"};
const char* kDayNames[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};

int daysInMonth(int year, int month) {  // month 0-11
  static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
    return 29;
  return d[month];
}
}  // namespace

void HomeContent::tick() {
  time_t now = time(nullptr);
  struct tm tmv;
  localtime_r(&now, &tmv);
  // 1 fps redraw for the second hand (a few ms per frame; still battery
  // friendly because nothing else redraws while idle)
  if (!m_valid || tmv.tm_sec != m_lastSecond || tmv.tm_min != m_lastMinute ||
      tmv.tm_hour != m_tm.tm_hour || tmv.tm_mday != m_tm.tm_mday ||
      tmv.tm_mon != m_tm.tm_mon) {
    m_lastSecond = tmv.tm_sec;
    m_lastMinute = tmv.tm_min;
    m_changed = true;
  }
  m_tm = tmv;
  m_valid = true;
}

void HomeContent::draw(Canvas& c, int x, int y, int w, int h, bool showClock,
                       bool showCalendar) {
  // paper panel holding both (either can be hidden from the Homepage settings)
  c.roundRect(x, y, w, h, 18, kCardFill);
  c.roundRectOutline(x, y, w, h, 18, kGridLineMajor, 3);
  if (!showClock && !showCalendar) return;

  int pad = std::max(10, h / 24);
  int gap = std::max(16, w / 24);
  if (showClock && showCalendar) {
    int clockW = (int)(w * 0.42f);
    drawClock(c, x + pad, y + pad, clockW, h - 2 * pad);
    drawCalendar(c, x + pad + clockW + gap, y + pad, w - clockW - 3 * pad - gap,
                 h - 2 * pad);
  } else if (showClock) {
    drawClock(c, x + pad, y + pad, w - 2 * pad, h - 2 * pad);
  } else {
    drawCalendar(c, x + pad, y + pad, w - 2 * pad, h - 2 * pad);
  }
}

void HomeContent::drawClock(Canvas& c, int x, int y, int w, int h) {
  int cx = x + w / 2;
  int cy = y + h / 2;
  int r = std::min(w, h) / 2 - 8;

  // face
  c.circle(cx, cy, r, 0xFAFAF8);
  for (int a = 0; a < 360; a += 6) {  // outline via dots (cheap circle edge)
    double rad = a * 3.14159265 / 180.0;
    int px = cx + (int)(std::cos(rad) * r);
    int py = cy + (int)(std::sin(rad) * r);
    c.rect(px - 1, py - 1, 3, 3, kGlyphInk);
  }

  // numerals 12/3/6/9
  const char* nums[] = {"12", "3", "6", "9"};
  int npos[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
  int nd = r - std::max(22, (int)(r * 0.30f));
  for (int i = 0; i < 4; ++i) {
    int nxp = cx + npos[i][0] * nd;
    int nyp = cy + npos[i][1] * nd;
    c.textCenterVC(nxp, nyp, nums[i], kGlyphInk, kFontMedium);
  }

  // hour ticks
  int t0 = std::max(10, (int)(r * 0.16f));
  int t1 = std::max(5, (int)(r * 0.06f));
  for (int i = 0; i < 12; ++i) {
    double rad = i * 30.0 * 3.14159265 / 180.0;
    int x0 = cx + (int)(std::cos(rad) * (r - t0));
    int y0 = cy + (int)(std::sin(rad) * (r - t0));
    int x1 = cx + (int)(std::cos(rad) * (r - t1));
    int y1 = cy + (int)(std::sin(rad) * (r - t1));
    c.line(x0, y0, x1, y1, i % 3 == 0 ? 5 : 3, kGlyphInk);
  }

  // hands (no seconds hand)
  double ha = (m_tm.tm_hour % 12 + m_tm.tm_min / 60.0) * 30.0 - 90.0;
  double ma = m_tm.tm_min * 6.0 - 90.0;
  int hx = cx + (int)(std::cos(ha * 3.14159265 / 180.0) * r * 0.52);
  int hy = cy + (int)(std::sin(ha * 3.14159265 / 180.0) * r * 0.52);
  int mx = cx + (int)(std::cos(ma * 3.14159265 / 180.0) * r * 0.76);
  int my = cy + (int)(std::sin(ma * 3.14159265 / 180.0) * r * 0.76);
  c.line(cx, cy, hx, hy, 8, kGlyphInk);
  c.line(cx, cy, mx, my, 5, kGlyphInk);
  c.circle(cx, cy, 7, kGlyphInk);
}

void HomeContent::drawCalendar(Canvas& c, int x, int y, int w, int h) {
  int year = m_tm.tm_year + 1900;
  int month = m_tm.tm_mon;
  int today = m_tm.tm_mday;

  std::string title = fmt("%s %d", kMonthNames[month], year);
  int titlePx = h < 330 ? kFontSmall + 6 : kFontMedium;
  // nudge the whole calendar down a little inside its box
  int top = 22;
  int headerH = std::max(58, (int)(h * 0.26f));
  c.textVC(x + w / 2 - c.textWidth(title.c_str(), titlePx) / 2, y + top,
           title.c_str(), kPanelInk, titlePx);

  int gridY = y + top + headerH;
  int cellW = w / 7;
  int cellH = std::max(16, (h - headerH) / 6);
  if (cellH > 56) cellH = 56;

  for (int i = 0; i < 7; ++i) {
    bool weekend = i == 0 || i == 6;
    c.textCenterVC(x + cellW * i + cellW / 2, y + headerH - 24, kDayNames[i],
                   weekend ? 0xE04A4A : kPanelInk, kFontSmall);
  }
  // header separator
  c.rect(x, gridY - 8, w, 2, kGridLineMajor);

  struct tm first{};
  first.tm_year = year - 1900;
  first.tm_mon = month;
  first.tm_mday = 1;
  mktime(&first);
  int lead = first.tm_wday;  // 0=Sunday
  int days = daysInMonth(year, month);

  for (int d = 1; d <= days; ++d) {
    int idx = lead + d - 1;
    int col = idx % 7;
    int row = idx / 7;
    int cxp = x + col * cellW;
    int cyp = gridY + row * cellH;
    bool weekend = col == 0 || col == 6;
    if (d == today) {
      c.roundRect(cxp + 3, cyp + 2, cellW - 6, cellH - 4, 10, kChromeDark);
      c.textCenterVC(cxp + cellW / 2, cyp + cellH / 2, fmt("%d", d).c_str(),
                     kWhite, kFontSmall);
    } else {
      c.textCenterVC(cxp + cellW / 2, cyp + cellH / 2, fmt("%d", d).c_str(),
                     weekend ? 0xE04A4A : kPanelInk, kFontSmall);
    }
  }
}

}  // namespace ui
}  // namespace ndsui
