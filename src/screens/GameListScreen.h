// GameListScreen: DS styled game browser. System cycle includes pseudo
// systems "Recently Played" and "Favorites"; X cycles the launch option,
// Y toggles favorite, A launches, B opens the power dialog, Start goes to
// the Apps screen.
#pragma once

#include <string>
#include <vector>

#include "screens/Screen.h"

namespace ndsui {

class GameListScreen : public Screen {
 public:
  GameListScreen(AppContext& ctx);

  void draw(Canvas& c) override;
  ScreenResult handle(ActionType a) override;
  void tick() override;

 private:
  void rebuildFilter();
  const Game* current() const;
  System* currentSystem();
  void move(int delta);
  void cycleSystem(int dir);
  int systemId() const;  // -2 recents, -1 favorites, >=0 index
  std::string systemLabel() const;
  uint32_t accent() const;

  AppContext& m_ctx;
  std::vector<int> m_filtered;  // indices into ctx.games
  int m_systemId = 0;           // -2 recents, -1 favorites, else system index
  int m_selection = 0;
  int m_scroll = 0;
  int m_launchSel = 0;          // selected launch option
  bool m_powerDialog = false;
  int m_powerSel = 0;           // 0 exit, 1 reboot, 2 poweroff
  std::string m_status;
  Uint32 m_statusUntil = 0;
  std::string m_clock, m_battery;
};

}  // namespace ndsui
