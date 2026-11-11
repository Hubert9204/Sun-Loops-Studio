#pragma once

#include <array>
#include <atomic>
#include <cmath>

#include <juce_audio_processors/juce_audio_processors.h>

// HexForge (0.50.0): a hybrid FM + subtractive synthesizer for sharp, aggressive, metallic sounds (cyberpunk, dark synth,
// industrial, drum & bass, old-school dubstep and neurofunk). Six operators with 32 band-limited waves and two spectral waves,
// a 6 x 6 FM matrix in one panel, an analog-modelled ladder filter with its own envelope and LFO. This file names its parameters.
namespace hexforge
{
constexpr int kNumOps = 6;
constexpr int kNumClassicWaves = 32;
constexpr int kNumOpWaves = kNumClassicWaves + 2; // + Spectral A, Spectral B
constexpr int kMaxUnison = 8;
constexpr int kMaxVoices = 16;
constexpr int kMatrixSlots = 8;
constexpr int kNumMacros = 4;

enum class LadderMode { LP24, LP12, HP24, HP12, BP };
enum class VoiceMode { Poly, Mono, Legato };

enum class Source { None, FilterLfo, Lfo2, FilterEnv, Velocity, ModWheel, Aftertouch, KeyTrack, Random, Macro1, Macro2, Macro3, Macro4, Count };
enum class Dest
{
    None, Op1Level, Op2Level, Op3Level, Op4Level, Op5Level, Op6Level, FmDepth, Pitch, Cutoff, Resonance, Drive, UnisonDetune, Pan, Amp,
    DistDrive, Lfo2Rate, Count
};
constexpr int kNumSources = static_cast<int> (Source::Count);
constexpr int kNumDests = static_cast<int> (Dest::Count);

const juce::StringArray& waveNames();   // 32 classic waves, then "Spectral A", "Spectral B"
const juce::StringArray& ratioNames();
double ratioOf (int index);
const juce::StringArray& ladderNames();
const juce::StringArray& lfoShapeNames();
const juce::StringArray& syncNames();
double syncBeats (int index);
const juce::StringArray& sourceNames();
const juce::StringArray& destNames();
const juce::StringArray& distNames();

juce::String opId (int op, const char* name);   // "op3_ratio" (op from 0)
juce::String fmId (int target, int source);     // "fm2_5": how much operator 5 modulates operator 2 (from 0); the diagonal is feedback
juce::String slotId (int slot, const char* name);
juce::String macroId (int macro);

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

struct ParamRefs
{
    using P = std::atomic<float>*;
    struct Env { P attack, decay, sustain, release; };
    struct Op { P wave, ratio, fine, level, out, pan, velocity; Env env; };
    struct Slot { P source, dest, amount; };

    std::array<Op, kNumOps> op {};
    std::array<std::array<P, kNumOps>, kNumOps> fm {};
    P fmDepth {}, unisonVoices {}, unisonDetune {}, unisonSpread {}, octave {}, dropAmount {}, dropTime {};
    P filterOn {}, filterMode {}, cutoff {}, resonance {}, drive {}, filterEnvAmount {}, filterKey {}, filterVelocity {};
    Env filterEnv {}, ampEnv {};
    P filterLfoAmount {}, filterLfoShape {}, filterLfoRate {}, filterLfoSync {}, filterLfoDivision {};
    P lfo2Shape {}, lfo2Rate {}, lfo2Sync {}, lfo2Division {};
    P ampVelocity {}, mode {}, voices {}, glide {}, bendRange {}, master {};
    std::array<Slot, kMatrixSlots> matrix {};
    std::array<P, kNumMacros> macro {};
    P distOn {}, distType {}, distDrive {}, distMix {};
    P eqOn {}, eqLow {}, eqMid {}, eqMidFreq {}, eqHigh {};
    P chorusOn {}, chorusRate {}, chorusDepth {}, chorusMix {};
    P delayOn {}, delayDivision {}, delayFeedback {}, delayPingPong {}, delayTone {}, delayMix {};
    P reverbOn {}, reverbSize {}, reverbDamping {}, reverbMix {};

    void attach (juce::AudioProcessorValueTreeState& state);
};

inline float get (const std::atomic<float>* p) noexcept { return p != nullptr ? p->load (std::memory_order_relaxed) : 0.0f; }
inline int geti (const std::atomic<float>* p) noexcept { return static_cast<int> (std::lround (get (p))); }
inline bool getb (const std::atomic<float>* p) noexcept { return get (p) > 0.5f; }
} // namespace hexforge
