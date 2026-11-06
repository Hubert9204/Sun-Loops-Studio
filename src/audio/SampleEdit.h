#pragma once

#include <memory>
#include <vector>

#include <juce_core/juce_core.h>

#include "audio/Project.h"

namespace sls
{
// Editing the audio of a sample: every function returns a NEW sample and leaves its input alone (samples are shared and
// immutable once in use). All of it runs on the message thread, none of it is real-time safe. The kinds of edit follow
// the usual sample editors: cut, crop, silence, fades, reverse, gain, normalise, remove DC offset, invert phase, noise
// reduction, plus the detection of where the sounds start (for slicing).
//
// A range is [begin, end) in frames; it is clamped to the sample and an empty range after clamping is refused
// (nullptr) wherever the edit needs frames to act on.
struct FrameRange
{
    int begin = 0;
    int end = 0;
    int length() const noexcept { return end - begin; }
};

// Same range inside [0, numFrames]; begin > end is swapped.
FrameRange clampRange (FrameRange r, int numFrames) noexcept;

enum class FadeShape { Linear, Curve, SCurve }; // Curve: slow start, fast finish (like a natural decay); SCurve: smooth at both ends

using SamplePtr = std::shared_ptr<const SampleData>;

SamplePtr cutRange (const SamplePtr& in, FrameRange r);       // removes the frames: the sound after them moves earlier
SamplePtr cropToRange (const SamplePtr& in, FrameRange r);    // keeps only the frames
SamplePtr silenceRange (const SamplePtr& in, FrameRange r);   // zeros the frames, keeping the length
SamplePtr fadeRange (const SamplePtr& in, FrameRange r, bool fadeIn, FadeShape shape);
SamplePtr reverseRange (const SamplePtr& in, FrameRange r);
SamplePtr gainRange (const SamplePtr& in, FrameRange r, double gainDb); // -120 .. +48 dB; the result can go past full scale
SamplePtr invertPhase (const SamplePtr& in, FrameRange r);
SamplePtr removeDcOffset (const SamplePtr& in, FrameRange r); // subtracts the mean of each channel (measured over the whole sample)

// Highest sample (either channel) in the range, as a linear value; 0 for an empty range.
double peakOf (const SamplePtr& in, FrameRange r);
double toDb (double linear) noexcept;

// Scales the range so that its loudest sample reaches `targetPeakDb` (-60 .. 0). Silence is returned unchanged.
SamplePtr normaliseRange (const SamplePtr& in, FrameRange r, double targetPeakDb);

// Noise reduction by spectral gating. The noise is measured on `noise` (a stretch of the sample where only the noise is
// heard); every bin of the whole sample (or only `range`) that stays below what the noise reaches is lowered.
struct NoiseReduction
{
    double reductionDb = 12.0;   // how far the noise is lowered: 0 .. 60
    double sensitivity = 3.0;    // how many standard deviations above the average noise level count as signal: 0 .. 8
    int frequencySmoothing = 3;  // bins each side over which the decision is averaged (avoids "bubbling" artefacts): 0 .. 16
    double releaseSeconds = 0.05; // how slowly the gate closes again after a sound: 0.005 .. 1
};
constexpr int kNoiseFftSize = 2048;
constexpr int kNoiseMinProfileFrames = kNoiseFftSize + 3 * (kNoiseFftSize / 4); // shortest usable noise sample
// nullptr if the noise selection is shorter than kNoiseMinProfileFrames, or the sample shorter than one FFT.
SamplePtr reduceNoise (const SamplePtr& in, FrameRange noise, FrameRange range, const NoiseReduction& settings);

// Where sounds start (seconds from the start of the sample), by spectral flux: a new note, hit or word makes the spectrum
// grow suddenly. `sensitivity` 0 (only the clearest hits) .. 1 (everything that changes); `minGapSeconds` is the least
// time between two onsets. The positions are refined to the first rise of the waveform.
std::vector<double> detectOnsets (const SamplePtr& in, double sensitivity, double minGapSeconds = 0.05);

// n-1 positions (seconds) that cut the sample into n equal parts; empty for n < 2.
std::vector<double> gridSlices (const SamplePtr& in, int n);

// Writes the sample as a 32-bit float stereo WAV file (nothing is clipped or rounded). Never replaces a file: fails if it exists.
juce::Result writeSampleWav (const SampleData& sample, const juce::File& file);

// A file name for an edited copy: <folder>/<name>-edit.wav, <name>-edit-2.wav, ... that does not exist yet.
juce::File uniqueEditedFile (const juce::File& folder, const juce::String& baseName);
} // namespace sls
