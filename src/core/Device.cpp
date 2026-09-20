#include "core/Device.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <atomic>
#include <thread>

#include <alsa/asoundlib.h>

#include "util/Log.h"
#include "util/Platform.h"

namespace ndsui {
namespace dev {

namespace {

constexpr const char* kLedDir = "/sys/class/led_anim";

void writeFile(const char* path, const char* value) {
  FILE* fp = fopen(path, "w");
  if (!fp) {
    LOG_DEBUG("write %s: not available", path);
    return;
  }
  fputs(value, fp);
  fclose(fp);
}

std::string readFile(const char* path) {
  FILE* fp = fopen(path, "r");
  if (!fp) return "";
  char buf[512] = {0};
  size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
  fclose(fp);
  return trim(std::string(buf, n));
}

void writeIntAttr(const char* path, int v) { writeFile(path, fmt("%d", v).c_str()); }

}  // namespace

// ---------------------------------------------------------------- display

void setBrightness(int raw) {
  int fd = open("/dev/disp", O_RDWR);
  if (fd < 0) {
    LOG_DEBUG("no /dev/disp");
    return;
  }
  unsigned long param[4] = {(unsigned long)0, (unsigned long)raw, 0, 0};
  ioctl(fd, 0x102 /* DISP_LCD_SET_BRIGHTNESS */, &param);
  close(fd);
}

void setColortemp(int raw) {
  writeIntAttr("/sys/class/disp/disp/attr/color_temperature", raw);
}
void setContrast(int raw) {
  writeIntAttr("/sys/class/disp/disp/attr/enhance_contrast", raw);
}
void setSaturation(int raw) {
  writeIntAttr("/sys/class/disp/disp/attr/enhance_saturation", raw);
}
void setExposure(int raw) {
  writeIntAttr("/sys/class/disp/disp/attr/enhance_bright", raw);
}

// ---------------------------------------------------------------- audio

void setVolume(int pct) {
  // fire and forget: the mixer may block while other daemons hold the device
  std::thread([pct] { setVolumeBlocking(pct); }).detach();
}

void setVolumeBlocking(int pct) {
  pct = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
  snd_mixer_t* mixer = nullptr;
  if (snd_mixer_open(&mixer, 0) != 0) return;
  if (snd_mixer_attach(mixer, "hw:0") != 0) {
    snd_mixer_close(mixer);
    return;
  }
  snd_mixer_selem_register(mixer, nullptr, nullptr);
  snd_mixer_load(mixer);

  snd_mixer_elem_t* elem = nullptr;
  for (elem = snd_mixer_first_elem(mixer); elem; elem = snd_mixer_elem_next(elem)) {
    const char* name = snd_mixer_selem_get_name(elem);
    if (!name) continue;
    if (strcmp(name, "digital volume") == 0) {
      // reversed mapping (see NextUI msettings): 0 volume = full attenuation
      snd_mixer_selem_set_playback_volume_all(elem, (long)(100 - pct));
    } else if (strcmp(name, "DAC volume") == 0) {
      snd_mixer_selem_set_playback_volume_all(elem, pct == 0 ? 0 : 160);
    }
  }
  snd_mixer_close(mixer);
  // speaker amp mute for true silence
  writeFile("/sys/class/speaker/mute", pct == 0 ? "1" : "0");
}

int volume() {
  snd_mixer_t* mixer = nullptr;
  if (snd_mixer_open(&mixer, 0) != 0) return -1;
  if (snd_mixer_attach(mixer, "hw:0") != 0) {
    snd_mixer_close(mixer);
    return -1;
  }
  snd_mixer_selem_register(mixer, nullptr, nullptr);
  snd_mixer_load(mixer);
  int out = -1;
  for (snd_mixer_elem_t* elem = snd_mixer_first_elem(mixer); elem;
       elem = snd_mixer_elem_next(elem)) {
    const char* name = snd_mixer_selem_get_name(elem);
    if (name && strcmp(name, "digital volume") == 0) {
      long v = 0;
      if (snd_mixer_selem_get_playback_volume(elem, SND_MIXER_SCHN_FRONT_LEFT,
                                              &v) == 0)
        out = (int)(100 - v);
    }
  }
  snd_mixer_close(mixer);
  return out;
}

// ---------------------------------------------------------------- LEDs

std::vector<LedEffect> ledEffects() {
  std::vector<LedEffect> out;
  // "TRIMUI LED Animation Effect names:\n0 - disable\n1 - linear\n..."
  for (const std::string& line : split(readFile("/sys/class/led_anim/effect_names"), '\n')) {
    size_t dash = line.find(" - ");
    if (dash == std::string::npos) continue;
    LedEffect e;
    e.id = atoi(trim(line.substr(0, dash)).c_str());
    e.name = trim(line.substr(dash + 3));
    out.push_back(e);
  }
  return out;
}

void setLedEnable(bool on) { writeFile("/sys/class/led_anim/enable", on ? "1" : "0"); }

void setLedEffectAll(int id) {
  for (const char* group : {"effect_f1", "effect_f2", "effect_l", "effect_r",
                            "effect_m", "effect_lr"})
    writeIntAttr((std::string(kLedDir) + "/" + group).c_str(), id);
}

void setLedColor(unsigned rgb) {
  std::string hex = fmt("%06X", rgb & 0xFFFFFF);
  for (const char* group : {"effect_rgb_hex_f1", "effect_rgb_hex_f2",
                            "effect_rgb_hex_l", "effect_rgb_hex_r",
                            "effect_rgb_hex_m", "effect_rgb_hex_lr"})
    writeFile((std::string(kLedDir) + "/" + group).c_str(), hex.c_str());
}

void setLedBrightness(int scale) {
  writeIntAttr("/sys/class/led_anim/max_scale", scale);
}

int ledBrightness() {
  std::string v = readFile("/sys/class/led_anim/max_scale");
  return v.empty() ? -1 : atoi(v.c_str());
}

// ---------------------------------------------------------------- power

void reboot() {
  LOG_INFO("reboot requested");
  sync();
  system("reboot");
}

void poweroff() {
  LOG_INFO("poweroff requested");
  sync();
  system("poweroff");
}

// ---------------------------------------------------------------- storage

Storage storage() {
  Storage s;
  struct statvfs vfs;
  if (statvfs(sdcardRoot().c_str(), &vfs) == 0) {
    s.totalMB = (long)((double)vfs.f_blocks * vfs.f_frsize / (1024 * 1024));
    s.freeMB = (long)((double)vfs.f_bavail * vfs.f_frsize / (1024 * 1024));
  }
  return s;
}


// ---------------------------------------------------------------- network

namespace {
// run a command and return true when the output contains `needle`
bool probe(const char* cmd, const char* needle) {
  FILE* f = popen(cmd, "r");
  if (!f) return false;
  char buf[512];
  bool found = false;
  while (fgets(buf, sizeof(buf), f)) {
    if (strstr(buf, needle)) {
      found = true;
      break;
    }
  }
  pclose(f);
  return found;
}
}  // namespace

namespace {
std::atomic<bool> gWifiUp{false};
std::atomic<bool> gBtUp{false};
std::atomic<bool> gProbeRunning{false};

void probeOnce() {
  // timeout + closed stdin: bluetoothctl happily waits forever otherwise,
  // and a hanging probe used to freeze the whole UI thread on startup
  bool wifi = probe("timeout 2 wpa_cli -i wlan0 status </dev/null 2>/dev/null",
                    "wpa_state=COMPLETED");
  bool bt = probe("timeout 2 bluetoothctl devices Connected </dev/null 2>/dev/null",
                  "Device ");
  gWifiUp.store(wifi);
  gBtUp.store(bt);
  gProbeRunning.store(false);
}
}  // namespace

void startConnectivityProbe() {
  // fire and forget: the shell polls the cached flags, never the device
  if (gProbeRunning.exchange(true)) return;
  std::thread(probeOnce).detach();
}

bool wifiConnected() { return gWifiUp.load(); }
bool bluetoothConnected() { return gBtUp.load(); }

}  // namespace dev
}  // namespace ndsui
