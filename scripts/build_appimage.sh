#!/usr/bin/env bash
# Builds Sun-Loops-Studio-<version>-x86_64.AppImage.
#
#   scripts/build_appimage.sh            # configure + build in build-appimage/, write the AppImage to dist/
#
# Needs: cmake, ninja, a C++20 compiler, the JUCE build dependencies (see docs/building.md) and network access
# to download two pinned tools (checked against SHA-256): linuxdeploy and appimagetool.
# The result is built on the machine that runs this script, so it needs a glibc at least as new as the oldest
# system it should run on: build on the oldest distribution you want to support.
set -euo pipefail

LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20250213-2/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_SHA256="4648f278ab3ef31f819e67c30d50f462640e5365a77637d7e6f2ad9fd0b4522a"
APPIMAGETOOL_URL="https://github.com/AppImage/appimagetool/releases/download/1.9.0/appimagetool-x86_64.AppImage"
APPIMAGETOOL_SHA256="46fdd785094c7f6e545b61afcfb0f3d98d8eab243f644b4b17698c01d06083d1"

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${BUILD_DIR:-$root/build-appimage}"
tools="$build/tools"
appdir="$build/AppDir"
dist="${DIST_DIR:-$root/dist}"
version="$(sed -n 's/^project(SunLoopsStudio VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")"
[ -n "$version" ] || { echo "cannot read the version from CMakeLists.txt" >&2; exit 1; }

fetch() { # url sha256 file
    if [ -f "$3" ] && echo "$2  $3" | sha256sum -c --status; then return; fi
    echo "Downloading $(basename "$3")"
    curl -fsSL -o "$3" "$1"
    echo "$2  $3" | sha256sum -c --status || { echo "checksum mismatch for $3" >&2; rm -f "$3"; exit 1; }
    chmod +x "$3"
}

mkdir -p "$tools" "$dist"
fetch "$LINUXDEPLOY_URL"  "$LINUXDEPLOY_SHA256"  "$tools/linuxdeploy-x86_64.AppImage"
fetch "$APPIMAGETOOL_URL" "$APPIMAGETOOL_SHA256" "$tools/appimagetool-x86_64.AppImage"

# AppImages need FUSE to mount themselves; where it is missing (containers, CI) they are extracted and run instead.
if ! command -v fusermount >/dev/null 2>&1 && ! command -v fusermount3 >/dev/null 2>&1; then
    export APPIMAGE_EXTRACT_AND_RUN=1
fi

cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      ${FETCHCONTENT_SOURCE_DIR_JUCE:+-DFETCHCONTENT_SOURCE_DIR_JUCE="$FETCHCONTENT_SOURCE_DIR_JUCE"}
cmake --build "$build" --target SunLoopsStudio

rm -rf "$appdir"
DESTDIR="$appdir" cmake --install "$build" --prefix /usr

# linuxdeploy copies the libraries the program needs into the AppDir (skipping the ones every system provides,
# such as glibc and the ALSA/GL/X11 client libraries) and writes AppRun.
export LDAI_NO_APPSTREAM=1
# Keep the program's symbol table (about 0.5 MB in the AppImage): crash reports name every function with it (0.43.0).
export NO_STRIP=1
"$tools/linuxdeploy-x86_64.AppImage" --appdir "$appdir" \
    --executable "$appdir/usr/bin/sun-loops-studio" \
    --desktop-file "$appdir/usr/share/applications/sun-loops-studio.desktop" \
    --icon-file "$root/resources/icons/app_icon_256.png" --icon-filename sun-loops-studio

out="$dist/Sun-Loops-Studio-$version-x86_64.AppImage"
validate=()
if ! command -v desktop-file-validate >/dev/null 2>&1; then
    echo "warning: desktop-file-validate not found (package desktop-file-utils): skipping the .desktop check" >&2
    validate=(--no-validate)
fi
ARCH=x86_64 "$tools/appimagetool-x86_64.AppImage" --no-appstream "${validate[@]}" "$appdir" "$out"
chmod +x "$out"
echo
echo "Built $out ($(du -h "$out" | cut -f1))"
