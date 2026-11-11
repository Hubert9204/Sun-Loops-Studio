#pragma once

#include <vector>

namespace sunburst
{
// Band-limited single-cycle tables. Every table is kept in kTableLevels versions ("mipmaps"): level l keeps the harmonics up to
// 1024 >> l, so a note uses the richest version that has nothing above the Nyquist frequency (no aliasing). A wavetable set has
// kFrames frames; the position control crossfades between neighbouring frames. Built once, on first use (not on the audio thread).
constexpr int kTableSize = 2048;
constexpr int kTableLevels = 11;
constexpr int kFrames = 8;
constexpr int kNumWavetables = 14;

class Wavetables
{
public:
    static const Wavetables& get();

    // The mip level for a phase increment (cycles per sample).
    static int levelFor (double increment) noexcept;

    // A table of kTableSize + 1 samples (the last repeats the first, for interpolation).
    const float* frame (int set, int frame, int level) const noexcept;
    const float* triangle (int level) const noexcept { return tri.data() + static_cast<size_t> (level) * (kTableSize + 1); }
    const float* sine() const noexcept { return sin.data(); }

    // One interpolated sample: `position` 0..1 across the frames, `phase` 0..1.
    float sample (int set, float position, double phase, int level) const noexcept;

private:
    Wavetables();
    std::vector<float> sets; // [set][frame][level][kTableSize + 1]
    std::vector<float> tri;  // [level][kTableSize + 1]
    std::vector<float> sin;  // [kTableSize + 1]
};

inline float readTable (const float* t, double phase) noexcept
{
    const double x = phase * kTableSize;
    const int i = static_cast<int> (x);
    const float f = static_cast<float> (x - i);
    return t[i] + f * (t[i + 1] - t[i]);
}
} // namespace sunburst
