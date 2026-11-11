#pragma once

#include <atomic>
#include <utility>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

// What SunBurst and HexForge share as plugins (0.51.0): a stereo instrument with a parameter tree, factory presets, the user's
// preset files, programs for the host and the saved state.
namespace synthkit
{
// A factory preset: the parameters that differ from their defaults, in their own units (a choice by its index).
struct FactoryPreset
{
    juce::String name, category;
    std::vector<std::pair<juce::String, float>> values;
};

struct PresetEntry
{
    juce::String name, category;
    juce::File file; // empty for a factory preset
    int factoryIndex = -1;
};

class SynthProcessor : public juce::AudioProcessor
{
public:
    struct Identity
    {
        const char* name;            // also the parameter tree's type and the folder of the user's presets
        const char* presetTag;       // the preset files' XML tag
        const char* presetExtension; // ".sbpreset"
        double tailSeconds;
    };

    SynthProcessor (const Identity& identity, juce::AudioProcessorValueTreeState::ParameterLayout layout,
                    const std::vector<FactoryPreset>& factoryPresets);

    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return identity.name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return identity.tailSeconds; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override { loadFactoryPreset (index); }
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Presets: the factory ones, then the user's (files in userPresetFolder()).
    juce::File userPresetFolder() const;
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

protected:
    // Before a factory preset or the init patch sets its values: settings kept outside the parameters go back to their defaults.
    virtual void resetExtras() {}
    // A preset value that is not a parameter: true when it was used.
    virtual bool applyExtraValue (const juce::String& /*id*/, float /*value*/) { return false; }
    // The parameter tree was replaced (a preset file, the host's saved state).
    virtual void stateRestored() {}

    std::atomic<float> peak { 0.0f };
    std::atomic<int> voicesSounding { 0 };

private:
    void applyValues (const std::vector<std::pair<juce::String, float>>& values);
    void presetLoaded (const juce::String& name, const juce::String& category);

    Identity identity;
    const std::vector<FactoryPreset>& factory;
    int currentProgram = 0;
    juce::String presetName { "Init" }, presetCategory { "Init" };
    std::atomic<int> generation { 0 };
};
} // namespace synthkit
