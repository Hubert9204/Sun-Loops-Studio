#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sls
{
// Built-in insert effects. They are Sun Loops Studio's own DSP: VST3 hosting is not implemented yet,
// so the mixer's effect slots hold these four. Everything here is free of JUCE and of the GUI.

enum class EffectType : std::uint8_t { None, Equalizer, Reverb, Delay, Compressor };

constexpr int kEffectTypeCount = 5;
constexpr int kMaxEffectParams = 5;
constexpr int kSlotsPerTrack = 4;

struct EffectParamInfo
{
    const char* name;
    const char* unit;
    float min, max, def;
};

inline const char* effectName (EffectType t) noexcept
{
    switch (t)
    {
        case EffectType::Equalizer:  return "Equalizer";
        case EffectType::Reverb:     return "Reverb";
        case EffectType::Delay:      return "Delay";
        case EffectType::Compressor: return "Compressor";
        case EffectType::None:       break;
    }
    return "Empty";
}

inline int effectParamCount (EffectType t) noexcept
{
    switch (t)
    {
        case EffectType::Equalizer:
        case EffectType::Reverb:
        case EffectType::Delay:      return 4;
        case EffectType::Compressor: return 5;
        case EffectType::None:       break;
    }
    return 0;
}

inline EffectParamInfo effectParamInfo (EffectType t, int index) noexcept
{
    static constexpr EffectParamInfo eq[]   = { { "Low", "dB", -15.0f, 15.0f, 0.0f },
                                                { "Mid", "dB", -15.0f, 15.0f, 0.0f },
                                                { "Mid freq", "Hz", 200.0f, 5000.0f, 1000.0f },
                                                { "High", "dB", -15.0f, 15.0f, 0.0f } };
    static constexpr EffectParamInfo rev[]  = { { "Size", "", 0.0f, 1.0f, 0.5f },
                                                { "Damping", "", 0.0f, 1.0f, 0.5f },
                                                { "Width", "", 0.0f, 1.0f, 1.0f },
                                                { "Mix", "", 0.0f, 1.0f, 0.3f } };
    static constexpr EffectParamInfo dly[]  = { { "Time", "ms", 10.0f, 1000.0f, 375.0f },
                                                { "Feedback", "", 0.0f, 0.9f, 0.4f },
                                                { "Tone", "", 0.0f, 1.0f, 0.7f },
                                                { "Mix", "", 0.0f, 1.0f, 0.35f } };
    static constexpr EffectParamInfo comp[] = { { "Threshold", "dB", -60.0f, 0.0f, -18.0f },
                                                { "Ratio", ":1", 1.0f, 20.0f, 4.0f },
                                                { "Attack", "ms", 0.1f, 100.0f, 10.0f },
                                                { "Release", "ms", 10.0f, 1000.0f, 120.0f },
                                                { "Makeup", "dB", 0.0f, 24.0f, 0.0f } };
    static constexpr EffectParamInfo none { "", "", 0.0f, 1.0f, 0.0f };

    if (index < 0 || index >= effectParamCount (t))
        return none;

    switch (t)
    {
        case EffectType::Equalizer:  return eq[index];
        case EffectType::Reverb:     return rev[index];
        case EffectType::Delay:      return dly[index];
        case EffectType::Compressor: return comp[index];
        case EffectType::None:       break;
    }
    return none;
}

// One slot of a mixer track's effect chain: a type, a bypass switch and its parameter values.
struct EffectSlot
{
    EffectType type = EffectType::None;
    bool bypass = false;
    std::array<float, kMaxEffectParams> params {};

    static EffectSlot make (EffectType t) noexcept
    {
        EffectSlot s;
        s.type = t;
        for (int i = 0; i < effectParamCount (t); ++i)
            s.params[static_cast<size_t> (i)] = effectParamInfo (t, i).def;
        return s;
    }

    // Unknown types become empty, NaN/inf parameters become their default, the rest is clamped to its range.
    EffectSlot sanitised() const noexcept
    {
        if (static_cast<int> (type) >= kEffectTypeCount)
            return EffectSlot {};

        EffectSlot s = *this;
        for (int i = 0; i < kMaxEffectParams; ++i)
        {
            auto& v = s.params[static_cast<size_t> (i)];
            if (i >= effectParamCount (type))
            {
                v = 0.0f;
                continue;
            }
            const auto info = effectParamInfo (type, i);
            v = std::isfinite (v) ? (v < info.min ? info.min : (v > info.max ? info.max : v)) : info.def;
        }
        return s;
    }

    bool operator== (const EffectSlot& o) const noexcept
    {
        return type == o.type && bypass == o.bypass && params == o.params;
    }
};

// The DSP behind one slot. All memory is allocated in prepare(); process() never allocates, locks or
// does I/O. If the slot's type changes, the previous state is discarded and the new effect starts clean.
class EffectUnit
{
public:
    void prepare (double sampleRate);
    void process (const EffectSlot& slot, float* left, float* right, int numSamples) noexcept;

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1l = 0, z2l = 0, z1r = 0, z2r = 0;
        void process (float* l, float* r, int n) noexcept;
        void clearState() noexcept { z1l = z2l = z1r = z2r = 0.0; }
    };

    struct Comb
    {
        std::vector<float> buf;
        int idx = 0;
        float store = 0.0f;
    };

    struct Allpass
    {
        std::vector<float> buf;
        int idx = 0;
    };

    static constexpr int kCombs = 8;
    static constexpr int kAllpasses = 4;

    void reset() noexcept;
    void runEqualizer (const EffectSlot&, float*, float*, int) noexcept;
    void runReverb (const EffectSlot&, float*, float*, int) noexcept;
    void runDelay (const EffectSlot&, float*, float*, int) noexcept;
    void runCompressor (const EffectSlot&, float*, float*, int) noexcept;

    double sampleRate = 48000.0;
    EffectType active = EffectType::None;
    bool dirty = false; // bypassed since the state was last cleared

    // equalizer
    Biquad low, mid, high;
    std::array<float, 4> eqCached { 1.0e9f, 1.0e9f, 1.0e9f, 1.0e9f };

    // reverb (Freeverb layout), channel 0 = left, 1 = right
    std::array<std::array<Comb, kCombs>, 2> combs;
    std::array<std::array<Allpass, kAllpasses>, 2> allpasses;

    // delay
    std::array<std::vector<float>, 2> delayLine;
    int delayWrite = 0;
    float delayCurrent = 0.0f; // samples, glides towards the target
    std::array<float, 2> delayTone { 0.0f, 0.0f };

    // compressor
    float env = 0.0f;
};
} // namespace sls
