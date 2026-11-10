#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "audio/NoteSequence.h"

namespace sls::theory
{
// Scales, chords and the piano roll's editing tools and generators (0.23.0). Plain C++ on lists of notes, so it can be tested
// without a window; the piano roll calls it.

// --- scales ---------------------------------------------------------------------------------------------------------------
struct ScaleInfo
{
    const char* name;
    std::uint16_t mask; // bit n set: the note n semitones above the root is in the scale
};
const std::vector<ScaleInfo>& scales(); // [0] is "Chromatic" (every note)

struct Scale
{
    int root = 0; // pitch class, 0 = C
    int type = 0; // index into scales()

    bool contains (int pitch) const noexcept;
    bool isRoot (int pitch) const noexcept { return ((pitch - root) % 12 + 12) % 12 == 0; }
    int snap (int pitch) const noexcept;            // the nearest note of the scale (the lower one on a tie)
    int step (int pitch, int steps) const noexcept; // `steps` notes of the scale up (or down); snaps first
    std::vector<int> pitchClasses() const;          // the notes of the scale from the root, as pitch classes
    int size() const noexcept;                      // notes per octave
};

const char* pitchClassName (int pitchClass); // "C", "C#", ...
std::string noteName (int pitch);            // "C5" (MIDI 60): FL Studio's octave numbers (0.32.0; C4 before)

// --- chords -----------------------------------------------------------------------------------------------------------------
struct ChordInfo
{
    const char* name;
    std::vector<int> intervals; // semitones above the root, root first
};
const std::vector<ChordInfo>& chords();

// The pitches of a chord on `root` (a MIDI note), with `inversion` of its lowest notes moved up an octave (0 = root position).
std::vector<int> chordPitches (int root, int chord, int inversion = 0);

// A chord built from the scale itself on a degree (0 = the root): thirds stacked inside the scale, `size` notes (3 = triad,
// 4 = seventh). For a scale with other than seven notes the major or natural minor scale on the same root is used.
std::vector<int> diatonicChord (const Scale& scale, int degree, int size, int octave = 4);

// --- the notes of a pattern -------------------------------------------------------------------------------------------------
// Every tool works on the selected notes (`selected[i]` for notes[i]); with nothing selected, on every note. Tools that make or
// remove notes keep `selected` the same size as `notes` and select what they made.

void transpose (std::vector<Note>& notes, std::vector<bool>& selected, int semitones);
void transposeInScale (std::vector<Note>& notes, std::vector<bool>& selected, const Scale& scale, int steps);
void snapToScale (std::vector<Note>& notes, std::vector<bool>& selected, const Scale& scale);

// Starts (and with `ends`, lengths) moved towards the grid by `strength` (1 = onto it). A note never gets shorter than the grid.
void quantize (std::vector<Note>& notes, std::vector<bool>& selected, int gridTicks, float strength, bool ends, int patternTicks);

// Random timing (up to `ticks` either way) and velocity (up to `velocity` either way, 0..1), the same for the same seed.
void humanize (std::vector<Note>& notes, std::vector<bool>& selected, int ticks, float velocity, std::uint32_t seed, int patternTicks);

void legato (std::vector<Note>& notes, std::vector<bool>& selected, int patternTicks); // each note lasts until the next one starts
void staccato (std::vector<Note>& notes, std::vector<bool>& selected, float factor, int minimumTicks); // lengths times `factor`

// Notes that start together (a chord) are spread: the n-th from the bottom (up) or top starts n * `ticks` later, ends unchanged.
void strum (std::vector<Note>& notes, std::vector<bool>& selected, int ticks, bool up);

enum class ArpPattern { Up, Down, UpDown, Random };
// Every chord (notes starting together) becomes a run of its notes, one every `rateTicks`, for as long as the chord lasted.
void arpeggiate (std::vector<Note>& notes, std::vector<bool>& selected, int rateTicks, ArpPattern pattern, std::uint32_t seed);

void chop (std::vector<Note>& notes, std::vector<bool>& selected, int pieceTicks); // each note cut into pieces of that length
void flip (std::vector<Note>& notes, std::vector<bool>& selected);                 // upside down around the middle of their range
void velocityRamp (std::vector<Note>& notes, std::vector<bool>& selected, float from, float to); // by start time
void setVelocity (std::vector<Note>& notes, std::vector<bool>& selected, float velocity);
void randomizeVelocity (std::vector<Note>& notes, std::vector<bool>& selected, float low, float high, std::uint32_t seed);

// --- generators -------------------------------------------------------------------------------------------------------------
struct ProgressionInfo
{
    const char* name;
    std::vector<int> degrees; // scale degrees, 0 = the root
    bool sevenths;            // four-note chords
};
const std::vector<ProgressionInfo>& progressions();

enum class ChordRhythm { Held, Stabs, Pulse, Arpeggio };
const char* chordRhythmName (ChordRhythm r);

// One chord of a progression in time: where it is and what it is (for the bassline).
struct ChordSpan
{
    int startTick = 0, lengthTicks = 0;
    int root = 0;                 // pitch class
    std::vector<int> pitchClasses; // the chord's notes
};

// The chords of progression `which` in `scale`, one per `chordTicks`, repeated to fill `patternTicks`; voiced around `octave`
// with each chord in the inversion closest to the one before (smooth voice leading), played in `rhythm`.
std::vector<Note> progressionNotes (const Scale& scale, int which, ChordRhythm rhythm, int chordTicks, int patternTicks, int octave = 4);
std::vector<ChordSpan> progressionChords (const Scale& scale, int which, int chordTicks, int patternTicks);

// The chords that other notes play: for every `spanTicks`, the chord (triad or seventh) that matches most of the notes sounding in
// it, its root the lowest note when two match equally; a part with no notes keeps the chord before.
std::vector<ChordSpan> detectChords (const std::vector<Note>& notes, int spanTicks, int patternTicks);

enum class BassStyle { Roots, Pulse, Octaves, RootFifth, Walking, Syncopated };
const char* bassStyleName (BassStyle s);

// A bassline on `chords`: the root in the octave from `lowest` (a MIDI note, 28 = E1 by default) up, in `style`.
std::vector<Note> basslineNotes (const std::vector<ChordSpan>& chords, BassStyle style, const Scale& scale, int lowest = 28);
} // namespace sls::theory
