# Building, installing and testing

## Requirements

- A C++20 compiler: GCC 13 or newer. The automated checks run with GCC 13; the maintainer builds and runs it on
  Nobara 44 (GCC 16).
- CMake 3.22 or newer (3.28 or newer for a clean `cmake --install`, see below) and Ninja.
- Development files for ALSA, JACK, FreeType, Fontconfig, X11, OpenGL and **libcurl** (since 0.43.0: the update check and crash reports).
- Optional, at run time: **ffmpeg** (`sudo dnf install ffmpeg` on Nobara / Fedora) for exporting MP3 and AAC and for
  importing AAC and the sound of video files. Everything else works without it.
- Network access the first time you configure: CMake downloads JUCE 8.0.4 and spdlog 1.14.1 from GitHub (shallow clones, pinned
  tags) and nlohmann/json 3.11.3 (release archive, checked by SHA-256).

### Nobara / Fedora

```bash
./scripts/setup_nobara.sh
```

The script runs `sudo dnf install` for: `gcc-c++ cmake ninja-build git pkgconf-pkg-config alsa-lib-devel
pipewire-jack-audio-connection-kit-devel freetype-devel fontconfig-devel libX11-devel libXext-devel
libXinerama-devel libXrandr-devel libXcursor-devel libXrender-devel libXcomposite-devel mesa-libGL-devel libcurl-devel ladspa-devel`
(`ladspa-devel` only provides `ladspa.h`: without it the program builds but cannot host LADSPA plugins; VST3 and LV2 need nothing extra).
It only installs packages: it does not upgrade the system or touch audio services. On Fedora with PipeWire the
JACK headers come from `pipewire-jack-audio-connection-kit-devel`; do not also install
`jack-audio-connection-kit-devel`, the two conflict.

### Other distributions

Install the equivalents yourself. On Debian / Ubuntu the usual names are `build-essential cmake ninja-build git
libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxext-dev libxinerama-dev
libxrandr-dev libxcursor-dev libxrender-dev libxcomposite-dev libgl1-mesa-dev libcurl4-openssl-dev`. This path has not been tested.

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

The build also makes **SunBurst** (0.48.0), the synthesizer plugin: `build/SunBurst_artefacts/Release/VST3/SunBurst.vst3` and the
standalone program `build/SunBurst_artefacts/Release/Standalone/SunBurst`. Sun Loops Studio run from the build folder finds the
VST3 there. `-DSLS_BUILD_SUNBURST=OFF` leaves it out. To use it in another program, copy `SunBurst.vst3` to `~/.vst3`.

## Install

```bash
cmake --install build --prefix ~/.local
```

Installs, under the prefix: the `sun-loops-studio` command (`bin/`), the SunBurst plugin (`lib/vst3/SunBurst.vst3`, found by the
program next to it), the application-menu entry
(`share/applications/sun-loops-studio.desktop`), the icons (`share/icons/hicolor/{16x16,256x256}/apps/`) and the
file type of `.slp` projects (`share/mime/packages/sun-loops-studio.xml`).
Make sure `~/.local/bin` is in your `PATH`. KDE normally picks the menu entry up within moments; if not, log out and
in again or run `kbuildsycoca6`.

JUCE ships install rules of its own (headers, CMake files). `CMakeLists.txt` excludes them with `EXCLUDE_FROM_ALL`,
which needs CMake 3.28; on an older CMake `cmake --install` also copies JUCE. To uninstall, delete the four kinds of
file listed above.

## AppImage

```bash
./scripts/build_appimage.sh        # writes dist/Sun-Loops-Studio-<version>-x86_64.AppImage
chmod +x dist/Sun-Loops-Studio-*.AppImage
./dist/Sun-Loops-Studio-*.AppImage [project.slp]
```

The script configures a Release build in `build-appimage/`, installs it into an `AppDir`, lets linuxdeploy copy in
the libraries the program needs and packs the result with appimagetool. It downloads those two tools from GitHub
once, at pinned versions, and refuses them if their SHA-256 does not match (`build-appimage/tools/`). Set
`FETCHCONTENT_SOURCE_DIR_JUCE` to a local JUCE checkout to avoid downloading JUCE again. `desktop-file-utils` makes the
script check the `.desktop` file; without it the check is skipped with a warning.

What is inside and what is not: the program and the few libraries that are not everywhere (libpng, bzip2, brotli).
Like every AppImage it relies on the system for glibc, ALSA, FreeType, Fontconfig, X11 and OpenGL, which any desktop
Linux, Nobara included, provides. Because glibc is not bundled, **build on the oldest system you want to support**; an
AppImage built on Ubuntu 24.04 (glibc 2.39) runs on newer systems, not on older ones.

AppImages normally mount themselves through FUSE 2. Fedora and Nobara may not have it installed: either
`sudo dnf install fuse-libs`, or run with `--appimage-extract-and-run` (slower start, no FUSE needed).
The AppImage does not add a menu entry by itself; tools such as AppImageLauncher or Gear Lever do that, or use
`cmake --install` instead.

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
| Crackling, clicks or dropouts during playback | The audio buffer is too small for the computer. Open **Audio...** and choose a bigger buffer, or press **Auto: set for this computer**. The default is 1024 samples; see the user guide, *Buffer size*. |
| No sound although a device is selected | Select the ALSA or JACK device that PipeWire exposes; check the master volume in the toolbar and that no channel is muted or another channel soloed. |
| Panels are in strange places | Click **Reset**, or delete `~/.config/SunLoopsStudio/SunLoopsStudio.settings`. |
| Keyboard shortcuts do nothing | Click the window once so it is active; shortcuts do not work while a dialog is open. |
| `cmake --install` copies a lot of JUCE files | CMake older than 3.28, see above. |
| MP3 / AAC export is greyed out ("needs ffmpeg"), or importing a video or `.aac` fails | `ffmpeg` is not installed or not on the `PATH`. Install it, or set `SLS_FFMPEG=/path/to/ffmpeg`. |
| Record says no audio input could be opened | No microphone or audio interface is available to the audio system. Plug one in and choose it in **Audio...** (on PipeWire check `wpctl status`). |
| A MIDI keyboard is not listed in **Audio...** | ALSA exposes it through `/dev/snd/seq`; check `aconnect -l`. Tick it in the dialog. |

JUCE has no native PipeWire backend: on Linux it uses ALSA and JACK, and PipeWire serves both through
`pipewire-alsa` and `pipewire-jack`, which Nobara installs by default. JUCE draws through X11; on a Wayland session
it runs through XWayland.

## Crash reports and the update check (0.43.0)

- `-DSLS_GITHUB_REPO=owner/repo` (default `Hubert9204/Sun-Loops-Studio`) is where crash reports go and updates come from.
- `-DSLS_CRASH_REPORT_TOKEN=...` builds a GitHub token into the program so that *Send report* works without the user setting one.
  Never commit it and think before using it: **anyone who has the program can read a built-in token** and use it to open issues
  (give it *Issues: Read and write* on that one repository and nothing else, and be ready to revoke it). Without it, users paste their
  own token in the program or send the report from the browser.
- `SLS_GITHUB_TOKEN` (environment) sets a token for one run; `SLS_GITHUB_API` points both calls at another server (tests, GitHub
  Enterprise).
- The AppImage keeps the program's symbol table (`NO_STRIP=1`, about 0.5 MB) so stack traces name every function; the program's own
  symbols are also exported (`--export-dynamic-symbol=_ZN3sls*` and `_ZN4juce*`), which names them even in a stripped build.
