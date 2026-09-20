// AppsScreen: DS styled grid of the stock Apps/ entries.
#pragma once

#include <string>
#include <vector>

#include "screens/Screen.h"

namespace ndsui {

struct AppEntry {
  std::string dir;
  std::string label;
  std::string launch;   // script path
  std::string iconPath; // resolved icon file (may be empty)
};

class AppsScreen : public Screen {
 public:
  explicit AppsScreen(AppContext& ctx);
  void draw(Canvas& c) override;
  ScreenResult handle(ActionType a) override;
  void tick() override;
  std::string title() const override { return "Apps"; }

 private:
  void move(int dx, int dy);

  AppContext& m_ctx;
  std::vector<AppEntry> m_apps;
  int m_sel = 0;
  std::string m_status;
  Uint32 m_statusUntil = 0;
  std::string m_clock, m_battery;
};

}  // namespace ndsui
