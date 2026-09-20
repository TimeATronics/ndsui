// Parallax "math notebook" background: a paper base plus two grid layers
// that shift at different rates with the UI motion, giving the 3D feel of
// the DS menu. All layers are tiled from small patterns and blitted with
// offsets, so cost is a handful of small blits per redraw.
#pragma once

#include "ui/Canvas.h"

namespace ndsui {
namespace ui {

class Background {
 public:
  void init();
  // offset: UI motion in pixels (strip scroll + focus), scaled per layer
  void draw(Canvas& c, float offsetX, float offsetY);
  // rows [y0, y1): lets the shell repaint just the strip band when the
  // background itself has not moved (save-under without a cache)
  void drawRows(Canvas& c, float offsetX, float offsetY, int y0, int y1);

 private:
  void drawGridLayer(Canvas& c, int cell, int thick, uint32_t line,
                     uint32_t lineMajor, int majorEvery, float ox, float oy);

  bool m_inited = false;
};

}  // namespace ui
}  // namespace ndsui
