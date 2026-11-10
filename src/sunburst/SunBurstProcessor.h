#pragma once

#include <functional>

#include "sunburst/Arpeggiator.h"
#include "sunburst/Fx.h"
#include "sunburst/Voice.h"

namespace sunburst
{
struct PresetEntry
{
    juce::String name, category;
    juce::File file; // empty for a factory preset
    int factoryIndex = -1;
};

// The SunBurst synthesizer as an AudioProcessor (the plugin, the standalone program and the tests all use this class).
class SunBurstProcessor : public juce::AudioProcessor
{
public:
    SunBurstProcessor();
    ~SunBurstProcessor() override = default;

    static constexpr const char* kName = "SunBurst";
    static constexpr const char* kPresetExtension = ".sbpreset";
    // The plugin's editor is made by this function (set by the plugin; without it a generic editor is used).
    static std::function<juce::AudioProcessorEditor* (SunBurstProcessor&)>& editorFactory();

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
    double getTailLengthSeconds() const override { return 4.0; }

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
    void initPatch(); // every parameter back to its default
    juce::String getPresetName() const { return presetName; }
    juce::String getPresetCategory() const { return presetCategory; }
    int presetGeneration() const noexcept { return generation.load(); } // changes whenever a preset is loaded

    juce::AudioProcessorValueTreeState state;
    juce::MidiKeyboardState keyboardState; // the on-screen keyboard
    float consumePeak() noexcept { return peak.exchange (0.0f); }
    int activeVoices() const noexcept { return voicesSounding.load (std::memory_order_relaxed); }

private:
    void readPatch() noexcept;
    void handleMidi (const juce::MidiMessage& m) noexcept;
    void noteOn (int note, float velocity) noexcept;
    void noteOff (int note) noexcept;
    void allNotesOff (bool hard) noexcept;
    Voice* findVoiceFor (int note) noexcept;
    FxSettings fxSettings() noexcept;
    void applyValues (const std::vector<std::pair<juce::String, float>>& values);

    ParamRefs refs;
    Patch patch;
    Controls controls;
    std::array<Voice, kMaxVoices> voices;
    FxRack fx;
    Arpeggiator arp;
    juce::MidiBuffer arpOut;
    std::array<Lfo, kNumLfos> globalLfos;

    double rate = 48000.0;
    double bpm = 120.0, ppq = 0.0;
    bool hostPlaying = false;
    std::uint64_t noteCounter = 0;
    std::uint32_t seedCounter = 1;
    double lastNote = -1.0;
    bool sustain = false;
    std::array<int, 128> monoStack {};
    int monoCount = 0;
    bool arpWasOn = false;

    int currentProgram = 0;
    juce::String presetName { "Init" }, presetCategory { "Init" };
    std::atomic<int> generation { 0 };
    std::atomic<float> peak { 0.0f };
    std::atomic<int> voicesSounding { 0 };
};
} // namespace sunburst
