#include "core/Systems.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstring>

#include "core/Json.h"
#include "util/Log.h"
#include "util/Platform.h"

namespace ndsui {

unsigned System::accent() const {
  if (themeColor.size() == 6) {
    unsigned v = (unsigned)strtoul(themeColor.c_str(), nullptr, 16);
    return v & 0xFFFFFF;
  }
  return 0x41CBFB;  // DS chrome blue
}

namespace {

bool hiddenName(const char* name) { return name[0] == '.'; }

}  // namespace

std::vector<System> scanSystems() {
  std::vector<System> out;
  DIR* d = opendir(emusDir().c_str());
  if (!d) {
    LOG_WARN("no Emus dir at %s", emusDir().c_str());
    return out;
  }
  struct dirent* e;
  while ((e = readdir(d)) != nullptr) {
    if (hiddenName(e->d_name)) continue;
    std::string dir = joinPath(emusDir(), e->d_name);
    if (!dirExists(dir)) continue;
    std::string cfgPath = joinPath(dir, "config.json");
    JsonPtr cfg = jsonParseFile(cfgPath);
    if (!cfg || !cfg->isObject()) continue;

    System sys;
    sys.id = e->d_name;
    sys.label = cfg->getString("label", e->d_name);
    sys.themeColor = cfg->getString("themecolor");
    sys.defaultScript = cfg->getString("launch", "launch.sh");
    sys.extList = cfg->getString("extlist");

    std::string rompath = cfg->getString("rompath");
    if (!rompath.empty()) {
      if (rompath.rfind("../../", 0) == 0)
        sys.romPath = sdcardRoot() + "/" + rompath.substr(6);
      else if (rompath[0] == '/')
        sys.romPath = rompath;
      else
        sys.romPath = joinPath(sdcardRoot(), rompath);
    } else {
      sys.romPath = joinPath(romsDir(), sys.id);
    }

    if (const JsonValue* ll = cfg->get("launchlist");
        ll && ll->isArray()) {
      for (const JsonPtr& item : ll->arr) {
        if (!item || !item->isObject()) continue;
        LaunchOption lo;
        lo.name = item->getString("name");
        lo.script = item->getString("launch");
        if (!lo.script.empty()) sys.launches.push_back(lo);
      }
    }
    if (sys.launches.empty())
      sys.launches.push_back({"Default", sys.defaultScript});

    out.push_back(sys);
  }
  closedir(d);
  std::sort(out.begin(), out.end(),
            [](const System& a, const System& b) { return a.id < b.id; });
  return out;
}

namespace {
// Non-game files that live in the ROM folders (stock launcher caches and
// playlists): never list these, whatever the system's extlist says.
bool junkFile(const std::string& name) {
  std::string n = lower(name);
  if (n.find("_cache") != std::string::npos) return true;
  if (n.rfind("content_history", 0) == 0) return true;
  static const char* bad[] = {"db",  "lpl", "txt",  "log", "srm", "sav",
                              "dbg", "bak", "tmp",  "cfg", "ini", "dat"};
  std::string ext = lower(fileExt(name));
  for (const char* b : bad)
    if (ext == b) return true;
  return false;
}
}  // namespace

std::vector<Game> scanGames(std::vector<System>& systems) {
  std::vector<Game> out;
  for (size_t si = 0; si < systems.size(); ++si) {
    System& sys = systems[si];
    sys.gameCount = 0;
    std::vector<std::string> exts = sys.extList.empty()
                                        ? std::vector<std::string>()
                                        : split(sys.extList, '|');
    for (std::string& x : exts) x = lower(trim(x));

    DIR* d = opendir(sys.romPath.c_str());
    if (!d) continue;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
      if (hiddenName(e->d_name)) continue;
      if (junkFile(e->d_name)) continue;
      std::string path = joinPath(sys.romPath, e->d_name);
      struct stat st;
      if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
      std::string ext = fileExt(e->d_name);
      if (!exts.empty() &&
          std::find(exts.begin(), exts.end(), ext) == exts.end())
        continue;

      Game g;
      g.path = path;
      g.name = fileBase(e->d_name);
      g.systemIndex = (int)si;
      g.mtime = st.st_mtime;
      g.size = (long)st.st_size;
      out.push_back(g);
      sys.gameCount++;
    }
    closedir(d);
  }

  std::sort(out.begin(), out.end(), [](const Game& a, const Game& b) {
    int c = lower(a.name).compare(lower(b.name));
    return c < 0;
  });
  LOG_INFO("scan: %zu systems, %zu games", systems.size(), out.size());
  return out;
}

}  // namespace ndsui
