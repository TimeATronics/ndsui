// SettingsScreen: DS styled settings (categories left, items right with
// Left/Right adjustable values) + the DS color picker sub-screen.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "screens/Screen.h"

namespace ndsui {

class SettingsScreen : public Screen {
 public:
  explicit SettingsScreen(AppContext& ctx);

  void draw(Canvas& c) override;
  ScreenResult handle(ActionType a) override;
  void tick() override;
  std::string title() const override { return "Settings"; }

 private:
  struct Item {
    std::string label;
    enum Kind { Info, Spinner, Toggle, Action, ColorPicker } kind = Info;
    int value = 0;
    int minV = 0, maxV = 100, step = 1;
    std::string text;  // Info value / action name
    std::string id;    // handler id
  };
  struct Category {
    std::string name;
    std::vector<Item> items;
  };

  void buildCategories();
  void adjust(int dir);
  void activate();
  ScreenResult m_pending;  // picker push
  bool m_hasPending = false;

  AppContext& m_ctx;
  std::vector<Category> m_cats;
  int m_cat = 0;
  int m_item = 0;
  std::string m_status;
  Uint32 m_statusUntil = 0;
  std::string m_clock, m_battery;
};

// DS palette color picker (LEDs + accent)
class ColorPickerScreen : public Screen {
 public:
  ColorPickerScreen(AppContext& ctx, const std::string& title, unsigned initial,
                    std::function<void(unsigned)> onPick);

  void draw(Canvas& c) override;
  ScreenResult handle(ActionType a) override;
  void tick() override;

 private:
  AppContext& m_ctx;
  std::string m_title;
  int m_index = 0;
  std::function<void(unsigned)> m_onPick;
  std::string m_clock, m_battery;
};

// DS palette (from the design system color picker)
const std::vector<uint32_t>& dsPalette();

}  // namespace ndsui
