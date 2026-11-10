#pragma once

#include <array>
#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/Project.h"

namespace sls
{
// The window behind "Assign free mixer track" in a channel's right-click menu: the project's mixer tracks as a scrolling list
// (free ones are marked, the others show which instruments are on them; with fewer than kMaxTracks a last row offers a new
// track) and a button to put this instrument on the track that is selected. The first free track starts selected; when none is
// free, the new track.
class AssignTrackComponent : public juce::Component,
                             private juce::ListBoxModel
{
public:
    // `channels` is only read while the constructor runs. `onAssign` gets the chosen track (0-based; trackCount = a new track).
    AssignTrackComponent (const std::vector<Channel>& channels, int trackCount, int channelIndex, std::function<void (int)> onAssign);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void parentHierarchyChanged() override;

    void assign();                 // what the Assign button does: report the choice and close the window
    void selectTrack (int track);

    static constexpr int kWidth = 380;
    static constexpr int kHeight = 440;

    int getSelectedTrack() const noexcept { return selectedTrack; }
    int getNumRows() override { return static_cast<int> (rowText.size()); }

private:
    void close();
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

    std::function<void (int)> onAssign;
    int selectedTrack = 0;
    juce::Label title;
    juce::Label hint;
    std::vector<juce::String> rowText;
    std::vector<bool> rowFree;
    juce::ListBox list;
    juce::TextButton freeButton { "Select first free track" };
    juce::TextButton assignButton { "Assign" };
    juce::TextButton cancelButton { "Cancel" };
    int firstFree = -1;
};
} // namespace sls
