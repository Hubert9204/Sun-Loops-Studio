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
