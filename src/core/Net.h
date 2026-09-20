// Network helpers for the Settings screen, following NextUI's approach but
// adapted to the stock TrimUI firmware:
//   WiFi  : wpa_cli -p /etc/wifi/sockets -i wlan0, rfkill for on/off
//   BT    : /etc/bluetooth/bt_init.sh start + hciconfig + bluetoothctl,
//           rfkill for on/off
// Every possibly-blocking command runs on a background worker (bluetoothctl
// blocks for minutes while the radio is soft-blocked, which used to freeze
// the whole UI); the UI only ever reads the cached state below.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace ndsui {
namespace net {

// ---- generic async worker (one operation at a time) ----
void async(const std::function<void()>& fn);
bool busy();
// human readable status for the settings screen ("Turning Wi-Fi on...")
std::string status();
void setStatus(const std::string& s);

// ---- WiFi ----
struct WifiNetwork {
  std::string ssid;
  int signal = 0;      // 0-100 (from dBm)
  bool secure = false;
  bool known = false;      // stored credentials exist (no password needed)
  bool current = false;    // the connected one
};

bool wifiEnabled();          // cached (rfkill), refreshed by refreshState()
bool wifiConnected();        // cached
std::string wifiSsid();      // cached
int wifiSignal();            // cached dBm
void setWifiEnabled(bool on);
void refreshWifiState();   // async: rfkill + wpa status
void startWifiScan();        // async; results cached
bool wifiScanning();
std::vector<WifiNetwork> wifiScanResults();
void wifiConnect(const std::string& ssid, const std::string& psk,
                 bool secure);
void wifiDisconnect();
void wifiForget(const std::string& ssid);

// ---- Bluetooth ----
struct BtDevice {
  std::string mac;
  std::string name;
  bool connected = false;
  bool paired = false;
};

bool btEnabled();            // cached (rfkill)
bool btConnected();          // cached
void setBtEnabled(bool on);
void refreshBtState();     // async: rfkill
void refreshBtDevices();     // async; results cached
void btScan();               // async discovery (10 s)
std::vector<BtDevice> btDevices();
void btConnect(const std::string& mac);
void btDisconnect(const std::string& mac);
void btPair(const std::string& mac);

// ---- sound (stock "digital volume" control) ----
void setVolumePercent(int pct);
int volumePercent();
int hardwareVolume();       // cached read of the codec control (-1 unknown)
void fetchHardwareVolume(); // async refresh of the cache
void pulseRumble(int strength);  // 1-5, gpio227 + /sys/class/motor/voltage
// ---- LEDs (frame_hex / effect_* on /sys/class/led_anim) ----
void ledApply(int on, int effect, int colorIdx, int scale);
// ---- display (Allwinner /dev/disp + sysfs attr nodes, verified on device) --
bool brightnessSet(int value);          // 0-255 via DISP_LCD_SET_BRIGHTNESS
void displayEnhanceApply(int contrast, int saturation, int bright);  // 0-100
void colorTempSet(int value);           // 0-255
// convenience: 0..10 brightness / 0..40 colour temp / -n..m enhance values
void displayApplyFromSettings();

}  // namespace net
}  // namespace ndsui
