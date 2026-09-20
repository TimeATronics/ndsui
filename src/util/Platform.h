// Platform paths + small string helpers.
#pragma once

#include <string>
#include <vector>

namespace ndsui {

// On the Brick this is the real /mnt/SDCARD; on the host build the test data
// directory (NDS_SDCARD env or ./testdata) is used.
std::string sdcardRoot();
bool isDevice();

std::string emusDir();      // <sdcard>/Emus
std::string romsDir();      // <sdcard>/Roms
std::string appsDir();      // <sdcard>/Apps
std::string assetDir();     // where the binary + assets live

std::string joinPath(const std::string& a, const std::string& b);
std::string fileBase(const std::string& path);   // filename without extension
std::string fileExt(const std::string& path);    // lowercase, no dot
bool fileExists(const std::string& path);
bool dirExists(const std::string& path);

// string helpers
std::string trim(const std::string& s);
std::string lower(const std::string& s);
std::string upper(const std::string& s);
bool startsWith(const std::string& s, const std::string& prefix);
bool endsWith(const std::string& s, const std::string& suffix);
std::vector<std::string> split(const std::string& s, char sep);
std::string fmt(const char* fmt, ...);

// battery percent (device sysfs), -1 when unavailable/charging unknown
int batteryPercent();
bool batteryCharging();

}  // namespace ndsui
