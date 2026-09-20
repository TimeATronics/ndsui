#include "ui/Strip.h"

#include <algorithm>
#include <cmath>

#include "ui/Theme.h"
#include "util/Platform.h"

namespace ndsui {
namespace ui {

void Strip::setItems(std::vector<StripItem> items) {
  m_items = std::move(items);
  m_sel = 0;
  m_animX = 0;
  m_targetX = 0;
}

void Strip::setSelection(int index, bool animate) {
  if (m_items.empty()) return;
  int n = (int)m_items.size();
  m_sel = ((index % n) + n) % n;
  m_targetX = (float)m_sel;
  if (!animate) m_animX = m_targetX;
}

void Strip::tick() {
  float d = m_targetX - m_animX;
  if (std::fabs(d) < 0.004f) {
    m_animX = m_targetX;
    return;
  }
  // slightly snappier ease-out so a single press settles in ~150 ms
  m_animX += d * 0.34f;
}

namespace {
uint32_t blend(uint32_t a, uint32_t b, float t) {
  int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
  int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
  int r = (int)(ar + (br - ar) * t);
  int g = (int)(ag + (bg - ag) * t);
  int bl = (int)(ab + (bb - ab) * t);
  return (uint32_t)((r << 16) | (g << 8) | bl);
}
}  // namespace

void Strip::draw(Canvas& c, int x, int y, int w, int h) {
  if (m_items.empty()) return;
  const int iconSize = m_iconSize;
  const int stride = this->stride();

  int first = std::max(0, m_sel - 3);
  int last = std::min((int)m_items.size() - 1, m_sel + 3);
  for (int i = first; i <= last; ++i) {
    float ix = x + w / 2.f + (i - m_animX) * stride - iconSize / 2.f;
    float dist = std::fabs((float)i - m_animX);
    float scale = 1.f - std::min(0.30f, dist * 0.14f);
    float alpha = 1.f - std::min(0.60f, dist * 0.34f);
    int s = (int)(iconSize * scale);
    int iy = y + (h - s) / 2 + (int)((1.f - scale) * -6.f);
    bool sel = i == m_sel;

    // every item gets its squircle card; the selected one is emphasised and
    // gets the light-blue focus outline when this strip owns the focus
    const bool big = iconSize >= 100;
    int pw = s + (big ? 26 : 52), ph = s + (big ? 26 : 34);
    int rad = big ? 28 : 22;
    int px = (int)ix + s / 2 - pw / 2;
    int py = iy + s / 2 - ph / 2;
    c.roundRect(px, py, pw, ph, rad, kCardFill);
    c.roundRectOutline(px, py, pw, ph, rad,
                       sel ? (m_focus > 0.5f ? kFocusBlue : kGridLineMajor)
                           : kGridLine,
                       sel ? 3 : 2);

    if (m_items[i].icon) {
      // scale the icon to the *current* card size: the outer cards are drawn
      // smaller, and a fixed-size blit used to overflow their boxes
      int iw = m_items[i].icon->w, ih = m_items[i].icon->h;
      float k = std::min((float)s / iw, (float)s / ih);
      int dw = (int)(iw * k), dh = (int)(ih * k);
      if (dw < 1) dw = 1;
      if (dh < 1) dh = 1;
      SDL_Rect dst{(int)ix + (s - dw) / 2, iy + (s - dh) / 2, dw, dh};
      c.markRegionDirty(dst.x, dst.y, dst.w, dst.h);
      SDL_BlitScaled(m_items[i].icon, nullptr, c.surface(), &dst);
    } else {
      uint32_t col = m_items[i].tint ? m_items[i].tint : kChromeDark;
      uint32_t body = blend(kPaper, col, alpha);
      c.roundRect((int)ix, iy, s, s, 18, body);
      // inner detail so the glyph reads as a tile
      c.roundRect((int)ix + s / 4, iy + s / 4, s / 2, s / 2, 10,
                  blend(kPaper, col, alpha * 0.55f));
    }
  }
}

}  // namespace ui
}  // namespace ndsui
