#!/bin/sh
# Regenerates expected/config-defaults.json: the config.json real Quickshell writes for ii's
# Config.qml when there is none. Needs quickshell and a Wayland session (no window is shown).
# All XDG directories point at a scratch directory, so the user's config is not touched.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
ii=$(cd "$here/../../../dots/.config/quickshell/ii" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/shell" "$tmp/xdg"
for entry in "$ii"/*; do
  [ "$(basename "$entry")" = shell.qml ] || ln -s "$entry" "$tmp/shell/$(basename "$entry")"
done
cat > "$tmp/shell/shell.qml" <<'QML'
//@ pragma UseQApplication
import QtQuick
import Quickshell
import qs.modules.common

ShellRoot {
    Timer { interval: 3000; running: true; onTriggered: Qt.quit() }
    Component.onCompleted: console.info(Config.filePath)
}
QML
XDG_CONFIG_HOME="$tmp/xdg" XDG_STATE_HOME="$tmp/xdg" XDG_CACHE_HOME="$tmp/xdg" timeout 30 qs -p "$tmp/shell/shell.qml" >/dev/null 2>&1 || true
cp "$tmp/xdg/illogical-impulse/config.json" "$here/expected/config-defaults.json"
echo "wrote expected/config-defaults.json"
