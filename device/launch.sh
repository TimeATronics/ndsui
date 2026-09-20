#!/bin/sh
# NDSUI launcher - runs the DS styled frontend as a normal app.
APPDIR=/mnt/SDCARD/Apps/NDSUI

cd "$APPDIR"
export LD_LIBRARY_PATH="$APPDIR:/usr/trimui/lib:/usr/lib:$LD_LIBRARY_PATH"
exec ./ndsui "$@" > "$APPDIR/ndsui.log" 2>&1
