#pragma once

#include <functional>
#include <string>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/PlaylistTimeline.h"

namespace sls
{
// The Playlist panel body: a toolbar above the timeline, a readout below it.
//   First row  : the pattern to place (with New / Rename / Delete) and Undo / Redo
//   Second row : the tools (Select, Split, Cut, Erase, Zoom), the grid that audio snaps to, and the zoom buttons
//   Readout    : the position under the mouse (bars.beats.ticks and minutes:seconds.milliseconds) and the exact start,
//                length and end of the selected clip
// Everything about the clips is in PlaylistTimeline.
class PlaylistView : public juce::Component
{
public:
    explicit PlaylistView (Arrangement& arrangementToEdit);

    // The patterns in the list (names, in order) and the selected one.
    void setPatterns (const std::vector<std::string>& names, int selectedIndex);
    void setSelectedPattern (int patternIndex);
    int getSelectedPattern() const noexcept { return timeline.getSelectedPattern(); }

    void setPlayheadTick (int tick) { timeline.setPlayheadTick (tick); }
    void setSelectedTrack (int track) { timeline.setSelectedTrack (track); }
    void setRecordArms (const std::array<bool, kPlaylistTracks>& arms) { timeline.setRecordArms (arms); }
    juce::String trackName (int track) const { return timeline.trackName (track); }
    void setBpm (double bpm) { timeline.setBpm (bpm); }
    bool handleKey (const juce::KeyPress& key) { return timeline.handleKey (key); }
    void clearHistory() { timeline.clearHistory(); updateUndoButtons(); }

    // Automation clips (see PlaylistTimeline).
    int addAutomationClip (AutomationClip clip, int startTick) { return timeline.addAutomationClip (std::move (clip), startTick); }
    void pushUndoPoint() { timeline.pushUndoPoint(); updateUndoButtons(); }
    int getSelectedAutomation() const noexcept { return timeline.getSelectedAutomation(); }
    void refreshTimeline() { timeline.repaint(); }
    void setAutomationLabel (std::function<juce::String (const AutomationTarget&)> f) { timeline.automationLabel = std::move (f); }
    void setAutomationPicked (std::function<void (int)> f) { timeline.onAutomationPicked = std::move (f); }

    // Pattern list actions (the caller changes the arrangement, then calls setPatterns again).
    std::function<void (int)> onPatternSelected;
    std::function<void()> onNewPattern;
    std::function<void (int)> onRenamePattern;
    std::function<void (int)> onDeletePattern;

    // As PlaylistTimeline.
    std::function<void()> onChanged;
    std::function<void (int)> onPatternPicked;
    std::function<void (const juce::StringArray&, int, int)> onFilesDropped;
    std::function<void (const drag::Item&, int, int)> onItemDropped;
    std::function<void (int)> onTrackSelected;
    std::function<void (int)> onTrackArmToggled; // the record dot of a track, or its menu's "Record on this track"
    std::function<void (int)> onTrackInputMenu;  // the track menu's "Recording input..."
    std::function<void (int)> onEditSample; // an audio clip's "Edit sample..." was chosen: its index
    std::function<void (int)> onSeek;       // the ruler was clicked: the song position (ticks)
    std::function<void()> onLoopChanged;    // the loop region was made, changed or deleted
    juce::String useEditedAudio (int index, const std::shared_ptr<const SampleData>& expected, std::shared_ptr<const SampleData> edited,
                                 const std::vector<double>& sliceSeconds)
    {
        return timeline.useEditedAudio (index, expected, std::move (edited), sliceSeconds);
    }


    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kToolbarRowHeight = 26;
    static constexpr int kStatusHeight = 20;

private:
    void updateUndoButtons();
    void syncToolButtons();

    PlaylistTimeline timeline;
    juce::Label patternLabel { {}, "Pattern" };
    juce::ComboBox patternBox;
    juce::TextButton newButton { "New" };
    juce::TextButton renameButton { "Rename" };
    juce::TextButton deleteButton { "Delete" };
    juce::TextButton undoButton { "Undo" };
    juce::TextButton redoButton { "Redo" };
    juce::TextButton selectTool { "Select" };
    juce::TextButton splitTool { "Split" };
    juce::TextButton cutTool { "Cut" };
    juce::TextButton eraseTool { "Erase" };
    juce::TextButton zoomTool { "Zoom" };
    juce::Label snapLabel { {}, "Grid" };
    juce::ComboBox snapBox;
    juce::TextButton zoomOutButton { "-" };
    juce::TextButton zoomInButton { "+" };
    juce::TextButton zoomFitButton { "Fit" };
    juce::Label positionLabel;
    juce::Label selectionLabel;
    std::vector<int> snapValues;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaylistView)
};
} // namespace sls
