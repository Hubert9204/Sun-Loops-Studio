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
//   Second row : the tools in FL Studio's order (0.60.0: Draw, Delete, Mute, Slip, Slice, Cut, Select, Zoom), the snap ("Main"
//                follows the toolbar's global snap, as in FL Studio), and the zoom buttons
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
    void setRecordArms (const std::array<bool, kMaxPlaylistTracks>& arms) { timeline.setRecordArms (arms); }
    juce::String trackName (int track) const { return timeline.trackName (track); }
    void setBpm (double bpm) { timeline.setBpm (bpm); }
    bool handleKey (const juce::KeyPress& key) { return timeline.handleKey (key); }
    void clearHistory() { timeline.clearHistory(); timeline.tracksChanged(); updateUndoButtons(); } // the arrangement was replaced
    // The arrangement's tracks were added or removed from outside (a new pattern, an import): scroll bar and drawing (0.44.0).
    void tracksChanged() { timeline.tracksChanged(); }
    // The time signature or the tempo changes were changed (0.46.0): the grid's "Bar" and the ruler follow.
    void meterChanged() { applySnap(); timeline.repaint(); }
    void applySnap();
    // 0.60.0: the global snap of the toolbar (the code of PlaylistView's own list: 0 = bar, -1 = half a bar, else ticks); the
    // playlist follows it while its snap box says "Main".
    void setMainSnap (int code);
    void addTrack() { timeline.addTrack(); }
    void showTrack (int track) { timeline.showTrack (track); }

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
    std::function<void (int)> onTrackRemoved;    // the - button removed this track (0.44.0): what refers to tracks by number follows
    std::function<void (int)> onEditSample; // an audio clip's "Edit sample..." was chosen: its index
    std::function<void (int)> onEditPitch;  // ... "Edit pitch..."
    std::function<void (int)> onSplitStems; // ... "Split into stems..."
    juce::String replaceAudioClip (int index, const std::shared_ptr<const SampleData>& expected, const std::vector<AudioClip>& with)
    {
        auto error = timeline.replaceAudioClip (index, expected, with);
        updateUndoButtons();
        return error;
    }
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
    juce::TextButton drawTool { "Draw" };     // 0.59.0: was "Select"
    juce::TextButton selectTool { "Select" }; // 0.59.0: the selection rectangle
    juce::TextButton splitTool { "Slice" };   // 0.60.0: FL Studio's name (the tool is PlaylistTool::Split)
    juce::TextButton cutTool { "Cut" };
    juce::TextButton eraseTool { "Delete" };  // 0.60.0: FL Studio's name (the tool is PlaylistTool::Erase)
    juce::TextButton muteTool { "Mute" };     // 0.60.0
    juce::TextButton slipTool { "Slip" };     // 0.60.0
    juce::TextButton paintTool { "Paint" };   // 0.61.0
    // 0.61.0: time markers, as FL Studio's: add one at the playhead, jump to the previous / next one
    juce::TextButton markerButton { "+ Marker" };
    juce::TextButton markerPrevButton { juce::String::fromUTF8 ("\xe2\x97\x82") }, markerNextButton { juce::String::fromUTF8 ("\xe2\x96\xb8") };
    juce::TextButton zoomTool { "Zoom" };
    int mainSnapCode = kTicksPerBeat;         // 0.60.0: the toolbar's global snap
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
