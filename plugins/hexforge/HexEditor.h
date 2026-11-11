#pragma once

#include "common/HardwareUi.h"
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
    void showPresetMenu();
    void stepPreset (int delta);
    void savePresetDialog();
    void refreshPresetLabel();

    HexProcessor& processor;
    hwui::Look look;
    juce::Component content;
    std::vector<std::unique_ptr<juce::Component>> owned;
    juce::OwnedArray<juce::TextButton> pageButtons;
    std::array<juce::Component, 4> pages;
    juce::TextButton presetButton, prevButton { "<" }, nextButton { ">" }, saveButton { "Save" };
    juce::MidiKeyboardComponent keyboard;
    juce::Component* meter = nullptr;
    juce::Component* spectralEditor = nullptr;
    std::unique_ptr<juce::AlertWindow> saveWindow;
    int seenGeneration = -1;
    int page = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HexEditor)
};
} // namespace hexforge
