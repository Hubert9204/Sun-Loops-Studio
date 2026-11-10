#include "sunburst/Voice.h"

namespace sunburst
{
namespace
{
constexpr int kBlock = 32; // modulation is computed every 32 samples

inline size_t idx (Source s) noexcept { return static_cast<size_t> (s); }
inline size_t idx (Dest d) noexcept { return static_cast<size_t> (d); }

// The spread of the unison voices in semitones at detune 0..1: gentle at first, wide (a "super saw") at the top.
inline float unisonSemitones (float detune) noexcept { return 0.06f * detune + 0.7f * detune * detune; }
} // namespace

void Voice::prepare (double sampleRate) noexcept
{
    rate = sampleRate;
    for (auto* e : { &ampEnv, &filterEnv, &modEnv })
    {
        e->setSampleRate (sampleRate);
        e->reset();
    }
    filterL.reset();
    filterR.reset();
    note = -1;
    keyDown = false;
}

void Voice::start (int newNote, float newVelocity, double fromNote, bool legato, std::uint32_t seed, const Patch& patch) noexcept
{
    const bool wasSilent = ! ampEnv.isActive();
    note = newNote;
    velocity = std::clamp (newVelocity, 0.0f, 1.0f);
    keyDown = true;
    targetNote = newNote;
    if (legato && ! wasSilent)
    {
        if (patch.glide <= 0.0f)
            currentNote = targetNote;
        return; // the envelopes and LFOs go on
    }
    currentNote = fromNote >= 0.0 && patch.glide > 0.0f ? fromNote : targetNote;

    noise.seed (seed * 2654435761u + 1u);
    randomValue = noise.bipolar();
    if (wasSilent)
    {
        filterL.reset();
        filterR.reset();
        noiseState = 0.0f;
        for (size_t o = 0; o < phases.size(); ++o)
            for (auto& p : phases[o])
                p = patch.osc[o].randomPhase ? static_cast<double> (noise.unipolar()) : 0.0;
        subPhase = 0.0;
    }
    for (size_t l = 0; l < lfos.size(); ++l)
        if (patch.lfo[l].retrigger)
            lfos[l].reset (0.0f, seed + static_cast<std::uint32_t> (l) * 7919u);
    secondsSinceStart = 0.0;
    ampEnv.trigger();
    filterEnv.trigger();
    modEnv.trigger();
}

void Voice::release() noexcept
{
    keyDown = false;
    ampEnv.release();
    filterEnv.release();
    modEnv.release();
}

void Voice::kill() noexcept
{
    keyDown = false;
    ampEnv.reset();
    filterEnv.reset();
    modEnv.reset();
    note = -1;
}

void Voice::render (const Patch& patch, const Controls& controls, float* left, float* right, int numSamples) noexcept
{
    for (int done = 0; done < numSamples && ampEnv.isActive(); done += kBlock)
        renderBlock (patch, controls, left + done, right + done, std::min (kBlock, numSamples - done));
    if (! ampEnv.isActive())
        note = -1;
}

void Voice::renderBlock (const Patch& patch, const Controls& controls, float* left, float* right, int n) noexcept
{
    const double dt = n / rate;
    const auto& e = patch.env;
    ampEnv.set (e[0][0], e[0][1], e[0][2], e[0][3]);
    filterEnv.set (e[1][0], e[1][1], e[1][2], e[1][3]);
    modEnv.set (e[2][0], e[2][1], e[2][2], e[2][3]);

    // Modulation sources.
    std::array<float, kNumSources> src {};
    for (size_t l = 0; l < static_cast<size_t> (kNumLfos); ++l)
    {
        const auto& lp = patch.lfo[l];
        float v = 0.0f;
        if (lp.retrigger)
        {
            // Rate modulation from the matrix: last block's value (an LFO can modulate another one's rate without a loop).
            v = lfos[l].advance (lp.hz * std::exp2 (3.0 * lastRateMod[l]), dt, lp.shape);
        }
        else
            v = controls.globalLfo[l];
        if (lp.unipolar)
            v = 0.5f * (v + 1.0f);
        if (lp.fade > 0.0f)
            v *= static_cast<float> (std::min (1.0, secondsSinceStart / lp.fade));
        src[idx (Source::Lfo1) + l] = v;
    }
    src[idx (Source::Env2)] = filterEnv.current();
    src[idx (Source::Env3)] = modEnv.current();
    src[idx (Source::Velocity)] = velocity;
    src[idx (Source::ModWheel)] = controls.modWheel;
    src[idx (Source::Aftertouch)] = controls.aftertouch;
    src[idx (Source::PitchBend)] = controls.bend;
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
    for (size_t l = 0; l < static_cast<size_t> (kNumLfos); ++l)
        lastRateMod[l] = std::clamp (mod[idx (Dest::Lfo1Rate) + l], -2.0f, 2.0f);

    // Pitch: glide, bend, the mod envelope, the matrix.
    if (patch.glide > 0.0f)
        currentNote = targetNote + (currentNote - targetNote) * std::exp (-dt * 3.0 / patch.glide);
    else
        currentNote = targetNote;
    const double basePitch = currentNote + controls.bend * patch.bendRange + patch.pitchEnv * modEnv.current() + mod[idx (Dest::AllPitch)] * 24.0f;

    std::array<float, kBlock> mixL {}, mixR {};
    const auto& tables = Wavetables::get();

    for (size_t o = 0; o < static_cast<size_t> (kNumOscs); ++o)
    {
        const auto& op = patch.osc[o];
        const size_t base = idx (Dest::Osc1Pitch) + o * 6; // pitch, position, width, level, detune, pan
        const float level = std::clamp (op.level + mod[base + 3], 0.0f, 1.0f);
        if (! op.on || level <= 0.0f)
            continue;

        const int voices = op.wave == Wave::Noise ? 1 : std::clamp (op.voices, 1, kMaxUnison);
        const float detune = std::clamp (op.detune + mod[base + 4], 0.0f, 1.0f);
        const float spreadSemis = unisonSemitones (detune);
        const double inc = noteToHz (basePitch + op.tune + mod[base] * 24.0f) / rate;
        const float position = std::clamp (op.position + mod[base + 1], 0.0f, 1.0f);
        const float width = std::clamp (op.width + mod[base + 2] * 0.45f, 0.05f, 0.95f);
        const float pan = op.pan + mod[base + 5];
        const int mip = Wavetables::levelFor (inc * std::exp2 (spreadSemis / 12.0));

        // Unison weights: the centre is loudest at blend 0, all voices equal at blend 1.
        std::array<float, kMaxUnison> u {}, w {};
        float sumSquares = 0.0f;
        for (int k = 0; k < voices; ++k)
        {
            const auto kk = static_cast<size_t> (k);
            u[kk] = voices > 1 ? 2.0f * static_cast<float> (k) / static_cast<float> (voices - 1) - 1.0f : 0.0f;
            w[kk] = 1.0f - (1.0f - op.blend) * std::abs (u[kk]) * 0.85f;
            sumSquares += w[kk] * w[kk];
        }
        const float norm = level / std::sqrt (std::max (sumSquares, 1.0e-6f));

        for (int k = 0; k < voices; ++k)
        {
            const auto kk = static_cast<size_t> (k);
            const double vInc = inc * std::exp2 (u[kk] * spreadSemis / 12.0);
            const float angle = (std::clamp (pan + u[kk] * op.spread, -1.0f, 1.0f) + 1.0f) * static_cast<float> (kPi) * 0.25f;
            const float gl = std::cos (angle) * w[kk] * norm * 1.41421356f, gr = std::sin (angle) * w[kk] * norm * 1.41421356f;
            double ph = phases[o][kk];
            for (int i = 0; i < n; ++i)
            {
                float v = 0.0f;
                switch (op.wave)
                {
                    case Wave::Sine: v = readTable (tables.sine(), ph); break;
                    case Wave::Triangle: v = readTable (tables.triangle (mip), ph); break;
                    case Wave::Saw: v = static_cast<float> (2.0 * ph - 1.0) - polyBlep (ph, vInc); break;
                    case Wave::Square:
                    {
                        double p2 = ph + 1.0 - width;
                        if (p2 >= 1.0)
                            p2 -= 1.0;
                        v = (ph < width ? 1.0f : -1.0f) + polyBlep (ph, vInc) - polyBlep (p2, vInc) - (2.0f * width - 1.0f);
                        break;
                    }
                    case Wave::Noise: v = noise.bipolar(); break;
                    case Wave::Wavetable: v = tables.sample (op.table, position, ph, mip); break;
                }
                ph += vInc;
                if (ph >= 1.0)
                    ph -= 1.0;
                mixL[static_cast<size_t> (i)] += v * gl;
                mixR[static_cast<size_t> (i)] += v * gr;
            }
            phases[o][kk] = ph;
        }
    }

    // Sub oscillator and noise (centred).
    const float subLevel = std::clamp (patch.subLevel + mod[idx (Dest::SubLevel)], 0.0f, 1.0f);
    const float noiseLevel = std::clamp (patch.noiseLevel + mod[idx (Dest::NoiseLevel)], 0.0f, 1.0f);
    if (subLevel > 0.0f || noiseLevel > 0.0f)
    {
        const double subInc = noteToHz (basePitch - 12.0 * patch.subOctaves) / rate;
        const float colour = std::clamp (patch.noiseColour, 0.0f, 1.0f) * 0.97f;
        const float noiseGain = noiseLevel * (1.0f + 3.0f * colour);
        for (int i = 0; i < n; ++i)
        {
            float v = 0.0f;
            if (subLevel > 0.0f)
            {
                v += subLevel * (patch.subSquare ? (subPhase < 0.5 ? 1.0f : -1.0f) + polyBlep (subPhase, subInc) - polyBlep (std::fmod (subPhase + 0.5, 1.0), subInc)
                                                 : readTable (tables.sine(), subPhase));
                subPhase += subInc;
                if (subPhase >= 1.0)
                    subPhase -= 1.0;
            }
            if (noiseLevel > 0.0f)
            {
                noiseState = noiseState * colour + (1.0f - colour) * noise.bipolar();
                v += noiseState * noiseGain;
            }
            mixL[static_cast<size_t> (i)] += v;
            mixR[static_cast<size_t> (i)] += v;
        }
    }

    // Filter.
    if (patch.filterOn)
    {
        const double semis = 12.0 * std::log2 (std::max (20.0f, patch.cutoff)) + patch.filterEnv * 96.0f * filterEnv.current()
                           + patch.filterKey * static_cast<float> (note - 60) + patch.filterVelocity * (velocity - 0.5f) * 36.0f
                           + mod[idx (Dest::Cutoff)] * 72.0f;
        const double hz = std::clamp (std::exp2 (semis / 12.0), 20.0, std::min (20000.0, rate * 0.45));
        const float res = std::clamp (patch.resonance + mod[idx (Dest::Resonance)], 0.0f, 1.0f);
        const float drive = std::clamp (patch.filterDrive + mod[idx (Dest::Drive)], 0.0f, 1.0f);
        filterL.set (patch.filterType, hz, rate, res, drive);
        filterR.set (patch.filterType, hz, rate, res, drive);
        for (int i = 0; i < n; ++i)
        {
            mixL[static_cast<size_t> (i)] = filterL.process (mixL[static_cast<size_t> (i)]);
            mixR[static_cast<size_t> (i)] = filterR.process (mixR[static_cast<size_t> (i)]);
        }
    }

    // Amplifier and pan.
    const float velocityGain = 1.0f - patch.ampVelocity * (1.0f - velocity);
    const float ampMod = std::clamp (1.0f + mod[idx (Dest::Amp)], 0.0f, 2.0f);
    const float p = std::clamp (mod[idx (Dest::Pan)], -1.0f, 1.0f);
    const float balL = p > 0.0f ? 1.0f - p : 1.0f, balR = p < 0.0f ? 1.0f + p : 1.0f;
    const float gain = 0.5f * velocityGain * ampMod;
    for (int i = 0; i < n; ++i)
    {
        const float env = ampEnv.next();
        filterEnv.next();
        modEnv.next();
        left[i] += mixL[static_cast<size_t> (i)] * env * gain * balL;
        right[i] += mixR[static_cast<size_t> (i)] * env * gain * balR;
    }
    secondsSinceStart += dt;
}
} // namespace sunburst
