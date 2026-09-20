// Device control: display (brightness/colortemp/enhance), audio (ALSA),
// LEDs (led_anim sysfs), power (reboot/poweroff), storage.
#pragma once

#include <string>
#include <vector>

namespace ndsui {

namespace dev {

// ---- display ----
void setBrightness(int raw);      // 0-255 (/dev/disp ioctl 0x102)
void setColortemp(int raw);       // 0-255
void setContrast(int raw);        // 0-100
void setSaturation(int raw);      // 0-100
void setExposure(int raw);        // 0-100 (enhance_bright)

// ---- audio ----
// The ALSA mixer can block while the stock audio daemons hold the device, so
// writes happen on a detached thread and the value is cached by the caller.
void setVolume(int pct);          // 0-100 (async)
void setVolumeBlocking(int pct);  // 0-100 (do not call from the UI thread)
int volume();                     // best effort, may return -1

// ---- LEDs ----
struct LedEffect {
  int id;
  std::string name;
};
std::vector<LedEffect> ledEffects();
void setLedEnable(bool on);
void setLedEffectAll(int id);
void setLedColor(unsigned rgb);   // 0xRRGGBB
void setLedBrightness(int scale); // max_scale
int ledBrightness();

// ---- power ----
void reboot();
void poweroff();

// ---- storage ----
struct Storage {
  long totalMB = 0;
  long freeMB = 0;
};
Storage storage();

// ---- connectivity (top bar state) ----
void startConnectivityProbe();  // non-blocking, cached flags
bool wifiConnected();
bool bluetoothConnected();

}  // namespace dev
}  // namespace ndsui
