#include "util/Platform.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

namespace ndsui {

bool isDevice() {
  struct stat st;
  return stat("/mnt/SDCARD", &st) == 0;
}

std::string sdcardRoot() {
  if (isDevice()) return "/mnt/SDCARD";
  const char* env = getenv("NDS_SDCARD");
  if (env && *env) return env;
  if (dirExists("testdata")) return "testdata";
  if (dirExists("../testdata")) return "../testdata";
  return "testdata";
}

std::string emusDir() { return sdcardRoot() + "/Emus"; }
std::string romsDir() { return sdcardRoot() + "/Roms"; }
std::string appsDir() { return sdcardRoot() + "/Apps"; }

std::string assetDir() {
#ifdef HOST_BUILD
  const char* env = getenv("NDS_ASSETS");
  if (env && *env) return env;
  if (dirExists("assets")) return "assets";
  if (dirExists("../assets")) return "../assets";
  return "assets";
#else
  return ".";
#endif
}

std::string joinPath(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  if (a.back() == '/') return a + b;
  return a + "/" + b;
}

std::string fileBase(const std::string& path) {
  size_t slash = path.find_last_of('/');
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  size_t dot = name.find_last_of('.');
  return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

std::string fileExt(const std::string& path) {
  size_t slash = path.find_last_of('/');
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  size_t dot = name.find_last_of('.');
  if (dot == std::string::npos || dot == 0) return "";
  return lower(name.substr(dot + 1));
}

bool fileExists(const std::string& path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool dirExists(const std::string& path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && isspace((unsigned char)s[a])) ++a;
  while (b > a && isspace((unsigned char)s[b - 1])) --b;
  return s.substr(a, b - a);
}

std::string lower(const std::string& s) {
  std::string r = s;
  for (char& c : r) c = (char)tolower((unsigned char)c);
  return r;
}

std::string upper(const std::string& s) {
  std::string r = s;
  for (char& c : r) c = (char)toupper((unsigned char)c);
  return r;
}

bool startsWith(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= s.size()) {
    size_t pos = s.find(sep, start);
    if (pos == std::string::npos) {
      out.push_back(s.substr(start));
      break;
    }
    out.push_back(s.substr(start, pos - start));
    start = pos + 1;
  }
  return out;
}

std::string fmt(const char* f, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, f);
  vsnprintf(buf, sizeof(buf), f, ap);
  va_end(ap);
  return buf;
}

namespace {
std::string readSysfs(const char* path) {
  FILE* fp = fopen(path, "r");
  if (!fp) return "";
  char buf[64] = {0};
  size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
  fclose(fp);
  return trim(std::string(buf, n));
}
}  // namespace

int batteryPercent() {
  std::string v = readSysfs("/sys/class/power_supply/axp2202-battery/capacity");
  if (v.empty()) return -1;
  return atoi(v.c_str());
}

bool batteryCharging() {
  return readSysfs("/sys/class/power_supply/axp2202-usb/online") == "1";
}

}  // namespace ndsui
