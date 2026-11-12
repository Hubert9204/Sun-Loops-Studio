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

    // 0.60.0: the target channel, as in FL Studio's title bar: the channels' names and the one the roll edits. Choosing another
    // reports through onChannelChosen (the program opens the roll for it).
    void setChannels (const std::vector<juce::String>& names, int selectedIndex);
    std::function<void (int)> onChannelChosen;
    // 0.60.0: ghost notes (the Ghost switch): the other channels' notes behind the channel's own. Call it when the channel or
    // any pattern notes changed; it asks getOtherChannels when the switch is on.
    void refreshGhostNotes();
    // 0.60.0: the global snap of the toolbar; the roll follows it while its snap box says "Main". `ticks` as PianoRoll::setGridTicks
    // (0 = a bar).
    void setMainSnapTicks (int ticks);

    void resized() override;
    void paint (juce::Graphics&) override;

    static constexpr int kHeight = 32;

private:
    void showToolsMenu();
    void showGenerateMenu();
    void applyScale();
    void applySnap();
    theory::Scale currentScale() const;

    PianoRoll& roll;
    juce::ComboBox channelBox; // 0.60.0
    juce::Label snapLabel { {}, "Snap" }, scaleLabel { {}, "Scale" }, chordLabel { {}, "Chord" };
    juce::ComboBox snapBox, rootBox, scaleBox, chordBox, inversionBox;
    juce::ToggleButton scaleSnapToggle { "Snap to scale" }, velocityToggle { "Velocity" }, ghostToggle { "Ghost" };
    int mainSnapTicks = kTicksPerBeat / 4; // 0.60.0
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
