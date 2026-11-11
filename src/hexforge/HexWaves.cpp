#include "hexforge/HexWaves.h"

#include "hexforge/HexParams.h"

#include <cmath>
#include <functional>

#include <juce_audio_formats/juce_audio_formats.h>

namespace hexforge
{
namespace
{
constexpr double kTwoPi = 6.283185307179586;
using Cycle = std::vector<double>;

const char* const kNames[kNumClassicWaves] {
    "Sine", "Triangle", "Saw", "Ramp", "Square", "Pulse 25", "Pulse 12", "Pulse 5", "Half sine", "Full sine", "Quarter sine", "Camel",
    "Sine + octave", "Sine + fifth", "Organ", "Clarinet", "Bright saw", "Soft saw", "Soft square", "Trapezoid", "Staircase", "Saw-square",
    "Double saw", "Exponential", "Parabola", "Folded sine", "Sync 2x", "Sync 3.5x", "Vowel", "Bell", "Metal", "Digital" };

Cycle time (const std::function<double (double)>& f)
{
    Cycle c (static_cast<size_t> (kTableSize));
    for (int i = 0; i < kTableSize; ++i)
        c[static_cast<size_t> (i)] = f (static_cast<double> (i) / kTableSize);
    return c;
}

Cycle harmonics (const std::function<double (int)>& amplitude, int count)
{
    Cycle c (static_cast<size_t> (kTableSize), 0.0);
    for (int k = 1; k <= count; ++k)
        if (const double a = amplitude (k); std::abs (a) > 1.0e-6)
            for (int i = 0; i < kTableSize; ++i)
                c[static_cast<size_t> (i)] += a * std::sin (kTwoPi * k * i / kTableSize);
    return c;
}

double saw (double x) { return 2.0 * x - 1.0; }
double square (double x, double w = 0.5) { return x < w ? 1.0 : -1.0; }
double tri (double x) { return x < 0.25 ? 4.0 * x : x < 0.75 ? 2.0 - 4.0 * x : 4.0 * x - 4.0; }

Cycle makeClassic (int w)
{
    const auto pulse = [] (double width) { return time ([width] (double x) { return square (x, width); }); };
    switch (w)
    {
        case 0: return time ([] (double x) { return std::sin (kTwoPi * x); });
        case 1: return time (tri);
        case 2: return time (saw);
        case 3: return time ([] (double x) { return -saw (x); });
        case 4: return pulse (0.5);
        case 5: return pulse (0.25);
        case 6: return pulse (0.125);
        case 7: return pulse (0.05);
        case 8: return time ([] (double x) { return x < 0.5 ? std::sin (kTwoPi * x) : 0.0; });
        case 9: return time ([] (double x) { return std::abs (std::sin (kTwoPi * x)); });
        case 10: return time ([] (double x) { return std::fmod (x, 0.5) < 0.25 ? std::sin (kTwoPi * std::fmod (x, 0.5)) : 0.0; });
        case 11: return time ([] (double x) { return std::abs (std::sin (kTwoPi * x)) * (x < 0.5 ? 1.0 : -0.6); });
        case 12: return harmonics ([] (int k) { return k == 1 ? 1.0 : k == 2 ? 0.5 : 0.0; }, 2);
        case 13: return harmonics ([] (int k) { return k == 1 ? 1.0 : k == 3 ? 0.6 : 0.0; }, 3);
        case 14: return harmonics ([] (int k) { return k <= 4 ? 1.0 / k : (k == 6 || k == 8) ? 0.25 : 0.0; }, 8);
        case 15: return harmonics ([] (int k) { return k % 2 == 1 ? std::pow (static_cast<double> (k), -1.5) : 0.0; }, 255);
        case 16: return harmonics ([] (int k) { return 1.0 / std::sqrt (static_cast<double> (k)); }, 1023);
        case 17: return harmonics ([] (int k) { return 1.0 / (static_cast<double> (k) * k); }, 255);
        case 18: return harmonics ([] (int k) { return k % 2 == 1 ? std::pow (static_cast<double> (k), -1.7) : 0.0; }, 255);
        case 19: return time ([] (double x) { return std::clamp (2.5 * tri (x), -1.0, 1.0); });
        case 20: return time ([] (double x) { return std::floor (x * 4.0) / 1.5 - 1.0; });
        case 21: return time ([] (double x) { return 0.5 * saw (x) + 0.5 * square (x); });
        case 22: return time ([] (double x) { return 0.6 * saw (std::fmod (2.0 * x, 1.0)) + 0.4 * saw (x); });
        case 23: return time ([] (double x) { return 2.0 * (std::exp (4.0 * x) - 1.0) / (std::exp (4.0) - 1.0) - 1.0; });
        case 24: return time ([] (double x) { return x < 0.5 ? 1.0 - 16.0 * (x - 0.25) * (x - 0.25) : 16.0 * (x - 0.75) * (x - 0.75) - 1.0; });
        case 25: return time ([] (double x) { return std::sin (1.5707963 * 3.0 * std::sin (kTwoPi * x)); });
        case 26: return time ([] (double x) { return saw (std::fmod (2.0 * x, 1.0)) * (1.0 - x); });
        case 27: return time ([] (double x) { return saw (std::fmod (3.5 * x, 1.0)) * (1.0 - x); });
        case 28: return harmonics ([] (int k)
                                   {
                                       const double hz = 110.0 * k; // the vowel "a" on a low voice
                                       return (std::exp (-0.5 * std::pow ((hz - 800.0) / 90.0, 2.0)) + 0.6 * std::exp (-0.5 * std::pow ((hz - 1150.0) / 110.0, 2.0))
                                               + 0.3 * std::exp (-0.5 * std::pow ((hz - 2900.0) / 150.0, 2.0)) + 0.03) / std::sqrt (static_cast<double> (k));
                                   }, 180);
        case 29: return harmonics ([] (int k) { return k == 1 ? 1.0 : k == 4 ? 0.6 : k == 7 ? 0.45 : k == 11 ? 0.35 : k == 17 ? 0.2 : 0.0; }, 17);
        case 30:
            return harmonics ([] (int k)
                              {
                                  juce::Random r (static_cast<juce::int64> (k) * 7919);
                                  return k == 1 ? 1.0 : (r.nextInt (100) < 22 ? (0.3 + 0.6 * r.nextDouble()) / std::sqrt (static_cast<double> (k)) : 0.0);
                              }, 120);
        default:
        {
            juce::Random r (31337);
            Cycle c (static_cast<size_t> (kTableSize));
            for (int i = 0; i < kTableSize; i += 64) // 32 random steps: a hard digital texture
            {
                const double v = r.nextDouble() * 2.0 - 1.0;
                for (int j = 0; j < 64; ++j)
                    c[static_cast<size_t> (i) + static_cast<size_t> (j)] = v;
            }
            return c;
        }
    }
}

Cycle fromSpectrum (const Spectrum& s)
{
    Cycle c (static_cast<size_t> (kTableSize), 0.0);
    for (int k = 1; k <= kSpectralHarmonics; ++k)
        if (const double a = s.amplitude[static_cast<size_t> (k - 1)]; a > 1.0e-5)
            for (int i = 0; i < kTableSize; ++i)
                c[static_cast<size_t> (i)] += a * std::sin (kTwoPi * (static_cast<double> (k) * i / kTableSize + s.phase[static_cast<size_t> (k - 1)]));
    return c;
}
} // namespace

const HexWaves& HexWaves::get()
{
    static const HexWaves waves;
    return waves;
}

const char* HexWaves::classicName (int wave) { return kNames[std::clamp (wave, 0, kNumClassicWaves - 1)]; }

HexWaves::HexWaves()
{
    tables.assign (static_cast<size_t> (kNumClassicWaves) * kTableLevels * kLevelStride, 0.0f);
    for (int w = 0; w < kNumClassicWaves; ++w)
        sunburst::Wavetables::buildMips (makeClassic (w), tables.data() + static_cast<size_t> (w) * kTableLevels * kLevelStride);
}

const float* HexWaves::classic (int wave) const noexcept
{
    return tables.data() + static_cast<size_t> (std::clamp (wave, 0, kNumClassicWaves - 1)) * kTableLevels * kLevelStride;
}

// --- Spectra -----------------------------------------------------------------------------------------------------------------

Spectrum Spectrum::preset (const juce::String& name)
{
    Spectrum s;
    juce::Random r (name == "Random" ? juce::Time::currentTimeMillis() : 4242);
    for (int k = 1; k <= kSpectralHarmonics; ++k)
    {
        const auto i = static_cast<size_t> (k - 1);
        const double kk = k;
        float a = 0.0f;
        if (name == "Saw")
            a = static_cast<float> (1.0 / kk);
        else if (name == "Square")
            a = k % 2 == 1 ? static_cast<float> (1.0 / kk) : 0.0f;
        else if (name == "Odd")
            a = k % 2 == 1 ? static_cast<float> (1.0 / std::sqrt (kk)) : 0.0f;
        else if (name == "Bright")
            a = static_cast<float> (1.0 / std::sqrt (kk));
        else if (name == "Formant")
            a = static_cast<float> ((std::exp (-0.5 * std::pow ((kk - 9.0) / 2.0, 2.0)) + 0.6 * std::exp (-0.5 * std::pow ((kk - 22.0) / 3.0, 2.0))) + (k == 1 ? 0.5 : 0.0));
        else if (name == "Metal")
            a = k == 1 ? 1.0f : (r.nextInt (100) < 25 ? static_cast<float> (0.3 + 0.7 * r.nextDouble()) : 0.0f);
        else if (name == "Random")
            a = k == 1 ? 1.0f : static_cast<float> (r.nextDouble() * r.nextDouble());
        s.amplitude[i] = a;
        s.phase[i] = name == "Random" || name == "Metal" ? r.nextFloat() : 0.0f;
    }
    if (name == "Clear")
        s.amplitude[0] = 1.0f;
    return s;
}

std::optional<Spectrum> Spectrum::fromAudioFile (const juce::File& file, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples < 16)
    {
        error = "Cannot read " + file.getFileName() + " as audio.";
        return std::nullopt;
    }
    // A single-cycle file (up to 4096 samples) is one cycle; a longer file gives its first 2048 samples.
    const int length = static_cast<int> (std::min<juce::int64> (reader->lengthInSamples, reader->lengthInSamples <= 4096 ? 4096 : 2048));
    juce::AudioBuffer<float> audio (1, length);
    reader->read (&audio, 0, length, 0, true, false);
    Spectrum s;
    float peak = 0.0f;
    for (int k = 1; k <= kSpectralHarmonics; ++k)
    {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < length; ++i)
        {
            const double angle = kTwoPi * k * i / length;
            re += audio.getSample (0, i) * std::cos (angle);
            im += audio.getSample (0, i) * std::sin (angle);
        }
        const auto idx = static_cast<size_t> (k - 1);
        s.amplitude[idx] = static_cast<float> (2.0 * std::sqrt (re * re + im * im) / length);
        s.phase[idx] = static_cast<float> (std::fmod (std::atan2 (re, im) / kTwoPi + 1.0, 1.0)); // sin(x + phase) = im sin + re cos
        peak = std::max (peak, s.amplitude[idx]);
    }
    if (peak <= 1.0e-6f)
    {
        error = file.getFileName() + " is silent.";
        return std::nullopt;
    }
    for (auto& a : s.amplitude)
        a /= peak;
    return s;
}

juce::String Spectrum::toString() const
{
    juce::StringArray parts;
    for (size_t i = 0; i < amplitude.size(); ++i)
        parts.add (juce::String (amplitude[i], 4) + ":" + juce::String (phase[i], 4));
    return parts.joinIntoString (" ");
}

std::optional<Spectrum> Spectrum::fromString (const juce::String& text)
{
    const auto parts = juce::StringArray::fromTokens (text, " ", "");
    if (parts.size() != kSpectralHarmonics)
        return std::nullopt;
    Spectrum s;
    for (int i = 0; i < parts.size(); ++i)
    {
        s.amplitude[static_cast<size_t> (i)] = std::clamp (parts[i].upToFirstOccurrenceOf (":", false, false).getFloatValue(), 0.0f, 1.0f);
        s.phase[static_cast<size_t> (i)] = std::clamp (parts[i].fromFirstOccurrenceOf (":", false, false).getFloatValue(), 0.0f, 1.0f);
    }
    return s;
}

SpectralBank::SpectralBank()
{
    for (auto& slot : buffers)
        for (auto& b : slot)
            b.assign (static_cast<size_t> (kTableLevels) * kLevelStride, 0.0f);
    for (auto& c : current)
        c.store (0);
    set (0, Spectrum::preset ("Bright"));
    set (1, Spectrum::preset ("Metal"));
}

void SpectralBank::set (int slot, const Spectrum& s)
{
    slot &= 1;
    spectra[static_cast<size_t> (slot)] = s;
    const int next = 1 - current[static_cast<size_t> (slot)].load();
    sunburst::Wavetables::buildMips (fromSpectrum (s), buffers[static_cast<size_t> (slot)][static_cast<size_t> (next)].data());
    current[static_cast<size_t> (slot)].store (next, std::memory_order_release);
}

const float* SpectralBank::table (int slot) const noexcept
{
    slot &= 1;
    return buffers[static_cast<size_t> (slot)][static_cast<size_t> (current[static_cast<size_t> (slot)].load (std::memory_order_acquire))].data();
}
} // namespace hexforge
