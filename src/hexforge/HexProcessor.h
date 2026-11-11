#pragma once

#include <functional>

#include "synthkit/SynthProcessor.h"
#include "synthkit/VoiceRouter.h"

#include "hexforge/HexVoice.h"
#include "sunburst/Fx.h"

namespace hexforge
{
// The HexForge synthesizer (the plugin, the standalone program and the tests all use this class).
class HexProcessor : public synthkit::SynthProcessor
{
public:
    HexProcessor();
    ~HexProcessor() override = default;

    static constexpr const char* kName = "HexForge";
    static std::function<juce::AudioProcessorEditor* (HexProcessor&)>& editorFactory();

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;

    // The spectral waves (message thread). They are saved with the state and the presets.
    const Spectrum& getSpectrum (int slot) const noexcept { return spectral.spectrum (slot); }
    void setSpectrum (int slot, const Spectrum& s);

private:
    void readPatch() noexcept;
    sunburst::FxSettings fxSettings (float distDriveMod) noexcept;
    void resetExtras() override;
    bool applyExtraValue (const juce::String& id, float value) override;
    void stateRestored() override;

    ParamRefs refs;
    Patch patch;
    Controls controls;
    std::array<Voice, kMaxVoices> voices;
    synthkit::VoiceRouter<Voice, kMaxVoices> router { voices };
    sunburst::FxRack fx;
    SpectralBank spectral;
    sunburst::Lfo filterLfo, lfo2;
    juce::AudioBuffer<float> oversampled;
    struct Decimator
    {
        std::array<float, 64> history {};
        int pos = 0;
    } decimL, decimR;
    std::vector<float> halfband;

    double rate = 48000.0, bpm = 120.0, ppq = 0.0;
    bool hostPlaying = false;
};
} // namespace hexforge
