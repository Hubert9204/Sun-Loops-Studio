#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/Arrangement.h"

namespace sls
{
// The Playlist panel body: pattern clips on a timeline of kPlaylistTracks tracks.
//   Left click on empty space : place a clip of the selected pattern (one pattern long)
//   Drag a clip               : move it (snaps to pattern boundaries, never overlaps another clip)
//   Drag a clip's right edge  : make it longer or shorter (the pattern repeats)
//   Right click on a clip     : delete it
//   Double click on a clip    : select that clip's pattern
//   Wheel                     : scroll time | Ctrl+wheel: zoom
// The arrangement is owned by the caller; this view edits it in place and reports through onChanged.
class PlaylistView : public juce::Component
{
public:
    explicit PlaylistView (Arrangement& arrangementToEdit);

    void setSelectedPattern (int patternIndex);
    int getSelectedPattern() const noexcept { return selectedPattern; }

    // Position in ticks within the song (song mode), or < 0 to hide the playhead.
    void setPlayheadTick (int tick);

    std::function<void()> onChanged;                  // clips were added, moved, resized or removed
    std::function<void (int)> onPatternPicked;        // a clip was double-clicked: its pattern index

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void resized() override;

    static juce::Colour colourForPattern (int patternIndex);

private:
    enum class Drag { None, Move, Resize };

    static constexpr int kNameWidth = 64;
    static constexpr int kRulerHeight = 22;
    static constexpr int kEdgeGrab = 7;

    int trackHeight() const noexcept;
    int unitToX (int unit) const noexcept;
    int xToUnit (int x) const noexcept;       // clamped to the timeline
    int yToTrack (int y) const noexcept;      // clamped to the tracks
    juce::Rectangle<int> clipRect (const Clip& c) const noexcept;
    void clampScroll();
    void notifyChanged();

    Arrangement& arrangement;
    int selectedPattern = 0;
    int playheadTick = -1;

    double unitWidth = 90.0;
    double scrollX = 0.0;

    Drag drag = Drag::None;
    int dragClip = -1;
    int grabOffsetUnits = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaylistView)
};
} // namespace sls
