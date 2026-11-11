#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "synthkit/SynthProcessor.h"

// The synthesizer controls that SunBurst and HexForge share (0.51.0): the preset bar, the output meter and the rows of the
// modulation matrix.
namespace hwui
{
// "<", the preset's name (a menu of every preset by category), ">", "Save".
class PresetBar : public juce::Component
{
public:
    PresetBar (synthkit::SynthProcessor& processor, juce::StringArray categories, juce::LookAndFeel& look);
    ~PresetBar() override;
    void resized() override;
    // Call from the editor's timer: true when another preset was loaded since the last call (the label is up to date again).
    bool poll();

private:
    void showMenu();
    void step (int delta);
    void saveDialog();

    synthkit::SynthProcessor& processor;
    juce::StringArray categories; // the factory categories, in the order the menu shows them
    juce::LookAndFeel& look;
    juce::TextButton presetButton, prevButton { "<" }, nextButton { ">" }, saveButton { "Save" };
    std::unique_ptr<juce::AlertWindow> saveWindow;
    int seenGeneration = -1;
};

// The output level, falling back slowly. The bar runs from accent2 to accent, then to `hot` (when it is set).
class Meter : public juce::Component
{
public:
    explicit Meter (juce::Colour hot = {}) : hotColour (hot) {}
    void setLevel (float peak);
    void paint (juce::Graphics& g) override;

private:
    juce::Colour hotColour;
    float level = 0.0f;
};

// One row of a modulation matrix: number, source, destination, amount (bipolar, double-click for 0).
class MatrixRow : public juce::Component
{
public:
    struct Style
    {
        int numberWidth = 26, sourceWidth = 150, destWidth = 170, gap = 0, valueBoxWidth = 52;
        bool arrow = true;  // "→" between source and destination
        bool filled = true; // a darker strip behind the row
    };
    MatrixRow (juce::AudioProcessorValueTreeState& state, int slot, const juce::String& srcId, const juce::String& dstId, const juce::String& amtId,
               const juce::StringArray& sources, const juce::StringArray& destinations, const Style& style);
    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    Style style;
    juce::Label number, arrow;
    juce::ComboBox source, dest;
    juce::Slider amount;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sa, da;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aa;
};
} // namespace hwui
