#include "ui/PlaylistView.h"

#include <cmath>

namespace sls
{
namespace
{
const juce::Colour kBackground { 0xff22252b };
const juce::Colour kTrackA { 0xff2a2e36 };
const juce::Colour kTrackB { 0xff262930 };
const juce::Colour kNameBackground { 0xff30343c };
const juce::Colour kRuler { 0xff343841 };
const juce::Colour kGridLine { 0xff3c414c };
const juce::Colour kPlayhead { 0xffffb347 };
const juce::Colour kText { 0xffd5d9e0 };
} // namespace

PlaylistView::PlaylistView (Arrangement& arrangementToEdit) : arrangement (arrangementToEdit)
{
    setOpaque (true);
}

juce::Colour PlaylistView::colourForPattern (int patternIndex)
{
    const float hue = std::fmod (0.07f + 0.17f * static_cast<float> (patternIndex), 1.0f);
    return juce::Colour::fromHSV (hue, 0.55f, 0.78f, 1.0f);
}

void PlaylistView::setSelectedPattern (int patternIndex)
{
    selectedPattern = patternIndex;
    repaint();
}

void PlaylistView::setPlayheadTick (int tick)
{
    if (tick != playheadTick)
    {
        playheadTick = tick;
        repaint();
    }
}

int PlaylistView::trackHeight() const noexcept
{
    return std::clamp ((getHeight() - kRulerHeight) / kPlaylistTracks, 22, 40);
}

int PlaylistView::unitToX (int unit) const noexcept
{
    return kNameWidth + static_cast<int> (std::lround (unit * unitWidth - scrollX));
}

int PlaylistView::xToUnit (int x) const noexcept
{
    const double unit = (static_cast<double> (x - kNameWidth) + scrollX) / unitWidth;
    return std::clamp (static_cast<int> (std::floor (unit)), 0, kPlaylistUnits - 1);
}

int PlaylistView::yToTrack (int y) const noexcept
{
    return std::clamp ((y - kRulerHeight) / trackHeight(), 0, kPlaylistTracks - 1);
}

juce::Rectangle<int> PlaylistView::clipRect (const Clip& c) const noexcept
{
    const int x0 = unitToX (c.startUnit);
    const int x1 = unitToX (c.endUnit());
    return { x0, kRulerHeight + c.track * trackHeight(), x1 - x0, trackHeight() };
}

void PlaylistView::clampScroll()
{
    const double visible = std::max (1, getWidth() - kNameWidth);
    scrollX = std::clamp (scrollX, 0.0, std::max (0.0, kPlaylistUnits * unitWidth - visible));
}

void PlaylistView::resized()
{
    clampScroll();
}

void PlaylistView::notifyChanged()
{
    repaint();
    if (onChanged)
        onChanged();
}

void PlaylistView::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);

    const int th = trackHeight();
    const int barsPerUnit = std::max (1, arrangement.patternTicks / (4 * kTicksPerBeat));

    // Timeline area (clipped so scrolled content never covers the name column or the ruler).
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (kNameWidth, 0, getWidth() - kNameWidth, getHeight());

        for (int t = 0; t < kPlaylistTracks; ++t)
        {
            g.setColour (t % 2 == 0 ? kTrackA : kTrackB);
            g.fillRect (kNameWidth, kRulerHeight + t * th, getWidth() - kNameWidth, th);
        }

        g.setColour (kRuler);
        g.fillRect (kNameWidth, 0, getWidth() - kNameWidth, kRulerHeight);

        g.setFont (juce::FontOptions (11.0f));
        for (int u = 0; u <= kPlaylistUnits; ++u)
        {
            const int x = unitToX (u);
            if (x < kNameWidth - 1 || x > getWidth())
                continue;

            g.setColour (kGridLine);
            g.drawVerticalLine (x, static_cast<float> (kRulerHeight - 6), static_cast<float> (kRulerHeight + kPlaylistTracks * th));
            if (u < kPlaylistUnits)
            {
                g.setColour (kText);
                g.drawText (juce::String (u * barsPerUnit + 1), x + 4, 0, 40, kRulerHeight, juce::Justification::centredLeft);
            }
        }

        // End of the song: song mode loops here.
        const int songUnits = arrangement.songLengthUnits();
        if (songUnits > 0)
        {
            g.setColour (kPlayhead.withAlpha (0.35f));
            g.drawVerticalLine (unitToX (songUnits), static_cast<float> (kRulerHeight), static_cast<float> (kRulerHeight + kPlaylistTracks * th));
        }

        for (const auto& clip : arrangement.clips)
        {
            const auto r = clipRect (clip).reduced (0, 2).withTrimmedRight (1);
            const auto colour = colourForPattern (clip.pattern);
            g.setColour (colour);
            g.fillRoundedRectangle (r.toFloat(), 3.0f);

            // One tick per repeat of the pattern.
            g.setColour (colour.darker (0.5f));
            for (int u = clip.startUnit + 1; u < clip.endUnit(); ++u)
                g.drawVerticalLine (unitToX (u), static_cast<float> (r.getY() + 3), static_cast<float> (r.getBottom() - 3));

            if (clip.pattern >= 0 && clip.pattern < static_cast<int> (arrangement.patterns.size()))
            {
                g.setColour (juce::Colours::black.withAlpha (0.8f));
                g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
                g.drawText (juce::String (arrangement.patterns[static_cast<size_t> (clip.pattern)].name),
                            r.reduced (5, 0), juce::Justification::centredLeft, true);
            }
        }

        if (playheadTick >= 0 && arrangement.patternTicks > 0)
        {
            const double unit = static_cast<double> (playheadTick) / arrangement.patternTicks;
            const int x = kNameWidth + static_cast<int> (std::lround (unit * unitWidth - scrollX));
            g.setColour (kPlayhead);
            g.drawVerticalLine (x, 0.0f, static_cast<float> (getHeight()));
        }
    }

    // Track names.
    g.setColour (kNameBackground);
    g.fillRect (0, 0, kNameWidth, getHeight());
    g.setColour (kText);
    g.setFont (juce::FontOptions (12.0f));
    for (int t = 0; t < kPlaylistTracks; ++t)
        g.drawText ("Track " + juce::String (t + 1), 6, kRulerHeight + t * th, kNameWidth - 6, th, juce::Justification::centredLeft);
}

void PlaylistView::mouseDown (const juce::MouseEvent& e)
{
    drag = Drag::None;
    dragClip = -1;

    if (e.x < kNameWidth || e.y < kRulerHeight)
        return;

    const int track = yToTrack (e.y);
    const int unit = xToUnit (e.x);
    const int hit = arrangement.clipAt (track, unit);

    if (e.mods.isPopupMenu())
    {
        if (hit >= 0)
        {
            arrangement.clips.erase (arrangement.clips.begin() + hit);
            notifyChanged();
        }
        return;
    }

    if (hit < 0)
    {
        const Clip c { selectedPattern, track, unit, 1 };
        if (arrangement.canPlace (c))
        {
            arrangement.clips.push_back (c);
            notifyChanged();
            // Dragging right after placing resizes the new clip.
            dragClip = static_cast<int> (arrangement.clips.size()) - 1;
            drag = Drag::Resize;
        }
        return;
    }

    dragClip = hit;
    const auto r = clipRect (arrangement.clips[static_cast<size_t> (hit)]);
    if (e.x >= r.getRight() - kEdgeGrab)
    {
        drag = Drag::Resize;
    }
    else
    {
        drag = Drag::Move;
        grabOffsetUnits = unit - arrangement.clips[static_cast<size_t> (hit)].startUnit;
    }
}

void PlaylistView::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::None || dragClip < 0 || dragClip >= static_cast<int> (arrangement.clips.size()))
        return;

    auto clip = arrangement.clips[static_cast<size_t> (dragClip)];

    if (drag == Drag::Move)
    {
        clip.startUnit = std::clamp (xToUnit (e.x) - grabOffsetUnits, 0, kPlaylistUnits - clip.lengthUnits);
        clip.track = yToTrack (e.y);
    }
    else
    {
        clip.lengthUnits = std::clamp (xToUnit (e.x) - clip.startUnit + 1, 1, kPlaylistUnits - clip.startUnit);
    }

    auto& stored = arrangement.clips[static_cast<size_t> (dragClip)];
    if (! (clip.startUnit == stored.startUnit && clip.track == stored.track && clip.lengthUnits == stored.lengthUnits)
        && arrangement.canPlace (clip, dragClip))
    {
        stored = clip;
        repaint();
        if (onChanged)
            onChanged();
    }
}

void PlaylistView::mouseUp (const juce::MouseEvent&)
{
    drag = Drag::None;
    dragClip = -1;
}

void PlaylistView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.x < kNameWidth || e.y < kRulerHeight)
        return;

    const int hit = arrangement.clipAt (yToTrack (e.y), xToUnit (e.x));
    if (hit >= 0 && onPatternPicked)
        onPatternPicked (arrangement.clips[static_cast<size_t> (hit)].pattern);
}

void PlaylistView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const float delta = std::abs (wheel.deltaY) >= std::abs (wheel.deltaX) ? wheel.deltaY : wheel.deltaX;

    if (e.mods.isCtrlDown())
    {
        // Zoom around the unit under the mouse.
        const double unitAtMouse = (static_cast<double> (e.x - kNameWidth) + scrollX) / unitWidth;
        unitWidth = std::clamp (unitWidth * std::pow (1.25, static_cast<double> (delta) * 4.0), 24.0, 240.0);
        scrollX = unitAtMouse * unitWidth - static_cast<double> (e.x - kNameWidth);
    }
    else
    {
        scrollX -= static_cast<double> (delta) * 400.0;
    }

    clampScroll();
    repaint();
}
} // namespace sls
