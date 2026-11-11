#pragma once

#include <functional>

#include "hexforge/HexVoice.h"
#include "sunburst/Fx.h"

namespace hexforge
{
struct PresetEntry
{
    juce::String name, category;
    juce::File file; // empty for a factory preset
    int factoryIndex = -1;
};

// The HexForge synthesizer (the plugin, the standalone program and the tests all use this class).
class HexProcessor : public juce::AudioProcessor
{
public:
    HexProcessor();
    ~HexProcessor() override = default;

    static constexpr const char* kName = "HexForge";
    static constexpr const char* kPresetExtension = ".hfpreset";
    static std::function<juce::AudioProcessorEditor* (HexProcessor&)>& editorFactory();

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return kName; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Presets: the factory ones, then the user's (files in userPresetFolder()).
    static juce::File userPresetFolder();
    std::vector<PresetEntry> presets() const;
    void loadPreset (const PresetEntry& preset);
    void loadFactoryPreset (int index);
    bool loadPresetFile (const juce::File& file);
    juce::File savePreset (const juce::String& name, const juce::String& category);
    void initPatch();
    juce::String getPresetName() const { return presetName; }
    juce::String getPresetCategory() const { return presetCategory; }
    int presetGeneration() const noexcept { return generation.load(); }

    // The spectral waves (message thread). They are saved with the state and the presets.
    const Spectrum& getSpectrum (int slot) const noexcept { return spectral.spectrum (slot); }
    void setSpectrum (int slot, const Spectrum& s);

    juce::AudioProcessorValueTreeState state;
    juce::MidiKeyboardState keyboardState;
    float consumePeak() noexcept { return peak.exchange (0.0f); }
    int activeVoices() const noexcept { return voicesSounding.load (std::memory_order_relaxed); }

private:
    void readPatch() noexcept;
    void handleMidi (const juce::MidiMessage& m) noexcept;
    void noteOn (int note, float velocity) noexcept;
    void noteOff (int note) noexcept;
    void allNotesOff (bool hard) noexcept;
    Voice* findVoiceFor (int note) noexcept;
    sunburst::FxSettings fxSettings (float distDriveMod) noexcept;
    void applyValues (const std::vector<std::pair<juce::String, float>>& values);
    void restoreSpectra();

    ParamRefs refs;
    Patch patch;
    Controls controls;
    std::array<Voice, kMaxVoices> voices;
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
    std::uint64_t noteCounter = 0;
    std::uint32_t seedCounter = 1;
    double lastNote = -1.0;
    bool sustain = false;
    std::array<int, 128> monoStack {};
    int monoCount = 0;

    int currentProgram = 0;
    juce::String presetName { "Init" }, presetCategory { "Init" };
    std::atomic<int> generation { 0 };
    std::atomic<float> peak { 0.0f };
    std::atomic<int> voicesSounding { 0 };
};
} // namespace hexforge
