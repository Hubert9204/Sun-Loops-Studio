#include "sunburst/Wavetables.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include <juce_dsp/juce_dsp.h>

namespace sunburst
{
namespace
{
constexpr double kTwoPi = 6.283185307179586;
constexpr int kOrder = 11; // 2^11 = kTableSize
static_assert ((1 << kOrder) == kTableSize);

using Frame = std::vector<double>; // one naive cycle of kTableSize samples

double naiveSaw (double x) { return 2.0 * x - 1.0; }
double naiveSquare (double x, double width = 0.5) { return x < width ? 1.0 : -1.0; }
double naiveTriangle (double x) { return x < 0.5 ? 4.0 * x - 1.0 : 3.0 - 4.0 * x; }

Frame fromFunction (const std::function<double (double)>& f)
{
    Frame out (kTableSize);
    for (int i = 0; i < kTableSize; ++i)
        out[static_cast<size_t> (i)] = f (static_cast<double> (i) / kTableSize);
    return out;
}

// Sum of sines: amplitude(k) for harmonics 1..maxHarmonic.
Frame additive (const std::function<double (int)>& amplitude, int maxHarmonic = 1023)
{
    Frame out (kTableSize, 0.0);
    for (int k = 1; k <= maxHarmonic; ++k)
    {
        const double a = amplitude (k);
        if (std::abs (a) < 1.0e-5)
            continue;
        for (int i = 0; i < kTableSize; ++i)
            out[static_cast<size_t> (i)] += a * std::sin (kTwoPi * k * i / kTableSize);
    }
    return out;
}

// Writes the kTableLevels band-limited versions of a cycle into `dest` (each kTableSize + 1 floats), all scaled by the same
// factor so that the fullest version peaks at 1.
void bandLimit (const Frame& cycle, float* dest)
{
    juce::dsp::FFT fft (kOrder);
    std::vector<float> spectrum (2 * static_cast<size_t> (kTableSize), 0.0f);
    for (int i = 0; i < kTableSize; ++i)
        spectrum[static_cast<size_t> (i)] = static_cast<float> (cycle[static_cast<size_t> (i)]);
    fft.performRealOnlyForwardTransform (spectrum.data());

    float scale = 1.0f;
    for (int level = 0; level < kTableLevels; ++level)
    {
        const int keep = std::min (kTableSize / 2 - 1, 1024 >> level);
        std::vector<float> work (spectrum);
        work[0] = work[1] = 0.0f; // no DC
        for (int bin = keep + 1; bin <= kTableSize / 2; ++bin)
            work[2 * static_cast<size_t> (bin)] = work[2 * static_cast<size_t> (bin) + 1] = 0.0f;
        // The mirrored (negative-frequency) half must match for a real result.
        for (int bin = kTableSize / 2 + 1; bin < kTableSize; ++bin)
        {
            const int mirror = kTableSize - bin;
            work[2 * static_cast<size_t> (bin)] = work[2 * static_cast<size_t> (mirror)];
            work[2 * static_cast<size_t> (bin) + 1] = -work[2 * static_cast<size_t> (mirror) + 1];
        }
        fft.performRealOnlyInverseTransform (work.data());
        if (level == 0)
        {
            float peak = 0.0f;
            for (int i = 0; i < kTableSize; ++i)
                peak = std::max (peak, std::abs (work[static_cast<size_t> (i)]));
            scale = peak > 1.0e-9f ? 1.0f / peak : 0.0f;
        }
        float* out = dest + static_cast<size_t> (level) * (kTableSize + 1);
        for (int i = 0; i < kTableSize; ++i)
            out[i] = work[static_cast<size_t> (i)] * scale;
        out[kTableSize] = out[0];
    }
}

// The frames of each wavetable set (`f` from 0 to kFrames - 1, `t` = f / (kFrames - 1)).
Frame makeFrame (int set, int f)
{
    const double t = static_cast<double> (f) / (kFrames - 1);
    switch (set)
    {
        case 0: // Analog Morph: sine -> triangle -> saw -> square
        {
            const double pos = t * 3.0;
            const int a = std::min (2, static_cast<int> (pos));
            const double mix = pos - a;
            const auto shape = [] (int s, double x)
            {
                switch (s)
                {
                    case 0: return std::sin (kTwoPi * x);
                    case 1: return naiveTriangle (std::fmod (x + 0.25, 1.0));
                    case 2: return naiveSaw (x);
                    default: return naiveSquare (x);
                }
            };
            return fromFunction ([&] (double x) { return (1.0 - mix) * shape (a, x) + mix * shape (a + 1, x); });
        }
        case 1: // Pulse Width: 50 % down to 5 %
        {
            const double width = 0.5 - 0.45 * t;
            return fromFunction ([width] (double x) { return naiveSquare (x, width) + (2.0 * width - 1.0); });
        }
        case 2: // Harmonic Sweep: a saw that opens up from one harmonic to all of them
        {
            const double limit = std::pow (2.0, 0.2 + t * 9.8);
            return additive ([limit] (int k) { return k <= limit ? 1.0 / k : (k <= limit * 1.5 ? (1.0 / k) * (1.5 - k / limit) * 2.0 : 0.0); });
        }
        case 3: // Formant: the vowels A, E, I, O, U on a 110 Hz voice
        {
            static const double formants[5][3] { { 800, 1150, 2900 }, { 400, 2000, 2550 }, { 300, 2300, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
            const double pos = t * 4.0;
            const int a = std::min (3, static_cast<int> (pos));
            const double mix = pos - a;
            double f1[3];
            for (int i = 0; i < 3; ++i)
                f1[i] = (1.0 - mix) * formants[a][i] + mix * formants[a + 1][i];
            return additive ([&f1] (int k)
            {
                const double hz = 110.0 * k;
                double g = 0.0;
                for (int i = 0; i < 3; ++i)
                {
                    const double width = 80.0 + 40.0 * i;
                    g += std::exp (-0.5 * std::pow ((hz - f1[i]) / width, 2.0)) * (i == 0 ? 1.0 : 0.6 / i);
                }
                return (0.05 + g) / std::sqrt (static_cast<double> (k));
            }, 180);
        }
        case 4: // Digital Bells: few, far-apart harmonics that move with the position
        {
            const int step = 2 + f;
            return additive ([step, t] (int k)
            {
                if (k == 1)
                    return 1.0;
                if ((k - 1) % step == 0)
                    return 0.8 / std::sqrt (static_cast<double> (k)) * (0.4 + 0.6 * t);
                if (k == 2 * step + 3)
                    return 0.5;
                return 0.0;
            }, 256);
        }
        case 5: // Organ: drawbar registrations (harmonics 1, 2, 3, 4, 6, 8, 10, 12)
        {
            static const int harmonics[8] { 1, 2, 3, 4, 6, 8, 10, 12 };
            static const double bars[kFrames][8] { { 8, 8, 8, 0, 0, 0, 0, 0 }, { 8, 6, 8, 4, 0, 0, 0, 0 }, { 8, 8, 8, 8, 0, 0, 0, 0 },
                                                   { 8, 4, 6, 8, 4, 0, 2, 0 }, { 6, 8, 8, 6, 6, 4, 0, 0 }, { 8, 8, 8, 8, 8, 8, 0, 0 },
                                                   { 8, 3, 8, 3, 8, 3, 8, 3 }, { 8, 8, 8, 8, 8, 8, 8, 8 } };
            return additive ([f] (int k)
            {
                for (int i = 0; i < 8; ++i)
                    if (harmonics[i] == k)
                        return bars[f][i] / 8.0 / std::sqrt (static_cast<double> (i + 1));
                return 0.0;
            }, 12);
        }
        case 6: // Hard Sync: a saw synced to the fundamental, the slave from 1x to 8x
        {
            const double ratio = 1.0 + t * 7.0;
            return fromFunction ([ratio] (double x) { return naiveSaw (std::fmod (x * ratio, 1.0)); });
        }
        case 8: // Brass: harmonics falling steeply when soft, gently when loud, with a bump around 1.2 kHz (on a 220 Hz note)
        {
            const double slope = 2.2 - 1.6 * t;
            return additive ([slope, t] (int k)
            {
                const double hz = 220.0 * k;
                const double bump = 1.0 + (0.5 + t) * std::exp (-0.5 * std::pow ((hz - 1200.0) / 500.0, 2.0));
                return bump / std::pow (static_cast<double> (k), slope);
            }, 90);
        }
        case 9: // FM Bell: a sine phase-modulated by its 2nd (later its 3rd) harmonic, the index rising to 6
        {
            const double index = t * 6.0;
            const double ratio = f < 4 ? 2.0 : 3.0;
            return fromFunction ([index, ratio] (double x) { return std::sin (kTwoPi * x + index * std::sin (kTwoPi * ratio * x)); });
        }
        case 10: // Wavefold: a sine folded back on itself, harder and harder (the "west coast" sound)
        {
            const double gain = 1.0 + t * 7.0;
            return fromFunction ([gain] (double x) { return std::sin (1.5707963 * gain * std::sin (kTwoPi * x)); });
        }
        case 11: // Vowel Bass: a saw through a resonant peak that moves from the 2nd to the 24th harmonic
        {
            const double centre = 2.0 * std::pow (12.0, t);
            return additive ([centre] (int k)
            {
                const double peak = 4.0 * std::exp (-0.5 * std::pow ((k - centre) / (0.6 + centre * 0.15), 2.0));
                return (1.0 + peak) / k;
            }, 200);
        }
        case 12: // Chord Saw: a saw with its octave and fifth above (harmonics 2 and 3) faded in
            return fromFunction ([t] (double x)
            {
                return naiveSaw (x) + t * 0.7 * naiveSaw (std::fmod (2.0 * x, 1.0)) + t * t * 0.6 * naiveSaw (std::fmod (3.0 * x, 1.0));
            });
        case 13: // Metallic: a fixed random set of harmonics, more of them at each frame
        {
            const int count = 3 + f * 5;
            return additive ([count] (int k)
            {
                juce::Random r (static_cast<juce::int64> (k) * 7919);
                const bool on = k == 1 || (k < 120 && r.nextInt (120) < count);
                return on ? (k == 1 ? 1.0 : 0.25 + 0.5 * r.nextDouble()) / std::sqrt (static_cast<double> (k)) : 0.0;
            }, 120);
        }
        case 7:
        default: // Growl: a saw with a sine, ever more driven
        {
            const double drive = 1.0 + t * t * 14.0;
            return fromFunction ([drive, t] (double x)
            {
                const double v = naiveSaw (x) * 0.7 + 0.5 * std::sin (kTwoPi * 2.0 * x + t * 3.0);
                return std::tanh (drive * v) + 0.3 * t * std::sin (kTwoPi * 3.0 * x);
            });
        }
    }
}
} // namespace

const Wavetables& Wavetables::get()
{
    static const Wavetables tables;
    return tables;
}

Wavetables::Wavetables()
{
    const size_t perLevel = kTableSize + 1;
    sets.assign (static_cast<size_t> (kNumWavetables) * kFrames * kTableLevels * perLevel, 0.0f);
    for (int s = 0; s < kNumWavetables; ++s)
        for (int f = 0; f < kFrames; ++f)
            bandLimit (makeFrame (s, f), sets.data() + (static_cast<size_t> (s) * kFrames + static_cast<size_t> (f)) * kTableLevels * perLevel);

    tri.assign (static_cast<size_t> (kTableLevels) * perLevel, 0.0f);
    bandLimit (fromFunction ([] (double x) { return naiveTriangle (std::fmod (x + 0.25, 1.0)); }), tri.data());

    sin.resize (perLevel);
    for (int i = 0; i <= kTableSize; ++i)
        sin[static_cast<size_t> (i)] = static_cast<float> (std::sin (kTwoPi * i / kTableSize));
}

int Wavetables::levelFor (double increment) noexcept
{
    const double allowed = increment > 0.0 ? 0.5 / increment : 1.0e9;
    for (int level = 0; level < kTableLevels - 1; ++level)
        if ((1024 >> level) <= allowed)
            return level;
    return kTableLevels - 1;
}

const float* Wavetables::frame (int set, int f, int level) const noexcept
{
    set = std::clamp (set, 0, kNumWavetables - 1);
    f = std::clamp (f, 0, kFrames - 1);
    level = std::clamp (level, 0, kTableLevels - 1);
    return sets.data() + ((static_cast<size_t> (set) * kFrames + static_cast<size_t> (f)) * kTableLevels + static_cast<size_t> (level)) * (kTableSize + 1);
}

float Wavetables::sample (int set, float position, double phase, int level) const noexcept
{
    const float p = std::clamp (position, 0.0f, 1.0f) * (kFrames - 1);
    const int a = std::min (kFrames - 2, static_cast<int> (p));
    const float mix = p - static_cast<float> (a);
    const float x = readTable (frame (set, a, level), phase);
    const float y = readTable (frame (set, a + 1, level), phase);
    return x + mix * (y - x);
}
} // namespace sunburst
