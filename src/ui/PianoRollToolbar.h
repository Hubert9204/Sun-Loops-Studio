#pragma once

#include <functional>
#include <string>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/PianoRoll.h"

namespace sls
{
// The bar above the piano roll (0.23.0): snap, scale (shown on the grid, optionally snapping notes into it), a chord to place with
// one click, the velocity lane, the editing tools and the generators (chord progressions, basslines).
class PianoRollToolbar : public juce::Component
{
public:
    explicit PianoRollToolbar (PianoRoll& roll);

    // The other channels' names and notes in the current pattern (for a bassline that follows them).
    struct OtherChannel
    {
        juce::String name;
        std::vector<Note> notes;
    };
    std::function<std::vector<OtherChannel>()> getOtherChannels;

    void resized() override;
    void paint (juce::Graphics&) override;

    static constexpr int kHeight = 32;

private:
    void showToolsMenu();
    void showGenerateMenu();
    void applyScale();
    theory::Scale currentScale() const;

    PianoRoll& roll;
    juce::Label snapLabel { {}, "Snap" }, scaleLabel { {}, "Scale" }, chordLabel { {}, "Chord" };
    juce::ComboBox snapBox, rootBox, scaleBox, chordBox, inversionBox;
    juce::ToggleButton scaleSnapToggle { "Snap to scale" }, velocityToggle { "Velocity" };
    juce::TextButton toolsButton { "Tools" }, generateButton { "Generate" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollToolbar)
};

// The piano roll's panel content: the toolbar above the roll.
class PianoRollEditor : public juce::Component
{
public:
    PianoRollEditor (PianoRollToolbar& toolbarToShow, PianoRoll& rollToShow);
    void resized() override;

private:
    PianoRollToolbar& toolbar;
    PianoRoll& roll;
};
} // namespace sls
