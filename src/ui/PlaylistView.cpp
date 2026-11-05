#include "ui/PlaylistView.h"

#include <cmath>

#include "audio/AudioImport.h"

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
const juce::Colour kAudio { 0xff3f9fa6 };
const juce::Colour kAudioMissing { 0xff8a4a4a };
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

int PlaylistView::tickToX (int tick) const noexcept
{
    const double unit = arrangement.patternTicks > 0 ? static_cast<double> (tick) / arrangement.patternTicks : 0.0;
    return kNameWidth + static_cast<int> (std::lround (unit * unitWidth - scrollX));
}

int PlaylistView::xToTick (int x) const noexcept
{
    const double unit = (static_cast<double> (x - kNameWidth) + scrollX) / unitWidth;
    return std::clamp (static_cast<int> (std::floor (unit * arrangement.patternTicks)), 0, arrangement.timelineTicks() - 1);
}

int PlaylistView::xToBeatTick (int x) const noexcept
{
    return (xToTick (x) / kTicksPerBeat) * kTicksPerBeat;
}

juce::Rectangle<int> PlaylistView::audioClipRect (const AudioClip& c) const noexcept
{
    const int x0 = tickToX (c.startTick);
    const int x1 = tickToX (c.endTick());
    return { x0, kRulerHeight + c.track * trackHeight(), x1 - x0, trackHeight() };
}

void PlaylistView::setArmedTrack (int track)
{
    track = std::clamp (track, 0, kPlaylistTracks - 1);
    if (track != armedTrack)
    {
        armedTrack = track;
        repaint();
    }
}

void PlaylistView::setBpm (double newBpm)
{
    if (newBpm > 0.0 && std::abs (newBpm - bpm) > 1.0e-9)
    {
        bpm = newBpm;
        repaint();
    }
}

const PlaylistView::Peaks& PlaylistView::peaksFor (const std::shared_ptr<const SampleData>& data)
{
    for (const auto& p : waveformCache)
        if (p.data == data)
            return p;

    Peaks p;
    p.data = data;
    const int frames = data->numFrames();
    const int bins = (frames + kFramesPerPeak - 1) / kFramesPerPeak;
    p.lows.resize (static_cast<size_t> (bins));
    p.highs.resize (static_cast<size_t> (bins));
    for (int b = 0; b < bins; ++b)
    {
        float lo = 0.0f, hi = 0.0f;
        const int end = std::min (frames, (b + 1) * kFramesPerPeak);
        for (int i = b * kFramesPerPeak; i < end; ++i)
        {
            const float v = 0.5f * (data->left[static_cast<size_t> (i)] + data->right[static_cast<size_t> (i)]);
            lo = std::min (lo, v);
            hi = std::max (hi, v);
        }
        p.lows[static_cast<size_t> (b)] = lo;
        p.highs[static_cast<size_t> (b)] = hi;
    }
    waveformCache.push_back (std::move (p));
    return waveformCache.back();
}

void PlaylistView::pruneWaveformCache()
{
    waveformCache.erase (std::remove_if (waveformCache.begin(), waveformCache.end(),
                                         [this] (const Peaks& p)
                                         {
                                             return std::none_of (arrangement.audioClips.begin(), arrangement.audioClips.end(),
                                                                  [&p] (const AudioClip& c) { return c.data == p.data; });
                                         }),
                         waveformCache.end());
}

void PlaylistView::paintAudioClip (juce::Graphics& g, const AudioClip& clip)
{
    const auto r = audioClipRect (clip).reduced (0, 2).withTrimmedRight (1);
    const bool missing = clip.data == nullptr || clip.data->numFrames() < 2;
    const auto colour = missing ? kAudioMissing : kAudio;
    g.setColour (colour);
    g.fillRoundedRectangle (r.toFloat(), 3.0f);

    if (! missing)
    {
        // The waveform covers the real length of the audio at the current tempo (the clip may be shorter: it is cut).
        const auto& peaks = peaksFor (clip.data);
        const double pxPerTick = arrangement.patternTicks > 0 ? unitWidth / arrangement.patternTicks : 0.0;
        const double framesPerPixel = (bpm > 0.0 && pxPerTick > 0.0)
                                          ? clip.data->sampleRate * 60.0 / (bpm * kTicksPerBeat * pxPerTick)
                                          : 1.0;
        const float mid = static_cast<float> (r.getCentreY());
        const float half = static_cast<float> (r.getHeight()) * 0.5f - 3.0f;

        g.saveState();
        g.reduceClipRegion (r);
        g.setColour (colour.darker (0.65f));
        const int xFrom = std::max (r.getX(), kNameWidth);
        const int xTo = std::min (r.getRight(), getWidth());
        const int nBins = static_cast<int> (peaks.lows.size());
        for (int x = xFrom; x < xTo; ++x)
        {
            const double f0 = (x - r.getX()) * framesPerPixel;
            const double f1 = f0 + framesPerPixel;
            if (f0 >= clip.data->numFrames())
                break;
            const int b0 = std::clamp (static_cast<int> (f0 / kFramesPerPeak), 0, nBins - 1);
            const int b1 = std::clamp (static_cast<int> (f1 / kFramesPerPeak), b0, nBins - 1);
            float lo = 0.0f, hi = 0.0f;
            for (int b = b0; b <= b1; ++b)
            {
                lo = std::min (lo, peaks.lows[static_cast<size_t> (b)]);
                hi = std::max (hi, peaks.highs[static_cast<size_t> (b)]);
            }
            g.drawVerticalLine (x, mid - std::clamp (hi, 0.0f, 1.0f) * half - 0.5f, mid - std::clamp (lo, -1.0f, 0.0f) * half + 0.5f);
        }
        g.restoreState();
    }

    g.setColour (juce::Colours::white.withAlpha (0.92f));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (juce::String (clip.name) + (missing ? "  (file not found)" : ""), r.reduced (5, 0).withHeight (14).translated (0, 1),
                juce::Justification::centredLeft, true);
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

        pruneWaveformCache();
        for (const auto& clip : arrangement.audioClips)
            paintAudioClip (g, clip);

        if (dropTrack >= 0)
        {
            const auto r = juce::Rectangle<int> (tickToX (dropTick), kRulerHeight + dropTrack * th, tickToX (dropTick + 4 * kTicksPerBeat) - tickToX (dropTick), th);
            g.setColour (kAudio.withAlpha (0.35f));
            g.fillRect (r);
            g.setColour (kAudio);
            g.drawRect (r, 2);
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
    {
        if (t == armedTrack)
        {
            // the track Record writes to
            g.setColour (juce::Colour (0xffd9433b));
            g.fillEllipse (static_cast<float> (kNameWidth - 14), static_cast<float> (kRulerHeight + t * th + th / 2 - 4), 8.0f, 8.0f);
            g.setColour (juce::Colours::white);
        }
        else
            g.setColour (kText);
        g.drawText ("Track " + juce::String (t + 1), 6, kRulerHeight + t * th, kNameWidth - 20, th, juce::Justification::centredLeft);
    }
}

void PlaylistView::mouseDown (const juce::MouseEvent& e)
{
    drag = Drag::None;
    dragClip = -1;

    if (e.x < kNameWidth && e.y >= kRulerHeight)
    {
        const int track = yToTrack (e.y);
        setArmedTrack (track);
        if (onTrackSelected)
            onTrackSelected (track);
        return;
    }
    if (e.x < kNameWidth || e.y < kRulerHeight)
        return;

    const int track = yToTrack (e.y);
    const int unit = xToUnit (e.x);
    const int hit = arrangement.clipAt (track, unit);
    const int audioHit = arrangement.audioClipAt (track, xToTick (e.x));

    if (audioHit >= 0)
    {
        if (e.mods.isPopupMenu())
        {
            arrangement.audioClips.erase (arrangement.audioClips.begin() + audioHit);
            notifyChanged();
            return;
        }

        dragClip = audioHit;
        const auto& clip = arrangement.audioClips[static_cast<size_t> (audioHit)];
        if (e.x >= audioClipRect (clip).getRight() - kEdgeGrab)
            drag = Drag::ResizeAudio;
        else
        {
            drag = Drag::MoveAudio;
            grabOffsetTicks = xToBeatTick (e.x) - clip.startTick;
        }
        return;
    }

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
    if ((drag == Drag::MoveAudio || drag == Drag::ResizeAudio) && dragClip >= 0
        && dragClip < static_cast<int> (arrangement.audioClips.size()))
    {
        auto clip = arrangement.audioClips[static_cast<size_t> (dragClip)];
        if (drag == Drag::MoveAudio)
        {
            clip.startTick = std::clamp (xToBeatTick (e.x) - grabOffsetTicks, 0, arrangement.timelineTicks() - clip.lengthTicks);
            clip.startTick = (clip.startTick / kTicksPerBeat) * kTicksPerBeat;
            clip.track = yToTrack (e.y);
        }
        else
        {
            // As long as the audio really is at most; as short as one beat.
            const int natural = clip.naturalLengthTicks (bpm);
            const int end = ((xToTick (e.x) / kTicksPerBeat) + 1) * kTicksPerBeat;
            clip.lengthTicks = std::clamp (end - clip.startTick, kTicksPerBeat, std::max (kTicksPerBeat, natural));
        }

        auto& stored = arrangement.audioClips[static_cast<size_t> (dragClip)];
        if (! (clip.startTick == stored.startTick && clip.track == stored.track && clip.lengthTicks == stored.lengthTicks)
            && arrangement.canPlaceAudio (clip, dragClip))
        {
            stored = clip;
            repaint();
            if (onChanged)
                onChanged();
        }
        return;
    }

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

bool PlaylistView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (classifyMediaFile (juce::File (f)) != FileKind::Unsupported)
            return true;
    return false;
}

void PlaylistView::fileDragEnter (const juce::StringArray& files, int x, int y)
{
    fileDragMove (files, x, y);
}

void PlaylistView::fileDragMove (const juce::StringArray&, int x, int y)
{
    const bool onTimeline = x >= kNameWidth && y >= kRulerHeight;
    const int track = onTimeline ? yToTrack (y) : -1;
    const int tick = onTimeline ? xToBeatTick (x) : 0;
    if (track != dropTrack || tick != dropTick)
    {
        dropTrack = track;
        dropTick = tick;
        repaint();
    }
}

void PlaylistView::fileDragExit (const juce::StringArray&)
{
    dropTrack = -1;
    repaint();
}

void PlaylistView::filesDropped (const juce::StringArray& files, int x, int y)
{
    dropTrack = -1;
    repaint();

    // Dropped on the track names or the ruler counts as the start of the first track / the nearest track.
    const int track = yToTrack (y);
    const int tick = x < kNameWidth ? 0 : xToBeatTick (x);

    juce::StringArray usable;
    for (const auto& f : files)
        if (classifyMediaFile (juce::File (f)) != FileKind::Unsupported)
            usable.add (f);

    if (! usable.isEmpty() && onFilesDropped)
        onFilesDropped (usable, track, tick);
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
