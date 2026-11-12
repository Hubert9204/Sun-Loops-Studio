#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/MusicTheory.h"
#include "audio/NoteSequence.h"

namespace sls
{
// Note editor.
//   Left click on empty grid   : create a note (on release; drag its right edge afterwards to change its length); with a chord chosen in
//                                the toolbar, the whole chord
//   Left drag on empty grid    : (0.44.0) a selection rectangle: on release every note it touches or holds is selected (Shift: added to
//                                the selection); a drag shorter than 4 pixels is a click
//   Left drag on a note        : move it (drag its left or right edge to resize: the cursor becomes a left-right arrow there); on a
//                                selected note, every selected note moves / resizes. A click on a note also makes its length the
//                                length of the next notes drawn (0.32.0, as in FL Studio)
//   Ctrl + click on a note     : add it to the selection or take it out
//   Ctrl + drag on empty grid  : the same rectangle (as before 0.44.0)
//   Right click on a note      : delete it (on a selected note: every selected note)
//   Shift + click on a note    : changes how it joins the note before it: normal -> legato (blue: the sound goes on, the pitch
//                                jumps) -> slide (amber: the pitch glides, drawn as a line from the old pitch) -> normal. The note before must still be sounding
//                                when the linked note starts (turning legato on lengthens a note that stops up to a 16th early)
//   Alt while dragging         : no snapping to the grid
//   Left button on a key       : plays that note for as long as the button is down (slide to change the note)
//   Left button on the grid    : the note being created, moved or resized sounds while the button is down
//   Mouse wheel                : scroll pitch   | Shift+wheel (or a sideways wheel / touchpad): scroll time | Ctrl+wheel: zoom time;
//                                the scrolling glides (0.58.0)
// The spare quarter (0.58.0): after the pattern's end the roll shows one more quarter, shaded. It does not play while it is empty;
// a note written into it (created, moved, lengthened, pasted) asks for the pattern to become one quarter longer (takeLengthRequest),
// and the roll then shows a new spare quarter after the new end.
// Velocity lane (0.23.0), under the grid: a stalk per note; drag across it to draw the velocities (only of the selected notes, if any).
// Keys (handleKey, with the mouse over the piano roll): Delete, Ctrl+A / Escape (select all / none), Ctrl+C / X / V (copy, cut, paste
// at the mouse), Ctrl+D (duplicate after itself), Up / Down (a semitone; Shift: an octave; Ctrl: a step of the scale), Left / Right
// (a grid step; Shift: a beat).
// The ruler (0.20.0), as in the playlist:
//   Left click / drag          : move the pattern position there (also while playing; snapped to 16ths, Alt: no snap)
//   Right drag                 : draw a loop region; Pattern mode then repeats only that part of the pattern
//   Right drag on the region   : move it (on one of its edges: resize it)
//   Right click on the region  : menu Move / Resize / Delete loop region (Delete removes only the highlight)
class PianoRoll : public juce::Component, private juce::Timer
{
public:
    PianoRoll();

    void setNotes (std::vector<Note> newNotes); // clears the selection
    const std::vector<Note>& getNotes() const noexcept { return notes; }
    // 0.60.0: ghost notes, as in FL Studio: the other channels' notes of the pattern, drawn faintly behind the channel's own
    // (they cannot be edited here). An empty list hides them.
    void setGhostNotes (std::vector<Note> ghosts);

    // Scroll vertically so the current notes are centred (a channel just opened may sit far from middle C).
    void scrollToNotes();

    void setLoopLengthTicks (int ticks);
    int getLoopLengthTicks() const noexcept { return loopLengthTicks; }
    // The spare quarter after the end (0.58.0): shown and writable while the pattern can still grow.
    void setGrowable (bool canGrow);
    int editLimit() const noexcept { return loopLengthTicks + (growable ? kTicksPerBeat : 0); }
    // After onNotesChanged: the length the pattern should now have because a note went into the spare quarter (0: none). Reading it
    // clears it.
    int takeLengthRequest() noexcept { const int r = lengthRequest; lengthRequest = 0; return r; }

    // tick < 0 hides the playhead.
    void setPlayheadTick (int tick);

    // Where Play starts in Pattern mode (a small marker on the ruler while stopped), and the loop region of the pattern
    // (start < 0: none). Neither calls back.
    void setStartTick (int tick);
    void setLoopRegion (int startTick, int endTick);
    bool cancelLoopMode(); // Escape during Move / Resize from the menu: true if there was one to cancel

    // --- the toolbar's settings (0.23.0) ---
    void setGridTicks (int ticks);                 // the snap: 1920 (bar) ... 1 (none)
    int getGridTicks() const noexcept { return gridTicks; }
    void setScale (const theory::Scale& s, bool snapNotes); // the scale shown on the grid; snapNotes: new and moved notes go into it
    void setChordStamp (int chord, int inversion); // chord < 0: a click makes one note
    // 0.62.0: the event lane shows one of the notes' properties (as FL Studio's event editor): velocity, pan, release or fine
    // pitch. Click the lane's label (bottom left) to choose.
    enum class Lane { Velocity, Pan, Release, FinePitch };
    void setLane (Lane newLane);
    Lane getLane() const noexcept { return eventLane; }
    static const char* laneName (Lane l) noexcept;
    // 0.62.0: the tools, as FL Studio's: Draw (the roll's usual behaviour), Paint (drag to draw a row of notes), Mute (click a
    // note to mute or unmute it), Slice (click a note to cut it in two).
    enum class Tool { Draw, Paint, Mute, Slice };
    void setTool (Tool newTool);
    Tool getTool() const noexcept { return activeTool; }
    std::function<void()> onToolChanged;
    // 0.62.0: the colour group (0..15) of the selected notes and of the notes drawn next.
    void setNoteColour (int colour);
    // 0.62.0: the LFO tool, as FL Studio's: the lane's property of the selected notes (or all) follows a sine, one cycle every
    // `cycleTicks`, from the first of those notes; `depth` 0..1 around the middle of the lane.
    void applyLaneLfo (int cycleTicks, float depth);
    int getNoteColour() const noexcept { return currentColour; }
    static juce::Colour noteColour (int colour) noexcept;
    void setVelocityLaneVisible (bool visible);
    bool isVelocityLaneVisible() const noexcept { return velocityLane; }
    // The velocity lane's height (0.54.0): dragged by its top edge, double-click there for the default.
    void setVelocityLaneHeight (int height);
    int getVelocityLaneHeight() const noexcept { return velocityHeight; }
    std::function<void (int)> onVelocityLaneHeightChanged; // after a drag (the program remembers it)

    // --- editing (0.23.0) ---
    // Runs a tool on the notes and the selection (see MusicTheory.h), then repaints and reports the change. `what` goes to the status.
    void applyTool (const std::function<void (std::vector<Note>&, std::vector<bool>&)>& tool, const juce::String& what);
    void replaceAllNotes (std::vector<Note> newNotes, const juce::String& what); // a generator's result, selected
    void selectAll();
    void selectNone();
    int selectedCount() const noexcept;
    bool handleKey (const juce::KeyPress& key); // true if the key was used

    std::function<void (int tick)> onSeek;                   // left click on the ruler
    std::function<void (int start, int end)> onLoopChanged; // the region was drawn, moved, resized (-1, -1: deleted)
    std::function<void (const juce::String&)> onStatus;

    // Called on the message thread whenever the set of notes has changed.
    std::function<void()> onNotesChanged;

    // Notes played by hand: onNoteStarted(pitch) when a key or a note is pressed, onNoteStopped(pitch) when it is
    // released or replaced by another pitch. Every start is followed by exactly one stop.
    std::function<void (int)> onNoteStarted;
    std::function<void (int)> onNoteStopped;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void visibilityChanged() override;

private:
    // PendingEmpty (0.44.0): the button went down on empty grid; a drag turns it into Select, a release creates the note there.
    enum class Drag { None, Move, Resize, ResizeStart, Seek, LoopCreate, LoopMove, LoopResize, Select, Velocity, PendingEmpty, LaneHeight,
                      Paint }; // 0.62.0
    void createNotesAt (juce::Point<float> pos, bool noSnap);
    juce::Point<float> pendingPosition;
    bool pendingNoSnap = false;
    enum class LoopMode { None, Move, Resize }; // following the mouse after the region's menu

    juce::Rectangle<int> rulerArea() const;
    juce::Rectangle<int> velocityArea() const; // empty when the lane is hidden
    int rulerSnap (double tick, bool noSnap) const noexcept;
    bool overLoop (int x) const noexcept;
    int loopEdgeAt (int x) const noexcept; // -1 start edge, +1 end edge, 0 none
    void placeLoop (int start, int end);
    void announceLoop();
    void showLoopMenu();
    void followLoopMode (int x);

    static constexpr int kLowPitch = 0;    // C-1: every MIDI key (0.22.0; before, C2 to C7 only, so low bass notes were off the roll)
    static constexpr int kHighPitch = 127; // G9
    static constexpr int kKeyboardWidth = 64;
    static constexpr int kRulerHeight = 22;
    static constexpr int kRowHeight = 16;
    static constexpr int kVelocityHeight = 70;    // the velocity lane's height to start with (0.54.0: the user drags its top edge)
    static constexpr int kMinVelocityHeight = 36;
    static constexpr int kMinGridHeight = 64;      // the lane never grows over the notes beyond this
    int laneHeight() const noexcept;               // the lane's height as drawn (fits the panel)
    bool onLaneSplitter (juce::Point<int> p) const noexcept;
    static constexpr int kGridTicks = kTicksPerBeat / 4; // 16th notes: the loop region's step

    juce::Rectangle<int> gridArea() const;
    double pixelsPerTick() const noexcept { return pixelsPerBeat / kTicksPerBeat; }
    float tickToX (double tick) const noexcept;
    double xToTick (float x) const noexcept;
    float pitchToY (int pitch) const noexcept;
    int yToPitch (float y) const noexcept;
    int snap (double tick) const noexcept;               // to the grid
    int snapDown (double tick) const noexcept;           // the grid line at or before
    juce::Rectangle<float> noteRect (const Note& n) const noexcept;
    int noteEdgeAt (juce::Point<float> p, int index) const noexcept; // -1 on the note's left edge, +1 on its right edge, 0 inside
    int noteIndexAt (juce::Point<float> p) const noexcept;
    int soundingNoteBefore (int index) const noexcept; // the note that a legato / slide note takes over from, or -1
    void clampScroll();
    void fitWidth(); // until the user zooms, the pattern and its spare quarter fill the width
    void notifyChanged();
    void timerCallback() override; // the gliding scroll
    void sound (int pitch); // -1 stops; a different pitch stops the previous one and starts the new one
    bool isOnKeyboard (juce::Point<int> p) const noexcept;
    static bool isBlackKey (int pitch) noexcept;
    int fitPitch (int pitch) const noexcept { return scaleSnap ? scale.snap (pitch) : pitch; }
    void deleteSelected();
    void copySelected();
    void paste();
    void duplicateSelected();
    void moveSelected (int ticks, int semitones, int scaleSteps);
    void editVelocities (juce::Point<float> from, juce::Point<float> to);
    void say (const juce::String& text) const;
    juce::String describe (const Note& n) const;

    std::vector<Note> notes;
    std::vector<Note> ghostNotes; // 0.60.0
    std::vector<bool> selected; // one per note
    int loopLengthTicks = 16 * kTicksPerBeat;
    bool growable = false;
    int lengthRequest = 0;
    std::vector<Note> reportedNotes; // the notes as last set or reported: what an edit is compared with
    double targetScrollX = 0.0, targetScrollY = 0.0; // where the gliding scroll is heading
    double glideX = 0.0, glideY = 0.0;               // where it put the view last (moved by something else: the glide stops)

    double pixelsPerBeat = 56.0;
    double scrollX = 0.0;
    double scrollY = 0.0;
    bool initialScrollDone = false;
    bool userZoomed = false; // until the user zooms, the loop keeps fitting the width when the panel is resized

    int gridTicks = kTicksPerBeat / 4;
    theory::Scale scale;
    bool scaleSnap = false;
    int chordStamp = -1, chordInversion = 0;
    Lane eventLane = Lane::Velocity; // 0.62.0
    Tool activeTool = Tool::Draw;
    int currentColour = 0;
    int paintNextTick = 0;        // the Paint tool: where the next note of the stroke may start
    void showLaneMenu();
    bool velocityLane = true;
    int velocityHeight = kVelocityHeight;
    int laneGrab = 0; // while the splitter is dragged: the mouse's distance from the lane's top edge

    int playheadTick = -1;
    int startTick = 0;
    int loopStart = -1, loopEnd = -1;
    int loopOriginalStart = -1, loopOriginalEnd = -1;
    int loopGrab = 0;          // ticks from the region start to the mouse, while moving it
    int loopResizeEdge = 0;
    int loopAnchor = 0;        // where a new region was started
    bool loopDragMoved = false;
    LoopMode loopMode = LoopMode::None;
    int soundingPitch = -1;      // the note currently played by hand, -1 if none
    bool keyboardHeld = false;   // the button went down on the keyboard (not on the grid)

    Drag dragMode = Drag::None;
    int dragIndex = -1;             // the note under the mouse when the drag began
    std::vector<Note> dragOriginal; // every note as it was when the drag began (moves and resizes are computed from these)
    int grabOffsetTicks = 0;
    bool dragChanged = false;
    int lastLengthTicks = kTicksPerBeat / 2;
    juce::Rectangle<float> selectBox;
    bool selectAdds = false;
    std::vector<bool> selectionBefore;
    juce::Point<float> lastVelocityPoint;

    static std::vector<Note>& clipboard(); // shared by every piano roll of the program

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRoll)
};
} // namespace sls
