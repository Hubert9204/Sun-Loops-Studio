#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "common/HardwareUi.h"
#include "sunburst/SunBurstProcessor.h"

namespace sunburst
{
using SunBurstLook = hwui::Look;
using hwui::Choice;
using hwui::Knob;
using hwui::Panel;
using hwui::Switch;

class SunBurstEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SunBurstEditor (SunBurstProcessor&);
    ~SunBurstEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kWidth = 1240, kHeight = 800;

private:
    void timerCallback() override;
    void showPage (int page);
    void showPresetMenu();
    void stepPreset (int delta);
    void savePresetDialog();
    void refreshPresetLabel();

    SunBurstProcessor& processor;
    SunBurstLook look;
    juce::Component content; // laid out at kWidth x kHeight, scaled to the window
    std::vector<std::unique_ptr<juce::Component>> owned;
    juce::OwnedArray<juce::TextButton> pageButtons;
    std::array<juce::Component, 3> pages;
    juce::TextButton presetButton, prevButton { "<" }, nextButton { ">" }, saveButton { "Save" };
    juce::MidiKeyboardComponent keyboard;
    std::unique_ptr<juce::Component> meter;
    std::unique_ptr<juce::AlertWindow> saveWindow;
    int seenGeneration = -1;
    int page = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SunBurstEditor)
};
} // namespace sunburst
