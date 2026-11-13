#include "audio/Convolver.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sls
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kB = Convolver::kBlock;
constexpr int kN = 2 * kB;       // FFT size
constexpr int kBins = kB + 1;    // of a real signal's spectrum

// A radix-2 FFT of kN points on separate real and imaginary arrays (float: the convolution multiplies hundreds of spectra a block).
struct BlockFft
{
    std::array<float, kN / 2> cosT {}, sinT {};
    std::array<int, kN> reversed {};

    BlockFft()
    {
        for (int i = 0; i < kN / 2; ++i)
        {
            cosT[static_cast<size_t> (i)] = static_cast<float> (std::cos (2.0 * kPi * i / kN));
            sinT[static_cast<size_t> (i)] = static_cast<float> (std::sin (2.0 * kPi * i / kN));
        }
        int bits = 0;
        while ((1 << bits) < kN)
            ++bits;
        for (int i = 0; i < kN; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b))
                    r |= 1 << (bits - 1 - b);
            reversed[static_cast<size_t> (i)] = r;
        }
    }

    // forward: e^(-i...), inverse: e^(+i...) and divided by kN
    void run (float* re, float* im, bool inverse) const noexcept
    {
        for (int i = 0; i < kN; ++i)
        {
            const int j = reversed[static_cast<size_t> (i)];
            if (i < j)
            {
                std::swap (re[i], re[j]);
                std::swap (im[i], im[j]);
            }
        }
        for (int len = 2; len <= kN; len <<= 1)
        {
            const int half = len / 2, step = kN / len;
            for (int start = 0; start < kN; start += len)
                for (int k = 0; k < half; ++k)
                {
                    const float c = cosT[static_cast<size_t> (k * step)];
                    const float s = inverse ? sinT[static_cast<size_t> (k * step)] : -sinT[static_cast<size_t> (k * step)];
                    const int a = start + k, b = a + half;
                    const float tr = re[b] * c - im[b] * s;
                    const float ti = re[b] * s + im[b] * c;
                    re[b] = re[a] - tr;
                    im[b] = im[a] - ti;
                    re[a] += tr;
                    im[a] += ti;
                }
        }
        if (inverse)
        {
            const float scale = 1.0f / static_cast<float> (kN);
            for (int i = 0; i < kN; ++i)
            {
                re[i] *= scale;
                im[i] *= scale;
            }
        }
    }
};

const BlockFft& blockFft()
{
    static const BlockFft fft;
    return fft;
}

// Uniform noise in -1..1, the same on every machine.
struct Noise
{
    std::uint32_t state;
    float next() noexcept
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float> (static_cast<double> (state >> 8) / 8388608.0 - 1.0);
    }
};

struct SpaceShape
{
    double onsetMs;        // how long the dense part takes to build up
    double earlyFromMs, earlyToMs;
    int earlyTaps;
    double earlyLevel;     // of the first reflections against the dense part
    double topHz, endHz;   // the low-pass over the dense part, at the start and at the end (both scaled by the brightness)
};

SpaceShape shapeOf (int space) noexcept
{
    switch (space)
    {
        case Convolver::Room:      return { 2.0, 4.0, 35.0, 10, 0.9, 14000.0, 3500.0 };
        case Convolver::Plate:     return { 0.5, 0.0, 0.0, 0, 0.0, 18000.0, 6000.0 };
        case Convolver::Cathedral: return { 90.0, 30.0, 220.0, 12, 0.5, 9000.0, 1200.0 };
        case Convolver::Spring:    return { 1.0, 0.0, 0.0, 0, 0.0, 6000.0, 2500.0 };
        default:                   return { 25.0, 15.0, 95.0, 9, 0.7, 12000.0, 2200.0 }; // Hall
    }
}

void normalise (std::array<std::vector<float>, 2>& ir) noexcept
{
    // The same gain for both channels, so that the louder one has unit energy: noise keeps its level through the reverb.
    double energy = 0.0;
    for (const auto& ch : ir)
    {
        double e = 0.0;
        for (const float v : ch)
            e += static_cast<double> (v) * v;
        energy = std::max (energy, e);
    }
    if (energy < 1.0e-20)
        return;
    const auto gain = static_cast<float> (1.0 / std::sqrt (energy));
    for (auto& ch : ir)
        for (auto& v : ch)
            v *= gain;
}
} // namespace

std::vector<float> makeImpulseResponse (const Convolver::Settings& s, int channel)
{
    const double rate = std::max (8000.0, s.sampleRate);
    const double seconds = std::clamp (static_cast<double> (s.seconds), 0.05, static_cast<double> (Convolver::kMaxSeconds));
    const int frames = std::max (16, static_cast<int> (seconds * rate));
    const int preDelay = static_cast<int> (std::clamp (static_cast<double> (s.preDelayMs), 0.0, 200.0) * 0.001 * rate);
    const double bright = std::clamp (static_cast<double> (s.brightness), 0.0, 1.0);
    const auto shape = shapeOf (s.space);
    std::vector<float> body (static_cast<size_t> (frames), 0.0f);
    Noise noise { 0x9e3779b9u * static_cast<std::uint32_t> (s.space + 1) + (channel != 0 ? 0x51ed270bu : 0u) };

    if (s.space == Convolver::Spring)
    {
        // A spring: the pulse runs up and down the coil (an echo every few tens of milliseconds, weaker each time) and the coil is
        // dispersive (high frequencies arrive later), which a long chain of all-pass filters does to each echo: the "boing".
        const double period = (channel == 0 ? 0.037 : 0.041) * rate;
        for (int echo = 0; echo * period < frames; ++echo)
        {
            const auto at = static_cast<size_t> (echo * period);
            const double level = std::pow (10.0, -3.0 * static_cast<double> (at) / frames);
            body[at] += static_cast<float> (level * ((echo & 1) != 0 ? -1.0 : 1.0));
            if (at + 3 < body.size()) // a little noise after each echo: the coil's own rattle
                for (size_t k = 1; k < 3; ++k)
                    body[at + k] += static_cast<float> (0.3 * level * noise.next());
        }
        const float a = 0.62f;
        for (int stage = 0; stage < 90; ++stage)
        {
            float x1 = 0.0f, y1 = 0.0f;
            for (auto& v : body)
            {
                const float y = -a * v + x1 + a * y1;
                x1 = v;
                y1 = y;
                v = y;
            }
        }
    }
    else
    {
        // The dense part: noise that dies away by 60 dB over the length, after a short build-up.
        const double onset = std::max (1.0, shape.onsetMs * 0.001 * rate);
        for (int i = 0; i < frames; ++i)
        {
            const double t = static_cast<double> (i) / frames;
            const double env = std::pow (10.0, -3.0 * t) * std::min (1.0, i / onset);
            body[static_cast<size_t> (i)] = static_cast<float> (env * noise.next());
        }
        // The first reflections: a few single echoes, later and weaker one after the other.
        for (int k = 0; k < shape.earlyTaps; ++k)
        {
            const double u = (k + 0.5 + 0.35 * noise.next()) / shape.earlyTaps;
            const double ms = shape.earlyFromMs + (shape.earlyToMs - shape.earlyFromMs) * u * u;
            const auto at = static_cast<size_t> (ms * 0.001 * rate);
            if (at < body.size())
                body[at] += static_cast<float> (shape.earlyLevel * (1.0 - 0.6 * u) * (noise.next() > 0.0f ? 1.0 : -1.0) * 4.0);
        }
    }

    // The high end dies first: a low-pass whose cutoff falls from the top to the end frequency over the response.
    const double top = std::min (0.45 * rate, shape.topHz * (0.35 + 0.65 * bright) * (0.6 + 0.8 * bright));
    const double end = std::min (top, shape.endHz * (0.3 + 1.4 * bright));
    float lp = 0.0f, lp2 = 0.0f;
    for (int i = 0; i < frames; ++i)
    {
        const double hz = top * std::pow (end / top, static_cast<double> (i) / frames);
        const auto coef = static_cast<float> (1.0 - std::exp (-2.0 * kPi * hz / rate));
        lp += coef * (body[static_cast<size_t> (i)] - lp);
        lp2 += coef * (lp - lp2);
        body[static_cast<size_t> (i)] = lp2;
    }
    // (and no rumble: a gentle high-pass at 60 Hz)
    {
        const auto coef = static_cast<float> (1.0 - std::exp (-2.0 * kPi * 60.0 / rate));
        float low = 0.0f;
        for (auto& v : body)
        {
            low += coef * (v - low);
            v -= low;
        }
    }
    // The last few milliseconds fade to nothing, so the response does not end in a click.
    const int fade = std::min (frames, static_cast<int> (0.01 * rate));
    for (int i = 0; i < fade; ++i)
        body[static_cast<size_t> (frames - 1 - i)] *= static_cast<float> (i) / static_cast<float> (fade);

    std::vector<float> ir (static_cast<size_t> (preDelay), 0.0f);
    ir.insert (ir.end(), body.begin(), body.end());
    return ir;
}

std::shared_ptr<Convolver> Convolver::make (const Settings& requested, std::shared_ptr<const SampleData> file)
{
    std::shared_ptr<Convolver> c (new Convolver());
    c->config = requested;
    c->config.space = std::clamp (requested.space, 0, static_cast<int> (File));
    c->config.seconds = std::clamp (requested.seconds, 0.05f, kMaxSeconds);
    c->source = std::move (file);
    const double rate = std::max (8000.0, requested.sampleRate);

    if (c->config.space == File)
    {
        if (c->source != nullptr && c->source->numFrames() > 1)
        {
            // The file at the engine's rate (linear interpolation), cut at the length with a fade over its last tenth.
            const auto& data = *c->source;
            const double ratio = data.sampleRate / rate;
            const int frames = std::min (static_cast<int> (c->config.seconds * rate), static_cast<int> (data.numFrames() / ratio));
            const int preDelay = static_cast<int> (std::clamp (static_cast<double> (requested.preDelayMs), 0.0, 200.0) * 0.001 * rate);
            for (size_t ch = 0; ch < 2; ++ch)
            {
                const auto& in = ch == 0 || data.right.size() != data.left.size() ? data.left : data.right;
                auto& out = c->impulse[ch];
                out.assign (static_cast<size_t> (preDelay + std::max (0, frames)), 0.0f);
                const int fade = std::max (1, frames / 10);
                for (int i = 0; i < frames; ++i)
                {
                    const double at = i * ratio;
                    const auto i0 = static_cast<size_t> (at);
                    const auto frac = static_cast<float> (at - static_cast<double> (i0));
                    const float v = in[i0] + (in[std::min (i0 + 1, in.size() - 1)] - in[i0]) * frac;
                    out[static_cast<size_t> (preDelay + i)] = v * std::min (1.0f, static_cast<float> (frames - i) / static_cast<float> (fade));
                }
            }
        }
        // (no file yet: an empty response, the effect is silent on its wet side)
    }
    else
    {
        c->impulse[0] = makeImpulseResponse (c->config, 0);
        c->impulse[1] = makeImpulseResponse (c->config, 1);
    }
    normalise (c->impulse);

    const int frames = c->impulseFrames();
    c->partitions = frames > kB ? (frames - kB + kB - 1) / kB : 0;
    const auto& fft = blockFft();
    std::array<float, kN> re {}, im {};
    for (size_t ch = 0; ch < 2; ++ch)
    {
        const auto& ir = c->impulse[ch];
        for (int i = 0; i < kB; ++i)
            c->head[ch][static_cast<size_t> (i)] = i < frames ? ir[static_cast<size_t> (i)] : 0.0f;
        const auto size = static_cast<size_t> (c->partitions) * kBins;
        c->hRe[ch].assign (size, 0.0f);
        c->hIm[ch].assign (size, 0.0f);
        c->xRe[ch].assign (size, 0.0f);
        c->xIm[ch].assign (size, 0.0f);
        for (int p = 0; p < c->partitions; ++p)
        {
            re.fill (0.0f);
            im.fill (0.0f);
            for (int i = 0; i < kB; ++i)
            {
                const int at = (p + 1) * kB + i;
                if (at < frames)
                    re[static_cast<size_t> (i)] = ir[static_cast<size_t> (at)];
            }
            fft.run (re.data(), im.data(), false);
            std::copy_n (re.begin(), kBins, c->hRe[ch].begin() + p * kBins);
            std::copy_n (im.begin(), kBins, c->hIm[ch].begin() + p * kBins);
        }
    }
    return c;
}

void Convolver::clearState() noexcept
{
    for (size_t ch = 0; ch < 2; ++ch)
    {
        std::fill (xRe[ch].begin(), xRe[ch].end(), 0.0f);
        std::fill (xIm[ch].begin(), xIm[ch].end(), 0.0f);
        history[ch].fill (0.0f);
        blockIn[ch].fill (0.0f);
        blockBefore[ch].fill (0.0f);
        tailOut[ch].fill (0.0f);
    }
    pos = ring = 0;
}

void Convolver::computeTail() noexcept
{
    // The block just completed is block k: its spectrum (with the block before it, overlap-save) joins the ring, and the sum over
    // the partitions p of spectrum[k - p] x response[p] is what the taps after the first kBlock give for block k + 1.
    const auto& fft = blockFft();
    std::array<float, kN> re, im, accRe, accIm;
    for (size_t ch = 0; ch < 2; ++ch)
    {
        if (partitions > 0)
        {
            std::copy_n (blockBefore[ch].begin(), kB, re.begin());
            std::copy_n (blockIn[ch].begin(), kB, re.begin() + kB);
            im.fill (0.0f);
            fft.run (re.data(), im.data(), false);
            std::copy_n (re.begin(), kBins, xRe[ch].begin() + ring * kBins);
            std::copy_n (im.begin(), kBins, xIm[ch].begin() + ring * kBins);

            accRe.fill (0.0f);
            accIm.fill (0.0f);
            for (int p = 0; p < partitions; ++p)
            {
                int slot = ring - p;
                if (slot < 0)
                    slot += partitions;
                const float* xr = xRe[ch].data() + slot * kBins;
                const float* xi = xIm[ch].data() + slot * kBins;
                const float* hr = hRe[ch].data() + p * kBins;
                const float* hi = hIm[ch].data() + p * kBins;
                for (int b = 0; b < kBins; ++b)
                {
                    accRe[static_cast<size_t> (b)] += xr[b] * hr[b] - xi[b] * hi[b];
                    accIm[static_cast<size_t> (b)] += xr[b] * hi[b] + xi[b] * hr[b];
                }
            }
            for (int b = 1; b < kB; ++b) // the other half of a real signal's spectrum
            {
                accRe[static_cast<size_t> (kN - b)] = accRe[static_cast<size_t> (b)];
                accIm[static_cast<size_t> (kN - b)] = -accIm[static_cast<size_t> (b)];
            }
            fft.run (accRe.data(), accIm.data(), true);
            std::copy_n (accRe.begin() + kB, kB, tailOut[ch].begin());
        }
        blockBefore[ch] = blockIn[ch];
    }
    if (partitions > 0)
        ring = (ring + 1) % partitions;
}

void Convolver::process (float* left, float* right, int numSamples) noexcept
{
    float* io[2] = { left, right };
    for (int i = 0; i < numSamples; ++i)
    {
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const float x = io[ch][i];
            // The newest input sits at history[pos + kB] and at history[pos]: the kBlock samples ending there are one run.
            history[ch][static_cast<size_t> (pos)] = x;
            history[ch][static_cast<size_t> (pos + kB)] = x;
            const float* newest = history[ch].data() + pos + kB;
            const float* taps = head[ch].data();
            float acc = 0.0f;
            for (int m = 0; m < kB; ++m)
                acc += taps[m] * newest[-m];
            blockIn[ch][static_cast<size_t> (pos)] = x;
            io[ch][i] = acc + tailOut[ch][static_cast<size_t> (pos)];
        }
        if (++pos == kB)
        {
            computeTail();
            pos = 0;
        }
    }
}
} // namespace sls
