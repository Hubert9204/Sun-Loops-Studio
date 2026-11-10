#pragma once

#include <functional>
#include <memory>

#include "audio/SampleData.h"

namespace sls
{
// Stem separation without AI (0.39.0): a mix split into Drums, Bass and the rest by classic signal processing.
// Drums: what is short in time and wide in frequency (harmonic-percussive separation by median filtering, Fitzgerald 2010).
// Bass: what is steady and below the crossover. Other: what is steady and above it (voices, chords, leads).
// The three parts add up to the input (up to rounding), so nothing is lost; how clean each part is depends on the music.
struct StemSettings
{
    double bassCrossoverHz = 150.0; // steady sound below this is Bass (a smooth one-octave crossover around it)
    int timeSpan = 17;              // median lengths, in frames (time) and bins (frequency): longer = stricter separation
    int frequencySpan = 17;
    double hardness = 2.0;          // mask exponent: 1 = soft (less artefacts, more bleed), 4 = hard
};

struct Stems
{
    std::shared_ptr<SampleData> drums, bass, other;
};

// Not real-time: allocates and takes a few seconds per minute of audio. `progress` (0..1) is called now and then and may return
// false to stop (then the result is empty).
Stems splitStems (const SampleData& in, const StemSettings& settings = {}, const std::function<bool (double)>& progress = {});
} // namespace sls
