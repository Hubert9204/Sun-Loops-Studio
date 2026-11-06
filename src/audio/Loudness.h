#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace sls
{
// Loudness and peak metering of a stereo signal, as in ITU-R BS.1770-4 and EBU R128 / Tech 3341 / Tech 3342. Free of JUCE.
//
//   momentary  : K-weighted mean square over 400 ms, in LUFS
//   short-term : the same over 3 s
//   integrated : the whole programme since reset(), with the absolute gate (-70 LUFS) and the relative gate (-10 LU)
//   range (LRA): the spread (95th - 10th percentile) of the short-term loudness, absolute gate -70 LUFS, relative gate -20 LU
//   true peak  : the highest value of the signal reconstructed at four times the sample rate, in dBTP
//
// process() never allocates, locks or does I/O (the audio thread calls it). The results are atomics: any thread may read them.
// "Silence" (nothing above the gate yet) reads kSilence.
class LoudnessMeter
{
public:
    static constexpr float kSilence = -120.0f;

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        blockLength = std::max (1, static_cast<int> (std::lround (sampleRate * 0.1)));
        makeKWeighting();
        makeInterpolator();
        resetAll();
    }

    // Starts a new measurement (integrated loudness, range and peaks). Safe to call from any thread: the audio thread does it.
    void requestReset() noexcept { resetRequested.store (true, std::memory_order_release); }

    void process (const float* left, const float* right, int numSamples) noexcept
    {
        if (resetRequested.exchange (false, std::memory_order_acq_rel))
            resetAll();

        for (int i = 0; i < numSamples; ++i)
        {
            const float l = left != nullptr ? left[i] : 0.0f;
            const float r = right != nullptr ? right[i] : l;

            blockPeak = std::max ({ blockPeak, std::fabs (l), std::fabs (r) });
            truePeakBlock = std::max (truePeakBlock, interpolatedPeak (l, r));

            const double wl = weight (0, static_cast<double> (l));
            const double wr = weight (1, static_cast<double> (r));
            blockSum += wl * wl + wr * wr;

            if (++blockFill >= blockLength)
                finishBlock();
        }
        publishPeaks();
    }

    float momentary() const noexcept { return momentaryLufs.load (std::memory_order_relaxed); }
    float shortTerm() const noexcept { return shortTermLufs.load (std::memory_order_relaxed); }
    float integrated() const noexcept { return integratedLufs.load (std::memory_order_relaxed); }
    float range() const noexcept { return rangeLu.load (std::memory_order_relaxed); }
    float truePeakDb() const noexcept { return truePeak.load (std::memory_order_relaxed); }
    float samplePeakDb() const noexcept { return samplePeak.load (std::memory_order_relaxed); }

private:
    static constexpr int kBins = 800;       // 0.1 LU steps from -70 to +10 LUFS
    static constexpr double kFloorLufs = -70.0;
    static constexpr double kOffset = -0.691;
    static constexpr int kShortBlocks = 30; // 3 s of 100 ms blocks
    static constexpr int kPerPhase = 32;    // true-peak interpolator: 32 taps for each of the 4 phases
    static constexpr int kTaps = 4 * kPerPhase;
    static constexpr int kHistory = 32;     // a power of two, at least kPerPhase

    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        std::array<double, 2> z1 {}, z2 {}; // one state per channel

        double run (int ch, double x) noexcept
        {
            const auto c = static_cast<size_t> (ch);
            const double y = b0 * x + z1[c];
            z1[c] = b1 * x - a1 * y + z2[c];
            z2[c] = b2 * x - a2 * y;
            return y;
        }
    };

    struct Histogram
    {
        std::array<std::uint32_t, kBins> count {};
        std::array<double, kBins> energy {}; // sum of the mean-square energies in the bin

        void clear() noexcept { count.fill (0); energy.fill (0.0); }
        void add (double loudness, double meanSquare) noexcept
        {
            const int bin = std::clamp (static_cast<int> ((loudness - kFloorLufs) * 10.0), 0, kBins - 1);
            ++count[static_cast<size_t> (bin)];
            energy[static_cast<size_t> (bin)] += meanSquare;
        }
    };

    static double toLufs (double meanSquare) noexcept { return meanSquare > 0.0 ? kOffset + 10.0 * std::log10 (meanSquare) : -1.0e9; }
    static double binLufs (int bin) noexcept { return kFloorLufs + 0.1 * bin; }

    // Mean energy of everything above `gateLufs` in the histogram (0 and count 0 if nothing is).
    static double gatedMean (const Histogram& h, double gateLufs, std::uint64_t& n) noexcept
    {
        double sum = 0.0;
        n = 0;
        for (int b = 0; b < kBins; ++b)
            if (binLufs (b) > gateLufs && h.count[static_cast<size_t> (b)] > 0)
            {
                sum += h.energy[static_cast<size_t> (b)];
                n += h.count[static_cast<size_t> (b)];
            }
        return n > 0 ? sum / static_cast<double> (n) : 0.0;
    }

    void makeKWeighting() noexcept
    {
        constexpr double pi = 3.14159265358979323846;
        {   // stage 1: the "head" high shelf
            const double f0 = 1681.974450955533, gainDb = 3.999843853973347, q = 0.7071752369554196;
            const double k = std::tan (pi * f0 / sampleRate);
            const double vh = std::pow (10.0, gainDb / 20.0);
            const double vb = std::pow (vh, 0.4996667741545416);
            const double a0 = 1.0 + k / q + k * k;
            stage1.b0 = (vh + vb * k / q + k * k) / a0;
            stage1.b1 = 2.0 * (k * k - vh) / a0;
            stage1.b2 = (vh - vb * k / q + k * k) / a0;
            stage1.a1 = 2.0 * (k * k - 1.0) / a0;
            stage1.a2 = (1.0 - k / q + k * k) / a0;
        }
        {   // stage 2: the RLB high-pass
            const double f0 = 38.13547087602444, q = 0.5003270373238773;
            const double k = std::tan (pi * f0 / sampleRate);
            const double a0 = 1.0 + k / q + k * k;
            stage2.b0 = 1.0;
            stage2.b1 = -2.0;
            stage2.b2 = 1.0;
            stage2.a1 = 2.0 * (k * k - 1.0) / a0;
            stage2.a2 = (1.0 - k / q + k * k) / a0;
        }
    }

    // A windowed sinc, four phases, 32 taps each: reconstructs the signal between the samples. Every phase is scaled to unity
    // gain at DC, so a slow wave is measured at its true height.
    void makeInterpolator() noexcept
    {
        constexpr double pi = 3.14159265358979323846;
        constexpr int centre = kTaps / 2;
        std::array<double, kTaps> h {};
        for (int k = 0; k < kTaps; ++k)
        {
            const double x = (k - centre) / 4.0;
            const double sinc = std::fabs (x) < 1.0e-12 ? 1.0 : std::sin (pi * x) / (pi * x);
            const double t = static_cast<double> (k) / (kTaps - 1);
            const double window = 0.35875 - 0.48829 * std::cos (2.0 * pi * t) + 0.14128 * std::cos (4.0 * pi * t) - 0.01168 * std::cos (6.0 * pi * t); // Blackman-Harris
            h[static_cast<size_t> (k)] = sinc * window;
        }
        for (int phase = 0; phase < 4; ++phase)
        {
            double sum = 0.0;
            for (int j = 0; j < kPerPhase; ++j)
                sum += h[static_cast<size_t> (4 * j + phase)];
            for (int j = 0; j < kPerPhase; ++j)
                taps[static_cast<size_t> (4 * j + phase)] = static_cast<float> (h[static_cast<size_t> (4 * j + phase)] / sum);
        }
    }

    double weight (int ch, double x) noexcept { return stage2.run (ch, stage1.run (ch, x)); }

    float interpolatedPeak (float l, float r) noexcept
    {
        historyL[static_cast<size_t> (historyPos)] = l;
        historyR[static_cast<size_t> (historyPos)] = r;
        float best = 0.0f;
        for (int phase = 0; phase < 4; ++phase)
        {
            float sl = 0.0f, sr = 0.0f;
            for (int j = 0; j < kPerPhase; ++j)
            {
                const auto h = taps[static_cast<size_t> (4 * j + phase)];
                const auto idx = static_cast<size_t> ((historyPos - j + kHistory) & (kHistory - 1));
                sl += h * historyL[idx];
                sr += h * historyR[idx];
            }
            best = std::max ({ best, std::fabs (sl), std::fabs (sr) });
        }
        historyPos = (historyPos + 1) & (kHistory - 1);
        return best;
    }

    void finishBlock() noexcept
    {
        const double meanSquare = blockSum / static_cast<double> (blockLength); // left + right energy of this block (channel weights 1)
        blockFill = 0;
        blockSum = 0.0;

        energies[static_cast<size_t> (energyPos)] = meanSquare;
        energyPos = (energyPos + 1) % kShortBlocks;
        filled = std::min (filled + 1, kShortBlocks);

        const auto meanOfLast = [this] (int count)
        {
            double sum = 0.0;
            for (int i = 1; i <= count; ++i)
                sum += energies[static_cast<size_t> ((energyPos - i + 2 * kShortBlocks) % kShortBlocks)];
            return sum / count;
        };

        if (filled >= 4)
        {
            const double m = meanOfLast (4);
            const double lufs = toLufs (m);
            momentaryLufs.store (floorLufs (lufs), std::memory_order_relaxed);
            if (lufs > kFloorLufs) // absolute gate
                integratedHistogram.add (lufs, m);
            updateIntegrated();
        }
        if (filled >= kShortBlocks)
        {
            const double m = meanOfLast (kShortBlocks);
            const double lufs = toLufs (m);
            shortTermLufs.store (floorLufs (lufs), std::memory_order_relaxed);
            if (lufs > kFloorLufs)
                rangeHistogram.add (lufs, m);
            updateRange();
        }
        else if (filled >= 4)
        {
            shortTermLufs.store (floorLufs (toLufs (meanOfLast (filled))), std::memory_order_relaxed); // until 3 s have gone by: what there is
        }
    }

    static float floorLufs (double v) noexcept { return v > kSilence ? static_cast<float> (v) : kSilence; }

    void updateIntegrated() noexcept
    {
        std::uint64_t n = 0;
        const double absolute = gatedMean (integratedHistogram, kFloorLufs, n);
        if (n == 0)
        {
            integratedLufs.store (kSilence, std::memory_order_relaxed);
            return;
        }
        const double relativeGate = toLufs (absolute) - 10.0;
        const double gated = gatedMean (integratedHistogram, relativeGate, n);
        integratedLufs.store (n > 0 ? floorLufs (toLufs (gated)) : kSilence, std::memory_order_relaxed);
    }

    void updateRange() noexcept
    {
        std::uint64_t n = 0;
        const double absolute = gatedMean (rangeHistogram, kFloorLufs, n);
        if (n == 0)
            return;
        const double relativeGate = toLufs (absolute) - 20.0;

        std::uint64_t total = 0;
        for (int b = 0; b < kBins; ++b)
            if (binLufs (b) > relativeGate)
                total += rangeHistogram.count[static_cast<size_t> (b)];
        if (total < 2)
        {
            rangeLu.store (0.0f, std::memory_order_relaxed);
            return;
        }

        const auto percentile = [&] (double p)
        {
            const double target = p * static_cast<double> (total - 1);
            std::uint64_t seen = 0;
            for (int b = 0; b < kBins; ++b)
            {
                if (! (binLufs (b) > relativeGate))
                    continue;
                seen += rangeHistogram.count[static_cast<size_t> (b)];
                if (static_cast<double> (seen) > target)
                    return binLufs (b) + 0.05; // the middle of the bin
            }
            return binLufs (kBins - 1);
        };
        rangeLu.store (static_cast<float> (std::max (0.0, percentile (0.95) - percentile (0.10))), std::memory_order_relaxed);
    }

    void publishPeaks() noexcept
    {
        samplePeakMax = std::max (samplePeakMax, blockPeak);
        truePeakMax = std::max (truePeakMax, truePeakBlock);
        blockPeak = 0.0f;
        truePeakBlock = 0.0f;
        const auto db = [] (float v) { return v > 1.0e-6f ? 20.0f * std::log10 (v) : kSilence; };
        samplePeak.store (db (samplePeakMax), std::memory_order_relaxed);
        truePeak.store (db (truePeakMax), std::memory_order_relaxed);
    }

    void resetAll() noexcept
    {
        stage1.z1.fill (0.0); stage1.z2.fill (0.0);
        stage2.z1.fill (0.0); stage2.z2.fill (0.0);
        energies.fill (0.0);
        energyPos = 0;
        filled = 0;
        blockFill = 0;
        blockSum = 0.0;
        blockPeak = truePeakBlock = samplePeakMax = truePeakMax = 0.0f;
        historyL.fill (0.0f);
        historyR.fill (0.0f);
        historyPos = 0;
        integratedHistogram.clear();
        rangeHistogram.clear();
        momentaryLufs.store (kSilence, std::memory_order_relaxed);
        shortTermLufs.store (kSilence, std::memory_order_relaxed);
        integratedLufs.store (kSilence, std::memory_order_relaxed);
        rangeLu.store (0.0f, std::memory_order_relaxed);
        samplePeak.store (kSilence, std::memory_order_relaxed);
        truePeak.store (kSilence, std::memory_order_relaxed);
    }

    double sampleRate = 48000.0;
    int blockLength = 4800;
    Biquad stage1, stage2;
    std::array<float, kTaps> taps {};
    std::array<float, kHistory> historyL {}, historyR {};
    int historyPos = 0;

    int blockFill = 0;
    double blockSum = 0.0;
    float blockPeak = 0.0f, truePeakBlock = 0.0f, samplePeakMax = 0.0f, truePeakMax = 0.0f;
    std::array<double, kShortBlocks> energies {};
    int energyPos = 0, filled = 0;
    Histogram integratedHistogram, rangeHistogram;

    std::atomic<bool> resetRequested { false };
    std::atomic<float> momentaryLufs { kSilence }, shortTermLufs { kSilence }, integratedLufs { kSilence }, rangeLu { 0.0f };
    std::atomic<float> samplePeak { kSilence }, truePeak { kSilence };
};
} // namespace sls
