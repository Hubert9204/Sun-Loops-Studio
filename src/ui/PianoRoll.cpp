#include "ui/PianoRoll.h"

#include <algorithm>
#include <cmath>

namespace sls
{
namespace
{
const juce::Colour kBackground { 0xff23262d };
const juce::Colour kRowWhite { 0xff2c3038 };
const juce::Colour kRowBlack { 0xff262930 };
const juce::Colour kLineSub { 0x14ffffff };
const juce::Colour kLineBeat { 0x30ffffff };
const juce::Colour kLineBar { 0x66ffffff };
const juce::Colour kNote { 0xff4cc38a };
const juce::Colour kNoteSelected { 0xff8fe8b8 };
const juce::Colour kPlayhead { 0xffffb347 };
} // namespace

PianoRoll::PianoRoll()
{
    setOpaque (true);
}

bool PianoRoll::isBlackKey (int pitch) noexcept
{
    switch (((pitch % 12) + 12) % 12)
    {
        case 1: case 3: case 6: case 8: case 10: return true;
        default: return false;
    }
}

void PianoRoll::setNotes (std::vector<Note> newNotes)
{
    notes = std::move (newNotes);
    sound (-1);
    keyboardHeld = false;
    dragMode = Drag::None;
    dragIndex = -1;
    repaint();
}

void PianoRoll::scrollToNotes()
{
    if (notes.empty() || getHeight() <= 0)
        return;

    int lo = kHighPitch, hi = kLowPitch;
    for (const auto& n : notes)
    {
        lo = std::min (lo, n.pitch);
        hi = std::max (hi, n.pitch);
    }

    const double centrePitch = 0.5 * (lo + hi);
    const double visibleRows = static_cast<double> (gridArea().getHeight()) / kRowHeight;
    const double topPitch = centrePitch + visibleRows / 2.0;
    scrollY = (kHighPitch - topPitch) * kRowHeight;
    clampScroll();
    repaint();
}

void PianoRoll::setLoopLengthTicks (int ticks)
{
    loopLengthTicks = std::max (kGridTicks, ticks);
    clampScroll();
    repaint();
}

void PianoRoll::setPlayheadTick (int tick)
{
    if (tick != playheadTick)
    {
        playheadTick = tick;
        repaint();
    }
}

juce::Rectangle<int> PianoRoll::gridArea() const
{
    return getLocalBounds().withTrimmedLeft (kKeyboardWidth).withTrimmedTop (kRulerHeight);
}

float PianoRoll::tickToX (double tick) const noexcept
{
    return static_cast<float> (kKeyboardWidth + tick * pixelsPerTick() - scrollX);
}

double PianoRoll::xToTick (float x) const noexcept
{
    return (x - kKeyboardWidth + scrollX) / pixelsPerTick();
}

float PianoRoll::pitchToY (int pitch) const noexcept
{
    return static_cast<float> (kRulerHeight + (kHighPitch - pitch) * kRowHeight - scrollY);
}

int PianoRoll::yToPitch (float y) const noexcept
{
    const int row = static_cast<int> (std::floor ((y - kRulerHeight + scrollY) / kRowHeight));
    return std::clamp (kHighPitch - row, kLowPitch, kHighPitch);
}

int PianoRoll::snap (double tick) const noexcept
{
    return static_cast<int> (std::llround (tick / kGridTicks)) * kGridTicks;
}

juce::Rectangle<float> PianoRoll::noteRect (const Note& n) const noexcept
{
    const float x = tickToX (n.startTick);
    const float w = static_cast<float> (n.lengthTicks * pixelsPerTick());
    return { x, pitchToY (n.pitch) + 1.0f, std::max (3.0f, w - 1.0f), static_cast<float> (kRowHeight - 2) };
}

int PianoRoll::noteIndexAt (juce::Point<float> p) const noexcept
{
    for (int i = static_cast<int> (notes.size()) - 1; i >= 0; --i)
        if (noteRect (notes[static_cast<size_t> (i)]).contains (p))
            return i;
    return -1;
}

void PianoRoll::clampScroll()
{
    const auto grid = gridArea();
    const double totalH = (kHighPitch - kLowPitch + 1) * kRowHeight;
    const double totalW = loopLengthTicks * pixelsPerTick();
    scrollY = std::clamp (scrollY, 0.0, std::max (0.0, totalH - grid.getHeight()));
    scrollX = std::clamp (scrollX, 0.0, std::max (0.0, totalW - grid.getWidth()));
}

void PianoRoll::resized()
{
    if (getHeight() > 0)
    {
        if (! initialScrollDone)
        {
            // Start around middle C.
            scrollY = (kHighPitch - 72) * kRowHeight;
            initialScrollDone = true;
        }

        if (! userZoomed)
        {
            // Zoom so the whole loop fits the width; follows the panel while it is being resized.
            const double beats = static_cast<double> (loopLengthTicks) / kTicksPerBeat;
            pixelsPerBeat = std::clamp (static_cast<double> (gridArea().getWidth()) / beats, 16.0, 240.0);
        }
    }
    clampScroll();
}

void PianoRoll::sound (int pitch)
{
    if (pitch == soundingPitch)
        return;

    const int previous = soundingPitch;
    soundingPitch = pitch;
    if (previous >= 0 && onNoteStopped)
        onNoteStopped (previous);
    if (pitch >= 0 && onNoteStarted)
        onNoteStarted (pitch);
    repaint (0, 0, kKeyboardWidth, getHeight());
}

bool PianoRoll::isOnKeyboard (juce::Point<int> p) const noexcept
{
    return p.x >= 0 && p.x < kKeyboardWidth && p.y >= kRulerHeight && p.y < getHeight();
}

void PianoRoll::visibilityChanged()
{
    if (! isShowing())
        sound (-1); // never leave a note hanging when the panel is closed under the mouse
}

void PianoRoll::notifyChanged()
{
    if (onNotesChanged)
        onNotesChanged();
}

void PianoRoll::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);
    const auto grid = gridArea();

    // --- grid ---------------------------------------------------------------------
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (grid);

        for (int p = kLowPitch; p <= kHighPitch; ++p)
        {
            const float y = pitchToY (p);
            if (y + kRowHeight < grid.getY() || y > grid.getBottom())
                continue;
            g.setColour (isBlackKey (p) ? kRowBlack : kRowWhite);
            g.fillRect (static_cast<float> (grid.getX()), y, static_cast<float> (grid.getWidth()), static_cast<float> (kRowHeight));
            g.setColour (kLineSub);
            g.drawHorizontalLine (static_cast<int> (y), static_cast<float> (grid.getX()), static_cast<float> (grid.getRight()));
        }

        const int firstTick = std::max (0, static_cast<int> (xToTick (static_cast<float> (grid.getX()))) / kGridTicks * kGridTicks);
        for (int t = firstTick; t <= loopLengthTicks; t += kGridTicks)
        {
            const float x = tickToX (t);
            if (x > grid.getRight())
                break;
            g.setColour (t % (4 * kTicksPerBeat) == 0 ? kLineBar : t % kTicksPerBeat == 0 ? kLineBeat : kLineSub);
            g.drawVerticalLine (static_cast<int> (x), static_cast<float> (grid.getY()), static_cast<float> (grid.getBottom()));
        }

        // Everything after the loop end is dimmed.
        const float loopEndX = tickToX (loopLengthTicks);
        if (loopEndX < grid.getRight())
        {
            g.setColour (juce::Colour (0xaa000000));
            g.fillRect (loopEndX, static_cast<float> (grid.getY()), grid.getRight() - loopEndX, static_cast<float> (grid.getHeight()));
        }

        // Notes.
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const auto r = noteRect (notes[i]);
            const bool selected = static_cast<int> (i) == dragIndex && dragMode != Drag::None;
            g.setColour (kNote.withAlpha (0.45f + 0.55f * notes[i].velocity));
            if (selected)
                g.setColour (kNoteSelected);
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.drawRoundedRectangle (r, 2.0f, 1.0f);
        }

        // Playhead.
        if (playheadTick >= 0)
        {
            const float x = tickToX (playheadTick);
            g.setColour (kPlayhead);
            g.drawVerticalLine (static_cast<int> (x), static_cast<float> (grid.getY()), static_cast<float> (grid.getBottom()));
        }
    }

    // --- ruler --------------------------------------------------------------------
    {
        const auto ruler = getLocalBounds().withTrimmedLeft (kKeyboardWidth).removeFromTop (kRulerHeight);
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (ruler);
        g.setColour (juce::Colour (0xff1b1d22));
        g.fillRect (ruler);
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.setFont (11.0f);
        for (int t = 0; t < loopLengthTicks; t += 4 * kTicksPerBeat)
        {
            const float x = tickToX (t);
            g.drawVerticalLine (static_cast<int> (x), static_cast<float> (ruler.getY() + 4), static_cast<float> (ruler.getBottom()));
            g.drawText (juce::String (t / (4 * kTicksPerBeat) + 1), static_cast<int> (x) + 4, ruler.getY(), 40, ruler.getHeight(),
                        juce::Justification::centredLeft);
        }
    }

    // --- keyboard -----------------------------------------------------------------
    {
        const auto keys = getLocalBounds().withTrimmedTop (kRulerHeight).removeFromLeft (kKeyboardWidth);
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (keys);
        g.setFont (10.0f);
        for (int p = kLowPitch; p <= kHighPitch; ++p)
        {
            const float y = pitchToY (p);
            if (y + kRowHeight < keys.getY() || y > keys.getBottom())
                continue;
            const bool black = isBlackKey (p);
            g.setColour (p == soundingPitch ? kNoteSelected
                                            : (black ? juce::Colour (0xff15171b) : juce::Colour (0xffe8e8ea)));
            g.fillRect (0.0f, y, black ? kKeyboardWidth * 0.62f : static_cast<float> (kKeyboardWidth), static_cast<float> (kRowHeight));
            g.setColour (juce::Colour (0xff15171b));
            g.drawHorizontalLine (static_cast<int> (y), 0.0f, static_cast<float> (kKeyboardWidth));
            if (p % 12 == 0)
            {
                g.setColour (juce::Colours::black.withAlpha (0.8f));
                g.drawText ("C" + juce::String (p / 12 - 1), kKeyboardWidth - 26, static_cast<int> (y), 22, kRowHeight,
                            juce::Justification::centredRight);
            }
        }
        g.setColour (juce::Colour (0xff15171b));
        g.fillRect (kKeyboardWidth - 1, keys.getY(), 1, keys.getHeight());
    }

    g.setColour (juce::Colour (0xff15171b));
    g.fillRect (0, 0, kKeyboardWidth, kRulerHeight);
}

void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    if (isOnKeyboard (e.getPosition()))
    {
        if (! (e.mods.isRightButtonDown() || e.mods.isPopupMenu()))
        {
            keyboardHeld = true;
            sound (yToPitch (e.position.y));
        }
        return;
    }

    if (! gridArea().contains (e.getPosition()))
        return;

    const auto pos = e.position;
    const int hit = noteIndexAt (pos);

    if (e.mods.isRightButtonDown() || e.mods.isPopupMenu())
    {
        if (hit >= 0)
        {
            notes.erase (notes.begin() + hit);
            repaint();
            notifyChanged();
        }
        return;
    }

    dragChanged = false;

    if (hit >= 0)
    {
        auto& n = notes[static_cast<size_t> (hit)];
        dragIndex = hit;
        const bool nearRightEdge = pos.x > noteRect (n).getRight() - 6.0f;
        dragMode = nearRightEdge ? Drag::Resize : Drag::Move;
        grabOffsetTicks = static_cast<int> (xToTick (pos.x)) - n.startTick;
        sound (n.pitch);
        repaint();
        return;
    }

    const int tick = snap (xToTick (pos.x) - kGridTicks * 0.5);
    if (tick < 0 || tick >= loopLengthTicks)
        return;

    Note n;
    n.pitch = yToPitch (pos.y);
    n.startTick = tick;
    n.lengthTicks = std::min (lastLengthTicks, loopLengthTicks - tick);
    n.velocity = 0.8f;
    notes.push_back (n);

    dragIndex = static_cast<int> (notes.size()) - 1;
    dragMode = Drag::Resize;
    dragChanged = true; // creation is a change even without dragging
    sound (n.pitch);
    repaint();
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    if (keyboardHeld)
    {
        sound (yToPitch (e.position.y)); // sliding over the keys plays each one in turn
        return;
    }

    if (dragMode == Drag::None || dragIndex < 0 || dragIndex >= static_cast<int> (notes.size()))
        return;

    auto& n = notes[static_cast<size_t> (dragIndex)];

    if (dragMode == Drag::Move)
    {
        const int start = std::clamp (snap (xToTick (e.position.x) - grabOffsetTicks), 0, loopLengthTicks - n.lengthTicks);
        const int pitch = yToPitch (e.position.y);
        if (start != n.startTick || pitch != n.pitch)
        {
            n.startTick = start;
            n.pitch = pitch;
            dragChanged = true;
            sound (pitch);
        }
    }
    else
    {
        const int end = snap (xToTick (e.position.x));
        const int length = std::clamp (end - n.startTick, kGridTicks, loopLengthTicks - n.startTick);
        if (length != n.lengthTicks)
        {
            n.lengthTicks = length;
            lastLengthTicks = length;
            dragChanged = true;
        }
    }
    repaint();
}

void PianoRoll::mouseUp (const juce::MouseEvent&)
{
    sound (-1);
    keyboardHeld = false;
    const bool changed = dragChanged;
    dragMode = Drag::None;
    dragIndex = -1;
    dragChanged = false;
    repaint();
    if (changed)
        notifyChanged();
}

void PianoRoll::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        const double anchorTick = xToTick (e.position.x);
        userZoomed = true;
        pixelsPerBeat = std::clamp (pixelsPerBeat * std::pow (1.25, wheel.deltaY * 4.0), 16.0, 240.0);
        scrollX += tickToX (anchorTick) - e.position.x; // keep the tick under the cursor
    }
    else if (e.mods.isShiftDown())
    {
        scrollX -= wheel.deltaY * 400.0;
    }
    else
    {
        scrollY -= wheel.deltaY * 400.0;
    }
    clampScroll();
    repaint();
}
} // namespace sls
