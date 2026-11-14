#pragma once

#include <functional>

#include "synthkit/SynthProcessor.h"

#include "sunriff/RiffAmp.h"
#include "sunriff/RiffFx.h"
#include "sunriff/RiffPlayer.h"
#include "sunriff/RiffString.h"

// SunRiff (0.68.0): the electric guitar plugin, after FL Slayer. Strings (digital waveguides) -> pickup and body -> the AMP
// effects -> amplifier and cabinet (whose output can feed back into the strings) -> the MST effects -> master.
namespace sunriff
{
constexpr int kVoices = 12;

class RiffProcessor : public synthkit::SynthProcessor
{
public:
    RiffProcessor();
    ~RiffProcessor() override = default;

    static constexpr const char* kName = "SunRiff";
    static std::function<juce::AudioProcessorEditor* (RiffProcessor&)>& editorFactory();

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;

    // The notes the strings are playing now (for the editor and the tests).
    int soundingNotes (int* out, int max) const noexcept;

private:
    void apply (const StringEvent& e) noexcept;
    StringVoice* voiceFor (int note) noexcept;

    struct Refs
    {
        std::atomic<float>* p[27] {};
    } refs;

    std::array<StringVoice, kVoices> voices;
    Player player;
    Amp amp;
    FxUnit fx;
    StringSettings stringSettings;
    Biquad pickupResonance, formant1, formant2, formant3;
    DcBlocker outputDc;
    std::array<float, 16> chunkOut {}, feedbackIn {};
    double rate = 48000.0, bpm = 120.0;
    std::int64_t clock = 0;
    std::uint64_t ageCounter = 0;
    std::uint32_t seed = 1;
    float bend = 0.0f, bendTarget = 0.0f, wheel = 0.0f, smoothedMaster = 0.8f;
    bool pedal = false;
    float lastTone = -1.0f, lastHarmonic = -1.0f;
    int lastCoil = -1;
};
} // namespace sunriff
