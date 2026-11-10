#pragma once

#include <vector>

#include "sunburst/SynthDsp.h"

namespace sunburst
{
// What the effects need for one block (read from the parameters, with the global modulation applied).
struct FxSettings
{
    bool distOn = false;
    DistType distType = DistType::Soft;
    float distDrive = 0.3f, distMix = 1.0f;
    bool eqOn = false;
    float eqLow = 0.0f, eqMid = 0.0f, eqMidFreq = 1000.0f, eqHigh = 0.0f;
    bool filterOn = false;
    int filterType = 0; // low-pass, high-pass, band-pass
    float filterCutoff = 12000.0f, filterResonance = 0.1f;
    bool chorusOn = false;
    float chorusRate = 0.6f, chorusDepth = 0.5f, chorusMix = 0.35f;
    bool delayOn = false;
    double delaySeconds = 0.375;
    float delayFeedback = 0.35f, delayTone = 0.6f, delayMix = 0.25f;
    bool delayPingPong = true;
    bool reverbOn = false;
    float reverbSize = 0.6f, reverbDamping = 0.4f, reverbWidth = 1.0f, reverbPredelayMs = 10.0f, reverbMix = 0.25f;
    float masterGain = 0.7f;
};

// The effect rack, in series: distortion > EQ > filter > chorus > delay > reverb, then the master volume and a soft limiter.
class FxRack
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;
    void process (float* left, float* right, int numSamples, const FxSettings& s) noexcept;

private:
    static float readDelay (const std::vector<float>& buffer, int write, double delaySamples) noexcept;

    double rate = 48000.0;

    // Distortion (crush state)
    int crushCounter = 0;
    float crushHoldL = 0.0f, crushHoldR = 0.0f;
    // EQ
    Biquad lowL, lowR, midL, midR, highL, highR;
    // Filter
    MultiFilter fxFilterL, fxFilterR;
    // Chorus
    std::vector<float> chorusL, chorusR;
    int chorusWrite = 0;
    double chorusPhase = 0.0;
    // Delay
    std::vector<float> delayL, delayR;
    int delayWrite = 0;
    double delaySmoothed = 0.0;
    float toneL = 0.0f, toneR = 0.0f;
    // Reverb: pre-delay, two input diffusers, an 8-line feedback delay network
    std::vector<float> predelay;
    int predelayWrite = 0;
    std::array<std::vector<float>, 2> diffusers;
    std::array<int, 2> diffuserPos {};
    std::array<std::vector<float>, 8> lines;
    std::array<int, 8> linePos {};
    std::array<int, 8> lineLength {};
    std::array<float, 8> lineDamp {};
};
} // namespace sunburst
