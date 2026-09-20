// System + game discovery from the stock SD card layout:
//   Emus/<SYSTEM>/config.json   (label, rompath, extlist, launch, launchlist)
//   Roms/<SYSTEM>/              (game files)
#pragma once

#include <ctime>
#include <string>
#include <vector>

namespace ndsui {

struct LaunchOption {
  std::string name;    // display name from the launchlist
  std::string script;  // script file inside Emus/<id>/
};

struct System {
  std::string id;            // Emus/<id> folder name
  std::string label;         // display label
  std::string themeColor;    // "RRGGBB" accent (from config)
  std::string romPath;       // absolute ROM directory
  std::string extList;       // "z64|v64|n64|zip"
  std::string defaultScript; // default launch script
  std::vector<LaunchOption> launches;
  int gameCount = 0;

  // accent color as 0xRRGGBB (falls back to DS chrome blue)
  unsigned accent() const;
};

struct Game {
  std::string path;
  std::string name;      // display name (file base)
  int systemIndex = 0;
  time_t mtime = 0;
  long size = 0;
};

std::vector<System> scanSystems();

// Fills systems[i].gameCount and returns the (sorted) game list.
std::vector<Game> scanGames(std::vector<System>& systems);

}  // namespace ndsui
