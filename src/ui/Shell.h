// Shell: the new NDSUI navigation model.
//
//   [top bar: time ......... volume wifi bt battery]
//   [content: Home clock/calendar | system grid | games | apps grid |
//             settings categories grid | settings items list]
//   [selected item name, centered]
//   [left-right icon strip: sections, or context (systems/categories)]
//   [switch-style pill help bar]
//
// Focus model: strip (L/R cycles) -> Up enters the content -> Down/Up at
// content edges -> B backs out one level. Parallax background follows the
// animated offsets.
#pragma once

#include <string>
#include <vector>

#include "screens/Screen.h"
#include "ui/Background.h"
#include "ui/Carousel.h"
#include "ui/HomeContent.h"
#include "ui/Strip.h"

namespace ndsui {

class Shell : public Screen {
 public:
  Shell(AppContext& ctx, Canvas& canvas);

  void draw(Canvas& c) override;
  ScreenResult handle(ActionType a) override;
  void tick() override;
  bool animating() const override;
  void setRotateAxis(int axis) override { m_rotateAxis = axis; }
  void setPitchAxis(int axis) override { m_pitchAxis = axis; }
  void invalidateAll() override;  // full repaint (canvas was re-created)
  void setModelOverride(const std::string& model) override {
    m_carousel.setModelOverride(model);
  }
  std::string title() const override { return "NDSUI"; }

  // development: jump straight to a mode for screenshots
  // ("home" | "games" | "games2" | "apps" | "settings" | "settings2")
  void debugSetMode(const std::string& name);
  void debugCategory(int cat);   // harness: pick a settings category
  void debugSelect(int idx);     // harness: pick a carousel item
  std::string debugState() const;  // harness: human readable state

 private:
  // where we are (order matches the section strip)
  enum class Mode { Home, Apps, Games, Settings };
  enum class Level { Sections, Context };  // Context = inside Games/Settings

  void buildSections();
  void buildSystemStrip();
  void buildCategories();
  void buildAppsGrid();
  void refreshApps();      // rescan internal + SD app roots
  // writes/removes the `use_as_launcher` marker the stock runtrimui.sh checks
  void syncDefaultLauncherMarker();
  void drawAppsView(Canvas& c);
  // --- settings rows ---
  struct SettingRow {
    enum Kind { Toggle, Int, Choice, Action } kind = Toggle;
    std::string label;
    std::string key;                 // settings key (values persist here)
    std::string action;              // for Action rows
    std::string icon;                // stock skin png or procedural glyph
    int min = 0, max = 1;
    int def = 0;                     // default value (Reset to defaults)
    std::vector<std::string> choices;  // for Choice rows
  };
  void buildSettingRows();
  void applyLedFromSettings();
  void drawSettingsRows(Canvas& c);
  void adjustRow(SettingRow& r, int dir);
  void activateRow(const SettingRow& r);
  std::string rowValue(const SettingRow& r) const;
  std::vector<SettingRow> m_rows;
  int m_rowSel = 0;
  void drawHintPill(Canvas& c, int x, int y, const char* glyph, bool circle,
                    const char* label, uint32_t fill);
  void refreshSystemGames();
  void refreshShowcase();          // one game per system (root Games page)
  void applyPendingMode();         // switch pages after the strip settles
  const Game* focusedGame() const;
  // returns by value: with no explicit emulator choice this is the system's
  // default script from config.json (not launchlist[0], which can be broken)
  LaunchOption focusedLaunch() const;
  const System* selectedSystem() const;
  std::vector<ui::StripItem> systemItems() const;
  std::vector<ui::StripItem> categoryItems() const;
  void syncModeFromStrip();

  // --- games-screen layout (bigger console chooser + taller name pill) ---
  bool bigGames() const {
    return m_mode == Mode::Games && m_level == Level::Context;
  }
  int labelTop() const;      // name pill top
  int labelHeight() const;
  int contentBottom() const; // bottom of the 3D model band
  int stripTop() const;
  int stripHeight() const;
  void drawTopConsolePill(Canvas& c);   // [L] console [R]
  void drawBolt(Canvas& c, int cx, int cy, uint32_t col);
  void tickNameMarquee();

  // --- pseudo consoles (Recent / Favorites) ---
  bool pseudoEntry(int stripIdx, std::vector<const Game*>* out) const;
  const Game* gameByPath(const std::string& path) const;
  SDL_Surface* glyphSurface(const char* kind, int size,
                            uint32_t col) const;
  // stock skin icon, inverted in dark mode (dark outlines are
  // invisible on the dark grid)
  SDL_Surface* themedIcon(const std::string& path, int size) const;
  mutable std::map<std::string, SDL_Surface*> m_themedIcons;
  mutable bool m_themedDark = false;

  // --- options modals ---
  enum class Modal { None, Chooser, Game, NetList, Settings };
  struct ModalItem {
    std::string label;
    std::string value;    // emulator script (Game modal)
    int launchIdx = -1;   // index into sys.launches, -2 = default script
  };
  void openChooserModal();
  void openGameModal();
  void closeModal();
  void modalActivate(ScreenResult& r);
  void drawModal(Canvas& c);
  bool handleModal(ActionType a, ScreenResult& r);
  bool handleSettingsModal(ActionType a, ScreenResult& r);
  void drawSettingsModal(Canvas& c);

  // --- network sub-lists (wifi / bluetooth) ---
  enum class ListKind { Wifi, Bt };
  struct ListItem {
    std::string label;  // ssid / device name
    std::string sub;    // signal / mac / state
    std::string act;    // action tag consumed by listActivate()
    bool highlight = false;
  };
  void openNetList(ListKind k);
  void refreshNetList();
  void listActivate();
  ListKind m_listKind = ListKind::Wifi;
  std::vector<ListItem> m_listItems;
  std::string m_listTitle, m_listHint;

  // --- search (keyboard) ---
  void openSearch();
  // arbitrary text entry (e.g. wifi password); `tag` decides what
  // OK does with the buffer ("wifi:<ssid>")
  void openTextKeyboard(const std::string& title, const std::string& tag);
  bool m_kbText = false;
  std::string m_kbTag, m_kbTitle;
  void closeSearch();
  bool handleSearch(ActionType a, ScreenResult& r);
  void drawSearch(Canvas& c);
  void updateSearchResults();

  // library refresh (options menu)
  void refreshLibrary();

  void enterMode(Mode m);
  void back();

  // drawing pieces
  void drawTopBar(Canvas& c);
  void drawContent(Canvas& c);
  void drawNameLabel(Canvas& c);
  void drawHelpBar(Canvas& c);
  void drawGrid(Canvas& c, const std::vector<ui::StripItem>& items, int sel,
                bool focused, int perRow, int cellW, int cellH, int y);
  void drawContentGridPlaceholder(Canvas& c, const std::string& what);

  uint32_t accent() const;

  AppContext& m_ctx;
  Canvas& m_canvas;
  ui::Background m_bg;
  ui::HomeContent m_home;
  ui::Strip m_strip;     // small context carousel (bottom)
  Carousel m_carousel;
  std::vector<const Game*> m_systemGames;
  std::vector<const Game*> m_showcase;   // mixed-system preview at the root
  int m_pendingMode = -1;                // deferred page switch (smooth scroll)
  int m_rotateAxis = 0;                  // continuous L2/R2 rotation input
  int m_pitchAxis = 0;                   // L2/R2 + up/down tilt input

  Mode m_mode = Mode::Home;
  Level m_level = Level::Sections;
  bool m_contentFocus = false;
  float m_focusAnim = 0.f;    // 0 = strip, 1 = content
  float m_slideAnim = 0.f;    // 0 = context level (grid), 1 = inside (strip)

  int m_systemSel = 0;

  int m_gameSel = 0;
  int m_catSel = 0;
  int m_itemSel = 0;
  int m_appSel = 0;
  int m_appPage = 0;
  struct AppEntry {
    std::string label;
    std::string dir;
    std::string launch;
    std::string iconPath;
  };
  std::vector<AppEntry> m_apps;
  std::vector<ui::StripItem> m_appItems;  // grid tiles (icons resolved)
  int m_contentScroll = 0;

  std::string m_status;
  Uint32 m_statusUntil = 0;
  // perf instrumentation (NDS_FPSLOG)
  uint32_t m_profBg = 0, m_profContent = 0, m_profStrip = 0;
  int m_profN = 0;
  uint32_t m_profLast = 0;
  int m_lastBgX = -1000, m_lastBgY = -1000;
  std::vector<unsigned char> m_cartBg;  // save-under for the carousel rect
  bool m_cartBgValid = false;
  bool m_contentDirty = true;
  bool m_topDirty = true;
  bool m_stripDirty = true;   // bottom carousel + name pill + help
  bool m_pageClear = true;    // wipe the content band once per page switch
  std::string m_lastName;
  std::string nameForDisplay() const;
  std::string m_clock, m_battery;
  bool m_wifiUp = false, m_btUp = false;
  Uint32 m_netPollAt = 0;
  Uint32 m_lastInputAt = 0;    // idle timer (screen timeout)
  bool m_asleep = false;
  bool m_pendingQuit = false;  // System > Quit NDSUI
  int m_volume = -1;

  // name-pill marquee (long game names)
  std::string m_marqueeName;   // name the state below belongs to
  SDL_Surface* m_nameSurf = nullptr;  // rendered name text (owned)
  int m_nameSurfW = 0;
  SDL_Surface* m_nameShort = nullptr;  // "name..." variant (owned)
  int m_nameShortW = 0;
  float m_marqueeOff = 0.f;    // current scroll offset (px)
  int m_marqueeDir = 0;        // 0 = paused, -1 = back, +1 = forward
  Uint32 m_marqueeAt = 0;      // when the current pause ends
  Uint32 m_marqueeLastMs = 0;  // dt for the scroll speed
  bool m_pillDirty = false;    // marquee moved: repaint just the pill band
  std::string m_lastGreeting;  // repaint the Home band when it changes

  // pseudo consoles: indices [0, m_pseudoCount) in the strip
  bool m_hasRecent = false;
  bool m_hasFav = false;
  int m_pseudoCount = 0;
  mutable std::map<std::string, SDL_Surface*> m_glyphs;  // icon cache

  // options modal
  Modal m_modal = Modal::None;
  int m_modalSel = 0;
  std::vector<ModalItem> m_modalItems;
  std::string m_modalTitle;

  // search overlay
  bool m_kbOpen = false;
  std::string m_kbBuf;
  int m_kbX = 0, m_kbY = 0;      // 0..3 letter rows, 4 = action row
  bool m_kbShift = false;
  bool m_kbInResults = false;    // focus zone
  std::vector<const Game*> m_search;
  int m_searchSel = 0;
  int m_searchScroll = 0;
};

}  // namespace ndsui
