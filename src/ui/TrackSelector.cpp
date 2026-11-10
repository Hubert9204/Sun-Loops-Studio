#include "ui/TrackSelector.h"

#include <algorithm>
#include <cmath>

namespace sls
{
TrackSelector::TrackSelector (int numberOfTracks) : tracks (std::max (1, numberOfTracks))
{
    setTooltip ("Mixer track this channel plays through. Click to choose, drag or scroll to step.");
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void TrackSelector::setTrack (int zeroBasedTrack)
{
    const int clamped = std::clamp (zeroBasedTrack, 0, tracks - 1);
    if (clamped != track)
    {
        track = clamped;
        repaint();
    }
}

void TrackSelector::setNumTracks (int numberOfTracks)
{
    tracks = std::max (1, numberOfTracks);
    if (track >= tracks)
    {
        track = tracks - 1;
        repaint();
    }
}

void TrackSelector::changeTo (int newTrack)
{
    const int clamped = std::clamp (newTrack, 0, tracks - 1);
    if (clamped == track)
        return;

    track = clamped;
    repaint();
    if (onChange)
        onChange (track);
}

void TrackSelector::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colour (0xff15181c));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (juce::Colour (0xff4a505c));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);

    g.setColour (juce::Colour (0xff7fe6a8));
    g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold));
    g.drawText ("Track " + juce::String (track + 1), getLocalBounds(), juce::Justification::centred, false);
}

void TrackSelector::mouseDown (const juce::MouseEvent&)
{
    dragStartTrack = track;
    dragged = false;
}

void TrackSelector::mouseDrag (const juce::MouseEvent& e)
{
    const int steps = static_cast<int> (std::lround (static_cast<double> (-e.getDistanceFromDragStartY()) / 12.0));
    if (steps != 0 || dragged)
        dragged = true;
    if (dragged)
        changeTo (dragStartTrack + steps);
}

void TrackSelector::mouseUp (const juce::MouseEvent& e)
{
    if (! dragged && ! e.mods.isPopupMenu())
        showMenu();
}

void TrackSelector::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (w.deltaY > 0.0f)
        changeTo (track + 1);
    else if (w.deltaY < 0.0f)
        changeTo (track - 1);
}

void TrackSelector::showMenu()
{
    juce::PopupMenu menu;
    constexpr int kPerGroup = 50; // many tracks: in groups of 50, so the menu fits on the screen
    if (tracks <= kPerGroup)
        for (int i = 0; i < tracks; ++i)
            menu.addItem (i + 1, "Track " + juce::String (i + 1), true, i == track);
    else
        for (int first = 0; first < tracks; first += kPerGroup)
        {
            juce::PopupMenu group;
            const int last = std::min (tracks, first + kPerGroup);
            for (int i = first; i < last; ++i)
                group.addItem (i + 1, "Track " + juce::String (i + 1), true, i == track);
            menu.addSubMenu ("Tracks " + juce::String (first + 1) + " - " + juce::String (last), group, true, nullptr,
                             track >= first && track < last);
        }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [safe = juce::Component::SafePointer<TrackSelector> (this)] (int result)
                        {
                            if (safe != nullptr && result > 0)
                                safe->changeTo (result - 1);
                        });
}
} // namespace sls
