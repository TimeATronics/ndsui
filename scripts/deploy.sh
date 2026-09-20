#!/bin/bash
# Deploy NDSUI to the Brick: stages the app (binary, font, boot hook, config)
# and copies it to /mnt/SDCARD/Apps/NDSUI/.
#
#   bash scripts/deploy.sh
#
# Test on device (kills MainUI, runs the launcher, screenshots):
#   bash scripts/deploy.sh && bash scripts/run-device.sh
set -e
cd "$(dirname "$0")/.."

STAGE_WIN='C:\Users\Aradhya\AppData\Local\Temp\opencode\ndsui-stage\NDSUI'
STAGE=/mnt/c/Users/Aradhya/AppData/Local/Temp/opencode/ndsui-stage/NDSUI
PLINK=/mnt/c/Users/Aradhya/MyFiles/Applications/debian/plink.exe
PSCP=/mnt/c/Users/Aradhya/MyFiles/Applications/debian/pscp.exe
IP=${NDSUI_IP:-192.168.137.24}

make strip
rm -rf /mnt/c/Users/Aradhya/AppData/Local/Temp/opencode/ndsui-stage
mkdir -p "$STAGE/boot"
cp dist/ndsui "$STAGE/"
cp assets/NDS12.ttf assets/Fredoka-Medium.ttf assets/Fredoka-SemiBold.ttf "$STAGE/"
cp -r assets/models "$STAGE/"
cp -r assets/skin "$STAGE/"       # stock section/settings icons
cp device/config.json device/launch.sh device/ic-ndsui.png "$STAGE/"
cp device/boot/ndsui_boot.sh "$STAGE/boot/"

# keep user data (settings, recents, launcher marker) across deploys - a plain
# rm -rf of the app dir used to wipe favourites/recents on every update
"$PLINK" -ssh root@$IP -pw root -batch "mkdir -p /tmp/ndsui-keep && cp -f /mnt/SDCARD/Apps/NDSUI/settings.cfg /tmp/ndsui-keep/ 2>/dev/null; cp -f /mnt/SDCARD/Apps/NDSUI/recents.txt /tmp/ndsui-keep/ 2>/dev/null; cp -f /mnt/SDCARD/Apps/NDSUI/use_as_launcher /tmp/ndsui-keep/ 2>/dev/null; true"
"$PLINK" -ssh root@$IP -pw root -batch "rm -rf /mnt/SDCARD/Apps/NDSUI"
"$PSCP" -pw root -r "$STAGE_WIN" root@$IP:/mnt/SDCARD/Apps/
"$PLINK" -ssh root@$IP -pw root -batch "cp -f /tmp/ndsui-keep/settings.cfg /mnt/SDCARD/Apps/NDSUI/ 2>/dev/null; cp -f /tmp/ndsui-keep/recents.txt /mnt/SDCARD/Apps/NDSUI/ 2>/dev/null; cp -f /tmp/ndsui-keep/use_as_launcher /mnt/SDCARD/Apps/NDSUI/ 2>/dev/null; rm -f /mnt/SDCARD/Apps/NDSUI/boot_fails; chmod +x /mnt/SDCARD/Apps/NDSUI/launch.sh /mnt/SDCARD/Apps/NDSUI/ndsui /mnt/SDCARD/Apps/NDSUI/boot/start_launcher.sh /mnt/SDCARD/Apps/NDSUI/boot/install_boot_hook.sh; ls /mnt/SDCARD/Apps/NDSUI/"

# boot hook: NDSUI boots as a stock "app session" (System/starts script hands
# over from MainUI) so keymon hotkeys / OSD / power button keep working. Any
# earlier runtrimui.sh patch is reverted - that approach broke them.
"$PLINK" -ssh root@$IP -pw root -batch "[ -f /usr/trimui/bin/runtrimui.sh.ndsui-backup ] && cp -f /usr/trimui/bin/runtrimui.sh.ndsui-backup /usr/trimui/bin/runtrimui.sh; mkdir -p /mnt/SDCARD/System/starts; true"
"$PSCP" -pw root "$STAGE_WIN\\boot\\ndsui_boot.sh" root@$IP:/mnt/SDCARD/System/starts/ndsui_boot.sh
"$PLINK" -ssh root@$IP -pw root -batch "chmod +x /mnt/SDCARD/System/starts/ndsui_boot.sh; grep -c NDSUI_HOOK /usr/trimui/bin/runtrimui.sh; ls /mnt/SDCARD/System/starts/"
