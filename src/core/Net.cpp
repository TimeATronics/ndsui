#include "core/Net.h"

#include "core/Settings.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#include <SDL.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "util/Log.h"
#include "util/Platform.h"

namespace ndsui {
namespace net {

namespace {
constexpr const char* kWpaCtrl = "/etc/wifi/sockets";
constexpr const char* kBrightness = "/sys/class/led/tg5040_exterme_brightness";

std::mutex gMutex;
std::string gStatus;
std::atomic<bool> gBusy{false};

// cached state (written by the worker, read by the UI)
std::atomic<bool> gWifiEnabled{false};
std::atomic<bool> gWifiConnected{false};
std::atomic<bool> gBtEnabled{false};
std::atomic<bool> gBtConnected{false};
std::string gWifiSsid;
int gWifiRssi = -100;
std::vector<WifiNetwork> gWifiList;
std::vector<BtDevice> gBtList;
std::atomic<bool> gScanning{false};
Uint32 gScanStart = 0;

std::string shQuote(const std::string& s) {
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

// Run a command; the output is captured and the child is killed after
// `timeoutMs` so a blocking daemon client can never wedge the worker thread.
std::string runCmd(const std::string& cmd, int timeoutMs = 6000) {
  std::string full = cmd + " </dev/null 2>/dev/null";
  std::string out;
  FILE* f = popen(full.c_str(), "r");
  if (!f) return out;
  Uint32 start = SDL_GetTicks();
  char buf[512];
  while (fgets(buf, sizeof(buf), f)) {
    out += buf;
    if (timeoutMs > 0 && (int)(SDL_GetTicks() - start) > timeoutMs) break;
  }
  pclose(f);
  if (timeoutMs > 0 && (int)(SDL_GetTicks() - start) > timeoutMs) {
    // pclose may have blocked; nudge any stragglers of this command
    std::string kill = "pkill -f " + shQuote("bluetoothctl") + " 2>/dev/null";
    if (cmd.find("wpa_cli") == std::string::npos) system(kill.c_str());
  }
  return out;
}

// interactive bluetoothctl session: commands are fed over time because
// bluetoothctl aborts a pending blocking command on stdin EOF, and it only
// exits cleanly when it is told to quit.
std::string sessionctl(const std::string& script, int timeoutMs) {
  std::string cmd = "(" + script + ") | bluetoothctl";
  return runCmd(cmd, timeoutMs);
}

bool fileExistsC(const char* path) {
  FILE* f = fopen(path, "r");
  if (f) {
    fclose(f);
    return true;
  }
  return false;
}

std::string wpaStatus() {
  return runCmd(std::string("wpa_cli -p ") + kWpaCtrl + " -i wlan0 status");
}

bool rfkillBlocked(const char* what) {
  std::string out = runCmd("rfkill list bluetooth 2>/dev/null");
  std::string all = runCmd("rfkill list 2>/dev/null");
  (void)out;
  // find the block containing `what` and check its soft state
  size_t pos = all.find(what);
  if (pos == std::string::npos) return false;
  size_t end = all.find("\n\n", pos);
  std::string block = all.substr(pos, end == std::string::npos
                                          ? std::string::npos
                                          : end - pos);
  return block.find("Soft blocked: yes") != std::string::npos;
}

void wifiRefreshState() {
  gWifiEnabled.store(!rfkillBlocked("wlan") && !rfkillBlocked("phy0"));
  std::string st = wpaStatus();
  bool conn = st.find("wpa_state=COMPLETED") != std::string::npos;
  gWifiConnected.store(conn);
  std::string ssid;
  for (size_t i = 0; i < st.size();) {
    size_t e = st.find('\n', i);
    std::string line = st.substr(i, e == std::string::npos ? std::string::npos
                                                           : e - i);
    if (line.rfind("ssid=", 0) == 0) ssid = line.substr(5);
    if (e == std::string::npos) break;
    i = e + 1;
  }
  {
    std::lock_guard<std::mutex> lk(gMutex);
    gWifiSsid = ssid;
  }
  if (conn) {
    std::string link = runCmd("iw dev wlan0 link 2>/dev/null");
    size_t sp = link.find("signal:");
    if (sp != std::string::npos) gWifiRssi = atoi(link.c_str() + sp + 7);
  }
}

bool wifiHasCredentials(const std::string& ssid) {
  std::string list = runCmd(std::string("wpa_cli -p ") + kWpaCtrl +
                            " -i wlan0 list_networks");
  size_t i = list.find('\n');
  if (i == std::string::npos) return false;
  i += 1;
  while (i < list.size()) {
    size_t e = list.find('\n', i);
    std::string line = list.substr(i, e == std::string::npos ? std::string::npos
                                                             : e - i);
    size_t t1 = line.find('\t');
    if (t1 != std::string::npos) {
      size_t t2 = line.find('\t', t1 + 1);
      std::string s = line.substr(t1 + 1, t2 == std::string::npos
                                             ? std::string::npos
                                             : t2 - t1 - 1);
      if (s == ssid) return true;
    }
    if (e == std::string::npos) break;
    i = e + 1;
  }
  return false;
}

void btRefreshStateNow() {
  gBtEnabled.store(!rfkillBlocked("bluetooth"));
}

}  // namespace

// ---------------------------------------------------------------- worker

void async(const std::function<void()>& fn) {
  if (gBusy.exchange(true)) return;  // one operation at a time
  std::thread([fn]() {
    fn();
    gBusy.store(false);
  }).detach();
}

bool busy() { return gBusy.load(); }

std::string status() {
  std::lock_guard<std::mutex> lk(gMutex);
  return gStatus;
}

void setStatus(const std::string& s) {
  std::lock_guard<std::mutex> lk(gMutex);
  gStatus = s;
}

// ---------------------------------------------------------------- WiFi

void refreshWifiState() {
  async([]() { wifiRefreshState(); });
}

void refreshBtState() {
  async([]() { btRefreshStateNow(); });
}

bool wifiEnabled() { return gWifiEnabled.load(); }
bool wifiConnected() { return gWifiConnected.load(); }

std::string wifiSsid() {
  std::lock_guard<std::mutex> lk(gMutex);
  return gWifiSsid;
}

int wifiSignal() { return gWifiRssi; }

// Sound volume: the stock OSD uses the codec's "digital volume" control, so
// set the same one (0-100). Runs in the caller's worker thread context.
void setVolumePercent(int pct) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  runCmd("amixer sset \"digital volume\" " + std::to_string(pct) + "%");
}

int volumePercent() {
  std::string out = runCmd("amixer sget \"digital volume\"");
  size_t p = out.find('[');
  if (p == std::string::npos) return -1;
  return atoi(out.c_str() + p + 1);
}

static std::atomic<int> g_hwVolume{-1};

int hardwareVolume() { return g_hwVolume.load(); }

void fetchHardwareVolume() {
  async([]() { g_hwVolume.store(volumePercent()); });
}

void pulseRumble(int strength) {
  if (strength <= 0) return;
  long volt = 500000 + (long)strength * (3300000 - 500000) / 5;
  auto wr = [](const char* path, long v) {
    FILE* f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "%ld", v);
    fclose(f);
  };
  wr("/sys/class/motor/voltage", volt);
  wr("/sys/class/gpio/gpio227/value", 1);
  SDL_Delay(120);
  wr("/sys/class/gpio/gpio227/value", 0);
}

void setWifiEnabled(bool on) {
  setStatus(on ? "Turning Wi-Fi on..." : "Turning Wi-Fi off...");
  async([on]() {
    if (on) {
      runCmd("rfkill unblock wlan");
      runCmd("ifconfig wlan0 up");
      // the stock init script already keeps wpa_supplicant running; start it
      // if it went away
      std::string ps = runCmd("pidof wpa_supplicant");
      if (trim(ps).empty())
        runCmd("wpa_supplicant -B -D nl80211 -iwlan0 -c "
               "/etc/wifi/wpa_supplicant.conf");
      runCmd(std::string("wpa_cli -p ") + kWpaCtrl + " -i wlan0 reassociate");
    } else {
      runCmd(std::string("wpa_cli -p ") + kWpaCtrl + " -i wlan0 disconnect");
      runCmd("ifconfig wlan0 down");
      runCmd("rfkill block wlan");
    }
    wifiRefreshState();
    setStatus("");
  });
}

void startWifiScan() {
  if (gScanning.load()) return;
  gScanning.store(true);
  gScanStart = SDL_GetTicks();
  setStatus("Scanning...");
  async([]() {
    // wpa_cli's scan is async: trigger it, wait, then read the results
    runCmd(std::string("wpa_cli -p ") + kWpaCtrl + " -i wlan0 scan");
    SDL_Delay(2000);
    std::string out = runCmd(std::string("wpa_cli -p ") + kWpaCtrl +
                             " -i wlan0 scan_results");
    std::vector<WifiNetwork> list;
    size_t i = out.find('\n');
    i = i == std::string::npos ? out.size() : i + 1;
    std::string cur = wifiSsid();
    while (i < out.size()) {
      size_t e = out.find('\n', i);
      std::string line = out.substr(i, e == std::string::npos ? std::string::npos
                                                             : e - i);
      // bssid \t freq \t signal \t flags \t ssid
      size_t t1 = line.find('\t');
      size_t t2 = t1 == std::string::npos ? std::string::npos
                                          : line.find('\t', t1 + 1);
      size_t t3 = t2 == std::string::npos ? std::string::npos
                                          : line.find('\t', t2 + 1);
      size_t t4 = t3 == std::string::npos ? std::string::npos
                                          : line.find('\t', t3 + 1);
      if (t4 != std::string::npos) {
        int dbm = atoi(line.substr(t2 + 1, t3 - t2 - 1).c_str());
        std::string flags = line.substr(t3 + 1, t4 - t3 - 1);
        std::string ssid = line.substr(t4 + 1);
        while (!ssid.empty() && (ssid.back() == ' ' || ssid.back() == '\t' ||
                                 ssid.back() == '\r'))
          ssid.pop_back();
        if (!ssid.empty()) {
          WifiNetwork n;
          n.ssid = ssid;
          n.signal = dbm <= -90 ? 1 : (dbm >= -40 ? 100 : (dbm + 90) * 2);
          n.secure = flags.find("WPA") != std::string::npos ||
                     flags.find("WEP") != std::string::npos;
          n.known = wifiHasCredentials(ssid);
          n.current = (ssid == cur);
          bool dup = false;
          for (WifiNetwork& ex : list)
            if (ex.ssid == n.ssid) {
              ex.signal = std::max(ex.signal, n.signal);
              dup = true;
              break;
            }
          if (!dup) list.push_back(n);
        }
      }
      if (e == std::string::npos) break;
      i = e + 1;
    }
    {
      std::lock_guard<std::mutex> lk(gMutex);
      gWifiList = std::move(list);
    }
    gScanning.store(false);
    setStatus("");
  });
}

bool wifiScanning() { return gScanning.load(); }

std::vector<WifiNetwork> wifiScanResults() {
  std::lock_guard<std::mutex> lk(gMutex);
  return gWifiList;
}

void wifiConnect(const std::string& ssid, const std::string& psk, bool secure) {
  setStatus("Connecting to " + ssid + "...");
  async([ssid, psk, secure]() {
    // find or add the network
    std::string list = runCmd(std::string("wpa_cli -p ") + kWpaCtrl +
                              " -i wlan0 list_networks");
    int id = -1;
    size_t i = list.find('\n');
    i = i == std::string::npos ? list.size() : i + 1;
    while (i < list.size()) {
      size_t e = list.find('\n', i);
      std::string line = list.substr(i, e == std::string::npos ? std::string::npos
                                                              : e - i);
      size_t t1 = line.find('\t');
      size_t t2 = t1 == std::string::npos ? std::string::npos
                                          : line.find('\t', t1 + 1);
      if (t2 != std::string::npos &&
          line.substr(t1 + 1, t2 - t1 - 1) == ssid) {
        id = atoi(line.substr(0, t1).c_str());
        break;
      }
      if (e == std::string::npos) break;
      i = e + 1;
    }
    std::string wpa = std::string("wpa_cli -p ") + kWpaCtrl + " -i wlan0 ";
    if (id < 0) {
      std::string out = runCmd(wpa + "add_network");
      // reply is on the second line
      size_t nl = out.find('\n');
      id = atoi(nl == std::string::npos ? out.c_str() : out.c_str() + nl + 1);
      if (id < 0) id = 0;
      runCmd(wpa + "set_network " + std::to_string(id) + " ssid '\"" + ssid +
             "\"'");
    }
    if (secure && !psk.empty())
      runCmd(wpa + "set_network " + std::to_string(id) + " psk '\"" + psk +
             "\"'");
    else if (!secure)
      runCmd(wpa + "set_network " + std::to_string(id) + " key_mgmt NONE");
    runCmd(wpa + "enable_network " + std::to_string(id));
    runCmd(wpa + "select_network " + std::to_string(id));
    runCmd(wpa + "save_config");
    // wait for association, then ask for a lease with the stock script
    for (int tries = 0; tries < 12; ++tries) {
      SDL_Delay(500);
      std::string st = wpaStatus();
      if (st.find("wpa_state=COMPLETED") != std::string::npos) break;
    }
    runCmd("udhcpc -i wlan0 -s /etc/wifi/udhcpc_wlan0 -b");
    wifiRefreshState();
    setStatus("");
    startWifiScan();  // refresh the known/current flags
  });
}

void wifiDisconnect() {
  setStatus("Disconnecting...");
  async([]() {
    std::string wpa = std::string("wpa_cli -p ") + kWpaCtrl + " -i wlan0 ";
    runCmd(wpa + "disconnect");
    wifiRefreshState();
    setStatus("");
  });
}

void wifiForget(const std::string& ssid) {
  async([ssid]() {
    std::string wpa = std::string("wpa_cli -p ") + kWpaCtrl + " -i wlan0 ";
    std::string list = runCmd(wpa + "list_networks");
    size_t i = list.find('\n');
    i = i == std::string::npos ? list.size() : i + 1;
    while (i < list.size()) {
      size_t e = list.find('\n', i);
      std::string line = list.substr(i, e == std::string::npos ? std::string::npos
                                                              : e - i);
      size_t t1 = line.find('\t');
      size_t t2 = t1 == std::string::npos ? std::string::npos
                                          : line.find('\t', t1 + 1);
      if (t2 != std::string::npos &&
          line.substr(t1 + 1, t2 - t1 - 1) == ssid) {
        runCmd(wpa + "remove_network " + line.substr(0, t1));
        break;
      }
      if (e == std::string::npos) break;
      i = e + 1;
    }
    runCmd(wpa + "save_config");
    wifiRefreshState();
  });
}

// ---------------------------------------------------------------- BT
// Verified bring-up sequence for the Brick (xr829): the stock bt_init.sh
// fails on this firmware, so the stack is started by hand exactly like the
// music player does it. All of it runs on the worker thread (it takes ~20 s)
// with a status line; bluetoothctl is only ever driven through a scripted
// stdin so it can never block.

bool btEnabled() { return gBtEnabled.load(); }
bool btConnected() { return gBtConnected.load(); }

void btRefreshState() {
  bool up = fileExistsC("/sys/class/bluetooth/hci0");
  std::string pid = runCmd("pidof bluetoothd", 2000);
  up = up && !trim(pid).empty();
  gBtEnabled.store(up);
}

void setBtEnabled(bool on) {
  setStatus(on ? "Starting Bluetooth..." : "Turning Bluetooth off...");
  async([on]() {
    if (on) {
      runCmd("killall -9 bluealsa bluetoothd hciattach 2>/dev/null", 4000);
      runCmd("ln -snf /etc/bluetooth/keys /var/lib/bluetooth", 3000);
      runCmd("echo 0 > /sys/class/rfkill/rfkill0/state; sleep 1; "
             "echo 1 > /sys/class/rfkill/rfkill0/state; sleep 1", 6000);
      runCmd("(hciattach -n ttyS1 xradio >/dev/null 2>&1 &); sleep 5", 9000);
      runCmd("hciconfig hci0 up 2>/dev/null", 4000);
      runCmd("(bluetoothd -n >/dev/null 2>&1 &); sleep 3", 7000);
      runCmd("(bluealsa -p a2dp-source >/dev/null 2>&1 &); sleep 3", 7000);
      runCmd("bluetoothctl power on", 15000);
      runCmd("bluetoothctl pairable on", 15000);
    } else {
      runCmd("bluetoothctl power off", 8000);
      runCmd("killall bluealsa bluetoothd hciattach 2>/dev/null", 5000);
      runCmd("echo 0 > /sys/class/rfkill/rfkill0/state", 3000);
    }
    btRefreshStateNow();
    setStatus("");
    {
      std::lock_guard<std::mutex> lk(gMutex);
      gBtList.clear();
    }
  });
}

void btScan() {
  if (gScanning.load() || !gBtEnabled.load()) return;
  setStatus("Scanning...");
  async([]() {
    // one interactive session keeps discovery alive without leaking procs
    sessionctl("printf 'agent on\nscan on\n'; sleep 10; "
               "printf 'scan off\nquit\n'", 20000);
    refreshBtDevices();
    setStatus("");
  });
}

void refreshBtDevices() {
  if (gBusy.load()) return;
  async([]() {
    std::vector<BtDevice> list;
    if (!gBtEnabled.load()) {
      std::lock_guard<std::mutex> lk(gMutex);
      gBtList.clear();
      return;
    }
    auto add = [&](const std::string& out, bool connected, bool paired) {
      size_t i = 0;
      while (i < out.size()) {
        size_t e = out.find('\n', i);
        std::string line = out.substr(
            i, e == std::string::npos ? std::string::npos : e - i);
        if (line.rfind("Device ", 0) == 0) {
          std::string rest = line.substr(7);
          size_t sp = rest.find(' ');
          if (sp != std::string::npos) {
            BtDevice d;
            d.mac = rest.substr(0, sp);
            d.name = rest.substr(sp + 1);
            d.connected = connected;
            d.paired = paired;
            bool dup = false;
            for (BtDevice& ex : list)
              if (ex.mac == d.mac) {
                ex.connected = ex.connected || connected;
                ex.paired = ex.paired || paired;
                dup = true;
                break;
              }
            if (!dup) list.push_back(d);
          }
        }
        if (e == std::string::npos) break;
        i = e + 1;
      }
    };
    add(sessionctl("printf 'devices Paired\nquit\n'", 8000), false, true);
    add(sessionctl("printf 'devices Connected\nquit\n'", 8000), true, true);
    add(sessionctl("printf 'devices\nquit\n'", 8000), false, false);
    bool any = false;
    for (BtDevice& d : list) {
      if (d.connected) {
        any = true;
        continue;
      }
      // resolve the live connected state for this device
      std::string info = sessionctl("printf 'info " + d.mac +
                                        "\nquit\n'", 6000);
      if (info.find("Connected: yes") != std::string::npos) {
        d.connected = true;
        any = true;
      }
      if (info.find("Paired: yes") != std::string::npos) d.paired = true;
    }
    gBtConnected.store(any);
    std::lock_guard<std::mutex> lk(gMutex);
    gBtList = std::move(list);
  });
}

std::vector<BtDevice> btDevices() {
  std::lock_guard<std::mutex> lk(gMutex);
  return gBtList;
}

void btConnect(const std::string& mac) {
  setStatus("Connecting...");
  async([mac]() {
    sessionctl("printf 'agent on\npairable on\n'; sleep 1; printf 'connect " +
                   mac +
                   "\n'; for i in $(seq 1 12); do sleep 1; bluetoothctl info " +
                   mac + " 2>/dev/null | grep -q 'Connected: yes' && break; "
                         "done; printf 'quit\n'",
               45000);
    setStatus("");
    refreshBtDevices();
  });
}

void btDisconnect(const std::string& mac) {
  setStatus("Disconnecting...");
  async([mac]() {
    sessionctl("printf 'disconnect " + mac +
                   "\n'; sleep 3; printf 'quit\n'", 30000);
    setStatus("");
    refreshBtDevices();
  });
}

void btPair(const std::string& mac) {
  setStatus("Pairing...");
  async([mac]() {
    sessionctl(
        "printf 'agent on\npairable on\n'; sleep 1; printf 'pair " + mac +
            "\n'; for i in $(seq 1 25); do sleep 1; bluetoothctl info " + mac +
            " 2>/dev/null | grep -q 'Paired: yes' && break; done; "
            "printf 'trust " + mac + "\n'; sleep 1; printf 'quit\n'",
        70000);
    setStatus("");
    refreshBtDevices();
  });
}

// ---------------------------------------------------------------- display
// Brightness is a /dev/disp ioctl (the sysfs "led" node is not the panel).
// Colour temperature and the picture enhancements are sysfs attr nodes; the
// enhancement block must be enabled once (enhance_mode=1).

bool brightnessSet(int value) {
  if (value < 0) value = 0;
  if (value > 255) value = 255;
  int fd = open("/dev/disp", O_RDWR);
  if (fd < 0) return false;
  unsigned long param[4] = {0, (unsigned long)value, 0, 0};
  bool ok = ioctl(fd, 0x102 /* DISP_LCD_SET_BRIGHTNESS */, param) >= 0;
  close(fd);
  return ok;
}

static bool attrWrite(const char* node, int value) {
  std::string path = std::string("/sys/class/disp/disp/attr/") + node;
  FILE* f = fopen(path.c_str(), "w");
  if (!f) return false;
  fprintf(f, "%d", value);
  fclose(f);
  return true;
}

static int attrRead(const char* node) {
  std::string path = std::string("/sys/class/disp/disp/attr/") + node;
  FILE* f = fopen(path.c_str(), "r");
  if (!f) return -1;
  char buf[64] = {0};
  if (fgets(buf, sizeof(buf), f)) {
  }
  fclose(f);
  return atoi(buf);
}

// LEDs: the stock firmware drives the 23-LED frame buffer (frame_hex) and
// keeps max_scale at 0, so the static colour path writes frames. Effects use
// the per-zone effect_* interface.
void ledApply(int on, int effect, int colorIdx, int scaleM) {
  auto sys = [](const char* f) {
    return std::string("/sys/class/led_anim/") + f;
  };
  auto wr = [&](const char* f, const std::string& v) {
    FILE* fp = fopen(sys(f).c_str(), "w");
    if (!fp) return;
    fputs(v.c_str(), fp);
    fclose(fp);
  };
  static const uint32_t cols[] = {0xFFFFFF, 0xFF3020, 0x30FF40, 0x3060FF,
                                  0xFFD020, 0xFF30C0, 0x00E0E0};
  int ci = colorIdx < 0 ? 0 : (colorIdx > 6 ? 6 : colorIdx);
  uint32_t col = cols[ci];
  if (!on || effect == 0) {
    wr("enable", "0");
    wr("anim_frames_enable", "0");
    return;
  }
  float k = std::max(0, std::min(60, scaleM)) / 60.f;
  if (k < 0.05f) k = 0.05f;
  int r = (int)(((col >> 16) & 0xFF) * k);
  int g = (int)(((col >> 8) & 0xFF) * k);
  int b = (int)((col & 0xFF) * k);
  char hex[16];
  snprintf(hex, sizeof(hex), "%02X%02X%02X ", r, g, b);
  if (effect == 4) {  // static: fill the 23-LED frame
    std::string frame;
    for (int i = 0; i < 23; ++i) frame += hex;
    wr("frame_hex", frame);
    wr("anim_frames_enable", "1");
  } else {
    wr("anim_frames_enable", "0");
    for (const char* z : {"m", "lr", "f1", "f2"}) {
      wr((std::string("effect_") + z).c_str(), std::to_string(effect));
      char c[16];
      snprintf(c, sizeof(c), "%06X", col);
      wr((std::string("effect_rgb_hex_") + z).c_str(), c);
    }
    wr("effect_enable", "1");
  }
  wr("enable", "1");
}

void displayEnhanceApply(int contrast, int saturation, int bright) {
  attrWrite("enhance_mode", 1);
  attrWrite("enhance_contrast", std::max(0, std::min(100, contrast)));
  attrWrite("enhance_saturation", std::max(0, std::min(100, saturation)));
  attrWrite("enhance_bright", std::max(0, std::min(100, bright)));
}

void colorTempSet(int value) {
  if (value < 0) value = 0;
  if (value > 255) value = 255;
  attrWrite("color_temperature", value);
}

void displayApplyFromSettings() {
  Settings& st = Settings::instance();
  brightnessSet(st.getIntDefault("brightness", 5) * 255 / 10);
  // 0 = leave the panel's colour temperature untouched
  int ct = st.getIntDefault("color_temp", 0);
  if (ct > 0) colorTempSet(ct * 255 / 40);
  else attrWrite("color_temperature", 0);
  int contrast = 50 + st.getIntDefault("contrast", 0) * 5;
  int saturation = 50 + st.getIntDefault("saturation", 0) * 5;
  int exposure = 50 + st.getIntDefault("exposure", 0) * 5;
  displayEnhanceApply(contrast, saturation, exposure);
}

}  // namespace net
}  // namespace ndsui
