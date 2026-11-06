#include "ui/PlaylistTimeline.h"

#include "audio/MidiImport.h"

#include <cmath>

#include "audio/AudioImport.h"
#include "audio/ClipEdit.h"
#include "audio/TimeFormat.h"

namespace sls
{
namespace
{
const juce::Colour kBackground { 0xff22252b };
const juce::Colour kTrackA { 0xff2a2e36 };
const juce::Colour kTrackB { 0xff262930 };
const juce::Colour kNameBackground { 0xff30343c };
const juce::Colour kRuler { 0xff343841 };
const juce::Colour kGridUnit { 0xff565c69 };
const juce::Colour kGridBar { 0xff474c58 };
const juce::Colour kGridBeat { 0xff3a3f4a };
const juce::Colour kGridFine { 0xff30343d };
const juce::Colour kPlayhead { 0xffffb347 };
const juce::Colour kText { 0xffd5d9e0 };
const juce::Colour kTextDim { 0xff8a919d };
const juce::Colour kAudio { 0xff3f9fa6 };
const juce::Colour kAudioMissing { 0xff8a4a4a };
const juce::Colour kSelect { 0xffffffff };
const juce::Colour kCut { 0xffe5534b };
const juce::Colour kAutomationColour { 0xff9b6bd1 };
const juce::Colour kLaneA { 0xff262a38 };
const juce::Colour kLaneB { 0xff232634 };

constexpr int kMaxUndo = 64;

juce::String jstr (const std::string& s) { return juce::String (s); }
} // namespace

PlaylistTimeline::PlaylistTimeline (Arrangement& arrangementToEdit) : arrangement (arrangementToEdit)
{
    setOpaque (true);
    setWantsKeyboardFocus (false); // keys are routed by the main window (Space must keep playing)
    scrollBar.addListener (this);
    scrollBar.setAutoHide (false);
    addAndMakeVisible (scrollBar);
    unitWidth = 120.0;
}

PlaylistTimeline::~PlaylistTimeline()
{
    scrollBar.removeListener (this);
}

juce::Colour PlaylistTimeline::colourForPattern (int patternIndex)
{
    const float hue = std::fmod (0.07f + 0.17f * static_cast<float> (patternIndex), 1.0f);
    return juce::Colour::fromHSV (hue, 0.55f, 0.78f, 1.0f);
}

void PlaylistTimeline::setSelectedPattern (int patternIndex)
{
    selectedPattern = patternIndex;
    repaint();
}

void PlaylistTimeline::setPlayheadTick (int tick)
{
    if (tick != playheadTick)
    {
        playheadTick = tick;
        repaint();
    }
}

// ------------------------------------------------------------------------- coordinates and zoom

double PlaylistTimeline::getPixelsPerTick() const noexcept
{
    return arrangement.patternTicks > 0 ? unitWidth / arrangement.patternTicks : 1.0;
}

int PlaylistTimeline::trackHeight() const noexcept
{
    return std::clamp ((getHeight() - kRulerHeight - kScrollBarHeight - kAutomationLanes * kLaneHeight - kLanesGap) / kPlaylistTracks, 18, 56);
}

int PlaylistTimeline::lanesTop() const noexcept
{
    return kRulerHeight + kPlaylistTracks * trackHeight() + kLanesGap;
}

juce::Rectangle<int> PlaylistTimeline::automationClipRect (const AutomationClip& c) const noexcept
{
    const int x0 = tickToX (c.startTick);
    const int x1 = tickToX (c.endTick());
    return { x0, lanesTop() + c.lane * kLaneHeight, std::max (2, x1 - x0), kLaneHeight };
}

int PlaylistTimeline::unitToX (int unit) const noexcept
{
    return kNameWidth + static_cast<int> (std::lround (unit * unitWidth - scrollX));
}

int PlaylistTimeline::xToUnit (int x) const noexcept
{
    const double unit = (static_cast<double> (x - kNameWidth) + scrollX) / unitWidth;
    return std::clamp (static_cast<int> (std::floor (unit)), 0, kPlaylistUnits - 1);
}

int PlaylistTimeline::yToTrack (int y) const noexcept
{
    return std::clamp ((y - kRulerHeight) / trackHeight(), 0, kPlaylistTracks - 1);
}

juce::Rectangle<int> PlaylistTimeline::clipRect (const Clip& c) const noexcept
{
    const int x0 = unitToX (c.startUnit);
    const int x1 = unitToX (c.endUnit());
    return { x0, kRulerHeight + c.track * trackHeight(), x1 - x0, trackHeight() };
}

int PlaylistTimeline::tickToX (int tick) const noexcept
{
    return kNameWidth + static_cast<int> (std::lround (tick * getPixelsPerTick() - scrollX));
}

int PlaylistTimeline::xToTick (int x) const noexcept
{
    const double tick = (static_cast<double> (x - kNameWidth) + scrollX) / getPixelsPerTick();
    return std::clamp (static_cast<int> (std::floor (tick)), 0, arrangement.timelineTicks() - 1);
}

int PlaylistTimeline::snapTick (int tick, bool noSnap) const noexcept
{
    const int step = effectiveSnap (noSnap);
    if (step <= 1)
        return tick;
    return static_cast<int> (std::lround (static_cast<double> (tick) / step)) * step;
}

juce::Rectangle<int> PlaylistTimeline::audioClipRect (const AudioClip& c) const noexcept
{
    const int x0 = tickToX (c.startTick);
    const int x1 = tickToX (c.endTick());
    return { x0, kRulerHeight + c.track * trackHeight(), std::max (1, x1 - x0), trackHeight() };
}

double PlaylistTimeline::totalWidth() const noexcept
{
    return kPlaylistUnits * unitWidth;
}

void PlaylistTimeline::clampScroll()
{
    const double visible = std::max (1, getWidth() - kNameWidth);
    scrollX = std::clamp (scrollX, 0.0, std::max (0.0, totalWidth() - visible));
}

void PlaylistTimeline::updateScrollBar()
{
    const double visible = std::max (1, getWidth() - kNameWidth);
    scrollBar.setRangeLimits (0.0, std::max (totalWidth(), visible));
    scrollBar.setCurrentRange (scrollX, visible, juce::dontSendNotification);
}

void PlaylistTimeline::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    scrollX = newRangeStart;
    clampScroll();
    repaint();
}

void PlaylistTimeline::setUnitWidth (double newUnitWidth, int anchorX)
{
    const double visible = std::max (1, getWidth() - kNameWidth);
    const double minWidth = std::max (6.0, visible / kPlaylistUnits); // the whole timeline fits
    const double maxWidth = arrangement.patternTicks * 1.6;          // 1 tick = 1.6 pixels
    newUnitWidth = std::clamp (newUnitWidth, minWidth, maxWidth);

    if (anchorX < kNameWidth)
        anchorX = kNameWidth + static_cast<int> (visible / 2);

    const double tickAtAnchor = (static_cast<double> (anchorX - kNameWidth) + scrollX) / getPixelsPerTick();
    unitWidth = newUnitWidth;
    scrollX = tickAtAnchor * getPixelsPerTick() - static_cast<double> (anchorX - kNameWidth);
    clampScroll();
    updateScrollBar();
    repaint();
    if (onZoomChanged)
        onZoomChanged (getPixelsPerTick());
}

void PlaylistTimeline::zoomBy (double factor, int anchorX)
{
    setUnitWidth (unitWidth * factor, anchorX);
}

void PlaylistTimeline::zoomToFit()
{
    const double visible = std::max (1, getWidth() - kNameWidth);
    const int units = std::max (1, arrangement.songLengthUnits());
    setUnitWidth (visible / units, kNameWidth);
    scrollX = 0.0;
    updateScrollBar();
    repaint();
}

void PlaylistTimeline::zoomToTicks (int fromTick, int toTick)
{
    if (toTick - fromTick < 4)
        return;
    const double visible = std::max (1, getWidth() - kNameWidth);
    setUnitWidth (visible / (toTick - fromTick) * arrangement.patternTicks, kNameWidth);
    scrollX = fromTick * getPixelsPerTick();
    clampScroll();
    updateScrollBar();
    repaint();
}

void PlaylistTimeline::setTool (PlaylistTool newTool)
{
    tool = newTool;
    switch (tool)
    {
        case PlaylistTool::Select: setMouseCursor (juce::MouseCursor::NormalCursor); break;
        case PlaylistTool::Split:  setMouseCursor (juce::MouseCursor::IBeamCursor); break;
        case PlaylistTool::Cut:    setMouseCursor (juce::MouseCursor::CrosshairCursor); break;
        case PlaylistTool::Erase:  setMouseCursor (juce::MouseCursor::PointingHandCursor); break;
        case PlaylistTool::Zoom:   setMouseCursor (juce::MouseCursor::CrosshairCursor); break;
    }
    repaint();
}

void PlaylistTimeline::setSnapTicks (int ticks)
{
    snapTicks = std::max (1, ticks);
    repaint();
}

void PlaylistTimeline::setArmedTrack (int track)
{
    track = std::clamp (track, 0, kPlaylistTracks - 1);
    if (track != armedTrack)
    {
        armedTrack = track;
        repaint();
    }
}

void PlaylistTimeline::setBpm (double newBpm)
{
    if (newBpm > 0.0 && std::abs (newBpm - bpm) > 1.0e-9)
    {
        bpm = newBpm;
        repaint();
    }
}

// ------------------------------------------------------------------------- waveform

const PlaylistTimeline::Peaks& PlaylistTimeline::peaksFor (const std::shared_ptr<const SampleData>& data)
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

void PlaylistTimeline::pruneWaveformCache()
{
    waveformCache.erase (std::remove_if (waveformCache.begin(), waveformCache.end(),
                                         [this] (const Peaks& p)
                                         {
                                             return std::none_of (arrangement.audioClips.begin(), arrangement.audioClips.end(),
                                                                  [&p] (const AudioClip& c) { return c.data == p.data; });
                                         }),
                         waveformCache.end());
}

namespace
{
// "x2.00, +3 st" for the name of a stretched clip.
juce::String describeStretch (const StretchSettings& s)
{
    juce::StringArray parts;
    if (std::fabs (s.lengthRatio - 1.0) > 1.0e-6)
        parts.add ("x" + juce::String (s.lengthRatio, 2));
    if (std::fabs (s.semitones) > 1.0e-6)
        parts.add ((s.semitones > 0.0 ? "+" : "") + juce::String (s.semitones, std::fabs (s.semitones - std::round (s.semitones)) < 1.0e-6 ? 0 : 2) + " st");
    return parts.joinIntoString (", ");
}
} // namespace

void PlaylistTimeline::paintAudioClip (juce::Graphics& g, const AudioClip& clip, bool selected)
{
    const auto r = audioClipRect (clip).reduced (0, 2).withTrimmedRight (selected ? 0 : 1);
    const bool missing = clip.data == nullptr || clip.data->numFrames() < 2;
    const auto colour = missing ? kAudioMissing : kAudio;
    g.setColour (colour);
    g.fillRoundedRectangle (r.toFloat(), 3.0f);

    if (! missing)
    {
        // The waveform starts where the clip's audio starts (clip.offsetSeconds) and covers the real length of the audio
        // at the current tempo (the clip may be shorter: it is cut there). Zoomed in far enough, the samples themselves are drawn.
        const auto& peaks = peaksFor (clip.data);
        const double pxPerTick = getPixelsPerTick();
        const double framesPerPixel = (bpm > 0.0 && pxPerTick > 0.0)
                                          ? clip.data->sampleRate * 60.0 / (bpm * kTicksPerBeat * pxPerTick)
                                          : 1.0;
        const double firstFrame = clip.offsetSeconds * clip.data->sampleRate;
        const int frames = clip.data->numFrames();
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
            const double f0 = firstFrame + (x - r.getX()) * framesPerPixel;
            const double f1 = f0 + framesPerPixel;
            if (f0 >= frames)
                break;

            float lo = 0.0f, hi = 0.0f;
            if (framesPerPixel < kFramesPerPeak)
            {
                const int a = std::clamp (static_cast<int> (std::floor (f0)), 0, frames - 1);
                const int b = std::clamp (static_cast<int> (std::ceil (f1)), a + 1, frames);
                for (int i = a; i < b; ++i)
                {
                    const float v = 0.5f * (clip.data->left[static_cast<size_t> (i)] + clip.data->right[static_cast<size_t> (i)]);
                    lo = std::min (lo, v);
                    hi = std::max (hi, v);
                }
            }
            else
            {
                const int b0 = std::clamp (static_cast<int> (f0 / kFramesPerPeak), 0, nBins - 1);
                const int b1 = std::clamp (static_cast<int> (f1 / kFramesPerPeak), b0, nBins - 1);
                for (int b = b0; b <= b1; ++b)
                {
                    lo = std::min (lo, peaks.lows[static_cast<size_t> (b)]);
                    hi = std::max (hi, peaks.highs[static_cast<size_t> (b)]);
                }
            }
            g.drawVerticalLine (x, mid - std::clamp (hi * clip.gain, 0.0f, 1.0f) * half - 0.5f,
                                mid - std::clamp (lo * clip.gain, -1.0f, 0.0f) * half + 0.5f);
        }
        g.restoreState();
    }

    g.setColour (juce::Colours::white.withAlpha (0.92f));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (juce::String (clip.name) + (clip.stretch.isNeutral() ? juce::String() : "  [" + describeStretch (clip.stretch) + "]") + (missing ? "  (file not found)" : ""), r.reduced (5, 0).withHeight (14).translated (0, 1),
                juce::Justification::centredLeft, true);

    if (selected)
    {
        g.setColour (kSelect);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.5f);
    }
}

// ------------------------------------------------------------------------- painting

void PlaylistTimeline::resized()
{
    scrollBar.setBounds (kNameWidth, getHeight() - kScrollBarHeight, std::max (1, getWidth() - kNameWidth), kScrollBarHeight);
    clampScroll();
    updateScrollBar();
}

void PlaylistTimeline::paintGrid (juce::Graphics& g)
{
    const int top = kRulerHeight;
    const int bottom = lanesBottom();
    const double ppt = getPixelsPerTick();
    const int firstTick = std::max (0, static_cast<int> (std::floor (scrollX / ppt)) - 1);
    const int lastTick = std::min (arrangement.timelineTicks(), static_cast<int> (std::ceil ((scrollX + getWidth()) / ppt)) + 1);

    const auto lines = [&] (int step, juce::Colour colour)
    {
        if (step * ppt < 9.0)
            return;
        g.setColour (colour);
        for (int t = (firstTick / step) * step; t <= lastTick; t += step)
            g.drawVerticalLine (tickToX (t), static_cast<float> (top), static_cast<float> (bottom));
    };

    lines (15, kGridFine);
    lines (30, kGridFine);
    lines (60, kGridFine);
    lines (120, kGridFine.brighter (0.05f));
    lines (240, kGridBeat);
    lines (kTicksPerBeat, kGridBeat.brighter (0.1f));
    lines (kTicksPerBar, kGridBar);
    lines (arrangement.patternTicks, kGridUnit);
}

void PlaylistTimeline::paintRuler (juce::Graphics& g)
{
    const double ppt = getPixelsPerTick();
    const int firstTick = std::max (0, static_cast<int> (std::floor (scrollX / ppt)) - 1);
    const int lastTick = std::min (arrangement.timelineTicks(), static_cast<int> (std::ceil ((scrollX + getWidth()) / ppt)) + 1);
    const int rowH = kRulerHeight / 2;

    g.setColour (kRuler);
    g.fillRect (kNameWidth, 0, getWidth() - kNameWidth, kRulerHeight);
    g.setColour (kGridBar);
    g.drawHorizontalLine (rowH, static_cast<float> (kNameWidth), static_cast<float> (getWidth()));

    // Upper row: bars (numbered), beats and finer marks.
    g.setFont (juce::FontOptions (11.0f));
    const auto marks = [&] (int step, int height, juce::Colour colour)
    {
        if (step * ppt < 6.0)
            return;
        g.setColour (colour);
        for (int t = (firstTick / step) * step; t <= lastTick; t += step)
            g.drawVerticalLine (tickToX (t), static_cast<float> (rowH - height), static_cast<float> (rowH));
    };
    marks (30, 3, kTextDim.darker (0.4f));
    marks (120, 5, kTextDim.darker (0.2f));
    marks (kTicksPerBeat, 8, kTextDim);
    marks (kTicksPerBar, rowH - 2, kText);

    const double pxPerBar = kTicksPerBar * ppt;
    const int every = barLabelEvery (pxPerBar, 34.0);
    g.setColour (kText);
    for (int bar = (firstTick / kTicksPerBar / every) * every; bar * kTicksPerBar <= lastTick; bar += every)
        g.drawText (juce::String (bar + 1), tickToX (bar * kTicksPerBar) + 3, 0, 42, rowH, juce::Justification::centredLeft);

    if (kTicksPerBeat * ppt >= 44.0) // beat numbers "bar.beat" when there is room
    {
        g.setColour (kTextDim);
        g.setFont (juce::FontOptions (10.0f));
        for (int t = (firstTick / kTicksPerBeat) * kTicksPerBeat; t <= lastTick; t += kTicksPerBeat)
            if (t % kTicksPerBar != 0)
                g.drawText (juce::String (t / kTicksPerBar + 1) + "." + juce::String ((t % kTicksPerBar) / kTicksPerBeat + 1),
                            tickToX (t) + 3, 0, 36, rowH, juce::Justification::centredLeft);
    }

    // Lower row: time in minutes:seconds.milliseconds, with marks at a round step that fits the zoom.
    const double ticksPerSecond = secondsToTicks (1.0, bpm);
    const double pxPerSecond = ticksPerSecond * ppt;
    const double step = niceTimeStepSeconds (pxPerSecond, 70.0);
    const double firstSecond = firstTick / ticksPerSecond;
    const double lastSecond = lastTick / ticksPerSecond;
    g.setFont (juce::FontOptions (10.0f));
    for (double t = std::floor (firstSecond / step) * step; t <= lastSecond + step; t += step)
    {
        const int x = tickToX (static_cast<int> (std::lround (t * ticksPerSecond)));
        g.setColour (kTextDim);
        g.drawVerticalLine (x, static_cast<float> (rowH + 1), static_cast<float> (kRulerHeight));
        g.setColour (kTextDim.brighter (0.2f));
        g.drawText (jstr (formatSeconds (t)), x + 3, rowH, 70, rowH, juce::Justification::centredLeft);
    }
}

void PlaylistTimeline::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);

    const int th = trackHeight();

    // Timeline area (clipped so scrolled content never covers the name column or the ruler).
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (kNameWidth, 0, getWidth() - kNameWidth, getHeight());

        for (int t = 0; t < kPlaylistTracks; ++t)
        {
            g.setColour (t % 2 == 0 ? kTrackA : kTrackB);
            g.fillRect (kNameWidth, kRulerHeight + t * th, getWidth() - kNameWidth, th);
        }

        for (int lane = 0; lane < kAutomationLanes; ++lane)
        {
            g.setColour (lane % 2 == 0 ? kLaneA : kLaneB);
            g.fillRect (kNameWidth, lanesTop() + lane * kLaneHeight, getWidth() - kNameWidth, kLaneHeight);
        }

        paintGrid (g);
        paintRuler (g);

        // End of the song: song mode loops here.
        const int songUnits = arrangement.songLengthUnits();
        if (songUnits > 0)
        {
            g.setColour (kPlayhead.withAlpha (0.35f));
            g.drawVerticalLine (unitToX (songUnits), static_cast<float> (kRulerHeight), static_cast<float> (kRulerHeight + kPlaylistTracks * th));
        }

        for (int i = 0; i < static_cast<int> (arrangement.clips.size()); ++i)
        {
            const auto& clip = arrangement.clips[static_cast<size_t> (i)];
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

            if (selection.kind == Kind::Pattern && selection.index == i)
            {
                g.setColour (kSelect);
                g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.5f);
            }
        }

        pruneWaveformCache();
        for (int i = 0; i < static_cast<int> (arrangement.audioClips.size()); ++i)
            paintAudioClip (g, arrangement.audioClips[static_cast<size_t> (i)], selection.kind == Kind::Audio && selection.index == i);

        for (int i = 0; i < static_cast<int> (arrangement.automationClips.size()); ++i)
            paintAutomationClip (g, arrangement.automationClips[static_cast<size_t> (i)], selection.kind == Kind::Automation && selection.index == i);

        // Drop preview (files or sounds dragged over the timeline).
        if (dropTrack >= 0)
        {
            const auto r = juce::Rectangle<int> (tickToX (dropTick), kRulerHeight + dropTrack * th, std::max (6, tickToX (dropTick + 4 * kTicksPerBeat) - tickToX (dropTick)), th);
            g.setColour (kAudio.withAlpha (0.35f));
            g.fillRect (r);
            g.setColour (kAudio);
            g.drawRect (r, 2);
        }

        // Tool overlays.
        if (tool == PlaylistTool::Split && hoverTrack >= 0 && drag == Drag::None)
        {
            const bool onClip = arrangement.audioClipAt (hoverTrack, hoverTick) >= 0 || arrangement.clipAt (hoverTrack, hoverTick / std::max (1, arrangement.patternTicks)) >= 0;
            if (onClip)
            {
                const int audio = arrangement.audioClipAt (hoverTrack, hoverTick);
                const int tick = audio >= 0 ? snapTick (hoverTick, false)
                                            : static_cast<int> (std::lround (static_cast<double> (hoverTick) / arrangement.patternTicks)) * arrangement.patternTicks;
                g.setColour (kSelect);
                g.drawVerticalLine (tickToX (tick), static_cast<float> (kRulerHeight + hoverTrack * th), static_cast<float> (kRulerHeight + (hoverTrack + 1) * th));
            }
        }
        if (drag == Drag::CutRange)
        {
            const int from = std::min (dragStartTick, dragEndTick), to = std::max (dragStartTick, dragEndTick);
            const auto r = juce::Rectangle<int> (tickToX (from), kRulerHeight + dragTrack * th, std::max (2, tickToX (to) - tickToX (from)), th);
            g.setColour (kCut.withAlpha (0.35f));
            g.fillRect (r);
            g.setColour (kCut);
            g.drawRect (r, 2);
        }
        if (drag == Drag::ZoomRange)
        {
            const auto r = juce::Rectangle<int> (std::min (dragStartX, dragEndX), kRulerHeight, std::abs (dragEndX - dragStartX), kPlaylistTracks * th);
            g.setColour (kSelect.withAlpha (0.12f));
            g.fillRect (r);
            g.setColour (kSelect.withAlpha (0.8f));
            g.drawRect (r, 1);
        }

        if (playheadTick >= 0 && arrangement.patternTicks > 0)
        {
            g.setColour (kPlayhead);
            g.drawVerticalLine (tickToX (playheadTick), 0.0f, static_cast<float> (getHeight()));
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

    g.setColour (kAutomationColour.brighter (0.3f));
    g.setFont (juce::FontOptions (11.0f));
    for (int lane = 0; lane < kAutomationLanes; ++lane)
        g.drawText ("Auto " + juce::String (lane + 1), 6, lanesTop() + lane * kLaneHeight, kNameWidth - 8, kLaneHeight, juce::Justification::centredLeft);
}

void PlaylistTimeline::paintAutomationClip (juce::Graphics& g, const AutomationClip& c, bool selected)
{
    const auto r = automationClipRect (c).reduced (0, 1);
    if (r.getRight() < kNameWidth || r.getX() > getWidth())
        return;

    g.setColour (kAutomationColour.withAlpha (0.55f));
    g.fillRoundedRectangle (r.toFloat(), 2.0f);

    // The curve, scaled to the lowest and highest value the clip uses (a flat clip is a line in the middle).
    float lo = c.points.empty() ? 0.0f : c.points.front().value, hi = lo;
    for (const auto& p : c.points)
    {
        lo = std::min (lo, p.value);
        hi = std::max (hi, p.value);
    }
    const double span = static_cast<double> (hi) - lo;
    const int x0 = std::max (r.getX(), kNameWidth), x1 = std::min (r.getRight(), getWidth());
    const int curveHeight = std::max (4, r.getHeight() - 14); // the name is written above the curve
    juce::Path curve;
    bool started = false;
    for (int x = x0; x <= x1; x += 2)
    {
        const double tick = (static_cast<double> (x - kNameWidth) + scrollX) / getPixelsPerTick() - c.startTick;
        const float v = automationValueAt (c.points, tick, lo);
        const float norm = span > 1.0e-9 ? static_cast<float> ((v - lo) / span) : 0.5f;
        const float y = static_cast<float> (r.getBottom() - 3) - norm * static_cast<float> (curveHeight);
        if (! started)
            curve.startNewSubPath (static_cast<float> (x), y);
        else
            curve.lineTo (static_cast<float> (x), y);
        started = true;
    }
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.strokePath (curve, juce::PathStrokeType (1.2f));

    if (automationLabel)
    {
        g.setColour (juce::Colours::black.withAlpha (0.75f));
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (automationLabel (c.target), r.reduced (4, 0), juce::Justification::topLeft, true);
    }
    if (selected)
    {
        g.setColour (kSelect);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 2.0f, 1.5f);
    }
}

// ------------------------------------------------------------------------- history

PlaylistTimeline::Snapshot PlaylistTimeline::takeSnapshot() const
{
    return { arrangement.clips, arrangement.audioClips, arrangement.automationClips };
}

void PlaylistTimeline::restore (const Snapshot& s)
{
    arrangement.clips = s.clips;
    arrangement.audioClips = s.audioClips;
    arrangement.automationClips = s.automationClips;
}

void PlaylistTimeline::beginEdit()
{
    pendingUndo = takeSnapshot();
    pendingUndoValid = true;
}

void PlaylistTimeline::commitPendingUndo()
{
    if (! pendingUndoValid)
        return;
    undoStack.push_back (std::move (pendingUndo));
    if (undoStack.size() > static_cast<size_t> (kMaxUndo))
        undoStack.erase (undoStack.begin());
    redoStack.clear();
    pendingUndoValid = false;
}

void PlaylistTimeline::clearHistory()
{
    undoStack.clear();
    redoStack.clear();
    pendingUndoValid = false;
    clearSelection();
}

void PlaylistTimeline::undo()
{
    if (undoStack.empty())
        return;
    redoStack.push_back (takeSnapshot());
    restore (undoStack.back());
    undoStack.pop_back();
    clearSelection();
    notifyChanged();
}

void PlaylistTimeline::redo()
{
    if (redoStack.empty())
        return;
    undoStack.push_back (takeSnapshot());
    restore (redoStack.back());
    redoStack.pop_back();
    clearSelection();
    notifyChanged();
}

void PlaylistTimeline::notifyChanged()
{
    repaint();
    updateReadout (lastMouseX, lastMouseY);
    if (onChanged)
        onChanged();
}

void PlaylistTimeline::select (Kind kind, int index)
{
    selection = { kind, kind == Kind::None ? -1 : index };
    repaint();
    updateReadout (lastMouseX, lastMouseY);
}

void PlaylistTimeline::clearSelection()
{
    select (Kind::None, -1);
}

// ------------------------------------------------------------------------- readouts

juce::String PlaylistTimeline::describeSelection() const
{
    if (selection.kind == Kind::Audio && selection.index >= 0 && selection.index < static_cast<int> (arrangement.audioClips.size()))
    {
        const auto& c = arrangement.audioClips[static_cast<size_t> (selection.index)];
        juce::String text = juce::String (c.name) + "   start " + jstr (formatBarsBeatsTicks (c.startTick)) + " (" + jstr (formatSeconds (ticksToSeconds (c.startTick, bpm)))
                          + ")   length " + jstr (formatLengthBarsBeatsTicks (c.lengthTicks)) + " (" + jstr (formatSeconds (ticksToSeconds (c.lengthTicks, bpm)))
                          + ")   end " + jstr (formatBarsBeatsTicks (c.endTick())) + " (" + jstr (formatSeconds (ticksToSeconds (c.endTick(), bpm))) + ")";
        if (c.data != nullptr && c.data->sampleRate > 0.0)
            text += "   file " + jstr (formatSeconds (c.data->numFrames() / c.data->sampleRate)) + ", " + juce::String (juce::roundToInt (c.data->sampleRate))
                  + " Hz, starts at " + jstr (formatSeconds (c.offsetSeconds)) + " (sample " + juce::String (juce::roundToInt (c.offsetSeconds * c.data->sampleRate)) + ")";
        if (std::abs (c.gain - 1.0f) > 0.001f)
            text += "   gain " + juce::String (juce::Decibels::gainToDecibels (c.gain), 1) + " dB";
        return text;
    }

    if (selection.kind == Kind::Automation && selection.index >= 0 && selection.index < static_cast<int> (arrangement.automationClips.size()))
    {
        const auto& c = arrangement.automationClips[static_cast<size_t> (selection.index)];
        return (automationLabel ? automationLabel (c.target) : juce::String ("Automation")) + "   start " + jstr (formatBarsBeatsTicks (c.startTick))
             + " (" + jstr (formatSeconds (ticksToSeconds (c.startTick, bpm))) + ")   length " + jstr (formatLengthBarsBeatsTicks (c.lengthTicks))
             + " (" + jstr (formatSeconds (ticksToSeconds (c.lengthTicks, bpm))) + ")   " + juce::String (static_cast<int> (c.points.size())) + " points   double-click to edit";
    }

    if (selection.kind == Kind::Pattern && selection.index >= 0 && selection.index < static_cast<int> (arrangement.clips.size()))
    {
        const auto& c = arrangement.clips[static_cast<size_t> (selection.index)];
        const int start = c.startUnit * arrangement.patternTicks;
        const int length = c.lengthUnits * arrangement.patternTicks;
        const juce::String name = c.pattern >= 0 && c.pattern < static_cast<int> (arrangement.patterns.size())
                                      ? juce::String (arrangement.patterns[static_cast<size_t> (c.pattern)].name) : juce::String ("?");
        return name + "   start " + jstr (formatBarsBeatsTicks (start)) + " (" + jstr (formatSeconds (ticksToSeconds (start, bpm)))
             + ")   length " + juce::String (c.lengthUnits) + (c.lengthUnits == 1 ? " repeat = " : " repeats = ") + jstr (formatLengthBarsBeatsTicks (length))
             + " (" + jstr (formatSeconds (ticksToSeconds (length, bpm))) + ")";
    }
    return {};
}

void PlaylistTimeline::updateReadout (int x, int y)
{
    lastMouseX = x;
    lastMouseY = y;
    if (! onReadout)
        return;

    juce::String position;
    if (x >= kNameWidth && y >= kRulerHeight && y < kRulerHeight + kPlaylistTracks * trackHeight())
    {
        const int tick = xToTick (x);
        position = "Position " + jstr (formatBarsBeatsTicks (tick)) + "  =  " + jstr (formatSeconds (ticksToSeconds (tick, bpm)))
                 + "   Track " + juce::String (yToTrack (y) + 1);
    }
    else if (x >= kNameWidth && inLanes (y))
    {
        const int tick = xToTick (x);
        position = "Position " + jstr (formatBarsBeatsTicks (tick)) + "  =  " + jstr (formatSeconds (ticksToSeconds (tick, bpm)))
                 + "   Automation lane " + juce::String (yToLane (y) + 1);
    }
    onReadout (position, describeSelection());
}

void PlaylistTimeline::setHover (int track, int tick)
{
    hoverTrack = track;
    hoverTick = tick;
}

// ------------------------------------------------------------------------- editing actions

bool PlaylistTimeline::eraseAt (int track, int tick)
{
    const int audio = arrangement.audioClipAt (track, tick);
    if (audio >= 0)
    {
        commitPendingUndo();
        arrangement.audioClips.erase (arrangement.audioClips.begin() + audio);
        clearSelection();
        return true;
    }
    const int pattern = arrangement.clipAt (track, std::clamp (tick / std::max (1, arrangement.patternTicks), 0, kPlaylistUnits - 1));
    if (pattern >= 0)
    {
        commitPendingUndo();
        arrangement.clips.erase (arrangement.clips.begin() + pattern);
        clearSelection();
        return true;
    }
    return false;
}

int PlaylistTimeline::addAutomationClip (AutomationClip clip, int startTick)
{
    if (arrangement.automationClips.size() >= static_cast<size_t> (kMaxAutomationClips))
        return -1;

    // From the wanted tick, then from the start of each following beat, until a lane has room.
    int start = std::max (0, startTick);
    for (int attempt = 0; attempt < 4096; ++attempt)
    {
        if (start + clip.lengthTicks > arrangement.timelineTicks())
            return -1;
        const int lane = arrangement.findFreeAutomationLane (start, clip.lengthTicks);
        if (lane >= 0)
        {
            clip.startTick = start;
            clip.lane = lane;
            beginEdit();
            commitPendingUndo();
            arrangement.automationClips.push_back (clip);
            const int index = static_cast<int> (arrangement.automationClips.size()) - 1;
            select (Kind::Automation, index);
            notifyChanged();
            return index;
        }
        start = (start / kTicksPerBeat + 1) * kTicksPerBeat;
    }
    return -1;
}

void PlaylistTimeline::pushUndoPoint()
{
    beginEdit();
    commitPendingUndo();
}

void PlaylistTimeline::selectAutomation (int index)
{
    if (index >= 0 && index < static_cast<int> (arrangement.automationClips.size()))
        select (Kind::Automation, index);
    else
        clearSelection();
}

void PlaylistTimeline::showAutomationMenu (int index)
{
    select (Kind::Automation, index);
    juce::PopupMenu menu;
    menu.addItem (1, "Edit the points...");
    menu.addItem (2, "Duplicate (Ctrl+D)");
    menu.addSeparator();
    menu.addItem (3, "Delete (Delete)");
    menu.showMenuAsync (juce::PopupMenu::Options(),
                        [safe = juce::Component::SafePointer<PlaylistTimeline> (this), index] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result == 1 && safe->onAutomationPicked)
                                safe->onAutomationPicked (index);
                            else if (result == 2)
                                safe->duplicateSelected();
                            else if (result == 3)
                                safe->deleteSelected();
                        });
}

bool PlaylistTimeline::splitAt (int track, int tick, bool noSnap)
{
    const int audio = arrangement.audioClipAt (track, tick);
    if (audio >= 0)
    {
        beginEdit();
        if (splitAudioClip (arrangement, audio, snapTick (tick, noSnap), bpm) >= 0)
        {
            commitPendingUndo();
            select (Kind::Audio, audio);
            return true;
        }
        pendingUndoValid = false;
        return false;
    }

    const int unit = std::clamp (tick / std::max (1, arrangement.patternTicks), 0, kPlaylistUnits - 1);
    const int pattern = arrangement.clipAt (track, unit);
    if (pattern >= 0)
    {
        const int boundary = static_cast<int> (std::lround (static_cast<double> (tick) / arrangement.patternTicks));
        beginEdit();
        if (splitPatternClip (arrangement, pattern, boundary) >= 0)
        {
            commitPendingUndo();
            select (Kind::Pattern, pattern);
            return true;
        }
        pendingUndoValid = false;
    }
    return false;
}

void PlaylistTimeline::deleteSelected()
{
    if (selection.kind == Kind::Audio && selection.index >= 0 && selection.index < static_cast<int> (arrangement.audioClips.size()))
    {
        beginEdit();
        commitPendingUndo();
        arrangement.audioClips.erase (arrangement.audioClips.begin() + selection.index);
    }
    else if (selection.kind == Kind::Pattern && selection.index >= 0 && selection.index < static_cast<int> (arrangement.clips.size()))
    {
        beginEdit();
        commitPendingUndo();
        arrangement.clips.erase (arrangement.clips.begin() + selection.index);
    }
    else if (selection.kind == Kind::Automation && selection.index >= 0 && selection.index < static_cast<int> (arrangement.automationClips.size()))
    {
        beginEdit();
        commitPendingUndo();
        arrangement.automationClips.erase (arrangement.automationClips.begin() + selection.index);
    }
    else
        return;

    clearSelection();
    notifyChanged();
}

void PlaylistTimeline::copySelected()
{
    if (selection.kind == Kind::Audio && selection.index >= 0 && selection.index < static_cast<int> (arrangement.audioClips.size()))
    {
        clipboard.kind = Kind::Audio;
        clipboard.audio = arrangement.audioClips[static_cast<size_t> (selection.index)];
    }
    else if (selection.kind == Kind::Pattern && selection.index >= 0 && selection.index < static_cast<int> (arrangement.clips.size()))
    {
        clipboard.kind = Kind::Pattern;
        clipboard.pattern = arrangement.clips[static_cast<size_t> (selection.index)];
    }
}

void PlaylistTimeline::pasteAt (int track, int tick)
{
    if (clipboard.kind == Kind::Audio)
    {
        AudioClip c = clipboard.audio;
        if (arrangement.audioClips.size() >= static_cast<size_t> (kMaxAudioClips))
            return;
        c.track = std::clamp (track, 0, kPlaylistTracks - 1);
        c.startTick = std::clamp (snapTick (tick, false), 0, std::max (0, arrangement.timelineTicks() - c.lengthTicks));
        if (! arrangement.canPlaceAudio (c))
        {
            const int free = arrangement.findFreeAudioStart (c.track, c.startTick, c.lengthTicks);
            if (free < 0)
                return;
            c.startTick = free;
        }
        beginEdit();
        commitPendingUndo();
        arrangement.audioClips.push_back (c);
        select (Kind::Audio, static_cast<int> (arrangement.audioClips.size()) - 1);
        notifyChanged();
    }
    else if (clipboard.kind == Kind::Pattern)
    {
        Clip c = clipboard.pattern;
        c.track = std::clamp (track, 0, kPlaylistTracks - 1);
        c.startUnit = std::clamp (tick / std::max (1, arrangement.patternTicks), 0, kPlaylistUnits - c.lengthUnits);
        while (c.endUnit() <= kPlaylistUnits && ! arrangement.canPlace (c))
            ++c.startUnit;
        if (c.endUnit() > kPlaylistUnits)
            return;
        beginEdit();
        commitPendingUndo();
        arrangement.clips.push_back (c);
        select (Kind::Pattern, static_cast<int> (arrangement.clips.size()) - 1);
        notifyChanged();
    }
}

void PlaylistTimeline::duplicateSelected()
{
    if (selection.kind == Kind::Automation && selection.index >= 0 && selection.index < static_cast<int> (arrangement.automationClips.size()))
    {
        const auto original = arrangement.automationClips[static_cast<size_t> (selection.index)];
        addAutomationClip (original, original.endTick());
        return;
    }

    const auto saved = clipboard;
    copySelected();
    if (clipboard.kind == Kind::Audio)
        pasteAt (clipboard.audio.track, clipboard.audio.endTick());
    else if (clipboard.kind == Kind::Pattern)
        pasteAt (clipboard.pattern.track, clipboard.pattern.endUnit() * arrangement.patternTicks);
    clipboard = saved;
}

void PlaylistTimeline::editAudioProperties (int index)
{
    if (index < 0 || index >= static_cast<int> (arrangement.audioClips.size()))
        return;

    const auto clip = arrangement.audioClips[static_cast<size_t> (index)];
    auto* window = new juce::AlertWindow ("Audio clip", juce::String (clip.name) + "\nEnter the start and the length in seconds, and the gain in dB.\nTempo " + juce::String (bpm, 1) + " BPM: one beat = "
                                                          + juce::String (60.0 / bpm, 3) + " s.",
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("start", juce::String (ticksToSeconds (clip.startTick, bpm), 4), "Start (seconds)");
    window->addTextEditor ("length", juce::String (ticksToSeconds (clip.lengthTicks, bpm), 4), "Length (seconds)");
    window->addTextEditor ("gain", juce::String (juce::Decibels::gainToDecibels (clip.gain), 1), "Gain (dB, at most +6)");
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true,
                             juce::ModalCallbackFunction::create (
                                 [safe = juce::Component::SafePointer<PlaylistTimeline> (this), window, index, clip] (int result)
                                 {
                                     if (safe == nullptr || result != 1 || index >= static_cast<int> (safe->arrangement.audioClips.size()))
                                         return;

                                     auto edited = clip;
                                     const double bpmNow = safe->bpm;
                                     const double startSeconds = window->getTextEditorContents ("start").getDoubleValue();
                                     const double lengthSeconds = window->getTextEditorContents ("length").getDoubleValue();
                                     const double gainDb = window->getTextEditorContents ("gain").getDoubleValue();
                                     edited.startTick = static_cast<int> (std::lround (secondsToTicks (std::max (0.0, startSeconds), bpmNow)));
                                     edited.lengthTicks = std::clamp (static_cast<int> (std::lround (secondsToTicks (lengthSeconds, bpmNow))), 1,
                                                                      std::max (1, edited.naturalLengthTicks (bpmNow)));
                                     edited.gain = std::clamp (juce::Decibels::decibelsToGain (static_cast<float> (std::min (gainDb, 6.0))), 0.0f, 2.0f);

                                     if (! safe->arrangement.canPlaceAudio (edited, index))
                                     {
                                         juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Audio clip",
                                                                                 "The clip does not fit there: it would overlap another clip or go past the end of the playlist.");
                                         return;
                                     }
                                     safe->beginEdit();
                                     safe->commitPendingUndo();
                                     safe->arrangement.audioClips[static_cast<size_t> (index)] = edited;
                                     safe->notifyChanged();
                                 }),
                             true);
}

// Time-stretch and pitch-shift of one audio clip: the settings are typed in, the audio is rendered again at once (a few
// seconds for a long file) and the clip gets longer or shorter by the same factor.
void PlaylistTimeline::editAudioStretch (int index)
{
    if (index < 0 || index >= static_cast<int> (arrangement.audioClips.size()))
        return;

    const auto clip = arrangement.audioClips[static_cast<size_t> (index)];
    if (clip.data == nullptr || clip.data->numFrames() < 2)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Stretch and pitch", "This clip has no audio (its file is missing).");
        return;
    }

    auto* window = new juce::AlertWindow ("Stretch and pitch", juce::String (clip.name)
                                              + "\nLength 200 % = twice as long and half the tempo; 50 % = half as long. The pitch stays the same unless you change it."
                                              + "\nIf you know the tempo the audio was recorded at, type it: the length is set so that it plays in time at "
                                              + juce::String (bpm, 1) + " BPM (this overrides the length).",
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("length", juce::String (clip.stretch.lengthRatio * 100.0, 2), "Length (%, 25 to 400)");
    window->addTextEditor ("pitch", juce::String (clip.stretch.semitones, 2), "Pitch (semitones, -24 to +24, decimals allowed)");
    window->addTextEditor ("bpm", {}, "Tempo of the audio (BPM, optional)");
    window->addComboBox ("mode", { "Normal", "Keep the timbre (limits the shift of high frequencies)" }, "Pitch mode");
    if (auto* mode = window->getComboBoxComponent ("mode"))
        mode->setSelectedItemIndex (clip.stretch.keepTimbre ? 1 : 0, juce::dontSendNotification);
    window->addButton ("Apply", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Back to the original", 2);
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true,
                             juce::ModalCallbackFunction::create (
                                 [safe = juce::Component::SafePointer<PlaylistTimeline> (this), window, index, clip] (int result)
                                 {
                                     if (safe == nullptr || result == 0 || index >= static_cast<int> (safe->arrangement.audioClips.size()))
                                         return;

                                     StretchSettings wanted;
                                     if (result == 1)
                                     {
                                         wanted.lengthRatio = window->getTextEditorContents ("length").getDoubleValue() / 100.0;
                                         wanted.semitones = window->getTextEditorContents ("pitch").getDoubleValue();
                                         if (auto* mode = window->getComboBoxComponent ("mode"))
                                             wanted.keepTimbre = mode->getSelectedItemIndex() == 1;
                                         const auto sourceBpm = window->getTextEditorContents ("bpm").trim();
                                         if (sourceBpm.isNotEmpty())
                                             wanted.lengthRatio = lengthRatioForTempo (sourceBpm.getDoubleValue(), safe->bpm);
                                         if (! (wanted.lengthRatio > 0.0))
                                             wanted.lengthRatio = 1.0;
                                     }

                                     auto edited = clip;
                                     juce::MouseCursor::showWaitCursor();
                                     const bool rendered = restretchClip (edited, wanted, safe->bpm);
                                     juce::MouseCursor::hideWaitCursor();
                                     if (! rendered)
                                     {
                                         juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Stretch and pitch",
                                                                                 "The audio could not be processed (it is longer than about 15 minutes, or empty).");
                                         return;
                                     }
                                     if (! safe->arrangement.canPlaceAudio (edited, index))
                                     {
                                         juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Stretch and pitch",
                                                                                 "The longer clip does not fit there: it would overlap another clip or go past the end of the playlist. Move the neighbour away or choose a shorter length.");
                                         return;
                                     }
                                     safe->beginEdit();
                                     safe->commitPendingUndo();
                                     safe->arrangement.audioClips[static_cast<size_t> (index)] = edited;
                                     safe->notifyChanged();
                                 }),
                             true);
}

void PlaylistTimeline::showClipMenu (const juce::MouseEvent& e)
{
    const int track = yToTrack (e.y);
    const int tick = xToTick (e.x);
    const int audio = arrangement.audioClipAt (track, tick);
    const int pattern = audio >= 0 ? -1 : arrangement.clipAt (track, std::clamp (tick / std::max (1, arrangement.patternTicks), 0, kPlaylistUnits - 1));
    if (audio < 0 && pattern < 0)
        return;

    select (audio >= 0 ? Kind::Audio : Kind::Pattern, audio >= 0 ? audio : pattern);

    juce::PopupMenu menu;
    menu.addItem (1, "Split here");
    menu.addItem (2, "Duplicate (Ctrl+D)");
    menu.addItem (3, "Copy (Ctrl+C)");
    if (audio >= 0)
    {
        menu.addItem (4, "Position, length and gain...");
        menu.addItem (7, "Stretch and pitch...");
    }
    else
        menu.addItem (5, "Open this pattern");
    menu.addSeparator();
    menu.addItem (6, "Delete (Delete)");

    menu.showMenuAsync (juce::PopupMenu::Options(),
                        [safe = juce::Component::SafePointer<PlaylistTimeline> (this), track, tick, audio] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            switch (result)
                            {
                                case 1: if (safe->splitAt (track, tick, false)) safe->notifyChanged(); break;
                                case 2: safe->duplicateSelected(); break;
                                case 3: safe->copySelected(); break;
                                case 4: safe->editAudioProperties (audio); break;
                                case 7: safe->editAudioStretch (audio); break;
                                case 5:
                                    if (safe->selection.kind == Kind::Pattern && safe->selection.index >= 0
                                        && safe->selection.index < static_cast<int> (safe->arrangement.clips.size()) && safe->onPatternPicked)
                                        safe->onPatternPicked (safe->arrangement.clips[static_cast<size_t> (safe->selection.index)].pattern);
                                    break;
                                case 6: safe->deleteSelected(); break;
                                default: break;
                            }
                        });
}

bool PlaylistTimeline::handleKey (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const auto code = key.getKeyCode();
    const bool command = mods.isCommandDown();

    if (command)
    {
        const auto letter = juce::CharacterFunctions::toUpperCase (static_cast<juce::juce_wchar> (code));
        const int track = hoverTrack >= 0 ? hoverTrack : armedTrack;
        if (letter == 'Z') { if (mods.isShiftDown()) redo(); else undo(); return true; }
        if (letter == 'Y') { redo(); return true; }
        if (letter == 'C') { copySelected(); return selection.kind != Kind::None; }
        if (letter == 'X') { copySelected(); deleteSelected(); return true; }
        if (letter == 'V') { pasteAt (track, hoverTrack >= 0 ? hoverTick : 0); return true; }
        if (letter == 'D') { duplicateSelected(); return true; }
        return false;
    }

    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        const bool had = selection.kind != Kind::None;
        deleteSelected();
        return had;
    }

    if (mods.isAnyModifierKeyDown())
        return false;

    const auto changeTool = [this] (PlaylistTool t)
    {
        setTool (t);
        if (onToolChanged)
            onToolChanged (t);
    };
    switch (key.getTextCharacter())
    {
        case '1': changeTool (PlaylistTool::Select); return true;
        case '2': changeTool (PlaylistTool::Split); return true;
        case '3': changeTool (PlaylistTool::Cut); return true;
        case '4': changeTool (PlaylistTool::Erase); return true;
        case '5': changeTool (PlaylistTool::Zoom); return true;
        case '+': case '=': zoomBy (1.5, lastMouseX); return true;
        case '-': zoomBy (1.0 / 1.5, lastMouseX); return true;
        case 's': case 'S':
            // split the selected clip at the mouse position
            if (hoverTrack >= 0 && ((selection.kind == Kind::Audio && selection.index >= 0 && selection.index < static_cast<int> (arrangement.audioClips.size()))
                                    || (selection.kind == Kind::Pattern && selection.index >= 0 && selection.index < static_cast<int> (arrangement.clips.size()))))
            {
                const int track = selection.kind == Kind::Audio ? arrangement.audioClips[static_cast<size_t> (selection.index)].track
                                                                : arrangement.clips[static_cast<size_t> (selection.index)].track;
                if (splitAt (track, hoverTick, false))
                    notifyChanged();
                return true;
            }
            return false;
        default: break;
    }
    return false;
}

// ------------------------------------------------------------------------- mouse

void PlaylistTimeline::mouseMove (const juce::MouseEvent& e)
{
    updateReadout (e.x, e.y);

    if (e.x >= kNameWidth && e.y >= kRulerHeight && e.y < kRulerHeight + kPlaylistTracks * trackHeight())
        setHover (yToTrack (e.y), xToTick (e.x));
    else
        setHover (-1, 0);

    if (tool == PlaylistTool::Split || tool == PlaylistTool::Cut)
        repaint();

    if (tool == PlaylistTool::Select && e.x >= kNameWidth && inLanes (e.y))
    {
        const int hitClip = arrangement.automationClipAt (yToLane (e.y), xToTick (e.x));
        const bool edge = hitClip >= 0 && e.x >= automationClipRect (arrangement.automationClips[static_cast<size_t> (hitClip)]).getRight() - kEdgeGrab;
        setMouseCursor (edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        return;
    }

    // Resize cursor over the edge of an audio clip (Select tool).
    if (tool == PlaylistTool::Select && hoverTrack >= 0)
    {
        const int audio = arrangement.audioClipAt (hoverTrack, hoverTick);
        bool edge = false;
        if (audio >= 0)
        {
            const auto r = audioClipRect (arrangement.audioClips[static_cast<size_t> (audio)]);
            edge = r.getWidth() >= 3 * kEdgeGrab && (e.x <= r.getX() + kEdgeGrab || e.x >= r.getRight() - kEdgeGrab);
            edge = edge || (r.getWidth() < 3 * kEdgeGrab && e.x >= r.getRight() - kEdgeGrab);
        }
        else
        {
            const int pattern = arrangement.clipAt (hoverTrack, xToUnit (e.x));
            if (pattern >= 0)
                edge = e.x >= clipRect (arrangement.clips[static_cast<size_t> (pattern)]).getRight() - kEdgeGrab;
        }
        setMouseCursor (edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }
}

void PlaylistTimeline::mouseExit (const juce::MouseEvent&)
{
    setHover (-1, 0);
    updateReadout (-1, -1);
    if (tool == PlaylistTool::Split || tool == PlaylistTool::Cut)
        repaint();
}

void PlaylistTimeline::mouseDown (const juce::MouseEvent& e)
{
    drag = Drag::None;
    dragClip = -1;
    dragChanged = false;
    pendingUndoValid = false;
    lastMouseX = e.x;
    lastMouseY = e.y;

    if (e.x < kNameWidth && e.y >= kRulerHeight)
    {
        const int track = yToTrack (e.y);
        setArmedTrack (track);
        if (onTrackSelected)
            onTrackSelected (track);
        return;
    }
    if (e.x >= kNameWidth && inLanes (e.y))
    {
        const int lane = yToLane (e.y);
        const int tick = xToTick (e.x);
        const int hitClip = arrangement.automationClipAt (lane, tick);

        if (tool == PlaylistTool::Zoom)
        {
            if (e.mods.isPopupMenu() || e.mods.isShiftDown())
                zoomBy (1.0 / 2.0, e.x);
            else
            {
                drag = Drag::ZoomRange;
                dragStartX = dragEndX = e.x;
            }
            return;
        }
        if (hitClip < 0)
        {
            clearSelection();
            return;
        }
        if (e.mods.isPopupMenu())
        {
            showAutomationMenu (hitClip);
            return;
        }
        if (tool == PlaylistTool::Erase)
        {
            beginEdit();
            commitPendingUndo();
            arrangement.automationClips.erase (arrangement.automationClips.begin() + hitClip);
            clearSelection();
            notifyChanged();
            return;
        }
        if (tool != PlaylistTool::Select)
        {
            select (Kind::Automation, hitClip);
            return;
        }

        select (Kind::Automation, hitClip);
        dragClip = hitClip;
        dragAutomationOriginal = arrangement.automationClips[static_cast<size_t> (hitClip)];
        beginEdit();
        if (e.x >= automationClipRect (dragAutomationOriginal).getRight() - kEdgeGrab)
            drag = Drag::ResizeAutomation;
        else
        {
            drag = Drag::MoveAutomation;
            grabOffsetTicks = tick - dragAutomationOriginal.startTick;
        }
        return;
    }
    if (e.x < kNameWidth || e.y < kRulerHeight || e.y >= kRulerHeight + kPlaylistTracks * trackHeight())
    {
        // The ruler zooms too: a drag selects the stretch to zoom to.
        if (e.x >= kNameWidth && e.y < kRulerHeight && tool == PlaylistTool::Zoom && ! e.mods.isPopupMenu())
        {
            drag = Drag::ZoomRange;
            dragStartX = dragEndX = e.x;
        }
        return;
    }

    const int track = yToTrack (e.y);
    const int tick = xToTick (e.x);
    const int unit = xToUnit (e.x);
    const bool noSnap = e.mods.isAltDown();
    const int hit = arrangement.clipAt (track, unit);
    const int audioHit = arrangement.audioClipAt (track, tick);

    if (tool == PlaylistTool::Zoom)
    {
        if (e.mods.isPopupMenu() || e.mods.isShiftDown())
        {
            zoomBy (1.0 / 2.0, e.x);
            return;
        }
        drag = Drag::ZoomRange;
        dragStartX = dragEndX = e.x;
        return;
    }

    if (e.mods.isPopupMenu())
    {
        showClipMenu (e);
        return;
    }

    if (tool == PlaylistTool::Erase)
    {
        beginEdit();
        drag = Drag::EraseSweep;
        if (eraseAt (track, tick))
            notifyChanged();
        return;
    }

    if (tool == PlaylistTool::Split)
    {
        if (splitAt (track, tick, noSnap))
            notifyChanged();
        return;
    }

    if (tool == PlaylistTool::Cut)
    {
        if (audioHit >= 0)
        {
            drag = Drag::CutRange;
            dragClip = audioHit;
            dragTrack = track;
            dragStartTick = dragEndTick = snapTick (tick, noSnap);
            select (Kind::Audio, audioHit);
        }
        else if (hit >= 0)
        {
            drag = Drag::CutRange;
            dragClip = hit;
            dragTrack = track;
            dragStartTick = dragEndTick = unit * arrangement.patternTicks;
            select (Kind::Pattern, hit);
        }
        return;
    }

    // Select tool.
    if (audioHit >= 0)
    {
        dragClip = audioHit;
        select (Kind::Audio, audioHit);
        const auto& clip = arrangement.audioClips[static_cast<size_t> (audioHit)];
        const auto r = audioClipRect (clip);
        beginEdit();
        if (r.getWidth() >= 3 * kEdgeGrab && e.x <= r.getX() + kEdgeGrab)
            drag = Drag::TrimAudio;
        else if (e.x >= r.getRight() - kEdgeGrab)
            drag = Drag::ResizeAudio;
        else
        {
            drag = Drag::MoveAudio;
            grabOffsetTicks = tick - clip.startTick;
        }
        return;
    }

    if (hit < 0)
    {
        const Clip c { selectedPattern, track, unit, 1 };
        if (arrangement.canPlace (c))
        {
            beginEdit();
            commitPendingUndo();
            arrangement.clips.push_back (c);
            // Dragging right after placing resizes the new clip.
            dragClip = static_cast<int> (arrangement.clips.size()) - 1;
            drag = Drag::ResizePattern; // the resize that follows belongs to the same undo step as the placement
            select (Kind::Pattern, dragClip);
            notifyChanged();
        }
        else
            clearSelection();
        return;
    }

    dragClip = hit;
    select (Kind::Pattern, hit);
    beginEdit();
    const auto r = clipRect (arrangement.clips[static_cast<size_t> (hit)]);
    if (e.x >= r.getRight() - kEdgeGrab)
        drag = Drag::ResizePattern;
    else
    {
        drag = Drag::MovePattern;
        grabOffsetUnits = unit - arrangement.clips[static_cast<size_t> (hit)].startUnit;
    }
}

void PlaylistTimeline::mouseDrag (const juce::MouseEvent& e)
{
    lastMouseX = e.x;
    lastMouseY = e.y;
    const bool noSnap = e.mods.isAltDown();

    if (drag == Drag::ZoomRange)
    {
        dragEndX = e.x;
        repaint();
        return;
    }
    if (drag == Drag::EraseSweep)
    {
        if (e.x >= kNameWidth && e.y >= kRulerHeight && eraseAt (yToTrack (e.y), xToTick (e.x)))
            notifyChanged();
        return;
    }
    if (drag == Drag::CutRange)
    {
        if (dragClip >= 0 && selection.kind == Kind::Audio && dragClip < static_cast<int> (arrangement.audioClips.size()))
            dragEndTick = snapTick (xToTick (e.x), noSnap);
        else
        {
            const double u = static_cast<double> (xToTick (e.x)) / std::max (1, arrangement.patternTicks);
            dragEndTick = (e.x >= tickToX (dragStartTick) ? static_cast<int> (std::floor (u)) + 1 : static_cast<int> (std::floor (u))) * arrangement.patternTicks;
        }
        repaint();
        return;
    }

    if ((drag == Drag::MoveAutomation || drag == Drag::ResizeAutomation) && dragClip >= 0
        && dragClip < static_cast<int> (arrangement.automationClips.size()))
    {
        auto clip = dragAutomationOriginal;
        const int mouseTick = xToTick (e.x);
        if (drag == Drag::MoveAutomation)
        {
            clip.startTick = std::clamp (snapTick (mouseTick - grabOffsetTicks, noSnap), 0, std::max (0, arrangement.timelineTicks() - clip.lengthTicks));
            clip.lane = yToLane (e.y);
        }
        else
            setAutomationLength (clip, std::max (effectiveSnap (noSnap), snapTick (mouseTick, noSnap) - clip.startTick));

        auto& stored = arrangement.automationClips[static_cast<size_t> (dragClip)];
        if ((clip.startTick != stored.startTick || clip.lane != stored.lane || clip.lengthTicks != stored.lengthTicks)
            && arrangement.canPlaceAutomation (clip, dragClip))
        {
            commitPendingUndo();
            stored = clip;
            dragChanged = true;
            repaint();
            updateReadout (e.x, e.y);
            if (onChanged)
                onChanged();
        }
        return;
    }

    if ((drag == Drag::MoveAudio || drag == Drag::ResizeAudio || drag == Drag::TrimAudio) && dragClip >= 0
        && dragClip < static_cast<int> (arrangement.audioClips.size()))
    {
        auto clip = arrangement.audioClips[static_cast<size_t> (dragClip)];
        const int mouseTick = xToTick (e.x);
        const int step = effectiveSnap (noSnap);

        if (drag == Drag::MoveAudio)
        {
            clip.startTick = std::clamp (snapTick (mouseTick - grabOffsetTicks, noSnap), 0, std::max (0, arrangement.timelineTicks() - clip.lengthTicks));
            clip.track = yToTrack (e.y);
        }
        else if (drag == Drag::TrimAudio)
        {
            clip = trimmedAudioStart (clip, snapTick (mouseTick, noSnap), bpm);
        }
        else
        {
            // As long as the audio really is at most; as short as one grid step.
            const int natural = clip.naturalLengthTicks (bpm);
            const int end = snapTick (mouseTick, noSnap);
            clip.lengthTicks = std::clamp (end - clip.startTick, std::min (step, std::max (1, natural)), std::max (1, natural));
        }

        auto& stored = arrangement.audioClips[static_cast<size_t> (dragClip)];
        if (! (clip.startTick == stored.startTick && clip.track == stored.track && clip.lengthTicks == stored.lengthTicks
               && std::abs (clip.offsetSeconds - stored.offsetSeconds) < 1.0e-12)
            && arrangement.canPlaceAudio (clip, dragClip))
        {
            commitPendingUndo();
            stored = clip;
            dragChanged = true;
            repaint();
            updateReadout (e.x, e.y);
            if (onChanged)
                onChanged();
        }
        return;
    }

    if ((drag != Drag::MovePattern && drag != Drag::ResizePattern) || dragClip < 0 || dragClip >= static_cast<int> (arrangement.clips.size()))
        return;

    auto clip = arrangement.clips[static_cast<size_t> (dragClip)];

    if (drag == Drag::MovePattern)
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
        commitPendingUndo();
        stored = clip;
        dragChanged = true;
        repaint();
        updateReadout (e.x, e.y);
        if (onChanged)
            onChanged();
    }
}

void PlaylistTimeline::mouseUp (const juce::MouseEvent& e)
{
    if (drag == Drag::ZoomRange)
    {
        if (std::abs (dragEndX - dragStartX) > 8)
            zoomToTicks (xToTick (std::min (dragStartX, dragEndX)), xToTick (std::max (dragStartX, dragEndX)));
        else if (e.x >= kNameWidth)
            zoomBy (2.0, e.x);
    }
    else if (drag == Drag::CutRange && dragClip >= 0)
    {
        const int from = std::min (dragStartTick, dragEndTick);
        const int to = std::max (dragStartTick, dragEndTick);
        if (to > from)
        {
            beginEdit();
            int result = -1;
            if (selection.kind == Kind::Audio)
                result = cutAudioRange (arrangement, dragClip, from, to, bpm);
            else if (selection.kind == Kind::Pattern)
                result = cutPatternRange (arrangement, dragClip, from / std::max (1, arrangement.patternTicks),
                                          (to + arrangement.patternTicks - 1) / std::max (1, arrangement.patternTicks));
            if (result >= 0)
            {
                commitPendingUndo();
                clearSelection();
                notifyChanged();
            }
            else
                pendingUndoValid = false;
        }
    }

    drag = Drag::None;
    dragClip = -1;
    pendingUndoValid = false;
    repaint();
}

void PlaylistTimeline::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.x < kNameWidth || e.y < kRulerHeight || tool != PlaylistTool::Select)
        return;

    if (inLanes (e.y))
    {
        const int hitClip = arrangement.automationClipAt (yToLane (e.y), xToTick (e.x));
        if (hitClip >= 0 && onAutomationPicked)
            onAutomationPicked (hitClip);
        return;
    }

    const int track = yToTrack (e.y);
    const int audio = arrangement.audioClipAt (track, xToTick (e.x));
    if (audio >= 0)
    {
        editAudioProperties (audio);
        return;
    }

    const int hit = arrangement.clipAt (track, xToUnit (e.x));
    if (hit >= 0 && onPatternPicked)
        onPatternPicked (arrangement.clips[static_cast<size_t> (hit)].pattern);
}

void PlaylistTimeline::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const float delta = std::abs (wheel.deltaY) >= std::abs (wheel.deltaX) ? wheel.deltaY : wheel.deltaX;

    if (e.mods.isCtrlDown())
    {
        zoomBy (std::pow (1.25, static_cast<double> (delta) * 4.0), e.x);
        return;
    }

    scrollX -= static_cast<double> (delta) * 400.0;
    clampScroll();
    updateScrollBar();
    repaint();
}

// ------------------------------------------------------------------------- dropping files and sounds

bool PlaylistTimeline::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (classifyMediaFile (juce::File (f)) != FileKind::Unsupported || isMidiFileName (f.toStdString()))
            return true;
    return false;
}

void PlaylistTimeline::fileDragEnter (const juce::StringArray& files, int x, int y)
{
    fileDragMove (files, x, y);
}

void PlaylistTimeline::fileDragMove (const juce::StringArray&, int x, int y)
{
    const bool onTimeline = x >= kNameWidth && y >= kRulerHeight;
    const int track = onTimeline ? yToTrack (y) : -1;
    const int tick = onTimeline ? snapTick (xToTick (x), false) : 0;
    if (track != dropTrack || tick != dropTick)
    {
        dropTrack = track;
        dropTick = tick;
        repaint();
    }
}

void PlaylistTimeline::fileDragExit (const juce::StringArray&)
{
    dropTrack = -1;
    repaint();
}

void PlaylistTimeline::filesDropped (const juce::StringArray& files, int x, int y)
{
    dropTrack = -1;
    repaint();

    // Dropped on the track names or the ruler counts as the start of the first track / the nearest track.
    const int track = yToTrack (y);
    const int tick = x < kNameWidth ? 0 : std::clamp (snapTick (xToTick (x), false), 0, arrangement.timelineTicks() - 1);

    juce::StringArray usable;
    for (const auto& f : files)
        if (classifyMediaFile (juce::File (f)) != FileKind::Unsupported || isMidiFileName (f.toStdString()))
            usable.add (f);

    if (! usable.isEmpty() && onFilesDropped)
        onFilesDropped (usable, track, tick);
}

bool PlaylistTimeline::isInterestedInDragSource (const SourceDetails& details)
{
    drag::Item item;
    if (! drag::Item::parse (details.description, item))
        return false;
    if (item.kind == "file")
        return classifyMediaFile (juce::File (item.payload)) != FileKind::Unsupported;
    return item.kind == "builtin";
}

void PlaylistTimeline::itemDragEnter (const SourceDetails& details)
{
    itemDragMove (details);
}

void PlaylistTimeline::itemDragMove (const SourceDetails& details)
{
    fileDragMove ({}, details.localPosition.x, details.localPosition.y);
}

void PlaylistTimeline::itemDragExit (const SourceDetails&)
{
    dropTrack = -1;
    repaint();
}

void PlaylistTimeline::itemDropped (const SourceDetails& details)
{
    dropTrack = -1;
    repaint();

    drag::Item item;
    if (! drag::Item::parse (details.description, item) || ! onItemDropped)
        return;

    const int x = details.localPosition.x, y = details.localPosition.y;
    const int track = yToTrack (y);
    const int tick = x < kNameWidth ? 0 : std::clamp (snapTick (xToTick (x), false), 0, arrangement.timelineTicks() - 1);
    onItemDropped (item, track, tick);
}
} // namespace sls
