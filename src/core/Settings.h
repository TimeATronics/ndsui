// App settings + recent games, persisted as simple key=value files in the
// app directory.
#pragma once

#include <ctime>
#include <string>
#include <vector>

namespace ndsui {

class Store {
 public:
  void load(const std::string& path);
  void save(const std::string& path) const;
  std::string get(const std::string& key, const std::string& def = "") const;
  int getInt(const std::string& key, int def = 0) const;
  bool getBool(const std::string& key, bool def = false) const;
  void set(const std::string& key, const std::string& value);
  void setInt(const std::string& key, int v);
  void setBool(const std::string& key, bool v);

 private:
  std::vector<std::pair<std::string, std::string>> m_items;
};

struct Recent {
  std::string path;  // full ROM path
  time_t when = 0;
};

class Settings {
 public:
  static Settings& instance();

  void load(const std::string& dir);
  void save();

  // --- values ---
  std::string accent;          // "" = DS chrome blue, else "RRGGBB"
  bool perSystemAccent = true;
  bool bootToNdsui = false;
  std::string lastSystem;      // system id
  std::string lastGame;        // game path
  int launchIndex = 0;         // last chosen launch option
  std::vector<std::string> favorites;

  // --- recents ---
  std::vector<Recent> recents;
  void addRecent(const std::string& path);
  bool isFavorite(const std::string& path) const;
  void toggleFavorite(const std::string& path);

  // --- generic access (module-specific values like LED settings) ---
  std::string get(const std::string& key, const std::string& def = "") const {
    return m_store.get(key, def);
  }
  int getIntDefault(const std::string& key, int def) const {
    return m_store.getInt(key, def);
  }
  bool getBoolDefault(const std::string& key, bool def) const {
    return m_store.getBool(key, def);
  }
  void set(const std::string& key, const std::string& v) { m_store.set(key, v); }
  void setInt(const std::string& key, int v) { m_store.setInt(key, v); }
  void setBool(const std::string& key, bool v) { m_store.setBool(key, v); }

 private:
  std::string m_dir;
  Store m_store;
};

}  // namespace ndsui
