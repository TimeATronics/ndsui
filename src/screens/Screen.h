// Screen base + shared app context.
#pragma once

#include <string>
#include <vector>

#include "core/Settings.h"
#include "core/Systems.h"
#include "ui/Canvas.h"
#include "ui/Images.h"

namespace ndsui {

enum class ActionType {
  None,
  Up,
  Down,
  Left,
  Right,
  A,
  B,
  X,
  Y,
  L,
  R,
  Start,
  Select,
  RotateLeft,   // L2 + left
  RotateRight,  // L2/R2 + right
};

class Screen;

struct ScreenResult {
  enum Kind { None, Pop, Push, Launch, Exit } kind = None;
  Screen* next = nullptr;
  std::string script;  // Launch: script path
  std::string arg;     // Launch: argument (rom path, may be empty)
  std::string label;   // Launch: display label for the game/app
};

struct AppContext {
  std::vector<System> systems;
  std::vector<Game> games;
  ImageCache images;
  bool dirty = true;
};

class Screen {
 public:
  virtual ~Screen() = default;
  virtual void draw(Canvas& c) = 0;
  virtual ScreenResult handle(ActionType a) = 0;
  virtual void tick() {}
  // true while something is animating -> the main loop runs at ~60 Hz
  virtual bool animating() const { return false; }
  // continuous rotation input (-1 / 0 / +1) while L2/R2 + left/right are held
  virtual void setRotateAxis(int axis) { (void)axis; }
  virtual void setPitchAxis(int axis) { (void)axis; }
  // debug: force every cartridge to use this model (host/device previews)
  virtual void setModelOverride(const std::string& model) { (void)model; }
  // the canvas was re-created (returned from a launched game): every region
  // must repaint from scratch or the fresh surface keeps its dark fill
  virtual void invalidateAll() {}
  virtual std::string title() const { return ""; }
};

// shared helpers
std::string clockString();
std::string batteryString();

}  // namespace ndsui
