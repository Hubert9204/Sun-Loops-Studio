#pragma once

#include "sunsymphony/SunSymphonyProcessor.h"

// SunSymphony's editor (Milestone 0): the 16 parts with their instruments, a master level, the voice count and an on-screen
// keyboard on part 1. Milestone 9 replaces it with the full interface (library browser, zone and sample editors).
namespace sunsymphony
{
class SunSymphonyEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SunSymphonyEditor (SunSymphonyProcessor&);
    ~SunSymphonyEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshParts();
    void choose (int part);

    SunSymphonyProcessor& processor;
    juce::ListBox parts;
    struct PartsModel;
    std::unique_ptr<PartsModel> model;
    juce::Slider master;
    juce::Label status, libraryNote;
    juce::MidiKeyboardComponent keyboard;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::SliderParameterAttachment> masterAttachment;
};
} // namespace sunsymphony
