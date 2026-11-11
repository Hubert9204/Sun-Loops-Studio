#pragma once

#include "common/HardwareUi.h"
#include "common/SynthUi.h"
#include "hexforge/HexProcessor.h"

namespace hexforge
{
// HexForge's window: a dark steel panel with red and cyan lights. Pages: operators and the FM matrix, filter and amplifier,
// spectral waves and modulation, effects.
class HexEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit HexEditor (HexProcessor&);
    ~HexEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kWidth = 1240, kHeight = 800;

private:
    void timerCallback() override;
    void showPage (int page);

    HexProcessor& processor;
    hwui::Look look;
    juce::Component content;
    std::vector<std::unique_ptr<juce::Component>> owned;
    juce::OwnedArray<juce::TextButton> pageButtons;
    std::array<juce::Component, 4> pages;
    juce::MidiKeyboardComponent keyboard;
    hwui::PresetBar presetBar;
    hwui::Meter meter;
    juce::Component* spectralEditor = nullptr;
    int page = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HexEditor)
};
} // namespace hexforge
