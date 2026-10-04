#!/usr/bin/env bash
# Visual check of ii-shell's demo against Qt rendering the same QML (tests/visual/osd_demo.qml).
# Needs a running Hyprland session, grim and ImageMagick; uses a build in ../../build-release.
#
#   tests/visual/compare.sh [out-dir]
#
# Qt renders through its default GL backend at the output's scale (QT_SCALE_FACTOR reproduces
# 1.25x, since Qt on Hyprland otherwise renders at an integer 2x); ii-shell's window is captured
# from screen. Only pixels opaque in Qt's image are compared (the rest shows the desktop).
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
shell=$(cd "$here/../.." && pwd)
out=${1:-$(mktemp -d)}
scale=$(hyprctl monitors -j | python3 -c 'import json,sys; print(json.load(sys.stdin)[0]["scale"])')
read -r ow oh < <(hyprctl monitors -j | python3 -c '
import json,sys; m=json.load(sys.stdin)[0]; print(round(m["width"]/m["scale"]), round(m["height"]/m["scale"]))')
w=300 h=72
x=$(((ow - w) / 2)) y=$(((oh - h) / 2))

QT_FORCE_STDERR_LOGGING=1 QT_SCALE_FACTOR=$(python3 -c "print($scale / 2)") \
  qml6 "$here/render.qml" -- "$here/osd_demo.qml" "$out/qt.png" 2>/dev/null

"$shell/build-release/ii-shell" >"$out/ii-shell.log" 2>&1 &
pid=$!
sleep 2
grim -g "$x,$y ${w}x$h" "$out/ours.png"
kill -INT "$pid"
wait "$pid" || true

cd "$out"
magick qt.png -alpha extract -threshold 99% mask.png
magick qt.png -background black -alpha remove qt_rgb.png
magick ours.png -alpha off ours_rgb.png
magick ours_rgb.png qt_rgb.png -compose difference -composite \
  mask.png -compose multiply -composite -colorspace gray diff.png
n=$(magick mask.png -format '%[fx:mean*w*h]' info:)
mean=$(magick diff.png -format '%[fx:mean*255*w*h]' info: | awk -v n="$n" '{printf "%.2f", $1/n}')
over=$(magick diff.png -threshold 12.5% -format '%[fx:mean*w*h]' info:)
magick qt_rgb.png ours_rgb.png \( diff.png -auto-level \) -append compare.png
echo "mean |diff| over the panel: $mean / 255; pixels off by more than 32: $over of $n"
echo "side by side (Qt, ii-shell, diff): $out/compare.png"
