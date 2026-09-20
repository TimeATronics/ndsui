#include "core/Settings.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "util/Log.h"
#include "util/Platform.h"

namespace ndsui {

// ---------------------------------------------------------------- Store

void Store::load(const std::string& path) {
  m_items.clear();
  FILE* fp = fopen(path.c_str(), "r");
  if (!fp) return;
  char line[2048];
  while (fgets(line, sizeof(line), fp)) {
    std::string s = trim(line);
    if (s.empty() || s[0] == '#') continue;
    size_t eq = s.find('=');
    if (eq == std::string::npos) continue;
    m_items.emplace_back(trim(s.substr(0, eq)), trim(s.substr(eq + 1)));
  }
  fclose(fp);
}

void Store::save(const std::string& path) const {
  FILE* fp = fopen(path.c_str(), "w");
  if (!fp) {
    LOG_ERROR("cannot write %s", path.c_str());
    return;
  }
  for (const auto& kv : m_items)
    fprintf(fp, "%s=%s\n", kv.first.c_str(), kv.second.c_str());
  fclose(fp);
}

std::string Store::get(const std::string& key, const std::string& def) const {
  for (const auto& kv : m_items)
    if (kv.first == key) return kv.second;
  return def;
}

int Store::getInt(const std::string& key, int def) const {
  std::string v = get(key);
  return v.empty() ? def : atoi(v.c_str());
}

bool Store::getBool(const std::string& key, bool def) const {
  std::string v = lower(get(key));
  if (v.empty()) return def;
  return v == "1" || v == "true" || v == "yes" || v == "on";
}

void Store::set(const std::string& key, const std::string& value) {
  for (auto& kv : m_items)
    if (kv.first == key) {
      kv.second = value;
      return;
    }
  m_items.emplace_back(key, value);
}

void Store::setInt(const std::string& key, int v) {
  set(key, fmt("%d", v));
}

void Store::setBool(const std::string& key, bool v) { set(key, v ? "1" : "0"); }

// ---------------------------------------------------------------- Settings

Settings& Settings::instance() {
  static Settings s;
  return s;
}

void Settings::load(const std::string& dir) {
  m_dir = dir;
  m_store.load(joinPath(dir, "settings.cfg"));
  accent = m_store.get("accent");
  perSystemAccent = m_store.getBool("per_system_accent", true);
  bootToNdsui = m_store.getBool("boot_to_ndsui", false);
  lastSystem = m_store.get("last_system");
  lastGame = m_store.get("last_game");
  launchIndex = m_store.getInt("launch_index", 0);
  favorites.clear();
  for (const std::string& f : split(m_store.get("favorites"), '|')) {
    if (f.empty()) continue;
    // dedupe: a corrupted file must never create double entries
    if (std::find(favorites.begin(), favorites.end(), f) != favorites.end())
      continue;
    favorites.push_back(f);
  }

  recents.clear();
  FILE* fp = fopen(joinPath(dir, "recents.txt").c_str(), "r");
  if (fp) {
    char line[2048];
    while (fgets(line, sizeof(line), fp)) {
      std::string s = trim(line);
      if (s.empty()) continue;
      size_t tab = s.find('\t');
      Recent r;
      if (tab == std::string::npos) {
        r.path = s;
      } else {
        r.when = (time_t)atoll(s.substr(0, tab).c_str());
        r.path = s.substr(tab + 1);
      }
      recents.push_back(r);
    }
    fclose(fp);
  }
  LOG_INFO("settings loaded (%zu recents, %zu favorites)", recents.size(),
           favorites.size());
}

void Settings::save() {
  m_store.set("accent", accent);
  m_store.setBool("per_system_accent", perSystemAccent);
  m_store.setBool("boot_to_ndsui", bootToNdsui);
  m_store.set("last_system", lastSystem);
  m_store.set("last_game", lastGame);
  m_store.setInt("launch_index", launchIndex);
  std::string favs;
  for (const std::string& f : favorites) {
    if (!favs.empty()) favs += "|";
    favs += f;
  }
  m_store.set("favorites", favs);
  m_store.save(joinPath(m_dir, "settings.cfg"));

  FILE* fp = fopen(joinPath(m_dir, "recents.txt").c_str(), "w");
  if (fp) {
    for (const Recent& r : recents)
      fprintf(fp, "%lld\t%s\n", (long long)r.when, r.path.c_str());
    fclose(fp);
  }
}

void Settings::addRecent(const std::string& path) {
  for (auto it = recents.begin(); it != recents.end();) {
    if (it->path == path)
      it = recents.erase(it);
    else
      ++it;
  }
  recents.insert(recents.begin(), Recent{path, time(nullptr)});
  if (recents.size() > 15) recents.resize(15);  // F1: Recent pseudo-console cap
  save();
}

bool Settings::isFavorite(const std::string& path) const {
  return std::find(favorites.begin(), favorites.end(), path) !=
         favorites.end();
}

void Settings::toggleFavorite(const std::string& path) {
  auto it = std::find(favorites.begin(), favorites.end(), path);
  if (it == favorites.end())
    favorites.push_back(path);
  else
    favorites.erase(it);
  save();
}

}  // namespace ndsui
