#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "audio/SampleData.h"
#include "audio/SamplerProgram.h"

namespace sls
{
// The sound of a synth channel (0.18.0, block F). The "Classic" engine is the original sine with two harmonics, so projects made before
// 0.18.0 sound the same; the "Subtractive" engine is a two-oscillator synth with noise, a filter with its own envelope, an amplitude
// envelope and an LFO. The "FM" engine (0.19.0) has four sine operators, each with its frequency ratio, level and envelope, and a
// modulation matrix (who modulates whom, the diagonal being feedback) with an output level per operator, like a small Sytrus.
// Plain C++: the Player reads it from the channel of the snapshot, the UI edits it, the files store it.

// Exact equality of two stored settings (bit for bit: what was saved is what is read back).
inline bool sameValue (float a, float b) noexcept { return std::memcmp (&a, &b, sizeof (float)) == 0; }

enum class SynthEngine : std::uint8_t { Classic, Subtractive, FM };
enum class Waveform : std::uint8_t { Sine, Triangle, Saw, Square };
enum class LfoTarget : std::uint8_t { Off, Pitch, Cutoff, Amp, PulseWidth };

struct Adsr
{
    float attack = 0.005f; // seconds, 0..10
    float decay = 0.3f;    // seconds, 0..10
    float sustain = 0.8f;  // level, 0..1
    float release = 0.2f;  // seconds, 0..10

    Adsr sanitised (const Adsr& defaults) const noexcept
    {
        Adsr a = *this;
        a.attack = std::clamp (finiteOr (a.attack, defaults.attack), 0.0f, 10.0f);
        a.decay = std::clamp (finiteOr (a.decay, defaults.decay), 0.0f, 10.0f);
        a.sustain = std::clamp (finiteOr (a.sustain, defaults.sustain), 0.0f, 1.0f);
        a.release = std::clamp (finiteOr (a.release, defaults.release), 0.0f, 10.0f);
        return a;
    }
    bool operator== (const Adsr& o) const noexcept
    {
        return sameValue (attack, o.attack) && sameValue (decay, o.decay) && sameValue (sustain, o.sustain) && sameValue (release, o.release);
    }
};

struct SynthOsc
{
    Waveform wave = Waveform::Saw;
    float level = 1.0f;      // 0..1
    int octave = 0;          // -3..+3
    int semitones = 0;       // -12..+12
    float fine = 0.0f;       // cents, -100..+100
    float pulseWidth = 0.5f; // Square only, 0.05..0.95

    double ratio() const noexcept { return std::pow (2.0, (octave * 12 + semitones + static_cast<double> (fine) / 100.0) / 12.0); }
    bool operator== (const SynthOsc& o) const noexcept
    {
        return wave == o.wave && sameValue (level, o.level) && octave == o.octave && semitones == o.semitones && sameValue (fine, o.fine)
            && sameValue (pulseWidth, o.pulseWidth);
    }
};

constexpr int kFmOperators = 4;

struct FmOperator
{
    float ratio = 1.0f;  // frequency / note frequency, 0.125..32
    float level = 1.0f;  // 0..1
    Adsr env { 0.002f, 0.6f, 0.7f, 0.3f };

    bool operator== (const FmOperator& o) const noexcept { return sameValue (ratio, o.ratio) && sameValue (level, o.level) && env == o.env; }
};

// The FM matrix: fm[target][source] is how strongly operator `source` modulates operator `target` (0..1; 1 = about 6 radians of phase
// modulation); fm[i][i] is operator i's feedback. out[i] is how much of operator i is heard.
struct FmMatrix
{
    std::array<std::array<float, kFmOperators>, kFmOperators> fm {};
    std::array<float, kFmOperators> out { 1.0f, 0.0f, 0.0f, 0.0f };

    bool operator== (const FmMatrix& o) const noexcept
    {
        for (int t = 0; t < kFmOperators; ++t)
        {
            if (! sameValue (out[static_cast<size_t> (t)], o.out[static_cast<size_t> (t)]))
                return false;
            for (int s = 0; s < kFmOperators; ++s)
                if (! sameValue (fm[static_cast<size_t> (t)][static_cast<size_t> (s)], o.fm[static_cast<size_t> (t)][static_cast<size_t> (s)]))
                    return false;
        }
        return true;
    }
};

// Classic operator arrangements: which operators modulate which (at an amount of 0.5) and which are heard.
struct FmAlgorithm
{
    const char* name;
    FmMatrix matrix;
};

inline const std::vector<FmAlgorithm>& fmAlgorithms()
{
    static const std::vector<FmAlgorithm> list = []
    {
        const auto make = [] (std::initializer_list<std::pair<int, int>> links, std::initializer_list<int> carriers)
        {
            FmMatrix m;
            m.out = {};
            for (const auto& [target, source] : links)
                m.fm[static_cast<size_t> (target)][static_cast<size_t> (source)] = 0.5f;
            for (const int c : carriers)
                m.out[static_cast<size_t> (c)] = 1.0f;
            return m;
        };
        return std::vector<FmAlgorithm> {
            { "2 > 1", make ({ { 0, 1 } }, { 0 }) },
            { "3 > 2 > 1", make ({ { 0, 1 }, { 1, 2 } }, { 0 }) },
            { "4 > 3 > 2 > 1", make ({ { 0, 1 }, { 1, 2 }, { 2, 3 } }, { 0 }) },
            { "2 > 1 and 4 > 3", make ({ { 0, 1 }, { 2, 3 } }, { 0, 2 }) },
            { "2 + 3 + 4 > 1", make ({ { 0, 1 }, { 0, 2 }, { 0, 3 } }, { 0 }) },
            { "4 > 1, 2, 3", make ({ { 0, 3 }, { 1, 3 }, { 2, 3 } }, { 0, 1, 2 }) },
            { "1 with feedback", make ({ { 0, 0 } }, { 0 }) },
            { "All heard (organ)", make ({}, { 0, 1, 2, 3 }) },
        };
    }();
    return list;
}

struct SynthPatch
{
    SynthEngine engine = SynthEngine::Classic;
    std::array<SynthOsc, 2> osc { SynthOsc { Waveform::Saw, 1.0f, 0, 0, 0.0f, 0.5f }, SynthOsc { Waveform::Square, 0.0f, 0, 0, 7.0f, 0.5f } };
    float noise = 0.0f;       // 0..1

    SamplerFilter filter = SamplerFilter::LowPass;
    bool steep = false;       // 24 dB per octave (two filters in a row) instead of 12
    float cutoff = 0.75f;     // 0..1, logarithmic from 20 Hz to 20 kHz (SamplerSettings::cutoffHz)
    float resonance = 0.2f;   // 0..1
    float envAmount = 0.3f;   // -1..+1: how far the filter envelope moves the cutoff (1 = up 6 octaves)
    float keyTrack = 0.5f;    // 0..1: the cutoff follows the key (1 = an octave per octave, from C4)

    Adsr amp { 0.005f, 0.3f, 0.8f, 0.2f };
    Adsr filterEnv { 0.005f, 0.4f, 0.2f, 0.3f };

    Waveform lfoWave = Waveform::Sine; // Sine, Triangle, Saw or Square
    float lfoRate = 5.0f;              // Hz, 0.05..20
    LfoTarget lfoTarget = LfoTarget::Off;
    float lfoDepth = 0.3f;             // 0..1 (pitch: up to 2 semitones; cutoff: 3 octaves; amp: full tremolo; pulse width: 0.4)

    float gain = 0.8f;                 // 0..1.5

    // FM engine (0.19.0). The amplitude envelope (`amp`), the LFO (pitch or level) and `gain` apply to it too; the filter does not.
    std::array<FmOperator, kFmOperators> op { FmOperator { 1.0f, 1.0f, { 0.002f, 0.6f, 0.7f, 0.3f } },
                                              FmOperator { 1.0f, 0.8f, { 0.002f, 0.4f, 0.3f, 0.3f } },
                                              FmOperator { 2.0f, 0.0f, { 0.002f, 0.6f, 0.7f, 0.3f } },
                                              FmOperator { 3.0f, 0.0f, { 0.002f, 0.6f, 0.7f, 0.3f } } };
    FmMatrix matrix = fmAlgorithms().front().matrix;

    SynthPatch sanitised() const noexcept
    {
        const SynthPatch d;
        SynthPatch p = *this;
        if (static_cast<int> (p.engine) > static_cast<int> (SynthEngine::FM))
            p.engine = SynthEngine::Classic;
        for (auto& o : p.osc)
        {
            if (static_cast<int> (o.wave) > static_cast<int> (Waveform::Square))
                o.wave = Waveform::Saw;
            o.level = std::clamp (finiteOr (o.level, 1.0f), 0.0f, 1.0f);
            o.octave = std::clamp (o.octave, -3, 3);
            o.semitones = std::clamp (o.semitones, -12, 12);
            o.fine = std::clamp (finiteOr (o.fine, 0.0f), -100.0f, 100.0f);
            o.pulseWidth = std::clamp (finiteOr (o.pulseWidth, 0.5f), 0.05f, 0.95f);
        }
        p.noise = std::clamp (finiteOr (p.noise, 0.0f), 0.0f, 1.0f);
        if (static_cast<int> (p.filter) > static_cast<int> (SamplerFilter::BandPass))
            p.filter = SamplerFilter::LowPass;
        p.cutoff = std::clamp (finiteOr (p.cutoff, d.cutoff), 0.0f, 1.0f);
        p.resonance = std::clamp (finiteOr (p.resonance, d.resonance), 0.0f, 1.0f);
        p.envAmount = std::clamp (finiteOr (p.envAmount, d.envAmount), -1.0f, 1.0f);
        p.keyTrack = std::clamp (finiteOr (p.keyTrack, d.keyTrack), 0.0f, 1.0f);
        p.amp = p.amp.sanitised (d.amp);
        p.filterEnv = p.filterEnv.sanitised (d.filterEnv);
        if (static_cast<int> (p.lfoWave) > static_cast<int> (Waveform::Square))
            p.lfoWave = Waveform::Sine;
        p.lfoRate = std::clamp (finiteOr (p.lfoRate, d.lfoRate), 0.05f, 20.0f);
        if (static_cast<int> (p.lfoTarget) > static_cast<int> (LfoTarget::PulseWidth))
            p.lfoTarget = LfoTarget::Off;
        p.lfoDepth = std::clamp (finiteOr (p.lfoDepth, d.lfoDepth), 0.0f, 1.0f);
        p.gain = std::clamp (finiteOr (p.gain, d.gain), 0.0f, 1.5f);
        for (size_t i = 0; i < p.op.size(); ++i)
        {
            auto& o = p.op[i];
            o.ratio = std::clamp (finiteOr (o.ratio, d.op[i].ratio), 0.125f, 32.0f);
            o.level = std::clamp (finiteOr (o.level, d.op[i].level), 0.0f, 1.0f);
            o.env = o.env.sanitised (d.op[i].env);
            p.matrix.out[i] = std::clamp (finiteOr (p.matrix.out[i], 0.0f), 0.0f, 1.0f);
            for (auto& m : p.matrix.fm[i])
                m = std::clamp (finiteOr (m, 0.0f), 0.0f, 1.0f);
        }
        return p;
    }

    bool operator== (const SynthPatch& o) const noexcept
    {
        return engine == o.engine && osc == o.osc && sameValue (noise, o.noise) && filter == o.filter && steep == o.steep
            && sameValue (cutoff, o.cutoff) && sameValue (resonance, o.resonance) && sameValue (envAmount, o.envAmount)
            && sameValue (keyTrack, o.keyTrack) && amp == o.amp && filterEnv == o.filterEnv && lfoWave == o.lfoWave
            && sameValue (lfoRate, o.lfoRate) && lfoTarget == o.lfoTarget && sameValue (lfoDepth, o.lfoDepth) && sameValue (gain, o.gain)
            && op == o.op && matrix == o.matrix;
    }
};

// Ready-made sounds for the synth window's menu: the first is "Classic" (the original sound), the others use the subtractive engine.
struct NamedSynthPatch
{
    std::string name;
    SynthPatch patch;
};

inline const std::vector<NamedSynthPatch>& synthFactoryPatches()
{
    static const std::vector<NamedSynthPatch> list = []
    {
        std::vector<NamedSynthPatch> v;
        v.push_back ({ "Classic (sine)", SynthPatch {} });

        const auto sub = []
        {
            SynthPatch p;
            p.engine = SynthEngine::Subtractive;
            return p;
        };

        auto lead = sub();
        lead.osc[1] = { Waveform::Saw, 0.7f, 0, 0, 9.0f, 0.5f };
        lead.cutoff = 0.72f;
        lead.resonance = 0.25f;
        lead.envAmount = 0.25f;
        lead.amp = { 0.005f, 0.2f, 0.85f, 0.25f };
        lead.lfoTarget = LfoTarget::Pitch;
        lead.lfoRate = 5.5f;
        lead.lfoDepth = 0.12f;
        v.push_back ({ "Saw Lead", lead });

        auto bass = sub();
        bass.osc[0] = { Waveform::Square, 1.0f, -1, 0, 0.0f, 0.5f };
        bass.osc[1] = { Waveform::Saw, 0.6f, -1, 0, -6.0f, 0.5f };
        bass.steep = true;
        bass.cutoff = 0.45f;
        bass.resonance = 0.2f;
        bass.envAmount = 0.35f;
        bass.keyTrack = 0.3f;
        bass.amp = { 0.003f, 0.25f, 0.7f, 0.12f };
        bass.filterEnv = { 0.002f, 0.25f, 0.1f, 0.15f };
        v.push_back ({ "Square Bass", bass });

        auto acid = sub();
        acid.osc[0] = { Waveform::Saw, 1.0f, -1, 0, 0.0f, 0.5f };
        acid.osc[1].level = 0.0f;
        acid.steep = true;
        acid.cutoff = 0.32f;
        acid.resonance = 0.8f;
        acid.envAmount = 0.6f;
        acid.keyTrack = 0.2f;
        acid.amp = { 0.002f, 0.3f, 0.6f, 0.08f };
        acid.filterEnv = { 0.001f, 0.18f, 0.0f, 0.1f };
        v.push_back ({ "Acid Bass", acid });

        auto pad = sub();
        pad.osc[0] = { Waveform::Saw, 0.8f, 0, 0, -8.0f, 0.5f };
        pad.osc[1] = { Waveform::Saw, 0.8f, 0, 0, 8.0f, 0.5f };
        pad.noise = 0.04f;
        pad.cutoff = 0.6f;
        pad.resonance = 0.1f;
        pad.envAmount = 0.15f;
        pad.amp = { 0.6f, 1.0f, 0.8f, 1.2f };
        pad.filterEnv = { 0.9f, 1.5f, 0.5f, 1.2f };
        pad.lfoTarget = LfoTarget::Cutoff;
        pad.lfoRate = 0.3f;
        pad.lfoDepth = 0.2f;
        pad.gain = 0.6f;
        v.push_back ({ "Soft Pad", pad });

        auto pluck = sub();
        pluck.osc[0] = { Waveform::Square, 1.0f, 0, 0, 0.0f, 0.3f };
        pluck.osc[1] = { Waveform::Triangle, 0.6f, 1, 0, 0.0f, 0.5f };
        pluck.cutoff = 0.4f;
        pluck.resonance = 0.3f;
        pluck.envAmount = 0.55f;
        pluck.amp = { 0.001f, 0.35f, 0.0f, 0.2f };
        pluck.filterEnv = { 0.001f, 0.2f, 0.0f, 0.2f };
        v.push_back ({ "Pluck", pluck });

        auto brass = sub();
        brass.osc[0] = { Waveform::Saw, 1.0f, 0, 0, 0.0f, 0.5f };
        brass.osc[1] = { Waveform::Saw, 0.8f, 0, 0, 5.0f, 0.5f };
        brass.cutoff = 0.45f;
        brass.resonance = 0.15f;
        brass.envAmount = 0.4f;
        brass.amp = { 0.06f, 0.3f, 0.8f, 0.25f };
        brass.filterEnv = { 0.08f, 0.4f, 0.4f, 0.3f };
        v.push_back ({ "Brass", brass });

        auto sub808 = sub();
        sub808.osc[0] = { Waveform::Sine, 1.0f, -1, 0, 0.0f, 0.5f };
        sub808.osc[1] = { Waveform::Triangle, 0.3f, 0, 0, 0.0f, 0.5f };
        sub808.filter = SamplerFilter::Off;
        sub808.amp = { 0.003f, 0.8f, 0.0f, 0.3f };
        sub808.gain = 1.0f;
        v.push_back ({ "808 Sub", sub808 });

        auto organ = sub();
        organ.osc[0] = { Waveform::Sine, 1.0f, 0, 0, 0.0f, 0.5f };
        organ.osc[1] = { Waveform::Sine, 0.6f, 1, 0, 0.0f, 0.5f };
        organ.noise = 0.0f;
        organ.filter = SamplerFilter::Off;
        organ.amp = { 0.01f, 0.1f, 1.0f, 0.08f };
        organ.lfoTarget = LfoTarget::Amp;
        organ.lfoRate = 6.0f;
        organ.lfoDepth = 0.15f;
        v.push_back ({ "Organ", organ });

        auto pwm = sub();
        pwm.osc[0] = { Waveform::Square, 1.0f, 0, 0, 0.0f, 0.5f };
        pwm.osc[1].level = 0.0f;
        pwm.cutoff = 0.65f;
        pwm.envAmount = 0.1f;
        pwm.amp = { 0.02f, 0.4f, 0.8f, 0.4f };
        pwm.lfoTarget = LfoTarget::PulseWidth;
        pwm.lfoRate = 0.8f;
        pwm.lfoDepth = 0.8f;
        v.push_back ({ "PWM Strings", pwm });

        // FM sounds (0.19.0).
        const auto fm = [] (int algorithm)
        {
            SynthPatch p;
            p.engine = SynthEngine::FM;
            p.matrix = fmAlgorithms()[static_cast<size_t> (algorithm)].matrix;
            p.amp = { 0.002f, 0.3f, 1.0f, 0.3f };
            return p;
        };

        auto ep = fm (3); // two pairs: a soft tine and a bell-like attack
        ep.op[0] = { 1.0f, 1.0f, { 0.002f, 1.8f, 0.3f, 0.4f } };
        ep.op[1] = { 1.0f, 0.6f, { 0.002f, 1.2f, 0.15f, 0.4f } };
        ep.op[2] = { 1.0f, 0.5f, { 0.002f, 0.6f, 0.0f, 0.3f } };
        ep.op[3] = { 14.0f, 0.5f, { 0.001f, 0.15f, 0.0f, 0.1f } };
        ep.matrix.fm[0][1] = 0.35f;
        ep.matrix.fm[2][3] = 0.3f;
        ep.matrix.out = { 1.0f, 0.0f, 0.35f, 0.0f };
        ep.amp = { 0.002f, 0.2f, 1.0f, 0.5f };
        v.push_back ({ "FM E-Piano", ep });

        auto bell = fm (0);
        bell.op[0] = { 1.0f, 1.0f, { 0.001f, 3.0f, 0.0f, 1.5f } };
        bell.op[1] = { 3.5f, 0.8f, { 0.001f, 2.0f, 0.0f, 1.5f } };
        bell.matrix.fm[0][1] = 0.6f;
        bell.amp = { 0.001f, 0.1f, 1.0f, 1.5f };
        v.push_back ({ "FM Bell", bell });

        auto fmBass = fm (1);
        fmBass.op[0] = { 0.5f, 1.0f, { 0.001f, 0.6f, 0.6f, 0.15f } };
        fmBass.op[1] = { 0.5f, 0.9f, { 0.001f, 0.25f, 0.2f, 0.15f } };
        fmBass.op[2] = { 1.0f, 0.6f, { 0.001f, 0.12f, 0.0f, 0.1f } };
        fmBass.matrix.fm[0][1] = 0.5f;
        fmBass.matrix.fm[1][2] = 0.4f;
        v.push_back ({ "FM Bass", fmBass });

        auto fmBrass = fm (4);
        fmBrass.op[0] = { 1.0f, 1.0f, { 0.04f, 0.3f, 0.85f, 0.2f } };
        fmBrass.op[1] = { 1.0f, 0.7f, { 0.08f, 0.4f, 0.6f, 0.2f } };
        fmBrass.op[2] = { 2.0f, 0.3f, { 0.08f, 0.4f, 0.4f, 0.2f } };
        fmBrass.op[3] = { 1.0f, 0.4f, { 0.05f, 0.3f, 0.5f, 0.2f } };
        fmBrass.matrix.fm[0][0] = 0.15f; // a little feedback for the buzz
        v.push_back ({ "FM Brass", fmBrass });

        auto drawbar = fm (7);
        drawbar.op[0] = { 1.0f, 1.0f, { 0.005f, 0.1f, 1.0f, 0.06f } };
        drawbar.op[1] = { 2.0f, 0.7f, { 0.005f, 0.1f, 1.0f, 0.06f } };
        drawbar.op[2] = { 3.0f, 0.5f, { 0.005f, 0.1f, 1.0f, 0.06f } };
        drawbar.op[3] = { 4.0f, 0.35f, { 0.005f, 0.1f, 1.0f, 0.06f } };
        drawbar.matrix.out = { 0.7f, 0.7f, 0.7f, 0.7f };
        drawbar.amp = { 0.005f, 0.1f, 1.0f, 0.08f };
        v.push_back ({ "FM Drawbars", drawbar });

        auto growl = fm (6);
        growl.op[0] = { 1.0f, 1.0f, { 0.002f, 0.5f, 0.7f, 0.2f } };
        growl.matrix.fm[0][0] = 0.55f;
        growl.lfoTarget = LfoTarget::Amp;
        growl.lfoRate = 4.0f;
        growl.lfoDepth = 0.4f;
        v.push_back ({ "FM Growl", growl });
        return v;
    }();
    return list;
}
} // namespace sls
