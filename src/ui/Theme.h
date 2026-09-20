// DS design system palette (sampled from the community design system) plus
// runtime theme state.
//
// The theme-affected colours are *variables* (not constants) so the Light/Dark
// switch can retint every drawing call at runtime without touching the call
// sites: applyTheme() just reassigns them.
#pragma once

#include <cstdint>

namespace ndsui {

// ---- theme-affected palette (initialised to the Light theme) ----
inline uint32_t kBg          = 0xE7E7E7;  // dotted content background
inline uint32_t kBgDot       = 0xDCDCDC;  // dot grid
inline uint32_t kPanel       = 0xF0F0F0;  // light panel fill
inline uint32_t kPanelAlt    = 0xDCDCDC;  // pressed/secondary fill
inline uint32_t kWhite       = 0xFFFFFF;  // pure white (text on dark, icons)
inline uint32_t kCardFill    = 0xFFFFFF;  // squircle cards / panels / tiles
inline uint32_t kBorder      = 0xB5B5B5;  // light 1px border
inline uint32_t kBorderDark  = 0x7B7B7B;  // dark 1px border
inline uint32_t kText        = 0x000000;
inline uint32_t kTextDim     = 0x6B6B6B;
inline uint32_t kTextFaint   = 0xB5B5B5;
inline uint32_t kChrome      = 0x41CBFB;  // status bar blue
inline uint32_t kChromeTop   = 0x61D3FB;  // status bar top highlight
inline uint32_t kChromeDark  = 0x30BAF3;  // headers, accents
inline uint32_t kDescBar     = 0x292929;  // description strip
inline uint32_t kActionBar   = 0xF0F0F0;
inline uint32_t kGray        = 0xC6C6C6;
inline uint32_t kGrayDark    = 0x7B7B7B;
inline uint32_t kDialogBorder = 0xF08000;  // dialog orange frame

// --- new shell (math notebook + pills) ---
inline uint32_t kPaper        = 0xF4F4F1;  // notebook paper base
inline uint32_t kGridFar      = 0xE4E4E0;  // distant grid
inline uint32_t kGridLine     = 0xD7DCE2;  // main notebook grid (bluish)
inline uint32_t kGridLineMajor= 0xC3CBD6;  // every 4th line
inline uint32_t kGridFine     = 0xEBEDF0;  // fine grid layer
inline uint32_t kPillDark     = 0x2E2E33;  // switch-style pill bg
inline uint32_t kPillLight    = 0xFFFFFF;  // light pill
inline uint32_t kPillText     = 0xFFFFFF;
inline uint32_t kPanelInk     = 0x2B2B30;  // text on paper
inline uint32_t kSelInk       = 0xE8E8F0;  // selected-on-dark text
inline uint32_t kFocusBlue    = 0x4DA6FF;  // focus outline (light blue)
inline uint32_t kRowSel       = 0xE4F0FF;  // selected list row
inline uint32_t kHeartRed     = 0xF0455A;  // favourite heart
// ink for light glyph circles (needs to stay dark in both themes)
constexpr uint32_t kGlyphInk  = 0x2B2B30;

// Light / Dark switch: retints the variables above. `dark == false` restores
// the light DS notebook look.
void applyTheme(bool dark);
bool themeIsDark();

// Font sizes (UI font = Fredoka, smooth/rounded)
constexpr int kFontTiny   = 20;
constexpr int kFontSmall  = 28;
constexpr int kFontMedium = 38;
constexpr int kFontLarge  = 54;

struct Theme {
  uint32_t accent = kChromeDark;  // per-system accent (chrome tint)
  bool rounded = true;            // (future) rounded corners toggle
};

}  // namespace ndsui
