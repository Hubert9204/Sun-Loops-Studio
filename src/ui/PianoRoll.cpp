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
const juce::Colour kNoteLegato { 0xff4ca6e0 };
const juce::Colour kNoteSlide { 0xffe8a23a };
const juce::Colour kPlayhead { 0xffffb347 };
const juce::Colour kLoop { 0xff4a8fd8 };
const juce::Colour kOutOfScale { 0x55000000 }; // rows of notes outside the scale
const juce::Colour kScaleRoot { 0x184cc38a };  // rows of the scale's root
const juce::Colour kVelocityBack { 0xff1d2025 };
} // namespace

std::vector<Note>& PianoRoll::clipboard()
{
    static std::vector<Note> notes;
    return notes;
}

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
    selected.assign (notes.size(), false);
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

void PianoRoll::setStartTick (int tick)
{
    tick = std::clamp (tick, 0, loopLengthTicks);
    if (tick != startTick)
    {
        startTick = tick;
        repaint (rulerArea());
    }
}

void PianoRoll::setLoopRegion (int start, int end)
{
    if (start < 0 || end <= start || end > loopLengthTicks)
        start = end = -1;
    if (start != loopStart || end != loopEnd)
    {
        loopStart = start;
        loopEnd = end;
        if (loopStart < 0)
            loopMode = LoopMode::None;
        repaint();
    }
}

juce::Rectangle<int> PianoRoll::rulerArea() const
{
    return getLocalBounds().withTrimmedLeft (kKeyboardWidth).removeFromTop (kRulerHeight);
}

int PianoRoll::rulerSnap (double tick, bool noSnap) const noexcept
{
    const int t = noSnap ? static_cast<int> (std::llround (tick)) : static_cast<int> (std::llround (tick / kGridTicks)) * kGridTicks;
    return std::clamp (t, 0, loopLengthTicks);
}

bool PianoRoll::overLoop (int x) const noexcept
{
    return loopStart >= 0 && x >= tickToX (loopStart) && x <= tickToX (loopEnd);
}

int PianoRoll::loopEdgeAt (int x) const noexcept
{
    if (loopStart < 0)
        return 0;
    if (std::abs (static_cast<float> (x) - tickToX (loopStart)) <= 5.0f)
        return -1;
    if (std::abs (static_cast<float> (x) - tickToX (loopEnd)) <= 5.0f)
        return 1;
    return 0;
}

void PianoRoll::placeLoop (int start, int end)
{
    start = std::clamp (start, 0, loopLengthTicks);
    end = std::clamp (end, 0, loopLengthTicks);
    if (start == loopStart && end == loopEnd)
        return;
    loopStart = start;
    loopEnd = end;
    repaint();
}

void PianoRoll::announceLoop()
{
    if (onLoopChanged)
        onLoopChanged (loopStart, loopEnd);
}

bool PianoRoll::cancelLoopMode()
{
    if (loopMode == LoopMode::None)
        return false;
    loopMode = LoopMode::None;
    placeLoop (loopOriginalStart, loopOriginalEnd);
    setMouseCursor (juce::MouseCursor::NormalCursor);
    if (onStatus)
        onStatus ("Loop region left as it was");
    return true;
}

void PianoRoll::showLoopMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Loop region");
    menu.addItem (1, "Move");
    menu.addItem (2, "Resize");
    menu.addSeparator();
    menu.addItem (3, "Delete loop region");
    juce::Component::SafePointer<PianoRoll> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe] (int choice)
    {
        if (safe == nullptr || safe->loopStart < 0 || choice == 0)
            return;
        auto& self = *safe;
        const int mouseTick = self.rulerSnap (self.xToTick (static_cast<float> (self.getMouseXYRelative().x)), true);
        if (choice == 3)
        {
            self.loopStart = self.loopEnd = -1;
            self.repaint();
            self.announceLoop();
            return;
        }
        self.loopOriginalStart = self.loopStart;
        self.loopOriginalEnd = self.loopEnd;
        if (choice == 1)
        {
            self.loopMode = LoopMode::Move;
            self.loopGrab = std::clamp (mouseTick - self.loopStart, 0, self.loopEnd - self.loopStart);
            self.setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        }
        else
        {
            self.loopMode = LoopMode::Resize;
            self.loopResizeEdge = mouseTick < (self.loopStart + self.loopEnd) / 2 ? -1 : 1;
            self.setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        }
        if (self.onStatus)
            self.onStatus (choice == 1 ? "Moving the loop region: click to place it (right click or Escape: cancel)"
                                       : "Resizing the loop region: click to finish (right click or Escape: cancel)");
    });
}

void PianoRoll::followLoopMode (int x)
{
    if (loopMode == LoopMode::None || loopStart < 0)
        return;
    const int mouseTick = static_cast<int> (std::llround (xToTick (static_cast<float> (x))));
    const int length = loopEnd - loopStart;
    if (loopMode == LoopMode::Move)
    {
        const int start = std::clamp (rulerSnap (mouseTick - loopGrab, false), 0, loopLengthTicks - length);
        placeLoop (start, start + length);
    }
    else if (loopResizeEdge < 0)
        placeLoop (std::min (rulerSnap (mouseTick, false), loopEnd - kGridTicks), loopEnd);
    else
        placeLoop (loopStart, std::max (rulerSnap (mouseTick, false), loopStart + kGridTicks));
}

void PianoRoll::mouseMove (const juce::MouseEvent& e)
{
    if (loopMode != LoopMode::None)
    {
        followLoopMode (e.x);
        return;
    }
    if (rulerArea().contains (e.getPosition()))
        setMouseCursor (loopEdgeAt (e.x) != 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    else if (gridArea().contains (e.getPosition()))
    {
        // On the side edges of a note: the resize cursor (0.32.0).
        const int hit = noteIndexAt (e.position);
        setMouseCursor (hit >= 0 && noteEdgeAt (e.position, hit) != 0 ? juce::MouseCursor::LeftRightResizeCursor
                                                                       : juce::MouseCursor::NormalCursor);
    }
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

juce::Rectangle<int> PianoRoll::gridArea() const
{
    return getLocalBounds().withTrimmedLeft (kKeyboardWidth).withTrimmedTop (kRulerHeight).withTrimmedBottom (velocityLane ? kVelocityHeight : 0);
}

juce::Rectangle<int> PianoRoll::velocityArea() const
{
    if (! velocityLane)
        return {};
    return getLocalBounds().withTrimmedLeft (kKeyboardWidth).removeFromBottom (kVelocityHeight);
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
    return static_cast<int> (std::llround (tick / gridTicks)) * gridTicks;
}

int PianoRoll::snapDown (double tick) const noexcept
{
    return static_cast<int> (std::floor (tick / gridTicks)) * gridTicks;
}

juce::Rectangle<float> PianoRoll::noteRect (const Note& n) const noexcept
{
    const float x = tickToX (n.startTick);
    const float w = static_cast<float> (n.lengthTicks * pixelsPerTick());
    return { x, pitchToY (n.pitch) + 1.0f, std::max (3.0f, w - 1.0f), static_cast<float> (kRowHeight - 2) };
}

int PianoRoll::noteEdgeAt (juce::Point<float> p, int index) const noexcept
{
    if (index < 0 || index >= static_cast<int> (notes.size()))
        return 0;
    const auto r = noteRect (notes[static_cast<size_t> (index)]);
    const float zone = std::min (6.0f, r.getWidth() / 3.0f); // a short note keeps a middle to grab
    if (p.x >= r.getRight() - zone)
        return 1;
    if (p.x <= r.getX() + zone)
        return -1;
    return 0;
}

int PianoRoll::noteIndexAt (juce::Point<float> p) const noexcept
{
    for (int i = static_cast<int> (notes.size()) - 1; i >= 0; --i)
        if (noteRect (notes[static_cast<size_t> (i)]).contains (p))
            return i;
    return -1;
}

int PianoRoll::soundingNoteBefore (int index) const noexcept
{
    const auto& n = notes[static_cast<size_t> (index)];
    int best = -1;
    for (int i = 0; i < static_cast<int> (notes.size()); ++i)
    {
        const auto& o = notes[static_cast<size_t> (i)];
        if (i != index && o.startTick < n.startTick && o.startTick + o.lengthTicks >= n.startTick
            && (best < 0 || o.startTick > notes[static_cast<size_t> (best)].startTick))
            best = i;
    }
    return best;
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
    return p.x >= 0 && p.x < kKeyboardWidth && p.y >= kRulerHeight && p.y < gridArea().getBottom();
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
            if (scale.type != 0 && ! scale.contains (p))
            {
                g.setColour (kOutOfScale); // a note outside the scale: darker row
                g.fillRect (static_cast<float> (grid.getX()), y, static_cast<float> (grid.getWidth()), static_cast<float> (kRowHeight));
            }
            else if (scale.type != 0 && scale.isRoot (p))
            {
                g.setColour (kScaleRoot);
                g.fillRect (static_cast<float> (grid.getX()), y, static_cast<float> (grid.getWidth()), static_cast<float> (kRowHeight));
            }
            g.setColour (kLineSub);
            g.drawHorizontalLine (static_cast<int> (y), static_cast<float> (grid.getX()), static_cast<float> (grid.getRight()));
        }

        // Lines: every bar and beat, and the snap grid when it is at least 5 pixels wide.
        const int lineStep = gridTicks * pixelsPerTick() >= 5.0 ? gridTicks : kTicksPerBeat;
        const int firstTick = std::max (0, static_cast<int> (xToTick (static_cast<float> (grid.getX()))) / lineStep * lineStep);
        for (int t = firstTick; t <= loopLengthTicks; t += lineStep)
        {
            const float x = tickToX (t);
            if (x > grid.getRight())
                break;
            g.setColour (t % (4 * kTicksPerBeat) == 0 ? kLineBar : t % kTicksPerBeat == 0 ? kLineBeat : kLineSub);
            g.drawVerticalLine (static_cast<int> (x), static_cast<float> (grid.getY()), static_cast<float> (grid.getBottom()));
        }
        if (lineStep != kTicksPerBeat && kTicksPerBeat % lineStep != 0) // triplets: the beats too
            for (int t = 0; t <= loopLengthTicks; t += kTicksPerBeat)
            {
                g.setColour (t % (4 * kTicksPerBeat) == 0 ? kLineBar : kLineBeat);
                g.drawVerticalLine (static_cast<int> (tickToX (t)), static_cast<float> (grid.getY()), static_cast<float> (grid.getBottom()));
            }

        // Everything after the loop end is dimmed.
        const float loopEndX = tickToX (loopLengthTicks);
        if (loopEndX < grid.getRight())
        {
            g.setColour (juce::Colour (0xaa000000));
            g.fillRect (loopEndX, static_cast<float> (grid.getY()), grid.getRight() - loopEndX, static_cast<float> (grid.getHeight()));
        }

        // The loop region, faintly over the grid.
        if (loopStart >= 0)
        {
            g.setColour (kLoop.withAlpha (0.10f));
            g.fillRect (tickToX (loopStart), static_cast<float> (grid.getY()), tickToX (loopEnd) - tickToX (loopStart), static_cast<float> (grid.getHeight()));
        }

        // Notes.
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const auto r = noteRect (notes[i]);
            const bool isSelected = i < selected.size() && selected[i];
            const auto base = notes[i].link == NoteLink::Slide ? kNoteSlide : notes[i].link == NoteLink::Legato ? kNoteLegato : kNote;
            g.setColour (base.withAlpha (0.45f + 0.55f * notes[i].velocity));
            if (isSelected)
                g.setColour (kNoteSelected);
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (isSelected ? juce::Colours::white.withAlpha (0.95f) : juce::Colours::black.withAlpha (0.5f));
            g.drawRoundedRectangle (r, 2.0f, isSelected ? 1.5f : 1.0f);

            if (notes[i].link != NoteLink::None)
            {
                const int from = soundingNoteBefore (static_cast<int> (i));
                if (from >= 0)
                {
                    // the join: a straight tie for legato, a line gliding from the old pitch for slide
                    const auto old = noteRect (notes[static_cast<size_t> (from)]);
                    const float glide = notes[i].link == NoteLink::Slide
                                            ? static_cast<float> (std::min (notes[i].lengthTicks, kTicksPerBeat / 2) * pixelsPerTick()) : 0.0f;
                    g.setColour (juce::Colours::white.withAlpha (0.85f));
                    g.drawLine (r.getX(), old.getCentreY(), r.getX() + glide, r.getCentreY(), 1.5f);
                }
                if (r.getWidth() > 14.0f)
                {
                    g.setColour (juce::Colours::black.withAlpha (0.75f));
                    g.setFont (10.0f);
                    g.drawText (notes[i].link == NoteLink::Slide ? "S" : "L", r.withTrimmedLeft (3.0f), juce::Justification::centredLeft);
                }
            }
        }

        // The selection rectangle.
        if (dragMode == Drag::Select)
        {
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.fillRect (selectBox);
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.drawRect (selectBox, 1.0f);
        }

        // Playhead.
        if (playheadTick >= 0)
        {
            const float x = tickToX (playheadTick);
            g.setColour (kPlayhead);
            g.drawVerticalLine (static_cast<int> (x), static_cast<float> (grid.getY()), static_cast<float> (grid.getBottom()));
        }
    }

    // --- velocity lane ------------------------------------------------------------
    if (velocityLane)
    {
        const auto lane = velocityArea();
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (lane);
        g.setColour (kVelocityBack);
        g.fillRect (lane);
        g.setColour (juce::Colour (0xff15171b));
        g.fillRect (lane.getX(), lane.getY(), lane.getWidth(), 2);
        g.setColour (kLineSub);
        for (const float level : { 0.25f, 0.5f, 0.75f })
            g.drawHorizontalLine (lane.getBottom() - 4 - static_cast<int> (level * static_cast<float> (lane.getHeight() - 8)),
                                  static_cast<float> (lane.getX()), static_cast<float> (lane.getRight()));
        const bool anySelected = selectedCount() > 0;
        for (int pass = 0; pass < 2; ++pass) // unselected notes first, the selected ones on top
            for (size_t i = 0; i < notes.size(); ++i)
            {
                const bool isSelected = i < selected.size() && selected[i];
                if ((pass == 1) != isSelected)
                    continue;
                const float x = tickToX (notes[i].startTick);
                const float top = static_cast<float> (lane.getBottom() - 4) - notes[i].velocity * static_cast<float> (lane.getHeight() - 8);
                const auto colour = isSelected ? kNoteSelected : (anySelected ? kNote.withAlpha (0.35f) : kNote);
                g.setColour (colour);
                g.fillRect (x, top, 2.0f, static_cast<float> (lane.getBottom() - 4) - top);
                g.fillEllipse (x - 3.0f, top - 3.0f, 8.0f, 8.0f);
            }
    }

    // --- ruler --------------------------------------------------------------------
    {
        const auto ruler = getLocalBounds().withTrimmedLeft (kKeyboardWidth).removeFromTop (kRulerHeight);
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (ruler);
        g.setColour (juce::Colour (0xff1b1d22));
        g.fillRect (ruler);
        if (loopStart >= 0)
        {
            const float x0 = tickToX (loopStart), x1 = tickToX (loopEnd);
            g.setColour (kLoop.withAlpha (0.45f));
            g.fillRect (x0, static_cast<float> (ruler.getY()), x1 - x0, static_cast<float> (ruler.getHeight()));
            g.setColour (kLoop);
            g.fillRect (x0, static_cast<float> (ruler.getY()), 2.0f, static_cast<float> (ruler.getHeight()));
            g.fillRect (x1 - 2.0f, static_cast<float> (ruler.getY()), 2.0f, static_cast<float> (ruler.getHeight()));
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            if (x1 - x0 > 34.0f)
                g.drawText ("LOOP", static_cast<int> (x0) + 4, ruler.getBottom() - 11, 40, 10, juce::Justification::centredLeft);
        }
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.setFont (11.0f);
        for (int t = 0; t < loopLengthTicks; t += 4 * kTicksPerBeat)
        {
            const float x = tickToX (t);
            g.drawVerticalLine (static_cast<int> (x), static_cast<float> (ruler.getY() + 4), static_cast<float> (ruler.getBottom()));
            g.drawText (juce::String (t / (4 * kTicksPerBeat) + 1), static_cast<int> (x) + 4, ruler.getY(), 40, ruler.getHeight(),
                        juce::Justification::centredLeft);
        }
        // Where Play starts (stopped) or the playhead (playing).
        const float sx = tickToX (playheadTick >= 0 ? playheadTick : startTick);
        juce::Path marker;
        marker.addTriangle (sx - 5.0f, static_cast<float> (ruler.getY() + 2), sx + 5.0f, static_cast<float> (ruler.getY() + 2), sx,
                            static_cast<float> (ruler.getY() + 11));
        g.setColour (kPlayhead);
        g.fillPath (marker);
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
            if (p % 12 == 0 || (scale.type != 0 && scale.isRoot (p)))
            {
                // C on every octave, and the root of the scale (in green) when a scale is shown
                const bool root = scale.type != 0 && scale.isRoot (p);
                g.setColour (root ? juce::Colour (0xff2f8f5f) : juce::Colours::black.withAlpha (0.8f));
                g.drawText (juce::String (theory::noteName (p)), kKeyboardWidth - 30, static_cast<int> (y), 26, kRowHeight,
                            juce::Justification::centredRight);
            }
        }
        g.setColour (juce::Colour (0xff15171b));
        g.fillRect (kKeyboardWidth - 1, keys.getY(), 1, keys.getHeight());
    }

    g.setColour (juce::Colour (0xff15171b));
    g.fillRect (0, 0, kKeyboardWidth, kRulerHeight);
    if (velocityLane)
    {
        const auto corner = juce::Rectangle<int> (0, getHeight() - kVelocityHeight, kKeyboardWidth, kVelocityHeight);
        g.fillRect (corner);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.setFont (11.0f);
        g.drawText ("Velocity", corner.reduced (4), juce::Justification::centred);
    }
}

void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    const bool right = e.mods.isRightButtonDown() || e.mods.isPopupMenu();
    if (loopMode != LoopMode::None)
    {
        // Following the mouse after the menu: a left click places the region, a right click puts it back.
        if (right)
            cancelLoopMode();
        else
        {
            loopMode = LoopMode::None;
            setMouseCursor (juce::MouseCursor::NormalCursor);
            announceLoop();
        }
        return;
    }

    if (rulerArea().contains (e.getPosition()))
    {
        const int tick = rulerSnap (xToTick (e.position.x), e.mods.isAltDown());
        if (! right)
        {
            dragMode = Drag::Seek;
            if (onSeek)
                onSeek (tick);
            return;
        }
        loopDragMoved = false;
        loopOriginalStart = loopStart;
        loopOriginalEnd = loopEnd;
        if (const int edge = loopEdgeAt (e.x); edge != 0)
        {
            dragMode = Drag::LoopResize;
            loopResizeEdge = edge;
        }
        else if (overLoop (e.x))
        {
            dragMode = Drag::LoopMove;
            loopGrab = static_cast<int> (std::llround (xToTick (e.position.x))) - loopStart;
        }
        else
        {
            dragMode = Drag::LoopCreate;
            loopAnchor = tick;
        }
        return;
    }

    if (isOnKeyboard (e.getPosition()))
    {
        if (! (e.mods.isRightButtonDown() || e.mods.isPopupMenu()))
        {
            keyboardHeld = true;
            sound (yToPitch (e.position.y));
        }
        return;
    }

    if (velocityArea().contains (e.getPosition()))
    {
        if (right)
            return;
        dragMode = Drag::Velocity;
        dragChanged = false;
        lastVelocityPoint = e.position;
        editVelocities (e.position, e.position);
        return;
    }

    if (! gridArea().contains (e.getPosition()))
        return;

    const auto pos = e.position;
    const int hit = noteIndexAt (pos);
    const bool ctrl = e.mods.isCtrlDown() || e.mods.isCommandDown();
    selected.resize (notes.size(), false);

    if (hit >= 0 && e.mods.isShiftDown() && ! ctrl && ! right)
    {
        auto& link = notes[static_cast<size_t> (hit)].link; // normal -> legato -> slide -> normal
        link = link == NoteLink::None ? NoteLink::Legato : link == NoteLink::Legato ? NoteLink::Slide : NoteLink::None;
        if (link == NoteLink::Legato && soundingNoteBefore (hit) < 0)
        {
            // A legato note needs the note before it to be still sounding: if that one stops a little early (up to a 16th),
            // it is lengthened to meet this one.
            const auto& me = notes[static_cast<size_t> (hit)];
            int best = -1;
            for (int i = 0; i < static_cast<int> (notes.size()); ++i)
            {
                const auto& o = notes[static_cast<size_t> (i)];
                const int end = o.startTick + o.lengthTicks;
                if (i != hit && o.startTick < me.startTick && end < me.startTick && me.startTick - end <= kGridTicks
                    && (best < 0 || end > notes[static_cast<size_t> (best)].startTick + notes[static_cast<size_t> (best)].lengthTicks))
                    best = i;
            }
            if (best >= 0)
                notes[static_cast<size_t> (best)].lengthTicks = me.startTick - notes[static_cast<size_t> (best)].startTick;
        }
        repaint();
        notifyChanged();
        return;
    }

    if (right)
    {
        if (hit >= 0)
        {
            if (selected[static_cast<size_t> (hit)])
                deleteSelected(); // a right click on a selected note deletes the whole selection
            else
            {
                notes.erase (notes.begin() + hit);
                selected.erase (selected.begin() + hit);
                repaint();
                notifyChanged();
            }
        }
        return;
    }

    if (ctrl)
    {
        if (hit >= 0)
        {
            selected[static_cast<size_t> (hit)] = ! selected[static_cast<size_t> (hit)];
            repaint();
            say (juce::String (selectedCount()) + " note(s) selected");
        }
        else
        {
            dragMode = Drag::Select;
            selectAdds = e.mods.isShiftDown();
            selectionBefore = selected;
            selectBox = { pos, pos };
        }
        return;
    }

    dragChanged = false;

    if (hit >= 0)
    {
        if (! selected[static_cast<size_t> (hit)])
        {
            selected.assign (notes.size(), false); // a click on a note that is not selected selects only it
            selected[static_cast<size_t> (hit)] = true;
        }
        const auto& n = notes[static_cast<size_t> (hit)];
        dragIndex = hit;
        dragOriginal = notes;
        const int edge = noteEdgeAt (pos, hit);
        dragMode = edge > 0 ? Drag::Resize : (edge < 0 ? Drag::ResizeStart : Drag::Move);
        lastLengthTicks = n.lengthTicks; // the clicked note's length becomes the length of the next notes drawn (0.32.0)
        grabOffsetTicks = static_cast<int> (xToTick (pos.x)) - n.startTick;
        sound (n.pitch);
        say (describe (n));
        repaint();
        return;
    }

    const int tick = e.mods.isAltDown() ? static_cast<int> (xToTick (pos.x)) : snapDown (xToTick (pos.x));
    if (tick < 0 || tick >= loopLengthTicks)
        return;

    // A new note, or a whole chord when one is chosen in the toolbar (its root on the clicked key).
    selected.assign (notes.size(), false);
    const int root = fitPitch (yToPitch (pos.y));
    const auto pitches = chordStamp >= 0 ? theory::chordPitches (root, chordStamp, chordInversion) : std::vector<int> { root };
    dragIndex = static_cast<int> (notes.size());
    for (const int pitch : pitches)
    {
        Note n;
        n.pitch = pitch;
        n.startTick = tick;
        n.lengthTicks = std::max (1, std::min (lastLengthTicks, loopLengthTicks - tick));
        n.velocity = 0.8f;
        notes.push_back (n);
        selected.push_back (true);
    }
    dragOriginal = notes;
    dragMode = Drag::Resize;
    dragChanged = true; // creation is a change even without dragging
    sound (root);
    say (describe (notes[static_cast<size_t> (dragIndex)]) + (pitches.size() > 1 ? "  (" + juce::String (theory::chords()[static_cast<size_t> (chordStamp)].name) + ")" : juce::String()));
    repaint();
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    if (dragMode == Drag::Seek)
    {
        if (onSeek)
            onSeek (rulerSnap (xToTick (e.position.x), e.mods.isAltDown()));
        return;
    }
    if (dragMode == Drag::LoopCreate || dragMode == Drag::LoopMove || dragMode == Drag::LoopResize)
    {
        if (e.getDistanceFromDragStart() < 3 && ! loopDragMoved)
            return;
        loopDragMoved = true;
        const double mouse = xToTick (e.position.x);
        const int tick = rulerSnap (mouse, e.mods.isAltDown());
        if (dragMode == Drag::LoopCreate)
        {
            if (tick != loopAnchor)
                placeLoop (std::min (tick, loopAnchor), std::max (tick, loopAnchor));
        }
        else if (dragMode == Drag::LoopMove)
        {
            const int length = loopOriginalEnd - loopOriginalStart;
            const int start = std::clamp (rulerSnap (mouse - loopGrab, false), 0, loopLengthTicks - length);
            placeLoop (start, start + length);
        }
        else if (loopResizeEdge < 0)
            placeLoop (std::min (tick, loopOriginalEnd - kGridTicks), loopOriginalEnd);
        else
            placeLoop (loopOriginalStart, std::max (tick, loopOriginalStart + kGridTicks));
        if (onStatus && loopStart >= 0)
            onStatus ("Loop region: 16ths " + juce::String (loopStart / kGridTicks + 1) + " to " + juce::String (loopEnd / kGridTicks));
        return;
    }

    if (keyboardHeld)
    {
        sound (yToPitch (e.position.y)); // sliding over the keys plays each one in turn
        return;
    }

    if (dragMode == Drag::Select)
    {
        selectBox = juce::Rectangle<float> (e.mouseDownPosition, e.position);
        for (size_t i = 0; i < notes.size(); ++i)
            selected[i] = (selectAdds && i < selectionBefore.size() && selectionBefore[i]) || noteRect (notes[i]).intersects (selectBox);
        repaint();
        return;
    }

    if (dragMode == Drag::Velocity)
    {
        editVelocities (lastVelocityPoint, e.position);
        lastVelocityPoint = e.position;
        return;
    }

    if (dragMode == Drag::None || dragIndex < 0 || dragIndex >= static_cast<int> (notes.size()) || dragOriginal.size() != notes.size())
        return;

    const auto& grabbed = dragOriginal[static_cast<size_t> (dragIndex)];
    const bool free = e.mods.isAltDown();

    if (dragMode == Drag::Move)
    {
        // Every selected note moves by the same amount, as far as all of them stay inside the pattern and the keyboard.
        const double wanted = xToTick (e.position.x) - grabOffsetTicks;
        int delta = (free ? static_cast<int> (std::llround (wanted)) : snap (wanted)) - grabbed.startTick;
        int pitchDelta = yToPitch (e.position.y) - grabbed.pitch;
        for (size_t i = 0; i < notes.size(); ++i)
            if (selected[i])
            {
                const auto& o = dragOriginal[i];
                delta = std::clamp (delta, -o.startTick, loopLengthTicks - o.lengthTicks - o.startTick);
                pitchDelta = std::clamp (pitchDelta, kLowPitch - o.pitch, kHighPitch - o.pitch);
            }
        bool changed = false;
        for (size_t i = 0; i < notes.size(); ++i)
            if (selected[i])
            {
                const auto& o = dragOriginal[i];
                const int start = std::max (0, o.startTick + delta);
                const int pitch = pitchDelta == 0 ? o.pitch : fitPitch (o.pitch + pitchDelta);
                changed = changed || start != notes[i].startTick || pitch != notes[i].pitch;
                notes[i].startTick = start;
                notes[i].pitch = pitch;
            }
        if (changed)
        {
            dragChanged = true;
            sound (notes[static_cast<size_t> (dragIndex)].pitch);
        }
    }
    else if (dragMode == Drag::ResizeStart)
    {
        // The left edge (0.32.0): every selected note starts earlier or later by the same amount, its end staying put.
        const double wantedStart = xToTick (e.position.x);
        const int start = free ? static_cast<int> (std::llround (wantedStart)) : snap (wantedStart);
        int delta = start - grabbed.startTick;
        const int shortest = free ? 10 : gridTicks;
        for (size_t i = 0; i < notes.size(); ++i)
            if (selected[i])
            {
                const auto& o = dragOriginal[i];
                delta = std::clamp (delta, -o.startTick, std::max (0, o.lengthTicks - std::min (shortest, o.lengthTicks)));
            }
        bool changed = false;
        for (size_t i = 0; i < notes.size(); ++i)
            if (selected[i])
            {
                const auto& o = dragOriginal[i];
                changed = changed || o.startTick + delta != notes[i].startTick;
                notes[i].startTick = o.startTick + delta;
                notes[i].lengthTicks = o.lengthTicks - delta;
            }
        if (changed)
        {
            dragChanged = true;
            lastLengthTicks = notes[static_cast<size_t> (dragIndex)].lengthTicks;
        }
    }
    else
    {
        // Every selected note gets longer or shorter by the same amount.
        const double wantedEnd = xToTick (e.position.x);
        const int end = free ? static_cast<int> (std::llround (wantedEnd)) : snap (wantedEnd);
        const int delta = end - (grabbed.startTick + grabbed.lengthTicks);
        const int shortest = free ? 10 : gridTicks;
        bool changed = false;
        for (size_t i = 0; i < notes.size(); ++i)
            if (selected[i])
            {
                const auto& o = dragOriginal[i];
                const int length = std::clamp (o.lengthTicks + delta, std::min (shortest, o.lengthTicks), std::max (1, loopLengthTicks - o.startTick));
                changed = changed || length != notes[i].lengthTicks;
                notes[i].lengthTicks = length;
            }
        if (changed)
        {
            dragChanged = true;
            lastLengthTicks = notes[static_cast<size_t> (dragIndex)].lengthTicks;
        }
    }
    say (describe (notes[static_cast<size_t> (dragIndex)]));
    repaint();
}

void PianoRoll::mouseUp (const juce::MouseEvent& e)
{
    if (dragMode == Drag::Seek)
    {
        dragMode = Drag::None;
        return;
    }
    if (dragMode == Drag::LoopCreate || dragMode == Drag::LoopMove || dragMode == Drag::LoopResize)
    {
        const auto mode = dragMode;
        dragMode = Drag::None;
        if (loopDragMoved)
        {
            if (loopStart != loopOriginalStart || loopEnd != loopOriginalEnd)
                announceLoop();
        }
        else if (mode != Drag::LoopCreate && overLoop (e.x))
            showLoopMenu(); // a right click (no drag) on the region
        return;
    }

    if (dragMode == Drag::Select)
    {
        dragMode = Drag::None;
        repaint();
        say (juce::String (selectedCount()) + " note(s) selected");
        return;
    }

    sound (-1);
    keyboardHeld = false;
    const bool changed = dragChanged;
    dragMode = Drag::None;
    dragIndex = -1;
    dragChanged = false;
    dragOriginal.clear();
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
// --- toolbar settings and editing (0.23.0) -----------------------------------------------------------------------------------
void PianoRoll::setGridTicks (int ticks)
{
    gridTicks = std::clamp (ticks, 1, 4 * kTicksPerBeat);
    repaint();
}

void PianoRoll::setScale (const theory::Scale& s, bool snapNotes)
{
    scale = s;
    scaleSnap = snapNotes && s.type != 0;
    repaint();
}

void PianoRoll::setChordStamp (int chord, int inversion)
{
    chordStamp = chord >= 0 && chord < static_cast<int> (theory::chords().size()) ? chord : -1;
    chordInversion = std::max (0, inversion);
}

void PianoRoll::setVelocityLaneVisible (bool visible)
{
    if (visible == velocityLane)
        return;
    velocityLane = visible;
    clampScroll();
    repaint();
}

int PianoRoll::selectedCount() const noexcept
{
    return static_cast<int> (std::count (selected.begin(), selected.end(), true));
}

void PianoRoll::selectAll()
{
    selected.assign (notes.size(), true);
    repaint();
    say (juce::String (notes.size()) + " note(s) selected");
}

void PianoRoll::selectNone()
{
    selected.assign (notes.size(), false);
    repaint();
}

void PianoRoll::say (const juce::String& text) const
{
    if (onStatus)
        onStatus (text);
}

juce::String PianoRoll::describe (const Note& n) const
{
    const double sixteenths = static_cast<double> (n.lengthTicks) / static_cast<double> (kSixteenthTicks);
    return juce::String (theory::noteName (n.pitch)) + "   velocity " + juce::String (juce::roundToInt (n.velocity * 100.0f)) + " %   length "
           + (std::abs (sixteenths - std::round (sixteenths)) < 1e-6 ? juce::String (juce::roundToInt (sixteenths)) + "/16"
                                                                      : juce::String (n.lengthTicks) + " ticks");
}

void PianoRoll::applyTool (const std::function<void (std::vector<Note>&, std::vector<bool>&)>& tool, const juce::String& what)
{
    sound (-1);
    selected.resize (notes.size(), false);
    tool (notes, selected);
    selected.resize (notes.size(), false);
    repaint();
    notifyChanged();
    say (what);
}

void PianoRoll::replaceAllNotes (std::vector<Note> newNotes, const juce::String& what)
{
    sound (-1);
    notes = std::move (newNotes);
    selected.assign (notes.size(), true);
    repaint();
    notifyChanged();
    say (what);
}

void PianoRoll::deleteSelected()
{
    const int count = selectedCount();
    if (count == 0)
        return;
    std::vector<Note> kept;
    for (size_t i = 0; i < notes.size(); ++i)
        if (! selected[i])
            kept.push_back (notes[i]);
    notes = std::move (kept);
    selected.assign (notes.size(), false);
    repaint();
    notifyChanged();
    say (juce::String (count) + " note(s) deleted");
}

void PianoRoll::copySelected()
{
    auto& clip = clipboard();
    clip.clear();
    for (size_t i = 0; i < notes.size(); ++i)
        if (i < selected.size() && selected[i])
            clip.push_back (notes[i]);
    say (juce::String (clip.size()) + " note(s) copied");
}

void PianoRoll::paste()
{
    const auto& clip = clipboard();
    if (clip.empty())
        return;
    int first = clip.front().startTick;
    for (const auto& n : clip)
        first = std::min (first, n.startTick);
    // at the mouse when it is over the grid, otherwise where the notes were
    int at = first;
    const auto mouse = getMouseXYRelative();
    if (gridArea().contains (mouse))
        at = snapDown (xToTick (static_cast<float> (mouse.x)));
    selected.assign (notes.size(), false);
    int added = 0;
    for (auto n : clip)
    {
        n.startTick += at - first;
        if (n.startTick < 0 || n.startTick >= loopLengthTicks)
            continue;
        n.lengthTicks = std::min (n.lengthTicks, loopLengthTicks - n.startTick);
        notes.push_back (n);
        selected.push_back (true);
        ++added;
    }
    repaint();
    if (added > 0)
        notifyChanged();
    say (juce::String (added) + " note(s) pasted" + (added < static_cast<int> (clip.size()) ? " (the others would end after the pattern)" : ""));
}

void PianoRoll::duplicateSelected()
{
    int first = loopLengthTicks, last = 0;
    for (size_t i = 0; i < notes.size(); ++i)
        if (selected[i])
        {
            first = std::min (first, notes[i].startTick);
            last = std::max (last, notes[i].startTick + notes[i].lengthTicks);
        }
    if (first >= last)
        return;
    // the copy goes right after the selection, its length rounded up to the grid
    const int span = std::max (gridTicks, (last - first + gridTicks - 1) / gridTicks * gridTicks);
    std::vector<Note> copies;
    for (size_t i = 0; i < notes.size(); ++i)
        if (selected[i] && notes[i].startTick + span < loopLengthTicks)
        {
            auto n = notes[i];
            n.startTick += span;
            n.lengthTicks = std::min (n.lengthTicks, loopLengthTicks - n.startTick);
            copies.push_back (n);
        }
    if (copies.empty())
    {
        say ("No room after the selection in this pattern");
        return;
    }
    selected.assign (notes.size(), false);
    for (const auto& n : copies)
    {
        notes.push_back (n);
        selected.push_back (true);
    }
    repaint();
    notifyChanged();
    say (juce::String (copies.size()) + " note(s) duplicated");
}

void PianoRoll::moveSelected (int ticks, int semitones, int scaleSteps)
{
    if (selectedCount() == 0)
        return;
    for (size_t i = 0; i < notes.size(); ++i)
        if (selected[i])
        {
            ticks = std::clamp (ticks, -notes[i].startTick, loopLengthTicks - notes[i].lengthTicks - notes[i].startTick);
            semitones = std::clamp (semitones, kLowPitch - notes[i].pitch, kHighPitch - notes[i].pitch);
        }
    for (size_t i = 0; i < notes.size(); ++i)
        if (selected[i])
        {
            notes[i].startTick += ticks;
            notes[i].pitch = scaleSteps != 0 ? scale.step (notes[i].pitch, scaleSteps) : notes[i].pitch + semitones;
        }
    repaint();
    notifyChanged();
    for (size_t i = 0; i < notes.size(); ++i)
        if (selected[i])
        {
            say (describe (notes[i]));
            break;
        }
}

void PianoRoll::editVelocities (juce::Point<float> from, juce::Point<float> to)
{
    // Every note whose stalk lies between the two mouse positions takes the velocity of the line between them.
    const auto lane = velocityArea();
    if (lane.isEmpty())
        return;
    const bool onlySelected = selectedCount() > 0;
    const float x0 = std::min (from.x, to.x) - 3.0f, x1 = std::max (from.x, to.x) + 3.0f;
    const auto velocityAt = [&] (float y)
    {
        return std::clamp ((static_cast<float> (lane.getBottom() - 4) - y) / static_cast<float> (lane.getHeight() - 8), 0.01f, 1.0f);
    };
    bool changed = false;
    int last = -1;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        if (onlySelected && ! selected[i])
            continue;
        const float x = tickToX (notes[i].startTick);
        if (x < x0 || x > x1)
            continue;
        const float t = std::abs (to.x - from.x) > 0.5f ? std::clamp ((x - from.x) / (to.x - from.x), 0.0f, 1.0f) : 1.0f;
        const float v = velocityAt (from.y + t * (to.y - from.y));
        if (std::abs (notes[i].velocity - v) > 1e-4f)
        {
            notes[i].velocity = v;
            changed = true;
            last = static_cast<int> (i);
        }
    }
    if (changed)
    {
        dragChanged = true;
        repaint();
        say ("Velocity " + juce::String (juce::roundToInt (notes[static_cast<size_t> (last)].velocity * 100.0f)) + " %");
    }
}

bool PianoRoll::handleKey (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const bool cmd = mods.isCommandDown() || mods.isCtrlDown();
    const auto letter = juce::CharacterFunctions::toUpperCase (static_cast<juce::juce_wchar> (key.getKeyCode()));
    const bool any = selectedCount() > 0;

    if (key.isKeyCode (juce::KeyPress::deleteKey) || key.isKeyCode (juce::KeyPress::backspaceKey))
    {
        deleteSelected();
        return any;
    }
    if (cmd && letter == 'A') { selectAll(); return true; }
    if (cmd && letter == 'C') { if (any) copySelected(); return any; }
    if (cmd && letter == 'X') { if (any) { copySelected(); deleteSelected(); } return any; }
    if (cmd && letter == 'V') { paste(); return true; }
    if (cmd && letter == 'D') { if (any) duplicateSelected(); return any; }
    if (key.isKeyCode (juce::KeyPress::escapeKey) && any)
    {
        selectNone();
        return true;
    }
    if (! any)
        return false;
    if (key.isKeyCode (juce::KeyPress::upKey) || key.isKeyCode (juce::KeyPress::downKey))
    {
        const int dir = key.isKeyCode (juce::KeyPress::upKey) ? 1 : -1;
        if (cmd)
            moveSelected (0, 0, dir);
        else
            moveSelected (0, dir * (mods.isShiftDown() ? 12 : 1), 0);
        return true;
    }
    if (key.isKeyCode (juce::KeyPress::leftKey) || key.isKeyCode (juce::KeyPress::rightKey))
    {
        const int dir = key.isKeyCode (juce::KeyPress::rightKey) ? 1 : -1;
        moveSelected (dir * (mods.isShiftDown() ? kTicksPerBeat : gridTicks), 0, 0);
        return true;
    }
    return false;
}
} // namespace sls
