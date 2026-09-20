#include "ui/Theme.h"

namespace ndsui {

namespace {
bool gDark = false;

struct Palette {
  uint32_t bg, bgDot, panel, panelAlt, cardFill, border, borderDark;
  uint32_t text, textDim, textFaint;
  uint32_t paper, gridFar, gridLine, gridLineMajor, gridFine;
  uint32_t pillDark, pillText, panelInk, selInk, gray, grayDark;
  uint32_t rowSel;
};

const Palette kLight = {
    0xE7E7E7, 0xDCDCDC, 0xF0F0F0, 0xDCDCDC, 0xFFFFFF, 0xB5B5B5, 0x7B7B7B,
    0x000000, 0x6B6B6B, 0xB5B5B5,
    0xF4F4F1, 0xE4E4E0, 0xB8BEC8, 0x8E96A4, 0xEBEDF0,
    0x2E2E33, 0xFFFFFF, 0x2B2B30, 0xE8E8F0, 0xC6C6C6, 0x7B7B7B,
    0xE4F0FF,
};

const Palette kDark = {
    0x17191F, 0x1D2027, 0x24272E, 0x2E323A, 0x2A2E36, 0x3A404C, 0x4A5060,
    0xF2F3F7, 0xA8ACB8, 0x6C7280,
    // notebook grid kept very close to the paper: the old 0x3A414F/0x5A6270
    // lines read as bright white stripes on the dark theme
    0x1B1E24, 0x21242B, 0x282C35, 0x333845, 0x21242B,
    0x30343C, 0xF2F3F7, 0xE9EAF0, 0xE8E8F0, 0x8A8F9A, 0x5A6068,
    0x2C3A4E,
};

void assign(const Palette& p) {
  kBg = p.bg;
  kBgDot = p.bgDot;
  kPanel = p.panel;
  kPanelAlt = p.panelAlt;
  kCardFill = p.cardFill;
  kBorder = p.border;
  kBorderDark = p.borderDark;
  kText = p.text;
  kTextDim = p.textDim;
  kTextFaint = p.textFaint;
  kPaper = p.paper;
  kGridFar = p.gridFar;
  kGridLine = p.gridLine;
  kGridLineMajor = p.gridLineMajor;
  kGridFine = p.gridFine;
  kPillDark = p.pillDark;
  kPillText = p.pillText;
  kPanelInk = p.panelInk;
  kSelInk = p.selInk;
  kGray = p.gray;
  kGrayDark = p.grayDark;
  kRowSel = p.rowSel;
}
}  // namespace

void applyTheme(bool dark) {
  gDark = dark;
  assign(dark ? kDark : kLight);
}

bool themeIsDark() { return gDark; }

}  // namespace ndsui
