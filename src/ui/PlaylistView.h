#pragma once

#include <functional>
#include <vector>

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
//   Click on a track's name   : choose the track that Record writes to
//   Wheel                     : scroll time | Ctrl+wheel: zoom
// Audio clips (recordings, imported files) share the tracks: they are drawn with their waveform, move on the beat grid,
// can be shortened or lengthened from their right edge, and are deleted with a right click. Audio and video files
// dragged in from the file manager become audio clips (the caller does the importing: see onFilesDropped).
// The arrangement is owned by the caller; this view edits it in place and reports through onChanged.
class PlaylistView : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit PlaylistView (Arrangement& arrangementToEdit);

    void setSelectedPattern (int patternIndex);
    int getSelectedPattern() const noexcept { return selectedPattern; }

    // Position in ticks within the song (song mode), or < 0 to hide the playhead.
    void setPlayheadTick (int tick);

    std::function<void()> onChanged;                  // clips were added, moved, resized or removed
    std::function<void (int)> onPatternPicked;        // a clip was double-clicked: its pattern index

    // Files were dropped on the timeline: their paths, the track under the mouse and the beat (in ticks) there.
    std::function<void (const juce::StringArray&, int, int)> onFilesDropped;

    // The track that Record writes to: shown with a red mark, chosen by clicking its name.
    void setArmedTrack (int track);
    int getArmedTrack() const noexcept { return armedTrack; }
    std::function<void (int)> onTrackSelected;

    // Tempo, to draw audio at its real length (an audio clip lasts a fixed time, not a fixed number of beats).
    void setBpm (double newBpm);

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void resized() override;

    static juce::Colour colourForPattern (int patternIndex);

private:
    enum class Drag { None, Move, Resize, MoveAudio, ResizeAudio };

    struct Peaks
    {
        std::shared_ptr<const SampleData> data; // keeps the audio alive so the cache key cannot be reused
        std::vector<float> lows, highs;         // one pair per kFramesPerPeak frames
    };
    static constexpr int kFramesPerPeak = 256;
    const Peaks& peaksFor (const std::shared_ptr<const SampleData>& data);
    void pruneWaveformCache();
    void paintAudioClip (juce::Graphics& g, const AudioClip& clip);

    static constexpr int kNameWidth = 64;
    static constexpr int kRulerHeight = 22;
    static constexpr int kEdgeGrab = 7;

    int trackHeight() const noexcept;
    int unitToX (int unit) const noexcept;
    int xToUnit (int x) const noexcept;       // clamped to the timeline
    int yToTrack (int y) const noexcept;      // clamped to the tracks
    juce::Rectangle<int> clipRect (const Clip& c) const noexcept;
    int tickToX (int tick) const noexcept;
    int xToTick (int x) const noexcept;       // not snapped, clamped to the timeline
    int xToBeatTick (int x) const noexcept;   // snapped down to a beat
    juce::Rectangle<int> audioClipRect (const AudioClip& c) const noexcept;
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
    int grabOffsetTicks = 0;

    int armedTrack = 0;
    double bpm = 120.0;
    std::vector<Peaks> waveformCache;
    int dropTrack = -1; // while files are dragged over the timeline: where they would land
    int dropTick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaylistView)
};
} // namespace sls
