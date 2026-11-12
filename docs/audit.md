# Code audit (November 2026, after 0.10.0; notes added for 0.11.0)

Method: `clang-tidy` 18 with the `bugprone-*`, `performance-*`, `clang-analyzer-*`, `misc-unused-*` and `readability-redundant-*`
checks over every source file of the program; a script that looks for blocks of 7 or more identical lines; a script that lists
functions declared in a header and used nowhere else; reading of the real-time path (`Player`, `Effects`, `AudioEngine`).
The 0.10.0 audit had already removed ten unused getters and two duplicates; this one is the second pass.

## Fixed
- Three copies of the same "ask for a line of text" window (11 lines each) -> `promptForText` in `ui/TextPrompt.h`. This was the
  only repeated block of more than 7 lines in 11 000 lines of program code.
- Unused: `getArmedTrack`, `getSnapTicks`, five Playlist "for the GUI tests" accessors that no test ever used, two `getSelectedTrack`.
- `effectParamCount` had three identical `case` branches; `Sequence::make` took its list by value and copied it; a `20 * 48000`
  that is widened to 64 bits after the multiplication (harmless value, but a trap if the number grows).

## Looked at and left as it is
- Enums with an `int` base that clang-tidy would like as `uint8_t`: saves a few bytes in objects that exist once; not worth the churn.
- `stream.release()` "unused result" in `AudioExport.cpp` and `InputRecorder.cpp`: intended (the JUCE writer takes over the stream).
- `NoteRecorder.h` array size `kChannels * 128` and `OneShot.h` index arithmetic: values are tiny and constant.
- The audio callback: no allocation, no lock, no file or string work (also checked by the "no allocation" tests). `std::pow` is called
  once per note start and per block, not per sample; the only per-sample transcendental functions are the metronome click, the synth
  oscillators and the master `tanh`. A table or polynomial for the oscillators and `tanh` would help only with dozens of voices; it is
  not needed at the current maximum of 16 clip voices and eight channels.
- Every edit recompiles the whole `Project` snapshot (`compileCurrent`), copying all notes. With the current limits (8 channels, a few
  thousand notes) this takes microseconds; it becomes worth making incremental only with much larger projects.
- `MainComponent.cpp` was 2 600 lines. **Done in 0.11:** its member functions now live in seven files by area (the largest, the core,
  is about 1 000 lines, mostly the demo project and the panel layout). Still one class: a real split of the class (a separate
  project / pattern / recording controller) is for when automation and buses have added their own state.
- `tests/test_core.cpp` is 3 900 lines in one file; the same applies.

## After 0.56.0
The shared `Arrangement::patternTicks` was removed rather than kept beside the new `Pattern::lengthTicks`, so the compiler found
every place that read it (about 150 in the program and the tests) and none can go on using a stale shared length. The editors read
the current pattern's length through `MainComponent::currentPatternTicks()`, and `selectPattern` / `loadPatternIntoChannels` call
`showPatternLength()`, so the rack, the rolls, the drum machine and the note recorder follow a pattern switch.

## After 0.52.0
The demo song is built by code (`DemoSong.cpp`) rather than stored as a file, so it follows changes to the project format and to the
effects' parameters; the test renders it and checks levels, so a change that breaks a part of it (a preset renamed, an effect
parameter moved) is noticed. Plugin presets are chosen by name through the plugin's programs, and plugin automation finds its
parameter by name; both are reported in the session log when they cannot be found, and the channel falls back to a built-in synth.

## After 0.51.0 (review for duplicated, obsolete, redundant and slow code)
Method: a script that finds windows of 10 or more identical statements (spaces and comments ignored) across `src` and
`plugins` (about 53 000 lines); a script that lists names declared in a header and used at most twice; reading of the hot paths
(Player, the effects, both synthesizers' voices, the Playlist's paint, the undo history).

Fixed: the old note-only undo (never called, but copying every note at each note edit and keeping up to 200 copies); the
project being serialised to JSON at every mouse movement of a drag (now once when the button is let go); 14 unused functions;
about 250 duplicated lines between SunBurst and HexForge (now `src/synthkit` and `plugins/common/SynthUi`); three biquad
copies; three copies of the reader-to-`SampleData` code; the Player's two identical walks over every effect slot; three
identical drag endings in the Playlist; the clip-region copy shared by the sample and pitch editors; six sines per sample in
the synthesizers' chorus (now one sine and one cosine) and a division per sample in their delay and reverb lines; the Playlist
working out every pattern's pitch range on each paint (now only for the clips in sight).

Looked at and left as it is: the old-file compatibility code (`kLegacyPlaylistTracks`, `legacyActiveSteps`, `bakeLegacyRepeats`),
needed to open projects saved by early versions; SunBurst's float EQ biquad (per-sample float maths in a plugin, not worth moving
to doubles); the synthesizers' per-block `exp2`, `cos` and `sin` (once per block per voice, not per sample); HexForge's 31-tap
decimator (its cut-off is 0.23, not a half-band filter, so no taps are zero); the repeated include lists of the
`MainComponent*.cpp` files.

## After 0.50.0
HexForge reads a WAV for a spectral wave on the message thread with JUCE's readers (at most 4096 samples), and parses spectra
from its state with clamped values (a malformed one falls back to the default). A new spectral table is built off the audio
thread into the buffer not in use and then swapped; the editor sends one at most every 80 ms while drawing. Like SunBurst, it
allocates nothing on the audio thread (its doubled-rate buffer is made in prepareToPlay).

## After 0.48.0
SunBurst allocates only on the message thread (wavetables built once, about 6 MB; effect buffers in prepareToPlay; the arpeggiator's
MIDI buffer reserved). Its audio thread reads parameters with relaxed atomic loads, clamps every modulated value, and keeps the
output under a soft limiter. Presets and states are parsed with JUCE's XML reader; an unknown parameter in a preset is ignored. User
presets are written only to its own folder, with a legal file name. Sun Loops Studio loads the bundled VST3 from next to its own
executable (or the build folder) only.

## After 0.47.0
The new automation kinds index the two aux buses and VCA groups by `slot`, checked against the counts before use (a
`static_assert` keeps the counts equal to Project's). The Player's send levels per track live in a preallocated member array; a VCA
change recomputes the members' gains in the same chunk. The count-in is counted by the audio thread (no locks): the message thread
only stores a request or a cancel flag. Loop-take fields of a clip are read from the file clamped to 0 - 10^6 seconds, and a take is
chosen only inside the file's length.

## After 0.46.0
Tempo markers come from the file as data: `tidyTempoMarkers` drops markers at or before tick 0 or with a non-finite tempo,
clamps tempos to 20-400 BPM and keeps at most 256, so the Player's per-chunk lookup is bounded; it allocates nothing (the
markers live in the published Project). An unknown time signature reads as 4/4. The pattern length is clamped again when the
time signature changes.

## After 0.45.0
The sidechain adds one scan of the compressor slots per 64-sample chunk (500 tracks x 10 slots at most, comparisons only) and a
copy of 64 samples per source track; no allocation (the key buffers are made in the constructor). Ten slots per track raise the
Player's effect units from 2 000 to 5 000 (about 9 MB) - the big effect buffers are still lent from the pool only to the slots
that need them. An effect-chain file is parsed with the project's own slot reader (ranges clamped); plugin slots are never read
from it. Export tags are passed to ffmpeg as separate arguments (no shell).

## After 0.44.0
The playlist's track count is now data from the file: it is clamped to 1-128 and every clip's track is checked against the
actual count when placed or read (`canPlace`, `canPlaceAudio`); a pattern claimed by two tracks keeps the first. `RecordSetup`
keeps a fixed array of 128 (no allocation). Removing a track deletes its clips, after a question, and is one undo step. The
.slsproj extension is gone from the open dialog, the extension check and the desktop file type.

## After 0.43.0
New attack surface: the program now talks to the network (only api.github.com, over HTTPS with certificate checks, redirects
limited to HTTPS, answers capped at 1-2 MB, 5 s / 20 s timeouts) and parses JSON from it with nlohmann/json without exceptions
(`parse (..., nullptr, false)`, type-checked fields). The update check sends only the User-Agent; a crash report is sent only after a
click and can be read first; home folder and user name are redacted (other personal text in the last log lines - project and file
names - is visible in the preview). A GitHub token is stored in plain text in the settings file, which is set to mode 0600 when the
token is saved; a token built in with `SLS_CRASH_REPORT_TOKEN` can be read by anyone with the binary (documented). The signal
handler is async-signal-safe except for `backtrace_symbols_fd` and `prctl`, which are safe in practice on glibc (no malloc); a second
crashing thread waits instead of writing. The crash file and its archive hold at most 10 old reports.

## After 0.42.0
The clip model changed from pattern units to ticks; every reader of clips (compiler, playlist, FLP import / export, MIDI import,
project file) was moved over and the old file layout is converted on load (tested both ways). `compileSong` runs on the message
thread as before; a clip's passes are bounded by its length over the pattern length (at most the timeline). The rack's length
buttons go through the same path as the menu (`setPatternBeats`), with its timeline check and one undo step.

## After 0.41.0
The SFZ parser reads untrusted text: files over 16 MB are refused, numbers are parsed with strtod and range-checked, keys and
velocities clamped, loops checked against the sample's length after loading; `#include` is not followed (it could name any file).
A broken file and 100 000 `<` characters are in the tests. The Pitch Corrector's Follow adds one pass over 32 voices per 64-sample
chunk on the audio thread (no allocation, no lock). Still not done from earlier notes: formant preservation in the Pitch Corrector
(big shifts change the voice's character), GIG and GUS files.

## After 0.40.0
Helios reads only text built into the program; it opens no files and no network connection. The two manuals add about 200 KB to the
binary. Commands run on the message thread exactly as their buttons and menu items do.

## After 0.39.0
The stem split runs on a worker thread that touches only its own copy of the audio; the results go back to the message thread
through `callAsync`, and the playlist checks that the clip still holds the same audio before replacing it. Cancelling stops the
thread at the next 64 frames. Memory: three output buffers of the clip's length in doubles plus a ring of 17 frames.

## After 0.38.0
The pitch editor analyses and renders on the message thread (a second or two for a few minutes of audio; the window waits). It
allocates freely and never touches the audio thread except through the auditioner, which plays a finished buffer.

## After 0.37.0
The Slicer rebuilds the whole slice program (copies of every slice) on the message thread after each edit; the audio thread
only ever sees finished snapshots. A bug found by the tests and fixed before release: `slicer::addStart` computed the new
index from an iterator taken before `vector::insert` could reallocate (unspecified evaluation order).

## After 0.36.0
The audio callback now calls eight recorders' `process()` while recording (each a few atomic operations when it is not part of
the take). Up to eight `ThreadedWriter`s share nothing: each recorder has its own writer thread, started on its first take. The
input channel each take reads is resolved on the message thread when Record is pressed; if the device is changed while
recording, the takes keep reading the same positions of the callback's input array (missing ones become silence).

## After 0.35.0
Plugin parameters are set from the audio thread through `juce::AudioProcessorParameter::setValue`, as JUCE's own hosts do; what a
plugin does inside that call is the plugin's business (most only store an atomic). The call is skipped when the value has not
changed. The parameter list is cached on the message thread when the plugin is wrapped; plugins that change their parameter list
after loading (rare; JUCE reports it through `audioProcessorChanged`) are not followed: their new parameters cannot be automated
until the project is opened again. Automation has no per-sample smoothing of plugin parameters (64-sample steps).

## After 0.34.0
The .flp reader takes untrusted input: every size is checked against what is left of the file before it is used, lengths and
positions are clamped, 7-bit size groups stop after 35 bits, and a file whose data chunk claims more than the file holds is
refused. 3000 randomly damaged files are read in the tests; the reader also ran clean under AddressSanitizer and
UndefinedBehaviorSanitizer on the real FL Studio 20.8 project from PyFLP's tests and on files written here. Opening a .flp never
writes anything; the export writes through a temporary file.

## After 0.33.0
Clips may overlap on a playlist track: `Arrangement::canPlace` / `canPlaceAudio` only check the timeline, the hit tests pick the
last clip (drawn on top). The player already gave every audio clip its own voice (16 at once) and summed pattern notes, so
nothing changed there; more than 16 audio clips sounding at the same moment are still cut to 16.

## After 0.32.0
The typing keyboard goes through `AudioEngine::handleMidi`, the path a MIDI keyboard takes (the same lock-free queue), so it
adds nothing to the audio thread. Keys let go are found with `KeyPress::isKeyCurrentlyDown` on every key change and on every
timer tick (a key let go while another window had the keyboard still ends its note). The mapping is JUCE-free and tested.

## After 0.31.0
The step sequencer's rules live in `src/audio/StepSequence.h` (JUCE-free, tested); the rack repaints its rows from the notes on
every paint, nothing is cached. The repeats of a short sequence are made when the project snapshot is compiled, on the message
thread; the audio thread sees ordinary notes. The browser's one-click preview decodes on the message thread (the first 30 seconds
are kept, files over 200 MB are refused), like loading a sample into a channel.

## After 0.30.0
The Loop Starter is plain code on note lists, run on the message thread; its only resource use is loading the sounds it names (the
library instruments are decoded the first time, with a wait cursor). Every sound it can name is checked to exist by a test; if the
sound library were missing, a synth patch stands in and the status line says so. clang-tidy is clean on the new files.

## After 0.29.0
The mixer can have 500 tracks. The big effect buffers moved out of `EffectUnit` into `EffectMemory`, owned by the Player's pool
(`memoryStore`, `unique_ptr`s in a vector reserved for the maximum) and lent by pointer; memory made on the message thread travels
through a lock-free queue and is owned by it until adopted; queued memory is freed by `prepare` and by the destructor. Checked with
AddressSanitizer and UndefinedBehaviorSanitizer in a program that renders 300 random projects while another thread offers memory
and the rate changes half-way: nothing found. The audio thread allocates nothing (tested). `MixerPanel` and `ChannelStrip` hold
references into `MainComponent::tracks`, which is reserved for 500 so it never reallocates (asserted).

## After 0.28.0
The score log adds one push to a fixed-size lock-free queue per MIDI note on the audio thread (nothing is allocated there; a full
queue drops the event). The log on the message thread is bounded (five minutes, 20000 events).

## After 0.27.0
Audio clips gained pan, reverse and fades. Reading backwards never goes outside the audio (the voice ends below position 0, the
next frame is clamped to the last one); a clip with every setting on renders without allocation. Loading takes nonsense values
(NaN pan, negative or infinite fades) as none, and the snapshot clamps them again. clang-tidy is clean on the files changed,
after two older findings in the playlist were fixed (the ruler's time labels counted with a floating-point loop counter, and the
record marker's position used an integer division).

## After 0.26.0
The Pitch Corrector allocates nothing: its analysis arrays are fixed members and its recordings are buffers `prepare` already makes
for the delay and the reverb (tested: 2000 blocks without allocation). Every access stays inside its buffers: AddressSanitizer and
UndefinedBehaviorSanitizer found nothing over 540 runs (every range, shift, speed and three scales; tones gliding over six octaves,
noise, a noisy right channel, mix and amount at 50 %, 48 and 96 kHz, blocks of 37 samples). Valgrind found an overrun in the
Pitch Corrector test's own code (a tone longer than its buffer), now fixed. clang-tidy is clean on
`Effects.cpp` and `MixerPanel.cpp`.

## After 0.25.0
The Time Shaper's recordings (8.5 s, two channels) come from a pool of four made in `Player::prepare`; `lendTimeBuffers` hands them
out and takes them back by pointer once per block, without allocating (tested with five Time Shapers). `prepare` takes every buffer
back before it reallocates the pool, so no unit keeps a pointer into freed memory.

## After 0.24.0
Measuring the additive engine's aliasing found a quality problem elsewhere: the master's plain `tanh` bent every level (a sine at
-10 dBFS got a 3rd harmonic, and mixes got intermodulation that aliased: -69 dB at 22.9 kHz in the test). The limiter is now exactly
linear up to 0.7 (-3 dBFS) with a `tanh` knee above, which never passes 1 (tested both ways). The program also crashed when started
without a display (no `DISPLAY`): it now says so and exits with status 1. clang-tidy (the audit's check set) on `Player.cpp`,
`SynthPanel.cpp`, `ChannelPreset.cpp` and `main.cpp`: no findings.

## After 0.23.0
The piano roll's new code runs on the message thread only (`PianoRoll`, `PianoRollToolbar`, `MusicTheory`); the player sees the
notes through the usual project snapshot. clang-tidy (the audit's check set) on `MusicTheory.cpp`, `PianoRoll.cpp` and
`PianoRollToolbar.cpp`: four findings in `MusicTheory.cpp` fixed (three vectors filled without `reserve`, a rounding by
`+ 0.5`) and one in `PianoRoll.cpp` (an integer division in a floating-point expression, harmless but rewritten). The remaining
`bugprone-swapped-arguments` reports in `PianoRoll.cpp` are the false positive described after 0.20.0 (JUCE's
`drawVerticalLine (int, float, float)` / `drawHorizontalLine` called with explicit casts), now five of them.

## After 0.22.0
New code: `SoundLibrary.cpp` (zip archive in memory, decoding on worker threads, `weak_ptr` caches guarded by mutexes; reading the
archive is serialised by one mutex) and `DrumSynth.cpp` (generation on the message thread when a kit is loaded). Nothing of it runs in
the audio callback: the player only sees finished `SampleData`. The test run also found that three tests left folders in `/tmp` on
every run (`sls-export*`, `sls-import*`, `sls-record*`); they now delete them. clang-tidy (the audit's check set) on the new and
changed files: one finding fixed (a vector without `reserve`); `Library.cpp:49` ("forming reference to null pointer") is reported
before these changes too and is a false positive (the pointer is tested on the same line).

## After 0.21.0
The eight new effects run inside `Player::render` through `EffectUnit::process`: nothing allocates (tested, 8 effects x 500 blocks), all
lines and states are sized in `prepare`. The Overdrive is the heaviest (12 biquads at 4x the rate per channel plus a log-cosh per
oversampled sample); its CPU cost on a real computer was not measured. clang-tidy (the audit's check set) on `Effects.cpp`,
`MixerPanel.cpp` and `ProjectFile.cpp`: no findings.

## After 0.20.0
clang-tidy (the audit's check set) on `PianoRoll.cpp` reports four `bugprone-swapped-arguments` warnings, all on `drawHorizontalLine` /
`drawVerticalLine` calls that were there before 0.20.0: the order is JUCE's `(int position, float start, float end)` and the casts are
explicit, so they are false positives and were left alone. The commit message of 0.20.0 says "no findings"; that is wrong, this note
corrects it.

## After 0.19.0
An FM voice computes four `std::sin` per sample (32 voices: about 6 million per second at 48 kHz) and nothing allocates (tested with eight
voices at full matrix depth for 2000 blocks). clang-tidy (the audit's check set) was run on the changed files; see the commit message.

## After 0.18.0
The subtractive voice adds per-sample work to `Player::render` (two oscillators, noise, one or two filters) and a `std::pow` / `std::tan` per
voice every 16 samples; nothing allocates (tested with eight voices for 2000 blocks). The CPU load of 32 voices was not measured on the
maintainer's computer. clang-tidy (the audit's check set) was run on the new files; see the commit message.

## After 0.17.0
Block I added plugin processing to the audio thread: MIDI events written into fixed arrays, a pass over the plugin channels per chunk, and
`JucePlugin::process`, which copies into a preallocated buffer and calls the plugin under a try-lock (never waits). The "no allocation" test
runs a VST3 instrument and a VST3 effect for 2000 blocks; what a third-party plugin does inside `processBlock` is out of the program's hands.
`AudioEngine::setSuspended` uses two sequentially-consistent atomics so that an export can use the same instances. clang-tidy (the audit's
check set) on `PluginHost.cpp` and `MainComponentPlugins.cpp`: no findings.

## After 0.16.0
The sampler voice gained a loop, an envelope decay, a per-voice filter and zone gain / pan in `Player::render`; `noteOn` loops over a program's zones (no
allocation: tested with zones, loops, filters and envelopes for 2000 blocks). TinySoundFont is compiled in one file (`SoundFont.cpp`) with its warnings
silenced there and its directory as a SYSTEM include; only its parser runs, on the message thread. clang-tidy (the audit's check set) on the new files found
one widening multiplication in `SoundFont.cpp`, fixed. `PlaylistTimeline.cpp` and `Player.cpp` (now ~1 000 lines) are the next candidates to split.

## After 0.15.0
The audio thread gained `Player::seek`, `locate`, `startClipsInside` and `jumpTo` (binary searches and a loop over the audio events, no allocation: tested while
seeking and looping). `PlaylistTimeline.cpp` is now ~2 100 lines with the ruler and loop-region handling: still the first file to split (ruler / clips / lanes).
The undo history copies the whole editable state at every edit and serialises it to compare; this is simple and correct but grows with the project (not timed on
large projects). clang-tidy (the audit's check set) was run on the new and changed files; see the commit message for what it found.

## After 0.14.0
Block G changed the audio thread in one place: `Player::drumNoteOn` (a loop over at most 32 voices for the choke group, then the same voice allocation as before) and two more multiplications per sampler voice sample. A test
counts allocations while a busy drum pattern plays and swaps the kit under sounding voices. `Player::takeVoice` is the former inline allocation, unchanged in behaviour. `DrumMachine.cpp` is a new UI file (about 800 lines: the next candidate to split into the pad / inspector / step grid parts); the
JSON helpers moved to `library/JsonRead.h` so that the project and kit files share them. clang-tidy was run on the new files; the result is in the commit message.

## After 0.13.0
Block E added `SampleEdit.cpp` (pure functions; buffers are `std::vector`s, allocated on the UI thread only), `Auditioner.h` (the only new code on the audio thread:
no allocation, no lock, a bounded copy loop; covered by a test that runs `mixInto` on a second thread while the UI starts and stops it, but not by the
"no allocation" test of `Player`) and the editor UI. clang-tidy (the audit's check set) was run on the new files: the two by-value parameters were made `const&`; what is left is `bugprone-misplaced-widening-cast` on `static_cast<size_t> (i + k)`-style index sums (the sums are bounded by the sample length, far below 2^31), the intended `stream.release()` of the WAV writer, and two JUCE `drawHorizontalLine` / `drawVerticalLine` calls whose (int, float, float) signature is JUCE's own. `PlaylistTimeline.cpp` and `SampleEditor.cpp` (~860 lines)
are the next files to split.

## After 0.11.0
The audit was not repeated in full for automation, the console and the metering. Checked while writing them: the new audio code allocates
nothing in `Player::render` (the "no allocation" test covers automation running; the sends / buses code was only read for allocations (its buffers are `std::array`s in `Player`), not tested; the loudness meter and the ring live in `AudioEngine`, whose callback needs an audio device and is not covered by such a test, only read for allocations); the automation
evaluation is one binary search per parameter and chunk; the spectrum uses its own FFT instead of a new dependency. A new full pass with
clang-tidy and the duplicate-block script is recommended before the next feature block. `PlaylistTimeline.cpp` has grown (clips, lanes, undo): it is the next file to split.
