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

// RBJ shelf and peak designs (the low-, high- and band-pass ones are in Biquad.h).
using Coefs = BiquadCoefs;

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

// The slot's mix knob (0.70.0): `run (left, right, count, samplesAlreadyDone)` works in place; below 1 the sound before it is kept
// and blended back in, 256 samples at a time.
template <typename Run>
inline void blended (float mix, float* left, float* right, int numSamples, Run&& run) noexcept
{
    if (mix >= 0.9995f)
    {
        run (left, right, numSamples, 0);
        return;
    }
    std::array<float, 256> dryL, dryR;
    for (int done = 0; done < numSamples; done += 256)
    {
        const int count = std::min (256, numSamples - done);
        float* l = left + done;
        float* r = right + done;
        std::copy_n (l, count, dryL.begin());
        std::copy_n (r, count, dryR.begin());
        run (l, r, count, done);
        for (int i = 0; i < count; ++i)
        {
            l[i] = dryL[static_cast<size_t> (i)] + mix * (l[i] - dryL[static_cast<size_t> (i)]);
            r[i] = dryR[static_cast<size_t> (i)] + mix * (r[i] - dryR[static_cast<size_t> (i)]);
        }
    }
}
} // namespace

void EffectMemory::prepare (double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    const double scale = rate / 44100.0;
    for (size_t ch = 0; ch < 2; ++ch)
    {
        const int spread = ch == 0 ? 0 : kStereoSpread;
        for (size_t i = 0; i < kCombs; ++i)
            combs[ch][i].buf.assign (static_cast<size_t> (std::max (8.0, (kCombTunings[i] + spread) * scale)), 0.0f);
        for (size_t i = 0; i < kAllpasses; ++i)
            allpasses[ch][i].buf.assign (static_cast<size_t> (std::max (8.0, (kAllpassTunings[i] + spread) * scale)), 0.0f);
        delayLine[ch].assign (static_cast<size_t> (rate * kMaxDelaySeconds) + 8, 0.0f);
    }
    preDelay.assign (static_cast<size_t> (rate * 0.26) + 4, 0.0f);
    clear();
}

void EffectMemory::clear() noexcept
{
    for (auto& channel : combs)
        for (auto& c : channel)
        {
            std::fill (c.buf.begin(), c.buf.end(), 0.0f);
            c.idx = 0;
            c.store = 0.0f;
            c.len = static_cast<int> (std::max<size_t> (1, c.buf.size()));
        }
    for (auto& channel : allpasses)
        for (auto& a : channel)
        {
            std::fill (a.buf.begin(), a.buf.end(), 0.0f);
            a.idx = 0;
            a.len = static_cast<int> (std::max<size_t> (1, a.buf.size()));
        }
    std::fill (preDelay.begin(), preDelay.end(), 0.0f);
    for (auto& d : delayLine)
        std::fill (d.begin(), d.end(), 0.0f);
    pcDecim.fill (0.0f);
    pcScratch.fill (0.0f);
    pcDiff.fill (0.0f);
    for (auto& stage : vocMod)
        for (auto& q : stage)
            q.clearState();
    for (auto& stage : vocCar)
        for (auto& q : stage)
            q.clearState();
    vocEnv.fill (0.0f);
}

void EffectUnit::attachMemory (EffectMemory* m) noexcept
{
    memory = m;
    reset(); // a fresh start with these buffers (and the reverb's line lengths set again for them)
}

void EffectUnit::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    memory = nullptr; // memory made for the old rate is not used again

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
        const Biquad q (BiquadCoefs::lowPass (2.0 * kPi * fc / osRate, stageQ));
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
            (stage == 0 ? pcAa1 : pcAa2) = Biquad (BiquadCoefs::lowPass (2.0 * kPi * cutoff / sampleRate, qs[stage]));
        }
    }

    active = EffectType::None;
    reverbType = -1;
    reset();
}

void EffectUnit::setReverbType (int type) noexcept
{
    if (type == reverbType || memory == nullptr)
        return;
    reverbType = type;
    auto& combs = memory->combs;
    auto& allpasses = memory->allpasses;
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
    if (memory != nullptr)
        memory->clear();
    reverbType = -1; // the line lengths are set again on the next block
    preDelayWrite = 0;
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
    for (auto& b : bands) // 0.61.0
        b.clearState();
    for (auto* pair : { &xLow, &xMidHp, &xMidLp, &xHigh })
        for (auto& b : *pair)
            b.clearState();
    bandEnv = {};
    gateHold = 0;
    gateGain = 0.0f;
    sqHold = {};
    sqCount = 0;
    panPhase = 0.0;
    control = 0.0f; // 0.62.0
    vocCached.fill (-1.0f);
    convolverSeen = nullptr; // the convolver it meets next starts silent
    for (auto& channel : hilbert) // 0.70.0
        for (auto& section : channel)
            section.fill (0.0f);
    hilbertHeld = {};
    shiftPhase = {};
    grainVoices = {};
    grainClock = 0;
    for (auto& c : vintageCoef)
        c.fill (0.0f);
    ctlStage = 0;
    ctlLevel = 0.0f;
    ctlTime = ctlBeat = 0.0;
    for (size_t v = 0; v < hyperPhase.size(); ++v) // 0.73.0
        hyperPhase[v] = static_cast<double> (v) / static_cast<double> (hyperPhase.size());
    scratchDelay = scratchVelocity = 0.0;
    scratchFader = 1.0f;
    if (timeL != nullptr)
    {
        std::fill_n (timeL, timeFrames, 0.0f);
        std::fill_n (timeR, timeFrames, 0.0f);
    }
    timeWrite = 0;
    lastLag = fadeLag = 0.0;
    fadeLeft = 0;
    volume = 1.0f;

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
        {
            // 0.85.0: a plugin with a sidechain input hears the track its slot names (params[0]: 1-based, 0 = none)
            const float* keyL = nullptr;
            const float* keyR = nullptr;
            if (requested.plugin->hasSidechain() && std::isfinite (requested.params[0]))
                if (const int key = static_cast<int> (std::lround (requested.params[0])) - 1;
                    key >= 0 && key < transport.numKeys && transport.keyLeft != nullptr && transport.keyRight != nullptr)
                {
                    keyL = transport.keyLeft[key];
                    keyR = transport.keyRight[key];
                }
            blended (requested.mix, left, right, numSamples, [&] (float* l, float* r, int count, int done) noexcept
            {
                if (keyL == nullptr)
                {
                    requested.plugin->process (l, r, count, nullptr, 0);
                    return;
                }
                PluginIo io;
                io.sideLeft = keyL + done;
                io.sideRight = keyR != nullptr ? keyR + done : keyL + done;
                requested.plugin->process (l, r, count, nullptr, 0, &io);
            });
        }
        return;
    }
    if (requested.type == EffectType::None) // an empty slot (most of them): no copy of the slot, nothing to do
    {
        if (active != EffectType::None)
        {
            active = EffectType::None;
            reset();
        }
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

    if (effectIsController (slot.type)) // a controller has nothing to mix
    {
        runSlot (slot, left, right, numSamples, transport);
        return;
    }
    blended (slot.mix, left, right, numSamples, [&] (float* l, float* r, int count, int done) noexcept
    {
        if (done == 0)
        {
            runSlot (slot, l, r, count, transport);
            return;
        }
        auto later = transport; // (the song has moved on by the samples already done)
        if (later.tick >= 0.0)
            later.tick += done * later.ticksPerSample;
        runSlot (slot, l, r, count, later);
    });
}

void EffectUnit::runSlot (const EffectSlot& slot, float* left, float* right, int numSamples, const EffectTransport& transport) noexcept
{
    switch (slot.type)
    {
        case EffectType::Equalizer:    runEqualizer (slot, left, right, numSamples); break;
        case EffectType::Reverb:       runReverb (slot, left, right, numSamples); break;
        case EffectType::Delay:        runDelay (slot, left, right, numSamples); break;
        case EffectType::Compressor:   runCompressor (slot, left, right, numSamples, transport); break;
        case EffectType::Overdrive:    runOverdrive (slot, left, right, numSamples); break;
        case EffectType::Chorus:       runChorus (slot, left, right, numSamples); break;
        case EffectType::Phaser:       runPhaser (slot, left, right, numSamples); break;
        case EffectType::Filter:       runFilter (slot, left, right, numSamples); break;
        case EffectType::ParametricEq: runParametricEq (slot, left, right, numSamples); break;
        case EffectType::Maximizer:    runMaximizer (slot, left, right, numSamples); break;
        case EffectType::Transient:    runTransient (slot, left, right, numSamples); break;
        case EffectType::Imager:       runImager (slot, left, right, numSamples); break;
        case EffectType::TimeShaper:   runTimeShaper (slot, left, right, numSamples, transport); break;
        case EffectType::PitchCorrector: runPitchCorrector (slot, left, right, numSamples, transport); break;
        case EffectType::Flanger:      runFlanger (slot, left, right, numSamples); break;
        case EffectType::Gate:         runGate (slot, left, right, numSamples, transport); break;
        case EffectType::BassBoost:    runBassBoost (slot, left, right, numSamples); break;
        case EffectType::Panomatic:    runPanomatic (slot, left, right, numSamples, transport); break;
        case EffectType::Squeeze:      runSqueeze (slot, left, right, numSamples); break;
        case EffectType::Vibrato:      runVibrato (slot, left, right, numSamples); break;
        case EffectType::ParametricEq2: runParametricEq2 (slot, left, right, numSamples); break;
        case EffectType::MultibandComp: runMultibandComp (slot, left, right, numSamples); break;
        case EffectType::WaveShaper:   runWaveShaper (slot, left, right, numSamples); break;
        case EffectType::Vocoder:      runVocoder (slot, left, right, numSamples, transport); break;
        case EffectType::PeakController: runPeakController (slot, left, right, numSamples); break;
        case EffectType::Convolver:    runConvolver (slot, left, right, numSamples); break;
        case EffectType::FrequencyShifter: runFrequencyShifter (slot, left, right, numSamples); break;
        case EffectType::VintageChorus: runVintageChorus (slot, left, right, numSamples); break;
        case EffectType::VintagePhaser: runVintagePhaser (slot, left, right, numSamples); break;
        case EffectType::Effector:     runEffector (slot, left, right, numSamples); break;
        case EffectType::Grains:       runGrains (slot, left, right, numSamples); break;
        case EffectType::EnvelopeController: runEnvelopeController (slot, left, right, numSamples); break;
        case EffectType::FormulaController: runFormulaController (slot, left, right, numSamples, transport); break;
        case EffectType::HyperChorus:  runHyperChorus (slot, left, right, numSamples); break;
        case EffectType::Scratcher:    runScratcher (slot, left, right, numSamples, transport); break;
        case EffectType::Patcher:      runPatcher (slot, left, right, numSamples, transport); break;
        case EffectType::Plugin:
        case EffectType::None:         break;
    }
}

// ------------------------------------------------------------------------- equalizer

namespace
{
void processStereo (sls::Biquad<2>& q, float* l, float* r, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        l[i] = static_cast<float> (q.tick (0, l[i]));
        r[i] = static_cast<float> (q.tick (1, r[i]));
    }
}
} // namespace

void EffectUnit::runEqualizer (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    if (! eqValid || s.params != eqCached)
    {
        eqCached = s.params;
        eqValid = true;
        low.setCoefs (shelfCoefs (true, 120.0, s.params[0], sampleRate));
        high.setCoefs (shelfCoefs (false, 6000.0, s.params[3], sampleRate));
        mid.setCoefs (peakCoefs (s.params[2], s.params[1], 0.8, sampleRate));
    }

    // A band at 0 dB is skipped entirely (and kept clean for when it is raised again).
    if (std::fabs (s.params[0]) >= 0.01f) processStereo (low, l, r, n); else low.clearState();
    if (std::fabs (s.params[1]) >= 0.01f) processStereo (mid, l, r, n); else mid.clearState();
    if (std::fabs (s.params[3]) >= 0.01f) processStereo (high, l, r, n); else high.clearState();
}

void EffectUnit::runParametricEq (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    if (! eqValid || s.params != eqCached)
    {
        eqCached = s.params;
        eqValid = true;
        low.setCoefs (shelfCoefs (true, s.params[0], s.params[1], sampleRate));
        mid.setCoefs (peakCoefs (s.params[2], s.params[3], s.params[4], sampleRate));
        high.setCoefs (shelfCoefs (false, s.params[5], s.params[6], sampleRate));
    }
    if (std::fabs (s.params[1]) >= 0.01f) processStereo (low, l, r, n); else low.clearState();
    if (std::fabs (s.params[3]) >= 0.01f) processStereo (mid, l, r, n); else mid.clearState();
    if (std::fabs (s.params[6]) >= 0.01f) processStereo (high, l, r, n); else high.clearState();
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
    if (memory == nullptr)
        return; // no buffers lent (yet): the sound passes unchanged
    auto& combs = memory->combs;
    auto& allpasses = memory->allpasses;
    auto& preDelay = memory->preDelay;
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
    if (memory == nullptr)
        return; // no buffers lent (yet): the sound passes unchanged
    auto& delayLine = memory->delayLine;
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

void EffectUnit::runCompressor (const EffectSlot& s, float* l, float* r, int n, const EffectTransport& transport) noexcept
{
    // Sidechain (0.45.0): the level is measured on another track's sound when one is chosen and it is there in this block.
    const int key = static_cast<int> (std::lround (s.params[5])) - 1;
    const float* keyL = nullptr;
    const float* keyR = nullptr;
    if (key >= 0 && key < transport.numKeys && transport.keyLeft != nullptr && transport.keyRight != nullptr)
    {
        keyL = transport.keyLeft[key];
        keyR = transport.keyRight[key];
    }
    const float* detectL = keyL != nullptr && keyR != nullptr ? keyL : l;
    const float* detectR = keyL != nullptr && keyR != nullptr ? keyR : r;
    const float threshold = s.params[0];
    const float slope = 1.0f - 1.0f / s.params[1];
    const float attack = static_cast<float> (1.0 - std::exp (-1.0 / (s.params[2] * 0.001 * sampleRate)));
    const float release = static_cast<float> (1.0 - std::exp (-1.0 / (s.params[3] * 0.001 * sampleRate)));
    const float makeup = s.params[4];
    constexpr float kLn10Over20 = 0.115129254649702f;

    for (int i = 0; i < n; ++i)
    {
        const float level = std::max (std::fabs (detectL[i]), std::fabs (detectR[i]));
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
                    u = ch == 0 ? q.tick (0, u) : q.tick (1, u);
                // First-order antiderivative anti-aliasing of tanh (F = log cosh): the curve's own aliasing, which oversampling alone
                // leaves when the drive turns a sine into a square, drops by a further 1/n per harmonic.
                const double v = static_cast<double> (gain) * u + bias;
                const double vPrev = adaaLast[ch];
                adaaLast[ch] = v;
                const auto logCosh = [] (double z) { const double a = std::abs (z); return a + std::log1p (std::exp (-2.0 * a)) - 0.6931471805599453; };
                double d = (std::abs (v - vPrev) < 1.0e-6 ? std::tanh (0.5 * (v + vPrev)) : (logCosh (v) - logCosh (vPrev)) / (v - vPrev)) - biasOffset;
                for (auto& q : downFilter)
                    d = ch == 0 ? q.tick (0, d) : q.tick (1, d);
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
    if (memory == nullptr)
        return; // no buffers lent (yet): the sound passes unchanged
    auto& delayLine = memory->delayLine;
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

// ------------------------------------------------------------------------- 0.61.0: FL Studio's remaining native effects

namespace
{
// An LFO value in -1..1 for a phase in 0..1: 0 sine, 1 triangle, 2 square, 3 saw.
inline float lfoShape (int shape, double phase) noexcept
{
    phase -= std::floor (phase);
    switch (shape)
    {
        case 1: return static_cast<float> (phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase);
        case 2: return phase < 0.5 ? 1.0f : -1.0f;
        case 3: return static_cast<float> (2.0 * phase - 1.0);
        default: return static_cast<float> (std::sin (2.0 * kPi * phase));
    }
}

// A linear read of a delay line `d` samples behind `write`.
inline float readLine (const std::vector<float>& line, int write, float d) noexcept
{
    const int size = static_cast<int> (line.size());
    float rp = static_cast<float> (write) - d;
    while (rp < 0.0f)
        rp += static_cast<float> (size);
    const int i0 = std::min (static_cast<int> (rp), size - 1);
    const float frac = rp - static_cast<float> (i0);
    const int i1 = i0 + 1 >= size ? 0 : i0 + 1;
    return line[static_cast<size_t> (i0)] + frac * (line[static_cast<size_t> (i1)] - line[static_cast<size_t> (i0)]);
}
} // namespace

// Flanger: a short modulated delay with feedback, the two channels' LFOs offset by Phase (as Fruity Flanger).
void EffectUnit::runFlanger (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    if (memory == nullptr)
        return;
    auto& delayLine = memory->delayLine;
    const int size = static_cast<int> (delayLine[0].size());
    const float base = static_cast<float> (s.params[0] * 0.001 * sampleRate);
    const float depth = static_cast<float> (s.params[1] * 0.001 * sampleRate);
    const double step = static_cast<double> (s.params[2]) / sampleRate;
    const double phaseOffset = static_cast<double> (s.params[3]) / 360.0;
    const float feedback = s.params[4];
    const int shape = effectChoice (EffectType::Flanger, 5, s.params[5]);
    const float mix = s.params[6];
    float* io[2] = { l, r };
    for (int i = 0; i < n; ++i)
    {
        lfoPhase += step;
        if (lfoPhase >= 1.0)
            lfoPhase -= 1.0;
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const float d = std::max (1.0f, base + depth * 0.5f * (1.0f + lfoShape (shape, lfoPhase + (ch == 1 ? phaseOffset : 0.0))));
            const float tap = readLine (delayLine[ch], delayWrite, d);
            const float in = io[ch][i];
            delayLine[ch][static_cast<size_t> (delayWrite)] = flush (in + feedback * tap);
            io[ch][i] = in * (1.0f - mix) + tap * mix;
        }
        if (++delayWrite >= size)
            delayWrite = 0;
    }
}

// Gate: below the threshold the sound is turned down by Range, after Hold, with the attack and release times; a sidechain track
// may drive it instead of the track's own sound.
void EffectUnit::runGate (const EffectSlot& s, float* l, float* r, int n, const EffectTransport& transport) noexcept
{
    const int key = static_cast<int> (std::lround (s.params[5])) - 1;
    const float* detectL = l;
    const float* detectR = r;
    if (key >= 0 && key < transport.numKeys && transport.keyLeft != nullptr && transport.keyRight != nullptr
        && transport.keyLeft[key] != nullptr && transport.keyRight[key] != nullptr)
    {
        detectL = transport.keyLeft[key];
        detectR = transport.keyRight[key];
    }
    const float threshold = dbToGain (s.params[0]);
    const float attack = static_cast<float> (1.0 - std::exp (-1.0 / (s.params[1] * 0.001 * sampleRate)));
    const int hold = static_cast<int> (s.params[2] * 0.001 * sampleRate);
    const float release = static_cast<float> (1.0 - std::exp (-1.0 / (s.params[3] * 0.001 * sampleRate)));
    const float floorGain = s.params[4] <= -99.9f ? 0.0f : dbToGain (s.params[4]);
    const float detectorRate = static_cast<float> (1.0 - std::exp (-1.0 / (0.002 * sampleRate)));
    for (int i = 0; i < n; ++i)
    {
        const float level = std::max (std::fabs (detectL[i]), std::fabs (detectR[i]));
        env += (level - env) * (level > env ? 1.0f : detectorRate);
        env = flush (env);
        const bool open = env >= threshold;
        if (open)
            gateHold = hold;
        else if (gateHold > 0)
            --gateHold;
        const float target = open || gateHold > 0 ? 1.0f : floorGain;
        gateGain += (target - gateGain) * (target > gateGain ? attack : release);
        l[i] *= gateGain;
        r[i] *= gateGain;
    }
}

// Bass Boost: a low shelf (up to +15 dB) at the frequency, with a gentle saturation of what it adds, as Fruity Bass Boost.
void EffectUnit::runBassBoost (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    if (! eqValid || s.params != eqCached)
    {
        eqCached = s.params;
        eqValid = true;
        bands[0].setCoefs (shelfCoefs (true, s.params[0], 15.0 * s.params[1], sampleRate));
        bands[1].setCoefs (BiquadCoefs::lowPass (2.0 * kPi * std::min (static_cast<double> (s.params[0]) * 2.0, sampleRate * 0.45) / sampleRate, 0.7071));
    }
    if (s.params[1] < 0.001f)
    {
        bands[0].clearState();
        bands[1].clearState();
        return;
    }
    const float amount = s.params[1];
    float* io[2] = { l, r };
    for (size_t ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
        {
            const float in = io[ch][i];
            const float shelved = static_cast<float> (bands[0].tick (ch, in));
            const float lows = static_cast<float> (bands[1].tick (ch, shelved - in)); // what the shelf added, kept to the lows
            io[ch][i] = in + std::tanh (lows * (1.0f + 2.0f * amount)) / (1.0f + 2.0f * amount) * (1.0f + amount);
        }
}

// Panomatic: a volume and a pan, and an LFO (free or synced to the song) on one of them, as Fruity Panomatic.
void EffectUnit::runPanomatic (const EffectSlot& s, float* l, float* r, int n, const EffectTransport& transport) noexcept
{
    const float gain = dbToGain (s.params[0]);
    const float pan = s.params[1];
    const int target = effectChoice (EffectType::Panomatic, 2, s.params[2]);
    const int sync = effectChoice (EffectType::Panomatic, 4, s.params[4]);
    const float depth = s.params[5];
    const int shape = effectChoice (EffectType::Panomatic, 6, s.params[6]);
    double step = static_cast<double> (s.params[3]) / sampleRate;
    if (sync > 0 && transport.ticksPerSample > 0.0)
    {
        static constexpr double cycleBeats[] = { 0.0, 0.5, 1.0, 2.0, 4.0, 8.0 };
        const double cycleTicks = cycleBeats[sync] * 480.0; // 480 ticks per beat (NoteSequence.h)
        step = transport.ticksPerSample / cycleTicks;
        if (transport.tick >= 0.0)
            panPhase = std::fmod (transport.tick / cycleTicks, 1.0);
    }
    for (int i = 0; i < n; ++i)
    {
        float p = pan, g = gain;
        if (target != 0 && depth > 0.0f)
        {
            const float lfo = lfoShape (shape, panPhase);
            if (target == 1)
                p = std::clamp (pan + depth * lfo, -1.0f, 1.0f);
            else
                g = gain * (1.0f - depth * 0.5f * (1.0f - lfo)); // tremolo: down to (1 - depth) of the volume
        }
        panPhase += step;
        if (panPhase >= 1.0)
            panPhase -= 1.0;
        // a balance: the louder side stays at unity, the other goes down
        l[i] *= g * std::min (1.0f, 1.0f - p);
        r[i] *= g * std::min (1.0f, 1.0f + p);
    }
}

// Squeeze: a puncher (drive) before a bit reduction and a sample-and-hold downsampler, a low-pass after them, as Fruity Squeeze.
void EffectUnit::runSqueeze (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    if (! eqValid || s.params != eqCached)
    {
        eqCached = s.params;
        eqValid = true;
        bands[0].setCoefs (BiquadCoefs::lowPass (2.0 * kPi * std::min (static_cast<double> (s.params[3]), sampleRate * 0.45) / sampleRate, 0.7071));
    }
    const float levels = std::pow (2.0f, std::round (s.params[0])) * 0.5f; // steps per unit
    const int hold = std::max (1, static_cast<int> (std::lround (s.params[1])));
    const float punch = s.params[2];
    const bool filtered = s.params[3] < 19999.0f;
    const float mix = s.params[4];
    float* io[2] = { l, r };
    for (int i = 0; i < n; ++i)
    {
        const bool sample = sqCount == 0;
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const float in = io[ch][i];
            if (sample)
            {
                float v = punch > 0.0f ? std::tanh (in * (1.0f + 8.0f * punch)) : in;
                v = std::round (v * levels) / levels;
                sqHold[ch] = v;
            }
            float wet = sqHold[ch];
            if (filtered)
                wet = static_cast<float> (bands[0].tick (ch, wet));
            io[ch][i] = in * (1.0f - mix) + wet * mix;
        }
        if (++sqCount >= hold)
            sqCount = 0;
    }
}

// Vibrato: a modulated delay that moves the pitch up and down by Depth cents, as Fruity Vibrator.
void EffectUnit::runVibrato (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    if (memory == nullptr)
        return;
    auto& delayLine = memory->delayLine;
    const int size = static_cast<int> (delayLine[0].size());
    const double rate = static_cast<double> (s.params[0]);
    const double step = rate / sampleRate;
    // a sinusoidal delay d(t) = D sin(2 pi f t) shifts the pitch by up to 2 pi f D; for `cents` of vibrato D = ratio / (2 pi f)
    const double ratio = std::pow (2.0, s.params[1] / 1200.0) - 1.0;
    const float depth = static_cast<float> (ratio * sampleRate / (2.0 * kPi * rate));
    const float base = depth + 2.0f;
    const int shape = effectChoice (EffectType::Vibrato, 2, s.params[2]);
    const double offset = 0.5 * s.params[3];
    float* io[2] = { l, r };
    for (int i = 0; i < n; ++i)
    {
        lfoPhase += step;
        if (lfoPhase >= 1.0)
            lfoPhase -= 1.0;
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const float d = std::clamp (base + depth * lfoShape (shape, lfoPhase + (ch == 1 ? offset : 0.0)), 1.0f, static_cast<float> (size - 2));
            const float in = io[ch][i];
            delayLine[ch][static_cast<size_t> (delayWrite)] = in;
            io[ch][i] = readLine (delayLine[ch], delayWrite, d);
        }
        if (++delayWrite >= size)
            delayWrite = 0;
    }
}

// Parametric EQ 2: seven bands (a low shelf, five peaks, a high shelf) and an output gain, as Fruity Parametric EQ 2.
void EffectUnit::runParametricEq2 (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    if (! eqValid || s.params != eqCached)
    {
        eqCached = s.params;
        eqValid = true;
        for (int b = 0; b < effect_tables::kPeq2Bands; ++b)
        {
            const double hz = s.params[static_cast<size_t> (3 * b)], gain = s.params[static_cast<size_t> (3 * b + 1)], q = s.params[static_cast<size_t> (3 * b + 2)];
            if (b == 0)
                bands[0].setCoefs (shelfCoefs (true, hz, gain, sampleRate));
            else if (b == effect_tables::kPeq2Bands - 1)
                bands[static_cast<size_t> (b)].setCoefs (shelfCoefs (false, hz, gain, sampleRate));
            else
                bands[static_cast<size_t> (b)].setCoefs (peakCoefs (hz, gain, q, sampleRate));
        }
    }
    for (int b = 0; b < effect_tables::kPeq2Bands; ++b)
    {
        if (std::fabs (s.params[static_cast<size_t> (3 * b + 1)]) >= 0.01f)
            processStereo (bands[static_cast<size_t> (b)], l, r, n);
        else
            bands[static_cast<size_t> (b)].clearState();
    }
    const float out = s.params[21];
    if (std::fabs (out) >= 0.01f)
    {
        const float g = dbToGain (out);
        for (int i = 0; i < n; ++i)
        {
            l[i] *= g;
            r[i] *= g;
        }
    }
}

// Multiband compressor: the sound is split in three by two Linkwitz-Riley crossovers, each band compressed on its own and given
// its gain, then summed again, as Maximus.
void EffectUnit::runMultibandComp (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    if (! eqValid || s.params != eqCached)
    {
        eqCached = s.params;
        eqValid = true;
        const double w1 = 2.0 * kPi * std::min (static_cast<double> (s.params[0]), sampleRate * 0.45) / sampleRate;
        const double w2 = 2.0 * kPi * std::min (static_cast<double> (std::max (s.params[1], s.params[0] * 1.5f)), sampleRate * 0.45) / sampleRate;
        for (auto& b : xLow) b.setCoefs (BiquadCoefs::lowPass (w1, 0.7071));
        for (auto& b : xMidHp) b.setCoefs (BiquadCoefs::highPass (w1, 0.7071));
        for (auto& b : xMidLp) b.setCoefs (BiquadCoefs::lowPass (w2, 0.7071));
        for (auto& b : xHigh) b.setCoefs (BiquadCoefs::highPass (w2, 0.7071));
    }
    const float attack = static_cast<float> (1.0 - std::exp (-1.0 / (s.params[8] * 0.001 * sampleRate)));
    const float release = static_cast<float> (1.0 - std::exp (-1.0 / (s.params[9] * 0.001 * sampleRate)));
    constexpr float kLn10Over20 = 0.115129254649702f;
    const float threshold[3] = { s.params[2], s.params[4], s.params[6] };
    const float slope[3] = { 1.0f - 1.0f / s.params[3], 1.0f - 1.0f / s.params[5], 1.0f - 1.0f / s.params[7] };
    const float makeup[3] = { s.params[10], s.params[11], s.params[12] };
    for (int i = 0; i < n; ++i)
    {
        float band[3][2];
        const float in[2] = { l[i], r[i] };
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const double x = in[ch];
            band[0][ch] = static_cast<float> (xLow[1].tick (ch, xLow[0].tick (ch, x)));
            band[1][ch] = static_cast<float> (xMidLp[1].tick (ch, xMidLp[0].tick (ch, xMidHp[1].tick (ch, xMidHp[0].tick (ch, x)))));
            band[2][ch] = static_cast<float> (xHigh[1].tick (ch, xHigh[0].tick (ch, x)));
        }
        float outL = 0.0f, outR = 0.0f;
        for (size_t b = 0; b < 3; ++b)
        {
            const float level = std::max (std::fabs (band[b][0]), std::fabs (band[b][1]));
            bandEnv[b] += (level - bandEnv[b]) * (level > bandEnv[b] ? attack : release);
            bandEnv[b] = flush (bandEnv[b]);
            const float levelDb = 20.0f * std::log10 (std::max (bandEnv[b], 1.0e-6f));
            const float over = levelDb - threshold[b];
            const float reduction = over > 0.0f ? over * slope[b] : 0.0f;
            const float gain = std::exp ((makeup[b] - reduction) * kLn10Over20);
            outL += band[b][0] * gain;
            outR += band[b][1] * gain;
        }
        l[i] = outL;
        r[i] = outR;
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
        case EffectType::Flanger:
        {
            static const std::vector<EffectPreset> list {
                make ("Classic jet", { 1.0f, 3.0f, 0.2f, 90.0f, 0.6f, 0.0f, 0.5f }),
                make ("Slow sweep", { 3.0f, 6.0f, 0.08f, 180.0f, 0.4f, 1.0f, 0.5f }),
                make ("Metallic", { 0.5f, 1.0f, 2.0f, 0.0f, 0.85f, 0.0f, 0.6f }),
            };
            return list;
        }
        case EffectType::Gate:
        {
            static const std::vector<EffectPreset> list {
                make ("Noise gate", { -50.0f, 1.0f, 30.0f, 150.0f, -100.0f, 0.0f }),
                make ("Drum tightener", { -30.0f, 0.5f, 10.0f, 60.0f, -24.0f, 0.0f }),
                make ("Hard chop", { -20.0f, 0.2f, 0.0f, 20.0f, -100.0f, 0.0f }),
            };
            return list;
        }
        case EffectType::Panomatic:
        {
            static const std::vector<EffectPreset> list {
                make ("Auto-pan 1 bar", { 0.0f, 0.0f, 1.0f, 1.0f, 4.0f, 1.0f, 0.0f }),
                make ("Tremolo 1/8", { 0.0f, 0.0f, 2.0f, 1.0f, 1.0f, 0.8f, 0.0f }),
                make ("Helicopter", { 0.0f, 0.0f, 2.0f, 12.0f, 0.0f, 1.0f, 2.0f }),
            };
            return list;
        }
        case EffectType::Squeeze:
        {
            static const std::vector<EffectPreset> list {
                make ("8-bit", { 8.0f, 4.0f, 0.0f, 8000.0f, 1.0f }),
                make ("Lo-fi radio", { 10.0f, 2.0f, 0.3f, 4000.0f, 1.0f }),
                make ("Crushed", { 4.0f, 8.0f, 0.5f, 20000.0f, 1.0f }),
            };
            return list;
        }
        case EffectType::MultibandComp:
        {
            static const std::vector<EffectPreset> list {
                make ("Gentle master", { 150.0f, 2500.0f, -14.0f, 2.0f, -12.0f, 2.0f, -14.0f, 2.0f, 15.0f, 150.0f, 1.0f, 0.0f, 1.0f }),
                make ("Tight low end", { 120.0f, 3000.0f, -24.0f, 6.0f, -12.0f, 1.5f, -12.0f, 1.5f, 8.0f, 100.0f, 3.0f, 0.0f, 0.0f }),
                make ("De-harsh", { 200.0f, 2500.0f, -12.0f, 1.5f, -12.0f, 1.5f, -26.0f, 8.0f, 2.0f, 80.0f, 0.0f, 0.0f, 2.0f }),
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
        case EffectType::WaveShaper: // 0.62.0
        {
            static const std::vector<EffectPreset> list {
                make ("Warm tube", { 9.0f, 5.0f, 0.8f, 0.1f, -6.0f, 1.0f }),
                make ("Hard clip", { 18.0f, 1.0f, 1.0f, 0.0f, -4.0f, 1.0f }),
                make ("Wave folder", { 14.0f, 2.0f, 1.0f, 0.0f, -3.0f, 1.0f }),
                make ("Octave up", { 6.0f, 4.0f, 1.0f, 0.0f, -3.0f, 1.0f }),
            };
            return list;
        }
        case EffectType::Vocoder:
        {
            static const std::vector<EffectPreset> list {
                make ("Robot", { 0.0f, 2.0f, 2.0f, 30.0f, 1.0f, 0.0f, 6.0f, 1.0f }),
                make ("Old vocoder", { 0.0f, 0.0f, 5.0f, 80.0f, 0.7f, 0.0f, 8.0f, 1.0f }),
                make ("Whisper", { 0.0f, 2.0f, 1.0f, 15.0f, 1.6f, 4.0f, 6.0f, 1.0f }),
            };
            return list;
        }
        case EffectType::PeakController:
        {
            static const std::vector<EffectPreset> list {
                make ("Follow the level", { 0.0f, 100.0f, 150.0f, 0.0f, 2.0f }),
                make ("Duck (level pushes down)", { 100.0f, -150.0f, 250.0f, 0.0f, 2.0f }),
                make ("Slow LFO", { 50.0f, 0.0f, 150.0f, 50.0f, 0.25f }),
                make ("Level and wobble", { 20.0f, 80.0f, 200.0f, 20.0f, 6.0f }),
            };
            return list;
        }
        case EffectType::Convolver:
        {
            static const std::vector<EffectPreset> list {
                make ("Small room", { 0.0f, 0.5f, 0.6f, 0.0f, 0.2f, 0.0f }),
                make ("Concert hall", { 1.0f, 2.2f, 0.55f, 20.0f, 0.3f, 0.0f }),
                make ("Vocal plate", { 2.0f, 1.4f, 0.8f, 30.0f, 0.25f, 0.0f }),
                make ("Guitar spring", { 3.0f, 1.8f, 0.7f, 0.0f, 0.3f, 0.0f }),
                make ("Cathedral", { 4.0f, 4.0f, 0.4f, 40.0f, 0.4f, 0.0f }),
            };
            return list;
        }
        case EffectType::FrequencyShifter: // 0.70.0
        {
            static const std::vector<EffectPreset> list {
                make ("Slow barber pole", { 0.5f, 0.0f, 0.6f, 0.5f }),
                make ("Detune", { 4.0f, 3.0f, 0.0f, 0.5f }),
                make ("Bell tones", { 220.0f, 0.0f, 0.0f, 1.0f }),
                make ("Downward spiral", { -30.0f, 5.0f, 0.75f, 0.6f }),
            };
            return list;
        }
        case EffectType::VintageChorus:
        {
            static const std::vector<EffectPreset> list {
                make ("Ensemble I", { 0.0f, 1.0f, 0.6f, 0.5f }),
                make ("Ensemble II", { 1.0f, 1.0f, 0.6f, 0.5f }),
                make ("Shimmer (I + II)", { 2.0f, 1.0f, 0.7f, 0.5f }),
                make ("Dark and wide", { 0.0f, 1.0f, 0.2f, 0.6f }),
            };
            return list;
        }
        case EffectType::VintagePhaser:
        {
            static const std::vector<EffectPreset> list {
                make ("Slow stone", { 0.0f, 0.25f, 0.9f, 0.7f, 0.5f }),
                make ("Script ninety", { 1.0f, 0.8f, 0.7f, 0.2f, 0.5f }),
                make ("Throbbing vibe", { 2.0f, 4.0f, 0.9f, 0.1f, 0.6f }),
            };
            return list;
        }
        case EffectType::Effector:
        {
            static const std::vector<EffectPreset> list {
                make ("Telephone (lo-fi)", { 1.0f, 0.6f, 0.7f, 1.0f }),
                make ("Jet flanger", { 2.0f, 0.3f, 0.8f, 1.0f }),
                make ("Filter sweep down", { 4.0f, 0.35f, 0.6f, 1.0f }),
                make ("Dub echo", { 6.0f, 0.45f, 0.65f, 1.0f }),
                make ("Robot (ring mod)", { 7.0f, 0.4f, 1.0f, 1.0f }),
                make ("Talking (formant)", { 9.0f, 0.2f, 0.7f, 1.0f }),
            };
            return list;
        }
        case EffectType::Grains:
        {
            static const std::vector<EffectPreset> list {
                make ("Cloud", { 120.0f, 0.0f, 400.0f, 0.0f, 0.0f, 0.3f, 0.6f }),
                make ("Octave up shimmer", { 90.0f, 12.0f, 200.0f, 0.0f, 0.0f, 0.4f, 0.4f }),
                make ("Backwards", { 200.0f, 0.0f, 100.0f, 0.0f, 100.0f, 0.0f, 1.0f }),
                make ("Scattered pitches", { 60.0f, 0.0f, 600.0f, 7.0f, 30.0f, 0.2f, 0.7f }),
            };
            return list;
        }
        case EffectType::EnvelopeController:
        {
            static const std::vector<EffectPreset> list {
                make ("Pluck (opens, then closes)", { -30.0f, 5.0f, 250.0f, 0.0f, 200.0f, 0.0f, 100.0f }),
                make ("Swell", { -30.0f, 600.0f, 300.0f, 100.0f, 500.0f, 0.0f, 100.0f }),
                make ("Duck while it sounds", { -30.0f, 10.0f, 100.0f, 100.0f, 300.0f, 100.0f, -100.0f }),
            };
            return list;
        }
        case EffectType::HyperChorus: // 0.73.0
        {
            static const std::vector<EffectPreset> list {
                make ("Wide double", { 2.0f, 12.0f, 1.5f, 0.2f, 1.0f, 0.8f, 0.5f }),
                make ("Lush ensemble", { 6.0f, 18.0f, 4.0f, 0.35f, 0.9f, 0.6f, 0.55f }),
                make ("Eight-voice swarm", { 8.0f, 25.0f, 8.0f, 0.8f, 1.0f, 0.5f, 0.6f }),
            };
            return list;
        }
        case EffectType::Scratcher:
        {
            static const std::vector<EffectPreset> list {
                make ("Baby scratch", { 1.0f, 0.0f, 1.0f, 250.0f, 0.5f, 1.0f }),
                make ("Forwards", { 2.0f, 0.0f, 2.0f, 200.0f, 0.4f, 1.0f }),
                make ("Chirps", { 3.0f, 0.0f, 2.0f, 180.0f, 0.3f, 1.0f }),
                make ("Transformer", { 4.0f, 0.0f, 4.0f, 250.0f, 0.2f, 1.0f }),
                make ("By hand (automate Position)", { 0.0f, 0.0f, 1.0f, 500.0f, 0.6f, 1.0f }),
            };
            return list;
        }
        case EffectType::Patcher:
        case EffectType::FormulaController:
        case EffectType::Equalizer:
        case EffectType::BassBoost:
        case EffectType::Vibrato:
        case EffectType::ParametricEq2:
        case EffectType::Plugin:
        case EffectType::None:
            break;
    }
    return empty;
}

// ------------------------------------------------------------------------- 0.62.0: waveshaper, vocoder, peak controller, convolver

// WaveShaper: the sound is bent by one of six curves at 4x the sample rate (the same anti-aliasing filters as the Overdrive), with
// a bias that makes the curve asymmetric and a DC blocker after it. Amount blends the straight line with the curve.
void EffectUnit::runWaveShaper (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    const double pre = dbToGain (s.params[0]);
    const int curve = effectChoice (EffectType::WaveShaper, 1, s.params[1]);
    const double amount = s.params[2];
    const double bias = s.params[3];
    const float post = dbToGain (s.params[4]);
    const float mix = s.params[5];
    const auto bend = [curve] (double v) noexcept -> double
    {
        switch (curve)
        {
            case 1:  return std::clamp (v, -1.0, 1.0);
            case 2:  return std::asin (std::sin (v * kPi * 0.5)) * (2.0 / kPi); // folds back at +-1
            case 3:  return std::sin (v * kPi * 0.5);
            case 4:  return std::abs (std::tanh (v));                             // full-wave: an octave up
            case 5:  return v >= 0.0 ? std::tanh (v) : 0.6 * std::tanh (v / 0.6);   // the negative half gives in sooner
            default: return std::tanh (v);
        }
    };
    const double offset = bias + amount * (bend (bias) - bias);
    float* io[2] = { l, r };
    for (size_t ch = 0; ch < 2; ++ch)
    {
        float* x = io[ch];
        for (int i = 0; i < n; ++i)
        {
            const float dry = x[i];
            float y = 0.0f;
            for (int k = 0; k < kOversample; ++k)
            {
                double u = k == 0 ? static_cast<double> (dry) * kOversample : 0.0;
                for (auto& q : upFilter)
                    u = q.tick (ch, u);
                const double v = pre * u + bias;
                double d = v + amount * (bend (v) - v) - offset;
                for (auto& q : downFilter)
                    d = q.tick (ch, d);
                if (k == kOversample - 1)
                    y = static_cast<float> (d);
            }
            const float blocked = y - dcX[ch] + 0.995f * dcY[ch];
            dcX[ch] = y;
            dcY[ch] = flush (blocked);
            x[i] = dry + mix * (blocked * post - dry);
        }
    }
}

// Vocoder: the modulator (the voice) and the carrier are split into the same bands; the level of each band of the voice sets the
// level of that band of the carrier. Its filters and levels live in the lent memory.
void EffectUnit::runVocoder (const EffectSlot& s, float* l, float* r, int n, const EffectTransport& transport) noexcept
{
    if (memory == nullptr)
        return;
    const int key = static_cast<int> (std::lround (s.params[0])) - 1;
    const float* modL = nullptr;
    const float* modR = nullptr;
    if (key >= 0 && key < transport.numKeys && transport.keyLeft != nullptr && transport.keyRight != nullptr
        && transport.keyLeft[key] != nullptr && transport.keyRight[key] != nullptr)
    {
        modL = transport.keyLeft[key];
        modR = transport.keyRight[key];
    }
    static constexpr int kBandChoices[] = { 8, 12, 16 };
    const int bandCount = kBandChoices[std::clamp (effectChoice (EffectType::Vocoder, 1, s.params[1]), 0, 2)];
    if (std::abs (vocCached[0] - static_cast<float> (bandCount)) > 0.0f || std::abs (vocCached[1] - s.params[4]) > 0.0f
        || std::abs (vocCached[2] - s.params[5]) > 0.0f)
    {
        vocCached = { static_cast<float> (bandCount), s.params[4], s.params[5], 0.0f };
        // Bands from 90 Hz to 9 kHz, evenly spaced in pitch; Q so that neighbours cross near -3 dB at Width 1.
        const double lowHz = 90.0, highHz = std::min (9000.0, 0.4 * sampleRate);
        const double step = std::pow (highHz / lowHz, 1.0 / (bandCount - 1));
        const double q = std::sqrt (step) / (step - 1.0) * 1.3 / std::max (0.1, static_cast<double> (s.params[4]));
        const double shift = std::pow (2.0, s.params[5] / 12.0);
        for (int b = 0; b < bandCount; ++b)
        {
            const double hz = lowHz * std::pow (step, b);
            const auto mod = BiquadCoefs::bandPass (2.0 * kPi * hz / sampleRate, q);
            const auto car = BiquadCoefs::bandPass (2.0 * kPi * std::min (hz * shift, 0.45 * sampleRate) / sampleRate, q);
            for (size_t stage = 0; stage < 2; ++stage)
            {
                memory->vocMod[stage][static_cast<size_t> (b)].setCoefs (mod);
                memory->vocCar[stage][static_cast<size_t> (b)].setCoefs (car);
            }
        }
    }
    const float attack = static_cast<float> (1.0 - std::exp (-1.0 / (s.params[2] * 0.001 * sampleRate)));
    const float release = static_cast<float> (1.0 - std::exp (-1.0 / (s.params[3] * 0.001 * sampleRate)));
    const float out = dbToGain (s.params[6]) * 4.0f; // (a band-passed carrier is much quieter than the carrier)
    const float mix = s.params[7];
    for (int i = 0; i < n; ++i)
    {
        const double voice = modL != nullptr ? 0.5 * (static_cast<double> (modL[i]) + modR[i]) : static_cast<double> (l[i]);
        const double carL = modL != nullptr ? static_cast<double> (l[i]) : static_cast<double> (r[i]);
        const double carR = static_cast<double> (r[i]);
        double sumL = 0.0, sumR = 0.0;
        for (int b = 0; b < bandCount; ++b)
        {
            const auto band = static_cast<size_t> (b);
            const double m = memory->vocMod[1][band].tick (0, memory->vocMod[0][band].tick (0, voice));
            auto& level = memory->vocEnv[band];
            const auto rectified = static_cast<float> (std::abs (m));
            level = flush (level + (rectified - level) * (rectified > level ? attack : release));
            sumL += memory->vocCar[1][band].tick (0, memory->vocCar[0][band].tick (0, carL)) * level;
            sumR += memory->vocCar[1][band].tick (1, memory->vocCar[0][band].tick (1, carR)) * level;
        }
        const float wetL = static_cast<float> (sumL) * out, wetR = static_cast<float> (sumR) * out;
        l[i] += mix * (wetL - l[i]);
        r[i] += mix * (wetR - r[i]);
    }
}

// Peak Controller: follows the track's peak level (at once up, by Decay down) and adds an LFO; the result, 0..1, is read by the
// Player after the block (controlValue) and written into the parameter it drives. The sound passes unchanged, or not at all (Mute).
void EffectUnit::runPeakController (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    const float decay = static_cast<float> (std::exp (-1.0 / (s.params[2] * 0.001 * sampleRate)));
    for (int i = 0; i < n; ++i)
    {
        const float level = std::max (std::fabs (l[i]), std::fabs (r[i]));
        env = level > env ? level : flush (env * decay);
    }
    panPhase += static_cast<double> (s.params[4]) * n / sampleRate;
    panPhase -= std::floor (panPhase);
    const double lfo = std::sin (2.0 * kPi * panPhase);
    control = static_cast<float> (std::clamp (s.params[0] * 0.01 + s.params[1] * 0.01 * std::min (env, 1.0f) + s.params[3] * 0.01 * 0.5 * lfo, 0.0, 1.0));
    if (effectChoice (EffectType::PeakController, 8, s.params[8]) == 1)
    {
        std::fill_n (l, n, 0.0f);
        std::fill_n (r, n, 0.0f);
    }
}

// Convolver: the slot's impulse response does the work (see Convolver.h); here only the mix and the output level. A response made
// for another sample rate (the device changed and the project has not been published since) lets the sound through unchanged.
void EffectUnit::runConvolver (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    Convolver* c = s.convolver.get();
    if (c == nullptr || std::abs (c->settings().sampleRate - sampleRate) > 0.5)
        return;
    if (c != convolverSeen)
    {
        convolverSeen = c;
        c->clearState();
    }
    const float mix = s.params[4];
    const float out = dbToGain (s.params[5]);
    std::array<float, 256> wetL, wetR;
    for (int done = 0; done < n; done += 256)
    {
        const int count = std::min (256, n - done);
        std::copy_n (l + done, count, wetL.begin());
        std::copy_n (r + done, count, wetR.begin());
        c->process (wetL.data(), wetR.data(), count);
        for (int i = 0; i < count; ++i)
        {
            l[done + i] = (l[done + i] + mix * (wetL[static_cast<size_t> (i)] - l[done + i])) * out;
            r[done + i] = (r[done + i] + mix * (wetR[static_cast<size_t> (i)] - r[done + i])) * out;
        }
    }
}

// ------------------------------------------------------------------------- 0.70.0: frequency shifter, vintage chorus and phaser,
// effector, grains, envelope and formula controllers

namespace
{
// One tick of a state-variable filter (the same as the Filter's): the low-pass, band-pass and high-pass outputs.
struct SvfOut
{
    float low, band, high;
};
inline SvfOut svfTick (float in, float g, float k, float& ic1, float& ic2) noexcept
{
    const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
    const float v3 = in - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = flush (2.0f * v1 - ic1);
    ic2 = flush (2.0f * v2 - ic2);
    return { v2, v1, in - k * v1 - v2 };
}

inline float random01 (std::uint32_t& state) noexcept
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float> (state & 0xffffffu) / static_cast<float> (0x1000000);
}

// 0..1 on a logarithmic scale between two values.
inline double logScale (double x, double low, double high) noexcept { return low * std::pow (high / low, std::clamp (x, 0.0, 1.0)); }
} // namespace

// Frequency shifter: two chains of all-pass filters (Olli Niemitalo's coefficients) turn the sound into a pair of signals 90
// degrees apart; multiplied by a cosine and a sine of the shift frequency and added, only one side band is left: every frequency
// moved up (or down) by the same number of Hz.
void EffectUnit::runFrequencyShifter (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    static constexpr float kSquares[2][4] = { { 0.6923878f * 0.6923878f, 0.9360654322959f * 0.9360654322959f, 0.9882295226860f * 0.9882295226860f,
                                                0.9987488452737f * 0.9987488452737f },
                                              { 0.4021921162426f * 0.4021921162426f, 0.8561710882420f * 0.8561710882420f,
                                                0.9722909545651f * 0.9722909545651f, 0.9952884791278f * 0.9952884791278f } };
    const double step[2] = { (static_cast<double> (s.params[0]) + s.params[1]) / sampleRate, (static_cast<double> (s.params[0]) - s.params[1]) / sampleRate };
    const float feedback = s.params[2], mix = s.params[3];
    float* io[2] = { l, r };
    for (int i = 0; i < n; ++i)
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const float in = io[ch][i];
            const float x = in + feedback * phaserLast[ch];
            float path[2];
            for (size_t p = 0; p < 2; ++p)
            {
                float v = x;
                for (size_t k = 0; k < 4; ++k)
                {
                    auto& st = hilbert[ch][p * 4 + k]; // x1, x2, y1, y2
                    const float y = kSquares[p][k] * (v + st[3]) - st[1];
                    st[1] = st[0];
                    st[0] = v;
                    st[3] = st[2];
                    st[2] = flush (y);
                    v = y;
                }
                path[p] = v;
            }
            const float inPhase = hilbertHeld[ch]; // the first chain, one sample late
            hilbertHeld[ch] = path[0];
            const float quadrature = path[1];
            shiftPhase[ch] += step[ch];
            shiftPhase[ch] -= std::floor (shiftPhase[ch]);
            const double angle = 2.0 * kPi * shiftPhase[ch];
            const float out = inPhase * static_cast<float> (std::cos (angle)) + quadrature * static_cast<float> (std::sin (angle));
            phaserLast[ch] = flush (std::clamp (out, -4.0f, 4.0f));
            io[ch][i] = in * (1.0f - mix) + out * mix;
        }
}

// Vintage chorus: one short delay per channel swept by a triangle (the right channel the other way round), darkened and gently
// saturated as a bucket-brigade chip does. The three modes are the two buttons of an analogue polysynth's ensemble and both
// together (a fast, shallow shimmer).
void EffectUnit::runVintageChorus (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    if (memory == nullptr)
        return;
    static constexpr double kRate[] = { 0.513, 0.863, 9.75 };
    static constexpr float kShort[] = { 1.66f, 1.66f, 3.3f }, kLong[] = { 5.35f, 5.35f, 3.7f }; // ms
    auto& delayLine = memory->delayLine;
    const int size = static_cast<int> (delayLine[0].size());
    const auto mode = static_cast<size_t> (effectChoice (EffectType::VintageChorus, 0, s.params[0]));
    const double step = kRate[mode] / sampleRate;
    const float centre = static_cast<float> (0.5 * (kShort[mode] + kLong[mode]) * 0.001 * sampleRate);
    const float swing = static_cast<float> (0.5 * (kLong[mode] - kShort[mode]) * 0.001 * sampleRate) * s.params[1];
    const float tone = static_cast<float> (1.0 - std::exp (-2.0 * kPi * std::min (3000.0 * std::pow (4.0, static_cast<double> (s.params[2])), 0.45 * sampleRate) / sampleRate));
    const float mix = s.params[3];
    float* io[2] = { l, r };
    for (int i = 0; i < n; ++i)
    {
        lfoPhase += step;
        if (lfoPhase >= 1.0)
            lfoPhase -= 1.0;
        const float sweep = lfoShape (1, lfoPhase);
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const float in = io[ch][i];
            const float tap = readLine (delayLine[ch], delayWrite, std::max (1.0f, centre + (ch == 0 ? swing : -swing) * sweep));
            delayLine[ch][static_cast<size_t> (delayWrite)] = in;
            delayTone[ch] = flush (delayTone[ch] + tone * (tap - delayTone[ch]));
            const float wet = std::tanh (delayTone[ch] * 0.8f) * 1.25f;
            io[ch][i] = in * (1.0f - mix) + wet * mix;
        }
        if (++delayWrite >= size)
            delayWrite = 0;
    }
}

// Vintage phaser: four all-pass stages as three pedals wire them. Stone: a wide, slow sweep and a feedback that sings; Ninety: a
// narrower sweep around the mid range, less feedback, a little grit; Vibe: four stages tuned far apart and a lopsided sweep (a
// lamp and four light cells), the right channel a quarter turn later.
void EffectUnit::runVintagePhaser (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    const int model = effectChoice (EffectType::VintagePhaser, 0, s.params[0]);
    const double step = static_cast<double> (s.params[1]) / sampleRate;
    const float depth = s.params[2];
    const float feedback = s.params[3] * (model == 1 ? 0.5f : (model == 2 ? 0.35f : 1.0f));
    const float mix = s.params[4];
    static constexpr double kCentre[] = { 600.0, 800.0, 300.0 };
    static constexpr double kOctaves[] = { 2.5, 1.5, 1.6 };
    static constexpr double kVibeRatio[] = { 0.45, 1.0, 3.2, 9.0 };
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
                double sweep = std::sin (2.0 * kPi * (lfoPhase + (model == 2 && ch == 1 ? 0.25 : 0.0)));
                if (model == 2) // the lamp is slow to dim: more time near the top of the sweep
                    sweep = 2.0 * std::pow (0.5 + 0.5 * sweep, 0.6) - 1.0;
                const double base = kCentre[model] * std::pow (2.0, kOctaves[model] * depth * sweep);
                for (size_t st = 0; st < 4; ++st)
                {
                    const double hz = std::clamp (base * (model == 2 ? kVibeRatio[st] : 1.0), 20.0, 0.45 * sampleRate);
                    const double t = std::tan (kPi * hz / sampleRate);
                    vintageCoef[ch][st] = static_cast<float> ((t - 1.0) / (t + 1.0));
                }
            }
        }
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const float in = io[ch][i];
            float x = in + feedback * phaserLast[ch];
            if (model == 1)
                x = std::tanh (x * 1.2f) * (1.0f / 1.2f);
            for (size_t st = 0; st < 4; ++st)
            {
                auto& z = phaserState[ch][st];
                const float a = vintageCoef[ch][st];
                const float y = a * x + z;
                z = flush (x - a * y);
                x = y;
            }
            phaserLast[ch] = flush (std::clamp (x, -4.0f, 4.0f));
            io[ch][i] = in * (1.0f - mix) + x * mix;
        }
    }
}

// Effector: ten effects with two controls each, made to be moved while the song plays (automate X and Y).
//   Distortion  X drive, Y tone             Lo-fi      X sample rate, Y bits        Flanger   X delay, Y feedback
//   Phaser      X frequency, Y feedback     Low-pass   X cutoff, Y resonance        High-pass X cutoff, Y resonance
//   Delay       X time, Y feedback          Ring mod   X frequency, Y amount        Auto-pan  X rate, Y depth
//   Formant     X vowel (A E I O U), Y sharpness
void EffectUnit::runEffector (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    const int effect = effectChoice (EffectType::Effector, 0, s.params[0]);
    const double x = s.params[1], y = s.params[2];
    const float mix = s.params[3];
    float* io[2] = { l, r };
    std::vector<float>* lines = memory != nullptr ? memory->delayLine.data() : nullptr;
    const int size = lines != nullptr ? static_cast<int> (lines[0].size()) : 0;

    // what the controls mean for this effect, worked out once per block
    const auto fy = static_cast<float> (y);
    float p1 = 0.0f, p2 = 0.0f, p3 = 0.0f, p4 = 0.0f;
    double step = 0.0;
    switch (effect)
    {
        case 0: // drive, level, tone
            p1 = static_cast<float> (logScale (x, 1.0, 40.0));
            p2 = 0.5f + 0.5f / std::sqrt (p1);
            p3 = static_cast<float> (1.0 - std::exp (-2.0 * kPi * std::min (logScale (y, 500.0, 16000.0), 0.45 * sampleRate) / sampleRate));
            break;
        case 1: // hold length, steps
            p1 = static_cast<float> (std::lround (logScale (1.0 - x, 1.0, 60.0)));
            p2 = std::pow (2.0f, std::round (2.0f + 14.0f * fy)) * 0.5f;
            break;
        case 2: // centre delay, feedback
            p1 = static_cast<float> (logScale (x, 0.3, 10.0) * 0.001 * sampleRate);
            p2 = 0.9f * fy;
            step = 0.25 / sampleRate;
            break;
        case 3: // feedback
            p2 = 0.9f * fy;
            step = 0.4 / sampleRate;
            break;
        case 4:
        case 5: // cutoff, damping
            p1 = static_cast<float> (std::tan (kPi * std::min (logScale (x, 40.0, 18000.0), 0.45 * sampleRate) / sampleRate));
            p2 = 2.0f - 1.9f * fy;
            break;
        case 6: // time, feedback
            p1 = static_cast<float> (logScale (x, 20.0, 800.0) * 0.001 * sampleRate);
            p2 = 0.9f * fy;
            break;
        case 7:
            step = logScale (x, 20.0, 4000.0) / sampleRate;
            break;
        case 8:
            step = logScale (x, 0.1, 20.0) / sampleRate;
            break;
        default: // the two formants of the vowel between which X stands, and how sharp they are
        {
            static constexpr double kF1[] = { 730.0, 530.0, 270.0, 570.0, 300.0 }, kF2[] = { 1090.0, 1840.0, 2290.0, 840.0, 870.0 };
            const double at = std::clamp (x, 0.0, 1.0) * 4.0;
            const auto from = static_cast<size_t> (std::min (3.0, std::floor (at)));
            const double blend = at - static_cast<double> (from);
            const double f1 = kF1[from] * std::pow (kF1[from + 1] / kF1[from], blend), f2 = kF2[from] * std::pow (kF2[from + 1] / kF2[from], blend);
            p1 = static_cast<float> (std::tan (kPi * std::min (f1, 0.45 * sampleRate) / sampleRate));
            p3 = static_cast<float> (std::tan (kPi * std::min (f2, 0.45 * sampleRate) / sampleRate));
            p2 = 0.5f - 0.44f * fy;
            p4 = (1.0f + 2.0f * fy) * p2; // (a band-pass's peak is 1 / damping: brought back to 1, then made up for the narrower band)
            break;
        }
    }
    if ((effect == 2 || effect == 6) && lines == nullptr)
        return;

    for (int i = 0; i < n; ++i)
    {
        lfoPhase += step;
        if (lfoPhase >= 1.0)
            lfoPhase -= 1.0;
        if (effect == 3 && --controlLeft <= 0)
        {
            controlLeft = 16;
            const double hz = std::clamp (logScale (x, 100.0, 5000.0) * std::pow (2.0, std::sin (2.0 * kPi * lfoPhase)), 20.0, 0.45 * sampleRate);
            const double t = std::tan (kPi * hz / sampleRate);
            phaserCoef[0] = phaserCoef[1] = static_cast<float> ((t - 1.0) / (t + 1.0));
        }
        const bool sample = sqCount == 0;
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const float in = io[ch][i];
            float wet = in;
            switch (effect)
            {
                case 0:
                    delayTone[ch] = flush (delayTone[ch] + p3 * (std::tanh (in * p1) * p2 - delayTone[ch]));
                    wet = delayTone[ch];
                    break;
                case 1:
                    if (sample)
                        sqHold[ch] = std::round (in * p2) / p2;
                    wet = sqHold[ch];
                    break;
                case 2:
                {
                    const float d = std::max (1.0f, p1 * (1.0f + 0.5f * lfoShape (0, lfoPhase + (ch == 1 ? 0.25 : 0.0))));
                    const float tap = readLine (lines[ch], delayWrite, d);
                    lines[ch][static_cast<size_t> (delayWrite)] = flush (in + p2 * tap);
                    wet = 0.5f * (in + tap);
                    break;
                }
                case 3:
                {
                    float v = in + p2 * phaserLast[ch];
                    for (size_t st = 0; st < 6; ++st)
                    {
                        auto& z = phaserState[ch][st];
                        const float out = phaserCoef[ch] * v + z;
                        z = flush (v - phaserCoef[ch] * out);
                        v = out;
                    }
                    phaserLast[ch] = flush (std::clamp (v, -4.0f, 4.0f));
                    wet = 0.5f * (in + v);
                    break;
                }
                case 4: wet = svfTick (in, p1, p2, svf1a[ch], svf1b[ch]).low; break;
                case 5: wet = svfTick (in, p1, p2, svf1a[ch], svf1b[ch]).high; break;
                case 6:
                {
                    if (delayCurrent[ch] < 0.0f)
                        delayCurrent[ch] = p1;
                    delayCurrent[ch] += (p1 - delayCurrent[ch]) * 0.0004f; // a moved X glides (the pitch bends as a tape delay's)
                    const float tap = readLine (lines[ch], delayWrite, std::clamp (delayCurrent[ch], 1.0f, static_cast<float> (size - 2)));
                    lines[ch][static_cast<size_t> (delayWrite)] = flush (in + p2 * tap);
                    wet = in + 0.6f * tap;
                    break;
                }
                case 7: wet = in * (1.0f - fy + fy * lfoShape (0, lfoPhase)); break;
                case 8:
                {
                    const float pan = fy * lfoShape (0, lfoPhase);
                    wet = in * (ch == 0 ? std::min (1.0f, 1.0f - pan) : std::min (1.0f, 1.0f + pan));
                    break;
                }
                default:
                    wet = p4 * (svfTick (in, p1, p2, svf1a[ch], svf1b[ch]).band + 0.7f * svfTick (in, p3, p2, svf2a[ch], svf2b[ch]).band);
                    break;
            }
            io[ch][i] = in * (1.0f - mix) + wet * mix;
        }
        if (effect == 1 && ++sqCount >= static_cast<int> (p1))
            sqCount = 0;
        if ((effect == 2 || effect == 6) && ++delayWrite >= size)
            delayWrite = 0;
    }
}

// Grains: the sound is recorded, and four overlapping grains (Hann windows, a quarter of a grain apart) replay pieces of the last
// moments: from up to Scatter back, at their own pitch, some of them backwards.
void EffectUnit::runGrains (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    if (memory == nullptr)
        return;
    auto& delayLine = memory->delayLine;
    const int size = static_cast<int> (delayLine[0].size());
    const int life = std::clamp (static_cast<int> (s.params[0] * 0.001 * sampleRate), 64, std::max (64, size / 8));
    const int hop = std::max (1, life / kGrains);
    const double scatter = s.params[2] * 0.001 * sampleRate;
    const float feedback = s.params[5], mix = s.params[6];
    for (int i = 0; i < n; ++i)
    {
        if (--grainClock <= 0)
        {
            grainClock = hop;
            auto* voice = &grainVoices[0];
            for (auto& g : grainVoices) // a free voice, or the one that has played longest
                if (g.life == 0 || (voice->life != 0 && g.age > voice->age))
                    voice = &g;
            const double semitones = static_cast<double> (s.params[1]) + s.params[3] * (2.0 * random01 (grainRandom) - 1.0);
            const double ratio = std::pow (2.0, semitones / 12.0);
            const bool backwards = random01 (grainRandom) * 100.0f < s.params[4];
            voice->drift = backwards ? 1.0 + ratio : 1.0 - ratio;
            const double travel = voice->drift * life; // how far the delay moves while the grain plays
            double start = 2.0 + scatter * random01 (grainRandom) + std::max (0.0, -travel);
            start = std::min (start, static_cast<double> (size - 2) - std::max (0.0, travel));
            voice->delay = std::max (start, 2.0 + std::max (0.0, -travel));
            voice->age = 0;
            voice->life = life;
        }
        float wetL = 0.0f, wetR = 0.0f;
        for (auto& g : grainVoices)
        {
            if (g.life == 0)
                continue;
            const float window = 0.5f - 0.5f * static_cast<float> (std::cos (2.0 * kPi * g.age / g.life));
            const float d = static_cast<float> (std::clamp (g.delay, 1.0, static_cast<double> (size - 2)));
            wetL += window * readLine (delayLine[0], delayWrite, d);
            wetR += window * readLine (delayLine[1], delayWrite, d);
            g.delay += g.drift;
            if (++g.age >= g.life)
                g.life = 0;
        }
        wetL *= 0.5f; // four Hann windows a quarter apart add up to 2
        wetR *= 0.5f;
        delayLine[0][static_cast<size_t> (delayWrite)] = flush (l[i] + feedback * wetL);
        delayLine[1][static_cast<size_t> (delayWrite)] = flush (r[i] + feedback * wetR);
        l[i] = l[i] * (1.0f - mix) + wetL * mix;
        r[i] = r[i] * (1.0f - mix) + wetR * mix;
        if (++delayWrite >= size)
            delayWrite = 0;
    }
}

// Envelope controller: the track's sound rising above the threshold starts an envelope (attack, decay to the sustain level);
// falling below half the threshold releases it. The value goes to the parameter the slot names, as the Peak Controller's.
void EffectUnit::runEnvelopeController (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    const float threshold = dbToGain (s.params[0]);
    const float follow = static_cast<float> (std::exp (-1.0 / (0.005 * sampleRate)));
    const float attack = static_cast<float> (1.0 / std::max (1.0, s.params[1] * 0.001 * sampleRate));
    const float decay = static_cast<float> (std::exp (-1.0 / (s.params[2] * 0.001 * sampleRate * 0.25)));
    const float sustain = s.params[3] * 0.01f;
    const float release = static_cast<float> (std::exp (-1.0 / (s.params[4] * 0.001 * sampleRate * 0.25)));
    for (int i = 0; i < n; ++i)
    {
        const float level = std::max (std::fabs (l[i]), std::fabs (r[i]));
        ctlLevel = level > ctlLevel ? level : flush (ctlLevel * follow);
        const bool sounding = ctlStage == 1 || ctlStage == 2;
        const bool gate = ctlLevel > (sounding ? 0.5f * threshold : threshold);
        if (gate && ! sounding)
            ctlStage = 1;
        else if (! gate && sounding)
            ctlStage = 3;
        if (ctlStage == 1)
        {
            env += attack;
            if (env >= 1.0f)
            {
                env = 1.0f;
                ctlStage = 2;
            }
        }
        else if (ctlStage == 2)
            env = sustain + (env - sustain) * decay;
        else if (ctlStage == 3)
        {
            env *= release;
            if (env < 1.0e-4f)
            {
                env = 0.0f;
                ctlStage = 0;
            }
        }
    }
    control = std::clamp (s.params[5] * 0.01f + s.params[6] * 0.01f * env, 0.0f, 1.0f);
    if (effectChoice (EffectType::EnvelopeController, 10, s.params[10]) == 1)
    {
        std::fill_n (l, n, 0.0f);
        std::fill_n (r, n, 0.0f);
    }
}

// Formula controller: the slot's formula is worked out once per block from the three knobs, the time since the controller
// started, the song position (its own clock at 120 BPM while the song is stopped) and the track's level.
void EffectUnit::runFormulaController (const EffectSlot& s, float* l, float* r, int n, const EffectTransport& transport) noexcept
{
    const float follow = static_cast<float> (std::exp (-1.0 / (0.15 * sampleRate)));
    for (int i = 0; i < n; ++i)
    {
        const float level = std::max (std::fabs (l[i]), std::fabs (r[i]));
        ctlLevel = level > ctlLevel ? level : flush (ctlLevel * follow);
    }
    ctlTime += n / sampleRate;
    if (transport.tick >= 0.0)
        ctlBeat = (transport.tick + n * transport.ticksPerSample) / 480.0;
    else
        ctlBeat += 2.0 * n / sampleRate;
    if (s.formula == nullptr)
    {
        control = s.params[0];
        return;
    }
    Formula::Variables v;
    v.a = s.params[0];
    v.b = s.params[1];
    v.c = s.params[2];
    v.t = ctlTime;
    v.beat = ctlBeat;
    v.level = std::min (ctlLevel, 1.0f);
    control = s.formula->evaluate (v);
}

// ------------------------------------------------------------------------- 0.73.0: hyper chorus, scratcher, patcher

// Hyper chorus: up to eight taps of one delay line, each swept by its own slow sine (their phases spread evenly), panned across the
// stereo field from left to right and darkened a little.
void EffectUnit::runHyperChorus (const EffectSlot& s, float* l, float* r, int n) noexcept
{
    if (memory == nullptr)
        return;
    auto& delayLine = memory->delayLine;
    const int size = static_cast<int> (delayLine[0].size());
    const int voices = std::clamp (static_cast<int> (std::lround (s.params[0])), 2, 8);
    const float base = static_cast<float> (s.params[1] * 0.001 * sampleRate);
    const float swing = static_cast<float> (s.params[2] * 0.001 * sampleRate);
    const double step = static_cast<double> (s.params[3]) / sampleRate;
    const float spread = s.params[4];
    const float tone = static_cast<float> (1.0 - std::exp (-2.0 * kPi * std::min (2500.0 * std::pow (6.0, static_cast<double> (s.params[5])), 0.45 * sampleRate) / sampleRate));
    const float mix = s.params[6];
    std::array<float, 8> panL {}, panR {};
    for (int v = 0; v < voices; ++v)
    {
        const float place = voices > 1 ? (static_cast<float> (v) / static_cast<float> (voices - 1)) * 2.0f - 1.0f : 0.0f; // -1 .. 1
        const float pan = place * spread;
        panL[static_cast<size_t> (v)] = std::sqrt (0.5f * (1.0f - pan));
        panR[static_cast<size_t> (v)] = std::sqrt (0.5f * (1.0f + pan));
    }
    const float norm = 1.4142135f / std::sqrt (static_cast<float> (voices));
    for (int i = 0; i < n; ++i)
    {
        const float mono = 0.5f * (l[i] + r[i]);
        delayLine[0][static_cast<size_t> (delayWrite)] = mono;
        float wetL = 0.0f, wetR = 0.0f;
        for (int v = 0; v < voices; ++v)
        {
            auto& phase = hyperPhase[static_cast<size_t> (v)];
            phase += step * (1.0 + 0.13 * v); // (slightly different speeds: they never line up again)
            if (phase >= 1.0)
                phase -= 1.0;
            const float d = std::clamp (base * (1.0f + 0.07f * static_cast<float> (v)) + swing * static_cast<float> (std::sin (2.0 * kPi * phase)), 1.0f, static_cast<float> (size - 2));
            const float tap = readLine (delayLine[0], delayWrite, d);
            wetL += tap * panL[static_cast<size_t> (v)];
            wetR += tap * panR[static_cast<size_t> (v)];
        }
        delayTone[0] = flush (delayTone[0] + tone * (wetL * norm - delayTone[0]));
        delayTone[1] = flush (delayTone[1] + tone * (wetR * norm - delayTone[1]));
        l[i] = l[i] * (1.0f - mix) + delayTone[0] * mix;
        r[i] = r[i] * (1.0f - mix) + delayTone[1] * mix;
        if (++delayWrite >= size)
            delayWrite = 0;
    }
}

// Scratcher: the sound is recorded as it comes and played back from `scratchDelay` samples behind it, the record's place. By
// hand, the place follows Position (0: playing along, 1: Depth back) through a smoothing that gives the moves a hand's inertia, so
// automating Position scratches; the patterns move it themselves, Speed times a beat (or, while the song is stopped, at 120 BPM).
void EffectUnit::runScratcher (const EffectSlot& s, float* l, float* r, int n, const EffectTransport& transport) noexcept
{
    if (memory == nullptr)
        return;
    auto& delayLine = memory->delayLine;
    const int size = static_cast<int> (delayLine[0].size());
    const int mode = effectChoice (EffectType::Scratcher, 0, s.params[0]);
    const double depth = std::min (static_cast<double> (s.params[3]) * 0.001 * sampleRate, static_cast<double> (size - 4));
    const double smoothing = std::exp (-1.0 / (std::max (0.0005, 0.04 * static_cast<double> (s.params[4])) * sampleRate));
    const float mix = s.params[5];
    const double beatsPerSample = transport.tick >= 0.0 && transport.ticksPerSample > 0.0 ? transport.ticksPerSample / 480.0 : 2.0 / sampleRate;
    double beat = transport.tick >= 0.0 ? transport.tick / 480.0 : ctlBeat;
    for (int i = 0; i < n; ++i)
    {
        beat += beatsPerSample;
        const double phase = beat * static_cast<double> (s.params[2]) - std::floor (beat * static_cast<double> (s.params[2]));
        double target = 0.0;
        float fader = 1.0f;
        switch (mode)
        {
            case 0: target = depth * static_cast<double> (s.params[1]); break;
            case 1: target = depth * 0.5 * (1.0 - std::cos (2.0 * kPi * phase)); break; // back and forth
            case 2: target = phase < 0.5 ? depth * (1.0 - 2.0 * phase) : 0.0; break;     // pulled back, pushed forward
            case 3:
                target = depth * 0.5 * (1.0 - std::cos (2.0 * kPi * phase));
                fader = phase < 0.5 ? 1.0f : 0.0f; // the fader closes on the way back
                break;
            default: fader = std::fmod (phase * 4.0, 1.0) < 0.5 ? 1.0f : 0.0f; break; // transformer: the fader cuts the sound
        }
        scratchDelay = target + (scratchDelay - target) * smoothing;
        scratchFader += (fader - scratchFader) * 0.02f; // (no clicks)
        delayLine[0][static_cast<size_t> (delayWrite)] = l[i];
        delayLine[1][static_cast<size_t> (delayWrite)] = r[i];
        const float d = static_cast<float> (std::clamp (scratchDelay, 0.0, static_cast<double> (size - 4)));
        const float wetL = (d < 1.0f ? l[i] : readLine (delayLine[0], delayWrite, d)) * scratchFader;
        const float wetR = (d < 1.0f ? r[i] : readLine (delayLine[1], delayWrite, d)) * scratchFader;
        l[i] = l[i] * (1.0f - mix) + wetL * mix;
        r[i] = r[i] * (1.0f - mix) + wetR * mix;
        if (++delayWrite >= size)
            delayWrite = 0;
    }
    ctlBeat = beat;
}

// Patcher: its effects, one after the other (serial) or each on the slot's sound with the results averaged (parallel).
void EffectUnit::runPatcher (const EffectSlot& s, float* l, float* r, int n, const EffectTransport& transport) noexcept
{
    auto* state = s.patcherState.get();
    const auto* chain = s.patch.get();
    if (state == nullptr || chain == nullptr)
        return;
    const bool parallel = effectChoice (EffectType::Patcher, 0, s.params[0]) == 1;
    const float output = dbToGain (s.params[1]);
    const size_t count = std::min (chain->size(), static_cast<size_t> (kPatcherSlots));
    for (int done = 0; done < n; done += PatcherState::kChunk)
    {
        const int m = std::min (PatcherState::kChunk, n - done);
        float* cl = l + done;
        float* cr = r + done;
        auto later = transport;
        if (later.tick >= 0.0)
            later.tick += done * later.ticksPerSample;
        // 0.86.0: an effect whose parameters automation moves in this chunk plays a copy with those values
        const auto withAutomation = [state, &transport] (size_t k, const EffectSlot& sub, EffectSlot& held) -> const EffectSlot&
        {
            if (transport.chunk == 0)
                return sub;
            bool any = false;
            for (size_t p = 0; p < static_cast<size_t> (kMaxEffectParams); ++p)
                if (state->automatedAt[k][p] == transport.chunk)
                {
                    if (! any)
                        held = sub;
                    any = true;
                    const auto info = effectParamInfo (sub.type, static_cast<int> (p));
                    held.params[p] = std::clamp (state->automated[k][p], info.min, info.max);
                }
            return any ? held : sub;
        };
        EffectSlot held;
        if (! parallel)
        {
            for (size_t k = 0; k < count; ++k)
                if ((*chain)[k].type != EffectType::None && state->types[k] == (*chain)[k].type)
                    state->units[k].process (withAutomation (k, (*chain)[k], held), cl, cr, m, later);
        }
        else
        {
            std::copy_n (cl, m, state->dryL.begin());
            std::copy_n (cr, m, state->dryR.begin());
            std::fill_n (state->accL.begin(), m, 0.0f);
            std::fill_n (state->accR.begin(), m, 0.0f);
            int branches = 0;
            for (size_t k = 0; k < count; ++k)
            {
                const auto& sub = (*chain)[k];
                if (sub.type == EffectType::None || sub.bypass || state->types[k] != sub.type)
                    continue;
                std::copy_n (state->dryL.begin(), m, cl);
                std::copy_n (state->dryR.begin(), m, cr);
                state->units[k].process (withAutomation (k, sub, held), cl, cr, m, later);
                for (int i = 0; i < m; ++i)
                {
                    state->accL[static_cast<size_t> (i)] += cl[i];
                    state->accR[static_cast<size_t> (i)] += cr[i];
                }
                ++branches;
            }
            if (branches == 0)
            {
                std::copy_n (state->dryL.begin(), m, cl);
                std::copy_n (state->dryR.begin(), m, cr);
            }
            else
            {
                const float scale = 1.0f / static_cast<float> (branches);
                for (int i = 0; i < m; ++i)
                {
                    cl[i] = state->accL[static_cast<size_t> (i)] * scale;
                    cr[i] = state->accR[static_cast<size_t> (i)] * scale;
                }
            }
        }
        if (std::abs (output - 1.0f) > 1.0e-6f)
            for (int i = 0; i < m; ++i)
            {
                cl[i] *= output;
                cr[i] *= output;
            }
    }
}

void preparePatcher (EffectSlot& slot, double sampleRate, bool fresh)
{
    if (slot.type != EffectType::Patcher)
        return;
    // its own convolvers first (a new list when one of them changes: the old one may still be playing)
    if (slot.patch != nullptr)
    {
        auto copy = *slot.patch;
        bool changed = false;
        for (auto& sub : copy)
            if (sub.type == EffectType::Convolver)
            {
                const auto before = sub.convolver;
                prepareConvolver (sub, sampleRate, fresh);
                changed = changed || sub.convolver != before;
            }
        if (changed)
            slot.patch = std::make_shared<const std::vector<EffectSlot>> (std::move (copy));
    }
    // a state whose units match the effects (a fresh one when they do not: the old one may still be playing)
    const auto* state = slot.patcherState.get();
    bool fits = ! fresh && state != nullptr && std::abs (state->sampleRate - sampleRate) < 0.5;
    for (size_t k = 0; fits && k < static_cast<size_t> (kPatcherSlots); ++k)
    {
        const auto wanted = slot.patch != nullptr && k < slot.patch->size() ? (*slot.patch)[k].type : EffectType::None;
        fits = state->types[k] == wanted;
    }
    if (fits)
        return;
    auto made = std::make_shared<PatcherState>();
    made->sampleRate = sampleRate;
    made->dryL.assign (PatcherState::kChunk, 0.0f);
    made->dryR.assign (PatcherState::kChunk, 0.0f);
    made->accL.assign (PatcherState::kChunk, 0.0f);
    made->accR.assign (PatcherState::kChunk, 0.0f);
    for (size_t k = 0; k < static_cast<size_t> (kPatcherSlots); ++k)
    {
        const auto type = slot.patch != nullptr && k < slot.patch->size() ? (*slot.patch)[k].type : EffectType::None;
        made->types[k] = type;
        made->units[k].prepare (sampleRate);
        if (effectNeedsMemory (type))
        {
            made->memories[k] = std::make_unique<EffectMemory>();
            made->memories[k]->prepare (sampleRate);
            made->units[k].attachMemory (made->memories[k].get());
        }
    }
    slot.patcherState = std::move (made);
}

void prepareConvolver (EffectSlot& slot, double sampleRate, bool fresh)
{
    if (slot.type == EffectType::Patcher) // 0.73.0: the convolvers' preparation is also the Patcher's
    {
        preparePatcher (slot, sampleRate, fresh);
        return;
    }
    if (slot.type != EffectType::Convolver)
        return;
    Convolver::Settings wanted;
    wanted.space = effectChoice (EffectType::Convolver, 0, slot.params[0]);
    wanted.seconds = slot.params[1];
    wanted.brightness = slot.params[2];
    wanted.preDelayMs = slot.params[3];
    wanted.sampleRate = sampleRate;
    if (! fresh && slot.convolver != nullptr && slot.convolver->settings() == wanted)
        return;
    slot.convolver = Convolver::make (wanted, slot.convolver != nullptr ? slot.convolver->file() : nullptr);
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
    if (memory == nullptr)
        return 0.0; // no buffers lent (yet): the sound passes unchanged
    const auto& delayLine = memory->delayLine;
    const double size = static_cast<double> (delayLine[0].size());
    double pos = static_cast<double> (delayWrite) - delay;
    while (pos < 0.0)
        pos += size;
    return pos;
}

void EffectUnit::analysePitch (int range, int key, int scale, float amount, int heldNote) noexcept
{
    if (memory == nullptr)
        return; // no buffers lent (yet): the sound passes unchanged
    auto& preDelay = memory->preDelay;
    auto& pcDecim = memory->pcDecim;
    auto& pcScratch = memory->pcScratch;
    auto& pcDiff = memory->pcDiff;
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
    if (heldNote >= -1)
    {
        // Following a channel: its note, in the octave nearest to what is sung; nothing held, nothing changed.
        if (heldNote < 0)
        {
            pcNote = -1;
            pcTarget = 0.0f;
            return;
        }
        double target = heldNote;
        while (target - midi > 6.0)
            target -= 12.0;
        while (midi - target > 6.0)
            target += 12.0;
        pcNote = static_cast<int> (target);
        pcTarget = static_cast<float> (amount * (target - midi));
        return;
    }
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
    if (memory == nullptr)
        return; // no buffers lent (yet): the sound passes unchanged
    auto& preDelay = memory->preDelay;
    auto& delayLine = memory->delayLine;
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

void EffectUnit::runPitchCorrector (const EffectSlot& slot, float* left, float* right, int n, const EffectTransport& transport) noexcept
{
    if (memory == nullptr)
        return; // no buffers lent (yet): the sound passes unchanged
    auto& preDelay = memory->preDelay;
    auto& delayLine = memory->delayLine;
    auto& pcDecim = memory->pcDecim;
    const auto& p = slot.params;
    const int key = effectChoice (EffectType::PitchCorrector, 0, p[0]);
    const int scale = effectChoice (EffectType::PitchCorrector, 1, p[1]);
    const float speedMs = p[2];
    const float amount = p[3] / 100.0f;
    const float shift = p[4];
    const int range = effectChoice (EffectType::PitchCorrector, 5, p[5]);
    const float mix = p[6] / 100.0f;
    const int followChannel = effectChoice (EffectType::PitchCorrector, 7, p[7]) - 1; // -1: the scale
    int heldNote = -2;
    if (followChannel >= 0)
        heldNote = transport.channelNotes != nullptr && followChannel < transport.numChannelNotes ? transport.channelNotes[followChannel] : -1;
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

        const auto filtered = static_cast<float> (pcAa2.tick (0, pcAa1.tick (0, mono)));
        if (++pcPhase >= pcFactor)
        {
            pcPhase = 0;
            pcDecim[static_cast<size_t> (pcDecimWrite)] = flush (filtered);
            pcDecimWrite = (pcDecimWrite + 1) & (kPitchDecimated - 1);
            if (--pcHopLeft <= 0)
            {
                pcHopLeft = kPitchHop;
                analysePitch (range, key, scale, amount, heldNote);
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
