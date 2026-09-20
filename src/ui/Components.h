// Shared DS-styled UI components used by every screen.
#pragma once

#include <string>
#include <vector>

#include "ui/Canvas.h"

namespace ndsui {
namespace ui {

// Layout constants (single 1024x768 DS-like layout)
constexpr int kStatusH = 56;
constexpr int kDescH = 60;
constexpr int kActionH = 68;
constexpr int kDescY = Canvas::H - kActionH - kDescH;
constexpr int kPanelY = kStatusH + 12;
constexpr int kPanelH = kDescY - kPanelY - 12;

struct Hint {
  std::string key;    // "A", "B", "L/R"...
  std::string label;  // "Launch"
};

void statusBar(Canvas& c, const std::string& left, const std::string& center,
               const std::string& time, const std::string& battery,
               uint32_t accent);
void actionBar(Canvas& c, const std::vector<Hint>& hints);
void descBar(Canvas& c, const std::string& text);
void panel(Canvas& c, int x, int y, int w, int h);

// vertical list of rows inside (x,y,w,h); returns the number of visible rows
int listRows(Canvas& c, int x, int y, int w, int h,
             const std::vector<std::string>& rows, int selected, int scroll,
             int rowH = 56);
void scrollbar(Canvas& c, int x, int y, int h, int total, int visible,
               int scroll);

// DS dialog: dark box, orange border, centered lines
void dialog(Canvas& c, const std::string& title,
            const std::vector<std::string>& lines, const std::string& hint);

// --- button glyphs (Switch-style): circle for A/B/X/Y, rounded square for
// --- L/R/L2/R2, plus a d-pad cross. Used in pills and the help row.
void buttonGlyph(Canvas& c, int cx, int cy, int d, const char* label,
                 bool circle, uint32_t bg, uint32_t fg);
void dpadGlyph(Canvas& c, int cx, int cy, int d, uint32_t col);

// tile grid cell (icon + label), selected = white + accent border
void tile(Canvas& c, int x, int y, int w, int h, SDL_Surface* icon,
          const std::string& label, bool selected, uint32_t accent);

// settings row: label left, value right, Left/Right adjustable
void optionRow(Canvas& c, int x, int y, int w, const std::string& label,
               const std::string& value, bool selected, uint32_t accent);

// color swatch grid
void colorGrid(Canvas& c, int x, int y, int cell, int gap,
               const std::vector<uint32_t>& colors, int selected);

}  // namespace ui
}  // namespace ndsui
