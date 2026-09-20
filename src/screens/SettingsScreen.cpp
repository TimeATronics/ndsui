#include "screens/SettingsScreen.h"

#include <sys/stat.h>

#include <algorithm>

#include "core/Device.h"
#include "ui/Components.h"
#include "ui/Theme.h"
#include "util/Log.h"
#include "util/Platform.h"

namespace ndsui {

const std::vector<uint32_t>& dsPalette() {
  static const std::vector<uint32_t> p = {
      0x5A6E7A, 0x9C5A32, 0xE03030, 0xF08CC8, 0xF0A020, 0xF8E040,
      0xB8E858, 0x50C850, 0x38B0A0, 0x48A8E0, 0x3070D0, 0x7B68EE,
      0x4B2A8C, 0x9C3CC8, 0xE050C0, 0xE85890, 0x2A2A2A, 0xFFFFFF,
  };
  return p;
}

// ------------------------------------------------------------ SettingsScreen

SettingsScreen::SettingsScreen(AppContext& ctx) : m_ctx(ctx) {
  buildCategories();
}

void SettingsScreen::buildCategories() {
  m_cats.clear();

  {
    Category c{"Display", {}};
    c.items.push_back({"Brightness", Item::Spinner, 128, 0, 255, 16, "", "brightness"});
    c.items.push_back({"Color Temp", Item::Spinner, 0, 0, 255, 16, "", "colortemp"});
    c.items.push_back({"Contrast", Item::Spinner, 50, 0, 100, 5, "", "contrast"});
    c.items.push_back({"Saturation", Item::Spinner, 50, 0, 100, 5, "", "saturation"});
    c.items.push_back({"Exposure", Item::Spinner, 50, 0, 100, 5, "", "exposure"});
    m_cats.push_back(c);
  }
  {
    Category c{"Audio", {}};
    int vol = dev::volume();
    c.items.push_back({"Volume", Item::Spinner, vol < 0 ? 50 : vol, 0, 100, 5, "", "volume"});
    m_cats.push_back(c);
  }
  {
    Category c{"LEDs", {}};
    Settings& st = Settings::instance();
    c.items.push_back({"Enable", Item::Toggle,
                       st.getBoolDefault("leds_on", true) ? 1 : 0, 0, 1, 1, "", "led_enable"});
    c.items.push_back({"Effect", Item::Spinner, st.getIntDefault("led_effect", 1), 0,
                       (int)dev::ledEffects().size() - 1, 1, "", "led_effect"});
    c.items.push_back({"Brightness", Item::Spinner, st.getIntDefault("led_bright", 14), 0,
                       32, 2, "", "led_bright"});
    c.items.push_back({"Color", Item::ColorPicker, 0, 0, 0, 0, "", "led_color"});
    m_cats.push_back(c);
  }
  {
    Category c{"Interface", {}};
    Settings& st = Settings::instance();
    int accentMode = st.getIntDefault("accent_mode", 0);  // 0 per-system, 1 DS blue, 2 custom
    c.items.push_back({"Accent", Item::Spinner, accentMode, 0, 2, 1, "", "accent"});
    c.items.push_back({"Custom Color", Item::ColorPicker, 0, 0, 0, 0, "", "accent_color"});
    c.items.push_back({"Boot to NDSUI", Item::Toggle, st.bootToNdsui ? 1 : 0, 0, 1, 1, "",
                       "boot_ndsui"});
    m_cats.push_back(c);
  }
  {
    Category c{"System", {}};
    int pct = batteryPercent();
    c.items.push_back({"Battery", Item::Info, 0, 0, 0, 0,
                       pct < 0 ? "--" : fmt("%d%%", pct), "battery"});
    dev::Storage s = dev::storage();
    c.items.push_back({"Storage", Item::Info, 0, 0, 0, 0,
                       fmt("%ld MB free", s.freeMB), "storage"});
    c.items.push_back({"Version", Item::Info, 0, 0, 0, 0, "NDSUI 0.2", "version"});
    c.items.push_back({"Exit to MainUI", Item::Action, 0, 0, 0, 0, "", "exit"});
    m_cats.push_back(c);
  }
}

void SettingsScreen::adjust(int dir) {
  if (m_cat >= (int)m_cats.size()) return;
  Category& cat = m_cats[m_cat];
  if (m_item >= (int)cat.items.size()) return;
  Item& it = cat.items[m_item];
  if (it.kind == Item::Spinner) {
    it.value = std::clamp(it.value + dir * it.step, it.minV, it.maxV);
    // apply immediately + persist
    Settings& st = Settings::instance();
    if (it.id == "brightness") dev::setBrightness(it.value);
    else if (it.id == "colortemp") dev::setColortemp(it.value);
    else if (it.id == "contrast") dev::setContrast(it.value);
    else if (it.id == "saturation") dev::setSaturation(it.value);
    else if (it.id == "exposure") dev::setExposure(it.value);
    else if (it.id == "volume") dev::setVolume(it.value);
    else if (it.id == "led_effect") {
      dev::setLedEffectAll(it.value);
      st.setInt("led_effect", it.value);
    } else if (it.id == "led_bright") {
      dev::setLedBrightness(it.value);
      st.setInt("led_bright", it.value);
    } else if (it.id == "accent") {
      st.setInt("accent_mode", it.value);
      st.accent = it.value == 2 ? st.accent : (it.value == 1 ? "30BAF3" : "");
      st.perSystemAccent = it.value == 0;
    }
    st.save();
    m_ctx.dirty = true;
  } else if (it.kind == Item::Toggle) {
    it.value = it.value ? 0 : 1;
    activate();
  }
}

void SettingsScreen::activate() {
  if (m_cat >= (int)m_cats.size()) return;
  Category& cat = m_cats[m_cat];
  if (m_item >= (int)cat.items.size()) return;
  const Item& it = cat.items[m_item];
  Settings& st = Settings::instance();

  if (it.kind == Item::Toggle) {
    if (it.id == "led_enable") {
      dev::setLedEnable(it.value != 0);
      st.setBool("leds_on", it.value != 0);
      m_status = it.value ? "LEDs on" : "LEDs off";
    } else if (it.id == "boot_ndsui") {
      st.bootToNdsui = it.value != 0;
      // install/remove the stock start-script hook that boots NDSUI
      std::string hookDst = joinPath(sdcardRoot(), "System/starts/ndsui_boot.sh");
      std::string hookSrc = joinPath(assetDir(), "boot/ndsui_boot.sh");
      if (st.bootToNdsui) {
        std::string content;
        FILE* fp = fopen(hookSrc.c_str(), "r");
        if (fp) {
          char buf[4096];
          size_t n;
          while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
            content.append(buf, n);
          fclose(fp);
        }
        FILE* out = fopen(hookDst.c_str(), "w");
        if (out && !content.empty()) {
          fwrite(content.data(), 1, content.size(), out);
          fclose(out);
          chmod(hookDst.c_str(), 0755);
          m_status = "Boots into NDSUI";
        } else {
          if (out) fclose(out);
          st.bootToNdsui = false;
          m_status = "Boot hook missing (" + hookSrc + ")";
        }
      } else {
        remove(hookDst.c_str());
        m_status = "Boots into MainUI";
      }
    }
    st.save();
    m_statusUntil = SDL_GetTicks() + 3000;
    m_ctx.dirty = true;
    return;
  }
  if (it.kind == Item::Action && it.id == "exit") {
    m_pending.kind = ScreenResult::Exit;
    m_hasPending = true;
    return;
  }
  if (it.kind == Item::ColorPicker) {
    if (it.id == "led_color") {
      unsigned cur =
          (unsigned)strtoul(st.get("led_color", "41CBFB").c_str(), nullptr, 16);
      m_pending = ScreenResult{};
      m_pending.kind = ScreenResult::Push;
      m_pending.next = new ColorPickerScreen(m_ctx, "LED Color", cur,
                                             [](unsigned rgb) {
                                               Settings::instance().set("led_color",
                                                                        fmt("%06X", rgb));
                                               Settings::instance().save();
                                               dev::setLedColor(rgb);
                                               dev::setLedEnable(true);
                                             });
    } else if (it.id == "accent_color") {
      unsigned cur = (unsigned)strtoul(
          st.get("accent_custom", "5AB0E8").c_str(), nullptr, 16);
      m_pending = ScreenResult{};
      m_pending.kind = ScreenResult::Push;
      m_pending.next = new ColorPickerScreen(
          m_ctx, "Accent Color", cur, [](unsigned rgb) {
            Settings& s = Settings::instance();
            s.set("accent_custom", fmt("%06X", rgb));
            s.setInt("accent_mode", 2);
            s.accent = fmt("%06X", rgb);
            s.perSystemAccent = false;
            s.save();
          });
    }
    m_hasPending = true;
    m_ctx.dirty = true;
    return;
  }
  if (it.kind == Item::Toggle) return;
}

ScreenResult SettingsScreen::handle(ActionType a) {
  if (m_hasPending) {
    m_hasPending = false;
    return m_pending;
  }
  ScreenResult r;
  switch (a) {
    case ActionType::Up:
      m_item = std::max(0, m_item - 1);
      m_ctx.dirty = true;
      break;
    case ActionType::Down:
      if (m_cat < (int)m_cats.size())
        m_item = std::min((int)m_cats[m_cat].items.size() - 1, m_item + 1);
      m_ctx.dirty = true;
      break;
    case ActionType::Left:
      adjust(-1);
      break;
    case ActionType::Right:
      adjust(1);
      break;
    case ActionType::L:
      m_cat = (m_cat + (int)m_cats.size() - 1) % (int)m_cats.size();
      m_item = 0;
      m_ctx.dirty = true;
      break;
    case ActionType::R:
      m_cat = (m_cat + 1) % (int)m_cats.size();
      m_item = 0;
      m_ctx.dirty = true;
      break;
    case ActionType::A:
      activate();
      break;
    case ActionType::B:
      r.kind = ScreenResult::Pop;
      break;
    default:
      break;
  }
  return r;
}

void SettingsScreen::tick() {
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

void SettingsScreen::draw(Canvas& c) {
  c.dots(0, ui::kStatusH, Canvas::W, Canvas::H - ui::kStatusH, kBg, kBgDot);
  c.rect(0, ui::kStatusH - 4, Canvas::W, 4, kChromeDark);
  ui::statusBar(c, "Settings", m_cats.empty() ? "" : m_cats[m_cat].name, m_clock,
                m_battery, kChromeDark);

  // left: categories
  const int catX = 16, catW = 300;
  ui::panel(c, catX, ui::kPanelY, catW, ui::kPanelH);
  std::vector<std::string> names;
  for (const Category& cat : m_cats) names.push_back(cat.name);
  ui::listRows(c, catX, ui::kPanelY, catW, ui::kPanelH, names,
               m_cat, 0, 52);

  // right: items with values
  const int itX = catX + catW + 16, itW = Canvas::W - itX - 16;
  ui::panel(c, itX, ui::kPanelY, itW, ui::kPanelH);
  if (m_cat < (int)m_cats.size()) {
    const Category& cat = m_cats[m_cat];
    int y = ui::kPanelY + 10;
    for (size_t i = 0; i < cat.items.size(); ++i) {
      const Item& it = cat.items[i];
      std::string value;
      switch (it.kind) {
        case Item::Info:
          value = it.text;
          break;
        case Item::Action:
          value = "";
          break;
        case Item::Toggle:
          value = it.value ? "On" : "Off";
          break;
        case Item::ColorPicker: {
          if (it.id == "led_color")
            value = Settings::instance().get("led_color", "41CBFB");
          else
            value = Settings::instance().get("accent_custom", "5AB0E8");
          break;
        }
        case Item::Spinner:
          if (it.id == "accent") {
            static const char* modes[] = {"Per-system", "DS Blue", "Custom"};
            value = modes[std::clamp(it.value, 0, 2)];
          } else if (it.id == "led_effect") {
            auto eff = dev::ledEffects();
            value = (it.value >= 0 && it.value < (int)eff.size())
                        ? eff[it.value].name
                        : fmt("%d", it.value);
          } else {
            value = fmt("%d", it.value);
          }
          break;
      }
      ui::optionRow(c, itX + 8, y, itW - 16, it.label, value,
                    (int)i == m_item, kChromeDark);
      y += 56;
    }
  }

  ui::descBar(c, m_status.empty() ? "L/R: Category   Up/Down: Item   Left/Right: Change   A: Select"
                                  : m_status);
  ui::actionBar(c, {{"B", "Back"}, {"Start", "Games"}});
}

// ------------------------------------------------------------ ColorPicker

ColorPickerScreen::ColorPickerScreen(AppContext& ctx, const std::string& title,
                                     unsigned initial,
                                     std::function<void(unsigned)> onPick)
    : m_ctx(ctx), m_title(title), m_onPick(std::move(onPick)) {
  const auto& p = dsPalette();
  for (size_t i = 0; i < p.size(); ++i)
    if (p[i] == (initial & 0xFFFFFF)) m_index = (int)i;
}

ScreenResult ColorPickerScreen::handle(ActionType a) {
  ScreenResult r;
  const auto& p = dsPalette();
  int cols = 6;
  switch (a) {
    case ActionType::Left:
      m_index = std::max(0, m_index - 1);
      m_ctx.dirty = true;
      break;
    case ActionType::Right:
      m_index = std::min((int)p.size() - 1, m_index + 1);
      m_ctx.dirty = true;
      break;
    case ActionType::Up:
      m_index = std::max(0, m_index - cols);
      m_ctx.dirty = true;
      break;
    case ActionType::Down:
      m_index = std::min((int)p.size() - 1, m_index + cols);
      m_ctx.dirty = true;
      break;
    case ActionType::A:
      if (m_onPick) m_onPick(p[m_index]);
      r.kind = ScreenResult::Pop;
      break;
    case ActionType::B:
      r.kind = ScreenResult::Pop;
      break;
    default:
      break;
  }
  return r;
}

void ColorPickerScreen::tick() {
  std::string clk = clockString(), bat = batteryString();
  if (clk != m_clock || bat != m_battery) {
    m_clock = clk;
    m_battery = bat;
    m_ctx.dirty = true;
  }
}

void ColorPickerScreen::draw(Canvas& c) {
  c.dots(0, ui::kStatusH, Canvas::W, Canvas::H - ui::kStatusH, kBg, kBgDot);
  c.rect(0, ui::kStatusH - 4, Canvas::W, 4, kDialogBorder);
  ui::statusBar(c, m_title, "", m_clock, m_battery, kDialogBorder);

  const int cell = 96, gap = 22;
  int gridW = 6 * cell + 5 * gap;
  int gx = (Canvas::W - gridW) / 2;
  int gy = ui::kPanelY + 40;
  ui::colorGrid(c, gx, gy, cell, gap, dsPalette(), m_index);

  // preview
  int py = gy + 3 * (cell + gap) + 10;
  c.rect(Canvas::W / 2 - 120, py, 240, 90, dsPalette()[m_index]);
  c.rectOutline(Canvas::W / 2 - 120, py, 240, 90, kBorderDark, 3);

  ui::descBar(c, "Choose a color");
  ui::actionBar(c, {{"A", "Confirm"}, {"B", "Cancel"}});
}

}  // namespace ndsui
