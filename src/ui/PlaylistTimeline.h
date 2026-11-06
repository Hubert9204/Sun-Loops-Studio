#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/Arrangement.h"
#include "ui/DragItems.h"

namespace sls
{
enum class PlaylistTool { Select, Split, Cut, Erase, Zoom };

// The timeline of the Playlist panel: the ruler, the tracks and their clips (the toolbar around it is PlaylistView).
//
// Tools (chosen in the toolbar, or with the keys 1..5 while the mouse is over the timeline):
//   Select : click a clip to select it; drag it to move it (to another track too); drag its left or right edge to trim
//            or lengthen it (audio keeps its place: only more or less of the sound is heard); click empty space to put a
//            clip of the selected pattern there. Right click: a menu for the clip under the mouse.
//   Split  : click a clip to cut it in two at that point (snapped to the grid).
//   Cut    : drag across a clip to remove that stretch of it (audio: the sound after the gap stays where it is).
//   Erase  : click a clip to delete it; drag across several to delete them all.
//   Zoom   : click zooms in, right click or Shift+click zooms out, drag a stretch to zoom to it.
// Always: Ctrl+wheel zooms around the mouse, the wheel scrolls in time, Alt while dragging switches the grid off.
// Delete removes the selected clip; Ctrl+C / Ctrl+X / Ctrl+V / Ctrl+D copy, cut, paste and duplicate it; Ctrl+Z / Ctrl+Y undo and redo.
// Pattern clips always sit on whole pattern boundaries; audio clips (recordings, imported files) sit on the grid chosen
// with Snap, down to a single tick, and are drawn with their waveform. Click on a track's name to choose the track
// that Record writes to. Audio and video files dragged in from the file manager, and sounds dragged from the browser,
// become audio clips (the caller does the importing: see onFilesDropped / onItemDropped).
// The arrangement is owned by the caller; this view edits it in place and reports through onChanged.
class PlaylistTimeline : public juce::Component,
                         public juce::FileDragAndDropTarget,
                         public juce::DragAndDropTarget,
                         private juce::ScrollBar::Listener
{
public:
    explicit PlaylistTimeline (Arrangement& arrangementToEdit);
    ~PlaylistTimeline() override;

    void setSelectedPattern (int patternIndex);
    int getSelectedPattern() const noexcept { return selectedPattern; }

    // Position in ticks within the song (song mode), or < 0 to hide the playhead.
    void setPlayheadTick (int tick);

    void setTool (PlaylistTool newTool);
    PlaylistTool getTool() const noexcept { return tool; }

    // The grid that audio clips snap to while they are moved, trimmed, split or cut, in ticks (480 = one beat; 1 = none).
    void setSnapTicks (int ticks);

    // Zoom (horizontal). 1.0 means the whole song (or, when it is empty, the whole timeline) fills the view.
    void zoomBy (double factor, int anchorX = -1); // anchorX: view coordinate that stays put (default: the middle)
    void zoomToFit();
    void zoomToTicks (int fromTick, int toTick);
    double getPixelsPerTick() const noexcept;

    // Undo / redo of everything done to the clips with this view. The history is dropped (clearHistory) when the
    // arrangement is changed from outside (a project was opened, a recording was placed, a pattern was deleted).
    void undo();
    void redo();
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    void clearHistory();

    // Keys that act on the timeline (see above). Returns true if the key was used.
    bool handleKey (const juce::KeyPress& key);

    // Forget the selection (after the arrangement was replaced from outside).
    void clearSelection();

    std::function<void()> onChanged;                  // clips were added, moved, resized, cut or removed
    std::function<void (int)> onPatternPicked;        // a clip was double-clicked: its pattern index
    std::function<void (PlaylistTool)> onToolChanged; // a key changed the tool
    std::function<void (double)> onZoomChanged;       // pixels per tick
    // Text for the two readouts under the timeline: the position under the mouse, and the selected clip.
    std::function<void (const juce::String&, const juce::String&)> onReadout;

    // Files were dropped on the timeline: their paths, the track under the mouse and the position (in ticks) there.
    std::function<void (const juce::StringArray&, int, int)> onFilesDropped;
    // A sound was dragged here from the browser: the item, the track and the position (in ticks).
    std::function<void (const drag::Item&, int, int)> onItemDropped;

    // Automation clips live on four lanes under the tracks. `automationLabel` names what a clip moves ("Track 2 volume"),
    // `onAutomationPicked` is called when one is double-clicked or chosen from its menu (to open it in the editor).
    std::function<juce::String (const AutomationTarget&)> automationLabel;
    std::function<void (int)> onAutomationPicked;
    // Puts a new automation clip on the first lane where it fits at or after `startTick` (it checks the end of the
    // timeline too), selects it and records the step for undo. Returns its index, or -1 if there is no room.
    int addAutomationClip (AutomationClip clip, int startTick);
    // Remember the state now, as one undo step: the automation editor calls it before it changes points.
    void pushUndoPoint();
    int getSelectedAutomation() const noexcept { return selection.kind == Kind::Automation ? selection.index : -1; }
    void selectAutomation (int index);

    // The track that Record writes to: shown with a red mark, chosen by clicking its name.
    void setArmedTrack (int track);
    std::function<void (int)> onTrackSelected;

    // Tempo, to draw audio at its real length (an audio clip lasts a fixed time, not a fixed number of beats) and to
    // show times.
    void setBpm (double newBpm);

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    bool isInterestedInDragSource (const SourceDetails& details) override;
    void itemDragEnter (const SourceDetails& details) override;
    void itemDragMove (const SourceDetails& details) override;
    void itemDragExit (const SourceDetails& details) override;
    void itemDropped (const SourceDetails& details) override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void resized() override;

    static juce::Colour colourForPattern (int patternIndex);


private:
    enum class Drag { None, MovePattern, ResizePattern, MoveAudio, ResizeAudio, TrimAudio, CutRange, EraseSweep, ZoomRange, MoveAutomation, ResizeAutomation };
    enum class Kind { None, Pattern, Audio, Automation };

    struct Selection
    {
        Kind kind = Kind::None;
        int index = -1;
    };

    struct Snapshot
    {
        std::vector<Clip> clips;
        std::vector<AudioClip> audioClips;
        std::vector<AutomationClip> automationClips;
    };

    struct Peaks
    {
        std::shared_ptr<const SampleData> data; // keeps the audio alive so the cache key cannot be reused
        std::vector<float> lows, highs;         // one pair per kFramesPerPeak frames
    };
    static constexpr int kFramesPerPeak = 256;
    const Peaks& peaksFor (const std::shared_ptr<const SampleData>& data);
    void pruneWaveformCache();
    void paintAudioClip (juce::Graphics& g, const AudioClip& clip, bool selected);
    void paintRuler (juce::Graphics& g);
    void paintGrid (juce::Graphics& g);

    static constexpr int kNameWidth = 64;
    static constexpr int kRulerHeight = 38;
    static constexpr int kScrollBarHeight = 14;
    static constexpr int kEdgeGrab = 7;
    static constexpr int kLaneHeight = 22;
    static constexpr int kLanesGap = 4;

    int trackHeight() const noexcept;
    int lanesTop() const noexcept;                  // y of the first automation lane
    int lanesBottom() const noexcept { return lanesTop() + kAutomationLanes * kLaneHeight; }
    bool inLanes (int y) const noexcept { return y >= lanesTop() && y < lanesBottom(); }
    int yToLane (int y) const noexcept { return std::clamp ((y - lanesTop()) / kLaneHeight, 0, kAutomationLanes - 1); }
    juce::Rectangle<int> automationClipRect (const AutomationClip& c) const noexcept;
    void paintAutomationClip (juce::Graphics& g, const AutomationClip& c, bool selected);
    int unitToX (int unit) const noexcept;
    int xToUnit (int x) const noexcept;       // clamped to the timeline
    int yToTrack (int y) const noexcept;      // clamped to the tracks
    juce::Rectangle<int> clipRect (const Clip& c) const noexcept;
    int tickToX (int tick) const noexcept;
    int xToTick (int x) const noexcept;       // not snapped, clamped to the timeline
    int snapTick (int tick, bool noSnap) const noexcept; // to the nearest grid line (or not at all)
    int effectiveSnap (bool noSnap) const noexcept { return noSnap ? 1 : std::max (1, snapTicks); }
    juce::Rectangle<int> audioClipRect (const AudioClip& c) const noexcept;
    double totalWidth() const noexcept;
    void setUnitWidth (double newUnitWidth, int anchorX);
    void updateScrollBar();
    void clampScroll();
    void notifyChanged();
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

    // Editing
    Snapshot takeSnapshot() const;
    void restore (const Snapshot& s);
    void beginEdit();                  // call before a change: remembers the state for undo (once per gesture)
    void commitPendingUndo();
    bool eraseAt (int track, int tick);
    bool splitAt (int track, int tick, bool noSnap);
    void showClipMenu (const juce::MouseEvent& e);
    void editAudioProperties (int index);
    void editAudioStretch (int index);
    void showAutomationMenu (int index);
    void duplicateSelected();
    void copySelected();
    void pasteAt (int track, int tick);
    void deleteSelected();
    void select (Kind kind, int index);
    void updateReadout (int x, int y);
    juce::String describeSelection() const;
    void setHover (int track, int tick);

    Arrangement& arrangement;
    int selectedPattern = 0;
    int playheadTick = -1;

    double unitWidth = 90.0;
    double scrollX = 0.0;
    juce::ScrollBar scrollBar { false };

    PlaylistTool tool = PlaylistTool::Select;
    int snapTicks = kTicksPerBeat;

    Drag drag = Drag::None;
    int dragClip = -1;
    int grabOffsetUnits = 0;
    int grabOffsetTicks = 0;
    int dragStartTick = 0, dragEndTick = 0; // Cut / zoom range, in ticks
    int dragTrack = 0;
    int dragStartX = 0, dragEndX = 0;
    bool dragChanged = false;
    bool pendingUndoValid = false;
    Snapshot pendingUndo;
    AutomationClip dragAutomationOriginal; // the clip as it was when a move or resize began
    std::vector<Snapshot> undoStack, redoStack;
    Selection selection;

    struct Clipboard
    {
        Kind kind = Kind::None;
        Clip pattern;
        AudioClip audio;
    } clipboard;

    int armedTrack = 0;
    double bpm = 120.0;
    std::vector<Peaks> waveformCache;
    int dropTrack = -1; // while something is dragged over the timeline: where it would land
    int dropTick = 0;
    int hoverTrack = -1;
    int hoverTick = 0;
    int lastMouseX = 0, lastMouseY = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaylistTimeline)
};
} // namespace sls
