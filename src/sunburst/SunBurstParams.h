#pragma once

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "synthkit/ParamKit.h"

// SunBurst (0.48.0): a polyphonic synthesizer for electronic sounds, built as a VST3 / Standalone plugin and bundled with
// Sun Loops Studio. This file names every parameter, its range and the lists (waveforms, filters, modulation sources and
// destinations) that the engine and the interface share.
namespace sunburst
{
using synthkit::get;
using synthkit::getb;
using synthkit::geti;
using synthkit::distNames;
using synthkit::lfoShapeNames;
using synthkit::syncBeats;
using synthkit::syncNames;

constexpr int kNumOscs = 3;
constexpr int kNumEnvs = 3;  // 1 = amplitude, 2 = filter, 3 = modulation (also the pitch envelope)
constexpr int kNumLfos = 3;
constexpr int kNumMacros = 4;
constexpr int kMatrixSlots = 16;
constexpr int kMaxUnison = 16;
constexpr int kMaxVoices = 32;

enum class Wave { Sine, Triangle, Saw, Square, Noise, Wavetable };
enum class FilterType { LP12, LP24, HP12, HP24, BP12, BP24, Notch, Peak };
enum class LfoShape { Sine, Triangle, SawUp, SawDown, Square, SampleHold, SmoothRandom };
enum class VoiceMode { Poly, Mono, Legato };
enum class DistType { Soft, Hard, Tube, Fold, Crush };
enum class ArpMode { Up, Down, UpDown, Random, AsPlayed, Chord };

enum class Source
{
    None, Lfo1, Lfo2, Lfo3, Env2, Env3, Velocity, ModWheel, Aftertouch, PitchBend, KeyTrack, Random,
    Macro1, Macro2, Macro3, Macro4, Count
};

enum class Dest
{
    None,
    Osc1Pitch, Osc1Position, Osc1Width, Osc1Level, Osc1Detune, Osc1Pan,
    Osc2Pitch, Osc2Position, Osc2Width, Osc2Level, Osc2Detune, Osc2Pan,
    Osc3Pitch, Osc3Position, Osc3Width, Osc3Level, Osc3Detune, Osc3Pan,
    AllPitch, SubLevel, NoiseLevel,
    Cutoff, Resonance, Drive, Amp, Pan,
    Lfo1Rate, Lfo2Rate, Lfo3Rate,
    DistDrive, FxCutoff, ChorusMix, DelayMix, ReverbMix,
    Count
};

constexpr int kNumSources = static_cast<int> (Source::Count);
constexpr int kNumDests = static_cast<int> (Dest::Count);
constexpr int kFirstFxDest = static_cast<int> (Dest::DistDrive); // from here on: effects, modulated by global sources only

const juce::StringArray& waveNames();
const juce::StringArray& wavetableNames();
const juce::StringArray& filterNames();
const juce::StringArray& sourceNames();
const juce::StringArray& destNames();
const juce::StringArray& arpModeNames();
const juce::StringArray& arpRateNames();
double arpRateBeats (int index);

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// "o1_level", "env2_a", "lfo3_rate", "mx12_amt": the ids built the same way everywhere.
juce::String oscId (int osc, const char* name);    // osc from 0
juce::String envId (int env, const char* name);    // env from 0
juce::String lfoId (int lfo, const char* name);    // lfo from 0
juce::String matrixId (int slot, const char* name); // slot from 0
juce::String macroId (int macro);

// Raw pointers to every parameter's value, looked up once (the audio thread reads them with relaxed loads).
struct ParamRefs
{
    using P = std::atomic<float>*;
    struct Osc { P on, wave, table, position, width, octave, semi, fine, level, pan, voices, detune, blend, spread, randomPhase; };
    struct Env { P attack, decay, sustain, release; };
    struct Lfo { P shape, rate, sync, division, retrigger, fade, unipolar; };
    struct Slot { P source, dest, amount; };

    std::array<Osc, kNumOscs> osc {};
    P subLevel {}, subWave {}, subOctave {}, noiseLevel {}, noiseColour {};
    P filterOn {}, filterType {}, cutoff {}, resonance {}, filterDrive {}, filterEnv {}, filterKey {}, filterVelocity {};
    std::array<Env, kNumEnvs> env {};
    P ampVelocity {}, pitchEnv {};
    std::array<Lfo, kNumLfos> lfo {};
    std::array<Slot, kMatrixSlots> matrix {};
    std::array<P, kNumMacros> macro {};
    P mode {}, voices {}, glide {}, bendRange {}, master {};
    P distOn {}, distType {}, distDrive {}, distMix {};
    P eqOn {}, eqLow {}, eqMid {}, eqMidFreq {}, eqHigh {};
    P fxFilterOn {}, fxFilterType {}, fxCutoff {}, fxResonance {};
    P chorusOn {}, chorusRate {}, chorusDepth {}, chorusMix {};
    P delayOn {}, delaySync {}, delayDivision {}, delayTime {}, delayFeedback {}, delayPingPong {}, delayTone {}, delayMix {};
    P reverbOn {}, reverbSize {}, reverbDamping {}, reverbWidth {}, reverbPredelay {}, reverbMix {};
    P arpOn {}, arpMode {}, arpRate {}, arpOctaves {}, arpGate {};

    void attach (juce::AudioProcessorValueTreeState& state);
};

} // namespace sunburst
