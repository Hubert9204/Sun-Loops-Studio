#include "audio/StemSplit.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include "audio/Fft.h"

namespace sls
{
namespace
{
using Cplx = std::complex<double>;
constexpr int kSize = 2048;
constexpr int kHop = kSize / 4;
constexpr int kBins = kSize / 2 + 1;

struct FrameData
{
    int index = -1;
    std::vector<float> mag;           // of the mid signal, for the masks
    std::vector<Cplx> left, right;    // the spectra
};

double median (std::vector<float>& v, size_t n)
{
    std::nth_element (v.begin(), v.begin() + static_cast<std::ptrdiff_t> (n / 2), v.begin() + static_cast<std::ptrdiff_t> (n));
    return v[n / 2];
}
} // namespace

Stems splitStems (const SampleData& in, const StemSettings& settingsIn, const std::function<bool (double)>& progress)
{
    Stems out;
    const int total = in.numFrames();
    if (total < 2 || in.sampleRate <= 0.0)
        return out;
    StemSettings s = settingsIn;
    s.timeSpan = std::clamp (s.timeSpan | 1, 3, 63);
    s.frequencySpan = std::clamp (s.frequencySpan | 1, 3, 63);
    s.hardness = std::clamp (std::isfinite (s.hardness) ? s.hardness : 2.0, 0.5, 8.0);
    s.bassCrossoverHz = std::clamp (std::isfinite (s.bassCrossoverHz) ? s.bassCrossoverHz : 150.0, 40.0, 1000.0);

    // Padded by a frame on each side, so every sample is covered by four frames.
    const int padded = total + 2 * kSize;
    const int frames = (padded - kSize) / kHop + 1;
    const auto sampleAt = [&] (const std::vector<float>& x, int i) -> double
    {
        const int j = i - kSize;
        return j >= 0 && j < total ? x[static_cast<size_t> (j)] : 0.0;
    };

    const Fft fft (kSize);
    std::vector<double> window (kSize);
    for (int i = 0; i < kSize; ++i)
        window[static_cast<size_t> (i)] = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * i / kSize);

    // Bass weight per bin: 1 below crossover / sqrt 2, 0 above crossover * sqrt 2, a raised cosine in log frequency between.
    std::vector<double> low (kBins);
    for (int k = 0; k < kBins; ++k)
    {
        const double hz = k * in.sampleRate / kSize;
        const double lo = s.bassCrossoverHz / std::sqrt (2.0), hi = s.bassCrossoverHz * std::sqrt (2.0);
        if (hz <= lo)
            low[static_cast<size_t> (k)] = 1.0;
        else if (hz >= hi)
            low[static_cast<size_t> (k)] = 0.0;
        else
            low[static_cast<size_t> (k)] = 0.5 + 0.5 * std::cos (3.14159265358979323846 * std::log (hz / lo) / std::log (hi / lo));
    }

    const int half = s.timeSpan / 2;
    std::vector<FrameData> ring (static_cast<size_t> (s.timeSpan));
    std::vector<Cplx> work (kSize);
    const auto ensure = [&] (int f) -> FrameData&
    {
        auto& slot = ring[static_cast<size_t> (f % s.timeSpan)];
        if (slot.index == f)
            return slot;
        slot.index = f;
        slot.left.assign (kSize, {});
        slot.right.assign (kSize, {});
        for (int i = 0; i < kSize; ++i)
        {
            const double w = window[static_cast<size_t> (i)];
            slot.left[static_cast<size_t> (i)] = sampleAt (in.left, f * kHop + i) * w;
            slot.right[static_cast<size_t> (i)] = sampleAt (in.right, f * kHop + i) * w;
        }
        fft.forward (slot.left);
        fft.forward (slot.right);
        slot.mag.resize (kBins);
        for (int k = 0; k < kBins; ++k)
            slot.mag[static_cast<size_t> (k)] = static_cast<float> (std::abs (0.5 * (slot.left[static_cast<size_t> (k)] + slot.right[static_cast<size_t> (k)])));
        return slot;
    };

    std::vector<double> drumL (static_cast<size_t> (padded)), drumR (static_cast<size_t> (padded)), bassL (static_cast<size_t> (padded)),
        bassR (static_cast<size_t> (padded));
    std::vector<float> scratch (static_cast<size_t> (std::max (s.timeSpan, s.frequencySpan)));
    std::vector<double> harmonic (kBins), percussive (kBins);
    std::vector<Cplx> dL (kSize), dR (kSize), bL (kSize), bR (kSize);

    for (int t = 0; t < frames; ++t)
    {
        if (progress && (t & 63) == 0 && ! progress (static_cast<double> (t) / frames))
            return {};
        for (int f = std::max (0, t - half); f <= std::min (frames - 1, t + half); ++f)
            ensure (f);
        const auto& cur = ring[static_cast<size_t> (t % s.timeSpan)];

        // Steady (median across time) and short (median across frequency) parts of the magnitude.
        for (int k = 0; k < kBins; ++k)
        {
            size_t n = 0;
            for (int f = std::max (0, t - half); f <= std::min (frames - 1, t + half); ++f)
                scratch[n++] = ring[static_cast<size_t> (f % s.timeSpan)].mag[static_cast<size_t> (k)];
            harmonic[static_cast<size_t> (k)] = median (scratch, n);
        }
        const int fhalf = s.frequencySpan / 2;
        for (int k = 0; k < kBins; ++k)
        {
            size_t n = 0;
            for (int j = std::max (0, k - fhalf); j <= std::min (kBins - 1, k + fhalf); ++j)
                scratch[n++] = cur.mag[static_cast<size_t> (j)];
            percussive[static_cast<size_t> (k)] = median (scratch, n);
        }

        for (int k = 0; k < kBins; ++k)
        {
            const double h = std::pow (harmonic[static_cast<size_t> (k)], s.hardness), p = std::pow (percussive[static_cast<size_t> (k)], s.hardness);
            const double pm = h + p > 1.0e-30 ? p / (h + p) : 0.5;
            const double bm = (1.0 - pm) * low[static_cast<size_t> (k)];
            const auto kk = static_cast<size_t> (k);
            dL[kk] = cur.left[kk] * pm;
            dR[kk] = cur.right[kk] * pm;
            bL[kk] = cur.left[kk] * bm;
            bR[kk] = cur.right[kk] * bm;
            if (k > 0 && k < kSize / 2) // the mirrored half, so the inverse is real
            {
                const auto m = static_cast<size_t> (kSize - k);
                dL[m] = std::conj (dL[kk]);
                dR[m] = std::conj (dR[kk]);
                bL[m] = std::conj (bL[kk]);
                bR[m] = std::conj (bR[kk]);
            }
        }
        fft.inverse (dL);
        fft.inverse (dR);
        fft.inverse (bL);
        fft.inverse (bR);
        // Hann analysis and synthesis at a quarter-frame hop add up to 1.5: divided out here.
        for (int i = 0; i < kSize; ++i)
        {
            const double w = window[static_cast<size_t> (i)] / 1.5;
            const auto at = static_cast<size_t> (t) * kHop + static_cast<size_t> (i);
            drumL[at] += dL[static_cast<size_t> (i)].real() * w;
            drumR[at] += dR[static_cast<size_t> (i)].real() * w;
            bassL[at] += bL[static_cast<size_t> (i)].real() * w;
            bassR[at] += bR[static_cast<size_t> (i)].real() * w;
        }
    }

    const auto make = [&in] (const char* what)
    {
        auto d = std::make_shared<SampleData>();
        d->sampleRate = in.sampleRate;
        d->name = in.name + " " + what;
        d->left.resize (static_cast<size_t> (in.numFrames()));
        d->right.resize (static_cast<size_t> (in.numFrames()));
        return d;
    };
    out.drums = make ("drums");
    out.bass = make ("bass");
    out.other = make ("other");
    for (int i = 0; i < total; ++i)
    {
        const auto j = static_cast<size_t> (i), at = j + kSize;
        out.drums->left[j] = static_cast<float> (drumL[at]);
        out.drums->right[j] = static_cast<float> (drumR[at]);
        out.bass->left[j] = static_cast<float> (bassL[at]);
        out.bass->right[j] = static_cast<float> (bassR[at]);
        // The rest is what the other two leave: the three always add up to the input.
        out.other->left[j] = in.left[j] - out.drums->left[j] - out.bass->left[j];
        out.other->right[j] = in.right[j] - out.drums->right[j] - out.bass->right[j];
    }
    if (progress)
        progress (1.0);
    return out;
}
} // namespace sls
