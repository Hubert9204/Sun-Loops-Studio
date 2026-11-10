#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "audio/HostedPlugin.h"

namespace sls
{
// Insert effects: Sun Loops Studio's own and, since 0.17.0, a third-party plugin (EffectType::Plugin, whose processing is the
// plugin's). 0.21.0 adds Overdrive, Chorus, Phaser, Filter, Parametric EQ, Maximizer (multi-stage mastering limiter), Transient
// (attack / sustain splitter) and Stereo Imager, and gives the Reverb (Hall / Room / Plate, pre-delay) and the Delay (Stereo /
// Ping-pong / Tape) their modes. Everything here is free of JUCE and of the GUI. New types are appended so the numbers stay stable.

enum class EffectType : std::uint8_t
{
    None, Equalizer, Reverb, Delay, Compressor, Plugin,
    Overdrive, Chorus, Phaser, Filter, ParametricEq, Maximizer, Transient, Imager,
    TimeShaper, // 0.25.0: tempo-synced time and volume patterns (half speed, reverse, tape stop, stutter, gates), like Gross Beat
    PitchCorrector // 0.26.0: pulls a monophonic voice or instrument to the notes of a scale, like Pitcher
};

constexpr int kEffectTypeCount = 16;
constexpr int kMaxEffectParams = 8;
constexpr int kSlotsPerTrack = 4;

// The built-in effects in the order of the slot menu, grouped (the group starts where `group` is set).
struct EffectMenuEntry
{
    EffectType type;
    const char* group; // nullptr: same group as the entry before
};
inline constexpr EffectMenuEntry kEffectMenu[] = {
    { EffectType::Equalizer, "EQ and filter" }, { EffectType::ParametricEq, nullptr }, { EffectType::Filter, nullptr },
    { EffectType::Compressor, "Dynamics" },     { EffectType::Maximizer, nullptr },    { EffectType::Transient, nullptr },
    { EffectType::Overdrive, "Drive" },
    { EffectType::Chorus, "Modulation" },       { EffectType::Phaser, nullptr },
    { EffectType::Reverb, "Space and time" },   { EffectType::Delay, nullptr },          { EffectType::TimeShaper, nullptr },
    { EffectType::PitchCorrector, "Pitch" },
    { EffectType::Imager, "Stereo" },
};

struct EffectParamInfo
{
    const char* name;
    const char* unit;
    float min, max, def;
    const char* choices = nullptr; // "Hall|Room|Plate": a choice between whole numbers min..max, shown by name
};

inline const char* effectName (EffectType t) noexcept
{
    switch (t)
    {
        case EffectType::Equalizer:    return "Equalizer";
        case EffectType::Reverb:       return "Reverb";
        case EffectType::Delay:        return "Delay";
        case EffectType::Compressor:   return "Compressor";
        case EffectType::Plugin:       return "Plugin";
        case EffectType::Overdrive:    return "Overdrive";
        case EffectType::Chorus:       return "Chorus";
        case EffectType::Phaser:       return "Phaser";
        case EffectType::Filter:       return "Filter";
        case EffectType::ParametricEq: return "Parametric EQ";
        case EffectType::Maximizer:    return "Maximizer";
        case EffectType::Transient:    return "Transient";
        case EffectType::Imager:       return "Stereo Imager";
        case EffectType::TimeShaper:   return "Time Shaper";
        case EffectType::PitchCorrector: return "Pitch Corrector";
        case EffectType::None:         break;
    }
    return "Empty";
}

namespace effect_tables
{
inline constexpr EffectParamInfo eq[] = { { "Low", "dB", -15.0f, 15.0f, 0.0f },
                                          { "Mid", "dB", -15.0f, 15.0f, 0.0f },
                                          { "Mid freq", "Hz", 200.0f, 5000.0f, 1000.0f },
                                          { "High", "dB", -15.0f, 15.0f, 0.0f } };
// The first four are the reverb of 0.10.0 (unchanged with Hall and no pre-delay).
inline constexpr EffectParamInfo rev[] = { { "Size", "", 0.0f, 1.0f, 0.5f },
                                           { "Damping", "", 0.0f, 1.0f, 0.5f },
                                           { "Width", "", 0.0f, 1.0f, 1.0f },
                                           { "Mix", "", 0.0f, 1.0f, 0.3f },
                                           { "Type", "", 0.0f, 2.0f, 0.0f, "Hall|Room|Plate" },
                                           { "Pre-delay", "ms", 0.0f, 250.0f, 0.0f } };
// The first four are the delay of 0.10.0 (unchanged in Stereo mode with no spread and no wobble).
inline constexpr EffectParamInfo dly[] = { { "Time", "ms", 10.0f, 1000.0f, 375.0f },
                                           { "Feedback", "", 0.0f, 0.9f, 0.4f },
                                           { "Tone", "", 0.0f, 1.0f, 0.7f },
                                           { "Mix", "", 0.0f, 1.0f, 0.35f },
                                           { "Mode", "", 0.0f, 2.0f, 0.0f, "Stereo|Ping-pong|Tape" },
                                           { "Spread", "", 0.0f, 0.5f, 0.0f },
                                           { "Wobble", "", 0.0f, 1.0f, 0.0f } };
inline constexpr EffectParamInfo comp[] = { { "Threshold", "dB", -60.0f, 0.0f, -18.0f },
                                            { "Ratio", ":1", 1.0f, 20.0f, 4.0f },
                                            { "Attack", "ms", 0.1f, 100.0f, 10.0f },
                                            { "Release", "ms", 10.0f, 1000.0f, 120.0f },
                                            { "Makeup", "dB", 0.0f, 24.0f, 0.0f } };
inline constexpr EffectParamInfo drive[] = { { "Drive", "", 0.0f, 1.0f, 0.4f },
                                             { "Boost", "", 0.0f, 1.0f, 0.0f, "Off|x10" },
                                             { "Color", "", 0.0f, 1.0f, 0.5f },
                                             { "Tone", "", 0.0f, 1.0f, 0.6f },
                                             { "Character", "", 0.0f, 1.0f, 0.2f },
                                             { "Mix", "", 0.0f, 1.0f, 1.0f },
                                             { "Output", "dB", -24.0f, 12.0f, -6.0f } };
inline constexpr EffectParamInfo chorus[] = { { "Rate", "Hz", 0.05f, 5.0f, 0.8f },
                                              { "Depth", "ms", 0.0f, 10.0f, 3.0f },
                                              { "Delay", "ms", 5.0f, 40.0f, 15.0f },
                                              { "Feedback", "", -0.9f, 0.9f, 0.0f },
                                              { "Voices", "", 1.0f, 3.0f, 2.0f, "1|2|3" },
                                              { "Width", "", 0.0f, 1.0f, 1.0f },
                                              { "Mix", "", 0.0f, 1.0f, 0.5f } };
inline constexpr EffectParamInfo phaser[] = { { "Rate", "Hz", 0.02f, 10.0f, 0.5f },
                                              { "Depth", "", 0.0f, 1.0f, 0.8f },
                                              { "Center", "Hz", 100.0f, 4000.0f, 800.0f },
                                              { "Feedback", "", -0.95f, 0.95f, 0.5f },
                                              { "Stages", "", 0.0f, 3.0f, 1.0f, "4|6|8|12" },
                                              { "Width", "", 0.0f, 1.0f, 0.5f },
                                              { "Mix", "", 0.0f, 1.0f, 0.5f } };
inline constexpr EffectParamInfo filter[] = { { "Type", "", 0.0f, 3.0f, 0.0f, "Low-pass|High-pass|Band-pass|Notch" },
                                              { "Cutoff", "Hz", 20.0f, 20000.0f, 2000.0f },
                                              { "Resonance", "", 0.0f, 1.0f, 0.2f },
                                              { "Slope", "", 0.0f, 1.0f, 0.0f, "12 dB|24 dB" },
                                              { "Drive", "", 0.0f, 1.0f, 0.0f },
                                              { "LFO rate", "Hz", 0.0f, 20.0f, 0.0f },
                                              { "LFO depth", "", 0.0f, 1.0f, 0.5f },
                                              { "Mix", "", 0.0f, 1.0f, 1.0f } };
inline constexpr EffectParamInfo peq[] = { { "Low freq", "Hz", 20.0f, 500.0f, 100.0f },
                                           { "Low gain", "dB", -18.0f, 18.0f, 0.0f },
                                           { "Mid freq", "Hz", 100.0f, 10000.0f, 1000.0f },
                                           { "Mid gain", "dB", -18.0f, 18.0f, 0.0f },
                                           { "Mid Q", "", 0.2f, 10.0f, 1.0f },
                                           { "High freq", "Hz", 1000.0f, 20000.0f, 8000.0f },
                                           { "High gain", "dB", -18.0f, 18.0f, 0.0f },
                                           { "Output", "dB", -18.0f, 18.0f, 0.0f } };
inline constexpr EffectParamInfo maxim[] = { { "Input", "dB", 0.0f, 24.0f, 0.0f },
                                             { "Ceiling", "dB", -12.0f, 0.0f, -1.0f },
                                             { "Release", "ms", 1.0f, 1000.0f, 100.0f },
                                             { "Glue", "", 0.0f, 1.0f, 0.3f },
                                             { "Soft clip", "", 0.0f, 1.0f, 0.0f } };
inline constexpr EffectParamInfo trans[] = { { "Attack", "dB", -24.0f, 24.0f, 0.0f },
                                             { "Sustain", "dB", -24.0f, 24.0f, 0.0f },
                                             { "Speed", "", 0.0f, 1.0f, 0.3f },
                                             { "Listen", "", 0.0f, 2.0f, 0.0f, "Both|Transients|Sustain" },
                                             { "Mix", "", 0.0f, 1.0f, 1.0f },
                                             { "Output", "dB", -24.0f, 12.0f, 0.0f } };
inline constexpr EffectParamInfo imager[] = { { "Width", "%", 0.0f, 200.0f, 100.0f },
                                              { "Mid", "dB", -12.0f, 12.0f, 0.0f },
                                              { "Side", "dB", -12.0f, 12.0f, 0.0f },
                                              { "Mono bass", "Hz", 0.0f, 500.0f, 0.0f },
                                              { "Balance", "", -1.0f, 1.0f, 0.0f } };

// Time Shaper (0.25.0): a time pattern (where in the recent past it reads) and a volume pattern, over a cycle synced to the song.
inline constexpr EffectParamInfo timeshaper[] = {
    { "Time", "", 0.0f, 8.0f, 0.0f, "None|Half speed|Reverse|Tape stop|Slow down|Stutter 1/8|Stutter 1/16|Repeat 1/4|Scratch" },
    { "Volume", "", 0.0f, 7.0f, 0.0f, "None|Gate 1/8|Gate 1/16|Gate 1/32|Trance gate|Pump|Fade in|Fade out" },
    { "Cycle", "", 0.0f, 3.0f, 2.0f, "1 beat|2 beats|1 bar|2 bars" },
    { "Depth", "%", 0.0f, 100.0f, 100.0f },
    { "Smooth", "ms", 0.5f, 20.0f, 3.0f },
    { "Mix", "%", 0.0f, 100.0f, 100.0f } };

// Pitch Corrector (0.26.0): the pitch of one voice is followed and moved towards the nearest note of the scale. The scales are those
// of the piano roll (theory::scales(), in the same order).
inline constexpr EffectParamInfo pitch[] = {
    { "Key", "", 0.0f, 11.0f, 0.0f, "C|C#|D|D#|E|F|F#|G|G#|A|A#|B" },
    { "Scale", "", 0.0f, 12.0f, 0.0f, "Chromatic|Major|Natural minor|Harmonic minor|Melodic minor|Dorian|Phrygian|Lydian|Mixolydian|Locrian|Major pentatonic|Minor pentatonic|Blues" },
    { "Speed", "ms", 0.0f, 500.0f, 20.0f },
    { "Amount", "%", 0.0f, 100.0f, 100.0f },
    { "Shift", "st", -12.0f, 12.0f, 0.0f },
    { "Range", "", 0.0f, 2.0f, 1.0f, "Low (40-400 Hz)|Voice (70-900 Hz)|High (150-2000 Hz)" },
    { "Mix", "%", 0.0f, 100.0f, 100.0f } };

// The scales' notes as bit masks (bit n: n semitones above the key), in the order of the Scale choice; equal to theory::scales().
inline constexpr std::uint16_t kPitchScaleMasks[] = { 0x0fff, 0x0ab5, 0x05ad, 0x09ad, 0x0aad, 0x06ad, 0x05ab, 0x0ad5, 0x06b5, 0x056b, 0x0295, 0x04a9, 0x04e9 };

template <size_t N>
constexpr int count (const EffectParamInfo (&)[N]) noexcept { return static_cast<int> (N); }
} // namespace effect_tables

inline int effectParamCount (EffectType t) noexcept
{
    using namespace effect_tables;
    switch (t)
    {
        case EffectType::Equalizer:    return count (eq);
        case EffectType::Reverb:       return count (rev);
        case EffectType::Delay:        return count (dly);
        case EffectType::Compressor:   return count (comp);
        case EffectType::Overdrive:    return count (drive);
        case EffectType::Chorus:       return count (chorus);
        case EffectType::Phaser:       return count (phaser);
        case EffectType::Filter:       return count (filter);
        case EffectType::ParametricEq: return count (peq);
        case EffectType::Maximizer:    return count (maxim);
        case EffectType::Transient:    return count (trans);
        case EffectType::Imager:       return count (imager);
        case EffectType::TimeShaper:   return count (timeshaper);
        case EffectType::PitchCorrector: return count (pitch);
        case EffectType::Plugin:
        case EffectType::None:         break;
    }
    return 0;
}

inline EffectParamInfo effectParamInfo (EffectType t, int index) noexcept
{
    using namespace effect_tables;
    static constexpr EffectParamInfo none { "", "", 0.0f, 1.0f, 0.0f };
    if (index < 0 || index >= effectParamCount (t))
        return none;
    const auto i = static_cast<size_t> (index);
    switch (t)
    {
        case EffectType::Equalizer:    return eq[i];
        case EffectType::Reverb:       return rev[i];
        case EffectType::Delay:        return dly[i];
        case EffectType::Compressor:   return comp[i];
        case EffectType::Overdrive:    return drive[i];
        case EffectType::Chorus:       return chorus[i];
        case EffectType::Phaser:       return phaser[i];
        case EffectType::Filter:       return filter[i];
        case EffectType::ParametricEq: return peq[i];
        case EffectType::Maximizer:    return maxim[i];
        case EffectType::Transient:    return trans[i];
        case EffectType::Imager:       return imager[i];
        case EffectType::TimeShaper:   return timeshaper[i];
        case EffectType::PitchCorrector: return pitch[i];
        case EffectType::Plugin:
        case EffectType::None:         break;
    }
    return none;
}

// The whole-number choice a parameter value stands for (choices only), clamped to its range.
inline int effectChoice (EffectType t, int index, float value) noexcept
{
    const auto info = effectParamInfo (t, index);
    return static_cast<int> (std::lround (std::clamp (value, info.min, info.max)));
}

// One slot of a mixer track's effect chain: a type, a bypass switch and its parameter values.
struct EffectSlot
{
    EffectType type = EffectType::None;
    bool bypass = false;
    std::array<float, kMaxEffectParams> params {};
    std::shared_ptr<HostedPlugin> plugin; // EffectType::Plugin only (its parameters live in the plugin)

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
        if (static_cast<int> (type) >= kEffectTypeCount || (type == EffectType::Plugin && plugin == nullptr))
            return EffectSlot {};

        EffectSlot s = *this;
        if (type != EffectType::Plugin)
            s.plugin = nullptr;
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
            if (info.choices != nullptr)
                v = std::round (v); // a choice is a whole number
        }
        return s;
    }

    bool operator== (const EffectSlot& o) const noexcept
    {
        return type == o.type && bypass == o.bypass && params == o.params && plugin == o.plugin;
    }
};

// Ready-made settings of a built-in effect (the effects panel's Preset menu): every parameter of the effect, in order.
struct EffectPreset
{
    const char* name;
    std::array<float, kMaxEffectParams> params;
};
const std::vector<EffectPreset>& effectPresets (EffectType t);

// Where the song is, for effects that follow it (the Time Shaper): the tick at the first sample of the block (negative when the
// transport is stopped: the effect then runs on its own clock) and how many ticks pass per sample (the tempo).
struct EffectTransport
{
    double tick = -1.0;
    double ticksPerSample = 0.0;
};

// The big buffers some effects need (0.29.0): the reverb's combs and all-passes, the pre-delay, the delay lines (also the chorus's
// and the Pitch Corrector's recordings) and the Pitch Corrector's analysis. About 0.8 MB at 48 kHz, so with up to 500 mixer tracks
// of four slots each they are not part of every slot: the Player lends one to each slot whose effect needs it (see
// effectNeedsMemory) from a pool that grows as more are needed. Without one such an effect lets the sound through unchanged.
struct EffectMemory
{
    struct Comb
    {
        std::vector<float> buf;
        int idx = 0;
        int len = 1; // the part of `buf` in use (Room and Plate use shorter lines than Hall)
        float store = 0.0f;
    };

    struct Allpass
    {
        std::vector<float> buf;
        int idx = 0;
        int len = 1;
    };

    static constexpr int kCombs = 8;
    static constexpr int kAllpasses = 4;
    static constexpr int kPitchDecimated = 1024; // the Pitch Corrector's decimated history (a power of two)

    void prepare (double sampleRate); // allocates for this rate
    void clear() noexcept;            // silence, positions to the start
    double preparedRate() const noexcept { return rate; }

    std::array<std::array<Comb, kCombs>, 2> combs;          // reverb (Freeverb layout), channel 0 = left, 1 = right
    std::array<std::array<Allpass, kAllpasses>, 2> allpasses;
    std::vector<float> preDelay;                           // mono: the reverb's input, the Pitch Corrector's analysis copy
    std::array<std::vector<float>, 2> delayLine;           // the delay's, the chorus's and the Pitch Corrector's recordings
    std::array<float, kPitchDecimated> pcDecim {}, pcScratch {}, pcDiff {};

private:
    double rate = 0.0;
};

// True for the effects that need an EffectMemory to work.
inline bool effectNeedsMemory (EffectType t) noexcept
{
    return t == EffectType::Reverb || t == EffectType::Delay || t == EffectType::Chorus || t == EffectType::PitchCorrector;
}

// The DSP behind one slot. All memory is allocated in prepare() (or lent: attachMemory, attachTimeBuffer); process() never
// allocates, locks or does I/O. If the slot's type changes, the previous state is discarded and the new effect starts clean.
class EffectUnit
{
public:
    void prepare (double sampleRate);

    // The big buffers (see EffectMemory), lent by the Player; the effect starts clean with them. Null takes them back.
    void attachMemory (EffectMemory* m) noexcept;
    EffectMemory* attachedMemory() const noexcept { return memory; }
    void process (const EffectSlot& slot, float* left, float* right, int numSamples, const EffectTransport& transport = {}) noexcept;

    // The Time Shaper's recording of the recent past (two channels of `frames` samples). It is lent by the Player from a small pool,
    // since a few seconds per slot of every track would be far too much memory; without one the Time Shaper only shapes the volume.
    void attachTimeBuffer (float* left, float* right, int frames) noexcept;
    bool hasTimeBuffer() const noexcept { return timeL != nullptr; }

    // What the Pitch Corrector last found: the input's pitch in Hz (0 when it found none) and the note it moves it to (MIDI, -1 for
    // none). For tests and a future display; read on the audio thread only.
    float detectedPitchHz() const noexcept { return pcPeriod > 0.0 ? static_cast<float> (sampleRate / pcPeriod) : 0.0f; }
    int targetNote() const noexcept { return pcNote; }

    // Samples by which the effect delays the sound (the Maximizer looks ahead); 0 for the others.
    int latencySamples (EffectType t) const noexcept { return t == EffectType::Maximizer ? lookahead : 0; }

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1l = 0, z2l = 0, z1r = 0, z2r = 0;
        void process (float* l, float* r, int n) noexcept;
        double tickL (double x) noexcept { const double y = b0 * x + z1l; z1l = b1 * x - a1 * y + z2l; z2l = b2 * x - a2 * y; return y; }
        double tickR (double x) noexcept { const double y = b0 * x + z1r; z1r = b1 * x - a1 * y + z2r; z2r = b2 * x - a2 * y; return y; }
        void clearState() noexcept { z1l = z2l = z1r = z2r = 0.0; }
    };

    using Comb = EffectMemory::Comb;
    using Allpass = EffectMemory::Allpass;
    static constexpr int kCombs = EffectMemory::kCombs;
    static constexpr int kAllpasses = EffectMemory::kAllpasses;
    static constexpr int kMaxPhaserStages = 12;
    static constexpr int kOversample = 4;

    void reset() noexcept;
    void runEqualizer (const EffectSlot&, float*, float*, int) noexcept;
    void runReverb (const EffectSlot&, float*, float*, int) noexcept;
    void runDelay (const EffectSlot&, float*, float*, int) noexcept;
    void runCompressor (const EffectSlot&, float*, float*, int) noexcept;
    void runOverdrive (const EffectSlot&, float*, float*, int) noexcept;
    void runChorus (const EffectSlot&, float*, float*, int) noexcept;
    void runPhaser (const EffectSlot&, float*, float*, int) noexcept;
    void runFilter (const EffectSlot&, float*, float*, int) noexcept;
    void runParametricEq (const EffectSlot&, float*, float*, int) noexcept;
    void runMaximizer (const EffectSlot&, float*, float*, int) noexcept;
    void runTransient (const EffectSlot&, float*, float*, int) noexcept;
    void runImager (const EffectSlot&, float*, float*, int) noexcept;
    void runTimeShaper (const EffectSlot&, float*, float*, int, const EffectTransport&) noexcept;
    void runPitchCorrector (const EffectSlot&, float*, float*, int) noexcept;
    void setReverbType (int type) noexcept;

    double sampleRate = 48000.0;
    EffectType active = EffectType::None;
    bool dirty = false; // bypassed since the state was last cleared

    // equalizer (also the parametric EQ: low shelf, peak, high shelf)
    Biquad low, mid, high;
    std::array<float, kMaxEffectParams> eqCached {};
    bool eqValid = false;

    EffectMemory* memory = nullptr; // lent: the buffers below live there

    // reverb (its combs, all-passes and pre-delay are in `memory`)
    int reverbType = -1;
    int preDelayWrite = 0;

    // delay (also the chorus's lines; in `memory`)
    int delayWrite = 0;
    std::array<float, 2> delayCurrent { -1.0f, -1.0f }; // samples, glide towards the target
    std::array<float, 2> delayTone { 0.0f, 0.0f };
    double wobblePhase = 0.0, flutterPhase = 0.0, lfoPhase = 0.0;

    // compressor, maximizer, transient: envelopes
    float env = 0.0f, env2 = 0.0f;
    float limiterGain = 1.0f, limiterTarget = 1.0f;

    // overdrive: 4x oversampling with a 12th-order Butterworth low-pass (six biquads, each with both channels' state) before and
    // after the curve
    static constexpr int kAaStages = 6;
    std::array<Biquad, kAaStages> upFilter, downFilter;
    std::array<float, 2> driveLast {}, colorLow {}, toneState {}, dcX {}, dcY {};
    std::array<double, 2> adaaLast {}; // the curve's previous input (antiderivative anti-aliasing)

    // phaser / filter states
    std::array<std::array<float, kMaxPhaserStages>, 2> phaserState {};
    std::array<float, 2> phaserLast {};
    std::array<float, 2> svf1a {}, svf1b {}, svf2a {}, svf2b {};
    int controlLeft = 0;
    float svfG = 0.0f, svfK = 2.0f;
    std::array<float, 2> phaserCoef {};

    // maximizer look-ahead
    std::array<std::vector<float>, 2> lookLine;
    int lookWrite = 0;
    int lookahead = 0;

    // imager
    std::array<float, 2> sideLow {};

    // time shaper
    float* timeL = nullptr;
    float* timeR = nullptr;
    int timeFrames = 0, timeWrite = 0;
    double freeTick = 0.0;     // its own clock while the transport is stopped
    double lastLag = 0.0;      // samples
    double fadeLag = 0.0;      // the stream being faded out after a jump
    int fadeLeft = 0, fadeLength = 1;
    float volume = 1.0f;       // the smoothed volume pattern

    // pitch corrector. Its stereo recording is the memory's `delayLine` and its mono recording (for the analysis) `preDelay`; the
    // pitch is found on a copy decimated to about 12 kHz (`pcDecim`) and refined at the full rate.
    struct PitchHead
    {
        double delay = 0.0; // samples
        int age = 0, life = 1;
    };
    void analysePitch (int range, int key, int scale, float amount) noexcept;
    void rebirthHead (PitchHead& head, const PitchHead& other, double ratio) noexcept;
    double headPosition (double delay) const noexcept; // read position in delayLine for a delay

    static constexpr int kPitchDecimated = EffectMemory::kPitchDecimated;
    static constexpr int kPitchHop = 64; // analysis every 64 decimated samples (about 5 ms)
    Biquad pcAa1, pcAa2; // the decimation's low-pass (fourth-order Butterworth), left channel state only
    int pcFactor = 4, pcPhase = 0, pcDecimWrite = 0, pcHopLeft = kPitchHop, pcControl = 0;
    double pcPeriod = 0.0;        // the input's period in samples, 0 = no pitch
    int pcNote = -1;              // the note it is pulled to (MIDI), -1 = none
    float pcTarget = 0.0f;        // correction in semitones the analysis asks for
    float pcOffset = 0.0f;        // the correction applied, smoothed by Speed
    double pcRatio = 1.0;         // playback speed of the heads
    double pcAlign = 0.0;         // samples: how far back the analysed sound is, on average (the heads' mean delay)
    std::array<PitchHead, 2> pcHeads {};
};
} // namespace sls
