#pragma once

#include <array>
#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/Project.h"

namespace sls
{
// The window behind "Assign free mixer track" in a channel's right-click menu: the eight mixer tracks as a list
// (free ones are marked, the others show which instruments are on them) and a button to put this instrument on the
// track that is selected. The first free track starts selected.
class AssignTrackComponent : public juce::Component
{
public:
    // `channels` is only read while the constructor runs. `onAssign` gets the chosen track (0-based).
    AssignTrackComponent (const std::vector<Channel>& channels, int channelIndex, std::function<void (int)> onAssign);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void parentHierarchyChanged() override;

    int getSelectedTrack() const noexcept { return selectedTrack; }
    void assign();                 // what the Assign button does: report the choice and close the window
    void selectTrack (int track);

    static constexpr int kWidth = 380;
    static constexpr int kHeight = 440;

private:
    void close();

    std::function<void (int)> onAssign;
    int selectedTrack = 0;
    juce::Label title;
    juce::Label hint;
    std::array<juce::ToggleButton, kMaxTracks> rows;
    juce::TextButton freeButton { "Select first free track" };
    juce::TextButton assignButton { "Assign" };
    juce::TextButton cancelButton { "Cancel" };
    int firstFree = -1;
};
} // namespace sls
