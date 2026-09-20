#include "screens/GameListScreen.h"

#include <algorithm>
#include <ctime>

#include "core/Device.h"
#include "ui/Components.h"
#include "ui/Theme.h"
#include "util/Log.h"
#include "util/Platform.h"

namespace ndsui {

namespace {
constexpr int kListX = 16;
constexpr int kListW = 414;
constexpr int kDetailX = 446;
constexpr int kDetailW = 562;

std::string humanSize(long bytes) {
  if (bytes < 1024) return fmt("%ld B", bytes);
  if (bytes < 1024 * 1024) return fmt("%.1f KB", bytes / 1024.0);
  return fmt("%.1f MB", bytes / (1024.0 * 1024.0));
}

std::string timeStr(time_t t) {
  if (t <= 0) return "-";
  struct tm tmv;
  localtime_r(&t, &tmv);
  char buf[32];
  strftime(buf, sizeof(buf), "%d/%m %H:%M", &tmv);
  return buf;
}

std::string fileNameOf(const std::string& path) {
  size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}
}  // namespace

GameListScreen::GameListScreen(AppContext& ctx) : m_ctx(ctx) {
  Settings& st = Settings::instance();
  // restore last system (by id or pseudo-system tag), else first with games
  int restored = -1;
  if (st.lastSystem == "@recents" && !st.recents.empty()) {
    m_systemId = -2;
    rebuildFilter();
    return;
  }
  if (st.lastSystem == "@favorites" && !st.favorites.empty()) {
    m_systemId = -1;
    rebuildFilter();
    return;
  }
  for (size_t i = 0; i < ctx.systems.size(); ++i)
    if (!st.lastSystem.empty() && ctx.systems[i].id == st.lastSystem)
      restored = (int)i;
  if (restored >= 0) {
    m_systemId = restored;
  } else {
    for (size_t i = 0; i < ctx.systems.size(); ++i)
      if (ctx.systems[i].gameCount > 0) {
        m_systemId = (int)i;
        break;
      }
  }
  rebuildFilter();
  // restore last game selection
  if (!st.lastGame.empty()) {
    for (size_t i = 0; i < m_filtered.size(); ++i) {
      if (m_ctx.games[m_filtered[i]].path == st.lastGame) {
        m_selection = (int)i;
        int visible = (ui::kPanelH - 16) / 56;
        if (m_selection >= visible) m_scroll = m_selection - visible + 1;
        break;
      }
    }
  }
  m_launchSel = st.launchIndex;
}

int GameListScreen::systemId() const { return m_systemId; }

System* GameListScreen::currentSystem() {
  if (m_systemId < 0 || m_systemId >= (int)m_ctx.systems.size()) return nullptr;
  return &m_ctx.systems[m_systemId];
}

std::string GameListScreen::systemLabel() const {
  if (m_systemId == -2) return "Recently Played";
  if (m_systemId == -1) return "Favorites";
  if (m_systemId >= 0 && m_systemId < (int)m_ctx.systems.size())
    return m_ctx.systems[m_systemId].label;
  return "NDSUI";
}

uint32_t GameListScreen::accent() const {
  Settings& st = Settings::instance();
  if (!st.accent.empty())
    return (uint32_t)strtoul(st.accent.c_str(), nullptr, 16) & 0xFFFFFF;
  if (st.perSystemAccent && m_systemId >= 0 &&
      m_systemId < (int)m_ctx.systems.size())
    return m_ctx.systems[m_systemId].accent();
  return kChromeDark;
}

void GameListScreen::rebuildFilter() {
  Settings& st = Settings::instance();
  m_filtered.clear();
  for (size_t i = 0; i < m_ctx.games.size(); ++i) {
    const Game& g = m_ctx.games[i];
    bool keep = false;
    if (m_systemId == -2) {
      for (const Recent& r : st.recents)
        if (r.path == g.path) {
          keep = true;
          break;
        }
    } else if (m_systemId == -1) {
      keep = st.isFavorite(g.path);
    } else {
      keep = g.systemIndex == m_systemId;
    }
    if (keep) m_filtered.push_back((int)i);
  }
  if (m_systemId == -2 && !st.recents.empty()) {
    // keep recents order (most recent first)
    std::sort(m_filtered.begin(), m_filtered.end(), [&](int a, int b) {
      const std::string& pa = m_ctx.games[a].path;
      const std::string& pb = m_ctx.games[b].path;
      auto rank = [&](const std::string& p) {
        for (size_t i = 0; i < st.recents.size(); ++i)
          if (st.recents[i].path == p) return (int)i;
        return 999;
      };
      return rank(pa) < rank(pb);
    });
  }
  m_selection = 0;
  m_scroll = 0;
  m_ctx.dirty = true;
}

const Game* GameListScreen::current() const {
  if (m_filtered.empty()) return nullptr;
  int idx = m_filtered[std::clamp(m_selection, 0, (int)m_filtered.size() - 1)];
  return &m_ctx.games[idx];
}

void GameListScreen::move(int delta) {
  if (m_filtered.empty()) return;
  int sel = std::clamp(m_selection + delta, 0, (int)m_filtered.size() - 1);
  if (sel == m_selection) return;
  m_selection = sel;
  int visible = (ui::kPanelH - 16) / 56;
  if (m_selection < m_scroll) m_scroll = m_selection;
  if (m_selection >= m_scroll + visible) m_scroll = m_selection - visible + 1;
  m_ctx.dirty = true;
}

void GameListScreen::cycleSystem(int dir) {
  int n = (int)m_ctx.systems.size();
  int id = m_systemId;
  for (int step = 0; step < n + 2; ++step) {
    id += dir;
    if (id > n - 1) id = -2;
    if (id < -2) id = n - 1;
    if (id == -2 || id == -1) {
      // pseudo systems need content
      if (id == -2 && Settings::instance().recents.empty()) continue;
      if (id == -1 && Settings::instance().favorites.empty()) continue;
      m_systemId = id;
      Settings::instance().lastSystem =
          id == -2 ? "@recents" : "@favorites";
      rebuildFilter();
      return;
    }
    if (m_ctx.systems[id].gameCount > 0) {
      m_systemId = id;
      Settings::instance().lastSystem = m_ctx.systems[id].id;
      rebuildFilter();
      return;
    }
  }
}

ScreenResult GameListScreen::handle(ActionType a) {
  ScreenResult r;
  if (m_powerDialog) {
    if (a == ActionType::Up) {
      m_powerSel = (m_powerSel + 2) % 3;
      m_ctx.dirty = true;
    } else if (a == ActionType::Down) {
      m_powerSel = (m_powerSel + 1) % 3;
      m_ctx.dirty = true;
    } else if (a == ActionType::A) {
      m_powerDialog = false;
      if (m_powerSel == 0) {
        r.kind = ScreenResult::Exit;
      } else if (m_powerSel == 1) {
        dev::reboot();
        r.kind = ScreenResult::Exit;
      } else {
        dev::poweroff();
        r.kind = ScreenResult::Exit;
      }
      return r;
    } else if (a == ActionType::B) {
      m_powerDialog = false;
      m_ctx.dirty = true;
    }
    return r;
  }

  switch (a) {
    case ActionType::Up:
      move(-1);
      break;
    case ActionType::Down:
      move(1);
      break;
    case ActionType::Left:
      cycleSystem(-1);
      break;
    case ActionType::Right:
      cycleSystem(1);
      break;
    case ActionType::L:
      cycleSystem(-1);
      break;
    case ActionType::R:
      cycleSystem(1);
      break;
    case ActionType::X: {
      System* sys = currentSystem();
      if (sys && !sys->launches.empty()) {
        m_launchSel = (m_launchSel + 1) % (int)sys->launches.size();
        Settings::instance().launchIndex = m_launchSel;
        Settings::instance().save();
        m_status = "Core: " + sys->launches[m_launchSel].name;
        m_statusUntil = SDL_GetTicks() + 2000;
        m_ctx.dirty = true;
      }
      break;
    }
    case ActionType::Y: {
      const Game* g = current();
      if (g) {
        Settings::instance().toggleFavorite(g->path);
        m_status = Settings::instance().isFavorite(g->path)
                       ? "Added to favorites"
                       : "Removed from favorites";
        m_statusUntil = SDL_GetTicks() + 2000;
        m_ctx.dirty = true;
      }
      break;
    }
    case ActionType::A: {
      const Game* g = current();
      System* sys = currentSystem();
      if (g && sys && !sys->launches.empty()) {
        int li = std::min(m_launchSel, (int)sys->launches.size() - 1);
        std::string script =
            joinPath(joinPath(emusDir(), sys->id), sys->launches[li].script);
        r.kind = ScreenResult::Launch;
        r.script = script;
        r.arg = g->path;
        r.label = g->name;
        Settings& st = Settings::instance();
        st.addRecent(g->path);
        st.lastGame = g->path;
        st.lastSystem = sys->id;
        st.save();
      }
      break;
    }
    case ActionType::B:
      m_powerDialog = true;
      m_powerSel = 0;
      m_ctx.dirty = true;
      break;
    default:
      break;
  }
  return r;
}

void GameListScreen::tick() {
  std::string clk = clockString();
  std::string bat = batteryString();
  if (clk != m_clock || bat != m_battery) {
    m_clock = clk;
    m_battery = bat;
    m_ctx.dirty = true;
  }
  if (!m_status.empty() && SDL_GetTicks() > m_statusUntil) {
    m_status.clear();
    m_ctx.dirty = true;
  }
}

void GameListScreen::draw(Canvas& c) {
  uint32_t acc = accent();

  c.dots(0, ui::kStatusH, Canvas::W, Canvas::H - ui::kStatusH, kBg, kBgDot);
  c.rect(0, ui::kStatusH - 4, Canvas::W, 4, acc);

  std::string center = m_filtered.empty()
                           ? "0/0"
                           : fmt("%d/%d", m_selection + 1,
                                 (int)m_filtered.size());
  ui::statusBar(c, systemLabel(), center, m_clock, m_battery, acc);

  // ---- list panel ----
  ui::panel(c, kListX, ui::kPanelY, kListW, ui::kPanelH);
  std::vector<std::string> rows;
  rows.reserve(m_filtered.size());
  for (int gi : m_filtered) rows.push_back(m_ctx.games[gi].name);
  int visible = ui::listRows(c, kListX, ui::kPanelY, kListW, ui::kPanelH, rows,
                             m_selection, m_scroll);
  ui::scrollbar(c, kListX + kListW - 14, ui::kPanelY + 8, ui::kPanelH - 16,
                (int)rows.size(), visible, m_scroll);

  // ---- details panel ----
  ui::panel(c, kDetailX, ui::kPanelY, kDetailW, ui::kPanelH);
  const Game* g = current();
  if (!g) {
    c.textCenter(kDetailX + kDetailW / 2, ui::kPanelY + ui::kPanelH / 2 - 24,
                 m_systemId == -2 ? "No recent games"
                 : m_systemId == -1 ? "No favorites yet (Y adds one)"
                                    : "No games",
                 kTextFaint, kFontMedium);
  } else {
    System& sys = m_ctx.systems[g->systemIndex];
    c.rect(kDetailX + 2, ui::kPanelY + 2, kDetailW - 4, 64, kPanel);
    c.textClipped(kDetailX + 16, ui::kPanelY + 10, kDetailW - 32,
                  g->name.c_str(), kText, kFontMedium);

    // box art from Imgs/<system>/<basename>.png (stock layout)
    int artW = 240, artH = 300;
    int artX = kDetailX + 24, artY = ui::kPanelY + 88;
    std::string img =
        joinPath(joinPath(sdcardRoot() + "/Imgs", sys.id),
                 fileBase(fileNameOf(g->path)) + ".png");
    SDL_Surface* art = m_ctx.images.get(img, artW, artH);
    if (art) {
      int ax = artX + (artW - art->w) / 2;
      int ay = artY + (artH - art->h) / 2;
      c.rect(artX, artY, artW, artH, kPanel);
      c.rectOutline(artX, artY, artW, artH, kBorder, 2);
      SDL_Rect dst{ax, ay, art->w, art->h};
      SDL_BlitSurface(art, nullptr, c.surface(), &dst);
    } else {
      // placeholder cartridge
      c.rect(artX, artY, artW, artH, kPanel);
      c.rectOutline(artX, artY, artW, artH, kBorder, 2);
      int cx = artX + artW / 2, cy = artY + artH / 2;
      c.rect(cx - 45, cy - 45, 90, 90, kPanelAlt);
      c.rectOutline(cx - 45, cy - 45, 90, 90, kBorderDark, 3);
      c.rect(cx - 33, cy - 33, 66, 30, kWhite);
    }

    // facts column
    int tx = artX + artW + 22;
    int tw = kDetailX + kDetailW - tx - 16;
    int ty = artY + 2;
    c.textClipped(tx, ty, tw, sys.label.c_str(), acc, kFontSmall);
    ty += 56;
    c.textClipped(tx, ty, tw, upper(fileExt(g->path)).c_str(), kTextDim,
                  kFontSmall);
    ty += 52;
    c.textClipped(tx, ty, tw, humanSize(g->size).c_str(), kText, kFontSmall);
    ty += 52;
    c.textClipped(tx, ty, tw, "Last played", kTextDim, kFontSmall);
    ty += 44;
    c.textClipped(tx, ty, tw, timeStr(g->mtime).c_str(), kText, kFontSmall);
    ty += 56;
    if (Settings::instance().isFavorite(g->path))
      c.textClipped(tx, ty, tw, "* Favorite", acc, kFontSmall);

    // launch options: pills, selected one highlighted (X cycles)
    int py = ui::kPanelY + ui::kPanelH - 70;
    int px = kDetailX + 24;
    for (size_t i = 0; i < sys.launches.size() && i < 3; ++i) {
      const std::string& nm = sys.launches[i].name;
      int w = c.textWidth(nm.c_str(), kFontTiny) + 32;
      if (px + w > kDetailX + kDetailW - 20) break;
      bool sel = (int)i == std::min(m_launchSel, (int)sys.launches.size() - 1);
      c.rect(px, py, w, 44, sel ? kWhite : kPanelAlt);
      c.rectOutline(px, py, w, 44, sel ? acc : kBorder, sel ? 3 : 2);
      c.textCenter(px + w / 2, py + 10, nm.c_str(), kText, kFontTiny);
      px += w + 12;
    }
  }

  // ---- description + action bars ----
  if (!m_status.empty())
    ui::descBar(c, m_status);
  else if (g)
    ui::descBar(c, fileNameOf(g->path));
  else
    ui::descBar(c, "No games. Check Roms/ and Emus/ on the SD card.");

  ui::actionBar(c, {{"A", "Play"}, {"B", "Power"}, {"X", "Core"},
                    {"Y", "Fav"}, {"L/R", "Sys"}, {"Start", "Apps"}});

  if (m_powerDialog) {
    std::vector<std::string> lines = {"Exit to MainUI", "Reboot",
                                      "Power Off"};
    for (size_t i = 0; i < lines.size(); ++i)
      if ((int)i == m_powerSel) lines[i] = "> " + lines[i] + " <";
    ui::dialog(c, "Power", lines, "A: Select    B: Cancel");
  }
}

}  // namespace ndsui
