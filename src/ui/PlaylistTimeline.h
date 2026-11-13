#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/Arrangement.h"
#include "ui/DragItems.h"

namespace sls
{
// 0.59.0: Draw was called Select; Select is the rectangle. 0.60.0: Mute and Slip, as in FL Studio's Playlist (the toolbar shows the
// tools in FL Studio's order and with its names: Draw, Delete (Erase), Mute, Slip, Slice (Split), Cut, Select, Zoom).
enum class PlaylistTool { Draw, Split, Cut, Erase, Zoom, Select, Mute, Slip, Paint }; // 0.61.0: Paint

// The timeline of the Playlist panel: the ruler, the tracks and their clips (the toolbar around it is PlaylistView).
//
// Tools (chosen in the toolbar, or with the keys 1..5 while the mouse is over the timeline):
//   Draw   : (1; called Select before 0.59.0) click a clip to select it; drag it to move it (to another track too); drag its
//            left or right edge to trim or lengthen it (audio keeps its place: only more or less of the sound is heard); click
//            empty space to put a clip of the selected pattern there, and drag right away to lengthen it. Dragging one of
//            several selected clips moves them all.
//   Select : (6, 0.59.0) drag from empty space: a rectangle selects every clip it touches (Shift adds to the selection;
//            Shift+click adds or removes one clip; a click on empty space selects nothing); drag a clip to move it, or all the
//            selected ones together. Delete, Ctrl+C / Ctrl+X / Ctrl+D act on all of them; Ctrl+A selects everything, Escape
//            nothing (with any tool).
// Any tool but Zoom: right click opens the menu of the clip under the mouse; right drag (0.57.0) erases every clip the mouse
// passes over - pattern, audio and automation clips - as one undo step.
//   Split  : click a clip to cut it in two at that point (snapped to the grid).
//   Cut    : drag across a clip to remove that stretch of it (audio: the sound after the gap stays where it is).
//   Erase  : click a clip to delete it; drag across several to delete them all. (Shown as "Delete", as in FL Studio.)
//   Mute   : (7, 0.60.0) click a clip to mute or unmute it (it stays on the playlist, greyed, and does not play); drag across
//            several to give them all the state of the first one. Tracks have their own M / S switches in their headers.
//   Slip   : (8, 0.60.0) drag a clip sideways to move what plays inside it while the clip itself stays where it is: a pattern
//            clip starts further into (or earlier in) its pattern, an audio clip plays from another point of its file.
//   Paint  : (9, 0.61.0) as Draw on empty space, but dragging along the track paints one clip after another (as FL Studio's).
//   Zoom   : click zooms in, right click or Shift+click zooms out, drag a stretch to zoom to it.
// Always: Ctrl+wheel zooms around the mouse, the wheel scrolls in time, Alt while dragging switches the grid off.
// Delete removes the selected clip; Ctrl+C / Ctrl+X / Ctrl+V / Ctrl+D copy, cut, paste and duplicate it; Ctrl+Z / Ctrl+Y undo and redo.
// Pattern clips and audio clips (recordings, imported files) sit on the grid chosen with Snap, down to a single tick (0.42.0);
// audio is drawn with its waveform. Tracks (0.44.0): free ones and one per pattern (a click there places that pattern), - / +
// in the top-left corner, scrolling when there are more than 8. Click on a track's name to choose the track
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
    // Left click (or drag) on the ruler: move the song position to this tick (snapped to the grid; Alt: no snap).
    std::function<void (int)> onSeek;
    // The loop region (Arrangement::loopStartTick / loopEndTick) was made, moved, resized or deleted on the ruler.
    std::function<void()> onLoopChanged;
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
    // "Edit sample..." in an audio clip's menu: the clip's index (the owner opens the sample editor).
    std::function<void (int)> onEditSample;
    std::function<void (int)> onEditPitch; // an audio clip's "Edit pitch..." (0.38.0): its index
    std::function<void (int)> onSplitStems; // ... "Split into stems..." (0.39.0)
    // 0.62.0, as FL Studio's clip menu: a pattern clip gets a pattern of its own; the pattern clips of a track between two ticks
    // are rendered to an audio clip (to < 0: the whole track)
    std::function<void (int)> onMakeUnique;
    std::function<void (const juce::String&)> onStatus; // a line for the status bar
    std::function<void (int, int, int)> onConsolidate;
    // Replaces audio clip `index` (if it still plays `expected`) by `with`, as one undo step. An error, or "".
    juce::String replaceAudioClip (int index, const std::shared_ptr<const SampleData>& expected, const std::vector<AudioClip>& with);
    // The sample editor saved an edited copy of clip `index`'s audio (`expected` is what the clip played when the editor
    // opened): the clip plays `edited` from its start, as long as it lasts or as far as fits, and is cut at `sliceSeconds`
    // (positions in `edited`). One undo step. Returns an error text, or an empty string.
    juce::String useEditedAudio (int index, const std::shared_ptr<const SampleData>& expected, std::shared_ptr<const SampleData> edited,
                                 const std::vector<double>& sliceSeconds);
    // Puts a new automation clip on the first lane where it fits at or after `startTick` (it checks the end of the
    // timeline too), selects it and records the step for undo. Returns its index, or -1 if there is no room.
    int addAutomationClip (AutomationClip clip, int startTick);
    // Remember the state now, as one undo step: the automation editor calls it before it changes points.
    void pushUndoPoint();
    int getSelectedAutomation() const noexcept { return selection.kind == Kind::Automation ? selection.index : -1; }

    // The selected track (its name in white; pasting goes there), chosen by clicking its name.
    void setSelectedTrack (int track);
    std::function<void (int)> onTrackSelected;
    // The tracks Record writes to (0.36.0): a red dot after the name. A click on the dot (or Ctrl+click on the name) toggles it;
    // the track menu has *Record on this track* and *Recording input...*.
    void setRecordArms (const std::array<bool, kMaxPlaylistTracks>& arms);
    std::function<void (int)> onTrackArmToggled;
    std::function<void (int)> onTrackInputMenu;

    // Tracks (0.44.0): the - and + buttons in the top-left corner, and the track menu's "Delete this track". Adding is one
    // undo step; removing a track with clips on it asks first, then removes the clips with it (one undo step too).
    void addTrack();
    void removeTrack (int track); // asks when the track holds clips
    // The arrangement's tracks changed from outside (a new pattern, an import, a project opened).
    void tracksChanged();
    void showTrack (int track) { tracksChanged(); scrollToTrack (track); } // scrolls the tracks so that `track` can be seen
    std::function<void (int)> onTrackRemoved; // the tracks after it moved up one place: what refers to tracks by number follows

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
    enum class Drag { None, MovePattern, ResizePattern, MoveAudio, ResizeAudio, TrimAudio, CutRange, EraseSweep, ZoomRange, MoveAutomation, ResizeAutomation,
                      Seek, LoopCreate, LoopPress, LoopMove, LoopResizeStart, LoopResizeEnd,
                      MoveGroup, RubberBand, RightPress, RightErase, // 0.57.0: the selection rectangle, the right-button eraser
                      MuteSweep, Slip,                               // 0.60.0
                      PaintSweep };                                  // 0.61.0
    // After "Move" or "Resize" in the loop region's menu the region follows the mouse until a click (right click / Escape: back as it was).
    enum class LoopMode { None, Move, Resize };
    enum class Kind { None, Pattern, Audio, Automation };

    struct Selection
    {
        Kind kind = Kind::None;
        int index = -1;
    };

    // The clips selected together (0.57.0), by index; the single selection above is always one of them.
    struct Group
    {
        std::vector<int> patterns, audio, automation;
        int size() const noexcept { return static_cast<int> (patterns.size() + audio.size() + automation.size()); }
        bool empty() const noexcept { return size() == 0; }
        void clear() noexcept { patterns.clear(); audio.clear(); automation.clear(); }
    };
    // Copies of clips: a copied or duplicated group, or a group as it was when a move began.
    struct ClipSet
    {
        std::vector<Clip> patterns;
        std::vector<AudioClip> audio;
        std::vector<AutomationClip> automation;
        bool empty() const noexcept { return patterns.empty() && audio.empty() && automation.empty(); }
    };

    struct Snapshot
    {
        std::vector<Clip> clips;
        std::vector<AudioClip> audioClips;
        std::vector<AutomationClip> automationClips;
        std::vector<PlaylistTrack> tracks; // names, colours and the pattern each is dedicated to (0.33.0, 0.44.0)
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
    void paintLoopRegion (juce::Graphics& g);
    // Loop region on the ruler.
    int loopStep() const noexcept { return std::max (kSixteenthTicks, snapTicks); } // the grid the region snaps to
    int loopSnap (int tick, bool noSnap) const noexcept;
    bool onLoopRegion (int x) const noexcept;
    int loopEdgeAt (int x) const noexcept; // -1 start edge, +1 end edge, 0 none
    bool placeLoop (int start, int end);   // clamps into the timeline; true when the region changed
    void showLoopMenu();
    void followLoopMode (int x);
    void endLoopMode (bool keep);
    void paintGrid (juce::Graphics& g);

    static constexpr int kNameWidth = 124; // 0.44.0: wider, for the patterns' names on their own tracks; 0.60.0: room for M / S
    static constexpr int kHeaderSwitch = 14; // the M and S switches of a track header (0.60.0), and the record dot after them
    juce::Rectangle<int> muteSwitchRect (int track) const noexcept;
    juce::Rectangle<int> soloSwitchRect (int track) const noexcept;
    void toggleTrackMute (int track);
    void toggleTrackSolo (int track);
    static constexpr int kRulerHeight = 38;
    static constexpr int kScrollBarHeight = 14;
    static constexpr int kEdgeGrab = 7;
    static constexpr int kLaneHeight = 22;
    static constexpr int kLanesGap = 4;

    static constexpr int kTrackScrollWidth = 10;
    static constexpr int kMaxVisibleTracks = 8; // more than this scroll (0.44.0)

    int trackCount() const noexcept { return std::max (1, arrangement.trackCount()); }
    int trackHeight() const noexcept;
    int tracksViewHeight() const noexcept;          // the visible height of the tracks (they scroll when there are more)
    // 0.62.0: tracks folded away under their group's head take no row (and sit far off-screen, so nothing of theirs is drawn or hit)
    int rowOf (int track) const noexcept;
    int visibleRows() const noexcept;
    int trackAtRow (int row) const noexcept;
    int trackTop (int track) const noexcept
    {
        return arrangement.trackHidden (track) ? -100000 : kRulerHeight + rowOf (track) * trackHeight() - scrollY;
    }
    juce::Rectangle<int> foldRect (int track) const noexcept; // the fold triangle of a group's head
    void toggleTrackFlag (int track, int which);              // 0 lock, 1 grouped with the track above, 2 folded
    bool inTracks (int y) const noexcept { return y >= kRulerHeight && y < kRulerHeight + tracksViewHeight(); }
    void clampScrollY();
    void updateTrackScrollBar();
    void scrollToTrack (int track);
    int lanesTop() const noexcept;                  // y of the first automation lane
    int lanesBottom() const noexcept { return lanesTop() + kAutomationLanes * kLaneHeight; }
    bool inLanes (int y) const noexcept { return y >= lanesTop() && y < lanesBottom(); }
    int yToLane (int y) const noexcept { return std::clamp ((y - lanesTop()) / kLaneHeight, 0, kAutomationLanes - 1); }
    juce::Rectangle<int> automationClipRect (const AutomationClip& c) const noexcept;
    void paintAutomationClip (juce::Graphics& g, const AutomationClip& c, bool selected);
    int floorToGrid (int tick) const noexcept; // the start of the grid square that holds `tick` (0.42.0)
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
    // A dragged clip moved to a new place: remembers the undo step, stores it and tells the program.
    template <class Clip>
    void storeDragged (Clip& stored, const Clip& clip, const juce::MouseEvent& e);
    bool eraseAt (int track, int tick);
    bool eraseUnder (int x, int y);                     // every clip (also automation) under a point of the view
    bool eraseAlong (int x0, int y0, int x1, int y1);   // ... and along a line, so a fast stroke misses nothing
    bool muteUnder (int x, int y, bool muted);          // 0.60.0: every pattern and audio clip under a point gets this state
    bool muteAlong (int x0, int y0, int x1, int y1, bool muted);
    void applySlip (int deltaTicks);                    // 0.60.0: the clip being slipped, from its state when the drag began
    bool isSelected (Kind kind, int index) const noexcept;
    void selectGroup (Group g);
    void selectAll();
    Group groupInRect (juce::Rectangle<int> r) const;   // the clips a selection rectangle (view coordinates) touches
    ClipSet groupClips() const;                         // copies of the selected group's clips
    bool pasteSet (const ClipSet& set, int deltaTicks, int deltaTracks); // adds the clips moved by these amounts, as one undo step
    void startGroupMove (Kind kind, int index, int tick);
    void moveGroup (const juce::MouseEvent& e);
    bool placeClipAt (int track, int tick, bool noSnap); // a click on empty space: a clip of the track's (or the selected) pattern
    // 0.61.0: time markers (the ruler's flags).
    int markerAt (int x) const noexcept;                 // the marker whose flag is under x in the ruler, -1 for none
    void showMarkerMenu (int marker);
public:
    void addMarker (int tick);                           // asks for the name, then adds it (undoable)
    void addMarkerAtPlayhead() { addMarker (playheadTick >= 0 ? playheadTick : lastSeekTick); }
    void jumpToMarker (int direction);                   // +1 the next marker after the playhead, -1 the previous one
private:
    int paintTrack = -1;                                 // the Paint tool's track
    int lastSeekTick = 0;                                // the last place clicked in the ruler
    juce::Rectangle<int> bandRect() const noexcept;
    bool splitAt (int track, int tick, bool noSnap);
    void showClipMenu (int x, int y);
    void showTrackMenu (int track);   // right click on a track's name (0.33.0): rename, colour
    void renameTrack (int track);
    void setTrackColour (int track, std::uint32_t colour);
public:
    juce::String trackName (int track) const; // its own name, or "Track N"
    static const std::vector<std::pair<const char*, std::uint32_t>>& trackColours(); // the colour menu (0 = default)
private:
    void editAudioProperties (int index);
    void editAudioStretch (int index);
    void toggleAudioReverse (int index); // 0.27.0
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
    std::vector<std::pair<int, int>> pitchRanges; // paint(): each pattern's lowest and highest note (kept to reuse its memory)
    double scrollX = 0.0;
    juce::ScrollBar scrollBar { false };
    int scrollY = 0; // the tracks, in pixels (0.44.0)
    juce::ScrollBar trackScrollBar { true };
    juce::TextButton removeTrackButton { "-" }, addTrackButton { "+" };

    PlaylistTool tool = PlaylistTool::Draw;
    int snapTicks = kTicksPerBeat;

    Drag drag = Drag::None;
    int dragClip = -1;
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
    Group group;

    struct Clipboard
    {
        Kind kind = Kind::None; // None with `group` filled: several clips were copied (0.57.0)
        Clip pattern;
        AudioClip audio;
        ClipSet group;
    } clipboard;

    // The group move, the selection rectangle and the right-button eraser (0.57.0).
    ClipSet groupOriginal;       // the selected clips as they were when the move began
    Group groupMoving;           // their indices
    Kind groupGrabKind = Kind::None;
    int groupGrabIndex = -1, groupGrabStart = 0, groupGrabTrack = -1;
    int bandStartX = 0, bandStartY = 0, bandEndX = 0, bandEndY = 0;
    bool bandActive = false, bandAdds = false;
    Group bandBase;              // the selection before a Shift+rectangle (it adds to it)
    int rightPressX = 0, rightPressY = 0, eraseLastX = 0, eraseLastY = 0;
    // The Mute and Slip tools (0.60.0).
    bool muteSweepState = false;    // what the clips the sweep passes over become
    Kind slipKind = Kind::None;     // the clip being slipped
    int slipStartTick = 0;          // where the mouse went down
    int slipOriginalOffsetTicks = 0;
    double slipOriginalOffsetSeconds = 0.0;
    void toggleInGroup (Kind kind, int index); // Shift+click on a clip
    void selectToolPress (Kind kind, int index, int tick, const juce::MouseEvent& e);

    int armedTrack = 0;                          // the selected track (named so before 0.36.0, when it was also the one recorded)
    std::array<bool, kMaxPlaylistTracks> recordArms {}; // the tracks Record writes to
    double bpm = 120.0;
    std::vector<Peaks> waveformCache;
    int dropTrack = -1; // while something is dragged over the timeline: where it would land
    int dropTick = 0;
    int hoverTrack = -1;
    int hoverTick = 0;
    int lastMouseX = 0, lastMouseY = 0;

    LoopMode loopMode = LoopMode::None;
    int loopAnchor = 0;                          // LoopCreate: the tick where the right button went down
    int loopGrab = 0;                            // LoopMove / Move mode: mouse tick minus region start when grabbed
    int loopOriginalStart = -1, loopOriginalEnd = -1; // the region before a gesture or a mode (to cancel)
    int loopResizeEdge = 0;                      // Resize mode: the edge that follows the mouse
    int pressX = 0;                              // LoopPress: where the right button went down

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaylistTimeline)
};
} // namespace sls
