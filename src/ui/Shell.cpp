#include "ui/Shell.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>

#include "core/Device.h"
#include <dirent.h>

#include "core/Json.h"
#include "core/Net.h"
#include "ui/Components.h"
#include "ui/Theme.h"
#include "util/Log.h"
#include "util/Platform.h"

namespace ndsui {

namespace {
constexpr int kTopBarH = 56;
constexpr int kHelpH = 44;
constexpr int kHelpY = Canvas::H - kHelpH - 10;  // bottom help row
constexpr int kStripH = 88;
constexpr int kLabelH = 40;
constexpr int kContentY = kTopBarH + 8;
constexpr int kContentH = Canvas::H - 48 - kStripH - kLabelH - kContentY;
constexpr int kLabelY = kContentY + kContentH + 2;
constexpr int kStripY = kLabelY + kLabelH;
// games context: 50% taller console chooser at its usual place; the models
// pane (models + name pill) sits higher so the chooser gets more air.
constexpr int kStripBigH = 132;
constexpr int kStripBigY = kHelpY - 38 - kStripBigH;
constexpr int kLabelBigH = 56;
constexpr int kLabelBigY = kStripBigY - 92;
// topmost row the moving widgets can touch (name pill floats above the strip)
constexpr int kLabelBandTop = kLabelY - 46;

constexpr int kGridCellW = 210;
constexpr int kGridCellH = 170;
constexpr int kGridGapX = 24;
constexpr int kGridGapY = 18;

// section definitions (order shown on the main strip)
struct SectionDef {
  const char* label;
  uint32_t tint;
};
const SectionDef kSections[] = {
    {"Home", 0x41CBFB}, {"Apps", 0x50B060}, {"Games", 0xE05050},
    {"Settings", 0x8A7BE0},
};

// Settings is now only Theme / Homepage / System (single grid row)
constexpr int kSettingsCategoryCount = 3;

std::string humanSize(long bytes) {
  if (bytes < 1024) return fmt("%ld B", bytes);
  if (bytes < 1024 * 1024) return fmt("%.1f KB", bytes / 1024.0);
  return fmt("%.1f MB", bytes / (1024.0 * 1024.0));
}
}  // namespace

Shell::Shell(AppContext& ctx, Canvas& canvas)
    : m_ctx(ctx), m_canvas(canvas), m_carousel(canvas, ctx) {
  srand((unsigned)time(nullptr));  // random Games-root samples
  applyTheme(Settings::instance().getBoolDefault("theme_dark", false));
  // NDSUI no longer manages panel/LED/sound/radio settings: the stock OSD and
  // MainUI own them. Code kept for reference only.
  // DISABLED: net::displayApplyFromSettings();
  m_bg.init();
  // the systems strip is expensive (an icon + model mapping per console) and
  // is only visible inside the Games context: build it when entering there
  // instead of at startup (that build was most of the boot delay)
  // buildSystemStrip();
  buildCategories();
  buildSections();
  refreshApps();
  // DISABLED: applyLedFromSettings();
  // DISABLED: net::async(...) setVolumePercent + fetchHardwareVolume
  m_lastInputAt = SDL_GetTicks();
  m_strip.setSelection((int)m_mode, false);
  refreshShowcase();
  m_home.tick();
  syncDefaultLauncherMarker();  // keep the boot hook in sync with the setting
  // first frame is complete: bar + carousel used to appear only after the
  // first navigation because the dirty flags started cold
  m_topDirty = m_contentDirty = m_stripDirty = true;
  m_ctx.dirty = true;
}

uint32_t Shell::accent() const {
  Settings& st = Settings::instance();
  if (!st.accent.empty())
    return (uint32_t)strtoul(st.accent.c_str(), nullptr, 16) & 0xFFFFFF;
  return kChromeDark;
}

int Shell::labelTop() const { return bigGames() ? kLabelBigY : kLabelY; }
int Shell::labelHeight() const { return bigGames() ? kLabelBigH : kLabelH; }
int Shell::contentBottom() const {
  // the root name pill floats at labelTop()-34; keep the models above it
  return bigGames() ? labelTop() - 8 : labelTop() - 46;
}
int Shell::stripTop() const { return bigGames() ? kStripBigY : kStripY; }
int Shell::stripHeight() const { return bigGames() ? kStripBigH : kStripH; }

void Shell::buildSections() {
  // real stock-skin icons (shipped in assets/skin, trimmed like the others)
  static const char* kSectionIcons[] = {"ic-homepage.png", "", "ic-game.png",
                                        "ic-system.png"};
  std::vector<ui::StripItem> items;
  for (size_t i = 0; i < sizeof(kSections) / sizeof(kSections[0]); ++i) {
    const SectionDef& s = kSections[i];
    ui::StripItem it;
    it.label = s.label;
    it.tint = s.tint;
    if (!kSectionIcons[i][0]) {
      // Apps: 8x8 app-drawer style grid, tinted like the other icons
      it.icon = glyphSurface("grid8", 76,
                             themeIsDark() ? 0xE8EAF0 : 0x2B2B30);
    } else {
      std::string icon =
          joinPath(joinPath(assetDir(), "skin"), kSectionIcons[i]);
      it.icon = themedIcon(icon, 76);
    }
    items.push_back(it);
  }
  m_strip.setItems(std::move(items));
  m_strip.setSelection((int)m_mode, false);
}

std::vector<ui::StripItem> Shell::systemItems() const {
  std::vector<ui::StripItem> items;
  // the games chooser shows large icons: trim the transparent padding of the
  // ic-<id>.png art so it spans almost the whole card
  const int iconSize = bigGames() ? 104 : 72;
  for (const System& sys : m_ctx.systems) {
    if (sys.gameCount == 0) continue;
    ui::StripItem it;
    it.label = sys.label;
    // PSP reads "PSP" in the UI (the folder/id stays PPSSPP)
    if (upper(sys.id) == "PPSSPP") it.label = "PSP";
    it.tint = sys.accent();
    // real system icon: Emus/_theme/ic-<id>.png, else the folder's own
    // icon.png (PORTS and JAVA have no _theme icon)
    std::string icon = joinPath(
        joinPath(emusDir(), "_theme"), "ic-" + lower(sys.id) + ".png");
    SDL_Surface* isurf = m_ctx.images.getTrimmed(icon, iconSize);
    if (!isurf) {
      std::string local = joinPath(joinPath(emusDir(), sys.id), "icon.png");
      isurf = m_ctx.images.getTrimmed(local, iconSize);
    }
    it.icon = isurf;
    items.push_back(it);
  }
  return items;
}

void Shell::buildSystemStrip() {
  std::vector<ui::StripItem> items;
  m_hasRecent = m_hasFav = false;
  const int iconPx = bigGames() ? 104 : 72;
  // Recent (clock) then Favorites (heart) - only when they have real games
  {
    int n = 0;
    for (const Recent& r : Settings::instance().recents)
      if (gameByPath(r.path)) ++n;
    if (n > 0) {
      ui::StripItem it;
      it.label = "Recent";
      it.icon = glyphSurface("clock", iconPx, kPanelInk);
      items.push_back(it);
      m_hasRecent = true;
    }
  }
  {
    int n = 0;
    for (const std::string& p : Settings::instance().favorites)
      if (gameByPath(p)) ++n;
    if (n > 0) {
      ui::StripItem it;
      it.label = "Favorites";
      it.icon = glyphSurface("heart", iconPx, kHeartRed);
      items.push_back(it);
      m_hasFav = true;
    }
  }
  m_pseudoCount = (int)items.size();
  std::vector<ui::StripItem> systems = systemItems();
  for (ui::StripItem& it : systems) items.push_back(std::move(it));
  if (items.empty()) {
    ui::StripItem it;
    it.label = "No systems";
    it.tint = kGrayDark;
    items.push_back(it);
  }
  m_strip.setItems(std::move(items));
  m_strip.setSelection(0, false);
}

std::vector<ui::StripItem> Shell::categoryItems() const {
  static const char* cats[] = {"Theme", "Homepage", "System"};
  static const uint32_t tints[] = {0x8A7BE0, 0x50C0E0, 0x909090};
  static const char* icons[] = {"ic-theme.png", "ic-homepage.png",
                                "ic-system.png"};
  std::vector<ui::StripItem> items;
  const int sz = 76;
  for (size_t i = 0; i < sizeof(cats) / sizeof(cats[0]); ++i) {
    ui::StripItem it;
    it.label = cats[i];
    it.tint = tints[i];
    if (std::string(icons[i]) == "bulb") {
      it.icon = glyphSurface("bulb", sz, tints[i]);
    } else {
      it.icon = themedIcon(
          joinPath(joinPath(assetDir(), "skin"), icons[i]), sz);
    }
    items.push_back(it);
  }
  return items;
}

void Shell::buildCategories() {
  std::vector<ui::StripItem> items = categoryItems();
  m_strip.setItems(std::move(items));
  // clamp: a stale selection (e.g. Settings = 3) on a shorter category strip
  // used to read past the end and crash
  int sel = m_catSel;
  if (sel >= m_strip.count()) sel = std::max(0, m_strip.count() - 1);
  if (sel < 0) sel = 0;
  m_catSel = sel;
  m_strip.setSelection(sel, false);
}

void Shell::buildAppsGrid() { refreshApps(); }

void Shell::syncDefaultLauncherMarker() {
  // The stock boot script (/usr/trimui/bin/runtrimui.sh) starts MainUI in a
  // loop; the NDSUI hook there checks this marker and runs our launch.sh
  // instead. Marker present = NDSUI is the system launcher.
  std::string marker = joinPath(assetDir(), "use_as_launcher");
  bool ndsui = Settings::instance().getIntDefault("default_launcher", 1) == 0;
  if (ndsui) {
    FILE* f = fopen(marker.c_str(), "w");
    if (f) {
      fputs("NDSUI is the default launcher\n", f);
      fclose(f);
    }
  } else {
    remove(marker.c_str());
  }
}

void Shell::refreshApps() {
  // app roots: internal storage + SD card (stock shows both); each app folder
  // has config.json (label/icontop/launch) and a launch.sh, and the root's
  // show.json can hide entries
  m_apps.clear();
  std::vector<std::string> roots = {"/usr/trimui/apps", appsDir()};
  for (const std::string& root : roots) {
    std::map<std::string, bool> visible;
    if (JsonPtr show = jsonParseFile(joinPath(root, "show.json"));
        show && show->isArray()) {
      for (const JsonPtr& it : show->arr) {
        if (!it || !it->isObject()) continue;
        bool vis = true;
        if (const JsonValue* sv = it->get("show"))
          vis = sv->number != 0;
        visible[it->getString("label")] = vis;
      }
    }
    DIR* d = opendir(root.c_str());
    if (!d) continue;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
      if (e->d_name[0] == '.') continue;
      std::string dir = joinPath(root, e->d_name);
      if (!dirExists(dir)) continue;
      JsonPtr cfg = jsonParseFile(joinPath(dir, "config.json"));
      if (!cfg || !cfg->isObject()) continue;
      AppEntry a;
      a.dir = dir;
      a.label = cfg->getString("label", e->d_name);
      // never list the launcher itself: launching NDSUI from inside NDSUI
      // would start a second instance over the one that is running
      if (a.label == "NDSUI" || e->d_name == std::string("NDSUI")) continue;
      auto vis = visible.find(a.label);
      if (vis != visible.end() && !vis->second) continue;  // hidden by stock
      a.launch = joinPath(dir, cfg->getString("launch", "launch.sh"));
      if (!fileExists(a.launch)) continue;
      std::string top = cfg->getString("icontop");
      if (!top.empty()) {
        std::string p = top[0] == '/' ? top : joinPath(dir, top);
        if (fileExists(p)) a.iconPath = p;
      }
      if (a.iconPath.empty()) {
        std::string p = joinPath(dir, "icon.png");
        if (fileExists(p)) a.iconPath = p;
      }
      if (a.iconPath.empty()) {
        // some stock apps only ship a differently named png (the File Manager
        // has commander.png): prefer a name match, else the first png
        std::string want = lower(e->d_name);
        std::string best, first;
        DIR* id = opendir(dir.c_str());
        if (id) {
          struct dirent* ie;
          while ((ie = readdir(id)) != nullptr) {
            std::string n = ie->d_name;
            if (n.size() < 5 || lower(n).substr(n.size() - 4) != ".png")
              continue;
            if (first.empty()) first = joinPath(dir, n);
            std::string ln = lower(n);
            if (ln.find(want) != std::string::npos ||
                ln.find("icon") != std::string::npos) {
              best = joinPath(dir, n);
              break;
            }
          }
          closedir(id);
        }
        a.iconPath = !best.empty() ? best : first;
      }
      m_apps.push_back(std::move(a));
    }
    closedir(d);
  }
  std::sort(m_apps.begin(), m_apps.end(), [](const AppEntry& a,
                                              const AppEntry& b) {
    return lower(a.label) < lower(b.label);  // alphabetical, case-insensitive
  });
  m_appItems.clear();
  for (const AppEntry& a : m_apps) {
    ui::StripItem it;
    it.label = a.label;
    it.tint = kChromeDark;
    if (!a.iconPath.empty())
      it.icon = m_ctx.images.getTrimmed(a.iconPath, 116);
    m_appItems.push_back(std::move(it));
  }
  if (m_appSel >= (int)m_apps.size()) m_appSel = std::max(0, (int)m_apps.size() - 1);
  LOG_INFO("apps: %zu", m_apps.size());
}

void Shell::drawHintPill(Canvas& c, int x, int y, const char* glyph,
                         bool circle, const char* label, uint32_t fill) {
  const int gd = 26, h = 44;
  int tw = c.textWidth(label, kFontSmall);
  int w = 12 + gd + 14 + 6 + 14 + tw + 14;
  c.pill(x, y, w, h, fill);
  int cx = x + 12 + gd / 2;
  ui::buttonGlyph(c, cx, y + h / 2, gd, glyph, circle,
                  circle ? 0xF0F0F0 : 0x45454E,
                  circle ? kGlyphInk : kPillText);
  cx += gd / 2 + 14;
  c.textVC(cx, y + h / 2, ":", kPillText, kFontSmall);
  cx += 8 + 6;
  c.textVC(cx, y + h / 2, label, kPillText, kFontSmall);
}

void Shell::drawAppsView(Canvas& c) {
  // hint: X refreshes the app list
  drawHintPill(c, 30, kContentY + 4, "X", true, "Refresh Apps", kPillDark);
  if (m_apps.empty()) {
    c.textCenter(Canvas::W / 2, kContentY + 140, "No apps found", kTextFaint,
                 kFontMedium);
    return;
  }
  const int cols = 4, rows = 2, perPage = cols * rows;
  int pages = ((int)m_apps.size() + perPage - 1) / perPage;
  if (m_appPage >= pages) m_appPage = pages - 1;
  if (m_appPage < 0) m_appPage = 0;
  const int cellW = 210, cellH = 168, gapX = 26, gapY = 20;
  int gridW = cols * cellW + (cols - 1) * gapX;
  int x0 = (Canvas::W - gridW) / 2;
  int y0 = kContentY + 62;
  for (int i = 0; i < perPage; ++i) {
    int idx = m_appPage * perPage + i;
    if (idx >= (int)m_apps.size()) break;
    int col = i % cols, row = i / cols;
    int cx = x0 + col * (cellW + gapX);
    int cy = y0 + row * (cellH + gapY);
    bool sel = idx == m_appSel;
    c.roundRect(cx, cy, cellW, cellH, 18, kCardFill);
    c.roundRectOutline(cx, cy, cellW, cellH, 18,
                       sel && m_contentFocus ? kFocusBlue : kGridLineMajor,
                       sel && m_contentFocus ? 4 : 2);
    if (m_appItems[idx].icon) {
      int iw = m_appItems[idx].icon->w, ih = m_appItems[idx].icon->h;
      int maxW = cellW - 36, maxH = cellH - 62;
      float k = std::min((float)maxW / iw, (float)maxH / ih);
      int dw = (int)(iw * k), dh = (int)(ih * k);
      SDL_Rect dst{cx + (cellW - dw) / 2, cy + 18, dw, dh};
      c.markRegionDirty(dst.x, dst.y, dst.w, dst.h);
      SDL_BlitScaled(m_appItems[idx].icon, nullptr, c.surface(), &dst);
    } else {
      c.roundRect(cx + cellW / 2 - 40, cy + 24, 80, 80, 18, kChromeDark);
    }
    c.textCenterClipped(cx + cellW / 2, cy + cellH - 40, cellW - 20,
                        m_apps[idx].label.c_str(), kPanelInk, kFontSmall);
  }
  // pagination indicator (bottom centre, above the strip)
  if (pages > 1) {
    std::string pg = fmt("%d / %d", m_appPage + 1, pages);
    int pw = c.textWidth(pg.c_str(), kFontTiny) + 40;
    c.pill((Canvas::W - pw) / 2, y0 + rows * (cellH + gapY) + 4, pw, 32,
           kPillDark);
    c.textCenterVC(Canvas::W / 2, y0 + rows * (cellH + gapY) + 20,
                   pg.c_str(), kPillText, kFontTiny);
  }
}

const System* Shell::selectedSystem() const {
  int idx = 0;
  const int want = m_strip.selection() - m_pseudoCount;
  if (want < 0) return nullptr;  // Recent / Favorites pseudo entries
  for (const System& s : m_ctx.systems) {
    if (s.gameCount == 0) continue;
    if (idx == want) return &s;
    ++idx;
  }
  return nullptr;
}

void Shell::invalidateAll() {
  // The canvas was destroyed and re-created (we just returned from a game):
  // the new surface holds its dark default fill and the damage tracking is
  // cold, so mark every region for a full repaint. Recents may have changed
  // (a game was just played), so rebuild the chooser as well.
  const std::string keep =
      m_strip.count() > 0 ? m_strip.items()[m_strip.selection()].label : "";
  // rebuild the strip that belongs to the CURRENT mode (a resume used to
  // leave the systems strip in Apps/Home, and the games list behind)
  if (m_level == Level::Context && m_mode == Mode::Games) {
    buildSystemStrip();
    for (int i = 0; i < m_strip.count(); ++i)
      if (m_strip.items()[i].label == keep) {
        m_strip.setSelection(i, false);
        break;
      }
    refreshSystemGames();
  } else if (m_level == Level::Context && m_mode == Mode::Settings) {
    buildCategories();
  } else {
    buildSections();
    m_strip.setSelection((int)m_mode, false);
  }
  m_carousel.clearCaches();  // derived surfaces point at freed art
  // DISABLED (NDSUI must not touch the panel/LEDs): apps/games reset them and
  // the stock OSD/MainUI own them now.
  // net::displayApplyFromSettings();
  // if (Settings::instance().getBoolDefault("led_configured", false))
  //   applyLedFromSettings();
  m_lastBgX = -100000;
  m_lastBgY = -100000;
  m_topDirty = true;
  m_contentDirty = true;
  m_stripDirty = true;
  m_pageClear = false;
  m_cartBgValid = false;
  m_status.clear();
  m_carousel.invalidate();
  m_canvas.markUiDirty();
}

void Shell::refreshSystemGames() {
  m_systemGames.clear();
  const int sel = m_strip.selection();
  // Recent / Favorites pseudo consoles
  if (pseudoEntry(sel, &m_systemGames)) {
    m_carousel.setGames(m_systemGames);
    if (!m_systemGames.empty())
      m_carousel.setSystem(m_ctx.systems[m_systemGames[0]->systemIndex]);
    m_carousel.setSelection(0);
    return;
  }
  const System* sys = selectedSystem();
  if (!sys) return;
  for (const Game& g : m_ctx.games)
    if (&m_ctx.systems[g.systemIndex] == sys) m_systemGames.push_back(&g);
  m_carousel.setSmall(false);
  m_carousel.setSystem(*sys);
  m_carousel.setGames(m_systemGames);
  m_carousel.setSelection(0);
}

void Shell::syncModeFromStrip() {
  if (m_level != Level::Sections) return;
  int sel = m_strip.selection();
  if (sel < 0 || sel > 3) return;
  // switch the page right away (waiting for the scroll to settle made the
  // content appear "late"); the strip animation is independent
  m_pendingMode = sel;
  applyPendingMode();
}

void Shell::refreshShowcase() {
  // The root Games page shows three RANDOM games from the whole library with
  // the middle one focused (one model left, one right). Re-sampled every time
  // the user lands on the Games section.
  m_showcase.clear();
  if (m_ctx.games.empty()) return;
  std::vector<const Game*> pool;
  pool.reserve(m_ctx.games.size());
  for (const Game& g : m_ctx.games) pool.push_back(&g);
  for (int i = 0; i < 3 && !pool.empty(); ++i) {
    int idx = rand() % (int)pool.size();
    m_showcase.push_back(pool[idx]);
    pool.erase(pool.begin() + idx);
  }
  if (m_level == Level::Sections) {
    m_carousel.setSmall(true);
    m_carousel.setGames(m_showcase);
    // focused = the middle sample, so neighbours appear left and right
    m_carousel.setSelection((int)m_showcase.size() / 2);
  }
}

const Game* Shell::gameByPath(const std::string& path) const {
  for (const Game& g : m_ctx.games)
    if (g.path == path) return &g;
  return nullptr;
}

bool Shell::pseudoEntry(int idx, std::vector<const Game*>* out) const {
  if (idx < 0 || idx >= m_pseudoCount) return false;
  const Settings& st = Settings::instance();
  int slot = 0;
  if (m_hasRecent) {
    if (idx == slot) {
      for (const Recent& r : st.recents) {
        const Game* g = gameByPath(r.path);
        if (g) out->push_back(g);
      }
      return true;
    }
    ++slot;
  }
  if (m_hasFav && idx == slot) {
    for (const std::string& p : st.favorites) {
      const Game* g = gameByPath(p);
      if (g) out->push_back(g);
    }
    return true;
  }
  return false;
}

SDL_Surface* Shell::themedIcon(const std::string& path, int size) const {
  // user preference: stock outline icons are INVERTED in light mode and
  // keep their original inks in dark mode
  const bool invert = !themeIsDark();
  const bool dark = invert;
  if (dark != m_themedDark) {  // theme switched: drop the tinted copies
    for (auto& kv : m_themedIcons)
      if (kv.second) SDL_FreeSurface(kv.second);
    m_themedIcons.clear();
    m_themedDark = dark;
  }
  std::string key = path + "@" + std::to_string(size) + (dark ? "d" : "l");
  auto it = m_themedIcons.find(key);
  if (it != m_themedIcons.end()) return it->second;
  SDL_Surface* src = m_ctx.images.getTrimmed(path, size);
  if (!src) {
    m_themedIcons[key] = nullptr;
    return nullptr;
  }
  SDL_Surface* out = nullptr;
  if (!dark) {
    out = src;  // light theme: the stock outlines are already correct
  } else {
    out = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_ARGB8888, 0);
    if (out) {
      for (int y = 0; y < out->h; ++y) {
        uint32_t* row =
            (uint32_t*)((uint8_t*)out->pixels + (size_t)y * out->pitch);
        for (int x = 0; x < out->w; ++x) {
          uint32_t p = row[x];
          uint32_t a = p >> 24;
          if (!a) continue;
          uint32_t r = 255 - ((p >> 16) & 0xFF);
          uint32_t g = 255 - ((p >> 8) & 0xFF);
          uint32_t b = 255 - (p & 0xFF);
          row[x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
      }
    } else {
      out = src;
    }
  }
  m_themedIcons[key] = out;
  return out;
}

SDL_Surface* Shell::glyphSurface(const char* kind, int size,
                                  uint32_t col) const {
  std::string key = fmt("%s@%d@%X", kind, size, col);
  auto it = m_glyphs.find(key);
  if (it != m_glyphs.end()) return it->second;
  SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32,
                                                  SDL_PIXELFORMAT_ARGB8888);
  if (!s) return nullptr;
  SDL_FillRect(s, nullptr, 0);
  uint32_t* px = (uint32_t*)s->pixels;
  const int pitch = s->pitch / 4;
  const float c = size * 0.5f;
  const bool heart = strcmp(kind, "heart") == 0;
  const bool bulb = strcmp(kind, "bulb") == 0;
  const float R = size * (heart ? 0.34f : (bulb ? 0.30f : 0.36f));
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      float dx = x + 0.5f - c, dy = y + 0.5f - c;
      bool on = false;
      const bool grid8 = strcmp(kind, "grid8") == 0;
      if (grid8) {
        // 8x8 app-drawer grid: 64 small rounded squares
        const int n2 = 8;
        float cell = size * 0.90f / n2;
        float x0 = (size - cell * n2) * 0.5f;
        int cx8 = (int)((x + 0.5f - x0) / cell);
        int cy8 = (int)((y + 0.5f - x0) / cell);
        if (cx8 >= 0 && cx8 < n2 && cy8 >= 0 && cy8 < n2) {
          float lx = x0 + cx8 * cell, ly8 = x0 + cy8 * cell;
          float inset = cell * 0.16f;
          if (x + 0.5f >= lx + inset && x + 0.5f <= lx + cell - inset &&
              y + 0.5f >= ly8 + inset && y + 0.5f <= ly8 + cell - inset)
            on = true;
        }
      } else if (bulb) {
        // bulb: circle + two base bars
        float d = std::sqrt(dx * dx + (dy + size * 0.08f) *
                                         (dy + size * 0.08f));
        on = d <= R;
        if (!on && std::fabs(dx) < size * 0.14f && dy > R - size * 0.06f &&
            dy < R + size * 0.10f)
          on = true;
        if (!on && std::fabs(dx) < size * 0.16f && dy > R + size * 0.12f &&
            dy < R + size * 0.20f)
          on = true;
      } else if (heart) {
        // classic implicit heart, y flipped for screen space (tip at bottom)
        float u = dx / R, v = -dy / R;
        float t = u * u + v * v - 1.f;
        on = (t * t * t - u * u * v * v * v) <= 0.f;
      } else {
        // clock: ring + hands
        float d = std::sqrt(dx * dx + dy * dy);
        on = d <= R && d >= R - size * 0.10f;
        if (!on) {
          float ax = std::fabs(dx), ay = std::fabs(dy);
          on = (ay < size * 0.035f && dx > -size * 0.02f && ax < R * 0.55f) ||
               (ax < size * 0.035f && dy > -size * 0.02f && ay < R * 0.75f);
        }
      }
      if (on) px[y * pitch + x] = 0xFF000000u | col;
    }
  }
  m_glyphs[key] = s;
  return s;
}

// ---------------------------------------------------------------- modals

void Shell::openChooserModal() {
  m_modal = Modal::Chooser;
  m_modalSel = 0;
  m_modalTitle = "Options";
  m_modalItems.clear();
  const Game* g = focusedGame();
  bool fav = g && Settings::instance().isFavorite(g->path);
  m_modalItems.push_back({"Refresh Games", "", -1});
  m_modalItems.push_back({"Search Games", "", -1});
  m_modalItems.push_back({fav ? "Unfavorite" : "Favorite", "", -1});
  m_ctx.dirty = true;
}

void Shell::openGameModal() {
  const Game* g = focusedGame();
  if (!g) return;
  const System& sys = m_ctx.systems[g->systemIndex];
  m_modal = Modal::Game;
  m_modalSel = 0;
  m_modalTitle = sys.label + " - Emulator";
  m_modalItems.clear();
  // the system's default script (config.json "launch") comes first, labelled
  // with the launchlist's emulator name when it has one
  std::string defName = "Default";
  for (const LaunchOption& lo : sys.launches)
    if (lo.script == sys.defaultScript) {
      defName = lo.name;
      break;
    }
  m_modalItems.push_back({defName, sys.defaultScript, -2});
  for (size_t i = 0; i < sys.launches.size(); ++i) {
    if (sys.launches[i].script == sys.defaultScript) continue;
    m_modalItems.push_back({sys.launches[i].name, sys.launches[i].script, (int)i});
  }
  // mark the active choice
  int cur = Settings::instance().getIntDefault("emu:" + g->path, -2);
  for (ModalItem& it : m_modalItems) it.value += (it.launchIdx == cur) ? " *" : "";
  m_ctx.dirty = true;
}

void Shell::closeModal() {
  m_modal = Modal::None;
  m_modalItems.clear();
  m_contentDirty = true;
  m_stripDirty = true;
  m_topDirty = true;
  // the dim covered the whole screen; force the paper to be repainted so no
  // dark patches are left between the widget regions
  m_lastBgX = -100000;
  m_lastBgY = -100000;
  m_cartBgValid = false;
  m_ctx.dirty = true;
}

void Shell::modalActivate(ScreenResult& r) {
  const Game* g = focusedGame();
  if (m_modal == Modal::Chooser) {
    const int sel = m_modalSel;
    closeModal();
    if (sel == 0) {
      refreshLibrary();
    } else if (sel == 1) {
      openSearch();
    } else if (sel == 2 && g) {
      const std::string keepGame = g->path;
      Settings::instance().toggleFavorite(g->path);
      Settings::instance().save();
      bool nowFav = Settings::instance().isFavorite(g->path);
      m_status = nowFav ? "Added to Favorites" : "Removed from Favorites";
      m_statusUntil = SDL_GetTicks() + 1600;
      const std::string keep =
          m_strip.count() > 0 ? m_strip.items()[m_strip.selection()].label : "";
      buildSystemStrip();
      // keep the console (and the focused game) selected
      for (int i = 0; i < m_strip.count(); ++i)
        if (m_strip.items()[i].label == keep) {
          m_strip.setSelection(i, false);
          break;
        }
      refreshSystemGames();
      for (size_t i = 0; i < m_systemGames.size(); ++i)
        if (m_systemGames[i]->path == keepGame) {
          m_carousel.setSelection((int)i);
          break;
        }
      m_stripDirty = true;
      m_contentDirty = true;
      m_ctx.dirty = true;
    }
    (void)r;
    return;
  }
  if (m_modal == Modal::NetList) {
    listActivate();
    return;
  }
  if (m_modal == Modal::Game && g && m_modalSel < (int)m_modalItems.size()) {
    const ModalItem it = m_modalItems[m_modalSel];
    Settings::instance().setInt("emu:" + g->path, it.launchIdx);
    Settings::instance().save();
    // launch right away with the chosen emulator
    r.kind = ScreenResult::Launch;
    r.script = joinPath(joinPath(emusDir(), m_ctx.systems[g->systemIndex].id),
                        it.value.empty() ? it.label : it.value);
    // strip the " *" marker if present
    size_t star = r.script.rfind(" *");
    if (star != std::string::npos && star == r.script.size() - 2)
      r.script = r.script.substr(0, star);
    r.arg = g->path;
    Settings::instance().addRecent(g->path);
    Settings::instance().save();
    closeModal();
  }
}

bool Shell::handleModal(ActionType a, ScreenResult& r) {
  if (m_modal == Modal::None) return false;
  const int count = m_modal == Modal::NetList ? (int)m_listItems.size()
                                              : (int)m_modalItems.size();
  switch (a) {
    case ActionType::Up:
      if (m_modalSel > 0) --m_modalSel;
      break;
    case ActionType::Down:
      if (m_modalSel + 1 < count) ++m_modalSel;
      break;
    case ActionType::X:
      if (m_modal == Modal::NetList && m_listKind == ListKind::Wifi &&
          m_modalSel < (int)m_listItems.size()) {
        net::wifiForget(m_listItems[m_modalSel].label);
        m_status = "Forgot " + m_listItems[m_modalSel].label;
        m_statusUntil = SDL_GetTicks() + 1600;
        refreshNetList();
        m_ctx.dirty = true;
        return true;
      }
      closeModal();
      return true;
    case ActionType::B:
      closeModal();
      return true;
    case ActionType::A:
      modalActivate(r);
      break;
    default:
      break;
  }
  m_ctx.dirty = true;
  return true;
}

bool Shell::handleSettingsModal(ActionType a, ScreenResult& r) {
  if (m_modal != Modal::Settings) return false;
  // DISABLED: wifi/bluetooth live-list refresh (those categories are gone)
  const int n = (int)m_rows.size();
  switch (a) {
    case ActionType::Up:
      if (m_modalSel > 0) --m_modalSel;
      break;
    case ActionType::Down:
      if (m_modalSel + 1 < n) ++m_modalSel;
      break;
    case ActionType::Left:
      if (m_modalSel < n) adjustRow(m_rows[m_modalSel], -1);
      break;
    case ActionType::Right:
      if (m_modalSel < n) adjustRow(m_rows[m_modalSel], 1);
      break;
    case ActionType::A:
      if (m_modalSel < n) {
        const SettingRow row = m_rows[m_modalSel];
        activateRow(row);
        if (m_kbOpen) return true;  // password prompt took over
        if (m_pendingQuit) {
          m_pendingQuit = false;
          r.kind = ScreenResult::Exit;
          return true;
        }
      }
      break;
    case ActionType::X:
      // DISABLED: wifi "forget saved network" shortcut
      break;
    case ActionType::B:
      closeModal();
      m_status.clear();
      return true;
    default:
      break;
  }
  m_ctx.dirty = true;
  return true;
}

namespace {

// button-glyph hint line, games-style: [A] change  [+] select  [B] close
void glyphHintRow(Canvas& c, int cx, int y,
                  std::initializer_list<std::pair<const char*, const char*>>
                      items) {
  const int gd = 24, gap = 24, tgap = 8;
  int total = 0;
  for (const auto& it : items)
    total += gd + tgap + c.textWidth(it.second, kFontTiny) + gap;
  total -= gap;
  int x = cx - total / 2;
  for (const auto& it : items) {
    if (std::string(it.first) == "dp")
      ui::dpadGlyph(c, x + gd / 2, y, gd, kTextDim);
    else
      ui::buttonGlyph(c, x + gd / 2, y, gd, it.first, true, kWhite, kGlyphInk);
    x += gd + tgap;
    c.textVC(x, y, it.second, kTextDim, kFontTiny);
    x += c.textWidth(it.second, kFontTiny) + gap;
  }
}

}  // namespace

void Shell::drawSettingsModal(Canvas& c) {
  if (m_modal != Modal::Settings) return;
  const int rowH = 62;
  int w = 660;
  for (const SettingRow& row : m_rows) {
    int tw = c.textWidth(row.label.c_str(), kFontSmall) + 240;
    if (tw > w) w = tw;
  }
  if (w > 900) w = 900;
  int h = 108 + (int)m_rows.size() * rowH + 70;
  const int maxH = Canvas::H - 80;
  if (h > maxH) h = maxH;
  int x = (Canvas::W - w) / 2, y = (Canvas::H - h) / 2;
  c.rectAlpha(0, 0, Canvas::W, Canvas::H, 0x101018, 150);
  c.roundRect(x, y, w, h, 26, kCardFill);
  c.roundRectOutline(x, y, w, h, 26, kFocusBlue, 4);
  static const char* cats[] = {"Theme", "Homepage", "System"};
  std::string title =
      m_catSel >= 0 && m_catSel < 3 ? cats[m_catSel] : "Settings";
  c.textCenterVC(x + w / 2, y + 46, title.c_str(), kPanelInk, kFontMedium);

  int listTop = y + 88;
  int listH = h - 88 - 56;
  int visible = listH / rowH;
  int first = 0;
  if (m_modalSel >= visible) first = m_modalSel - visible + 1;
  for (int i = 0; i < visible; ++i) {
    int idx = first + i;
    if (idx >= (int)m_rows.size()) break;
    int ry = listTop + i * rowH;
    bool sel = idx == m_modalSel;
    if (sel) c.roundRect(x + 20, ry, w - 40, rowH - 8, 14, kRowSel);
    int textX = x + 40;
    const SettingRow& row = m_rows[idx];
    if (!row.icon.empty()) {
      SDL_Surface* ic =
          row.icon == "bulb"
              ? glyphSurface("bulb", 30, kTextDim)
              : themedIcon(joinPath(joinPath(assetDir(), "skin"), row.icon),
                           30);
      if (ic) {
        SDL_Rect dst{textX, ry + (rowH - 8) / 2 - ic->h / 2, ic->w, ic->h};
        c.markRegionDirty(dst.x, dst.y, dst.w, dst.h);
        SDL_BlitScaled(ic, nullptr, c.surface(), &dst);
      }
      textX += 42;
    }
    c.textVC(textX, ry + (rowH - 8) / 2, row.label.c_str(), kPanelInk,
             kFontSmall);
    std::string val = rowValue(row);
    if (!val.empty()) {
      uint32_t col = kTextDim;
      if (row.kind == SettingRow::Toggle)
        col = val == "On" ? 0x3E9E5A : kTextDim;
      int vw = c.textWidth(val.c_str(), kFontSmall);
      int vx = x + w - 40 - vw;
      int labelEnd =
          textX + c.textWidth(row.label.c_str(), kFontSmall) + 18;
      if (vx < labelEnd) {  // long read-only values must not hit the label
        c.textClipped(labelEnd, ry + (rowH - 8) / 2, x + w - 40 - labelEnd,
                      val.c_str(), col, kFontSmall);
      } else {
        c.textRightVC(x + w - 40, ry + (rowH - 8) / 2, val.c_str(), col,
                      kFontSmall);
      }
      if (row.kind == SettingRow::Int || row.kind == SettingRow::Choice)
        c.textRightVC(x + w - 40 - c.textWidth(val.c_str(), kFontSmall) - 14,
                      ry + (rowH - 8) / 2, "< >", kTextFaint, kFontTiny);
    } else if (row.key == "hl") {
      ui::buttonGlyph(c, x + w - 56, ry + (rowH - 8) / 2, 24, "A", true,
                      kFocusBlue, kWhite);
    }
  }
  glyphHintRow(c, x + w / 2, y + h - 30,
               {{"A", "change"}, {"dp", "select"}, {"B", "close"}});
}

void Shell::drawModal(Canvas& c) {
  if (m_modal == Modal::None) return;
  if (m_modal == Modal::NetList) {
    // refresh the list contents (wifi results are cheap; bt every few secs)
    static Uint32 lastBt = 0;
    bool scanning = m_listKind == ListKind::Wifi && net::wifiScanning();
    refreshNetList();  // reads the cached lists (cheap)
    if (m_listKind == ListKind::Bt && SDL_GetTicks() - lastBt > 6000) {
      lastBt = SDL_GetTicks();
      if (!net::busy()) net::refreshBtDevices();
    }
    const int rowH = 58;
    int w = 660;
    int h = 110 + (int)m_listItems.size() * rowH + 70;
    if (h > Canvas::H - 120) h = Canvas::H - 120;
    int x = (Canvas::W - w) / 2, y = (Canvas::H - h) / 2;
    c.rectAlpha(0, 0, Canvas::W, Canvas::H, 0x101018, 150);
    c.roundRect(x, y, w, h, 26, kCardFill);
    c.roundRectOutline(x, y, w, h, 26, kFocusBlue, 4);
    c.textCenterVC(x + w / 2, y + 42, m_listTitle.c_str(), kPanelInk,
                   kFontMedium);
    int ry = y + 80;
    int rows = (int)m_listItems.size();
    std::string st = net::status();
    if (!st.empty())
      c.textCenterVC(x + w / 2, ry - 6, st.c_str(), kFocusBlue, kFontSmall);
    if (rows == 0)
      c.textCenterVC(x + w / 2, ry + 28,
                     scanning ? "Scanning..." : "Nothing found", kTextDim,
                     kFontSmall);
    for (int i = 0; i < rows; ++i) {
      if (ry + rowH > y + h - 60) break;
      bool sel = i == m_modalSel;
      if (sel) c.roundRect(x + 22, ry, w - 44, rowH - 6, 14, kRowSel);
      c.textVC(x + 44, ry + (rowH - 6) / 2, m_listItems[i].label.c_str(),
               kPanelInk, kFontSmall);
      if (m_listItems[i].highlight)
        ui::buttonGlyph(c, x + w - 60, ry + (rowH - 6) / 2, 24, "A", true,
                        kFocusBlue, kWhite);
      else
        c.textRightVC(x + w - 44, ry + (rowH - 6) / 2,
                      m_listItems[i].sub.c_str(), kTextDim, kFontTiny);
      ry += rowH;
    }
    c.textCenterVC(x + w / 2, y + h - 30, m_listHint.c_str(), kTextDim,
                   kFontTiny);
    return;
  }
  const int rowH = 62;
  int w = 620;
  for (const ModalItem& it : m_modalItems) {
    int tw = c.textWidth(it.label.c_str(), kFontSmall) + 90;
    if (tw > w) w = tw;
  }
  int h = 96 + (int)m_modalItems.size() * rowH + 66;
  int x = (Canvas::W - w) / 2, y = (Canvas::H - h) / 2;
  c.rectAlpha(0, 0, Canvas::W, Canvas::H, 0x101018, 150);
  c.roundRect(x, y, w, h, 26, kCardFill);
  c.roundRectOutline(x, y, w, h, 26, kFocusBlue, 4);
  c.textCenterVC(x + w / 2, y + 44, m_modalTitle.c_str(), kPanelInk,
                 kFontMedium);
  int listTop = y + 86;
  int listH = h - 86 - 66;
  int visibleRows = listH / rowH;
  if (visibleRows < 1) visibleRows = 1;
  int first = 0;
  if (m_modalSel >= visibleRows) first = m_modalSel - visibleRows + 1;
  int ry = listTop;
  for (int i = first; i < (int)m_modalItems.size(); ++i) {
    if (i - first >= visibleRows) break;
    bool sel = i == m_modalSel;
    if (sel) c.roundRect(x + 22, ry, w - 44, rowH - 8, 16, kRowSel);
    c.textVC(x + 44, ry + (rowH - 8) / 2, m_modalItems[i].label.c_str(),
             kPanelInk, kFontSmall);
    // active choice marker (no script file names in the list)
    if (!m_modalItems[i].value.empty() &&
        m_modalItems[i].value.back() == '*')
      ui::buttonGlyph(c, x + w - 60, ry + (rowH - 8) / 2, 26, "A", true,
                      kFocusBlue, kWhite);
    ry += rowH;
  }
  int hy = y + h - 34;
  ui::buttonGlyph(c, x + 44, hy, 24, "A", true, kGlyphInk, kWhite);
  c.textVC(x + 60, hy, "Select", kPanelInk, kFontSmall);
  ui::buttonGlyph(c, x + 210, hy, 24, "B", true, kGlyphInk, kWhite);
  c.textVC(x + 226, hy, "Close", kPanelInk, kFontSmall);
}

// ---------------------------------------------------------------- search

void Shell::openSearch() {
  m_kbOpen = true;
  m_kbText = false;
  m_kbTitle = "Search Games";
  m_kbBuf.clear();
  m_kbX = 0;
  m_kbY = 0;
  m_kbShift = false;
  m_kbInResults = false;
  m_searchSel = 0;
  m_searchScroll = 0;
  updateSearchResults();
  m_ctx.dirty = true;
}

void Shell::closeSearch() {
  m_kbOpen = false;
  m_search.clear();
  m_contentDirty = true;
  m_stripDirty = true;
  m_topDirty = true;
  // repaint the paper under the dismissed overlay (no dark patches)
  m_lastBgX = -100000;
  m_lastBgY = -100000;
  m_cartBgValid = false;
  m_ctx.dirty = true;
}

void Shell::updateSearchResults() {
  m_search.clear();
  std::string q = lower(m_kbBuf);
  for (const Game& g : m_ctx.games) {
    if (q.empty() || lower(g.name).find(q) != std::string::npos)
      m_search.push_back(&g);
  }
  if (m_searchSel >= (int)m_search.size())
    m_searchSel = std::max(0, (int)m_search.size() - 1);
  m_searchScroll = 0;
}

namespace {
// keyboard rows (same layout as the reference device keyboard)
const char* kKbRows[4] = {"1234567890", "qwertyuiop", "asdfghjkl;",
                          "zxcvbnm,./"};
const int kKbBottom[4] = {0, 2, 6, 8};  // Shift / Space / Del / OK
}  // namespace

bool Shell::handleSearch(ActionType a, ScreenResult& r) {
  if (!m_kbOpen) return false;
  if (m_kbText) {
    // text entry (password): no results, Up/Down stays inside the keys
    switch (a) {
      case ActionType::Up:
        if (m_kbY > 0) --m_kbY;
        break;
      case ActionType::Down:
        if (m_kbY < 4) ++m_kbY;
        break;
      case ActionType::Left:
        if (m_kbY == 4) {
          int cur = 0;
          for (int i = 0; i < 4; ++i)
            if (m_kbX >= kKbBottom[i]) cur = i;
          if (cur > 0) m_kbX = kKbBottom[cur - 1];
        } else if (m_kbX > 0) {
          --m_kbX;
        }
        break;
      case ActionType::Right:
        if (m_kbY == 4) {
          int cur = 0;
          for (int i = 0; i < 4; ++i)
            if (m_kbX >= kKbBottom[i]) cur = i;
          if (cur < 3) m_kbX = kKbBottom[cur + 1];
        } else if (m_kbX < 9) {
          ++m_kbX;
        }
        break;
      case ActionType::B:
        if (!m_kbBuf.empty()) m_kbBuf.pop_back();
        else closeSearch();
        break;
      case ActionType::Start:
        // fallthrough to OK
      case ActionType::A:
        if (a == ActionType::A && m_kbY != 4) {
          char ch = kKbRows[m_kbY][m_kbX];
          if (m_kbShift && ch >= 'a' && ch <= 'z') {
            ch = (char)(ch - 'a' + 'A');
            m_kbShift = false;
          }
          if (m_kbBuf.size() < 40) m_kbBuf += ch;
          break;
        }
        if (a == ActionType::A && m_kbY == 4 && m_kbX < 2) {
          m_kbShift = !m_kbShift;
          break;
        }
        if (a == ActionType::A && m_kbY == 4 && m_kbX < 6) {
          if (m_kbBuf.size() < 40) m_kbBuf += ' ';
          break;
        }
        if (a == ActionType::A && m_kbY == 4 && m_kbX < 8) {
          if (!m_kbBuf.empty()) m_kbBuf.pop_back();
          break;
        }
        {
          // OK: run the tagged action with the entered text
          std::string tag = m_kbTag;
          std::string text = m_kbBuf;
          closeSearch();
          if (tag.rfind("wifi:", 0) == 0) {
            std::string ssid = tag.substr(5);
            net::wifiConnect(ssid, text, true);
            m_status = "Connecting to " + ssid + "...";
            m_statusUntil = SDL_GetTicks() + 2000;
            m_topDirty = true;
          }
          m_ctx.dirty = true;
        }
        break;
      default:
        break;
    }
    m_ctx.dirty = true;
    return true;
  }
  const int nRes = (int)m_search.size();
  switch (a) {
    case ActionType::Up:
      if (m_kbInResults) {
        if (m_searchSel > 0) --m_searchSel;
        else m_kbInResults = false;  // back to the keys
      } else if (m_kbY > 0) {
        --m_kbY;
      } else if (nRes > 0) {
        m_kbInResults = true;
      }
      break;
    case ActionType::Down:
      if (m_kbInResults) {
        if (m_searchSel + 1 < nRes) ++m_searchSel;
        else {
          m_kbInResults = false;
          m_kbY = 4;
        }
      } else if (m_kbY < 4) {
        ++m_kbY;
      }
      break;
    case ActionType::Left:
      if (!m_kbInResults) {
        if (m_kbY == 4) {
          int cur = 0;
          for (int i = 0; i < 4; ++i)
            if (m_kbX >= kKbBottom[i]) cur = i;
          if (cur > 0) m_kbX = kKbBottom[cur - 1];
        } else if (m_kbX > 0) {
          --m_kbX;
        }
      }
      break;
    case ActionType::Right:
      if (!m_kbInResults) {
        if (m_kbY == 4) {
          int cur = 0;
          for (int i = 0; i < 4; ++i)
            if (m_kbX >= kKbBottom[i]) cur = i;
          if (cur < 3) m_kbX = kKbBottom[cur + 1];
        } else if (m_kbX < 9) {
          ++m_kbX;
        }
      }
      break;
    case ActionType::A:
      if (m_kbInResults) {
        if (m_searchSel < nRes) {
          const Game* g = m_search[m_searchSel];
          r.kind = ScreenResult::Launch;
          const System& sys = m_ctx.systems[g->systemIndex];
          LaunchOption opt;
          int idx = Settings::instance().getIntDefault("emu:" + g->path, -1);
          if (idx >= 0 && idx < (int)sys.launches.size())
            opt = sys.launches[idx];
          else {
            opt.name = "Default";
            opt.script = sys.defaultScript;
          }
          r.script = joinPath(joinPath(emusDir(), sys.id), opt.script);
          r.arg = g->path;
          Settings::instance().addRecent(g->path);
          Settings::instance().save();
          closeSearch();
        }
        break;
      }
      // on the keys: press the selected key
      if (m_kbY == 4) {
        if (m_kbX < 2) {
          m_kbShift = !m_kbShift;
        } else if (m_kbX < 6) {
          m_kbBuf += ' ';
          updateSearchResults();
        } else if (m_kbX < 8) {
          if (!m_kbBuf.empty()) m_kbBuf.pop_back();
          updateSearchResults();
        } else {
          closeSearch();
        }
      } else {
        char ch = kKbRows[m_kbY][m_kbX];
        if (m_kbShift && ch >= 'a' && ch <= 'z') {
          ch = (char)(ch - 'a' + 'A');
          m_kbShift = false;
        }
        if (m_kbBuf.size() < 40) {
          m_kbBuf += ch;
          updateSearchResults();
        }
      }
      break;
    case ActionType::B:
      if (!m_kbBuf.empty()) {
        m_kbBuf.pop_back();
        updateSearchResults();
      } else {
        closeSearch();
      }
      break;
    case ActionType::Start:
    case ActionType::Select:
      closeSearch();
      break;
    default:
      break;
  }
  m_ctx.dirty = true;
  return true;
}

void Shell::drawSearch(Canvas& c) {
  if (!m_kbOpen) return;
  c.rectAlpha(0, 0, Canvas::W, Canvas::H, 0x101018, 170);

  // text field
  c.textVC(40, 34, upper(m_kbTitle.empty() ? "Search Games" : m_kbTitle).c_str(),
           kFocusBlue, kFontSmall);
  int fx = 40, fy = 52, fw = Canvas::W - 80, fh = 56;
  c.roundRect(fx, fy, fw, fh, 16, kCardFill);
  c.roundRectOutline(fx, fy, fw, fh, 16, kFocusBlue, 3);
  std::string shown = m_kbBuf.empty() ? "type to search..." : m_kbBuf;
  int tx = fx + 18;
  c.textVC(tx, fy + fh / 2, shown.c_str(),
           m_kbBuf.empty() ? kTextFaint : kPanelInk, kFontSmall);
  if (!m_kbBuf.empty()) {
    int cw = c.textWidth(m_kbBuf.c_str(), kFontSmall);
    c.rect(tx + cw + 3, fy + 14, 3, fh - 28, kFocusBlue);
  }
  c.textRightVC(fx + fw - 18, fy + fh / 2,
                fmt("%d results", (int)m_search.size()).c_str(), kTextDim,
                kFontTiny);

  if (m_kbText) {
    // keys only: the field already showed the text
    const int keyW = 92, keyH = 58, gap = 5;
    int kw = 10 * keyW + 9 * gap;
    int kx0 = (Canvas::W - kw) / 2;
    const int ky0 = 200;
    for (int row = 0; row < 4; ++row) {
      for (int col = 0; col < 10; ++col) {
        char ch = kKbRows[row][col];
        char out[2] = {ch, 0};
        if (m_kbShift && ch >= 'a' && ch <= 'z') out[0] = (char)(ch - 32);
        int x = kx0 + col * (keyW + gap), y = ky0 + row * (keyH + gap);
        bool sel = m_kbY == row && m_kbX == col;
        c.roundRect(x, y, keyW, keyH, 12, sel ? kFocusBlue : kCardFill);
        c.roundRectOutline(x, y, keyW, keyH, 12, kBorder, 2);
        c.textCenterVC(x + keyW / 2, y + keyH / 2, out,
                       sel ? kWhite : kPanelInk, kFontSmall);
      }
    }
    const char* bottomLabels[4] = {"Shift", "Space", "Del", "OK"};
    const int bottomW[4] = {2, 4, 2, 2};
    int bx = kx0;
    const int by = ky0 + 4 * (keyH + gap);
    for (int i = 0; i < 4; ++i) {
      int w = bottomW[i] * keyW + (bottomW[i] - 1) * gap;
      int cur = 0;
      for (int k = 0; k < 4; ++k)
        if (m_kbX >= kKbBottom[k]) cur = k;
      bool sel = m_kbY == 4 && cur == i;
      c.roundRect(bx, by, w, keyH, 12, sel ? kFocusBlue : kCardFill);
      c.roundRectOutline(bx, by, w, keyH, 12, kBorder, 2);
      c.textCenterVC(bx + w / 2, by + keyH / 2, bottomLabels[i],
                     sel ? kWhite : kPanelInk, kFontSmall);
      bx += w + gap;
    }
    int hy2 = by + keyH + 30;
    ui::buttonGlyph(c, 60, hy2, 24, "A", true, kWhite, kGlyphInk);
    c.textVC(76, hy2, "Key", kPillText, kFontSmall);
    ui::buttonGlyph(c, 200, hy2, 24, "B", true, kWhite, kGlyphInk);
    c.textVC(216, hy2, "Backspace", kPillText, kFontSmall);
    ui::buttonGlyph(c, 420, hy2, 24, "Start", false, kWhite, kGlyphInk);
    c.textVC(452, hy2, "OK", kPillText, kFontSmall);
    return;
  }

  // results (top) + keyboard (bottom)
  const int listY = 120, listH = 176, rowH = 44;
  int listW = Canvas::W - 80;
  c.roundRect(40, listY, listW, listH, 16, kCardFill);
  int visible = listH / rowH;
  if (m_search.empty()) {
    c.textVC(60, listY + 24, m_kbBuf.empty() ? "No query" : "No matches",
             kTextDim, kFontSmall);
  } else {
    if (m_searchSel < m_searchScroll) m_searchScroll = m_searchSel;
    if (m_searchSel >= m_searchScroll + visible)
      m_searchScroll = m_searchSel - visible + 1;
    for (int i = 0; i < visible; ++i) {
      int gi = m_searchScroll + i;
      if (gi >= (int)m_search.size()) break;
      const Game& g = *m_search[gi];
      bool sel = m_kbInResults && gi == m_searchSel;
      if (sel) c.roundRect(48, listY + 6 + i * rowH, listW - 16, rowH - 6, 12,
                           kRowSel);
      const System& sys = m_ctx.systems[g.systemIndex];
      // truncate long names (no marquee in the list)
      int nameMaxW = listW - 16 - 24 - c.textWidth(sys.label.c_str(), kFontTiny) -
                     30;
      c.textClipped(64, listY + 6 + i * rowH + (rowH - 6) / 2 - 12, nameMaxW,
                    g.name.c_str(), kPanelInk, kFontSmall);
      c.textRightVC(40 + listW - 20, listY + 6 + i * rowH + (rowH - 6) / 2,
                    sys.label.c_str(), kTextDim, kFontTiny);
    }
  }

  // keyboard
  const int keyW = 92, keyH = 58, gap = 5;
  int kw = 10 * keyW + 9 * gap;
  int kx0 = (Canvas::W - kw) / 2;
  const int ky0 = 300;
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 10; ++col) {
      char ch = kKbRows[row][col];
      char out[2] = {ch, 0};
      if (m_kbShift && ch >= 'a' && ch <= 'z') out[0] = (char)(ch - 32);
      int x = kx0 + col * (keyW + gap), y = ky0 + row * (keyH + gap);
      bool sel = !m_kbInResults && m_kbY == row && m_kbX == col;
      c.roundRect(x, y, keyW, keyH, 12, sel ? kFocusBlue : kCardFill);
      c.roundRectOutline(x, y, keyW, keyH, 12, kBorder, 2);
      c.textCenterVC(x + keyW / 2, y + keyH / 2, out,
                     sel ? kWhite : kPanelInk, kFontSmall);
    }
  }
  const char* bottomLabels[4] = {"Shift", "Space", "Del", "OK"};
  const int bottomW[4] = {2, 4, 2, 2};
  int bx = kx0;
  const int by = ky0 + 4 * (keyH + gap);
  for (int i = 0; i < 4; ++i) {
    int w = bottomW[i] * keyW + (bottomW[i] - 1) * gap;
    int cur = 0;
    for (int k = 0; k < 4; ++k)
      if (m_kbX >= kKbBottom[k]) cur = k;
    bool sel = !m_kbInResults && m_kbY == 4 && cur == i;
    c.roundRect(bx, by, w, keyH, 12, sel ? kFocusBlue : kCardFill);
    c.roundRectOutline(bx, by, w, keyH, 12, kBorder, 2);
    c.textCenterVC(bx + w / 2, by + keyH / 2, bottomLabels[i],
                   sel ? kWhite : kPanelInk, kFontSmall);
    bx += w + gap;
  }

  // hints
  int hy = by + keyH + 30;
  ui::buttonGlyph(c, 60, hy, 24, "A", true, kWhite, kGlyphInk);
  c.textVC(76, hy, "Key", kPillText, kFontSmall);
  ui::buttonGlyph(c, 200, hy, 24, "B", true, kWhite, kGlyphInk);
  c.textVC(216, hy, "Backspace", kPillText, kFontSmall);
  {
    // wide pill so "Start" fits inside the glyph
    int tw = c.textWidth("Start", kFontTiny);
    int pw = tw + 26;
    int cx = 420 + pw / 2;
    c.roundRect(cx - pw / 2, hy - 12, pw, 24, 12, kCardFill);
    c.textCenterVC(cx, hy - 1, "Start", kPanelInk, kFontTiny);
    c.textVC(cx + pw / 2 + 12, hy, "Close", kPillText, kFontSmall);
  }
}

void Shell::refreshLibrary() {
  m_ctx.systems = scanSystems();
  m_ctx.games = scanGames(m_ctx.systems);
  m_ctx.images.clear();          // pick up new art/icons
  m_canvas.clearSurfaceCache();
  m_carousel.clearCaches();
  // rebuild only the games chooser: calling buildCategories() here used to
  // replace the strip with the Settings categories while still in Games
  buildSystemStrip();
  refreshShowcase();
  refreshSystemGames();
  m_pageClear = true;
  m_contentDirty = true;
  m_stripDirty = true;
  m_topDirty = true;
  m_lastBgX = -100000;
  m_lastBgY = -100000;
  m_cartBgValid = false;
  m_status = fmt("Library refreshed: %d games", (int)m_ctx.games.size());
  m_statusUntil = SDL_GetTicks() + 2000;
  m_ctx.dirty = true;
}

const Game* Shell::focusedGame() const {
  if (m_level == Level::Sections) {
    int i = m_carousel.selection();
    if (m_showcase.empty() || i < 0 || i >= (int)m_showcase.size())
      return nullptr;
    return m_showcase[i];
  }
  int i = m_carousel.selection();
  if (m_systemGames.empty() || i < 0 || i >= (int)m_systemGames.size())
    return nullptr;
  return m_systemGames[i];
}

LaunchOption Shell::focusedLaunch() const {
  LaunchOption opt;
  const Game* g = focusedGame();
  if (!g) return opt;
  const System& sys = m_ctx.systems[g->systemIndex];
  // -1 = the user has not chosen an emulator for this game yet
  int idx = Settings::instance().getIntDefault("emu:" + g->path, -1);
  if (idx < 0) {
    // mirror the stock launcher: run config.json's "launch" script. The
    // first launchlist entry is not always usable (DC's flycast.sh sources a
    // missing file and dies before the emulator starts).
    opt.name = "Default";
    opt.script = sys.defaultScript;
    return opt;
  }
  if (sys.launches.empty()) {
    opt.name = "Default";
    opt.script = sys.defaultScript;
    return opt;
  }
  if (idx >= (int)sys.launches.size()) idx = 0;
  return sys.launches[idx];
}

void Shell::applyPendingMode() {
  if (m_pendingMode < 0) return;
  int sel = m_pendingMode;
  m_pendingMode = -1;
  if (sel < 0 || sel > 3 || (Mode)sel == m_mode) return;
  m_mode = (Mode)sel;
  m_slideAnim = 0.f;
  m_contentScroll = 0;
  m_pageClear = true;
  if (m_mode == Mode::Games) {
    m_systemSel = 0;
    refreshShowcase();
  }
  m_ctx.dirty = true;
  m_contentDirty = true;
}

void Shell::enterMode(Mode m) {
  m_mode = m;
  m_pageClear = true;
  m_level = Level::Sections;
  m_contentFocus = false;
  m_focusAnim = 0.f;
  m_slideAnim = 0.f;
  m_contentScroll = 0;
  buildSections();
  refreshApps();
  m_strip.setSelection((int)m, false);
  if (m == Mode::Games) {
    m_carousel.setSelection(0);
    refreshShowcase();
  }
  m_ctx.dirty = true;
  m_contentDirty = true;
}

void Shell::back() {
  if (m_contentFocus) {
    m_contentFocus = false;
    m_ctx.dirty = true;
    return;
  }
  if (m_level == Level::Context) {
    m_level = Level::Sections;
    m_slideAnim = 0.f;
    m_pageClear = true;
    buildSections();
    m_strip.setSelection((int)m_mode, false);
    if (m_mode == Mode::Games) {
      // back at the root: fresh random samples, small models (NOT the last
      // console's game list - that used to be shown here)
      refreshShowcase();
    }
    m_ctx.dirty = true;
    m_contentDirty = true;
    m_topDirty = true;
    m_stripDirty = true;
    return;
  }
}

void Shell::debugCategory(int cat) {
  m_catSel = std::clamp(cat, 0, 7);
  if (m_level == Level::Context && m_mode == Mode::Settings) {
    buildSettingRows();
    m_rowSel = 0;
  }
  if (m_mode == Mode::Settings) {
    buildCategories();
    m_strip.setSelection(m_catSel, false);
  }
  m_pageClear = true;
  m_contentDirty = true;
  m_stripDirty = true;
  m_ctx.dirty = true;
}

void Shell::debugSelect(int idx) {
  if (idx >= 0 && idx < m_strip.count()) {
    m_strip.setSelection(idx, false);
    if (m_mode == Mode::Games) {
      m_systemSel = idx;
      refreshSystemGames();
    } else if (m_mode == Mode::Settings) {
      m_catSel = idx;
      if (m_level == Level::Context) buildSettingRows();
    }
    m_contentDirty = true;
    m_stripDirty = true;
    m_ctx.dirty = true;
  }
}

std::string Shell::debugState() const {
  const char* modeName = "home";
  switch (m_mode) {
    case Mode::Home: modeName = "home"; break;
    case Mode::Apps: modeName = "apps"; break;
    case Mode::Games: modeName = "games"; break;
    case Mode::Settings: modeName = "settings"; break;
  }
  return fmt("mode=%s level=%s focus=%d strip=%d/%d cat=%d row=%d/%d games=%d "
             "apps=%d modal=%d kb=%d status='%s'",
             modeName, m_level == Level::Context ? "context" : "sections",
             m_contentFocus ? 1 : 0, m_strip.selection(), m_strip.count(),
             m_catSel, m_rowSel, (int)m_rows.size(), (int)m_systemGames.size(),
             (int)m_apps.size(), (int)m_modal, m_kbOpen ? 1 : 0,
             m_status.c_str());
}

void Shell::debugSetMode(const std::string& name) {
  if (name == "games") {
    enterMode(Mode::Games);
  } else if (name == "games2") {
    enterMode(Mode::Games);
    m_level = Level::Context;
    m_slideAnim = 1.f;
    m_contentFocus = true;
    m_focusAnim = 1.f;
    buildSystemStrip();
    const char* sysEnv = getenv("NDS_SYS");
    int si = sysEnv ? atoi(sysEnv) : 0;
    if (si < 0 || si >= m_strip.count()) si = 0;
    m_systemSel = si;
    m_strip.setSelection(si, false);
    refreshSystemGames();
    if (const char* mo = getenv("NDS_MODEL")) m_carousel.setModelOverride(mo);
    const char* rotEnv = getenv("NDS_ROT");
    if (rotEnv) m_carousel.rotate((float)atoi(rotEnv) * 12.f);
    const char* pitEnv = getenv("NDS_PITCH");
    if (pitEnv) m_carousel.rotatePitch((float)atoi(pitEnv) * 12.f);

  } else if (name == "apps") {
    enterMode(Mode::Apps);
    m_contentFocus = true;
    m_focusAnim = 1.f;
  } else if (name == "settings") {
    enterMode(Mode::Settings);  // sections strip + the category grid below
    m_contentFocus = true;
    m_focusAnim = 1.f;
  } else if (name == "settings2") {
    enterMode(Mode::Settings);
    m_level = Level::Context;
    m_slideAnim = 1.f;
    m_contentFocus = true;
    m_focusAnim = 1.f;
    const char* cat = getenv("NDS_CAT");
    m_catSel = cat ? std::clamp(atoi(cat), 0, 2) : 0;
    buildSettingRows();
  } else {
    enterMode(Mode::Home);
    m_strip.setSelection((int)Mode::Home, false);
  }
  // screenshot helpers: open an overlay after the screen is set up
  if (const char* modalEnv = getenv("NDS_MODAL")) {
    std::string m = modalEnv;
    if (m == "chooser") openChooserModal();
    else if (m == "game") openGameModal();
    else if (m == "search") openSearch();
    else if (m == "settings") {
      buildSettingRows();
      m_modalSel = 0;
      m_modal = Modal::Settings;
    }
  }
}

ScreenResult Shell::handle(ActionType a) {
  m_lastInputAt = SDL_GetTicks();
  ScreenResult r;
  // overlays capture input first: the search keyboard, then the modals
  if (m_kbOpen) {
    if (handleSearch(a, r)) return r;
  }
  if (m_modal == Modal::Settings) {
    if (handleSettingsModal(a, r)) return r;
  }
  if (m_modal != Modal::None) {
    if (handleModal(a, r)) return r;
  }
  if (!m_status.empty() && SDL_GetTicks() > m_statusUntil) m_status.clear();
  m_contentDirty = true;  // any action can affect the content area
  m_stripDirty = true;

  const bool inContent = m_contentFocus;

  switch (a) {
    case ActionType::L:
      m_strip.setSelection(m_strip.selection() - 1);
      if (m_mode == Mode::Games) {
        m_systemSel = m_strip.selection();
        refreshSystemGames();
      }
      m_ctx.dirty = true;
      break;
    case ActionType::R:
      m_strip.setSelection(m_strip.selection() + 1);
      if (m_mode == Mode::Games) {
        m_systemSel = m_strip.selection();
        refreshSystemGames();
      }
      m_ctx.dirty = true;
      break;

    case ActionType::RotateLeft:
      if (m_mode == Mode::Games && m_level == Level::Context) {
        m_carousel.rotate(-8.f);
        m_ctx.dirty = true;
      }
      break;
    case ActionType::RotateRight:
      if (m_mode == Mode::Games && m_level == Level::Context) {
        m_carousel.rotate(8.f);
        m_ctx.dirty = true;
      }
      break;

    case ActionType::Down: {
      if (inContent && m_mode == Mode::Games && m_level == Level::Sections) {
        // the root Games page samples three random games: Up/Down does
        // nothing there (only A enters the console carousel)
        break;
      } else if (inContent && m_mode == Mode::Settings &&
                 m_level == Level::Sections) {
        if (m_catSel + 4 < kSettingsCategoryCount) {
          m_catSel += 4;
          m_pageClear = true;
        } else {
          m_contentFocus = false;  // single row: back to the sections strip
        }
        m_ctx.dirty = true;
      } else if (inContent && m_mode == Mode::Settings &&
                 m_level == Level::Context) {
        if (m_rowSel + 1 < (int)m_rows.size()) ++m_rowSel;
        m_pageClear = true;
        m_ctx.dirty = true;
      } else if (inContent && m_mode == Mode::Apps) {
        int row = (m_appSel % 8) / 4;
        if (row == 1) {
          // bottom row: Down returns to the carousel (never to the next page)
          m_contentFocus = false;
          m_ctx.dirty = true;
          break;
        }
        int n = (int)m_apps.size();
        int next = m_appSel + 4;
        if (next < n) {
          m_appSel = next;
          m_pageClear = true;
        }
        m_ctx.dirty = true;
      } else if (inContent) {
        // leave the content -> back to the strip
        m_contentFocus = false;
        m_ctx.dirty = true;
      }
      break;
    }
    case ActionType::Up: {
      if (!inContent) {
        // enter the content. Home has nothing to focus, and the root Games
        // page only enters with A (Up must not start scrolling the samples)
        bool focusable = (m_mode == Mode::Apps) || (m_mode == Mode::Settings) ||
                         (m_mode == Mode::Games && m_level == Level::Context);
        if (focusable) {
          m_contentFocus = true;
          m_ctx.dirty = true;
        }
      } else if (m_mode == Mode::Games && m_level == Level::Sections) {
        // no scrolling of the random samples (see the Down case)
        break;
      } else if (m_mode == Mode::Settings && m_level == Level::Sections) {
        m_catSel = std::max(0, m_catSel - 4);
        m_ctx.dirty = true;
      } else if (m_mode == Mode::Settings && m_level == Level::Context) {
        if (m_rowSel > 0) --m_rowSel;
        m_pageClear = true;
        m_ctx.dirty = true;
      } else if (m_mode == Mode::Apps) {
        if ((m_appSel % 8) / 4 == 0) {
          // top row: Up stays (no wrap to the previous page)
          break;
        }
        m_appSel -= 4;
        m_pageClear = true;
        m_ctx.dirty = true;
      }
      break;
    }

    case ActionType::A: {
      if (m_level == Level::Sections) {
        // entering the selected section / opening its focused item
        if (m_mode == Mode::Games) {
          m_level = Level::Context;
          m_slideAnim = 0.f;
          buildSystemStrip();
          if (m_systemSel >= m_strip.count()) m_systemSel = 0;
          m_strip.setSelection(m_systemSel, false);
          m_gameSel = 0;
          refreshSystemGames();
          m_contentFocus = true;
          m_focusAnim = 1.f;
          if (!m_strip.items().empty())
            m_status = "System: " + m_strip.items()[m_strip.selection()].label;
          m_statusUntil = SDL_GetTicks() + 1500;
        } else if (m_mode == Mode::Settings) {
          // grid like Apps: A focuses it, A again opens the category modal
          if (!m_contentFocus) {
            m_contentFocus = true;
            m_focusAnim = 1.f;
          } else {
            buildSettingRows();
            m_modalSel = 0;
            m_modal = Modal::Settings;
          }
        } else if (m_mode == Mode::Apps) {
          if (!m_contentFocus) {
            m_contentFocus = true;  // enter the grid
          } else if (m_appSel >= 0 && m_appSel < (int)m_apps.size()) {
            r.kind = ScreenResult::Launch;
            r.script = m_apps[m_appSel].launch;
            r.arg.clear();
            r.label = m_apps[m_appSel].label;
          }
        }

        // Home has nothing to focus: A does nothing there
        m_ctx.dirty = true;
        break;
      }
      // Context levels
      if (m_mode == Mode::Games) {
        if (!inContent) {
          // bottom (systems) focused: A jumps into the games listing
          m_contentFocus = true;
          m_ctx.dirty = true;
          break;
        }
        const Game* g = focusedGame();
        const System* sys =
            g ? &m_ctx.systems[g->systemIndex] : nullptr;
        const LaunchOption opt = focusedLaunch();
        if (g && sys && !opt.script.empty()) {
          r.kind = ScreenResult::Launch;
          r.script = joinPath(joinPath(emusDir(), sys->id), opt.script);
          r.arg = g->path;
          Settings::instance().addRecent(g->path);
          Settings::instance().save();
        }
      } else if (m_mode == Mode::Settings) {
        if (m_level == Level::Context && m_rowSel >= 0 &&
            m_rowSel < (int)m_rows.size())
          activateRow(m_rows[m_rowSel]);
      } else if (m_mode == Mode::Apps) {
        if (!inContent) {
          m_contentFocus = true;  // A jumps into the grid
        } else if (m_appSel >= 0 && m_appSel < (int)m_apps.size()) {
          r.kind = ScreenResult::Launch;
          r.script = m_apps[m_appSel].launch;
          r.arg.clear();
          r.label = m_apps[m_appSel].label;
        }
      }
      break;
    }

    case ActionType::B:
      // B in the root carousel must NOT exit the launcher: quitting is only
      // possible through the System > Quit NDSUI row
      back();
      break;

    case ActionType::Left:
    case ActionType::Right: {
      int dir = (a == ActionType::Right ? 1 : -1);
      if (inContent) {
        // d-pad navigates the focused carousel (games / content)
        if (m_mode == Mode::Games) {
          m_carousel.setSelection(m_carousel.selection() + dir);
          if (m_level == Level::Context) m_gameSel = m_carousel.selection();
          m_ctx.dirty = true;
          m_contentDirty = true;
        } else if (m_mode == Mode::Apps) {
          int n = (int)m_apps.size();
          if (n > 0) {
            int next = std::clamp(m_appSel + dir, 0, n - 1);
            if (next / 8 != m_appSel / 8) m_pageClear = true;
            m_appSel = next;
            m_appPage = m_appSel / 8;  // page follows the selection
          }
          m_ctx.dirty = true;
        } else if (m_mode == Mode::Settings &&
                   m_level == Level::Context) {
          if (m_rowSel >= 0 && m_rowSel < (int)m_rows.size())
            adjustRow(m_rows[m_rowSel], dir);
        } else if (m_mode == Mode::Settings && m_level == Level::Sections) {
          m_catSel =
              std::clamp(m_catSel + dir, 0, kSettingsCategoryCount - 1);
          m_pageClear = true;
          m_ctx.dirty = true;
        }
        break;
      }
      if (m_level == Level::Sections) {
        // the sections carousel at the root
        m_strip.setSelection(m_strip.selection() + dir);
        syncModeFromStrip();
      } else {
        // inside a section the d-pad drives the focused content
        m_strip.setSelection(m_strip.selection() + dir);
        if (m_mode == Mode::Games) {
          m_systemSel = m_strip.selection();
          refreshSystemGames();
        } else if (m_mode == Mode::Settings) {
          m_catSel = m_strip.selection();
        }
      }
      m_ctx.dirty = true;
      break;
    }

    case ActionType::X: {
      if (m_mode == Mode::Apps) {
        // X: refresh the app list
        refreshApps();
        m_status = fmt("Apps refreshed: %d", (int)m_apps.size());
        m_statusUntil = SDL_GetTicks() + 1600;
        m_contentDirty = true;
        m_ctx.dirty = true;
        break;
      }
      if (m_mode != Mode::Games) break;
      // X = Options: the console chooser opens the library menu, a focused
      // game opens its emulator picker
      if (m_contentFocus) openGameModal();
      else openChooserModal();
      break;
    }

    default:
      break;
  }
  return r;
}

void Shell::tick() {
  m_strip.tick();
  if (m_strip.animating()) {
    m_ctx.dirty = true;  // redraw every eased frame
    m_stripDirty = true;
  }
  {
    std::string n = nameForDisplay();
    if (n != m_lastName) {
      m_lastName = n;
      m_stripDirty = true;
    }
  }
  // the top console pill follows the chooser (prev/current/next)
  if (bigGames() && m_strip.animating()) m_topDirty = true;
  tickNameMarquee();

  m_home.tick();
  if (m_home.changed()) {
    m_ctx.dirty = true;
    m_contentDirty = true;
    m_home.clearChanged();  // consume: the loop watches animating() for pace
  }

  // page switches wait for the strip to settle (keeps dpad scrolling at 60)
  applyPendingMode();

  // carousel: eases and renders when the scene changes; the geometry is
  // queued for present() (rasterized after drawContent). While an overlay is
  // open the carts are frozen (input belongs to the overlay, and present()
  // must not paint them over the modal).
  const bool overlayOpen = (m_modal != Modal::None || m_kbOpen);
  if (m_mode == Mode::Games && !overlayOpen) {
    // rotation only inside a console list: the root samples are static
    if (m_rotateAxis != 0 && m_level == Level::Context) {
      m_carousel.rotate((float)m_rotateAxis * 3.4f);
      m_ctx.dirty = true;
    }
    if (m_pitchAxis != 0 && m_level == Level::Context) {
      m_carousel.rotatePitch((float)m_pitchAxis * 3.0f);
      m_ctx.dirty = true;
    }
    m_carousel.tick();
    // re-render the carts whenever they changed (composited in present()).
    // m_ctx.dirty must be set too: the main loop only redraws on that, and
    // without it the carousel animation rendered ~2 frames per move.
    if (m_carousel.dirty()) {
      m_contentDirty = true;
      m_ctx.dirty = true;
      m_canvas.beginCartFrame();
      m_carousel.drawOverlay();
      m_canvas.endCartFrame();
    }
  }

  // focus animation
  float target = m_contentFocus ? 1.f : 0.f;
  if (std::fabs(target - m_focusAnim) > 0.005f) {
    m_focusAnim += (target - m_focusAnim) * 0.25f;
    m_ctx.dirty = true;
    m_contentDirty = true;
  }
  // slide animation (context entry)
  if (m_level == Level::Context && m_slideAnim < 1.f) {
    m_slideAnim = std::min(1.f, m_slideAnim + 0.09f);
    m_ctx.dirty = true;
    m_contentDirty = true;
  } else if (m_level == Level::Sections && m_slideAnim > 0.f) {
    m_slideAnim = std::max(0.f, m_slideAnim - 0.09f);
    m_ctx.dirty = true;
    m_contentDirty = true;
  }

  std::string clk = clockString();
  std::string bat = batteryString();
  if (clk != m_clock || bat != m_battery) {
    m_clock = clk;
    m_battery = bat;
    m_ctx.dirty = true;
    m_topDirty = true;
  }
  // DISABLED: live network state + volume polling. The top bar has no
  // wifi/bluetooth/volume icons any more, and NDSUI does not touch radios
  // or sound (the stock OSD / MainUI own them).
  // if (SDL_GetTicks() > m_netPollAt) { ... }
  if (!m_status.empty() && SDL_GetTicks() > m_statusUntil) {
    m_status.clear();
    m_ctx.dirty = true;
    m_contentDirty = true;
  }
  // DISABLED: screen timeout / suspend. NDSUI must not drive the panel or
  // the power state any more - the stock OSD and MainUI own that.
  // {
  //   int ti = Settings::instance().getIntDefault("screen_timeout", 0);
  //   static const Uint32 kMins[] = {0, 1, 5, 10, 15, 30};
  //   if (ti > 0 && ti < 6 && !m_asleep &&
  //       SDL_GetTicks() - m_lastInputAt > kMins[ti] * 60000u) {
  //     m_asleep = true;
  //     net::brightnessSet(0);
  //     FILE* f = fopen("/sys/power/state", "w");
  //     if (f) {
  //       fputs("mem", f);
  //       fclose(f);
  //     }
  //     net::displayApplyFromSettings();
  //     m_lastInputAt = SDL_GetTicks();
  //     m_asleep = false;
  //     m_ctx.dirty = true;
  //     m_topDirty = true;
  //   }
  // }
  // volume icon removed from the top bar: no mixer polling any more
  // int vol = Settings::instance().getIntDefault("volume", 50);
  // if (vol != m_volume) {
  //   m_volume = vol;
  //   m_ctx.dirty = true;
  //   m_topDirty = true;
  // }
}

bool Shell::animating() const {
  if (m_strip.animating()) return true;
  if (m_marqueeDir != 0) return true;  // name pill is scrolling
  if (std::fabs((m_contentFocus ? 1.f : 0.f) - m_focusAnim) > 0.01f) return true;
  if (m_level == Level::Context && m_slideAnim < 1.f) return true;
  if (m_level == Level::Sections && m_slideAnim > 0.f) return true;
  if (m_mode == Mode::Games && m_carousel.dirty())
    return true;
  return false;
}

void Shell::draw(Canvas& c) {
  static const bool prof = getenv("NDS_FPSLOG") != nullptr;
  Uint32 t0 = prof ? SDL_GetTicks() : 0;
  // overlays (modal / search) draw over everything: skip the cart composite
  const bool overlay = (m_modal != Modal::None || m_kbOpen);
  // Parallax follows section/level transitions and content focus only. The
  // per-item strip scroll leaves the background static so the frame can use
  // the cheap band-repaint + partial texture upload path.
  int ox = (int)(m_slideAnim * 24.f);
  int oy = (int)(m_focusAnim * 16.f + m_slideAnim * 10.f);
  if (overlay) {
    m_canvas.setCartDest(0, 0, 0, 0);
    // Repaint the whole paper first. The content band's margins are not part
    // of any widget band, so they would otherwise keep the dim of the previous
    // frame and the translucent backdrop stacks to black over a few frames.
    m_bg.draw(c, (float)ox, (float)oy);
    m_lastBgX = ox;
    m_lastBgY = oy;
    c.markUiDirty();
    // any partial repaint would leave a hole in the dim backdrop: force the
    // whole frame while an overlay is up (they are static, so this happens
    // only when something actually changed)
    m_topDirty = m_contentDirty = m_stripDirty = true;
    m_pillDirty = false;
  }
  bool bgMoved = (ox != m_lastBgX || oy != m_lastBgY);
  const bool bandDirty = m_stripDirty || bgMoved;
  if (bgMoved) {
    m_lastBgX = ox;
    m_lastBgY = oy;
    m_cartBgValid = false;  // the paper moved: re-cache the save-under
    m_bg.draw(c, (float)ox, (float)oy);
    c.markUiDirty();
  } else if (m_pageClear) {
    // full paper repaint on a page/theme switch, BEFORE the top bar refresh
    // below: wiping the paper after the bar was drawn made it blink out for
    // a frame (and left old-palette grid lines in untouched regions)
    m_cartBgValid = false;
    m_bg.draw(c, (float)ox, (float)oy);
    c.markUiDirty();
    m_pageClear = false;
    m_topDirty = m_contentDirty = m_stripDirty = true;
  } else if (bandDirty) {
    // repaint the paper under the widgets we are about to redraw
    m_bg.drawRows(c, (float)ox, (float)oy, labelTop() - 56, Canvas::H);
  }
  Uint32 t1 = prof ? SDL_GetTicks() : 0;

  if (m_topDirty || bgMoved) {
    drawTopBar(c);
    m_topDirty = false;
    // partial uploads around the bar showed up as flicker: send it whole
    m_canvas.markUiDirty();
  }
  if (m_contentDirty || bgMoved) {
    drawContent(c);
    m_contentDirty = false;
    // after a content repaint only the composite needs refreshing - the cart
    // surface itself is already current (the tick renders when it changes)
    if (m_mode == Mode::Games && !m_carousel.dirty())
      m_canvas.markCartDirty();
  }
  Uint32 t2 = prof ? SDL_GetTicks() : 0;
  if (bandDirty) {
    drawNameLabel(c);
    // the console chooser grows in the games context; other pages keep the
    // compact strip
    const bool big = bigGames();
    m_strip.setMetrics(big ? 104 : 76, big ? 40 : 26);
    m_strip.setFocused(m_contentFocus ? 0.f : 1.f);
    m_strip.draw(c, 0, stripTop(), Canvas::W, stripHeight());
    drawHelpBar(c);
    m_stripDirty = false;
    m_pillDirty = false;
  } else if (m_pillDirty) {
    // the marquee moved: repaint only the pill's band (cheap, small damage)
    m_bg.drawRows(c, (float)ox, (float)oy, labelTop() - 56,
                  labelTop() + labelHeight() + 8);
    drawNameLabel(c);
    m_pillDirty = false;
  }
  // overlays draw last, over everything
  if (m_modal == Modal::Settings) drawSettingsModal(c);
  if (m_modal != Modal::None && m_modal != Modal::Settings) drawModal(c);
  if (m_kbOpen) drawSearch(c);
  if (prof) {
    Uint32 t3 = SDL_GetTicks();
    m_profBg += t1 - t0;
    m_profContent += t2 - t1;
    m_profStrip += t3 - t2;
    ++m_profN;
    Uint32 now = SDL_GetTicks();
    if (now - m_profLast > 1000) {
      m_profLast = now;
      LOG_INFO("perf/shell: bg=%.1f content=%.1f rest=%.1f ms over %d frames",
               (double)m_profBg / (m_profN ? m_profN : 1),
               (double)m_profContent / (m_profN ? m_profN : 1),
               (double)m_profStrip / (m_profN ? m_profN : 1), m_profN);
      m_profBg = m_profContent = m_profStrip = 0;
      m_profN = 0;
    }
  }
}

void Shell::drawTopBar(Canvas& c) {
  // clear the bar strip first: the console pill changes size when the
  // selection moves, and without this a shrinking pill leaves stale pixels
  m_bg.drawRows(c, (float)m_lastBgX, (float)m_lastBgY, 0, kTopBarH + 6);
  // time pill (left, optional)
  if (Settings::instance().getBoolDefault("show_clock", true)) {
    std::string t = m_clock.empty() ? clockString() : m_clock;
    c.pill(20, 10, 150, 38, kPillDark);
    c.textCenterVC(20 + 75, 10 + 19, t.c_str(), kPillText, kFontSmall);
  }

  // --- right side: fixed-width battery pill (the only right-side item) ---
  {
    // one pill, percentage inside, bolt when charging; the fill colour shows
    // the charge level and the width never changes (1/2/3 digits fit)
    int pct = batteryPercent();
    bool charging = batteryCharging();
    const int bw = 128, bh = 38;
    uint32_t fill = kPillDark;
    if (pct >= 51) fill = 0x3E9E5A;        // green
    else if (pct >= 16) fill = 0xE0A020;   // yellow
    else if (pct >= 0) fill = 0xD04040;    // red
    int bx = Canvas::W - 20 - bw;
    c.pill(bx, 10, bw, bh, fill);
    bool showPct = Settings::instance().getBoolDefault("show_battery_pct", true);
    std::string txt = pct < 0 ? "--" : fmt("%d%%", pct);
    int tw = showPct ? c.textWidth(txt.c_str(), kFontSmall) : 0;
    int content = tw + (charging ? 26 : 0);
    int cx = bx + (bw - content) / 2;
    if (charging) {
      drawBolt(c, cx + 9, 10 + bh / 2, kPillText);
      cx += 26;
    }
    if (showPct)
      c.textVC(cx, 10 + bh / 2 - 1, txt.c_str(), kPillText, kFontSmall);
    else {
      // glyph only: a small battery outline with a level bar
      int gy = 10 + bh / 2;
      int gx = bx + bw / 2 - 16;
      c.roundRectOutline(gx, gy - 9, 32, 18, 4, kPillText, 2);
      c.rect(gx + 32, gy - 4, 3, 8, kPillText);
      int lvl = pct < 0 ? 0 : pct * 26 / 100;
      if (lvl > 0) c.roundRect(gx + 3, gy - 6, lvl, 12, 2, kPillText);
    }
  }

  // DISABLED: volume / wifi / bluetooth indicators. The top bar now only
  // shows the clock and the battery pill.
  // volume: speaker + 0..3 bars (muted shows the X)
  // {
  //   ...
  // }
  //
  // wifi: shown dim unless connected to a network
  // {
  //   ...
  // }
  //
  // bluetooth: bright when a device is connected
  // {
  //   ...
  // }

  if (bigGames()) drawTopConsolePill(c);
}

void Shell::drawBolt(Canvas& c, int cx, int cy, uint32_t col) {
  // small lightning bolt (charging indicator - never a "+")
  c.line(cx + 3, cy - 10, cx - 3, cy - 1, 4, col);
  c.line(cx - 3, cy - 1, cx + 3, cy + 1, 4, col);
  c.line(cx + 3, cy + 1, cx - 3, cy + 10, 4, col);
}

void Shell::drawTopConsolePill(Canvas& c) {
  // centered: [L] current console [R] - the chooser's context. The name is
  // bounded so the pill never reaches the clock or the status icons.
  const auto& items = m_strip.items();
  if (items.empty()) return;
  int sel = m_strip.selection();
  if (sel < 0 || sel >= (int)items.size()) return;
  const std::string& cur = items[sel].label;

  const int gd = 30, gap = 12, pad = 16;
  int nameW = c.textWidth(cur.c_str(), kFontSmall);
  if (nameW > 240) nameW = 240;
  int w = pad + gd + gap + nameW + gap + gd + pad;
  int x = (Canvas::W - w) / 2;
  const int y = 10, h = 38;
  c.pill(x, y, w, h, kPillDark);

  int cx = x + pad + gd / 2;
  ui::buttonGlyph(c, cx, y + h / 2, gd, "L", false, 0x45454E, kPillText);
  cx += gd / 2 + gap;
  c.textCenterClipped(cx + nameW / 2, y + (h - c.textHeight(kFontSmall)) / 2,
                      nameW, cur.c_str(), kPillText, kFontSmall);
  cx += nameW + gap;
  ui::buttonGlyph(c, cx + gd / 2, y + h / 2, gd, "R", false, 0x45454E,
                  kPillText);
}

void Shell::drawGrid(Canvas& c, const std::vector<ui::StripItem>& items,
                     int sel, bool focused, int perRow, int cellW, int cellH,
                     int y) {
  int total = (int)items.size();
  if (total == 0) {
    c.textCenter(Canvas::W / 2, y + 80, "Nothing here yet", kTextFaint,
                 kFontMedium);
    return;
  }
  int rows = (total + perRow - 1) / perRow;
  int gridW = std::min(perRow, total) * cellW + (std::min(perRow, total) - 1) * kGridGapX;
  int x0 = (Canvas::W - gridW) / 2;

  for (int i = 0; i < total; ++i) {
    int col = i % perRow, row = i / perRow;
    int cx = x0 + col * (cellW + kGridGapX);
    int cy = y + row * (cellH + kGridGapY);
    bool isSel = i == sel;
    // tile
    c.roundRect(cx, cy, cellW, cellH, 16, kCardFill);
    c.roundRectOutline(cx, cy, cellW, cellH, 16,
                       isSel && focused ? accent() : kGridLineMajor,
                       isSel && focused ? 4 : 2);
    // icon + name (labels matter for the settings categories)
    if (items[i].icon) {
      int iw = items[i].icon->w, ih = items[i].icon->h;
      int maxW = cellW - 36, maxH = cellH - 72;
      float k = std::min((float)maxW / iw, (float)maxH / ih);
      if (k > 1.f) k = 1.f;
      int dw = (int)(iw * k), dh = (int)(ih * k);
      SDL_Rect dst{cx + (cellW - dw) / 2, cy + 20, dw, dh};
      c.markRegionDirty(dst.x, dst.y, dst.w, dst.h);
      SDL_BlitScaled(items[i].icon, nullptr, c.surface(), &dst);
      c.textCenterClipped(cx + cellW / 2, cy + cellH - 42, cellW - 20,
                          items[i].label.c_str(), kPanelInk, kFontSmall);
    } else {
      uint32_t tint = items[i].tint ? items[i].tint : kChromeDark;
      c.roundRect(cx + cellW / 2 - 40, cy + 24, 80, 80, 18, tint);
      c.roundRect(cx + cellW / 2 - 26, cy + 38, 52, 52, 12,
                  kWhite);
      c.roundRect(cx + cellW / 2 - 16, cy + 48, 32, 32, 8, tint);
    }
    (void)rows;
  }
}

void Shell::drawContent(Canvas& c) {
  // The page content is the BIG area at the top; the sections/context
  // carousel is the small strip at the bottom. Inside Games the content is
  // the 3D cartridge carousel (rasterized over the UI in drawOverlay()).
  if (m_mode == Mode::Games) {
    int cw = (int)(Canvas::W * 0.94f);
    int cx = (Canvas::W - cw) / 2;
    int cy = kContentY + 4;
    int ch = contentBottom() - cy;  // keep clear of the name pill + strip
    m_canvas.setCarouselRect(cx, cy, cw, ch);
    m_canvas.setCartDest(cx, cy, cw, ch);
    // erase the previously baked carts (save-under) before the new composite
    if (m_cartBgValid) {
      int px0 = m_canvas.cartDmgPX0(), py0 = m_canvas.cartDmgPY0();
      int px1 = m_canvas.cartDmgPX1(), py1 = m_canvas.cartDmgPY1();
      if (px1 > px0 && py1 > py0) {
        // only the previous carts' bbox needs the damage report
        float sx = (float)cw / m_canvas.cartSurfaceW();
        float sy = (float)ch / m_canvas.cartSurfaceH();
        int dx = cx + (int)(px0 * sx) - 16;
        int dy = cy + (int)(py0 * sy) - 16;
        int dw = (int)((px1 - px0) * sx) + 32;
        int dh = (int)((py1 - py0) * sy) + 32;
        m_canvas.restoreRegion(cx, cy, cw, ch, m_cartBg, dx, dy, dw, dh);
      } else {
        m_canvas.restoreRegion(cx, cy, cw, ch, m_cartBg, cx, cy, cw, ch);
      }
    } else {
      m_bg.drawRows(c, (float)m_lastBgX, (float)m_lastBgY, cy, cy + ch);
      m_canvas.saveRegion(cx, cy, cw, ch, m_cartBg);
      m_cartBgValid = true;
    }
    // content repaints wipe the baked carts: re-render them with the page
    m_carousel.invalidate();
    if (m_systemGames.empty() && m_showcase.empty())
      c.textCenter(Canvas::W / 2, kContentY + kContentH / 2,
                   m_strip.count() == 0 ? "No systems found" : "No games",
                   kTextFaint, kFontMedium);
    return;
  }

  m_canvas.setCarouselRect(0, 0, 0, 0);

  if (m_mode == Mode::Home) {
    // wider + thinner panel, padded below the top bar and above the Home pill
    int w = (int)(Canvas::W * 0.80f);
    int h = 330;  // taller box: the bottom edge extends, the top stays put
    int x = (Canvas::W - w) / 2;
    int y = kContentY + 40;
    Settings& st = Settings::instance();
    m_home.draw(c, x, y, w, h, st.getBoolDefault("home_clock", true),
                st.getBoolDefault("home_calendar", true));
    // time-of-day greeting under the panel (single line, clamped)
    if (st.getBoolDefault("home_greeting", true)) {
      time_t now = time(nullptr);
      struct tm lt{};
      localtime_r(&now, &lt);
      int hr = lt.tm_hour;
      const char* g1, *g2;
      if (hr >= 5 && hr < 12) {
        g1 = "Good Morning!";
        g2 = "Rise and shine!";
      } else if (hr >= 12 && hr < 18) {
        g1 = "Hello!";
        g2 = "Welcome back!";
      } else if (hr >= 18 && hr < 22) {
        g1 = "Good Evening!";
        g2 = "Welcome back!";
      } else {
        g1 = "Good Night!";
        g2 = "Time to rest!";
      }
      const char* greet = ((lt.tm_min / 30) & 1) ? g2 : g1;
      c.textCenterClipped(Canvas::W / 2, y + h + 52, w - 60, greet, kPanelInk,
                          kFontLarge);
      if (greet != m_lastGreeting) {
        // repaint the whole band: the old glyphs were alpha-blended over
        m_lastGreeting = greet;
        m_pageClear = true;
        m_contentDirty = true;
      }
    }
    return;
  }

  if (m_mode == Mode::Apps) {
    drawAppsView(c);
    return;
  }

  if (m_mode == Mode::Settings) {
    // exactly the Apps grid layout: 4 columns x 2 rows per page
    std::vector<ui::StripItem> cats = categoryItems();
    const int cols = 4, rows = 2, perPage = cols * rows;
    const int cellW = 210, cellH = 168, gapX = 26, gapY = 20;
    int pages = ((int)cats.size() + perPage - 1) / perPage;
    int page = m_catSel / perPage;
    if (page >= pages) page = pages - 1;
    if (page < 0) page = 0;
    int gridW = cols * cellW + (cols - 1) * gapX;
    int x0 = (Canvas::W - gridW) / 2;
    int y0 = kContentY + 34;
    for (int i = 0; i < perPage; ++i) {
      int idx = page * perPage + i;
      if (idx >= (int)cats.size()) break;
      int col = i % cols, row = i / cols;
      int cx = x0 + col * (cellW + gapX);
      int cy = y0 + row * (cellH + gapY);
      bool sel = idx == m_catSel;
      c.roundRect(cx, cy, cellW, cellH, 18, kCardFill);
      c.roundRectOutline(cx, cy, cellW, cellH, 18,
                         sel && m_contentFocus ? kFocusBlue : kGridLineMajor,
                         sel && m_contentFocus ? 4 : 2);
      if (cats[idx].icon) {
        int iw = cats[idx].icon->w, ih = cats[idx].icon->h;
        int maxW = cellW - 36, maxH = cellH - 72;
        float k = std::min((float)maxW / iw, (float)maxH / ih);
        if (k > 1.f) k = 1.f;
        int dw = (int)(iw * k), dh = (int)(ih * k);
        SDL_Rect dst{cx + (cellW - dw) / 2, cy + 20, dw, dh};
        c.markRegionDirty(dst.x, dst.y, dst.w, dst.h);
        SDL_BlitScaled(cats[idx].icon, nullptr, c.surface(), &dst);
      }
      c.textCenterClipped(cx + cellW / 2, cy + cellH - 42, cellW - 20,
                          cats[idx].label.c_str(), kPanelInk, kFontSmall);
    }
    if (pages > 1) {
      std::string pg = fmt("%d / %d", page + 1, pages);
      int pw = c.textWidth(pg.c_str(), kFontTiny) + 40;
      c.pill((Canvas::W - pw) / 2, y0 + rows * (cellH + gapY) + 4, pw, 32,
             kPillDark);
      c.textCenterVC(Canvas::W / 2, y0 + rows * (cellH + gapY) + 20,
                     pg.c_str(), kPillText, kFontTiny);
    }
    return;
  }
}

void Shell::drawContentGridPlaceholder(Canvas& c, const std::string& what) {
  c.roundRect((Canvas::W - 700) / 2, kContentY + 60, 700, 240, 18, kCardFill);
  c.roundRectOutline((Canvas::W - 700) / 2, kContentY + 60, 700, 240, 18,
                     kGridLineMajor, 3);
  c.textCenter(Canvas::W / 2, kContentY + 130, what.c_str(), kPanelInk,
               kFontLarge);
  c.textCenter(Canvas::W / 2, kContentY + 210, "(coming in the next phase)",
               kTextFaint, kFontSmall);
}

std::string Shell::nameForDisplay() const {
  if (!m_status.empty()) return m_status;
  // the root level shows the section name ("Games"); only inside a console
  // list does the pill show the focused game's name
  if (m_mode == Mode::Games && m_level == Level::Context) {
    const Game* g = focusedGame();
    if (g && !g->name.empty()) return g->name;
  }
  if (m_strip.count() > 0) return m_strip.items()[m_strip.selection()].label;
  return "";
}

void Shell::tickNameMarquee() {
  // overlays are static: freeze the marquee so it cannot repaint its band
  // over the modal (that caused flicker + constant redraws)
  if (m_modal != Modal::None || m_kbOpen) {
    m_marqueeDir = 0;
    m_pillDirty = false;
    return;
  }
  // inner width of the (bounded) name pill: mirrors the drawing code so the
  // ellipsis/marquee decision is identical in both places
  const int maxInner = (Canvas::W - 200) - 22 * 2 - (30 + 16);
  if (!bigGames()) {
    if (!m_marqueeName.empty()) {
      m_marqueeName.clear();
      m_marqueeOff = 0.f;
      m_marqueeDir = 0;
    }
    return;
  }
  std::string name = nameForDisplay();
  if (name != m_marqueeName) {
    m_marqueeName = name;
    if (m_nameSurf) SDL_FreeSurface(m_nameSurf);
    m_nameSurf = m_canvas.renderTextSurface(name.c_str(), kFontMedium,
                                            kPanelInk);
    m_nameSurfW = m_nameSurf ? m_nameSurf->w : 0;
    // "name..." form shown while the pill is parked at the start
    std::string shortName = name;
    if (m_canvas.textWidth(name.c_str(), kFontMedium) <= maxInner) {
      shortName.clear();
    } else {
      while (!shortName.empty() &&
             m_canvas.textWidth((shortName + "...").c_str(), kFontMedium) >
                 maxInner)
        shortName.pop_back();
      shortName += "...";
    }
    if (m_nameShort) SDL_FreeSurface(m_nameShort);
    m_nameShort = shortName.empty()
                      ? nullptr
                      : m_canvas.renderTextSurface(shortName.c_str(),
                                                   kFontMedium, kPanelInk);
    m_nameShortW = m_nameShort ? m_nameShort->w : 0;
    static const bool dbg = getenv("NDS_MARQDBG") != nullptr;
    if (dbg)
      LOG_INFO("marquee: name=%zu px maxInner=%d short='%s' shortW=%d", 
               (size_t)m_nameSurfW, maxInner, shortName.c_str(), m_nameShortW);
    m_marqueeOff = 0.f;
    m_marqueeDir = 0;
    m_marqueeAt = SDL_GetTicks() + 1200;  // show "<name>..." first
  }
  if (!m_nameSurf) return;
  int maxOff = m_nameSurfW - maxInner;
  if (maxOff <= 0) {
    if (m_marqueeOff != 0.f) {
      m_marqueeOff = 0.f;
      m_marqueeDir = 0;
      m_pillDirty = true;
    }
    return;
  }
  Uint32 now = SDL_GetTicks();
  if (now < m_marqueeAt) return;  // paused at one of the ends
  if (m_marqueeDir == 0) {
    // start moving: from the start we go reveal the end, from the end back
    m_marqueeDir = (m_marqueeOff >= (float)maxOff - 0.5f) ? -1 : 1;
  }
  const float kSpeed = 38.f;  // px/s - slow, low-power ping-pong
  float dt = m_marqueeLastMs ? (now - m_marqueeLastMs) / 1000.f : 0.f;
  m_marqueeLastMs = now;
  if (dt > 0.1f) dt = 0.1f;
  if (dt <= 0.f) return;
  m_marqueeOff += kSpeed * dt * m_marqueeDir;
  if (m_marqueeDir > 0 && m_marqueeOff >= maxOff) {
    m_marqueeOff = (float)maxOff;
    m_marqueeDir = 0;
    m_marqueeAt = now + 1000;  // end reached: pause, then bounce back
    m_marqueeLastMs = 0;
  } else if (m_marqueeDir < 0 && m_marqueeOff <= 0.f) {
    m_marqueeOff = 0.f;
    m_marqueeDir = 0;
    m_marqueeAt = now + 1200;  // back at the start: pause, then again
    m_marqueeLastMs = 0;
  }
  m_pillDirty = true;
}

void Shell::drawNameLabel(Canvas& c) {
  std::string name = nameForDisplay();
  if (name.empty()) return;
  const bool big = bigGames();
  const int pad = big ? 22 : 28;
  const int gd = 30;
  const int glyphZone = big ? (gd + 16) : 0;
  int textW = big && m_nameSurf ? m_nameSurfW
                                : c.textWidth(name.c_str(), kFontMedium);
  int maxW = big ? (Canvas::W - 200) : (Canvas::W - 120);
  int w = std::min(maxW, textW + pad * 2 + glyphZone);
  int h = big ? labelHeight() : 38;
  int ly = big ? labelTop() : (labelTop() - 44);  // clear of the strip band
  int x = (Canvas::W - w) / 2;
  c.pill(x, ly, w, h, kCardFill);
  // focus outline: light blue while the model (this pill) owns the focus
  bool focused = big && m_contentFocus;
  c.pillOutline(x, ly, w, h, focused ? kFocusBlue : kGridLineMajor,
                focused ? 4 : 2);

  int textX = x + pad;
  if (big) {
    ui::buttonGlyph(c, x + pad + gd / 2, ly + h / 2, gd, "A", true, kGlyphInk,
                    kWhite);
    textX += glyphZone;
  }
  if (big) {
    // clipped blit so a long name cannot leave the pill; scrolled by the
    // marquee when it does not fit. innerW mirrors tickNameMarquee(): the
    // pill hugs short names, so the inner width is capped by the text width.
    const int maxInner = (Canvas::W - 200) - pad * 2 - glyphZone;
    int innerW = std::min(maxInner, m_nameSurfW);
    bool overflow = m_nameSurfW > maxInner;
    SDL_Surface* surf = (overflow && m_nameShort && m_marqueeOff < 4.f)
                            ? m_nameShort
                            : m_nameSurf;
    if (surf) {
      int sw = surf == m_nameShort ? m_nameShortW : m_nameSurfW;
      int tx = textX;
      if (!overflow || surf == m_nameShort) tx += (innerW - sw) / 2;
      else tx -= (int)m_marqueeOff;
      SDL_Rect clip{textX, ly, innerW, h};
      SDL_SetClipRect(c.surface(), &clip);
      SDL_Rect dst{tx, ly + (h - surf->h) / 2, surf->w, surf->h};
      SDL_BlitSurface(surf, nullptr, c.surface(), &dst);
      SDL_SetClipRect(c.surface(), nullptr);
      c.markRegionDirty(x, ly, w, h);
    }
  } else {
    // compact pill: still clipped so a long name stays inside the viewport,
    // vertically centred on the pill
    int ty = ly + (h - c.textHeight(kFontMedium)) / 2 - 2;
    if (c.textWidth(name.c_str(), kFontMedium) <= w - 30)
      c.textCenterVC(Canvas::W / 2, ly + h / 2, name.c_str(), kPanelInk,
                     kFontMedium);
    else
      c.textCenterClipped(Canvas::W / 2, ty, w - 30, name.c_str(), kPanelInk,
                          kFontMedium);
  }
}

void Shell::drawHelpBar(Canvas& c) {
  if (!bigGames()) return;
  // left: [L2][R2] + [dpad] : Rotate      right: [X] : Options
  const int gd = 26;
  int y = kHelpY + kHelpH / 2;
  {
    int x = 24;
    const char* left = "Rotate";
    int tw = c.textWidth(left, kFontSmall);
    int w = 12 + gd * 2 + 8 + 26 + 14 + 6 + 14 + tw + 14;
    c.pill(x, kHelpY, w, kHelpH, kPillDark);
    int cx = x + 12 + gd / 2;
    ui::buttonGlyph(c, cx, y, gd, "L2", false, 0x45454E, kPillText);
    cx += gd + 4;
    ui::buttonGlyph(c, cx, y, gd, "R2", false, 0x45454E, kPillText);
    cx += gd / 2 + 8;
    ui::dpadGlyph(c, cx + 13, y, 26, kPillText);
    cx += 26 + 14;
    c.textVC(cx, y, ":", kPillText, kFontSmall);
    cx += 8 + 6;
    c.textVC(cx, y, left, kPillText, kFontSmall);
  }
  {
    const char* right = "Options";
    int tw = c.textWidth(right, kFontSmall);
    int w = 12 + gd + 14 + 6 + 14 + tw + 14;
    int x = Canvas::W - 24 - w;
    c.pill(x, kHelpY, w, kHelpH, kPillDark);
    int cx = x + 12 + gd / 2;
    ui::buttonGlyph(c, cx, y, gd, "X", true, 0xF0F0F0, kPanelInk);
    cx += gd / 2 + 14;
    c.textVC(cx, y, ":", kPillText, kFontSmall);
    cx += 8 + 6;
    c.textVC(cx, y, right, kPillText, kFontSmall);
  }
}


namespace {

bool writeSysfs(const char* path, const std::string& value) {
  FILE* f = fopen(path, "w");
  if (!f) return false;
  fputs(value.c_str(), f);
  fclose(f);
  return true;
}

bool readSysfs(const char* path, std::string* out) {
  FILE* f = fopen(path, "r");
  if (!f) return false;
  char buf[256] = {0};
  if (fgets(buf, sizeof(buf), f)) *out = trim(buf);
  fclose(f);
  return true;
}

}  // namespace

void Shell::applyLedFromSettings() {
  Settings& st = Settings::instance();
  net::ledApply(st.getBoolDefault("led_enable", false),
                st.getIntDefault("led_effect", 0),
                st.getIntDefault("led_color", 0),
                st.getIntDefault("led_scale_m", 30));
}

std::string Shell::rowValue(const SettingRow& r) const {
  Settings& st = Settings::instance();
  if (r.kind == SettingRow::Action && !r.choices.empty())
    return r.choices[0];  // dynamic rows carry their value text here
  // network rows report the LIVE device state, not a stored intent
  if (r.key == "wifi_on") {
    if (!net::wifiEnabled()) return "Off";
    std::string ssid = net::wifiSsid();
    return ssid.empty() ? "On" : "On - " + ssid;
  }
  if (r.key == "bt_on") return net::btEnabled() ? "On" : "Off";
  switch (r.kind) {
    case SettingRow::Toggle:
      return st.getBoolDefault(r.key, r.max != 0) ? "On" : "Off";
    case SettingRow::Int:
      return fmt("%d", st.getIntDefault(r.key, r.min));
    case SettingRow::Choice: {
      int idx = st.getIntDefault(r.key, 0);
      if (idx < 0 || idx >= (int)r.choices.size()) idx = 0;
      return r.choices.empty() ? "-" : r.choices[idx];
    }
    case SettingRow::Action:
      return "";
  }
  return "";
}

void Shell::adjustRow(SettingRow& r, int dir) {
  Settings& st = Settings::instance();
  switch (r.kind) {
    case SettingRow::Toggle:
      st.setBool(r.key, !st.getBoolDefault(r.key, r.max != 0));
      break;
    case SettingRow::Int: {
      int v = st.getIntDefault(r.key, r.min) + dir;
      v = std::clamp(v, r.min, r.max);
      st.setInt(r.key, v);
      break;
    }
    case SettingRow::Choice: {
      int n = (int)r.choices.size();
      if (n == 0) break;
      int idx = std::clamp(st.getIntDefault(r.key, 0) + dir, 0, n - 1);
      st.setInt(r.key, idx);
      break;
    }
    case SettingRow::Action:
      break;
  }
  st.save();
  // NDSUI does not apply device settings any more (display, LEDs, sound,
  // rumble, radios are all owned by the stock OSD / MainUI). The code is kept
  // for reference and can be re-enabled when the boot hook lands.
  if (r.key == "default_launcher") {
    buildSettingRows();              // Quit row appears/hides
    syncDefaultLauncherMarker();     // boot hook follows the setting
  }
  // DISABLED: volume -> net::setVolumePercent
  // DISABLED: rumble -> net::pulseRumble
  // DISABLED: led_* -> applyLedFromSettings
  // DISABLED: brightness/color_temp/contrast/saturation/exposure -> display
  // DISABLED: wifi_on / bt_on -> setWifiEnabled / setBtEnabled
  if (r.key == "theme_dark") {
    applyTheme(st.getBoolDefault("theme_dark", false));
    // drop the tinted icon cache WITHOUT freeing: the strips still reference
    // the old surfaces until they are rebuilt below (freeing crashed)
    m_themedIcons.clear();
    // Rebuild the strip that is actually on screen. NEVER buildCategories()
    // here: that swaps the sections strip for the category tiles while the
    // shell is still at Section level, so the next strip move re-interprets
    // the selection as a section and jumps to a random page (the "carousel
    // showed the settings sections" glitch).
    if (m_level == Level::Context && m_mode == Mode::Games)
      buildSystemStrip();
    else
      buildSections();
    m_ctx.dirty = true;
    m_pageClear = true;
  }
  m_contentDirty = true;
  m_stripDirty = true;
  m_topDirty = true;
  m_ctx.dirty = true;
}

void Shell::activateRow(const SettingRow& r) {
  Settings& st = Settings::instance();
  if (r.kind == SettingRow::Action) {
    if (r.action == "refresh_roms") {
      refreshLibrary();
    } else if (r.action == "poweroff") {
      m_status = "Powering off...";
      m_statusUntil = SDL_GetTicks() + 2000;
      system("sync; /sbin/poweroff");
    } else if (r.action == "quit") {
      m_pendingQuit = true;
    } else if (r.action == "reboot") {
      m_status = "Rebooting...";
      m_statusUntil = SDL_GetTicks() + 2000;
      system("sync; /sbin/reboot");
    } else if (r.action == "wifi_scan") {
      net::startWifiScan();
      m_status = "Scanning Wi-Fi...";
      m_statusUntil = SDL_GetTicks() + 3000;
    } else if (r.action == "bt_scan") {
      net::btScan();
      m_status = "Scanning Bluetooth...";
      m_statusUntil = SDL_GetTicks() + 12000;
    } else if (r.action.rfind("wifi:", 0) == 0) {
      const std::string& a = r.action;
      size_t c1 = a.find(':', 5);
      std::string ssid =
          a.substr(5, c1 == std::string::npos ? std::string::npos : c1 - 5);
      std::string kind = c1 == std::string::npos ? "" : a.substr(c1 + 1);
      if (r.key == "hl") {
        net::wifiDisconnect();
        m_status = "Disconnected from " + ssid;
      } else if (kind == "psk") {
        openTextKeyboard("Password for " + ssid, "wifi:" + ssid);
        return;
      } else {
        net::wifiConnect(ssid, "", false);
        m_status = "Connecting to " + ssid + "...";
      }
      m_statusUntil = SDL_GetTicks() + 2000;
      m_ctx.dirty = true;
    } else if (r.action.rfind("bt:", 0) == 0) {
      const std::string& a = r.action;
      size_t c1 = a.find(':', 3);
      std::string mac =
          a.substr(3, c1 == std::string::npos ? std::string::npos : c1 - 3);
      std::string kind = c1 == std::string::npos ? "" : a.substr(c1 + 1);
      if (kind == "disconnect")
        net::btDisconnect(mac);
      else
        net::btConnect(mac);
      m_status = "Bluetooth: " + kind;
      m_statusUntil = SDL_GetTicks() + 2000;
      m_ctx.dirty = true;
    } else if (r.action == "reset_category") {
      for (const SettingRow& row : m_rows) {
        if (row.key.empty()) continue;
        switch (row.kind) {
          case SettingRow::Toggle:
            Settings::instance().setBool(row.key, row.def != 0);
            break;
          case SettingRow::Int:
            Settings::instance().setInt(row.key, row.def);
            break;
          case SettingRow::Choice:
            Settings::instance().setInt(row.key, 0);
            break;
          default:
            break;
        }
      }
      Settings::instance().save();
      // DISABLED: applyLedFromSettings(); net::displayApplyFromSettings();
      buildSettingRows();
      m_pageClear = true;
      m_status = "Defaults restored";
      m_statusUntil = SDL_GetTicks() + 1600;
      m_ctx.dirty = true;
    } else if (r.action == "device_info") {
      m_status = fmt("NDSUI - %d systems, %d games", (int)m_ctx.systems.size(),
                     (int)m_ctx.games.size());
      m_statusUntil = SDL_GetTicks() + 2600;
    }
    m_ctx.dirty = true;
    return;
  }
  // toggles / ints / choices also respond to A
  SettingRow copy = r;
  int dir = (r.kind == SettingRow::Int || r.kind == SettingRow::Choice) ? 1 : 0;
  adjustRow(copy, dir);
}

namespace {

std::string procField(const char* path, const char* key) {
  FILE* f = fopen(path, "r");
  if (!f) return "";
  char line[256];
  std::string val;
  while (fgets(line, sizeof(line), f)) {
    std::string l = trim(line);
    if (l.rfind(key, 0) == 0) {
      size_t c = l.find(':');
      if (c != std::string::npos) {
        val = trim(l.substr(c + 1));
        break;
      }
    }
  }
  fclose(f);
  return val;
}

std::string cpuName() {
  std::string hw = procField("/proc/cpuinfo", "Hardware");
  if (!hw.empty()) return hw;
  std::string mn = procField("/proc/cpuinfo", "model name");
  return mn.empty() ? "unknown" : mn;
}

std::string ramText() {
  long kb = atol(procField("/proc/meminfo", "MemTotal").c_str());
  if (kb <= 0) return "unknown";
  char b[32];
  snprintf(b, sizeof(b), "%.1f GB", kb / 1048576.0);
  return b;
}

std::string storageText() {
  dev::Storage s = dev::storage();
  char b[48];
  snprintf(b, sizeof(b), "%.1f / %.1f GB free", s.freeMB / 1024.0,
           s.totalMB / 1024.0);
  return b;
}

}  // namespace

void Shell::buildSettingRows() {
  Settings& st = Settings::instance();
  m_rows.clear();
  const int cat = m_catSel;
  const char* catIcon = "ic-system.png";
  switch (cat) {
    case 0: catIcon = "ic-theme.png"; break;
    case 1: catIcon = "ic-homepage.png"; break;
    case 2: catIcon = "ic-system.png"; break;
    default: catIcon = "ic-system.png"; break;
  }
  auto toggle = [&](const char* label, const char* key, bool def) {
    SettingRow r;
    r.kind = SettingRow::Toggle;
    r.label = label;
    r.key = key;
    r.icon = catIcon;
    r.def = def ? 1 : 0;
    r.max = def ? 1 : 0;
    m_rows.push_back(r);
  };
  auto integer = [&](const char* label, const char* key, int lo, int hi,
                     int def) {
    SettingRow r;
    r.kind = SettingRow::Int;
    r.label = label;
    r.key = key;
    r.icon = catIcon;
    r.min = lo;
    r.max = hi;
    r.def = def;
    st.setInt(key, st.getIntDefault(key, def));  // seed defaults
    m_rows.push_back(r);
  };
  auto choice = [&](const char* label, const char* key,
                    std::vector<std::string> opts) {
    SettingRow r;
    r.kind = SettingRow::Choice;
    r.label = label;
    r.key = key;
    r.icon = catIcon;
    r.choices = std::move(opts);
    m_rows.push_back(r);
  };
  auto action = [&](const char* label, const char* act) {
    SettingRow r;
    r.kind = SettingRow::Action;
    r.label = label;
    r.action = act;
    r.icon = catIcon;
    m_rows.push_back(r);
  };
  auto info = [&](const char* label, const std::string& value) {
    SettingRow r;
    r.kind = SettingRow::Action;
    r.label = label;
    r.icon = catIcon;
    if (!value.empty()) r.choices.push_back(value);  // read-only value text
    m_rows.push_back(r);
  };

  auto netRow = [&](const char* label, const std::string& act,
                    const std::string& sub, bool highlight) {
    SettingRow r;
    r.kind = SettingRow::Action;
    r.label = label;
    r.action = act;
    r.icon = catIcon;
    r.choices.clear();
    r.key = highlight ? "hl" : "";
    if (!sub.empty()) r.choices.push_back(sub);  // value text (hack: 1 choice)
    m_rows.push_back(r);
  };
  switch (cat) {
    case 0:  // Theme
      toggle("Dark Mode", "theme_dark", false);
      choice("Clock Format", "clock_24h", {"24 Hour", "12 Hour"});
      break;
    case 1:  // Homepage
      toggle("Show Clock", "home_clock", true);
      toggle("Show Calendar", "home_calendar", true);
      toggle("Show Greeting", "home_greeting", true);
      break;
    case 2:  // System
      // MainUI is the default launcher until the boot hook lands
      st.setInt("default_launcher", st.getIntDefault("default_launcher", 1));
      choice("Default Launcher", "default_launcher", {"NDSUI", "MainUI"});
      if (st.getIntDefault("default_launcher", 1) == 1)
        action("Quit NDSUI", "quit");
      action("Power Off", "poweroff");
      break;
    default:
      break;
  }
  // DISABLED: per-category Reset to Defaults row (settings are now minimal)
  // {
  //   SettingRow r;
  //   r.kind = SettingRow::Action;
  //   r.label = "Reset to Defaults";
  //   r.action = "reset_category";
  //   r.icon = catIcon;
  //   m_rows.push_back(r);
  // }
  if (m_rowSel >= (int)m_rows.size()) m_rowSel = std::max(0, (int)m_rows.size() - 1);
}

void Shell::drawSettingsRows(Canvas& c) {
  int w = 700;
  int x = (Canvas::W - w) / 2;
  int y = kContentY + 30;
  int rows = (int)m_rows.size();
  int rowH = 64;
  int h = 30 + rows * rowH + 30;
  if (h > kContentH - 60) h = kContentH - 60;
  c.roundRect(x, y, w, h, 20, kCardFill);
  c.roundRectOutline(x, y, w, h, 20,
                     m_contentFocus ? kFocusBlue : kGridLineMajor,
                     m_contentFocus ? 4 : 2);
  for (int i = 0; i < rows; ++i) {
    int ry = y + 26 + i * rowH;
    if (ry + rowH > y + h) break;
    bool sel = i == m_rowSel;
    if (sel)
      c.roundRect(x + 14, ry - 6, w - 28, rowH - 8, 14,
                  m_contentFocus ? kRowSel : kPanelAlt);
    int textX = x + 34;
    if (!m_rows[i].icon.empty()) {
      SDL_Surface* ic =
          m_rows[i].icon == "bulb"
              ? glyphSurface("bulb", 30, kTextDim)
              : themedIcon(
                    joinPath(joinPath(assetDir(), "skin"), m_rows[i].icon),
                    30);
      if (ic) {
        SDL_Rect dst{x + 30, ry + (rowH - 8) / 2 - ic->h / 2, ic->w, ic->h};
        c.markRegionDirty(dst.x, dst.y, dst.w, dst.h);
        SDL_BlitScaled(ic, nullptr, c.surface(), &dst);
      }
      textX = x + 72;
    }
    c.textVC(textX, ry + (rowH - 8) / 2, m_rows[i].label.c_str(), kPanelInk,
             kFontSmall);
    std::string val = rowValue(m_rows[i]);
    if (!val.empty()) {
      uint32_t col = kTextDim;
      if (m_rows[i].kind == SettingRow::Toggle)
        col = val == "On" ? 0x3E9E5A : kTextDim;
      c.textRightVC(x + w - 40, ry + (rowH - 8) / 2, val.c_str(), col,
                    kFontSmall);
      if (m_rows[i].kind == SettingRow::Int ||
          m_rows[i].kind == SettingRow::Choice) {
        // little left/right affordance
        c.textRightVC(x + w - 40 - c.textWidth(val.c_str(), kFontSmall) - 14,
                      ry + (rowH - 8) / 2, "< >", kTextFaint, kFontTiny);
      }
    }
  }
}



// ---------------------------------------------------------------- net lists

void Shell::openNetList(ListKind k) {
  m_listKind = k;
  m_modal = Modal::NetList;
  m_modalSel = 0;
  m_listItems.clear();
  if (k == ListKind::Wifi) {
    m_listTitle = "Wi-Fi Networks";
    m_listHint = "A: connect / disconnect   X: forget";
    net::startWifiScan();
  } else {
    m_listTitle = "Bluetooth Devices";
    m_listHint = "A: connect / disconnect";
    net::btScan();
  }
  refreshNetList();
  m_ctx.dirty = true;
}

void Shell::refreshNetList() {
  m_listItems.clear();
  if (m_listKind == ListKind::Wifi) {
    bool conn = net::wifiConnected();
    std::string cur = net::wifiSsid();
    for (const net::WifiNetwork& n : net::wifiScanResults()) {
      ListItem it;
      it.label = n.ssid;
      it.sub = fmt("%d%%%s%s", n.signal, n.secure ? " *" : "",
                   n.known ? " saved" : "");
      it.highlight = n.current || (conn && n.ssid == cur);
      // a saved network connects straight away (no password prompt)
      it.act = "wifi:" + n.ssid +
               (!n.secure ? ":open" : (n.known ? ":saved" : ":psk"));
      m_listItems.push_back(it);
    }
  } else {
    for (const net::BtDevice& d : net::btDevices()) {
      ListItem it;
      it.label = d.name.empty() ? d.mac : d.name;
      it.sub = d.connected ? "connected" : (d.paired ? "paired" : d.mac);
      it.highlight = d.connected;
      it.act = "bt:" + d.mac + (d.connected ? ":disconnect" : ":connect");
      m_listItems.push_back(it);
    }
  }
}

void Shell::listActivate() {
  if (m_modalSel < 0 || m_modalSel >= (int)m_listItems.size()) return;
  const std::string act = m_listItems[m_modalSel].act;
  if (act.rfind("wifi:", 0) == 0) {
    // wifi:<ssid>:psk|open  (or the current network -> disconnect)
    size_t a = act.find(':', 5);
    std::string ssid = act.substr(5, a == std::string::npos ? std::string::npos
                                                            : a - 5);
    std::string kind = a == std::string::npos ? "" : act.substr(a + 1);
    if (m_listItems[m_modalSel].highlight) {
      net::wifiDisconnect();
      m_status = "Disconnected from " + ssid;
    } else if (kind == "psk") {
      // ask for the password first
      openTextKeyboard("Password for " + ssid, "wifi:" + ssid);
      return;
    } else {
      net::wifiConnect(ssid, "", false);  // open or saved network
      m_status = "Connecting to " + ssid + "...";
    }
    m_statusUntil = SDL_GetTicks() + 2000;
    closeModal();
    m_ctx.dirty = true;
    return;
  }
  if (act.rfind("bt:", 0) == 0) {
    size_t a = act.find(':', 3);
    std::string mac = act.substr(3, a == std::string::npos ? std::string::npos
                                                           : a - 3);
    std::string kind = a == std::string::npos ? "" : act.substr(a + 1);
    if (kind == "disconnect")
      net::btDisconnect(mac);
    else
      net::btConnect(mac);
    m_status = "Bluetooth: " + kind;
    m_statusUntil = SDL_GetTicks() + 2000;
    closeModal();
    m_ctx.dirty = true;
  }
}

// ---------------------------------------------------------------- keyboard

void Shell::openTextKeyboard(const std::string& title, const std::string& tag) {
  m_kbOpen = true;
  m_kbText = true;
  m_kbTitle = title;
  m_kbTag = tag;
  m_kbBuf.clear();
  m_kbX = 0;
  m_kbY = 0;
  m_kbShift = false;
  m_kbInResults = false;
  m_ctx.dirty = true;
}

}  // namespace ndsui}  // namespace ndsui
