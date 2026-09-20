// Home content: DS style analog clock + month calendar, drawn on a paper
// panel inside the top area.
#pragma once

#include <ctime>

#include "ui/Canvas.h"

namespace ndsui {
namespace ui {

class HomeContent {
 public:
  void tick();  // caches time; marks changed when the displayed minute flips
  bool changed() const { return m_changed; }
  void clearChanged() { m_changed = false; }

  void draw(Canvas& c, int x, int y, int w, int h, bool showClock = true,
            bool showCalendar = true);

 private:
  void drawClock(Canvas& c, int x, int y, int w, int h);
  void drawCalendar(Canvas& c, int x, int y, int w, int h);

  struct tm m_tm{};
  bool m_valid = false;
  bool m_changed = true;
  int m_lastSecond = -1;
  int m_lastMinute = -1;
};

}  // namespace ui
}  // namespace ndsui
