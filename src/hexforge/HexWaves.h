#pragma once

#include <array>
#include <atomic>
#include <optional>
#include <vector>

#include <juce_core/juce_core.h>

#include "sunburst/Wavetables.h"

namespace hexforge
{
using sunburst::kTableLevels;
using sunburst::kTableSize;
constexpr int kSpectralHarmonics = 64;
constexpr size_t kLevelStride = kTableSize + 1;

// The operators' waves: 32 classic single cycles, built once and band-limited in sunburst::kTableLevels mip levels (no aliasing).
class HexWaves
{
public:
    static const HexWaves& get();
    static const char* classicName (int wave);
    const float* classic (int wave) const noexcept; // level 0; level l is at + l * kLevelStride

private:
    HexWaves();
    std::vector<float> tables;
};

// A spectral wave: the amplitude (0..1) and phase (0..1 of a cycle) of harmonics 1..64.
struct Spectrum
{
    std::array<float, kSpectralHarmonics> amplitude {}, phase {};

    static Spectrum preset (const juce::String& name); // "Saw", "Square", "Odd", "Bright", "Formant", "Metal", "Random", "Clear"
    // From a single-cycle WAV (or the first 2048 samples of a longer one): its first 64 harmonics. Empty on error.
    static std::optional<Spectrum> fromAudioFile (const juce::File& file, juce::String& error);
    juce::String toString() const;
    static std::optional<Spectrum> fromString (const juce::String& text);
    bool operator== (const Spectrum&) const = default;
};

// The two spectral waves the operators can use (Spectral A and B). The message thread builds a new table into the buffer that is
// not playing and then switches; the audio thread asks for the current one at each block.
class SpectralBank
{
public:
    SpectralBank();
    void set (int slot, const Spectrum& s);                   // message thread
    const Spectrum& spectrum (int slot) const noexcept { return spectra[static_cast<size_t> (slot & 1)]; }
    const float* table (int slot) const noexcept;             // audio thread: level 0 of the current table

private:
    std::array<Spectrum, 2> spectra {};
    std::array<std::array<std::vector<float>, 2>, 2> buffers {}; // [slot][buffer]
    std::array<std::atomic<int>, 2> current {};
};
} // namespace hexforge
