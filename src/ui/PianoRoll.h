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
//   Mouse wheel                : scroll pitch   | Shift+wheel: scroll time | Ctrl+wheel: zoom time
class PianoRoll : public juce::Component
{
public:
    PianoRoll();

    void setNotes (std::vector<Note> newNotes);
    const std::vector<Note>& getNotes() const noexcept { return notes; }

    // Scroll vertically so the current notes are centred (a channel just opened may sit far from middle C).
    void scrollToNotes();

    void setLoopLengthTicks (int ticks);
    int getLoopLengthTicks() const noexcept { return loopLengthTicks; }

    // tick < 0 hides the playhead.
    void setPlayheadTick (int tick);

    // Called on the message thread whenever the set of notes has changed.
    std::function<void()> onNotesChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    enum class Drag { None, Move, Resize };

    static constexpr int kLowPitch = 36;   // C2
    static constexpr int kHighPitch = 96;  // C7
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
    void clampScroll();
    void notifyChanged();
    static bool isBlackKey (int pitch) noexcept;

    std::vector<Note> notes;
    int loopLengthTicks = 16 * kTicksPerBeat;

    double pixelsPerBeat = 56.0;
    double scrollX = 0.0;
    double scrollY = 0.0;
    bool initialScrollDone = false;
    bool userZoomed = false; // until the user zooms, the loop keeps fitting the width when the panel is resized

    int playheadTick = -1;

    Drag dragMode = Drag::None;
    int dragIndex = -1;
    int grabOffsetTicks = 0;
    bool dragChanged = false;
    int lastLengthTicks = kTicksPerBeat / 2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRoll)
};
} // namespace sls
