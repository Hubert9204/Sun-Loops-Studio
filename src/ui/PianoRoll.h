#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/NoteSequence.h"

namespace sls
{
// Note editor.
//   Left click on empty grid   : create a note (drag right to set its length)
//   Left drag on a note        : move it (drag its right edge to resize)
//   Right click on a note      : delete it
//   Shift + click on a note    : changes how it joins the note before it: normal -> legato (blue: the sound goes on, the pitch
//                                jumps) -> slide (amber: the pitch glides, drawn as a line from the old pitch) -> normal. The note before must still be sounding
//                                when the linked note starts (turning legato on lengthens a note that stops up to a 16th early)
//   Left button on a key       : plays that note for as long as the button is down (slide to change the note)
//   Left button on the grid    : the note being created, moved or resized sounds while the button is down
//   Mouse wheel                : scroll pitch   | Shift+wheel: scroll time | Ctrl+wheel: zoom time
// The ruler (0.20.0), as in the playlist:
//   Left click / drag          : move the pattern position there (also while playing; snapped to 16ths, Alt: no snap)
//   Right drag                 : draw a loop region; Pattern mode then repeats only that part of the pattern
//   Right drag on the region   : move it (on one of its edges: resize it)
//   Right click on the region  : menu Move / Resize / Delete loop region (Delete removes only the highlight)
class PianoRoll : public juce::Component
{
public:
    PianoRoll();

    void setNotes (std::vector<Note> newNotes);
    const std::vector<Note>& getNotes() const noexcept { return notes; }

    // Scroll vertically so the current notes are centred (a channel just opened may sit far from middle C).
    void scrollToNotes();

    void setLoopLengthTicks (int ticks);

    // tick < 0 hides the playhead.
    void setPlayheadTick (int tick);

    // Where Play starts in Pattern mode (a small marker on the ruler while stopped), and the loop region of the pattern
    // (start < 0: none). Neither calls back.
    void setStartTick (int tick);
    void setLoopRegion (int startTick, int endTick);
    int getLoopStart() const noexcept { return loopStart; }
    int getLoopEnd() const noexcept { return loopEnd; }
    bool cancelLoopMode(); // Escape during Move / Resize from the menu: true if there was one to cancel

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
    enum class Drag { None, Move, Resize, Seek, LoopCreate, LoopMove, LoopResize };
    enum class LoopMode { None, Move, Resize }; // following the mouse after the region's menu

    juce::Rectangle<int> rulerArea() const;
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
    static constexpr int kGridTicks = kTicksPerBeat / 4; // 16th notes

    juce::Rectangle<int> gridArea() const;
    double pixelsPerTick() const noexcept { return pixelsPerBeat / kTicksPerBeat; }
    float tickToX (double tick) const noexcept;
    double xToTick (float x) const noexcept;
    float pitchToY (int pitch) const noexcept;
    int yToPitch (float y) const noexcept;
    int snap (double tick) const noexcept;
    juce::Rectangle<float> noteRect (const Note& n) const noexcept;
    int noteIndexAt (juce::Point<float> p) const noexcept;
    int soundingNoteBefore (int index) const noexcept; // the note that a legato / slide note takes over from, or -1
    void clampScroll();
    void notifyChanged();
    void sound (int pitch); // -1 stops; a different pitch stops the previous one and starts the new one
    bool isOnKeyboard (juce::Point<int> p) const noexcept;
    static bool isBlackKey (int pitch) noexcept;

    std::vector<Note> notes;
    int loopLengthTicks = 16 * kTicksPerBeat;

    double pixelsPerBeat = 56.0;
    double scrollX = 0.0;
    double scrollY = 0.0;
    bool initialScrollDone = false;
    bool userZoomed = false; // until the user zooms, the loop keeps fitting the width when the panel is resized

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
    int dragIndex = -1;
    int grabOffsetTicks = 0;
    bool dragChanged = false;
    int lastLengthTicks = kTicksPerBeat / 2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRoll)
};
} // namespace sls
