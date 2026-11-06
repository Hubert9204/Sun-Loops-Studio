#pragma once

#include <cmath>
#include <memory>

#include "audio/Project.h"

namespace sls
{
constexpr double kMinStretchRatio = 0.25; // output is at least a quarter as long as the input
constexpr double kMaxStretchRatio = 4.0;
constexpr double kMaxStretchSemitones = 24.0;
constexpr int kMaxStretchFrames = 48000 * 60 * 15; // about 15 minutes at 48 kHz; longer audio is refused (it would also take minutes)

// How an audio clip is changed in time and pitch. The two are independent: a clip can be made longer without changing
// its pitch, higher without changing its length, or both.
struct StretchSettings
{
    double lengthRatio = 1.0; // duration after / duration before: 2 = twice as long (half the tempo), 0.5 = half as long
    double semitones = 0.0;   // pitch change, fractions allowed (0.01 = one cent)
    bool keepTimbre = false;  // limits how far the spectrum is moved above 8 kHz when pitch-shifting (helps a little on voices and cymbals)

    bool isNeutral() const noexcept { return near (lengthRatio, 1.0) && near (semitones, 0.0); }
    bool operator== (const StretchSettings& o) const noexcept
    {
        return near (lengthRatio, o.lengthRatio) && near (semitones, o.semitones) && keepTimbre == o.keepTimbre;
    }
    bool operator!= (const StretchSettings& o) const noexcept { return ! (*this == o); }

    // Numbers closer than this are the same setting (a project file keeps them to 6 decimals).
    static bool near (double a, double b) noexcept { return std::fabs (a - b) < 1.0e-9; }

    // Same values forced into the supported range (non-finite numbers become the neutral value).
    StretchSettings sanitised() const noexcept;
};

// Renders `input` with `settings` (offline, whole file: Signalsmith Stretch, MIT). The result keeps the sample rate, the
// name and the `source` of the input, so a project can find the file again; it lasts `input` x lengthRatio.
// Returns nullptr if the input is empty or longer than kMaxStretchFrames. A neutral setting returns `input` itself.
// Not real-time safe: it allocates and takes about as long as the audio lasts divided by 20-100.
std::shared_ptr<const SampleData> stretchSample (const std::shared_ptr<const SampleData>& input, const StretchSettings& settings);
} // namespace sls
