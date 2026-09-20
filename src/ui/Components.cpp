#include "ui/Components.h"

#include <algorithm>

#include "ui/Theme.h"

namespace ndsui {
namespace ui {

void statusBar(Canvas& c, const std::string& left, const std::string& center,
               const std::string& time, const std::string& battery,
               uint32_t accent) {
  (void)accent;
  c.rect(0, 0, Canvas::W, kStatusH, kChrome);
  c.rect(0, 0, Canvas::W, 2, kChromeTop);
  c.text(20, 12, left.c_str(), kWhite, kFontSmall);
  if (!center.empty()) c.textCenter(Canvas::W / 2, 12, center.c_str(), kWhite, kFontSmall);
  int x = Canvas::W - 20;
  if (!battery.empty()) {
    c.textRight(x, 12, battery.c_str(), kWhite, kFontSmall);
    x -= c.textWidth(battery.c_str(), kFontSmall) + 32;
  }
  if (!time.empty()) c.textRight(x, 12, time.c_str(), kWhite, kFontSmall);
}

void actionBar(Canvas& c, const std::vector<Hint>& hints) {
  c.rect(0, Canvas::H - kActionH, Canvas::W, kActionH, kActionBar);
  c.rect(0, Canvas::H - kActionH, Canvas::W, 2, kBorder);
  int x = 20;
  int y = Canvas::H - kActionH + 18;
  for (const Hint& h : hints) {
    std::string s = h.key + ": " + h.label;
    if (x + c.textWidth(s.c_str(), kFontSmall) > Canvas::W - 16) break;
    c.text(x, y, s.c_str(), kText, kFontSmall);
    x += c.textWidth(s.c_str(), kFontSmall) + 28;
  }
}

void descBar(Canvas& c, const std::string& text) {
  c.rect(0, kDescY, Canvas::W, kDescH, kDescBar);
  c.textClipped(20, kDescY + 12, Canvas::W - 40, text.c_str(), kWhite,
                kFontSmall);
}

void panel(Canvas& c, int x, int y, int w, int h) {
  c.rect(x, y, w, h, kWhite);
  c.rectOutline(x, y, w, h, kBorder, 2);
}

int listRows(Canvas& c, int x, int y, int w, int h,
             const std::vector<std::string>& rows, int selected, int scroll,
             int rowH) {
  int visible = (h - 16) / rowH;
  for (int i = 0; i < visible; ++i) {
    int idx = scroll + i;
    if (idx >= (int)rows.size()) break;
    int ry = y + 8 + i * rowH;
    bool sel = idx == selected;
    if (sel) {
      c.rect(x + 6, ry - 2, w - 12, rowH - 2, kWhite);
      c.rectOutline(x + 6, ry - 2, w - 12, rowH - 2, kChromeDark, 3);
    }
    c.textClipped(x + 18, ry + 10, w - 40, rows[idx].c_str(),
                  sel ? kText : kTextDim, kFontSmall);
  }
  return visible;
}

void scrollbar(Canvas& c, int x, int y, int h, int total, int visible,
               int scroll) {
  if (total <= visible) return;
  c.rect(x, y, 8, h, kPanelAlt);
  int thumbH = std::max(24, h * visible / total);
  int maxScroll = total - visible;
  int thumbY = y + (h - thumbH) * (maxScroll ? scroll / (float)maxScroll : 0);
  c.rect(x, thumbY, 8, thumbH, kGrayDark);
}

void dialog(Canvas& c, const std::string& title,
            const std::vector<std::string>& lines, const std::string& hint) {
  int w = 620, h = 220 + (int)lines.size() * 44;
  int x = (Canvas::W - w) / 2, y = (Canvas::H - h) / 2 - 40;
  c.rect(x, y, w, h, kText);
  c.rectOutline(x, y, w, h, kDialogBorder, 4);
  c.textCenterClipped(x + w / 2, y + 40, w - 40, title.c_str(), kWhite,
                      kFontMedium);
  int ly = y + 126;
  for (const std::string& line : lines) {
    c.textCenterClipped(x + w / 2, ly, w - 40, line.c_str(), kWhite,
                        kFontSmall);
    ly += 44;
  }
  if (!hint.empty())
    c.textCenter(x + w / 2, y + h - 56, hint.c_str(), kWhite, kFontSmall);
}

void buttonGlyph(Canvas& c, int cx, int cy, int d, const char* label,
                 bool circle, uint32_t bg, uint32_t fg) {
  int r = d / 2;
  if (circle) {
    c.circle(cx, cy, r, bg);
  } else {
    c.roundRect(cx - r, cy - r, d, d, d / 4, bg);
  }
  c.textCenterVC(cx, cy - 1, label, fg, kFontTiny);
}

void dpadGlyph(Canvas& c, int cx, int cy, int d, uint32_t col) {
  // plus/cross: four rounded arms around the centre
  int arm = d / 3;
  int len = d / 2;
  c.roundRect(cx - arm / 2, cy - len, arm, len, 2, col);          // up
  c.roundRect(cx - arm / 2, cy, arm, len, 2, col);                // down
  c.roundRect(cx - len, cy - arm / 2, len, arm, 2, col);          // left
  c.roundRect(cx, cy - arm / 2, len, arm, 2, col);                // right
}

void tile(Canvas& c, int x, int y, int w, int h, SDL_Surface* icon,
          const std::string& label, bool selected, uint32_t accent) {
  c.rect(x, y, w, h, selected ? kCardFill : kPanel);
  c.rectOutline(x, y, w, h, selected ? accent : kBorder, selected ? 4 : 2);

  int iy = y + 14;
  if (icon) {
    int iw = std::min((int)icon->w, w - 24);
    int ih = std::min((int)icon->h, h - 70);
    SDL_Rect dst{x + (w - iw) / 2, iy, iw, ih};
    c.markRegionDirty(dst.x, dst.y, dst.w, dst.h);
    SDL_BlitSurface(icon, nullptr, c.surface(), &dst);
    iy += ih + 8;
  } else {
    // placeholder glyph
    c.rect(x + w / 2 - 24, iy + 12, 48, 48, kPanelAlt);
    c.rectOutline(x + w / 2 - 24, iy + 12, 48, 48, kBorderDark, 2);
    iy += 68;
  }
  c.textCenterClipped(x + w / 2, iy, w - 16, label.c_str(), kText,
                      kFontTiny);
}

void optionRow(Canvas& c, int x, int y, int w, const std::string& label,
               const std::string& value, bool selected, uint32_t accent) {
  if (selected) {
    c.rect(x, y, w, 52, kWhite);
    c.rectOutline(x, y, w, 52, accent, 3);
  }
  c.textClipped(x + 14, y + 8, w / 2, label.c_str(), kText, kFontSmall);
  c.textRight(x + w - 14, y + 8, value.c_str(),
              selected ? accent : kTextDim, kFontSmall);
}

void colorGrid(Canvas& c, int x, int y, int cell, int gap,
               const std::vector<uint32_t>& colors, int selected) {
  int cols = 6;
  for (size_t i = 0; i < colors.size(); ++i) {
    int cx = x + (int)(i % cols) * (cell + gap);
    int cy = y + (int)(i / cols) * (cell + gap);
    c.rect(cx, cy, cell, cell, colors[i]);
    c.rectOutline(cx, cy, cell, cell,
                  (int)i == selected ? kDialogBorder : kBorder,
                  (int)i == selected ? 4 : 2);
  }
}

}  // namespace ui
}  // namespace ndsui
