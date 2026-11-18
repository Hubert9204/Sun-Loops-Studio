#!/usr/bin/env bash
# Takes the README's screenshots (docs/images/*.png) from a Linux build, in a virtual display: Xvfb for the screen, openbox to
# place the windows, xdotool for the keys and clicks, ImageMagick's import for the pictures (0.77.1). The program starts with a
# fresh home folder, so it opens as on a first start: the demo song "Sunrise Drive", the default panel layout, no audio device.
#
#   scripts/capture_screenshots.sh [build folder] [output folder]          (defaults: build, docs/images)
#
# Needs a build of the program and of the four instruments' Standalone versions:
#   cmake --build build --target SunLoopsStudio SunBurst_Standalone HexForge_Standalone SunSymphony_Standalone SunRiff_Standalone
# and: Xvfb, openbox, xdotool, ImageMagick (Fedora: xorg-x11-server-Xvfb openbox xdotool ImageMagick). The continuous
# integration runs it in .github/workflows/screenshots.yml whenever this script changes, or by hand (Actions > Screenshots).
#
# The clicks are made at fixed places of the 1280 x 860 window (the default layout of a first start): if the interface moves,
# look at the pictures and change the coordinates below.
set -euo pipefail

build="$(cd "${1:-build}" && pwd)"
out="$(mkdir -p "${2:-docs/images}" && cd "${2:-docs/images}" && pwd)"
app="$build/SunLoopsStudio_artefacts/Release/Sun Loops Studio"
[ -x "$app" ] || { echo "not built: $app" >&2; exit 1; }

# --- a virtual screen, a window manager and a home of its own ---------------------------------------------------------------
export HOME="$(mktemp -d)"
export XDG_CONFIG_HOME="$HOME/.config" XDG_DATA_HOME="$HOME/.local/share" XDG_CACHE_HOME="$HOME/.cache"
mkdir -p "$HOME/Music"
export DISPLAY=:97
Xvfb "$DISPLAY" -screen 0 1600x1000x24 -nolisten tcp >/dev/null 2>&1 &
xvfb=$!
pids=("$xvfb")
cleanup() { for p in "${pids[@]}"; do kill "$p" 2>/dev/null || true; done; }
trap cleanup EXIT
for _ in $(seq 50); do xdpyinfo >/dev/null 2>&1 && break; sleep 0.2; done
openbox >/dev/null 2>&1 &
pids+=($!)
sleep 1

# The window whose title contains $1 (waits up to 60 s).
window() {
    for _ in $(seq 120); do
        local id
        id="$(xdotool search --onlyvisible --name "$1" 2>/dev/null | head -1 || true)"
        [ -n "$id" ] && { echo "$id"; return; }
        sleep 0.5
    done
    echo "no window \"$1\"" >&2
    return 1
}
# Gives window $1 the size $2 x $3 at the screen's top left corner and the keyboard.
place() {
    xdotool windowsize "$1" "$2" "$3" windowmove "$1" 0 0
    xdotool windowactivate --sync "$1" 2>/dev/null || xdotool windowfocus "$1"
    sleep 1.5
}
# The window $1 as the picture $2.
shot() {
    sleep "${3:-1.5}"
    import -window "$1" "$out/$2"
    echo "wrote $out/$2 ($(identify -format '%wx%h' "$out/$2"))"
}
key() { xdotool key --clearmodifiers "$@"; sleep 1; }
# A click at $2, $3 inside window $1.
click_in() { xdotool mousemove --window "$1" "$2" "$3" click 1; sleep 1; }

# --- Sun Loops Studio ---------------------------------------------------------------------------------------------------
"$app" >/dev/null 2>&1 &
pids+=($!)
main="$(window "Sun Loops Studio")"
place "$main" 1280 860
sleep 6 # the demo song, the built-in plugins and the sounds load

# The main window as it opens: browser, Channel Rack and Playlist with the demo song.
shot "$main" overview.png

# The Mixer (F9) with the demo's mix: the selected channel (Drums) shows its track's three effects on the right.
key F9
shot "$main" mixer.png
key F9

# The Piano Roll (F7) on the Piano channel's own pattern: open it, go to the previous pattern ("Piano", the arrow left of the
# pattern box), then select the Piano channel (a click on its strip's left edge in the Channel Rack), which scrolls the roll to
# its notes.
key F7
click_in "$main" 377 58
click_in "$main" 284 342
shot "$main" pianoroll.png 2.5
key F7

kill "${pids[-1]}" 2>/dev/null || true
sleep 2

# --- the four instruments, as their Standalone versions -------------------------------------------------------------------
for plugin in SunBurst HexForge SunSymphony SunRiff; do
    standalone="$build/${plugin}_artefacts/Release/Standalone/$plugin"
    if [ ! -x "$standalone" ]; then
        echo "not built, skipped: $standalone" >&2
        continue
    fi
    "$standalone" >/dev/null 2>&1 &
    pid=$!
    win="$(window "$plugin")"
    xdotool windowmove "$win" 0 0
    xdotool windowactivate --sync "$win" 2>/dev/null || true
    sleep 4
    # SunSymphony opens its Library page while no orchestra is installed: its Close button shows the instrument itself.
    [ "$plugin" = SunSymphony ] && click_in "$win" 705 50
    name="$(echo "$plugin" | tr '[:upper:]' '[:lower:]').png"
    shot "$win" "$name" 2
    kill "$pid" 2>/dev/null || true
    sleep 1
done
