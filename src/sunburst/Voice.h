#pragma once

#include <array>

#include "sunburst/SynthDsp.h"
#include "sunburst/Wavetables.h"

namespace sunburst
{
// Everything a voice needs from the parameters, read once per block.
struct OscPatch
{
    bool on = false;
    Wave wave = Wave::Saw;
    int table = 0;
    float position = 0.0f, width = 0.5f, level = 0.8f, pan = 0.0f, detune = 0.25f, blend = 0.5f, spread = 0.7f;
    int voices = 1;
    float tune = 0.0f; // octave * 12 + semitones + cents / 100
    bool randomPhase = true;
};

struct LfoPatch
{
    LfoShape shape = LfoShape::Sine;
    double hz = 2.0;
    bool retrigger = true, unipolar = false;
    float fade = 0.0f;
};

struct SlotPatch
{
    Source source = Source::None;
    Dest dest = Dest::None;
    float amount = 0.0f;
};

struct Patch
{
    std::array<OscPatch, kNumOscs> osc {};
    float subLevel = 0.0f, noiseLevel = 0.0f, noiseColour = 0.0f;
    bool subSquare = false;
    int subOctaves = 1;
    bool filterOn = true;
    FilterType filterType = FilterType::LP24;
    float cutoff = 9000.0f, resonance = 0.15f, filterDrive = 0.0f, filterEnv = 0.0f, filterKey = 0.5f, filterVelocity = 0.0f;
    std::array<std::array<float, 4>, kNumEnvs> env {};
    float ampVelocity = 0.5f, pitchEnv = 0.0f;
    std::array<LfoPatch, kNumLfos> lfo {};
    std::array<SlotPatch, kMatrixSlots> matrix {};
    int matrixCount = 0; // slots in use, packed at the front
    float glide = 0.0f, bendRange = 2.0f;
};

// Performance controls and the values the processor computes once per block.
struct Controls
{
    double sampleRate = 48000.0;
    float modWheel = 0.0f, aftertouch = 0.0f, bend = 0.0f; // bend -1..1
    std::array<float, kNumMacros> macro {};
    std::array<float, kNumLfos> globalLfo {};               // the free-running LFOs (for voices that do not retrigger them)
};

class Voice
{
public:
    void prepare (double sampleRate) noexcept;
    // `fromNote` < 0: no glide. `legato`: keep the envelopes running (mono legato).
    void start (int note, float velocity, double fromNote, bool legato, std::uint32_t seed, const Patch& patch) noexcept;
    void release() noexcept;
    void kill() noexcept;
    void render (const Patch& patch, const Controls& controls, float* left, float* right, int numSamples) noexcept;

    bool isActive() const noexcept { return ampEnv.isActive(); }
    bool isReleasing() const noexcept { return ampEnv.isReleasing(); }
    float level() const noexcept { return ampEnv.current(); }

    int note = -1;
    float velocity = 0.0f;
    std::uint64_t age = 0; // when it was started (for stealing the oldest)
    bool keyDown = false;  // the key is held (or the sustain pedal holds it)

private:
    void renderBlock (const Patch& patch, const Controls& controls, float* left, float* right, int n) noexcept;

    double rate = 48000.0;
    double targetNote = 60.0, currentNote = 60.0;
    Envelope ampEnv, filterEnv, modEnv;
    std::array<Lfo, kNumLfos> lfos {};
    std::array<float, kNumLfos> lastRateMod {};
    double secondsSinceStart = 0.0;
    float randomValue = 0.0f;
    Random noise;
    float noiseState = 0.0f;
    std::array<std::array<double, kMaxUnison>, kNumOscs> phases {};
    double subPhase = 0.0;
    MultiFilter filterL, filterR;
};
} // namespace sunburst
