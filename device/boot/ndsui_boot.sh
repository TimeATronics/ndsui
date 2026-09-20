#!/bin/sh
# NDSUI boot hook - installed to /mnt/SDCARD/System/starts/ndsui_boot.sh
#
# The stock runtrimui.sh starts MainUI in a loop, then runs /tmp/cmd_to_run.sh
# for the app that asked for it. Everything stock (keymon hotkeys, the OSD,
# power-button handling, crash fallback) works inside such an "app session" -
# and NOT when a third-party launcher takes MainUI's place.
#
# So: when NDSUI is the default launcher (marker file), this script waits for
# MainUI to come up, queues NDSUI as the app to run and makes MainUI exit
# before it really renders. The boot loop then runs NDSUI as a normal app:
#   - MENU+SELECT OSD, keymon power-button handling all keep working
#   - quitting NDSUI (or a crash) brings back MainUI instead of a black screen
#
# Toggle the setting off in NDSUI (or delete the marker) to boot to MainUI.
# Recovery: remove Apps/NDSUI/use_as_launcher, or create System/no_ndsui.

APPDIR=/mnt/SDCARD/Apps/NDSUI
MARKER=$APPDIR/use_as_launcher
KILLSWITCH=/mnt/SDCARD/System/no_ndsui

# The stock starts loop waits only 3 s for the SD card, often not enough on a
# cold boot (this script - and everything after it - would be skipped). Wait
# properly; since the loop runs the scripts in order, this also fixes the
# scripts that come after ours.
i=0
while [ ! -d "$APPDIR" ] && [ $i -lt 60 ]; do
    sleep 0.5
    i=$((i + 1))
done
[ -d "$APPDIR" ] || exit 0

[ -f "$KILLSWITCH" ] && exit 0
[ -f "$MARKER" ] || exit 0
[ -f "$APPDIR/launch.sh" ] || exit 0

(
    # wait for MainUI (max 60 s), let it initialise, then swap in NDSUI
    i=0
    pid=""
    while [ $i -lt 120 ]; do
        pid=$(ps -w | grep '[M]ainUI' | awk '{print $1}' | head -1)
        [ -n "$pid" ] && break
        sleep 0.5
        i=$((i + 1))
    done
    [ -n "$pid" ] || exit 0

    sleep 3
    printf '#!/bin/sh\nsh %s/launch.sh\n' "$APPDIR" > /tmp/cmd_to_run.sh
    chmod +x /tmp/cmd_to_run.sh
    kill -9 "$pid" 2>/dev/null
    echo "ndsui_boot: handed over from MainUI (pid $pid)" >> "$APPDIR/ndsui.log"
) &
