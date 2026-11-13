#!/usr/bin/env bash
# Builds Sun-Loops-Studio-<version>-macOS.dmg (0.63.0): one universal app (Apple silicon and Intel) with the SunBurst and HexForge
# plugins inside it, in a disk image with a link to Applications.
#
#   scripts/build_macos.sh               # configure + build in build-macos/, write the .dmg to dist/
#
# Needs: macOS 11 or newer with the Xcode command line tools (xcode-select --install), cmake and ninja (brew install cmake ninja).
# ARCHS="arm64" builds for Apple silicon only (faster). The app is signed "ad hoc" (no Apple Developer certificate): macOS
# asks for confirmation the first time it is opened (right-click > Open, or System Settings > Privacy & Security > Open Anyway).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${BUILD_DIR:-$root/build-macos}"
dist="${DIST_DIR:-$root/dist}"
archs="${ARCHS:-arm64;x86_64}"
version="$(sed -n 's/^project(SunLoopsStudio VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")"
[ -n "$version" ] || { echo "cannot read the version from CMakeLists.txt" >&2; exit 1; }

cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$archs" -DSLS_BUILD_TEST_PLUGINS=OFF \
      ${FETCHCONTENT_SOURCE_DIR_JUCE:+-DFETCHCONTENT_SOURCE_DIR_JUCE="$FETCHCONTENT_SOURCE_DIR_JUCE"}
cmake --build "$build" --target SunLoopsStudio

app_built="$build/SunLoopsStudio_artefacts/Release/Sun Loops Studio.app"
[ -d "$app_built" ] || { echo "the app was not built: $app_built" >&2; exit 1; }

stage="$build/dmg"
rm -rf "$stage"
mkdir -p "$stage"
app="$stage/Sun Loops Studio.app"
cp -R "$app_built" "$app"

# The bundled plugins go inside the app (Contents/PlugIns/VST3), where the program looks for them.
mkdir -p "$app/Contents/PlugIns/VST3"
for plugin in SunBurst HexForge; do
    bundle="$build/${plugin}_artefacts/Release/VST3/$plugin.vst3"
    if [ -d "$bundle" ]; then
        cp -R "$bundle" "$app/Contents/PlugIns/VST3/"
    else
        echo "warning: $plugin.vst3 was not built" >&2
    fi
done

# Ad-hoc signature: required on Apple silicon (an unsigned program does not start at all), inside out.
for plugin in "$app/Contents/PlugIns/VST3/"*.vst3; do
    [ -d "$plugin" ] && codesign --force --sign - --timestamp=none "$plugin"
done
codesign --force --sign - --timestamp=none "$app" # (the microphone question comes from Info.plist)
codesign --verify --deep --strict "$app"
echo "Architectures: $(lipo -archs "$app/Contents/MacOS/Sun Loops Studio")"

ln -s /Applications "$stage/Applications"
mkdir -p "$dist"
out="$dist/Sun-Loops-Studio-$version-macOS.dmg"
rm -f "$out"
hdiutil create -volname "Sun Loops Studio $version" -srcfolder "$stage" -ov -format UDZO "$out"
echo
echo "Built $out ($(du -h "$out" | cut -f1))"
