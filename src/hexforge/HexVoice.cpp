#include "hexforge/HexVoice.h"

namespace hexforge
{
namespace
{
constexpr int kBlock = 32;
inline size_t idx (Source s) noexcept { return static_cast<size_t> (s); }
inline size_t idx (Dest d) noexcept { return static_cast<size_t> (d); }
inline float readTable (const float* t, double phase) noexcept
{
    const double x = phase * kTableSize;
    const int i = static_cast<int> (x);
    const float f = static_cast<float> (x - i);
    return t[i] + f * (t[i + 1] - t[i]);
}
// How far (in cycles) a full-scale modulator moves its target at matrix amount 1 and depth 1.
constexpr double kIndexCycles = 0.9;
} // namespace

void Voice::prepare (double oversampledRate) noexcept
{
    rate = oversampledRate;
    amp.setSampleRate (rate);
    filter.setSampleRate (rate);
    for (auto& e : opEnv)
    {
        e.setSampleRate (rate);
        e.reset();
    }
    amp.reset();
    filter.reset();
    ladderL.reset();
    ladderR.reset();
    note = -1;
    keyDown = false;
}

void Voice::start (int newNote, float newVelocity, double fromNote, bool legato, std::uint32_t seed, const Patch& patch) noexcept
{
    const bool wasSilent = ! amp.isActive();
    note = newNote;
    velocity = std::clamp (newVelocity, 0.0f, 1.0f);
    keyDown = true;
    targetNote = newNote;
    if (legato && ! wasSilent)
    {
        if (patch.glide <= 0.0f)
            currentNote = targetNote;
        return;
    }
    currentNote = fromNote >= 0.0 && patch.glide > 0.0f ? fromNote : targetNote;
    random.seed (seed * 2654435761u + 7u);
    randomValue = random.bipolar();
    if (wasSilent)
    {
        ladderL.reset();
        ladderR.reset();
        for (auto& u : previous)
            u.fill (0.0f);
        // Unison voices start at different phases (one voice: from zero, for a hard, repeatable attack).
        for (size_t u = 0; u < phase.size(); ++u)
            for (auto& p : phase[u])
                p = u == 0 ? 0.0 : static_cast<double> (random.unipolar());
    }
    drop = 1.0;
    amp.trigger();
    filter.trigger();
    for (auto& e : opEnv)
        e.trigger();
}

void Voice::release() noexcept
{
    keyDown = false;
    amp.release();
    filter.release();
    for (auto& e : opEnv)
        e.release();
}

void Voice::kill() noexcept
{
    keyDown = false;
    amp.reset();
    filter.reset();
    for (auto& e : opEnv)
        e.reset();
    note = -1;
}

void Voice::render (const Patch& patch, const Controls& controls, float* left, float* right, int numSamples) noexcept
{
    for (int done = 0; done < numSamples && amp.isActive(); done += kBlock)
        renderBlock (patch, controls, left + done, right + done, std::min (kBlock, numSamples - done));
    if (! amp.isActive())
        note = -1;
}

void Voice::renderBlock (const Patch& patch, const Controls& controls, float* left, float* right, int n) noexcept
{
    const double dt = n / rate;
    amp.set (patch.ampEnv[0], patch.ampEnv[1], patch.ampEnv[2], patch.ampEnv[3]);
    filter.set (patch.filterEnv[0], patch.filterEnv[1], patch.filterEnv[2], patch.filterEnv[3]);
    for (size_t o = 0; o < opEnv.size(); ++o)
        opEnv[o].set (patch.op[o].env[0], patch.op[o].env[1], patch.op[o].env[2], patch.op[o].env[3]);

    // Modulation.
    std::array<float, kNumSources> src {};
    src[idx (Source::FilterLfo)] = controls.filterLfo;
    src[idx (Source::Lfo2)] = controls.lfo2;
    src[idx (Source::FilterEnv)] = filter.current();
    src[idx (Source::Velocity)] = velocity;
    src[idx (Source::ModWheel)] = controls.modWheel;
    src[idx (Source::Aftertouch)] = controls.aftertouch;
    src[idx (Source::KeyTrack)] = static_cast<float> (note - 60) / 60.0f;
    src[idx (Source::Random)] = randomValue;
    for (size_t m = 0; m < static_cast<size_t> (kNumMacros); ++m)
        src[idx (Source::Macro1) + m] = controls.macro[m];
    std::array<float, kNumDests> mod {};
    for (int s = 0; s < patch.matrixCount; ++s)
    {
        const auto& slot = patch.matrix[static_cast<size_t> (s)];
        mod[idx (slot.dest)] += src[idx (slot.source)] * slot.amount;
    }

    // Pitch: glide, bend, the pitch drop (a fast fall at the start of a note, for zaps and punchy basses), the matrix.
    if (patch.glide > 0.0f)
        currentNote = targetNote + (currentNote - targetNote) * std::exp (-dt * 3.0 / patch.glide);
    else
        currentNote = targetNote;
    drop *= std::exp (-dt * 4.6 / std::max (0.005f, patch.dropTime));
    const double pitch = currentNote + 12.0 * patch.octave + controls.bend * patch.bendRange + mod[idx (Dest::Pitch)] * 24.0f + drop * patch.dropAmount;

    // The operators in use: those heard, and those that modulate an operator in use.
    std::array<bool, kNumOps> used {};
    std::array<float, kNumOps> opLevel {};
    for (size_t o = 0; o < static_cast<size_t> (kNumOps); ++o)
    {
        const auto& op = patch.op[o];
        opLevel[o] = std::clamp (op.level + mod[idx (Dest::Op1Level) + o], 0.0f, 1.0f) * (1.0f - op.velocity * (1.0f - velocity));
        used[o] = op.out > 0.0f && opLevel[o] > 0.0f;
    }
    for (int pass = 0; pass < kNumOps; ++pass)
        for (size_t t = 0; t < static_cast<size_t> (kNumOps); ++t)
            if (used[t])
                for (size_t s = 0; s < static_cast<size_t> (kNumOps); ++s)
                    if (patch.fm[t][s] > 0.0f && opLevel[s] > 0.0f)
                        used[s] = true;

    const int voices = std::clamp (patch.unison, 1, kMaxUnison);
    const float detune = std::clamp (patch.detune + mod[idx (Dest::UnisonDetune)], 0.0f, 1.0f);
    const float spreadSemis = 0.05f * detune + 0.6f * detune * detune;
    const double depth = std::clamp (patch.fmDepth + mod[idx (Dest::FmDepth)], 0.0f, 3.0f) * kIndexCycles;

    std::array<std::array<double, kNumOps>, kMaxUnison> inc {};
    std::array<const float*, kNumOps> table {};
    std::array<float, kMaxUnison> gl {}, gr {};
    std::array<std::array<float, kNumOps>, 2> opPan {};
    const auto& waves = HexWaves::get();
    for (size_t o = 0; o < static_cast<size_t> (kNumOps); ++o)
    {
        if (! used[o])
            continue;
        const auto& op = patch.op[o];
        const double base = sunburst::noteToHz (pitch + op.fine / 100.0) * op.ratio / rate;
        for (size_t u = 0; u < static_cast<size_t> (voices); ++u)
        {
            const float offset = voices > 1 ? 2.0f * static_cast<float> (u) / static_cast<float> (voices - 1) - 1.0f : 0.0f;
            inc[u][o] = base * std::exp2 (offset * spreadSemis / 12.0);
        }
        const int level = sunburst::Wavetables::levelFor (base * std::exp2 (spreadSemis / 12.0));
        const float* t0 = op.wave < kNumClassicWaves ? waves.classic (op.wave) : patch.spectral[static_cast<size_t> (op.wave - kNumClassicWaves) & 1u];
        table[o] = t0 != nullptr ? t0 + static_cast<size_t> (level) * kLevelStride : waves.classic (0);
        const float angle = (std::clamp (op.pan, -1.0f, 1.0f) + 1.0f) * static_cast<float> (sunburst::kPi) * 0.25f;
        opPan[0][o] = std::cos (angle) * 1.41421356f;
        opPan[1][o] = std::sin (angle) * 1.41421356f;
    }
    float norm = 0.0f;
    for (size_t u = 0; u < static_cast<size_t> (voices); ++u)
    {
        const float offset = voices > 1 ? 2.0f * static_cast<float> (u) / static_cast<float> (voices - 1) - 1.0f : 0.0f;
        const float angle = (std::clamp (offset * patch.spread, -1.0f, 1.0f) + 1.0f) * static_cast<float> (sunburst::kPi) * 0.25f;
        gl[u] = std::cos (angle) * 1.41421356f;
        gr[u] = std::sin (angle) * 1.41421356f;
        norm += 1.0f;
    }
    norm = 1.0f / std::sqrt (norm);

    std::array<float, kBlock> mixL {}, mixR {};
    for (int i = 0; i < n; ++i)
    {
        std::array<float, kNumOps> env {};
        for (size_t o = 0; o < static_cast<size_t> (kNumOps); ++o)
            env[o] = opEnv[o].next() * opLevel[o];
        float l = 0.0f, r = 0.0f;
        for (size_t u = 0; u < static_cast<size_t> (voices); ++u)
        {
            auto& prev = previous[u];
            auto& ph = phase[u];
            std::array<float, kNumOps> out {};
            float vl = 0.0f, vr = 0.0f;
            for (size_t o = 0; o < static_cast<size_t> (kNumOps); ++o)
            {
                if (! used[o])
                    continue;
                // Phase modulation from every operator (and from itself: feedback), each one sample behind.
                double pm = 0.0;
                for (size_t s = 0; s < static_cast<size_t> (kNumOps); ++s)
                    pm += static_cast<double> (patch.fm[o][s] * prev[s]);
                double p = ph[o] + pm * depth;
                p -= std::floor (p);
                out[o] = readTable (table[o], p) * env[o];
                ph[o] += inc[u][o];
                if (ph[o] >= 1.0)
                    ph[o] -= 1.0;
                const float heard = out[o] * patch.op[o].out;
                vl += heard * opPan[0][o];
                vr += heard * opPan[1][o];
            }
            prev = out;
            l += vl * gl[u];
            r += vr * gr[u];
        }
        mixL[static_cast<size_t> (i)] = l * norm;
        mixR[static_cast<size_t> (i)] = r * norm;
    }

    if (patch.filterOn)
    {
        const double semis = 12.0 * std::log2 (std::max (20.0f, patch.cutoff)) + patch.filterEnvAmount * 84.0f * filter.current()
                           + patch.filterKey * static_cast<float> (note - 60) + patch.filterVelocity * (velocity - 0.5f) * 36.0f
                           + patch.filterLfoAmount * controls.filterLfo * 48.0f + mod[idx (Dest::Cutoff)] * 72.0f;
        const double hz = std::clamp (std::exp2 (semis / 12.0), 20.0, 20000.0);
        const float res = std::clamp (patch.resonance + mod[idx (Dest::Resonance)], 0.0f, 1.0f);
        const float drv = std::clamp (patch.drive + mod[idx (Dest::Drive)], 0.0f, 1.0f);
        ladderL.set (patch.mode, hz, rate, res, drv);
        ladderR.set (patch.mode, hz, rate, res, drv);
        for (int i = 0; i < n; ++i)
        {
            mixL[static_cast<size_t> (i)] = ladderL.process (mixL[static_cast<size_t> (i)]);
            mixR[static_cast<size_t> (i)] = ladderR.process (mixR[static_cast<size_t> (i)]);
        }
    }

    const float velocityGain = 1.0f - patch.ampVelocity * (1.0f - velocity);
    const float ampMod = std::clamp (1.0f + mod[idx (Dest::Amp)], 0.0f, 2.0f);
    const float pan = std::clamp (mod[idx (Dest::Pan)], -1.0f, 1.0f);
    const float balL = pan > 0.0f ? 1.0f - pan : 1.0f, balR = pan < 0.0f ? 1.0f + pan : 1.0f;
    const float gain = 0.5f * velocityGain * ampMod;
    for (int i = 0; i < n; ++i)
    {
        const float a = amp.next();
        filter.next();
        left[i] += mixL[static_cast<size_t> (i)] * a * gain * balL;
        right[i] += mixR[static_cast<size_t> (i)] * a * gain * balR;
    }
}
} // namespace hexforge
