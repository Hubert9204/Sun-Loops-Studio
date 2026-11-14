#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "orchestra/Engine.h"

// SunSymphony (0.66.0, Milestone 0): the orchestral sampler plugin's processor. 16 parts on the 16 MIDI channels; each plays an
// instrument: the built-in test tone (no library needed) or an SFZ file. The orchestral library, its download manager and the
// rest of the engine come in the next milestones (claude/orchestral-sampler-plan.md).
namespace sunsymphony
{
class SunSymphonyProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    SunSymphonyProcessor();
    ~SunSymphonyProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "SunSymphony"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Message thread. `source` is "test-tone" or an .sfz file's full path. False (and `error`) when nothing could be loaded.
    bool loadInstrument (int part, const juce::String& source, juce::String& error);
    juce::String partSource (int part) const { return sources[static_cast<size_t> (juce::jlimit (0, orchestra::kParts - 1, part))]; }
    juce::String partName (int part) const;

    // Where the orchestral library lives (Milestone 4 lets the user move it).
    static juce::File defaultLibraryFolder();

    int activeVoices() const noexcept { return engine.activeVoices(); }
    float masterGain() const noexcept { return master->get(); }
    juce::MidiKeyboardState& keyboardState() noexcept { return keyboard; }

    static std::function<juce::AudioProcessorEditor* (SunSymphonyProcessor&)>& editorFactory();

private:
    void timerCallback() override { engine.collectRetired(); }

    orchestra::Engine engine;
    std::array<juce::String, orchestra::kParts> sources;
    juce::AudioParameterFloat* master = nullptr;
    juce::MidiKeyboardState keyboard; // the editor's on-screen keyboard plays part 1
    juce::AudioFormatManager formats;
    float smoothedGain = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SunSymphonyProcessor)
};
} // namespace sunsymphony
