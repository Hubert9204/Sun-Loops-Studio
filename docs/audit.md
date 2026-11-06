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

## After 0.11.0
The audit was not repeated in full for automation, the console and the metering. Checked while writing them: the new audio code allocates
nothing in `Player::render` (the "no allocation" test covers automation running; the sends / buses code was only read for allocations (its buffers are `std::array`s in `Player`), not tested; the loudness meter and the ring live in `AudioEngine`, whose callback needs an audio device and is not covered by such a test, only read for allocations); the automation
evaluation is one binary search per parameter and chunk; the spectrum uses its own FFT instead of a new dependency. A new full pass with
clang-tidy and the duplicate-block script is recommended before the next feature block. `PlaylistTimeline.cpp` has grown (clips, lanes, undo): it is the next file to split.
