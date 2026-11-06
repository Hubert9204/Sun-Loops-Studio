#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/Project.h"

namespace sls
{
// The settings of one synth channel (0.18.0, block F): a menu of ready-made sounds, the engine (Classic = the original sine sound,
// Subtractive = two oscillators and noise, a filter with its own envelope, an amplitude envelope and an LFO) and every setting of the
// subtractive engine. Like the sampler window it never keeps a copy of the channel: `getChannel` returns the live channel (nullptr
// when it is gone or no longer a synth), every change is written to it and announced with `onChanged`.
class SynthPanel : public juce::Component
{
public:
    SynthPanel();

    std::function<Channel*()> getChannel;
    std::function<void()> onChanged;               // a setting changed: publish the project
    std::function<void (bool down)> onPreview;     // the "Play C4" button is pressed (true) or released (false)

    void refresh(); // show the channel as it is now

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Param
    {
        juce::Label label;
        juce::Slider slider;
        std::function<double (const SynthPatch&)> read;
        std::function<void (SynthPatch&, double)> write;
    };
    struct Choice
    {
        juce::Label label;
        juce::ComboBox box;
        std::function<int (const SynthPatch&)> read;    // item index (0-based)
        std::function<void (SynthPatch&, int)> write;
    };
    // The layout: sections of rows, in four columns.
    struct Row
    {
        juce::Label* label = nullptr;
        juce::Component* control = nullptr;
    };
    struct Section
    {
        juce::String title;
        int column = 0;
        std::vector<Row> rows;
        juce::Rectangle<int> header;
    };

    Param& addParam (Section& section, const juce::String& name, double lo, double hi, double step, const juce::String& suffix,
                     std::function<double (const SynthPatch&)> read, std::function<void (SynthPatch&, double)> write);
    Choice& addChoice (Section& section, const juce::String& name, const juce::StringArray& items,
                       std::function<int (const SynthPatch&)> read, std::function<void (SynthPatch&, int)> write);
    Section& addSection (const juce::String& title, int column);
    void edit (const std::function<void (SynthPatch&)>& change);

    juce::Label titleLabel, noteLabel;
    juce::ComboBox patchBox;
    juce::TextButton previewButton { "Play C4" };
    juce::ToggleButton steepToggle { "24 dB / octave" };
    std::vector<std::unique_ptr<Param>> params;
    std::vector<std::unique_ptr<Choice>> choices;
    std::vector<std::unique_ptr<Section>> sections;
    Choice* engineChoice = nullptr;
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthPanel)
};

class SynthWindow : public juce::DocumentWindow
{
public:
    SynthWindow();
    ~SynthWindow() override { clearContentComponent(); }
    SynthPanel& panel() noexcept { return content; }
    std::function<void()> onClose;
    void closeButtonPressed() override { if (onClose) onClose(); }

private:
    SynthPanel content;
};
} // namespace sls
