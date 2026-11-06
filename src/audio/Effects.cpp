#include "audio/Effects.h"

#include <algorithm>

namespace sls
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr float kDenormal = 1.0e-20f;

inline float flush (float v) noexcept { return std::fabs (v) < kDenormal ? 0.0f : v; }

// Freeverb tunings at 44.1 kHz.
constexpr int kCombTunings[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
constexpr int kAllpassTunings[4] = { 556, 441, 341, 225 };
constexpr int kStereoSpread = 23;

constexpr float kMaxDelaySeconds = 1.0f;
} // namespace

void EffectUnit::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    const double scale = sampleRate / 44100.0;

    for (size_t ch = 0; ch < 2; ++ch)
    {
        const int spread = ch == 0 ? 0 : kStereoSpread;
        for (size_t i = 0; i < kCombs; ++i)
            combs[ch][i].buf.assign (static_cast<size_t> (std::max (8.0, (kCombTunings[i] + spread) * scale)), 0.0f);
        for (size_t i = 0; i < kAllpasses; ++i)
            allpasses[ch][i].buf.assign (static_cast<size_t> (std::max (8.0, (kAllpassTunings[i] + spread) * scale)), 0.0f);

        delayLine[ch].assign (static_cast<size_t> (sampleRate * kMaxDelaySeconds) + 8, 0.0f);
    }

    active = EffectType::None;
    reset();
}

void EffectUnit::reset() noexcept
{
    for (auto& channel : combs)
        for (auto& c : channel)
        {
            std::fill (c.buf.begin(), c.buf.end(), 0.0f);
            c.idx = 0;
            c.store = 0.0f;
        }
    for (auto& channel : allpasses)
        for (auto& a : channel)
        {
            std::fill (a.buf.begin(), a.buf.end(), 0.0f);
            a.idx = 0;
        }
    for (auto& d : delayLine)
        std::fill (d.begin(), d.end(), 0.0f);

    delayWrite = 0;
    delayCurrent = -1.0f; // snap to the target time on first use
    delayTone = { 0.0f, 0.0f };
    env = 0.0f;
    low.clearState();
    mid.clearState();
    high.clearState();
    eqCached = { 1.0e9f, 1.0e9f, 1.0e9f, 1.0e9f }; // force coefficients to be recomputed
    dirty = false;
}

void EffectUnit::process (const EffectSlot& requested, float* left, float* right, int numSamples) noexcept
{
    if (requested.type == EffectType::Plugin)
    {
        // A plugin keeps its own state; bypass simply leaves the signal alone.
        active = EffectType::Plugin;
        if (! requested.bypass && requested.plugin != nullptr)
            requested.plugin->process (left, right, numSamples, nullptr, 0);
        return;
    }
    const EffectSlot slot = requested.sanitised();

    if (slot.type != active)
    {
        active = slot.type;
        reset();
    }

    if (slot.type == EffectType::None)
        return;

    if (slot.bypass)
    {
        dirty = true; // whatever rings inside is stale by the time the effect is switched on again
        return;
    }
    if (dirty)
        reset();

    switch (slot.type)
    {
        case EffectType::Equalizer:  runEqualizer (slot, left, right, numSamples); break;
        case EffectType::Reverb:     runReverb (slot, left, right, numSamples); break;
        case EffectType::Delay:      runDelay (slot, left, right, numSamples); break;
        case EffectType::Compressor: runCompressor (slot, left, right, numSamples); break;
        case EffectType::Plugin:
        case EffectType::None:       break;
    }
}

// ------------------------------------------------------------------------- equalizer

void EffectUnit::Biquad::process (float* l, float* r, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const double xl = l[i];
        const double yl = b0 * xl + z1l;
        z1l = b1 * xl - a1 * yl + z2l;
        z2l = b2 * xl - a2 * yl;
        l[i] = static_cast<float> (yl);

        const double xr = r[i];
        const double yr = b0 * xr + z1r;
        z1r = b1 * xr - a1 * yr + z2r;
        z2r = b2 * xr - a2 * yr;
        r[i] = static_cast<float> (yr);
    }
}

void EffectUnit::runEqualizer (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    const std::array<float, 4> wanted { s.params[0], s.params[1], s.params[2], s.params[3] };

    if (wanted != eqCached)
    {
        eqCached = wanted;

        auto finish = [] (Biquad& q, double b0, double b1, double b2, double a0, double a1, double a2)
        {
            q.b0 = b0 / a0;
            q.b1 = b1 / a0;
            q.b2 = b2 / a0;
            q.a1 = a1 / a0;
            q.a2 = a2 / a0;
        };

        auto shelf = [&] (Biquad& q, bool isLow, double hz, double gainDb)
        {
            const double A = std::pow (10.0, gainDb / 40.0);
            const double w0 = 2.0 * kPi * std::min (hz, sampleRate * 0.45) / sampleRate;
            const double cw = std::cos (w0);
            const double alpha = std::sin (w0) / 2.0 * std::sqrt (2.0); // shelf slope S = 1
            const double k = 2.0 * std::sqrt (A) * alpha;
            if (isLow)
                finish (q, A * ((A + 1) - (A - 1) * cw + k), 2 * A * ((A - 1) - (A + 1) * cw),
                        A * ((A + 1) - (A - 1) * cw - k),
                        (A + 1) + (A - 1) * cw + k, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - k);
            else
                finish (q, A * ((A + 1) + (A - 1) * cw + k), -2 * A * ((A - 1) + (A + 1) * cw),
                        A * ((A + 1) + (A - 1) * cw - k),
                        (A + 1) - (A - 1) * cw + k, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - k);
        };

        shelf (low, true, 120.0, wanted[0]);
        shelf (high, false, 6000.0, wanted[3]);

        const double A = std::pow (10.0, static_cast<double> (wanted[1]) / 40.0);
        const double w0 = 2.0 * kPi * std::min (static_cast<double> (wanted[2]), sampleRate * 0.45) / sampleRate;
        const double alpha = std::sin (w0) / (2.0 * 0.8); // Q = 0.8
        const double cw = std::cos (w0);
        finish (mid, 1 + alpha * A, -2 * cw, 1 - alpha * A, 1 + alpha / A, -2 * cw, 1 - alpha / A);
    }

    // A band at 0 dB is skipped entirely (and kept clean for when it is raised again).
    if (std::fabs (wanted[0]) >= 0.01f) low.process (l, r, n); else low.clearState();
    if (std::fabs (wanted[1]) >= 0.01f) mid.process (l, r, n); else mid.clearState();
    if (std::fabs (wanted[3]) >= 0.01f) high.process (l, r, n); else high.clearState();
}

// ------------------------------------------------------------------------- reverb

void EffectUnit::runReverb (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    const float feedback = 0.7f + 0.28f * s.params[0];
    const float damp1 = 0.4f * s.params[1];
    const float damp2 = 1.0f - damp1;
    const float width = s.params[2];
    const float mix = s.params[3];
    const float wet1 = 3.0f * (width * 0.5f + 0.5f) * mix;
    const float wet2 = 3.0f * ((1.0f - width) * 0.5f) * mix;
    const float dry = 1.0f - mix;
    constexpr float kInputGain = 0.015f;

    for (int i = 0; i < n; ++i)
    {
        const float inL = l[i];
        const float inR = r[i];
        const float input = (inL + inR) * kInputGain;

        float out[2] = { 0.0f, 0.0f };
        for (size_t ch = 0; ch < 2; ++ch)
        {
            for (auto& c : combs[ch])
            {
                const float v = c.buf[static_cast<size_t> (c.idx)];
                c.store = flush (v * damp2 + c.store * damp1);
                c.buf[static_cast<size_t> (c.idx)] = flush (input + c.store * feedback);
                if (++c.idx >= static_cast<int> (c.buf.size()))
                    c.idx = 0;
                out[ch] += v;
            }
            for (auto& a : allpasses[ch])
            {
                const float buffered = a.buf[static_cast<size_t> (a.idx)];
                const float y = -out[ch] + buffered;
                a.buf[static_cast<size_t> (a.idx)] = flush (out[ch] + buffered * 0.5f);
                if (++a.idx >= static_cast<int> (a.buf.size()))
                    a.idx = 0;
                out[ch] = y;
            }
        }

        l[i] = out[0] * wet1 + out[1] * wet2 + inL * dry;
        r[i] = out[1] * wet1 + out[0] * wet2 + inR * dry;
    }
}

// ------------------------------------------------------------------------- delay

void EffectUnit::runDelay (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    const int size = static_cast<int> (delayLine[0].size());
    const float target = std::min (static_cast<float> (s.params[0] * 0.001 * sampleRate), static_cast<float> (size - 4));
    const float feedback = s.params[1];
    const double fc = 400.0 * std::pow (40.0, static_cast<double> (s.params[2])); // 400 Hz .. 16 kHz
    const float toneCoef = static_cast<float> (std::min (1.0, 1.0 - std::exp (-2.0 * kPi * fc / sampleRate)));
    const float mix = s.params[3];
    const float dry = 1.0f - mix;

    if (delayCurrent < 0.0f)
        delayCurrent = target;

    float* io[2] = { l, r };

    for (int i = 0; i < n; ++i)
    {
        delayCurrent += (target - delayCurrent) * 0.0008f; // glide, so changing the time does not click

        float rp = static_cast<float> (delayWrite) - delayCurrent;
        if (rp < 0.0f)
            rp += static_cast<float> (size);
        const int i0 = std::min (static_cast<int> (rp), size - 1);
        const float frac = rp - static_cast<float> (i0);
        const int i1 = i0 + 1 >= size ? 0 : i0 + 1;

        for (size_t ch = 0; ch < 2; ++ch)
        {
            auto& line = delayLine[ch];
            const float delayed = line[static_cast<size_t> (i0)] + frac * (line[static_cast<size_t> (i1)] - line[static_cast<size_t> (i0)]);

            delayTone[ch] = flush (delayTone[ch] + toneCoef * (delayed - delayTone[ch]));
            const float in = io[ch][i];
            line[static_cast<size_t> (delayWrite)] = flush (in + delayTone[ch] * feedback);
            io[ch][i] = in * dry + delayed * mix;
        }

        if (++delayWrite >= size)
            delayWrite = 0;
    }
}

// ------------------------------------------------------------------------- compressor

void EffectUnit::runCompressor (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    const float threshold = s.params[0];
    const float slope = 1.0f - 1.0f / s.params[1];
    const float attack = static_cast<float> (1.0 - std::exp (-1.0 / (s.params[2] * 0.001 * sampleRate)));
    const float release = static_cast<float> (1.0 - std::exp (-1.0 / (s.params[3] * 0.001 * sampleRate)));
    const float makeup = s.params[4];
    constexpr float kLn10Over20 = 0.115129254649702f;

    for (int i = 0; i < n; ++i)
    {
        const float level = std::max (std::fabs (l[i]), std::fabs (r[i]));
        env += (level - env) * (level > env ? attack : release);
        env = flush (env);

        const float levelDb = 20.0f * std::log10 (std::max (env, 1.0e-6f));
        const float over = levelDb - threshold;
        const float reduction = over > 0.0f ? over * slope : 0.0f;
        const float gain = std::exp ((makeup - reduction) * kLn10Over20);

        l[i] *= gain;
        r[i] *= gain;
    }
}
} // namespace sls
