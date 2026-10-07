#!/usr/bin/env bash
# Visual check of ii-shell's volume OSD against Quickshell's (`qs -c ii`), both on screen.
# Needs a running Hyprland session with `qs -c ii`, grim and ImageMagick. Starts
# ../../build-release/ii-shell unless an ii-shell is already running.
#
#   tests/visual/compare.sh [out-dir]
#
# Each shell's OSD is opened through its IPC (osdVolume trigger) and captured from screen, one
# at a time; both use the same layer namespace and geometry. The OSD's rectangle is taken from
# Hyprland's layer list, so the comparison includes the desktop behind its rounded corners.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
shell=$(cd "$here/../.." && pwd)
out=${1:-$(mktemp -d)}
mkdir -p "$out"

pgrep -x qs >/dev/null || { echo "qs -c ii is not running" >&2; exit 1; }
started=
if ! pgrep -f '/ii-shell$' >/dev/null; then
  "$shell/build-release/ii-shell" >"$out/ii-shell.log" 2>&1 &
  started=$!
  sleep 2
fi

# The OSD layer's logical rectangle on screen, once one is open.
osd_geometry() {
  hyprctl layers -j | python3 -c '
import json, sys
for mon in json.load(sys.stdin).values():
    for layers in mon["levels"].values():
        for l in layers:
            if l["namespace"] == "quickshell:onScreenDisplay":
                print("%d,%d %dx%d" % (l["x"], l["y"], l["w"], l["h"])); sys.exit()'
}

qs -c ii ipc call osdVolume trigger >/dev/null
sleep 0.8
geom=$(osd_geometry)
grim -g "$geom" "$out/qs.png"
sleep 2.5 # until qs's OSD has closed
"$shell/build-release/ii-shell" ipc call osdVolume trigger >/dev/null
sleep 0.8
grim -g "$geom" "$out/ours.png"
if [[ -n $started ]]; then
  kill -INT "$started"
  wait "$started" || true
fi

cd "$out"
magick qs.png ours.png -compose difference -composite -colorspace gray diff.png
n=$(magick diff.png -format '%[fx:w*h]' info:)
mean=$(magick diff.png -format '%[fx:mean*255]' info:)
over=$(magick diff.png -threshold 12.5% -format '%[fx:mean*w*h]' info:)
magick qs.png ours.png \( diff.png -auto-level \) -append compare.png
echo "mean |diff|: $mean / 255; pixels off by more than 32: $over of $n"
echo "side by side (qs, ii-shell, diff): $out/compare.png"
