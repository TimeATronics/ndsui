// Horizontal icon strip: the NDS-style left-right scroller used for the
// main sections and for context lists (systems, settings categories).
// Items are icons only; the selected item's name is drawn by the shell.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ui/Canvas.h"

namespace ndsui {
namespace ui {

struct StripItem {
  std::string label;
  std::string iconPath;      // optional PNG (empty -> procedural glyph)
  SDL_Surface* icon = nullptr;  // resolved by the shell
  uint32_t tint = 0;         // procedural glyph color
};

class Strip {
 public:
  void setItems(std::vector<StripItem> items);
  void setSelection(int index, bool animate = true);

  int selection() const { return m_sel; }
  int count() const { return (int)m_items.size(); }
  const std::vector<StripItem>& items() const { return m_items; }

  void tick();
  bool animating() const { return m_animX != m_targetX; }
  // draws the strip; returns nothing; scrollPX reports the animated offset
  // for parallax
  void draw(Canvas& c, int x, int y, int w, int h);
  float scrollPX() const { return m_animX; }

  // focus indication: 0..1 (1 = focused)
  void setFocused(float f) { m_focus = f; }
  float focused() const { return m_focus; }

  // geometry: the same widget backs both the small context strip and the
  // big top carousel (only the tile size / gap change)
  void setMetrics(int iconSize, int gap) {
    m_iconSize = iconSize;
    m_gap = gap;
  }
  int stride() const { return m_iconSize + m_gap + 54; }

 private:
  std::vector<StripItem> m_items;
  int m_sel = 0;
  float m_animX = 0;      // animated pixel offset towards the selection
  float m_targetX = 0;
  float m_focus = 1.f;    // 0..1 focus emphasis
  int m_iconSize = 76;
  int m_gap = 26;
};

}  // namespace ui
}  // namespace ndsui
