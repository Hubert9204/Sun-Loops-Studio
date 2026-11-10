#pragma once

#include <memory>
#include <vector>

#include "audio/SampleData.h"

namespace sls
{
// Offline pitch analysis of a monophonic recording (a voice, a bass, a lead line), for the pitch editor (0.38.0): the pitch every
// 5 ms with YIN (de Cheveigné and Kawahara, 2002), then notes where the pitch holds steady. Plain C++, tested; not real-time.
namespace pitchtrack
{
struct Frame
{
    double seconds = 0.0; // centre of the analysis window
    double hz = 0.0;      // 0 = no pitch (silence, noise, a chord)
    double midi = 0.0;    // the same as a MIDI note number with cents (69 = A4 = 440 Hz); 0 when hz is 0
    double level = 0.0;   // RMS of the window
};

struct Note
{
    double start = 0.0, end = 0.0; // seconds
    double midi = 0.0;             // the note's pitch centre (median of its frames), with cents
    double shift = 0.0;            // semitones the editor moves it by (0 = as sung)
    double level = 0.0;            // loudest frame's RMS

    int nearestKey() const noexcept;      // the MIDI key nearest to midi + shift
    double centsOff() const noexcept;     // how far midi + shift is from that key, in cents (-50..+50)
};

// The pitch of `in` (its left and right averaged), one frame every `hopSeconds`, between `minHz` and `maxHz`.
std::vector<Frame> track (const SampleData& in, double hopSeconds = 0.005, double minHz = 50.0, double maxHz = 1500.0);

// Notes: runs of frames with a pitch that stay within `toleranceSemitones` of their running median for at least `minSeconds`.
// A jump larger than that starts a new note; a gap without pitch ends one (gaps shorter than `bridgeSeconds` are bridged).
std::vector<Note> notesFrom (const std::vector<Frame>& frames, double minSeconds = 0.06, double toleranceSemitones = 0.7,
                             double bridgeSeconds = 0.03);

// The audio with every note moved by its `shift` (each note's span is pitch-shifted on its own, its length kept, with 10 ms
// crossfades at its edges); what lies outside the notes is left as it was. Returns `in` itself when no note moves.
std::shared_ptr<const SampleData> render (const std::shared_ptr<const SampleData>& in, const std::vector<Note>& notes);

// The shift that brings a note to the nearest key (amount 1) or part of the way (0..1); with a scale (12 flags from C, `root`
// 0 = C), to the nearest key in the scale.
double correction (const Note& note, double amount, const bool* scale = nullptr, int root = 0);
} // namespace pitchtrack
} // namespace sls
