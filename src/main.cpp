// NDSUI - Nintendo DS styled launcher for the TrimUI Brick (stock OS).
// Screen stack: Games (root) -> Apps -> Settings, launched apps/games run in
// the foreground with the UI torn down and restored around them.
#include <cstring>
#include <string>
#include <vector>

#include <SDL.h>

#include "core/Device.h"
#include "core/Launcher.h"
#include "core/Settings.h"
#include "core/Systems.h"
#include "ui/Shell.h"
#include "screens/Screen.h"
#include "ui/Canvas.h"
#include "util/Log.h"
#include "util/Platform.h"

using namespace ndsui;

namespace {

// single-quote a string for /bin/sh -c, escaping embedded quotes: ROM names
// like "Link's Awakening" otherwise break the command line and the shell dies
// with a syntax error instead of launching the game
std::string shellQuote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'')
      out += "'\\''";
    else
      out += c;
  }
  out += "'";
  return out;
}

struct Input {
  SDL_Joystick* joy = nullptr;
  int m_rotateAxis = 0;
  int m_pitchAxis = 0;
  bool m_shoulderMod = false;
  bool m_rotLatch = false;
  bool m_tiltLatch = false;
  bool prev[13] = {false};

  bool init() {
    SDL_InitSubSystem(SDL_INIT_JOYSTICK);
    // The vendor SDL floods the event queue with joystick axis/hat events
    // while the triggers or the d-pad are held (the values jitter). We read
    // the joystick state directly with the Get* calls, so drop the events
    // entirely: draining the flood every iteration stalls the main loop and
    // makes held-button rotation (L2/R2 + dpad) stutter or stop.
    SDL_EventState(SDL_JOYAXISMOTION, SDL_IGNORE);
    SDL_EventState(SDL_JOYHATMOTION, SDL_IGNORE);
    SDL_EventState(SDL_JOYBALLMOTION, SDL_IGNORE);
    SDL_EventState(SDL_JOYBUTTONDOWN, SDL_IGNORE);
    SDL_EventState(SDL_JOYBUTTONUP, SDL_IGNORE);
    if (SDL_NumJoysticks() > 0) {
      joy = SDL_JoystickOpen(0);
      LOG_INFO("joystick: %s", joy ? SDL_JoystickName(joy) : "open failed");
    }
    return true;
  }
  void shutdown() {
    if (joy) SDL_JoystickClose(joy);
    joy = nullptr;
  }

  std::vector<ActionType> poll() {
    std::vector<ActionType> out;
    SDL_PumpEvents();

    bool up = false, down = false, left = false, right = false;
    bool a = false, b = false, x = false, y = false, l = false, r = false;
    bool start = false, select = false;
    bool trigger = false;  // L2/R2 held (axes 2/5 on the Brick)
    if (joy) {
      Uint8 h = SDL_JoystickGetHat(joy, 0);
      up = h & SDL_HAT_UP;
      down = h & SDL_HAT_DOWN;
      left = h & SDL_HAT_LEFT;
      right = h & SDL_HAT_RIGHT;
      a = SDL_JoystickGetButton(joy, 1);
      b = SDL_JoystickGetButton(joy, 0);
      y = SDL_JoystickGetButton(joy, 2);
      x = SDL_JoystickGetButton(joy, 3);
      l = SDL_JoystickGetButton(joy, 4);
      r = SDL_JoystickGetButton(joy, 5);
      select = SDL_JoystickGetButton(joy, 6);
      start = SDL_JoystickGetButton(joy, 7);
      // Triggers (L2/R2) rest at -32768 and read positive when pressed on
      // this device, so only a POSITIVE reading means held (a sign-agnostic
      // check would treat the resting value as permanently pressed).
      for (int axis = 2; axis < SDL_JoystickNumAxes(joy); ++axis) {
        int v = SDL_JoystickGetAxis(joy, axis);
        if (v > 8000) trigger = true;
      }
      // the d-pad is also presented as axes 0/1 (rest -1, full deflection
      // when pressed): use them as a fallback if the hat is not reported
      int ax0 = SDL_JoystickGetAxis(joy, 0);
      int ax1 = SDL_JoystickGetAxis(joy, 1);
      if (ax0 > 8000) right = true;
      else if (ax0 < -8000) left = true;
      if (ax1 > 8000) down = true;
      else if (ax1 < -8000) up = true;
    }
    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    if (ks) {
      up |= ks[SDL_SCANCODE_UP];
      down |= ks[SDL_SCANCODE_DOWN];
      left |= ks[SDL_SCANCODE_LEFT];
      right |= ks[SDL_SCANCODE_RIGHT];
      a |= ks[SDL_SCANCODE_RETURN];
      b |= ks[SDL_SCANCODE_BACKSPACE];
      x |= ks[SDL_SCANCODE_X];
      y |= ks[SDL_SCANCODE_Y];
      l |= ks[SDL_SCANCODE_Q];
      r |= ks[SDL_SCANCODE_E];
      select |= ks[SDL_SCANCODE_TAB];
      start |= ks[SDL_SCANCODE_SPACE];
    }
    m_shoulderMod = (l || r) && (up || down || left || right);
    trigger = trigger || m_shoulderMod;

    // L2/R2 + left/right = rotate the carousel; the axis is applied every
    // frame by the screen (continuous, smooth), so no edge events here.
    // The modifier latches while a direction is held: the trigger value can
    // flicker around its threshold, and dropping out mid-spin looks broken.
    bool rotate = (left || right) && (trigger || m_rotLatch);
    bool tilt = (up || down) && (trigger || m_tiltLatch);
    m_rotLatch = rotate;
    m_tiltLatch = tilt;
    m_rotateAxis = rotate ? (left ? -1 : 1) : 0;
    m_pitchAxis = tilt ? (down ? 1 : -1) : 0;

    struct Edge {
      bool cur;
      int slot;
      ActionType type;
    };
    const Edge edges[] = {
        {up, 0, ActionType::Up},     {down, 1, ActionType::Down},
        {left, 2, rotate ? ActionType::RotateLeft : ActionType::Left},
        {right, 3, rotate ? ActionType::RotateRight : ActionType::Right},
        {a, 4, ActionType::A},       {b, 5, ActionType::B},
        {x, 9, ActionType::X},       {y, 10, ActionType::Y},
        {l, 6, ActionType::L},       {r, 7, ActionType::R},
        {start, 8, ActionType::Start},
        {select, 11, ActionType::Select},
    };
    for (const Edge& e : edges) {
      if ((e.type == ActionType::Up || e.type == ActionType::Down) && tilt)
        continue;  // consumed as tilt input
      if ((e.type == ActionType::Left || e.type == ActionType::Right) &&
          rotate)
        continue;  // consumed as yaw input
      if ((e.type == ActionType::L || e.type == ActionType::R) &&
          m_shoulderMod)
        continue;  // consumed as the rotation modifier
      if (e.cur && !prev[e.slot]) out.push_back(e.type);
      prev[e.slot] = e.cur;
    }
    return out;
  }

  std::string debugState() {
    if (!joy) return "";
    Uint8 h = SDL_JoystickGetHat(joy, 0);
    int pressed = 0;
    int n = SDL_JoystickNumButtons(joy);
    for (int b = 0; b < n; ++b)
      if (SDL_JoystickGetButton(joy, b)) pressed |= (1 << b);
    if (!h && !pressed) return "";
    return fmt("hat=%d buttons=0x%04X axes=%d,%d,%d,%d,%d,%d", h, pressed,
               SDL_JoystickGetAxis(joy, 0), SDL_JoystickGetAxis(joy, 1),
               SDL_JoystickGetAxis(joy, 2), SDL_JoystickGetAxis(joy, 3),
               SDL_JoystickGetAxis(joy, 4), SDL_JoystickGetAxis(joy, 5));
  }

  int rotateAxis() const { return m_rotateAxis; }
  int pitchAxis() const { return m_pitchAxis; }

  void waitRelease(int timeoutMs) {
    Uint32 deadline = SDL_GetTicks() + timeoutMs;
    while (SDL_GetTicks() < deadline) {
      SDL_PumpEvents();
      bool held = false;
      if (joy) {
        held = SDL_JoystickGetHat(joy, 0) != SDL_HAT_CENTERED;
        for (int i = 0; i < 8; ++i)
          held = held || SDL_JoystickGetButton(joy, i);
      }
      const Uint8* ks = SDL_GetKeyboardState(nullptr);
      if (ks && (ks[SDL_SCANCODE_RETURN] || ks[SDL_SCANCODE_BACKSPACE]))
        held = true;
      if (!held) return;
      SDL_Delay(16);
    }
  }
};

}  // namespace

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i)
    if (strcmp(argv[i], "--debug") == 0) logSetLevel(LogLevel::Debug);
  const char* shot = getenv("NDS_SHOT");

  LOG_INFO("ndsui starting");
  LOG_INFO("sdcard: %s", sdcardRoot().c_str());

  Settings::instance().load(assetDir());
  Settings& settings = Settings::instance();

  AppContext ctx;
  ctx.systems = scanSystems();
  ctx.games = scanGames(ctx.systems);

  Canvas canvas;
  if (!canvas.init(joinPath(assetDir(), "NDS12.ttf"))) {
    LOG_ERROR("canvas init failed");
    return 1;
  }

  // screen stack (root = the new shell)
  std::vector<Screen*> stack;
  stack.push_back(new Shell(ctx, canvas));

  if (shot) {
    const char* scr = getenv("NDS_SCREEN");
    if (scr) {
      static_cast<Shell*>(stack.back())->debugSetMode(scr);
      // let the animations settle for the shot
      for (int i = 0; i < 60; ++i) stack.back()->tick();
    }
    stack.back()->tick();
    stack.back()->draw(canvas);
    canvas.present();
    if (canvas.screenshot(shot)) LOG_INFO("screenshot: %s", shot);
    canvas.shutdown();
    return 0;
  }

  Input input;
  input.init();

  bool running = true;
  const bool autotest = getenv("NDS_AUTOTEST") != nullptr;
  if (autotest)
    if (const char* mo = getenv("NDS_MODEL"))
      stack.back()->setModelOverride(mo);
  // ---- Playwright-style control harness -------------------------------
  // NDSUI_CTL=<path>: the app polls that file every frame and executes each
  // new line as a command, so the device can be driven over SSH.
  //   a b x y l r up down left right start select   -> button press
  //   hold rot 1|0|-1 / hold pitch ...              -> continuous rotation
  //   wait <ms>                                     -> pause
  //   shot <path.bmp>                               -> screenshot
  //   screen <home|apps|games|games2|settings|settings2>
  //   cat <0-7>                                     -> settings category
  //   select <n>                                    -> carousel item
  //   state                                         -> append state to the log
  //   quit                                          -> exit the app
  const char* ctlPath = getenv("NDSUI_CTL");
  long ctlOff = 0;
  const bool fpsLog = getenv("NDS_FPSLOG") != nullptr;
  const char* autoApp = getenv("NDS_AUTOAPP");  // test hook: launch a script
  Uint32 autoAppAt = autoApp ? SDL_GetTicks() + 2000 : 0;
  int autoStep = 0;
  Uint32 autoStart = SDL_GetTicks();
  // test hook: simulate the canvas re-init that happens when a launched game
  // exits, then grab NDS_SHOT (used to verify the full repaint after resume)
  bool resumeDone = false;
  const char* resumeAt = getenv("NDS_RESUME_TEST");
  while (running) {
    if (resumeAt && !resumeDone && SDL_GetTicks() > (Uint32)atoi(resumeAt)) {
      resumeDone = true;
      LOG_INFO("resume test: re-initializing the canvas");
      input.shutdown();
      canvas.shutdown();
      SDL_InitSubSystem(SDL_INIT_VIDEO);
      if (!canvas.init(joinPath(assetDir(), "NDS12.ttf")))
        LOG_ERROR("canvas re-init failed");
      input.init();
      stack.back()->invalidateAll();
      ctx.dirty = true;
    }
    if (shot && resumeDone && SDL_GetTicks() > (Uint32)atoi(resumeAt) + 1200) {
      stack.back()->draw(canvas);
      canvas.present();
      if (canvas.screenshot(shot)) LOG_INFO("screenshot: %s", shot);
      running = false;
      break;
    }
    // autonomous navigation test (NDS_AUTOTEST): inject actions + captures
    if (autotest) {
      Uint32 t = SDL_GetTicks() - autoStart;
      static int lastStep = -1;
      if (autoStep != lastStep) {
        lastStep = autoStep;
        LOG_INFO("autotest: step %d (t=%u)", autoStep, t);
      }
      auto inject = [&](ActionType a) {
        stack.back()->handle(a);
        ctx.dirty = true;
      };
      if (autoStep == 0 && t > 2000) {
        autoStep = 1;
        inject(ActionType::Right);  // sections: Home -> Apps
      } else if (autoStep == 1 && t > 3200) {
        autoStep = 2;
        inject(ActionType::Right);  // sections: Apps -> Games
      } else if (autoStep == 2 && t > 5000) {
        autoStep = 3;
        canvas.screenshot("/tmp/auto1.bmp");  // root showcase
        LOG_INFO("autotest: games root shot");
      } else if (autoStep == 3 && t > 6000) {
        autoStep = 4;
        inject(ActionType::A);  // enter Games (systems + carts)
      } else if (autoStep == 4 && t > 8000) {
        autoStep = 5;
        inject(ActionType::Down);  // focus the systems strip
      } else if (autoStep == 5 && t > 9000) {
        autoStep = 6;
        inject(ActionType::L);  // next system
      } else if (autoStep == 6 && t > 10500) {
        autoStep = 7;
        inject(ActionType::Up);  // focus the games
        stack.back()->setRotateAxis(1);   // held rotation (continuous)
        stack.back()->setPitchAxis(-1);   // held pitch (continuous)
      } else if (autoStep == 7 && t > 13000) {
        autoStep = 8;
        stack.back()->setRotateAxis(0);
        stack.back()->setPitchAxis(0);
      } else if (autoStep == 8 && t > 13500) {
        autoStep = 9;
        canvas.screenshot("/tmp/auto2.bmp");  // rotated carts
      } else if (autoStep == 9 && t > 15000) {
        autoStep = 10;
        inject(ActionType::Right);  // next game
        inject(ActionType::A);      // launch: exercises the canvas re-init
      } else if (autoStep == 10 && t > 17000) {
        autoStep = 11;
        canvas.screenshot("/tmp/auto3.bmp");  // next game + status
      } else if (autoStep == 11 && t > 18500) {
        autoStep = 12;
        inject(ActionType::B);  // back to sections
      } else if (autoStep == 12 && t > 20000) {
        canvas.screenshot("/tmp/auto4.bmp");  // back at the sections page
        LOG_INFO("autotest: done");
        running = false;
      }
    }
    // The vendor SDL's SDL_WaitEventTimeout sleeps far longer than asked on
    // this device (~500 ms), which made every animation stutter. Pump events
    // and delay briefly instead; present() still syncs to vsync.
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) {
        LOG_INFO("event: SDL_QUIT");
        running = false;
      }
    }
    if (!running) break;
    SDL_Delay(stack.back()->animating() || autotest ? 1 : 40);
    if (fpsLog) {
      static Uint32 lastIt = 0;
      static int iters = 0;
      ++iters;
      Uint32 now = SDL_GetTicks();
      if (now - lastIt >= 1000) {
        LOG_INFO("perf/loop: %d iterations/s", iters);
        iters = 0;
        lastIt = now;
      }
    }

    // debug: dump the raw joystick state when anything is pressed
    // (behind NDS_INPUTDBG: this used to write several lines per second)
    static const bool inputDbg = getenv("NDS_INPUTDBG") != nullptr;
    if (inputDbg) {
      static Uint32 lastDump = 0;
      Uint32 now = SDL_GetTicks();
      if (now - lastDump > 300) {
        lastDump = now;
        std::string st = input.debugState();
        if (!st.empty()) LOG_INFO("input raw: %s", st.c_str());
      }
    }

    // continuous rotation input for the active screen (the autotest drives
    // the axis itself, so don't clobber it with the idle physical input)
    if (!autotest) {
      stack.back()->setRotateAxis(input.rotateAxis());
      stack.back()->setPitchAxis(input.pitchAxis());
    }
    for (ActionType a : input.poll()) {
      ScreenResult r = stack.back()->handle(a);

      if (r.kind == ScreenResult::Exit) {
        running = false;
        break;
      }
      if (r.kind == ScreenResult::Pop) {
        if (stack.size() > 1) {
          delete stack.back();
          stack.pop_back();
        } else {
          running = false;
        }
        ctx.dirty = true;
        break;
      }
      if (r.kind == ScreenResult::Push && r.next) {
        stack.push_back(r.next);
        ctx.dirty = true;
        break;
      }
      if (r.kind == ScreenResult::Launch) {
        LOG_INFO("launching: %s %s", r.script.c_str(), r.arg.c_str());
        input.waitRelease(400);
        input.shutdown();
        canvas.shutdown();
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        SDL_QuitSubSystem(SDL_INIT_JOYSTICK);

        int code = 0;
        if (!fileExists(r.script)) {
          LOG_ERROR("script missing: %s", r.script.c_str());
          code = 127;
        } else {
          std::string cmd = shellQuote(r.script);
          if (!r.arg.empty()) cmd += " " + shellQuote(r.arg);
          code = system(cmd.c_str());
          LOG_INFO("launch returned %d", code);
        }
        // the button that quit the app must not leak into our UI: it used to
        // back out of the grid (and even exit NDSUI) right after a game/app
        input.waitRelease(500);
        LOG_INFO("resume: input drained");

        // restore UI
        SDL_InitSubSystem(SDL_INIT_VIDEO);
        LOG_INFO("resume: video subsystem up");
        if (!canvas.init(joinPath(assetDir(), "NDS12.ttf")))
          LOG_ERROR("canvas re-init failed");
        LOG_INFO("resume: canvas ready");
        input.init();
        LOG_INFO("resume: input ready");
        stack.back()->invalidateAll();  // repaint everything on the fresh canvas
        LOG_INFO("resume: UI invalidated");
        ctx.dirty = true;
        break;
      }
      ctx.dirty = true;
    }

    // test hook: launch an app/game script once to exercise the resume path
    if (autoApp && SDL_GetTicks() > autoAppAt) {
      std::string script = autoApp;
      autoApp = nullptr;
      LOG_INFO("autoapp: launching %s", script.c_str());
      input.waitRelease(400);
      input.shutdown();
      canvas.shutdown();
      SDL_QuitSubSystem(SDL_INIT_VIDEO);
      SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
      int code = system(shellQuote(script).c_str());
      LOG_INFO("autoapp returned %d", code);
      input.waitRelease(500);
      LOG_INFO("resume: input drained");
      SDL_InitSubSystem(SDL_INIT_VIDEO);
      LOG_INFO("resume: video subsystem up");
      if (!canvas.init(joinPath(assetDir(), "NDS12.ttf")))
        LOG_ERROR("canvas re-init failed");
      LOG_INFO("resume: canvas ready");
      input.init();
      LOG_INFO("resume: input ready");
      stack.back()->invalidateAll();
      LOG_INFO("resume: UI invalidated");
      ctx.dirty = true;
    }

    // control harness: execute any new commands from the control file
    if (ctlPath) {
      FILE* f = fopen(ctlPath, "r");
      if (f) {
        // the file may have been truncated by a fresh upload
        if (fseek(f, 0, SEEK_END) == 0 && ftell(f) < ctlOff) ctlOff = 0;
        if (fseek(f, ctlOff, SEEK_SET) == 0) {
          char line[512];
          while (fgets(line, sizeof(line), f)) {
            ctlOff = ftell(f);
            std::string cmd = trim(line);
            if (cmd.empty()) continue;
            size_t sp = cmd.find(' ');
            std::string op = sp == std::string::npos ? cmd : cmd.substr(0, sp);
            std::string arg = sp == std::string::npos ? "" : cmd.substr(sp + 1);
            auto press = [&](ActionType a, bool down) {
              stack.back()->handle(a);
              ctx.dirty = true;
              (void)down;
            };
            if (op == "wait") {
              SDL_Delay((Uint32)atoi(arg.c_str()));
            } else if (op == "shot") {
              stack.back()->tick();
              stack.back()->draw(canvas);
              canvas.present();
              if (canvas.screenshot(arg)) LOG_INFO("ctl: shot %s", arg.c_str());
            } else if (op == "screen") {
              static_cast<Shell*>(stack.back())->debugSetMode(arg);
              ctx.dirty = true;
            } else if (op == "cat") {
              static_cast<Shell*>(stack.back())->debugCategory(atoi(arg.c_str()));
            } else if (op == "select") {
              static_cast<Shell*>(stack.back())->debugSelect(atoi(arg.c_str()));
            } else if (op == "state") {
              LOG_INFO("ctl: %s",
                       static_cast<Shell*>(stack.back())->debugState().c_str());
            } else if (op == "hold") {
              size_t sp2 = arg.find(' ');
              std::string what = sp2 == std::string::npos ? arg : arg.substr(0, sp2);
              int v = atoi(arg.substr(sp2 + 1).c_str());
              if (what == "rot") stack.back()->setRotateAxis(v);
              else if (what == "pitch") stack.back()->setPitchAxis(v);
              ctx.dirty = true;
            } else if (op == "quit") {
              running = false;
            } else {
              ActionType a = ActionType::A;
              if (op == "a") a = ActionType::A;
              else if (op == "b") a = ActionType::B;
              else if (op == "x") a = ActionType::X;
              else if (op == "y") a = ActionType::Y;
              else if (op == "l") a = ActionType::L;
              else if (op == "r") a = ActionType::R;
              else if (op == "up") a = ActionType::Up;
              else if (op == "down") a = ActionType::Down;
              else if (op == "left") a = ActionType::Left;
              else if (op == "right") a = ActionType::Right;
              else if (op == "start") a = ActionType::Start;
              else if (op == "select") a = ActionType::Select;
              else if (!op.empty()) {
                LOG_WARN("ctl: unknown command '%s'", op.c_str());
                continue;
              }
              press(a, true);
            }
          }
        }
        fclose(f);
      }
    }

    // tick every iteration (the shell paces itself via animating())
    Uint32 tTick0 = SDL_GetTicks();
    for (Screen* s : stack) s->tick();
    Uint32 tTick1 = SDL_GetTicks();
    if (fpsLog) {
      static Uint32 tickAcc = 0;
      static int tickN = 0;
      tickAcc += tTick1 - tTick0;
      if (++tickN >= 30) {
        LOG_INFO("perf/loop: tick avg %.1f ms", (double)tickAcc / tickN);
        tickAcc = 0;
        tickN = 0;
      }
    }

    if (ctx.dirty) {
      ctx.dirty = false;
      Uint32 t0 = SDL_GetTicks();
      stack.back()->draw(canvas);
      Uint32 t1 = SDL_GetTicks();
      canvas.present();  // damage tracking from draw() flags the texture
      if (fpsLog) {
        static Uint32 accDraw = 0;
        static Uint32 lastLog = 0;
        accDraw += t1 - t0;
        Uint32 now = SDL_GetTicks();
        if (now - lastLog > 1000) {
          lastLog = now;
          LOG_INFO("perf/shell: draw avg %.1f ms", (double)accDraw / 8.0);
          accDraw = 0;
          canvas.logPerfStats();
        }
      }
    }
  }

  settings.save();
  while (!stack.empty()) {
    delete stack.back();
    stack.pop_back();
  }
  input.shutdown();
  canvas.shutdown();
  LOG_INFO("ndsui exiting");
  return 0;
}
