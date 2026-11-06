#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace sls
{
// The audio thread writes the last stretch of the stereo output here; the GUI thread reads it for the spectrum analyser and
// the goniometer. One writer, one reader, no locks: the reader copies frames that are old enough (a quarter of a second at the
// end of the buffer) that the writer cannot overwrite them while they are copied. Free of JUCE.
class StereoRing
{
public:
    static constexpr int kSize = 16384; // frames (a power of two)

    void push (const float* left, const float* right, int numSamples) noexcept
    {
        auto w = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
        {
            const float l = left != nullptr ? left[i] : 0.0f;
            data[(w & (kSize - 1)) * 2] = l;
            data[(w & (kSize - 1)) * 2 + 1] = right != nullptr ? right[i] : l;
            ++w;
        }
        writePos.store (w, std::memory_order_release);
    }

    // The latest `count` frames (at most kSize / 2), oldest first. Returns how many were copied (fewer at the very start).
    int latest (float* left, float* right, int count) const noexcept
    {
        count = std::min (count, kSize / 2);
        const auto w = writePos.load (std::memory_order_acquire);
        const auto available = static_cast<int> (std::min<std::uint64_t> (w, static_cast<std::uint64_t> (count)));
        for (int i = 0; i < available; ++i)
        {
            const auto idx = ((w - static_cast<std::uint64_t> (available) + static_cast<std::uint64_t> (i)) & (kSize - 1)) * 2;
            left[i] = data[idx];
            right[i] = data[idx + 1];
        }
        return available;
    }

private:
    std::array<float, kSize * 2> data {};
    std::atomic<std::uint64_t> writePos { 0 };
};

// Magnitude spectrum of a block of mono samples: Hann window, scaled so that a full-scale sine reads 0 dB at its frequency.
class SpectrumAnalyser
{
public:
    static constexpr int kFftSize = 4096;
    static constexpr int kBins = kFftSize / 2 + 1;

    SpectrumAnalyser()
    {
        constexpr double pi = 3.14159265358979323846;
        for (int i = 0; i < kFftSize; ++i)
            window[static_cast<size_t> (i)] = 0.5 - 0.5 * std::cos (2.0 * pi * i / kFftSize);
        double sum = 0.0;
        for (const auto w : window)
            sum += w;
        scale = 2.0 / sum;
        for (int i = 0; i < kFftSize / 2; ++i)
            twiddle[static_cast<size_t> (i)] = std::polar (1.0, -2.0 * pi * i / kFftSize);
    }

    // `samples`: kFftSize values (fewer are padded with zeros). `outDb`: kBins values, in dB below full scale (floor -140).
    void analyse (const float* samples, int count, float* outDb)
    {
        for (int i = 0; i < kFftSize; ++i)
            work[static_cast<size_t> (i)] = { i < count ? static_cast<double> (samples[i]) * window[static_cast<size_t> (i)] : 0.0, 0.0 };
        fft();
        for (int b = 0; b < kBins; ++b)
        {
            const double magnitude = std::abs (work[static_cast<size_t> (b)]) * scale * (b == 0 || b == kFftSize / 2 ? 0.5 : 1.0);
            outDb[b] = magnitude > 1.0e-7 ? static_cast<float> (20.0 * std::log10 (magnitude)) : -140.0f;
        }
    }

    static double binFrequency (int bin, double sampleRate) noexcept { return sampleRate * bin / kFftSize; }

private:
    void fft()
    {
        // iterative radix-2, decimation in time
        for (int i = 1, j = 0; i < kFftSize; ++i)
        {
            int bit = kFftSize >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap (work[static_cast<size_t> (i)], work[static_cast<size_t> (j)]);
        }
        for (int len = 2; len <= kFftSize; len <<= 1)
        {
            const int step = kFftSize / len;
            for (int i = 0; i < kFftSize; i += len)
                for (int k = 0; k < len / 2; ++k)
                {
                    const auto u = work[static_cast<size_t> (i + k)];
                    const auto v = work[static_cast<size_t> (i + k + len / 2)] * twiddle[static_cast<size_t> (k * step)];
                    work[static_cast<size_t> (i + k)] = u + v;
                    work[static_cast<size_t> (i + k + len / 2)] = u - v;
                }
        }
    }

    std::array<double, kFftSize> window {};
    std::array<std::complex<double>, kFftSize / 2> twiddle {};
    std::array<std::complex<double>, kFftSize> work {};
    double scale = 1.0;
};

// Correlation of the two channels of a block, -1 (opposite) .. +1 (identical); 0 for silence or unrelated signals.
inline float stereoCorrelation (const float* left, const float* right, int count) noexcept
{
    double ll = 0.0, rr = 0.0, lr = 0.0;
    for (int i = 0; i < count; ++i)
    {
        ll += static_cast<double> (left[i]) * left[i];
        rr += static_cast<double> (right[i]) * right[i];
        lr += static_cast<double> (left[i]) * right[i];
    }
    const double d = std::sqrt (ll * rr);
    return d > 1.0e-12 ? static_cast<float> (std::clamp (lr / d, -1.0, 1.0)) : 0.0f;
}
} // namespace sls
