#!/usr/bin/env bash
# Makes the demo projects (resources/demos/*.slp) again: builds DemoTool, writes its catalog (the plugins' preset states, the
# effects' parameter tables), writes the three projects and renders each one, printing its levels.
#
#   tools/demos/make_demos.sh            # uses build/ (configured with -DSLS_BUILD_DEMO_TOOL=ON if needed)
#   RENDER=0 tools/demos/make_demos.sh   # without the renders (they take about a minute each)
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="${BUILD_DIR:-$root/build}"
out="$root/resources/demos"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSLS_BUILD_DEMO_TOOL=ON >/dev/null
cmake --build "$build" --target SunLoopsDemoTool
tool="$build/SunLoopsDemoTool_artefacts/Release/SunLoopsDemoTool"

"$tool" catalog "$work/catalog.json"
"$tool" sunrise "$work/sunrise-drive.slp"
python3 "$root/tools/demos/white_line.py" "$work/catalog.json" "$work/white-line.slp"
python3 "$root/tools/demos/ryo.py" "$work/catalog.json" "$work/ryo.slp"

# The plugins' descriptions name the folder they were found in on this machine: write where an installed program has them
# (the program finds SunBurst and HexForge by name and id wherever they are).
for f in sunrise-drive white-line ryo; do
    sed -E 's#file=\\"[^"\\]*/(SunBurst|HexForge)\.vst3\\"#file=\\"/usr/lib/vst3/\1.vst3\\"#g' "$work/$f.slp" > "$out/$f.slp"
    if [ "${RENDER:-1}" != 0 ]; then
        "$tool" render "$out/$f.slp" "$work/$f.wav"
    fi
done
echo "Written: $out/{sunrise-drive,white-line,ryo}.slp"
