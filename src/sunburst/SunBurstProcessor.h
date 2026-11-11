#pragma once

#include <functional>

#include "synthkit/SynthProcessor.h"
#include "synthkit/VoiceRouter.h"

#include "sunburst/Arpeggiator.h"
#include "sunburst/Fx.h"
#include "sunburst/Voice.h"

namespace sunburst
{
// The SunBurst synthesizer as an AudioProcessor (the plugin, the standalone program and the tests all use this class).
class SunBurstProcessor : public synthkit::SynthProcessor
{
public:
    SunBurstProcessor();
    ~SunBurstProcessor() override = default;

    static constexpr const char* kName = "SunBurst";
    // The plugin's editor is made by this function (set by the plugin; without it a generic editor is used).
    static std::function<juce::AudioProcessorEditor* (SunBurstProcessor&)>& editorFactory();

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;

private:
    void readPatch() noexcept;
    FxSettings fxSettings() noexcept;

    ParamRefs refs;
    Patch patch;
    Controls controls;
    std::array<Voice, kMaxVoices> voices;
    synthkit::VoiceRouter<Voice, kMaxVoices> router { voices };
    FxRack fx;
    Arpeggiator arp;
    juce::MidiBuffer arpOut;
    std::array<Lfo, kNumLfos> globalLfos;

    double rate = 48000.0;
    double bpm = 120.0, ppq = 0.0;
    bool hostPlaying = false;
    bool arpWasOn = false;

};
} // namespace sunburst
