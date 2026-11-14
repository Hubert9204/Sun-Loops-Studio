#pragma once

#include <vector>

#include <juce_core/juce_core.h>

#include "sunriff/RiffDsp.h"

// SunRiff's effects unit (0.68.0), after FL Slayer's MFX-90: one effect, chosen from two groups. The AMP effects sit before the
// amplifier (Phaser, Tremolo, WahWah, WahWah LFO, Ringmod, Fuzz Box, Harmonizer, Talkbox); the MST (master) effects after it, in
// stereo (Chorus, Flanger, Phaser, Leslie, Tremolo, DubDelay, Multitap). Each has two parameters, 0..1.
namespace sunriff
{
enum FxType : int
{
    FxDry, FxPhaser, FxTremolo, FxWah, FxWahLfo, FxRingmod, FxFuzz, FxHarmonizer, FxTalkbox,
    FxChorus, FxFlanger, FxStereoPhaser, FxLeslie, FxStereoTremolo, FxDubDelay, FxMultitap, FxCount
};
const juce::StringArray& fxNames();
// The names of an effect's two parameters ("LFO speed", "Depth"...); empty when it has none.
std::pair<juce::String, juce::String> fxParamNames (int type);
inline bool fxIsPre (int type) noexcept { return type >= FxPhaser && type <= FxTalkbox; }
inline bool fxIsPost (int type) noexcept { return type >= FxChorus && type < FxCount; }
// The tempo divisions the synced effects step through (Param 1), in beats.
double fxDivisionBeats (float param, int& index) noexcept;
const juce::StringArray& fxDivisionNames();

struct FxParams
{
    int type = FxDry;
    float p1 = 0.5f, p2 = 0.5f;
    double bpm = 120.0;
};

class FxUnit
{
public:
    void prepare (double sampleRate);   // allocates the delay lines (not on the audio thread)
    void reset() noexcept;
    void configure (const FxParams& p) noexcept; // once per block
    void noteOn (double hz) noexcept;   // retriggers the synced LFOs; the ring modulator follows the note
    float pre (float x) noexcept;       // the AMP effects (mono)
    void post (float x, float& left, float& right) noexcept; // the MST effects (mono in, stereo out)

private:
    float phaser (float x, int which, float lfo) noexcept;
    float readDelay (const std::vector<float>& buf, int write, float delay) const noexcept;

    double rate = 48000.0;
    FxParams params;
    Lfo lfo, lfo2, ringOsc;
    double ringHz = 220.0, noteHz = 220.0;
    float lfoHz = 1.0f, depth = 0.5f, feedbackAmount = 0.0f;
    // phasers (two, for stereo)
    std::array<std::array<Allpass1, 6>, 2> phaseStages {};
    std::array<float, 2> phaseFeedback {};
    // wah and talkbox
    Biquad wah, formant1, formant2;
    float follower = 0.0f, followerAttack = 0.01f, followerRelease = 0.001f;
    int filterCounter = 0;
    // fuzz
    float holdSample = 0.0f;
    int holdCount = 0;
    // harmonizer
    DcBlocker upDc;
    OnePole upHp, downLp, inputLp, downEnv;
    float flip = 1.0f, lastLp = 0.0f;
    // delays (stereo)
    std::vector<float> delayL, delayR;
    int writePos = 0, delayMask = 0;
    OnePole fbFilterL, fbFilterR;
    float leslieSpeed = 1.0f, leslieTarget = 1.0f;
    OnePole leslieLow;
};
} // namespace sunriff
