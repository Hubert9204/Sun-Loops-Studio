#!/usr/bin/env bash
# Installs the build dependencies of Sun Loops Studio on Nobara / Fedora.
# It only installs packages: it does not upgrade the system and does not touch audio services.
set -euo pipefail

# On Fedora with PipeWire, the JACK development files come from pipewire-jack-audio-connection-kit-devel.
# Do not also install jack-audio-connection-kit-devel: the two conflict.
packages=(
    gcc-c++ cmake ninja-build git pkgconf-pkg-config
    alsa-lib-devel
    pipewire-jack-audio-connection-kit-devel
    freetype-devel fontconfig-devel
    libX11-devel libXext-devel libXinerama-devel libXrandr-devel
    libXcursor-devel libXrender-devel libXcomposite-devel
    mesa-libGL-devel
    ladspa-devel    # the LADSPA header: hosting LADSPA plugins (VST3 and LV2 need nothing extra)
)

sudo dnf install -y "${packages[@]}"

echo
echo "Dependencies installed. Build with:"
echo "  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release"
echo "  cmake --build build"
echo "  ./build/SunLoopsStudio_artefacts/Release/'Sun Loops Studio'"
