#include "screens/AppsScreen.h"

#include <dirent.h>

#include <algorithm>

#include "core/Json.h"
#include "ui/Components.h"
#include "ui/Theme.h"
#include "util/Log.h"
#include "util/Platform.h"

namespace ndsui {

namespace {
constexpr int kCols = 4;
constexpr int kRows = 2;   // 8 per page
constexpr int kTileW = 226;
constexpr int kTileH = 240;
constexpr int kGapX = 22;
constexpr int kGapY = 20;

std::string resolveIcon(const std::string& dir, const std::string& name) {
  if (name.empty()) return "";
  std::string p = name[0] == '/' ? name : joinPath(dir, name);
  return fileExists(p) ? p : "";
}
}  // namespace

AppsScreen::AppsScreen(AppContext& ctx) : m_ctx(ctx) {
  DIR* d = opendir(appsDir().c_str());
  if (d) {
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
      if (e->d_name[0] == '.') continue;
      std::string dir = joinPath(appsDir(), e->d_name);
      if (!dirExists(dir)) continue;
      JsonPtr cfg = jsonParseFile(joinPath(dir, "config.json"));
      if (!cfg || !cfg->isObject()) continue;
      AppEntry a;
      a.dir = dir;
      a.label = cfg->getString("label", e->d_name);
      a.launch = joinPath(dir, cfg->getString("launch", "launch.sh"));
      a.iconPath = resolveIcon(dir, cfg->getString("icontop"));
      if (a.iconPath.empty()) a.iconPath = resolveIcon(dir, "icon.png");
      if (!fileExists(a.launch)) continue;
      m_apps.push_back(a);
    }
    closedir(d);
  }
  std::sort(m_apps.begin(), m_apps.end(),
            [](const AppEntry& a, const AppEntry& b) { return a.label < b.label; });
  LOG_INFO("apps: %zu", m_apps.size());
}

void AppsScreen::move(int dx, int dy) {
  if (m_apps.empty()) return;
  int page = kCols * kRows;
  int idx = m_sel + dy * kCols + dx;
  if (idx < 0) idx = 0;
  if (idx >= (int)m_apps.size()) idx = (int)m_apps.size() - 1;
  if (idx != m_sel) {
    m_sel = idx;
    m_ctx.dirty = true;
  }
  (void)page;
}

ScreenResult AppsScreen::handle(ActionType a) {
  ScreenResult r;
  switch (a) {
    case ActionType::Up: move(0, -1); break;
    case ActionType::Down: move(0, 1); break;
    case ActionType::Left: move(-1, 0); break;
    case ActionType::Right: move(1, 0); break;
    case ActionType::L: move(-kCols, 0); break;
    case ActionType::R: move(kCols, 0); break;
    case ActionType::A: {
      if (m_sel >= 0 && m_sel < (int)m_apps.size()) {
        r.kind = ScreenResult::Launch;
        r.script = m_apps[m_sel].launch;
        r.arg = "";
        r.label = m_apps[m_sel].label;
      }
      break;
    }
    case ActionType::B:
      r.kind = ScreenResult::Pop;
      break;
    default:
      break;
  }
  return r;
}

void AppsScreen::tick() {
  std::string clk = clockString(), bat = batteryString();
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

void AppsScreen::draw(Canvas& c) {
  c.dots(0, ui::kStatusH, Canvas::W, Canvas::H - ui::kStatusH, kBg, kBgDot);
  c.rect(0, ui::kStatusH - 4, Canvas::W, 4, kChromeDark);
  ui::statusBar(c, "Apps", "", m_clock, m_battery, kChromeDark);

  int gridW = kCols * kTileW + (kCols - 1) * kGapX;
  int x0 = (Canvas::W - gridW) / 2;
  int y0 = ui::kPanelY + 10;

  for (size_t i = 0; i < m_apps.size() && i < (size_t)(kCols * kRows); ++i) {
    int cx = x0 + (int)(i % kCols) * (kTileW + kGapX);
    int cy = y0 + (int)(i / kCols) * (kTileH + kGapY);
    SDL_Surface* icon = nullptr;
    if (!m_apps[i].iconPath.empty())
      icon = m_ctx.images.get(m_apps[i].iconPath, 96, 96);
    ui::tile(c, cx, cy, kTileW, kTileH, icon, m_apps[i].label,
             (int)i == m_sel, kChromeDark);
  }
  if (m_apps.empty())
    c.textCenter(Canvas::W / 2, Canvas::H / 2 - 40, "No apps found",
                 kTextFaint, kFontMedium);

  ui::descBar(c, m_apps.empty() ? "" : m_apps[m_sel].dir);
  ui::actionBar(c, {{"A", "Open"}, {"B", "Back"}, {"Start", "Settings"}});
}

}  // namespace ndsui
