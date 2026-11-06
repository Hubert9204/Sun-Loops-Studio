#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace sls
{
// Numeric LCD-style display of the mixer track a channel plays through ("Track 3").
// Click: pick from a list. Drag up/down or use the mouse wheel: step through the tracks.
class TrackSelector : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit TrackSelector (int numberOfTracks);

    void setTrack (int zeroBasedTrack);

    std::function<void (int)> onChange; // called with the new 0-based track

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void changeTo (int newTrack);
    void showMenu();

    int tracks;
    int track = 0;
    int dragStartTrack = 0;
    bool dragged = false;
};
} // namespace sls
