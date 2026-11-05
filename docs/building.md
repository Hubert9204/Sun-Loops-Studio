# Building, installing and testing

## Requirements

- A C++20 compiler: GCC 13 or newer. The automated checks run with GCC 13; the maintainer builds and runs it on
  Nobara 44 (GCC 16).
- CMake 3.22 or newer (3.28 or newer for a clean `cmake --install`, see below) and Ninja.
- Development files for ALSA, JACK, FreeType, Fontconfig, X11 and OpenGL.
- Network access the first time you configure: CMake downloads JUCE 8.0.4 from GitHub (shallow clone, pinned tag).

### Nobara / Fedora

```bash
./scripts/setup_nobara.sh
```

The script runs `sudo dnf install` for: `gcc-c++ cmake ninja-build git pkgconf-pkg-config alsa-lib-devel
pipewire-jack-audio-connection-kit-devel freetype-devel fontconfig-devel libX11-devel libXext-devel
libXinerama-devel libXrandr-devel libXcursor-devel libXrender-devel libXcomposite-devel mesa-libGL-devel`.
It only installs packages: it does not upgrade the system or touch audio services. On Fedora with PipeWire the
JACK headers come from `pipewire-jack-audio-connection-kit-devel`; do not also install
`jack-audio-connection-kit-devel`, the two conflict.

### Other distributions

Install the equivalents yourself. On Debian / Ubuntu the usual names are `build-essential cmake ninja-build git
libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxext-dev libxinerama-dev
libxrandr-dev libxcursor-dev libxrender-dev libxcomposite-dev libgl1-mesa-dev`. This path has not been tested.

## Build

```bash
cd Sun-Loops-Studio
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/SunLoopsStudio_artefacts/Release/"Sun Loops Studio"
```

The program file has spaces in its name, so quote it. The first build compiles JUCE modules and takes a few minutes;
later builds only recompile what changed. To update: `git pull`, then `cmake --build build`.

Use `-DCMAKE_BUILD_TYPE=Debug` while developing: it enables JUCE's `jassert` checks.

## Install

```bash
cmake --install build --prefix ~/.local
```

Installs, under the prefix: the `sun-loops-studio` command (`bin/`), the application-menu entry
(`share/applications/sun-loops-studio.desktop`) and the icons (`share/icons/hicolor/{16x16,256x256}/apps/`).
Make sure `~/.local/bin` is in your `PATH`. KDE normally picks the menu entry up within moments; if not, log out and
in again or run `kbuildsycoca6`.

JUCE ships install rules of its own (headers, CMake files). `CMakeLists.txt` excludes them with `EXCLUDE_FROM_ALL`,
which needs CMake 3.28; on an older CMake `cmake --install` also copies JUCE. To uninstall, delete the four kinds of
file listed above.

## Tests

```bash
./build/SunLoopsStudioTests_artefacts/Release/SunLoopsStudioTests     # prints every check
ctest --test-dir build                                                 # same, as pass/fail
```

The tests are headless: no audio device and no display. The last line says `ALL TESTS PASSED`; the exit code is 0
only then. See [architecture.md](architecture.md#testing) for what they cover.

The user interface itself is not covered by automated tests. During development it was exercised in a virtual
display (`Xvfb`) with `xdotool` for mouse and keys and ImageMagick's `import` for screenshots; this is a manual check,
not part of the repository.

## Troubleshooting

| Symptom | Cause and fix |
| --- | --- |
| `cmake: command not found`, `ninja: command not found` | Dependencies are not installed. Run `./scripts/setup_nobara.sh`. |
| `No such file or directory` for the repository or `CMakeLists.txt` missing | You are in the wrong folder. `cd` into the cloned `Sun-Loops-Studio` directory before running CMake. |
| `bash: ./build/...: No such file` | The build did not finish, or the path is not quoted (`"Sun Loops Studio"` has spaces). |
| Configure fails while fetching JUCE | No network, or GitHub unreachable. Retry; the download happens once and is cached in `build/_deps`. |
| `-Wmismatched-new-delete` from `tests/test_core.cpp` | Newer GCC versions warn about the test's allocation counter. The file silences it with pragmas; if you still see it, please report your compiler version. |
| Window opens but the status line says `No audio device` | Open **Audio...** and choose an output. Check that your system has one (`wpctl status` on PipeWire). |
| No sound although a device is selected | Select the ALSA or JACK device that PipeWire exposes; check the master volume in the toolbar and that no channel is muted or another channel soloed. |
| Panels are in strange places | Click **Reset**, or delete `~/.config/SunLoopsStudio/SunLoopsStudio.settings`. |
| Keyboard shortcuts do nothing | Click the window once so it is active; shortcuts do not work while a dialog is open. |
| `cmake --install` copies a lot of JUCE files | CMake older than 3.28, see above. |

JUCE has no native PipeWire backend: on Linux it uses ALSA and JACK, and PipeWire serves both through
`pipewire-alsa` and `pipewire-jack`, which Nobara installs by default. JUCE draws through X11; on a Wayland session
it runs through XWayland.
