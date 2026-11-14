#pragma once

#include "common/HardwareUi.h"
#include "common/SynthUi.h"
#include "sunriff/RiffProcessor.h"

namespace sunriff
{
// SunRiff's window (0.68.0), laid out as FL Slayer's: the guitar with its pickup (drag it along the strings), the playing mode,
// the string types, the guitar's properties, the amp and cabinet, the effects unit, the presets and a keyboard.
class RiffEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit RiffEditor (RiffProcessor&);
    ~RiffEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kWidth = 1180, kHeight = 592;

private:
    void timerCallback() override;

    RiffProcessor& processor;
    hwui::Look look;
    juce::Component content;
    std::vector<std::unique_ptr<juce::Component>> owned;
    juce::MidiKeyboardComponent keyboard;
    hwui::PresetBar presetBar;
    hwui::Meter meter;
    juce::Component* guitar = nullptr;
    hwui::Knob* fxKnob1 = nullptr;
    hwui::Knob* fxKnob2 = nullptr;
    int shownFx = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RiffEditor)
};
} // namespace sunriff
