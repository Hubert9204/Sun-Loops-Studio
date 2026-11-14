#pragma once

#include "sunriff/RiffDsp.h"

// SunRiff's string (0.68.0): a digital waveguide (an extended Karplus-Strong loop) with a fractional delay, a loop filter for the
// frequency-dependent damping, an allpass for the string's stiffness, a plucked excitation shaped by the pick and its position,
// fret noise, a slap nonlinearity, and a pickup tap whose position combs the spectrum as a real one does.
namespace sunriff
{
// The string types (FL Slayer's list): their excitation and their physics.
enum class StringType : int { Noise, Six1, Six2, Six3, Slap, EBass, Fretless };
enum class Coil : int { None, Single, Double };

struct StringSettings
{
    StringType type = StringType::Six1;
    Coil coil = Coil::Single;
    float pickup = 0.3f;      // 0 bridge .. 1 neck
    float slap = 0.0f;        // 0..1
    float fret = 0.2f;        // fret noise 0..1
    float velSens = 0.7f;     // 0..1
    float damping = 0.3f;     // 0 long .. 1 short
    float dampVel = 0.3f;     // how much velocity lengthens the ring
    bool hold = false;
};

class StringVoice
{
public:
    static constexpr int kDelay = 8192;

    void prepare (double sampleRate) noexcept;
    // A new pluck (`seed` varies the noise).
    void start (int note, float velocity, const StringSettings& s, std::uint32_t seed) noexcept;
    // Slide to another note without plucking again (legato, solo modes), over `samples`.
    void glideTo (int note, int samples) noexcept;
    void release() noexcept;    // the key is up: a mute unless Hold
    void kill() noexcept;       // stolen: a 3 ms fade
    void setHold (bool h) noexcept { hold = h; }

    // Adds `n` samples to `out`. `feedback` (may be null) is the amp's output fed back into the string; `bendSemitones` the bend.
    void render (float* out, const float* feedback, float feedbackGain, int n, float bendSemitones) noexcept;

    bool isActive() const noexcept { return active; }
    bool isReleased() const noexcept { return released; }
    int currentNote() const noexcept { return note; }
    float level() const noexcept { return energy; }
    bool keyDown = false;
    std::uint64_t age = 0;

private:
    void updatePitch (float bend) noexcept;
    void updateLoop (float t60) noexcept;

    double rate = 48000.0;
    DelayLine<kDelay> line;
    DelayLine<kDelay> heard; // what the pickup hears: the string without the amp's feedback pushed into it this very sample
    Allpass1 stiffness;
    float loopB = 0.1f, loopGain = 0.99f, prevY = 0.0f, delay = 100.0f, period = 100.0f;
    float pickupA = 0.2f, pickupB = -1.0f;            // the pickup's places (fractions of the string); B < 0: one coil
    float pickupATaps = 0.0f, pickupBTaps = -1.0f;    // the same in samples behind the loop's input
    double pitch = 60.0, glideTarget = 60.0, glideStep = 0.0;
    int glideLeft = 0;
    int note = -1;
    bool active = false, released = false, hold = false;
    float t60 = 3.0f, releaseT60 = 0.07f;
    float energy = 0.0f, quietTime = 0.0f;
    float killGain = 1.0f, killStep = 0.0f;
    StringSettings settings;
    // excitation
    std::array<float, 4096> excite {};
    int exciteLength = 0, excitePos = 0;
    // fret noise and slap click
    Noise noise;
    OnePole fretFilter;
    float fretEnv = 0.0f, fretDecay = 0.999f, slapAmount = 0.0f, buzzThreshold = 10.0f, buzzTime = 0.0f;
    float lastBend = 1.0e9f;
};
} // namespace sunriff
