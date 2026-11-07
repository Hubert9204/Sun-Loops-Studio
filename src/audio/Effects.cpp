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

constexpr float kMaxDelaySeconds = 1.6f; // 1 s plus the right channel's spread (x 1.5) and the tape wobble

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }

// RBJ filter designs, normalised (a0 = 1).
struct Coefs { double b0, b1, b2, a1, a2; };

Coefs shelfCoefs (bool isLow, double hz, double gainDb, double sampleRate) noexcept
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * std::min (hz, sampleRate * 0.45) / sampleRate;
    const double cw = std::cos (w0);
    const double alpha = std::sin (w0) / 2.0 * std::sqrt (2.0); // shelf slope S = 1
    const double k = 2.0 * std::sqrt (A) * alpha;
    double b0, b1, b2, a0, a1, a2;
    if (isLow)
    {
        b0 = A * ((A + 1) - (A - 1) * cw + k); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - k);
        a0 = (A + 1) + (A - 1) * cw + k; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - k;
    }
    else
    {
        b0 = A * ((A + 1) + (A - 1) * cw + k); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - k);
        a0 = (A + 1) - (A - 1) * cw + k; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - k;
    }
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

Coefs peakCoefs (double hz, double gainDb, double q, double sampleRate) noexcept
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * std::min (hz, sampleRate * 0.45) / sampleRate;
    const double alpha = std::sin (w0) / (2.0 * q);
    const double cw = std::cos (w0);
    const double a0 = 1 + alpha / A;
    return { (1 + alpha * A) / a0, -2 * cw / a0, (1 - alpha * A) / a0, -2 * cw / a0, (1 - alpha / A) / a0 };
}
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
    preDelay.assign (static_cast<size_t> (sampleRate * 0.26) + 4, 0.0f);

    // Maximizer: 1.5 ms of look-ahead.
    lookahead = std::max (1, static_cast<int> (std::lround (0.0015 * sampleRate)));
    for (auto& line : lookLine)
        line.assign (static_cast<size_t> (lookahead) + 1, 0.0f);

    // Overdrive: 12th-order Butterworth low-pass at 0.38 x the base rate (18 kHz at 48 kHz), run at the oversampled rate.
    const double osRate = sampleRate * kOversample;
    const double fc = 0.38 * sampleRate;
    for (size_t stage = 0; stage < static_cast<size_t> (kAaStages); ++stage)
    {
        const double stageQ = 1.0 / (2.0 * std::cos ((2.0 * static_cast<double> (stage) + 1.0) * kPi / (4.0 * kAaStages)));
        const double w0 = 2.0 * kPi * fc / osRate;
        const double alpha = std::sin (w0) / (2.0 * stageQ);
        const double cw = std::cos (w0);
        const double a0 = 1.0 + alpha;
        Biquad q;
        q.b0 = (1.0 - cw) / 2.0 / a0;
        q.b1 = (1.0 - cw) / a0;
        q.b2 = (1.0 - cw) / 2.0 / a0;
        q.a1 = -2.0 * cw / a0;
        q.a2 = (1.0 - alpha) / a0;
        upFilter[stage] = q;
        downFilter[stage] = q;
    }

    // Pitch Corrector: the analysis runs at the rate divided by pcFactor (about 12 kHz), after a fourth-order Butterworth low-pass at
    // 0.38 x the decimated rate.
    pcFactor = std::max (1, static_cast<int> (std::lround (sampleRate / 12000.0)));
    {
        const double cutoff = 0.38 * sampleRate / pcFactor;
        const double qs[2] = { 0.54119610, 1.30656296 };
        for (int stage = 0; stage < 2; ++stage)
        {
            const double w0 = 2.0 * kPi * cutoff / sampleRate;
            const double alpha = std::sin (w0) / (2.0 * qs[stage]);
            const double cw = std::cos (w0);
            const double a0 = 1.0 + alpha;
            Biquad q;
            q.b0 = (1.0 - cw) / 2.0 / a0;
            q.b1 = (1.0 - cw) / a0;
            q.b2 = (1.0 - cw) / 2.0 / a0;
            q.a1 = -2.0 * cw / a0;
            q.a2 = (1.0 - alpha) / a0;
            (stage == 0 ? pcAa1 : pcAa2) = q;
        }
    }

    active = EffectType::None;
    reverbType = -1;
    setReverbType (0);
    reset();
}

void EffectUnit::setReverbType (int type) noexcept
{
    if (type == reverbType)
        return;
    reverbType = type;
    // Hall uses the whole lines (the original Freeverb tuning); Room and Plate shorter ones.
    const double factor = type == 1 ? 0.55 : type == 2 ? 0.8 : 1.0;
    for (auto& channel : combs)
        for (auto& c : channel)
        {
            c.len = std::clamp (static_cast<int> (std::lround (static_cast<double> (c.buf.size()) * factor)), 1, static_cast<int> (c.buf.size()));
            c.idx = 0;
        }
    for (auto& channel : allpasses)
        for (auto& ap : channel)
        {
            ap.len = std::clamp (static_cast<int> (std::lround (static_cast<double> (ap.buf.size()) * (type == 2 ? 0.7 : factor))), 1,
                                 static_cast<int> (ap.buf.size()));
            ap.idx = 0;
        }
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
    std::fill (preDelay.begin(), preDelay.end(), 0.0f);
    preDelayWrite = 0;
    for (auto& d : delayLine)
        std::fill (d.begin(), d.end(), 0.0f);
    for (auto& d : lookLine)
        std::fill (d.begin(), d.end(), 0.0f);
    lookWrite = 0;
    limiterGain = limiterTarget = 1.0f;

    delayWrite = 0;
    delayCurrent = { -1.0f, -1.0f }; // snap to the target time on first use
    delayTone = { 0.0f, 0.0f };
    wobblePhase = flutterPhase = lfoPhase = 0.0;
    env = env2 = 0.0f;
    low.clearState();
    mid.clearState();
    high.clearState();
    eqValid = false; // force coefficients to be recomputed
    for (auto& q : upFilter)
        q.clearState();
    for (auto& q : downFilter)
        q.clearState();
    driveLast = colorLow = toneState = dcX = dcY = {};
    adaaLast = {};
    for (auto& st : phaserState)
        st.fill (0.0f);
    phaserLast = {};
    svf1a = svf1b = svf2a = svf2b = {};
    controlLeft = 0;
    sideLow = {};
    if (timeL != nullptr)
    {
        std::fill_n (timeL, timeFrames, 0.0f);
        std::fill_n (timeR, timeFrames, 0.0f);
    }
    timeWrite = 0;
    lastLag = fadeLag = 0.0;
    fadeLeft = 0;
    volume = 1.0f;

    pcDecim.fill (0.0f);
    pcAa1.clearState();
    pcAa2.clearState();
    pcPhase = pcDecimWrite = pcControl = 0;
    pcHopLeft = kPitchHop;
    pcPeriod = 0.0;
    pcNote = -1;
    pcTarget = pcOffset = 0.0f;
    pcRatio = 1.0;
    pcAlign = 0.01 * sampleRate; // until the first analysis says how far back it looks
    const int life = std::max (16, static_cast<int> (0.03 * sampleRate));
    pcHeads[0] = { pcAlign, 0, life };
    pcHeads[1] = { pcAlign, life / 2, life };
    dirty = false;
}

void EffectUnit::attachTimeBuffer (float* left, float* right, int frames) noexcept
{
    timeL = frames > 0 ? left : nullptr;
    timeR = frames > 0 ? right : nullptr;
    timeFrames = timeL != nullptr ? frames : 0;
    if (timeL != nullptr)
    {
        std::fill_n (timeL, timeFrames, 0.0f);
        std::fill_n (timeR, timeFrames, 0.0f);
    }
    timeWrite = 0;
    lastLag = fadeLag = 0.0;
    fadeLeft = 0;
}

void EffectUnit::process (const EffectSlot& requested, float* left, float* right, int numSamples, const EffectTransport& transport) noexcept
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
        case EffectType::Equalizer:    runEqualizer (slot, left, right, numSamples); break;
        case EffectType::Reverb:       runReverb (slot, left, right, numSamples); break;
        case EffectType::Delay:        runDelay (slot, left, right, numSamples); break;
        case EffectType::Compressor:   runCompressor (slot, left, right, numSamples); break;
        case EffectType::Overdrive:    runOverdrive (slot, left, right, numSamples); break;
        case EffectType::Chorus:       runChorus (slot, left, right, numSamples); break;
        case EffectType::Phaser:       runPhaser (slot, left, right, numSamples); break;
        case EffectType::Filter:       runFilter (slot, left, right, numSamples); break;
        case EffectType::ParametricEq: runParametricEq (slot, left, right, numSamples); break;
        case EffectType::Maximizer:    runMaximizer (slot, left, right, numSamples); break;
        case EffectType::Transient:    runTransient (slot, left, right, numSamples); break;
        case EffectType::Imager:       runImager (slot, left, right, numSamples); break;
        case EffectType::TimeShaper:   runTimeShaper (slot, left, right, numSamples, transport); break;
        case EffectType::PitchCorrector: runPitchCorrector (slot, left, right, numSamples); break;
        case EffectType::Plugin:
        case EffectType::None:         break;
    }
}

// ------------------------------------------------------------------------- equalizer

void EffectUnit::Biquad::process (float* l, float* r, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        l[i] = static_cast<float> (tickL (l[i]));
        r[i] = static_cast<float> (tickR (r[i]));
    }
}

namespace
{
void setCoefs (double& b0, double& b1, double& b2, double& a1, double& a2, const Coefs& c) noexcept
{
    b0 = c.b0;
    b1 = c.b1;
    b2 = c.b2;
    a1 = c.a1;
    a2 = c.a2;
}
} // namespace

void EffectUnit::runEqualizer (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    if (! eqValid || s.params != eqCached)
    {
        eqCached = s.params;
        eqValid = true;
        setCoefs (low.b0, low.b1, low.b2, low.a1, low.a2, shelfCoefs (true, 120.0, s.params[0], sampleRate));
        setCoefs (high.b0, high.b1, high.b2, high.a1, high.a2, shelfCoefs (false, 6000.0, s.params[3], sampleRate));
        setCoefs (mid.b0, mid.b1, mid.b2, mid.a1, mid.a2, peakCoefs (s.params[2], s.params[1], 0.8, sampleRate));
    }

    // A band at 0 dB is skipped entirely (and kept clean for when it is raised again).
    if (std::fabs (s.params[0]) >= 0.01f) low.process (l, r, n); else low.clearState();
    if (std::fabs (s.params[1]) >= 0.01f) mid.process (l, r, n); else mid.clearState();
    if (std::fabs (s.params[3]) >= 0.01f) high.process (l, r, n); else high.clearState();
}

void EffectUnit::runParametricEq (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    if (! eqValid || s.params != eqCached)
    {
        eqCached = s.params;
        eqValid = true;
        setCoefs (low.b0, low.b1, low.b2, low.a1, low.a2, shelfCoefs (true, s.params[0], s.params[1], sampleRate));
        setCoefs (mid.b0, mid.b1, mid.b2, mid.a1, mid.a2, peakCoefs (s.params[2], s.params[3], s.params[4], sampleRate));
        setCoefs (high.b0, high.b1, high.b2, high.a1, high.a2, shelfCoefs (false, s.params[5], s.params[6], sampleRate));
    }
    if (std::fabs (s.params[1]) >= 0.01f) low.process (l, r, n); else low.clearState();
    if (std::fabs (s.params[3]) >= 0.01f) mid.process (l, r, n); else mid.clearState();
    if (std::fabs (s.params[6]) >= 0.01f) high.process (l, r, n); else high.clearState();
    if (std::fabs (s.params[7]) >= 0.01f)
    {
        const float g = dbToGain (s.params[7]);
        for (int i = 0; i < n; ++i)
        {
            l[i] *= g;
            r[i] *= g;
        }
    }
}

// ------------------------------------------------------------------------- reverb

void EffectUnit::runReverb (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    const int type = effectChoice (EffectType::Reverb, 4, s.params[4]);
    if (type != reverbType)
    {
        setReverbType (type);
        for (auto& channel : combs)
            for (auto& c : channel)
            {
                std::fill (c.buf.begin(), c.buf.end(), 0.0f);
                c.store = 0.0f;
            }
        for (auto& channel : allpasses)
            for (auto& ap : channel)
                std::fill (ap.buf.begin(), ap.buf.end(), 0.0f);
    }

    // Hall is the reverb of 0.10.0. Room: shorter lines, less feedback, more damping. Plate: dense and bright.
    const float size = s.params[0];
    const float feedback = type == 1 ? 0.55f + 0.33f * size : type == 2 ? 0.72f + 0.26f * size : 0.7f + 0.28f * size;
    const float damp1 = type == 1 ? 0.15f + 0.45f * s.params[1] : type == 2 ? 0.2f * s.params[1] : 0.4f * s.params[1];
    const float damp2 = 1.0f - damp1;
    const float allpassGain = type == 2 ? 0.62f : 0.5f;
    const float width = s.params[2];
    const float mix = s.params[3];
    const float wet1 = 3.0f * (width * 0.5f + 0.5f) * mix;
    const float wet2 = 3.0f * ((1.0f - width) * 0.5f) * mix;
    const float dry = 1.0f - mix;
    constexpr float kInputGain = 0.015f;
    const int preSize = static_cast<int> (preDelay.size());
    const int preSamples = std::min (static_cast<int> (s.params[5] * 0.001f * static_cast<float> (sampleRate)), preSize - 1);

    for (int i = 0; i < n; ++i)
    {
        const float inL = l[i];
        const float inR = r[i];
        float input = (inL + inR) * kInputGain;
        if (preSamples > 0)
        {
            preDelay[static_cast<size_t> (preDelayWrite)] = input;
            int readAt = preDelayWrite - preSamples;
            if (readAt < 0)
                readAt += preSize;
            input = preDelay[static_cast<size_t> (readAt)];
            if (++preDelayWrite >= preSize)
                preDelayWrite = 0;
        }

        float out[2] = { 0.0f, 0.0f };
        for (size_t ch = 0; ch < 2; ++ch)
        {
            for (auto& c : combs[ch])
            {
                const float v = c.buf[static_cast<size_t> (c.idx)];
                c.store = flush (v * damp2 + c.store * damp1);
                c.buf[static_cast<size_t> (c.idx)] = flush (input + c.store * feedback);
                if (++c.idx >= c.len)
                    c.idx = 0;
                out[ch] += v;
            }
            for (auto& a : allpasses[ch])
            {
                const float buffered = a.buf[static_cast<size_t> (a.idx)];
                const float y = -out[ch] + buffered;
                a.buf[static_cast<size_t> (a.idx)] = flush (out[ch] + buffered * allpassGain);
                if (++a.idx >= a.len)
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
    const int mode = effectChoice (EffectType::Delay, 4, s.params[4]); // 0 stereo, 1 ping-pong, 2 tape
    const float base = static_cast<float> (s.params[0] * 0.001 * sampleRate);
    const float wobble = s.params[6];
    const float maxDelay = static_cast<float> (size - 4) - (wobble > 0.0f ? static_cast<float> (0.012 * sampleRate) : 0.0f);
    const std::array<float, 2> target { std::min (base, maxDelay), std::min (base * (1.0f + s.params[5]), maxDelay) };
    const float feedback = s.params[1];
    const double fc = 400.0 * std::pow (40.0, static_cast<double> (s.params[2])) * (mode == 2 ? 0.45 : 1.0); // 400 Hz .. 16 kHz (tape: darker)
    const float toneCoef = static_cast<float> (std::min (1.0, 1.0 - std::exp (-2.0 * kPi * fc / sampleRate)));
    const float mix = s.params[3];
    const float dry = 1.0f - mix;
    const double wowStep = 2.0 * kPi * 0.6 / sampleRate, flutterStep = 2.0 * kPi * 6.5 / sampleRate;

    for (size_t ch = 0; ch < 2; ++ch)
        if (delayCurrent[ch] < 0.0f)
            delayCurrent[ch] = target[ch];

    float* io[2] = { l, r };

    for (int i = 0; i < n; ++i)
    {
        // Tape wow and flutter: the read point drifts (up to about 6 ms at full wobble).
        float drift = 0.0f;
        if (wobble > 0.0f)
        {
            wobblePhase += wowStep;
            flutterPhase += flutterStep;
            if (wobblePhase > 2.0 * kPi) wobblePhase -= 2.0 * kPi;
            if (flutterPhase > 2.0 * kPi) flutterPhase -= 2.0 * kPi;
            drift = wobble * static_cast<float> (sampleRate * (0.005 * (1.0 + std::sin (wobblePhase)) + 0.0004 * (1.0 + std::sin (flutterPhase))));
        }

        std::array<float, 2> delayed {};
        for (size_t ch = 0; ch < 2; ++ch)
        {
            delayCurrent[ch] += (target[ch] - delayCurrent[ch]) * 0.0008f; // glide, so changing the time does not click
            float rp = static_cast<float> (delayWrite) - (delayCurrent[ch] + drift);
            while (rp < 0.0f)
                rp += static_cast<float> (size);
            const int i0 = std::min (static_cast<int> (rp), size - 1);
            const float frac = rp - static_cast<float> (i0);
            const int i1 = i0 + 1 >= size ? 0 : i0 + 1;
            const auto& line = delayLine[ch];
            delayed[ch] = line[static_cast<size_t> (i0)] + frac * (line[static_cast<size_t> (i1)] - line[static_cast<size_t> (i0)]);
            delayTone[ch] = flush (delayTone[ch] + toneCoef * (delayed[ch] - delayTone[ch]));
        }

        const float inL = io[0][i], inR = io[1][i];
        auto repeat = [mode] (float x) { return mode == 2 ? std::tanh (1.3f * x) / 1.3f : x; }; // tape saturates its repeats
        if (mode == 1)
        {
            // Ping-pong: the input (in mono) goes left, each repeat crosses to the other side.
            delayLine[0][static_cast<size_t> (delayWrite)] = flush (0.5f * (inL + inR) + repeat (delayTone[1] * feedback));
            delayLine[1][static_cast<size_t> (delayWrite)] = flush (repeat (delayTone[0] * feedback));
        }
        else
        {
            delayLine[0][static_cast<size_t> (delayWrite)] = flush (inL + repeat (delayTone[0] * feedback));
            delayLine[1][static_cast<size_t> (delayWrite)] = flush (inR + repeat (delayTone[1] * feedback));
        }
        io[0][i] = inL * dry + delayed[0] * mix;
        io[1][i] = inR * dry + delayed[1] * mix;

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

// ------------------------------------------------------------------------- overdrive

void EffectUnit::runOverdrive (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    // Color tilts the input (below 0.5: darker, above: brighter) before the curve; the curve is an asymmetric tanh run at 4x the
    // sample rate between two anti-aliasing low-pass filters (12th order, 18 kHz at 48 kHz); Tone is a low-pass after it (1 - 20 kHz), then a DC blocker.
    const float gain = dbToGain (s.params[0] * 40.0f) * (effectChoice (EffectType::Overdrive, 1, s.params[1]) == 1 ? 10.0f : 1.0f);
    const float color = s.params[2] * 2.0f;
    const float colorCoef = static_cast<float> (1.0 - std::exp (-2.0 * kPi * 700.0 / sampleRate));
    const float toneCoef = static_cast<float> (1.0 - std::exp (-2.0 * kPi * (1000.0 * std::pow (20.0, static_cast<double> (s.params[3]))) / sampleRate));
    const float bias = 0.6f * s.params[4];
    const float biasOffset = std::tanh (bias);
    const float mix = s.params[5];
    const float out = dbToGain (s.params[6]);
    float* io[2] = { l, r };

    for (size_t ch = 0; ch < 2; ++ch)
    {
        float* x = io[ch];
        for (int i = 0; i < n; ++i)
        {
            const float dry = x[i];
            colorLow[ch] = flush (colorLow[ch] + colorCoef * (dry - colorLow[ch]));
            const float shaped = colorLow[ch] + (dry - colorLow[ch]) * color;

            // Up-sample by zero stuffing (x4 keeps the level), filter, bend, filter, keep every fourth sample.
            float y = 0.0f;
            for (int k = 0; k < kOversample; ++k)
            {
                const double u0 = k == 0 ? static_cast<double> (shaped) * kOversample : 0.0;
                double u = u0;
                for (auto& q : upFilter)
                    u = ch == 0 ? q.tickL (u) : q.tickR (u);
                // First-order antiderivative anti-aliasing of tanh (F = log cosh): the curve's own aliasing, which oversampling alone
                // leaves when the drive turns a sine into a square, drops by a further 1/n per harmonic.
                const double v = static_cast<double> (gain) * u + bias;
                const double vPrev = adaaLast[ch];
                adaaLast[ch] = v;
                const auto logCosh = [] (double z) { const double a = std::abs (z); return a + std::log1p (std::exp (-2.0 * a)) - 0.6931471805599453; };
                double d = (std::abs (v - vPrev) < 1.0e-6 ? std::tanh (0.5 * (v + vPrev)) : (logCosh (v) - logCosh (vPrev)) / (v - vPrev)) - biasOffset;
                for (auto& q : downFilter)
                    d = ch == 0 ? q.tickL (d) : q.tickR (d);
                if (k == kOversample - 1)
                    y = static_cast<float> (d);
            }
            driveLast[ch] = y;

            // DC blocker (the asymmetric curve adds an offset), then the tone filter.
            const float blocked = y - dcX[ch] + 0.995f * dcY[ch];
            dcX[ch] = y;
            dcY[ch] = flush (blocked);
            toneState[ch] = flush (toneState[ch] + toneCoef * (blocked - toneState[ch]));
            x[i] = (dry + mix * (toneState[ch] - dry)) * out;
        }
    }
}

// ------------------------------------------------------------------------- chorus

void EffectUnit::runChorus (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    const int size = static_cast<int> (delayLine[0].size());
    const double step = static_cast<double> (s.params[0]) / sampleRate;
    const float depth = static_cast<float> (s.params[1] * 0.001 * sampleRate);
    const float base = static_cast<float> (s.params[2] * 0.001 * sampleRate);
    const float feedback = s.params[3];
    const int voices = effectChoice (EffectType::Chorus, 4, s.params[4]);
    const float width = s.params[5];
    const float mix = s.params[6];
    float* io[2] = { l, r };

    for (int i = 0; i < n; ++i)
    {
        lfoPhase += step;
        if (lfoPhase >= 1.0)
            lfoPhase -= 1.0;
        for (size_t ch = 0; ch < 2; ++ch)
        {
            float wet = 0.0f, first = 0.0f;
            for (int v = 0; v < voices; ++v)
            {
                const double phase = lfoPhase + static_cast<double> (v) / voices + (ch == 1 ? 0.25 * width : 0.0);
                const float d = base + depth * 0.5f * (1.0f + static_cast<float> (std::sin (2.0 * kPi * phase)));
                float rp = static_cast<float> (delayWrite) - d;
                while (rp < 0.0f)
                    rp += static_cast<float> (size);
                const int i0 = std::min (static_cast<int> (rp), size - 1);
                const float frac = rp - static_cast<float> (i0);
                const int i1 = i0 + 1 >= size ? 0 : i0 + 1;
                const auto& line = delayLine[ch];
                const float tap = line[static_cast<size_t> (i0)] + frac * (line[static_cast<size_t> (i1)] - line[static_cast<size_t> (i0)]);
                wet += tap;
                if (v == 0)
                    first = tap;
            }
            wet /= static_cast<float> (voices);
            const float in = io[ch][i];
            delayLine[ch][static_cast<size_t> (delayWrite)] = flush (in + feedback * first);
            io[ch][i] = in * (1.0f - mix) + wet * mix;
        }
        if (++delayWrite >= size)
            delayWrite = 0;
    }
}

// ------------------------------------------------------------------------- phaser

void EffectUnit::runPhaser (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    static constexpr int kStages[] = { 4, 6, 8, 12 };
    const int stages = kStages[static_cast<size_t> (effectChoice (EffectType::Phaser, 4, s.params[4]))];
    const double step = static_cast<double> (s.params[0]) / sampleRate;
    const float depth = s.params[1];
    const double centre = s.params[2];
    const float feedback = s.params[3];
    const float width = s.params[5];
    const float mix = s.params[6];
    float* io[2] = { l, r };

    for (int i = 0; i < n; ++i)
    {
        lfoPhase += step;
        if (lfoPhase >= 1.0)
            lfoPhase -= 1.0;
        if (--controlLeft <= 0)
        {
            controlLeft = 16;
            for (size_t ch = 0; ch < 2; ++ch)
            {
                const double sweep = std::sin (2.0 * kPi * (lfoPhase + (ch == 1 ? 0.5 * width : 0.0)));
                const double hz = std::clamp (centre * std::pow (2.0, 2.5 * depth * sweep), 20.0, 0.45 * sampleRate);
                const double t = std::tan (kPi * hz / sampleRate);
                phaserCoef[ch] = static_cast<float> ((t - 1.0) / (t + 1.0));
            }
        }
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const float in = io[ch][i];
            float x = in + feedback * phaserLast[ch];
            const float a = phaserCoef[ch];
            for (int st = 0; st < stages; ++st)
            {
                auto& z = phaserState[ch][static_cast<size_t> (st)];
                const float y = a * x + z;
                z = flush (x - a * y);
                x = y;
            }
            phaserLast[ch] = flush (std::clamp (x, -4.0f, 4.0f));
            io[ch][i] = in * (1.0f - mix) + x * mix;
        }
    }
}

// ------------------------------------------------------------------------- filter

void EffectUnit::runFilter (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    const int type = effectChoice (EffectType::Filter, 0, s.params[0]);
    const bool steep = effectChoice (EffectType::Filter, 3, s.params[3]) == 1;
    const float drive = s.params[4];
    const float driveGain = 1.0f + 9.0f * drive;
    const float driveNorm = drive > 0.0f ? 1.0f / std::tanh (driveGain) : 1.0f;
    const double step = static_cast<double> (s.params[5]) / sampleRate;
    const float mix = s.params[7];
    float* io[2] = { l, r };

    for (int i = 0; i < n; ++i)
    {
        if (s.params[5] > 0.0f)
        {
            lfoPhase += step;
            if (lfoPhase >= 1.0)
                lfoPhase -= 1.0;
        }
        if (--controlLeft <= 0)
        {
            controlLeft = 16;
            const double lfo = s.params[5] > 0.0f ? std::sin (2.0 * kPi * lfoPhase) * 3.0 * s.params[6] : 0.0; // up to +-3 octaves
            const double hz = std::clamp (static_cast<double> (s.params[1]) * std::pow (2.0, lfo), 20.0, 0.45 * sampleRate);
            svfG = static_cast<float> (std::tan (kPi * hz / sampleRate));
            svfK = 2.0f - 1.96f * s.params[2] * (steep ? 0.85f : 1.0f);
        }
        const float g = svfG, k = svfK;
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const auto svf = [&] (float in, float& ic1, float& ic2)
        {
            const float v3 = in - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = flush (2.0f * v1 - ic1);
            ic2 = flush (2.0f * v2 - ic2);
            switch (type)
            {
                case 1:  return in - k * v1 - v2; // high-pass
                case 2:  return v1;               // band-pass
                case 3:  return in - k * v1;      // notch
                default: return v2;               // low-pass
            }
        };
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const float dry = io[ch][i];
            float x = drive > 0.0f ? std::tanh (dry * driveGain) * driveNorm : dry;
            x = svf (x, svf1a[ch], svf1b[ch]);
            if (steep)
                x = svf (x, svf2a[ch], svf2b[ch]);
            io[ch][i] = dry + mix * (x - dry);
        }
    }
}

// ------------------------------------------------------------------------- maximizer

void EffectUnit::runMaximizer (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    // Three stages after the input gain: an optional soft clipper, a gentle "glue" compressor with a soft knee, and a look-ahead
    // brick-wall limiter that holds the peaks at the ceiling; a last clamp makes sure no sample goes over it.
    const float input = dbToGain (s.params[0]);
    const float ceiling = dbToGain (s.params[1]);
    const float release = static_cast<float> (1.0 - std::exp (-1.0 / (s.params[2] * 0.001 * sampleRate)));
    const float glue = s.params[3];
    const float softClip = s.params[4];
    const float glueAttack = static_cast<float> (1.0 - std::exp (-1.0 / (0.010 * sampleRate)));
    const float glueRelease = static_cast<float> (1.0 - std::exp (-1.0 / (0.150 * sampleRate)));
    const float smooth = static_cast<float> (1.0 - std::exp (-1.0 / (0.25 * lookahead)));
    const float thresholdDb = s.params[1] - 6.0f;
    const float ratio = 1.0f + 3.0f * glue;
    constexpr float kKnee = 6.0f;
    const int size = static_cast<int> (lookLine[0].size());

    for (int i = 0; i < n; ++i)
    {
        float xl = l[i] * input, xr = r[i] * input;
        if (softClip > 0.0f)
        {
            const float c = ceiling * 1.25f;
            xl += softClip * (c * std::tanh (xl / c) - xl);
            xr += softClip * (c * std::tanh (xr / c) - xr);
        }
        if (glue > 0.0f)
        {
            const float level = std::max (std::fabs (xl), std::fabs (xr));
            env += (level - env) * (level > env ? glueAttack : glueRelease);
            env = flush (env);
            const float over = 20.0f * std::log10 (std::max (env, 1.0e-6f)) - thresholdDb;
            float reduction = 0.0f;
            if (over > kKnee * 0.5f)
                reduction = over * (1.0f - 1.0f / ratio);
            else if (over > -kKnee * 0.5f)
                reduction = (1.0f - 1.0f / ratio) * (over + kKnee * 0.5f) * (over + kKnee * 0.5f) / (2.0f * kKnee);
            const float g = dbToGain (-reduction);
            xl *= g;
            xr *= g;
        }

        // The gain needed for the sample entering the look-ahead line drops at once and recovers with the release; it is smoothed
        // a little and applied to the sample leaving the line, so the drop starts before the peak gets out.
        const float peak = std::max (std::fabs (xl), std::fabs (xr));
        const float needed = peak > ceiling ? ceiling / peak : 1.0f;
        limiterTarget = needed < limiterTarget ? needed : flush (limiterTarget + (1.0f - limiterTarget) * release);
        limiterGain += (limiterTarget - limiterGain) * smooth; // reaches the drop within the look-ahead

        lookLine[0][static_cast<size_t> (lookWrite)] = xl;
        lookLine[1][static_cast<size_t> (lookWrite)] = xr;
        int readAt = lookWrite - lookahead;
        if (readAt < 0)
            readAt += size;
        if (++lookWrite >= size)
            lookWrite = 0;
        const float gain = std::min (limiterGain, 1.0f);
        l[i] = std::clamp (lookLine[0][static_cast<size_t> (readAt)] * gain, -ceiling, ceiling);
        r[i] = std::clamp (lookLine[1][static_cast<size_t> (readAt)] * gain, -ceiling, ceiling);
    }
}

// ------------------------------------------------------------------------- transient

void EffectUnit::runTransient (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    // Two followers of the level, one fast and one slow: where the fast one is above the slow one the sound is a transient (an
    // attack), where it is below it is sustain. Each part gets its own gain, or one of them is heard alone (Listen).
    const float speed = s.params[2];
    const auto coef = [this] (double ms) { return static_cast<float> (1.0 - std::exp (-1.0 / (ms * 0.001 * sampleRate))); };
    const float fastAttack = coef (0.5), fastRelease = coef (5.0 + 45.0 * speed);
    const float slowAttack = coef (10.0 + 40.0 * speed), slowRelease = coef (100.0 + 300.0 * speed);
    const float attackDb = s.params[0], sustainDb = s.params[1];
    const int listen = effectChoice (EffectType::Transient, 3, s.params[3]);
    const float mix = s.params[4];
    const float out = dbToGain (s.params[5]);

    for (int i = 0; i < n; ++i)
    {
        const float level = std::max (std::fabs (l[i]), std::fabs (r[i]));
        env += (level - env) * (level > env ? fastAttack : fastRelease);
        env2 += (level - env2) * (level > env2 ? slowAttack : slowRelease);
        env = flush (env);
        env2 = flush (env2);
        const float diffDb = 20.0f * std::log10 ((env + 1.0e-6f) / (env2 + 1.0e-6f));
        const float transient = std::clamp (diffDb / 6.0f, 0.0f, 1.0f);
        const float sustain = std::clamp (-diffDb / 6.0f, 0.0f, 1.0f);
        float g = 1.0f;
        if (listen == 1)
            g = transient;
        else if (listen == 2)
            g = 1.0f - transient;
        else
            g = dbToGain (attackDb * transient + sustainDb * sustain);
        const float wet = 1.0f + mix * (g - 1.0f);
        l[i] *= wet * out;
        r[i] *= wet * out;
    }
}

// ------------------------------------------------------------------------- stereo imager

void EffectUnit::runImager (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    const float width = s.params[0] / 100.0f;
    const float midGain = dbToGain (s.params[1]);
    const float sideGain = dbToGain (s.params[2]) * width;
    const float monoHz = s.params[3];
    const float lowCoef = monoHz > 0.0f ? static_cast<float> (1.0 - std::exp (-2.0 * kPi * monoHz / sampleRate)) : 0.0f;
    const float balance = s.params[4];
    const float gainL = balance > 0.0f ? 1.0f - balance : 1.0f;
    const float gainR = balance < 0.0f ? 1.0f + balance : 1.0f;

    for (int i = 0; i < n; ++i)
    {
        const float m = 0.5f * (l[i] + r[i]);
        float side = 0.5f * (l[i] - r[i]);
        if (monoHz > 0.0f)
        {
            sideLow[0] = flush (sideLow[0] + lowCoef * (side - sideLow[0]));
            side -= sideLow[0]; // the bass leaves the sides: below the frequency the sound is mono
        }
        const float mm = m * midGain, ss = side * sideGain;
        l[i] = (mm + ss) * gainL;
        r[i] = (mm - ss) * gainR;
    }
}

// ------------------------------------------------------------------------- presets

const std::vector<EffectPreset>& effectPresets (EffectType t)
{
    static const std::vector<EffectPreset> empty;
    const auto make = [t] (const char* name, std::initializer_list<float> values)
    {
        EffectPreset p { name, EffectSlot::make (t).params };
        size_t i = 0;
        for (const float v : values)
            if (i < p.params.size())
                p.params[i++] = v;
        return p;
    };
    switch (t)
    {
        case EffectType::Reverb:
        {
            static const std::vector<EffectPreset> list {
                make ("Hall", { 0.75f, 0.45f, 1.0f, 0.3f, 0.0f, 20.0f }),
                make ("Large hall", { 0.92f, 0.35f, 1.0f, 0.35f, 0.0f, 40.0f }),
                make ("Room", { 0.5f, 0.6f, 0.8f, 0.25f, 1.0f, 5.0f }),
                make ("Small room", { 0.25f, 0.7f, 0.6f, 0.2f, 1.0f, 0.0f }),
                make ("Plate", { 0.6f, 0.2f, 1.0f, 0.3f, 2.0f, 10.0f }),
                make ("Vocal plate", { 0.45f, 0.3f, 0.9f, 0.22f, 2.0f, 60.0f }),
            };
            return list;
        }
        case EffectType::Delay:
        {
            static const std::vector<EffectPreset> list {
                make ("Stereo 1/8 (120 BPM)", { 250.0f, 0.35f, 0.7f, 0.3f, 0.0f, 0.5f, 0.0f }),
                make ("Ping-pong 1/4 (120 BPM)", { 500.0f, 0.5f, 0.65f, 0.35f, 1.0f, 0.0f, 0.0f }),
                make ("Tape echo", { 340.0f, 0.55f, 0.5f, 0.35f, 2.0f, 0.0f, 0.35f }),
                make ("Slapback", { 90.0f, 0.1f, 0.8f, 0.3f, 0.0f, 0.1f, 0.0f }),
                make ("Dub space", { 375.0f, 0.75f, 0.35f, 0.4f, 2.0f, 0.2f, 0.6f }),
            };
            return list;
        }
        case EffectType::Overdrive:
        {
            static const std::vector<EffectPreset> list {
                make ("Warm", { 0.25f, 0.0f, 0.45f, 0.5f, 0.15f, 1.0f, -3.0f }),
                make ("Crunch", { 0.55f, 0.0f, 0.6f, 0.65f, 0.25f, 1.0f, -8.0f }),
                make ("Fuzz", { 0.8f, 1.0f, 0.55f, 0.55f, 0.6f, 1.0f, -14.0f }),
                make ("Parallel grit", { 0.6f, 0.0f, 0.7f, 0.7f, 0.3f, 0.35f, -2.0f }),
            };
            return list;
        }
        case EffectType::Chorus:
        {
            static const std::vector<EffectPreset> list {
                make ("Subtle", { 0.5f, 2.0f, 12.0f, 0.0f, 1.0f, 0.8f, 0.3f }),
                make ("Wide", { 0.8f, 4.0f, 18.0f, 0.1f, 2.0f, 1.0f, 0.5f }),
                make ("Ensemble", { 1.6f, 6.0f, 20.0f, 0.0f, 3.0f, 1.0f, 0.6f }),
                make ("Flanger", { 0.25f, 3.0f, 5.0f, 0.75f, 1.0f, 0.5f, 0.5f }),
            };
            return list;
        }
        case EffectType::Phaser:
        {
            static const std::vector<EffectPreset> list {
                make ("Slow sweep", { 0.2f, 0.9f, 700.0f, 0.5f, 1.0f, 0.5f, 0.5f }),
                make ("Fast", { 3.0f, 0.6f, 1200.0f, 0.3f, 0.0f, 0.3f, 0.5f }),
                make ("Deep 12", { 0.4f, 1.0f, 600.0f, 0.75f, 3.0f, 0.7f, 0.5f }),
            };
            return list;
        }
        case EffectType::Filter:
        {
            static const std::vector<EffectPreset> list {
                make ("Low-pass 24 dB", { 0.0f, 800.0f, 0.3f, 1.0f, 0.0f, 0.0f, 0.5f, 1.0f }),
                make ("High-pass", { 1.0f, 300.0f, 0.2f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f }),
                make ("Wobble", { 0.0f, 600.0f, 0.6f, 1.0f, 0.2f, 4.0f, 0.6f, 1.0f }),
                make ("Telephone", { 2.0f, 1500.0f, 0.4f, 1.0f, 0.3f, 0.0f, 0.5f, 1.0f }),
            };
            return list;
        }
        case EffectType::ParametricEq:
        {
            static const std::vector<EffectPreset> list {
                make ("Vocal presence", { 120.0f, -3.0f, 3000.0f, 3.0f, 1.0f, 10000.0f, 2.0f, 0.0f }),
                make ("Kick punch", { 60.0f, 4.0f, 350.0f, -4.0f, 1.4f, 5000.0f, 2.0f, 0.0f }),
                make ("Warm master", { 100.0f, 1.5f, 2500.0f, -1.0f, 0.7f, 12000.0f, 1.0f, 0.0f }),
            };
            return list;
        }
        case EffectType::Maximizer:
        {
            static const std::vector<EffectPreset> list {
                make ("Transparent", { 3.0f, -1.0f, 150.0f, 0.2f, 0.0f }),
                make ("Loud", { 8.0f, -0.5f, 60.0f, 0.5f, 0.4f }),
                make ("Streaming (-1 dB)", { 4.0f, -1.0f, 100.0f, 0.3f, 0.1f }),
            };
            return list;
        }
        case EffectType::Transient:
        {
            static const std::vector<EffectPreset> list {
                make ("Punchy drums", { 6.0f, -2.0f, 0.3f, 0.0f, 1.0f, -1.0f }),
                make ("Tighten", { 2.0f, -8.0f, 0.4f, 0.0f, 1.0f, 0.0f }),
                make ("More room", { -2.0f, 6.0f, 0.6f, 0.0f, 1.0f, -2.0f }),
                make ("Transients only", { 0.0f, 0.0f, 0.3f, 1.0f, 1.0f, 0.0f }),
            };
            return list;
        }
        case EffectType::TimeShaper:
        {
            // Time, Volume, Cycle (0 beat, 1 two beats, 2 bar, 3 two bars), Depth, Smooth, Mix
            static const std::vector<EffectPreset> list {
                make ("Half-time bar", { 1.0f, 0.0f, 2.0f, 100.0f, 3.0f, 100.0f }),
                make ("Reverse every bar", { 2.0f, 0.0f, 2.0f, 100.0f, 4.0f, 100.0f }),
                make ("Tape stop", { 3.0f, 0.0f, 2.0f, 100.0f, 3.0f, 100.0f }),
                make ("Stutter 1/16", { 6.0f, 0.0f, 0.0f, 100.0f, 2.0f, 100.0f }),
                make ("Beat repeat", { 7.0f, 0.0f, 2.0f, 100.0f, 3.0f, 100.0f }),
                make ("Scratch", { 8.0f, 0.0f, 0.0f, 100.0f, 2.0f, 100.0f }),
                make ("Trance gate", { 0.0f, 4.0f, 2.0f, 100.0f, 2.0f, 100.0f }),
                make ("Gate 1/16", { 0.0f, 2.0f, 2.0f, 100.0f, 1.5f, 100.0f }),
                make ("Sidechain pump", { 0.0f, 5.0f, 2.0f, 85.0f, 2.0f, 100.0f }),
                make ("Half-time gated", { 1.0f, 2.0f, 2.0f, 60.0f, 2.0f, 100.0f }),
            };
            return list;
        }
        case EffectType::PitchCorrector:
        {
            // Key, Scale (1 major, 2 natural minor, 11 minor pentatonic), Speed, Amount, Shift, Range (0 low, 1 voice, 2 high), Mix
            static const std::vector<EffectPreset> list {
                make ("Natural (chromatic)", { 0.0f, 0.0f, 60.0f, 80.0f, 0.0f, 1.0f, 100.0f }),
                make ("Hard tune (chromatic)", { 0.0f, 0.0f, 0.0f, 100.0f, 0.0f, 1.0f, 100.0f }),
                make ("C major, fast", { 0.0f, 1.0f, 10.0f, 100.0f, 0.0f, 1.0f, 100.0f }),
                make ("A minor, natural", { 9.0f, 2.0f, 50.0f, 90.0f, 0.0f, 1.0f, 100.0f }),
                make ("A minor pentatonic, hard", { 9.0f, 11.0f, 0.0f, 100.0f, 0.0f, 1.0f, 100.0f }),
                make ("Bass, chromatic", { 0.0f, 0.0f, 30.0f, 100.0f, 0.0f, 0.0f, 100.0f }),
                make ("Octave up", { 0.0f, 0.0f, 20.0f, 0.0f, 12.0f, 1.0f, 100.0f }),
                make ("Fifth harmony", { 0.0f, 0.0f, 20.0f, 0.0f, 7.0f, 1.0f, 50.0f }),
            };
            return list;
        }
        case EffectType::Imager:
        {
            static const std::vector<EffectPreset> list {
                make ("Wider", { 150.0f, 0.0f, 0.0f, 120.0f, 0.0f }),
                make ("Mono bass", { 100.0f, 0.0f, 0.0f, 150.0f, 0.0f }),
                make ("Mono", { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }),
            };
            return list;
        }
        case EffectType::Compressor:
        {
            static const std::vector<EffectPreset> list {
                make ("Gentle", { -18.0f, 2.0f, 20.0f, 200.0f, 2.0f }),
                make ("Drum bus", { -20.0f, 4.0f, 10.0f, 120.0f, 4.0f }),
                make ("Vocal", { -24.0f, 3.0f, 5.0f, 150.0f, 6.0f }),
            };
            return list;
        }
        case EffectType::Equalizer:
        case EffectType::Plugin:
        case EffectType::None:
            break;
    }
    return empty;
}

// ------------------------------------------------------------------------- time shaper (0.25.0)
// Every sample goes into a recording of the last few seconds; the output reads it `lag` samples back, where the lag follows the
// time pattern over a cycle of the song (1 beat to 2 bars): half speed reads at half the speed from the start of the cycle, reverse
// reads the cycle before backwards, tape stop slows to a halt, stutter repeats the cycle's first slice. A jump of the lag (a new
// cycle, a new slice) is crossfaded over the Smooth time. The volume pattern (gates, pump, fades) is smoothed by the same time.
void EffectUnit::runTimeShaper (const EffectSlot& slot, float* left, float* right, int n, const EffectTransport& transport) noexcept
{
    const auto& p = slot.params;
    const int timeMode = timeL != nullptr ? effectChoice (EffectType::TimeShaper, 0, p[0]) : 0;
    const int volumeMode = effectChoice (EffectType::TimeShaper, 1, p[1]);
    constexpr double cycleBeatsOf[] = { 1.0, 2.0, 4.0, 8.0 };
    const double cycleBeats = cycleBeatsOf[effectChoice (EffectType::TimeShaper, 2, p[2])];
    const float depth = p[3] / 100.0f;
    const double smoothSamples = std::max (1.0, static_cast<double> (p[4]) * 0.001 * sampleRate);
    const float mix = p[5] / 100.0f;
    const double ticksPerSample = transport.ticksPerSample > 0.0 ? transport.ticksPerSample : 120.0 * 480.0 / (60.0 * sampleRate);
    const double samplesPerBeat = 480.0 / ticksPerSample;
    const double cycle = cycleBeats * samplesPerBeat;
    const bool following = transport.tick >= 0.0;
    const auto volumeCoef = static_cast<float> (1.0 - std::exp (-1.0 / smoothSamples));
    fadeLength = static_cast<int> (smoothSamples);
    constexpr bool trance[16] = { true, false, true, true, false, true, true, false, true, true, false, true, true, false, true, true };

    for (int i = 0; i < n; ++i)
    {
        const double tick = following ? transport.tick + i * ticksPerSample : freeTick + i * ticksPerSample;
        const double beats = tick / 480.0;
        const double inCycle = beats / cycleBeats;
        const double phase = inCycle - std::floor (inCycle); // 0..1 through the cycle
        const float dryL = left[i], dryR = right[i];
        float wetL = dryL, wetR = dryR;

        if (timeMode != 0)
        {
            timeL[timeWrite] = dryL;
            timeR[timeWrite] = dryR;
            const double done = phase * cycle; // samples since the cycle began
            double lag = 0.0;
            switch (timeMode)
            {
                case 1: lag = 0.5 * done; break;                                   // half speed
                case 2: lag = 2.0 * done; break;                                   // reverse (the cycle before, backwards)
                case 3: lag = 0.5 * done * phase; break;                           // tape stop: speed 1 - phase
                case 4: lag = 0.25 * done * phase; break;                          // slow down to half speed
                case 5: lag = done - std::fmod (done, 0.5 * samplesPerBeat); break;  // stutter 1/8
                case 6: lag = done - std::fmod (done, 0.25 * samplesPerBeat); break; // stutter 1/16
                case 7: lag = done - std::fmod (done, 0.25 * cycle); break;          // repeat the first quarter of the cycle
                case 8: lag = 0.125 * samplesPerBeat * (1.0 - std::cos (8.0 * 3.14159265358979 * phase)); break; // scratch back and forth
                default: break;
            }
            lag = std::clamp (lag, 0.0, static_cast<double> (timeFrames - 4));
            if (std::abs (lag - lastLag) > 8.0) // a jump: fade the old stream (read at normal speed) out
            {
                fadeLag = lastLag;
                fadeLeft = fadeLength;
            }
            lastLag = lag;
            const auto read = [this] (const float* buf, double back) noexcept
            {
                double pos = static_cast<double> (timeWrite) - back;
                if (pos < 0.0)
                    pos += timeFrames;
                const auto i0 = static_cast<int> (pos);
                const auto frac = static_cast<float> (pos - i0);
                const int i1 = i0 + 1 >= timeFrames ? 0 : i0 + 1;
                return buf[i0] + frac * (buf[i1] - buf[i0]);
            };
            wetL = read (timeL, lag);
            wetR = read (timeR, lag);
            if (fadeLeft > 0)
            {
                const float g = static_cast<float> (fadeLeft) / static_cast<float> (fadeLength);
                wetL = (1.0f - g) * wetL + g * read (timeL, fadeLag);
                wetR = (1.0f - g) * wetR + g * read (timeR, fadeLag);
                --fadeLeft;
            }
            if (++timeWrite >= timeFrames)
                timeWrite = 0;
            if (timeMode == 3) // a stopping tape also fades
            {
                const auto fade = static_cast<float> (1.0 - phase * phase);
                wetL *= fade;
                wetR *= fade;
            }
        }

        float target = 1.0f;
        const auto fraction = [] (double x) { return x - std::floor (x); };
        switch (volumeMode)
        {
            case 1: target = fraction (beats * 2.0) < 0.5 ? 1.0f : 0.0f; break;
            case 2: target = fraction (beats * 4.0) < 0.5 ? 1.0f : 0.0f; break;
            case 3: target = fraction (beats * 8.0) < 0.5 ? 1.0f : 0.0f; break;
            case 4: target = trance[static_cast<int> (std::floor (beats * 4.0)) & 15] && fraction (beats * 4.0) < 0.75 ? 1.0f : 0.0f; break;
            case 5: target = static_cast<float> (std::min (1.0, std::pow (fraction (beats) / 0.45, 1.5))); break; // sidechain-like pump
            case 6: target = static_cast<float> (phase); break;
            case 7: target = static_cast<float> (1.0 - phase); break;
            default: break;
        }
        volume += (target - volume) * volumeCoef;
        const float v = 1.0f - depth * (1.0f - volume);
        left[i] = dryL * (1.0f - mix) + wetL * v * mix;
        right[i] = dryR * (1.0f - mix) + wetR * v * mix;
    }
    if (! following)
        freeTick += n * ticksPerSample;
    else
        freeTick = transport.tick + n * ticksPerSample;
}
// ------------------------------------------------------------------------- pitch corrector (0.26.0)
// The input's pitch is found every 5 ms with YIN (de Cheveigné and Kawahara, 2002) on a copy decimated to about 12 kHz, refined at
// the full rate; the nearest note of the scale (with a little hysteresis) gives the correction, which Speed smooths. The sound is
// then read from a recording by two heads that move at the corrected speed, each faded in and out with a Hann window; a head that
// ends its window starts again at the delay where the recording best matches what the other head is playing (normalised
// cross-correlation), so the crossfades join in phase (the way WSOLA does). Formants move with the pitch.
namespace
{
constexpr double kPitchMinDelay = 3.0; // samples: the cubic reading needs two samples after the read position
constexpr float kPitchRanges[3][2] = { { 40.0f, 400.0f }, { 70.0f, 900.0f }, { 150.0f, 2000.0f } };
constexpr float kPitchSilence = 0.003f; // RMS (-50 dBFS) below which there is no pitch to find
constexpr float kYinThreshold = 0.2f;
constexpr float kNoteHysteresis = 0.15f; // semitones: the current note is kept until another is this much nearer

inline float hermite (float xm1, float x0, float x1, float x2, float t) noexcept
{
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}
} // namespace

double EffectUnit::headPosition (double delay) const noexcept
{
    const double size = static_cast<double> (delayLine[0].size());
    double pos = static_cast<double> (delayWrite) - delay;
    while (pos < 0.0)
        pos += size;
    return pos;
}

void EffectUnit::analysePitch (int range, int key, int scale, float amount) noexcept
{
    const double decRate = sampleRate / pcFactor;
    const float minHz = kPitchRanges[range][0], maxHz = kPitchRanges[range][1];
    const int tauMax = std::min (kPitchDecimated / 2 - 2, static_cast<int> (std::ceil (decRate / minHz)) + 2);
    const int tauMin = std::max (2, static_cast<int> (std::floor (decRate / maxHz)) - 1);
    const int window = std::max (tauMax, 96);
    const int total = window + tauMax;
    // The pitch found describes the middle of the window, on average half a hop ago: the heads read the sound that far back, so
    // each correction meets the sound it was measured on.
    pcAlign = 0.5 * (window + kPitchHop) * pcFactor;

    // The latest `total` decimated samples, oldest first.
    for (int j = 0; j < total; ++j)
        pcScratch[static_cast<size_t> (j)] = pcDecim[static_cast<size_t> ((pcDecimWrite - total + j) & (kPitchDecimated - 1))];

    double energy = 0.0;
    for (int j = tauMax; j < total; ++j)
    {
        const double v = pcScratch[static_cast<size_t> (j)];
        energy += v * v;
    }
    int found = -1;
    if (std::sqrt (energy / window) >= kPitchSilence)
    {
        // The difference function and its cumulative mean normalised form, on the newest `window` samples.
        const float* x = pcScratch.data() + tauMax;
        double running = 0.0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double d = 0.0;
            for (int j = 0; j < window; ++j)
            {
                const double diff = static_cast<double> (x[j]) - x[j - tau];
                d += diff * diff;
            }
            running += d;
            pcDiff[static_cast<size_t> (tau)] = running > 0.0 ? static_cast<float> (d * tau / running) : 1.0f;
        }
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (pcDiff[static_cast<size_t> (tau)] < kYinThreshold)
            {
                for (int next = tau + 1; next < tauMax && pcDiff[static_cast<size_t> (next)] < pcDiff[static_cast<size_t> (tau)]; ++next)
                    tau = next;
                found = tau;
                break;
            }
    }

    double period = 0.0;
    if (found > 0)
    {
        // Refined at the full rate: the plain difference around the decimated lag, its minimum interpolated with a parabola.
        const int monoSize = static_cast<int> (preDelay.size());
        const int fullWindow = window * pcFactor;
        const int centre = found * pcFactor;
        const int lo = std::max (2, centre - pcFactor - 1), hi = centre + pcFactor + 1;
        auto at = [&] (int back) // `back` < monoSize samples before the newest
        {
            const int index = preDelayWrite - back;
            return preDelay[static_cast<size_t> (index < 0 ? index + monoSize : index)];
        };
        auto difference = [&] (int tau)
        {
            double d = 0.0;
            for (int j = 0; j < fullWindow; ++j)
            {
                const double diff = static_cast<double> (at (j)) - at (j + tau);
                d += diff * diff;
            }
            return d;
        };
        int best = centre;
        double bestValue = difference (centre);
        double prev = 0.0, next = 0.0;
        for (int tau = lo; tau <= hi; ++tau)
            if (tau != centre)
            {
                const double v = difference (tau);
                if (v < bestValue)
                {
                    bestValue = v;
                    best = tau;
                }
            }
        prev = difference (best - 1);
        next = difference (best + 1);
        const double curvature = prev - 2.0 * bestValue + next;
        const double shift = curvature > 0.0 ? std::clamp (0.5 * (prev - next) / curvature, -0.5, 0.5) : 0.0;
        period = best + shift;
        const double hz = sampleRate / period;
        if (hz < minHz * 0.97 || hz > maxHz * 1.03)
            period = 0.0;
    }

    pcPeriod = period;
    if (period <= 0.0)
    {
        pcNote = -1;
        pcTarget = 0.0f; // no pitch (silence, breath, consonants): leave the sound alone
        return;
    }

    const double midi = 69.0 + 12.0 * std::log2 (sampleRate / period / 440.0);
    const auto mask = effect_tables::kPitchScaleMasks[scale];
    auto inScale = [&] (int note) { return (mask & (1u << static_cast<unsigned> (((note - key) % 12 + 12) % 12))) != 0; };
    int nearest = -1;
    double nearestDistance = 1.0e9;
    const int base = static_cast<int> (std::floor (midi));
    for (int note = base - 6; note <= base + 7; ++note)
        if (inScale (note) && std::fabs (note - midi) < nearestDistance)
        {
            nearestDistance = std::fabs (note - midi);
            nearest = note;
        }
    if (pcNote >= 0 && inScale (pcNote) && std::fabs (pcNote - midi) < nearestDistance + kNoteHysteresis)
        nearest = pcNote;
    pcNote = nearest;
    pcTarget = static_cast<float> (amount * (nearest - midi));
}

void EffectUnit::rebirthHead (PitchHead& head, const PitchHead& other, double ratio) noexcept
{
    const int target = std::max (16, static_cast<int> (0.03 * sampleRate));
    const int remaining = std::max (1, other.life - other.age);
    head.life = std::clamp (2 * remaining, target / 2, target + target / 2);
    head.age = 0;

    // The delay changes by (1 - ratio) per sample: room is left for the whole window (with a margin for a changing ratio).
    const int monoSize = static_cast<int> (preDelay.size());
    const int window = std::clamp (pcPeriod > 0.0 ? static_cast<int> (pcPeriod) : 0, static_cast<int> (0.005 * sampleRate),
                                   static_cast<int> (0.02 * sampleRate));
    const double maxDelay = static_cast<double> (std::min (static_cast<int> (delayLine[0].size()), monoSize) - window - 8);
    const double drift = (1.0 - ratio) * head.life;
    const double hardLowest = kPitchMinDelay + 1.0 + std::max (0.0, -drift) * 1.25;
    const double highest = maxDelay - std::max (0.0, drift) * 1.25;
    const int span = pcPeriod > 0.0 ? static_cast<int> (std::ceil (pcPeriod)) + 2 : static_cast<int> (0.01 * sampleRate);
    // Over its window the head's delay averages pcAlign, unless that is too close to now for the drift.
    const double lowest = std::max (hardLowest, pcAlign - 0.5 * drift - 0.5 * span);

    // Candidates keep the other head's fraction of a sample, so a match is exact to the sample.
    const int firstLag = static_cast<int> (std::ceil (lowest - other.delay));
    const int finalLag = std::min (firstLag + span, static_cast<int> (std::floor (highest - other.delay)));
    if (finalLag < firstLag)
    {
        head.delay = std::clamp (lowest, kPitchMinDelay, std::max (kPitchMinDelay, maxDelay));
        return;
    }

    auto at = [&] (int back) // `back` < monoSize samples before the newest
        {
            const int index = preDelayWrite - back;
            return preDelay[static_cast<size_t> (index < 0 ? index + monoSize : index)];
        };
    const int otherBack = static_cast<int> (std::lround (other.delay));
    const int step = std::max (1, pcFactor / 2);
    auto score = [&] (int lag, int stride)
    {
        double ab = 0.0, aa = 0.0, bb = 0.0;
        for (int k = 0; k < window; k += stride)
        {
            const double a = at (otherBack + k), b = at (otherBack + lag + k);
            ab += a * b;
            aa += a * a;
            bb += b * b;
        }
        return ab / std::sqrt (aa * bb + 1.0e-12);
    };
    int bestLag = firstLag;
    double bestScore = -2.0;
    for (int lag = firstLag; lag <= finalLag; lag += step)
    {
        const double v = score (lag, step);
        if (v > bestScore + 1.0e-9)
        {
            bestScore = v;
            bestLag = lag;
        }
    }
    const int coarse = bestLag;
    bestScore = -2.0;
    for (int lag = std::max (firstLag, coarse - step); lag <= std::min (finalLag, coarse + step); ++lag)
    {
        const double v = score (lag, 1);
        if (v > bestScore + 1.0e-9)
        {
            bestScore = v;
            bestLag = lag;
        }
    }
    head.delay = other.delay + bestLag;
}

void EffectUnit::runPitchCorrector (const EffectSlot& slot, float* left, float* right, int n) noexcept
{
    const auto& p = slot.params;
    const int key = effectChoice (EffectType::PitchCorrector, 0, p[0]);
    const int scale = effectChoice (EffectType::PitchCorrector, 1, p[1]);
    const float speedMs = p[2];
    const float amount = p[3] / 100.0f;
    const float shift = p[4];
    const int range = effectChoice (EffectType::PitchCorrector, 5, p[5]);
    const float mix = p[6] / 100.0f;
    const float follow = speedMs <= 0.0f ? 1.0f : 1.0f - std::exp (-1000.0f / (speedMs * static_cast<float> (sampleRate)));

    const int lineSize = static_cast<int> (delayLine[0].size());
    const int monoSize = static_cast<int> (preDelay.size());
    const double maxDelay = static_cast<double> (std::min (lineSize, monoSize) - 8);
    auto& lineL = delayLine[0];
    auto& lineR = delayLine[1];

    for (int i = 0; i < n; ++i)
    {
        const float inL = left[i], inR = right[i];
        const float mono = 0.5f * (inL + inR);
        lineL[static_cast<size_t> (delayWrite)] = inL;
        lineR[static_cast<size_t> (delayWrite)] = inR;
        preDelay[static_cast<size_t> (preDelayWrite)] = mono;

        const auto filtered = static_cast<float> (pcAa2.tickL (pcAa1.tickL (mono)));
        if (++pcPhase >= pcFactor)
        {
            pcPhase = 0;
            pcDecim[static_cast<size_t> (pcDecimWrite)] = flush (filtered);
            pcDecimWrite = (pcDecimWrite + 1) & (kPitchDecimated - 1);
            if (--pcHopLeft <= 0)
            {
                pcHopLeft = kPitchHop;
                analysePitch (range, key, scale, amount);
            }
        }

        pcOffset += follow * (pcTarget - pcOffset);
        if (--pcControl <= 0)
        {
            pcControl = 16;
            pcRatio = std::exp2 (static_cast<double> (pcOffset + shift) / 12.0);
        }

        float wetL = 0.0f, wetR = 0.0f, gains = 0.0f;
        for (size_t h = 0; h < 2; ++h)
        {
            auto& head = pcHeads[h];
            const float phase = static_cast<float> (head.age) / static_cast<float> (head.life);
            const float s = std::sin (static_cast<float> (kPi) * phase);
            const float g = s * s;
            const double pos = headPosition (head.delay);
            const int i0 = static_cast<int> (pos);
            const auto t = static_cast<float> (pos - i0);
            auto idx = [&] (int k) { return static_cast<size_t> (((i0 + k) % lineSize + lineSize) % lineSize); };
            wetL += g * hermite (lineL[idx (-1)], lineL[idx (0)], lineL[idx (1)], lineL[idx (2)], t);
            wetR += g * hermite (lineR[idx (-1)], lineR[idx (0)], lineR[idx (1)], lineR[idx (2)], t);
            gains += g;

            head.delay = std::clamp (head.delay + (1.0 - pcRatio), kPitchMinDelay, maxDelay);
            if (++head.age >= head.life)
                rebirthHead (head, pcHeads[1 - h], pcRatio);
        }
        const float norm = 1.0f / std::max (gains, 0.25f);
        left[i] = inL + mix * (wetL * norm - inL);
        right[i] = inR + mix * (wetR * norm - inR);

        delayWrite = (delayWrite + 1) % lineSize;
        preDelayWrite = (preDelayWrite + 1) % monoSize;
    }
}
} // namespace sls
