#include "ui/PlaylistTimeline.h"

#include "audio/TempoMap.h"

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
const juce::Colour kLoop { 0xff5fb0ff };

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
    trackScrollBar.addListener (this);
    trackScrollBar.setAutoHide (false);
    addChildComponent (trackScrollBar);
    // The tracks' - and + (0.44.0), in the corner above the track names.
    removeTrackButton.setTooltip ("Remove the last playlist track (right-click a track's name to delete that one)");
    addTrackButton.setTooltip ("Add a playlist track at the end");
    removeTrackButton.onClick = [this] { removeTrack (arrangement.trackCount() - 1); };
    addTrackButton.onClick = [this] { addTrack(); };
    for (auto* b : { &removeTrackButton, &addTrackButton })
    {
        b->setMouseClickGrabsKeyboardFocus (false);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    unitWidth = 120.0;
}

PlaylistTimeline::~PlaylistTimeline()
{
    scrollBar.removeListener (this);
    trackScrollBar.removeListener (this);
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

namespace
{
int availableTrackHeight (int componentHeight) noexcept
{
    return componentHeight - 38 - 14 - kAutomationLanes * 22 - 4; // ruler, scroll bar, automation lanes, gap
}
} // namespace

// Up to kMaxVisibleTracks tracks share the height; more keep that size and scroll (0.44.0).
int PlaylistTimeline::trackHeight() const noexcept
{
    return std::clamp (availableTrackHeight (getHeight()) / std::clamp (trackCount(), 1, kMaxVisibleTracks), 18, 56);
}

int PlaylistTimeline::tracksViewHeight() const noexcept
{
    const int th = trackHeight();
    return std::max (th, std::min (trackCount() * th, availableTrackHeight (getHeight())));
}

int PlaylistTimeline::lanesTop() const noexcept
{
    return kRulerHeight + tracksViewHeight() + kLanesGap;
}

void PlaylistTimeline::clampScrollY()
{
    scrollY = std::clamp (scrollY, 0, std::max (0, trackCount() * trackHeight() - tracksViewHeight()));
}

void PlaylistTimeline::updateTrackScrollBar()
{
    const int content = trackCount() * trackHeight();
    const int view = tracksViewHeight();
    trackScrollBar.setVisible (content > view);
    trackScrollBar.setBounds (getWidth() - kTrackScrollWidth, kRulerHeight, kTrackScrollWidth, view);
    trackScrollBar.setRangeLimits (0.0, std::max (content, view));
    trackScrollBar.setCurrentRange (scrollY, view, juce::dontSendNotification);
}

void PlaylistTimeline::scrollToTrack (int track)
{
    const int th = trackHeight();
    const int top = track * th;
    if (top < scrollY)
        scrollY = top;
    else if (top + th > scrollY + tracksViewHeight())
        scrollY = top + th - tracksViewHeight();
    clampScrollY();
    updateTrackScrollBar();
    repaint();
}

void PlaylistTimeline::tracksChanged()
{
    armedTrack = std::clamp (armedTrack, 0, trackCount() - 1);
    removeTrackButton.setEnabled (arrangement.trackCount() > 1);
    addTrackButton.setEnabled (arrangement.trackCount() < kMaxPlaylistTracks);
    clampScrollY();
    updateTrackScrollBar();
    repaint();
}

void PlaylistTimeline::addTrack()
{
    if (arrangement.trackCount() >= kMaxPlaylistTracks)
        return;
    beginEdit();
    arrangement.addTrack();
    commitPendingUndo();
    tracksChanged();
    scrollToTrack (arrangement.trackCount() - 1);
    notifyChanged();
}

void PlaylistTimeline::removeTrack (int track)
{
    if (track < 0 || track >= arrangement.trackCount() || arrangement.trackCount() <= 1)
        return;
    const auto doRemove = [safe = juce::Component::SafePointer<PlaylistTimeline> (this), track]
    {
        if (safe == nullptr || track >= safe->arrangement.trackCount())
            return;
        safe->beginEdit();
        if (! safe->arrangement.removeTrack (track))
        {
            safe->pendingUndoValid = false;
            return;
        }
        safe->commitPendingUndo();
        safe->select (Kind::None, -1);
        safe->tracksChanged();
        if (safe->onTrackRemoved)
            safe->onTrackRemoved (track);
        safe->notifyChanged();
    };
    const int clipsThere = arrangement.clipsOnTrack (track);
    if (clipsThere == 0)
    {
        doRemove();
        return;
    }
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Delete " + trackName (track) + "?",
                                        "The track holds " + juce::String (clipsThere) + (clipsThere == 1 ? " clip, which is" : " clips, which are")
                                            + " deleted with it (Undo brings them back).",
                                        "Delete the track", "Cancel", this,
                                        juce::ModalCallbackFunction::create ([doRemove] (int result) { if (result != 0) doRemove(); }));
}

juce::Rectangle<int> PlaylistTimeline::automationClipRect (const AutomationClip& c) const noexcept
{
    const int x0 = tickToX (c.startTick);
    const int x1 = tickToX (c.endTick());
    return { x0, lanesTop() + c.lane * kLaneHeight, std::max (2, x1 - x0), kLaneHeight };
}

int PlaylistTimeline::yToTrack (int y) const noexcept
{
    return std::clamp ((y - kRulerHeight + scrollY) / trackHeight(), 0, trackCount() - 1);
}

juce::Rectangle<int> PlaylistTimeline::clipRect (const Clip& c) const noexcept
{
    const int x0 = tickToX (c.startTick);
    const int x1 = tickToX (c.endTick());
    return { x0, trackTop (c.track), std::max (2, x1 - x0), trackHeight() };
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

int PlaylistTimeline::floorToGrid (int tick) const noexcept
{
    const int step = effectiveSnap (false);
    return step <= 1 ? tick : (tick / step) * step;
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
    return { x0, trackTop (c.track), std::max (1, x1 - x0), trackHeight() };
}

double PlaylistTimeline::totalWidth() const noexcept
{
    return (arrangement.timelineTicks() / static_cast<double> (std::max (1, arrangement.patternTicks))) * unitWidth;
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

void PlaylistTimeline::scrollBarMoved (juce::ScrollBar* bar, double newRangeStart)
{
    if (bar == &trackScrollBar)
    {
        scrollY = static_cast<int> (std::lround (newRangeStart));
        clampScrollY();
        repaint();
        return;
    }
    scrollX = newRangeStart;
    clampScroll();
    repaint();
}

void PlaylistTimeline::setUnitWidth (double newUnitWidth, int anchorX)
{
    const double visible = std::max (1, getWidth() - kNameWidth);
    const double minWidth = std::max (6.0, visible / (arrangement.timelineTicks() / static_cast<double> (std::max (1, arrangement.patternTicks)))); // the whole timeline fits
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
    const double units = std::max (1.0, arrangement.songLengthTicks() / static_cast<double> (std::max (1, arrangement.patternTicks)));
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

void PlaylistTimeline::setRecordArms (const std::array<bool, kMaxPlaylistTracks>& arms)
{
    if (arms != recordArms)
    {
        recordArms = arms;
        repaint();
    }
}

void PlaylistTimeline::setSelectedTrack (int track)
{
    track = std::clamp (track, 0, trackCount() - 1);
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
        const double clipSeconds = ticksToSeconds (clip.lengthTicks, bpm);
        const auto fadeAt = [&] (double seconds) // the clip's fades at this point (0.27.0)
        {
            double f = 1.0;
            if (clip.fadeInSeconds > 0.0)
                f *= std::sin (0.5 * juce::MathConstants<double>::pi * std::clamp (seconds / clip.fadeInSeconds, 0.0, 1.0));
            if (clip.fadeOutSeconds > 0.0)
                f *= std::sin (0.5 * juce::MathConstants<double>::pi * std::clamp ((clipSeconds - seconds) / clip.fadeOutSeconds, 0.0, 1.0));
            return static_cast<float> (f);
        };
        for (int x = xFrom; x < xTo; ++x)
        {
            double f0 = firstFrame + (x - r.getX()) * framesPerPixel;
            double f1 = f0 + framesPerPixel;
            if (f0 >= frames)
                break;
            if (clip.reverse)
            {
                // the reversed audio: its position p is frame (frames - 1 - p) of the audio
                const double from = frames - 1 - f1, to = frames - 1 - f0;
                f0 = std::max (0.0, from);
                f1 = std::max (f0 + 1.0, to);
            }
            const float fade = fadeAt (ticksToSeconds ((x - r.getX()) / pxPerTick, bpm));

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
            g.drawVerticalLine (x, mid - std::clamp (hi * clip.gain * fade, 0.0f, 1.0f) * half - 0.5f,
                                mid - std::clamp (lo * clip.gain * fade, -1.0f, 0.0f) * half + 0.5f);
        }

        // The fade curves, from the bottom corner to the top.
        if (clip.fadeInSeconds > 0.0 || clip.fadeOutSeconds > 0.0)
        {
            juce::Path in, out;
            const float top = static_cast<float> (r.getY()) + 2.0f, bottom = static_cast<float> (r.getBottom()) - 2.0f;
            const auto curve = [&] (juce::Path& path, double fromSeconds, double toSeconds)
            {
                for (int k = 0; k <= 24; ++k)
                {
                    const double t = fromSeconds + (toSeconds - fromSeconds) * k / 24.0;
                    const auto px = static_cast<float> (r.getX() + secondsToTicks (t, bpm) * pxPerTick);
                    const float py = bottom - (bottom - top) * fadeAt (t);
                    if (k == 0)
                        path.startNewSubPath (px, py);
                    else
                        path.lineTo (px, py);
                }
            };
            if (clip.fadeInSeconds > 0.0)
                curve (in, 0.0, std::min (clip.fadeInSeconds, clipSeconds));
            if (clip.fadeOutSeconds > 0.0)
                curve (out, std::max (0.0, clipSeconds - clip.fadeOutSeconds), clipSeconds);
            g.setColour (juce::Colours::white.withAlpha (0.75f));
            g.strokePath (in, juce::PathStrokeType (1.2f));
            g.strokePath (out, juce::PathStrokeType (1.2f));
        }
        g.restoreState();
    }

    g.setColour (juce::Colours::white.withAlpha (0.92f));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    juce::StringArray tags;
    if (! clip.stretch.isNeutral())
        tags.add (describeStretch (clip.stretch));
    if (clip.reverse)
        tags.add ("reversed");
    if (std::abs (clip.pan) > 0.005f)
        tags.add ("pan " + juce::String (clip.pan < 0.0f ? "L" : "R") + juce::String (juce::roundToInt (std::abs (clip.pan) * 100.0f)));
    g.drawText (juce::String (clip.name) + (tags.isEmpty() ? juce::String() : "  [" + tags.joinIntoString (", ") + "]") + (missing ? "  (file not found)" : ""),
                r.reduced (5, 0).withHeight (14).translated (0, 1), juce::Justification::centredLeft, true);

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
    removeTrackButton.setBounds (6, 8, 38, kRulerHeight - 16);
    addTrackButton.setBounds (50, 8, 38, kRulerHeight - 16);
    clampScrollY();
    updateTrackScrollBar();
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
    lines (interfaceMeter().beatTicks(), kGridBeat.brighter (0.1f));
    lines (interfaceMeter().barTicks(), kGridBar);
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
    const int beatTicks = interfaceMeter().beatTicks(), barTicks = interfaceMeter().barTicks(); // the time signature (0.46.0)
    marks (beatTicks, 8, kTextDim);
    marks (barTicks, rowH - 2, kText);

    const double pxPerBar = barTicks * ppt;
    const int every = barLabelEvery (pxPerBar, 34.0);
    g.setColour (kText);
    for (int bar = (firstTick / barTicks / every) * every; bar * barTicks <= lastTick; bar += every)
        g.drawText (juce::String (bar + 1), tickToX (bar * barTicks) + 3, 0, 42, rowH, juce::Justification::centredLeft);

    if (beatTicks * ppt >= 44.0) // beat numbers "bar.beat" when there is room
    {
        g.setColour (kTextDim);
        g.setFont (juce::FontOptions (10.0f));
        for (int t = (firstTick / beatTicks) * beatTicks; t <= lastTick; t += beatTicks)
            if (t % barTicks != 0)
                g.drawText (juce::String (t / barTicks + 1) + "." + juce::String ((t % barTicks) / beatTicks + 1),
                            tickToX (t) + 3, 0, 36, rowH, juce::Justification::centredLeft);
    }

    // Lower row: time in minutes:seconds.milliseconds, with marks at a round step that fits the zoom; with tempo changes
    // (0.46.0) the seconds follow them.
    const auto& tempos = arrangement.tempoMarkers;
    const double ticksPerSecond = secondsToTicks (1.0, bpm);
    const double pxPerSecond = ticksPerSecond * ppt;
    const double step = niceTimeStepSeconds (pxPerSecond, 70.0);
    const double firstSecond = tickToSeconds (tempos, bpm, firstTick);
    const double lastSecond = tickToSeconds (tempos, bpm, lastTick);
    g.setFont (juce::FontOptions (10.0f));
    const auto firstIndex = static_cast<long long> (std::floor (firstSecond / step));
    const auto lastIndex = static_cast<long long> (std::ceil (lastSecond / step)) + 1;
    for (long long k = firstIndex; k <= lastIndex; ++k)
    {
        const double t = static_cast<double> (k) * step; // a whole number of steps: no drift from adding up
        const int x = tickToX (static_cast<int> (std::lround (secondsToTick (tempos, bpm, t))));
        g.setColour (kTextDim);
        g.drawVerticalLine (x, static_cast<float> (rowH + 1), static_cast<float> (kRulerHeight));
        g.setColour (kTextDim.brighter (0.2f));
        g.drawText (jstr (formatSeconds (t)), x + 3, rowH, 70, rowH, juce::Justification::centredLeft);
    }

    // Tempo changes (0.46.0): an orange flag with the new tempo.
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    for (const auto& m : tempos)
    {
        if (m.tick < firstTick || m.tick > lastTick)
            continue;
        const int x = tickToX (m.tick);
        g.setColour (juce::Colour (0xfff0a33a));
        g.fillRect (x, rowH, 2, rowH);
        const auto label = juce::String (m.bpm, std::abs (m.bpm - std::round (m.bpm)) < 0.05 ? 0 : 1) + " BPM";
        const auto box = juce::Rectangle<int> (x + 2, rowH + 1, 52, rowH - 2);
        g.fillRect (box);
        g.setColour (juce::Colours::black);
        g.drawText (label, box, juce::Justification::centred, false);
    }
}

// ------------------------------------------------------------------------- loop region (ruler)

int PlaylistTimeline::loopSnap (int tick, bool noSnap) const noexcept
{
    const int step = noSnap ? 1 : loopStep();
    const int snapped = static_cast<int> (std::lround (static_cast<double> (tick) / step)) * step;
    return std::clamp (snapped, 0, arrangement.timelineTicks());
}

bool PlaylistTimeline::onLoopRegion (int x) const noexcept
{
    return arrangement.hasLoop() && x >= tickToX (arrangement.loopStartTick) - kEdgeGrab / 2
           && x <= tickToX (arrangement.loopEndTick) + kEdgeGrab / 2;
}

int PlaylistTimeline::loopEdgeAt (int x) const noexcept
{
    if (! arrangement.hasLoop())
        return 0;
    const int a = tickToX (arrangement.loopStartTick), b = tickToX (arrangement.loopEndTick);
    if (b - a < 3 * kEdgeGrab) // too narrow to tell: only the end edge
        return std::abs (x - b) <= kEdgeGrab ? 1 : 0;
    if (std::abs (x - a) <= kEdgeGrab)
        return -1;
    if (std::abs (x - b) <= kEdgeGrab)
        return 1;
    return 0;
}

bool PlaylistTimeline::placeLoop (int start, int end)
{
    const int total = arrangement.timelineTicks();
    const int minLength = std::min (loopStep(), total);
    if (end < start)
        std::swap (start, end);
    start = std::clamp (start, 0, total - minLength);
    end = std::clamp (end, start + minLength, total);
    if (start == arrangement.loopStartTick && end == arrangement.loopEndTick)
        return false;
    arrangement.setLoop (start, end);
    repaint();
    if (onLoopChanged)
        onLoopChanged();
    return true;
}

void PlaylistTimeline::showLoopMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Loop region");
    menu.addItem (1, "Move");
    menu.addItem (2, "Resize");
    menu.addSeparator();
    menu.addItem (3, "Delete loop region");

    juce::Component::SafePointer<PlaylistTimeline> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe] (int choice)
    {
        if (safe == nullptr || ! safe->arrangement.hasLoop())
            return;
        auto& self = *safe;
        const int mouseTick = self.xToTick (self.getMouseXYRelative().x);
        if (choice == 1 || choice == 2)
        {
            self.loopOriginalStart = self.arrangement.loopStartTick;
            self.loopOriginalEnd = self.arrangement.loopEndTick;
            if (choice == 1)
            {
                self.loopMode = LoopMode::Move;
                self.loopGrab = std::clamp (mouseTick - self.arrangement.loopStartTick, 0, self.arrangement.loopEndTick - self.arrangement.loopStartTick);
                self.setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            }
            else
            {
                self.loopMode = LoopMode::Resize;
                const int middle = (self.arrangement.loopStartTick + self.arrangement.loopEndTick) / 2;
                self.loopResizeEdge = mouseTick < middle ? -1 : 1;
                self.setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
            }
            if (self.onReadout)
                self.onReadout (choice == 1 ? "Moving the loop region: click to place it (right click or Escape: cancel)"
                                            : "Resizing the loop region: click to finish (right click or Escape: cancel)", {});
        }
        else if (choice == 3)
        {
            self.arrangement.clearLoop();
            self.repaint();
            if (self.onLoopChanged)
                self.onLoopChanged();
        }
    });
}

void PlaylistTimeline::followLoopMode (int x)
{
    if (loopMode == LoopMode::None || ! arrangement.hasLoop())
        return;
    const int mouseTick = xToTick (x);
    const int length = arrangement.loopEndTick - arrangement.loopStartTick;
    if (loopMode == LoopMode::Move)
    {
        const int start = std::clamp (loopSnap (mouseTick - loopGrab, false), 0, arrangement.timelineTicks() - length);
        placeLoop (start, start + length);
    }
    else
    {
        const int edge = loopSnap (mouseTick, false);
        if (loopResizeEdge < 0)
            placeLoop (std::min (edge, arrangement.loopEndTick - loopStep()), arrangement.loopEndTick);
        else
            placeLoop (arrangement.loopStartTick, std::max (edge, arrangement.loopStartTick + loopStep()));
    }
}

void PlaylistTimeline::endLoopMode (bool keep)
{
    if (loopMode == LoopMode::None)
        return;
    loopMode = LoopMode::None;
    setMouseCursor (juce::MouseCursor::NormalCursor);
    if (! keep && loopOriginalStart >= 0)
        placeLoop (loopOriginalStart, loopOriginalEnd);
    loopOriginalStart = loopOriginalEnd = -1;
    updateReadout (lastMouseX, lastMouseY);
}

void PlaylistTimeline::paintLoopRegion (juce::Graphics& g)
{
    if (! arrangement.hasLoop())
        return;
    const int a = tickToX (arrangement.loopStartTick), b = tickToX (arrangement.loopEndTick);
    if (b < kNameWidth || a > getWidth())
        return;
    const auto band = juce::Rectangle<int> (a, 0, std::max (2, b - a), kRulerHeight / 2);
    const bool active = loopMode != LoopMode::None || drag == Drag::LoopCreate || drag == Drag::LoopMove
                        || drag == Drag::LoopResizeStart || drag == Drag::LoopResizeEnd;
    g.setColour (kLoop.withAlpha (active ? 0.75f : 0.55f));
    g.fillRect (band);
    g.setColour (kLoop);
    g.drawRect (band, 1);
    g.fillRect (a, 0, 2, kRulerHeight);
    g.fillRect (b - 2, 0, 2, kRulerHeight);
    // a faint band down the tracks, so the region can be seen against the clips
    g.setColour (kLoop.withAlpha (0.07f));
    g.fillRect (a, kRulerHeight, b - a, tracksViewHeight());
    g.setColour (kLoop.withAlpha (0.45f));
    g.drawVerticalLine (a, static_cast<float> (kRulerHeight), static_cast<float> (kRulerHeight + tracksViewHeight()));
    g.drawVerticalLine (b - 1, static_cast<float> (kRulerHeight), static_cast<float> (kRulerHeight + tracksViewHeight()));
    if (b - a > 44)
    {
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText ("LOOP", band.reduced (4, 0), juce::Justification::centredLeft, false);
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

        {
            juce::Graphics::ScopedSaveState rows (g);
            g.reduceClipRegion (kNameWidth, kRulerHeight, getWidth() - kNameWidth, tracksViewHeight());
            for (int t = 0; t < arrangement.trackCount(); ++t)
            {
                if (trackTop (t) + th < kRulerHeight || trackTop (t) > kRulerHeight + tracksViewHeight())
                    continue;
                auto row = t % 2 == 0 ? kTrackA : kTrackB;
                if (const auto colour = arrangement.playlistTracks[static_cast<size_t> (t)].colour; colour != 0)
                    row = row.interpolatedWith (juce::Colour (colour).withAlpha (1.0f), 0.14f); // a coloured track (0.33.0)
                g.setColour (row);
                g.fillRect (kNameWidth, trackTop (t), getWidth() - kNameWidth, th);
            }
        }

        for (int lane = 0; lane < kAutomationLanes; ++lane)
        {
            g.setColour (lane % 2 == 0 ? kLaneA : kLaneB);
            g.fillRect (kNameWidth, lanesTop() + lane * kLaneHeight, getWidth() - kNameWidth, kLaneHeight);
        }

        paintGrid (g);
        paintRuler (g);
        paintLoopRegion (g);

        // End of the song: song mode loops here.
        const int songTicks = arrangement.songLengthTicks();
        if (songTicks > 0)
        {
            g.setColour (kPlayhead.withAlpha (0.35f));
            g.drawVerticalLine (tickToX (songTicks), static_cast<float> (kRulerHeight), static_cast<float> (kRulerHeight + tracksViewHeight()));
        }

        // The tracks' clips, kept inside the tracks' area when the tracks scroll (0.44.0).
        g.saveState();
        g.reduceClipRegion (kNameWidth, kRulerHeight, getWidth() - kNameWidth, tracksViewHeight());
        // The notes' pitch range of each pattern, for the previews below (0.49.0); only worked out for the patterns of the clips in
        // sight, once per paint (0.51.0).
        pitchRanges.assign (arrangement.patterns.size(), { 129, -2 }); // (129, -2: not worked out yet)
        const auto pitchRangeOf = [this] (size_t p)
        {
            auto& range = pitchRanges[p];
            if (range.first == 129)
            {
                range = { 128, -1 };
                for (const auto& channelNotes : arrangement.patterns[p].notes)
                    for (const auto& n : channelNotes)
                    {
                        range.first = std::min (range.first, n.pitch);
                        range.second = std::max (range.second, n.pitch);
                    }
            }
            return range;
        };

        const auto visibleClip = g.getClipBounds();
        for (int i = 0; i < static_cast<int> (arrangement.clips.size()); ++i)
        {
            const auto& clip = arrangement.clips[static_cast<size_t> (i)];
            const auto r = clipRect (clip).reduced (0, 2).withTrimmedRight (1);
            if (! r.intersects (visibleClip))
                continue;
            const auto colour = colourForPattern (clip.pattern);
            const bool known = clip.pattern >= 0 && clip.pattern < static_cast<int> (arrangement.patterns.size());

            // 0.49.0: a see-through block with a title bar and a small piano roll of the pattern's notes, as in FL Studio.
            g.setColour (colour.withAlpha (0.2f));
            g.fillRoundedRectangle (r.toFloat(), 3.0f);
            // Tall rows get a title bar above the notes; short ones show the notes across the whole block, the name over them.
            const bool titleBar = r.getHeight() >= 30;
            const int titleHeight = titleBar ? 13 : 0;
            const auto title = titleBar ? r.withHeight (titleHeight) : r.withHeight (std::min (r.getHeight(), 14));
            if (titleBar)
            {
                g.setColour (colour.withAlpha (0.75f));
                g.fillRoundedRectangle (title.toFloat(), 3.0f);
                g.fillRect (title.withTrimmedTop (titleHeight / 2)); // square bottom corners where the notes begin
            }

            const auto notesArea = r.withTrimmedTop (titleHeight + 2).withTrimmedBottom (2).reduced (2, 0);
            if (known && notesArea.getHeight() >= 5 && notesArea.getWidth() >= 6)
            {
                const auto& pattern = arrangement.patterns[static_cast<size_t> (clip.pattern)];
                const auto [low, high] = pitchRangeOf (static_cast<size_t> (clip.pattern));
                if (high >= low)
                {
                    juce::Graphics::ScopedSaveState inside (g);
                    g.reduceClipRegion (notesArea);
                    const int length = std::max (1, arrangement.patternTicks);
                    const int offset = ((clip.offsetTicks % length) + length) % length;
                    const int rows = std::max (high - low + 1, 6); // a few notes do not fill the height with fat bars
                    const float rowHeight = static_cast<float> (notesArea.getHeight()) / static_cast<float> (rows);
                    const float barHeight = std::clamp (rowHeight * 0.8f, 1.5f, 5.0f);
                    const int bottomPad = (rows - (high - low + 1)) / 2;
                    g.setColour (colour.brighter (0.45f).withAlpha (0.95f));
                    // Each repeat of the pattern inside the clip, only those in view.
                    const int firstVisibleTick = std::max (clip.startTick, xToTick (visibleClip.getX()) - length);
                    const int lastVisibleTick = std::min (clip.endTick(), xToTick (visibleClip.getRight()) + 1);
                    for (int repeatStart = clip.startTick - offset; repeatStart < lastVisibleTick; repeatStart += length)
                    {
                        if (repeatStart + length <= firstVisibleTick)
                            continue;
                        for (const auto& channelNotes : pattern.notes)
                            for (const auto& n : channelNotes)
                            {
                                const int from = std::max (repeatStart + n.startTick, clip.startTick);
                                const int to = std::min ({ repeatStart + n.startTick + std::max (1, n.lengthTicks), repeatStart + length, clip.endTick() });
                                if (to <= from || n.startTick >= length)
                                    continue;
                                const float x0 = static_cast<float> (tickToX (from)), x1 = static_cast<float> (tickToX (to));
                                const float y = static_cast<float> (notesArea.getBottom())
                                              - (static_cast<float> (n.pitch - low + bottomPad) + 0.5f) * rowHeight - barHeight * 0.5f;
                                g.fillRect (x0, y, std::max (1.5f, x1 - x0 - 0.5f), barHeight);
                            }
                    }
                }
            }

            g.setColour (colour.withAlpha (0.9f)); // an outline, so clips laid over each other (0.33.0) stay apart
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.0f);

            // A mark where each repeat of the pattern begins.
            g.setColour (colour.withAlpha (0.6f));
            {
                const int length = std::max (1, arrangement.patternTicks);
                const int offset = ((clip.offsetTicks % length) + length) % length;
                for (int t = clip.startTick + (offset == 0 ? length : length - offset); t < clip.endTick(); t += length)
                    g.drawVerticalLine (tickToX (t), static_cast<float> (r.getY() + titleHeight), static_cast<float> (r.getBottom() - 2));
            }

            if (known)
            {
                const juce::String name (arrangement.patterns[static_cast<size_t> (clip.pattern)].name);
                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                if (titleBar)
                    g.setColour (juce::Colours::black.withAlpha (0.85f));
                else
                {
                    // A small dark label behind the name, so it reads over the notes.
                    const int w = std::min (title.getWidth() - 2, juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (11.0f, juce::Font::bold), name) + 8);
                    g.setColour (juce::Colours::black.withAlpha (0.45f));
                    g.fillRoundedRectangle (title.withWidth (w).reduced (1).toFloat(), 2.0f);
                    g.setColour (colour.brighter (0.7f));
                }
                g.drawText (name, title.reduced (5, 0), juce::Justification::centredLeft, true);
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

        // Drop preview (files or sounds dragged over the timeline).
        if (dropTrack >= 0)
        {
            const auto r = juce::Rectangle<int> (tickToX (dropTick), trackTop (dropTrack), std::max (6, tickToX (dropTick + 4 * kTicksPerBeat) - tickToX (dropTick)), th);
            g.setColour (kAudio.withAlpha (0.35f));
            g.fillRect (r);
            g.setColour (kAudio);
            g.drawRect (r, 2);
        }

        // Tool overlays.
        if (tool == PlaylistTool::Split && hoverTrack >= 0 && drag == Drag::None)
        {
            const bool onClip = arrangement.audioClipAt (hoverTrack, hoverTick) >= 0 || arrangement.clipAt (hoverTrack, hoverTick) >= 0;
            if (onClip)
            {
                const int tick = snapTick (hoverTick, false);
                g.setColour (kSelect);
                g.drawVerticalLine (tickToX (tick), static_cast<float> (trackTop (hoverTrack)), static_cast<float> (trackTop (hoverTrack) + th));
            }
        }
        if (drag == Drag::CutRange)
        {
            const int from = std::min (dragStartTick, dragEndTick), to = std::max (dragStartTick, dragEndTick);
            const auto r = juce::Rectangle<int> (tickToX (from), trackTop (dragTrack), std::max (2, tickToX (to) - tickToX (from)), th);
            g.setColour (kCut.withAlpha (0.35f));
            g.fillRect (r);
            g.setColour (kCut);
            g.drawRect (r, 2);
        }
        if (drag == Drag::ZoomRange)
        {
            const auto r = juce::Rectangle<int> (std::min (dragStartX, dragEndX), kRulerHeight, std::abs (dragEndX - dragStartX), tracksViewHeight());
            g.setColour (kSelect.withAlpha (0.12f));
            g.fillRect (r);
            g.setColour (kSelect.withAlpha (0.8f));
            g.drawRect (r, 1);
        }

        g.restoreState(); // back to the whole timeline: the automation lanes and the playhead

        for (int i = 0; i < static_cast<int> (arrangement.automationClips.size()); ++i)
            paintAutomationClip (g, arrangement.automationClips[static_cast<size_t> (i)], selection.kind == Kind::Automation && selection.index == i);

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
    g.saveState();
    g.reduceClipRegion (0, kRulerHeight, kNameWidth, tracksViewHeight());
    for (int t = 0; t < arrangement.trackCount(); ++t)
    {
        if (trackTop (t) + th < kRulerHeight || trackTop (t) > kRulerHeight + tracksViewHeight())
            continue;
        {
            // the tracks Record writes to: a red dot; the others a faint ring, to show where to click
            const juce::Rectangle<float> dot (static_cast<float> (kNameWidth - 14), static_cast<float> (trackTop (t)) + 0.5f * static_cast<float> (th) - 4.0f, 8.0f, 8.0f);
            if (recordArms[static_cast<size_t> (t)])
            {
                g.setColour (juce::Colour (0xffd9433b));
                g.fillEllipse (dot);
            }
            else
            {
                g.setColour (kText.withAlpha (0.25f));
                g.drawEllipse (dot, 1.0f);
            }
        }
        if (const auto colour = arrangement.playlistTracks[static_cast<size_t> (t)].colour; colour != 0)
        {
            g.setColour (juce::Colour (colour).withAlpha (1.0f)); // the track's colour (0.33.0)
            g.fillRect (0, trackTop (t) + 1, 4, th - 2);
        }
        const auto& own = arrangement.playlistTracks[static_cast<size_t> (t)];
        if (own.pattern >= 0 && own.colour == 0)
        {
            g.setColour (colourForPattern (own.pattern).withAlpha (0.8f)); // a track dedicated to a pattern: its colour (0.44.0)
            g.fillRect (0, trackTop (t) + 1, 4, th - 2);
        }
        g.setColour (t == armedTrack ? juce::Colours::white : kText);
        g.drawText (trackName (t), 8, trackTop (t), kNameWidth - 22, th, juce::Justification::centredLeft, true);
        g.setColour (kText.withAlpha (0.08f));
        g.drawHorizontalLine (trackTop (t) + th - 1, 0.0f, static_cast<float> (kNameWidth));
    }
    g.restoreState();

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

// --- playlist tracks' names and colours (0.33.0) --------------------------------------------------------------------------------

juce::String PlaylistTimeline::trackName (int track) const
{
    if (track < 0 || track >= arrangement.trackCount())
        return {};
    const auto& t = arrangement.playlistTracks[static_cast<size_t> (track)];
    if (! t.name.empty())
        return juce::String (t.name);
    if (t.pattern >= 0 && t.pattern < static_cast<int> (arrangement.patterns.size()))
        return juce::String (arrangement.patterns[static_cast<size_t> (t.pattern)].name); // a pattern's own track (0.44.0)
    return "Track " + juce::String (track + 1);
}

const std::vector<std::pair<const char*, std::uint32_t>>& PlaylistTimeline::trackColours()
{
    static const std::vector<std::pair<const char*, std::uint32_t>> colours {
        { "Red", 0xffe0524a },    { "Orange", 0xffe8943a }, { "Yellow", 0xffe3c84b }, { "Lime", 0xff9ccc4a },
        { "Green", 0xff4cc38a },  { "Teal", 0xff3fb7b0 },   { "Blue", 0xff4a90e2 },   { "Indigo", 0xff6c6ee0 },
        { "Purple", 0xffa66be0 }, { "Pink", 0xffe06bb5 },   { "Brown", 0xffa8774f },  { "Grey", 0xff9aa1ab },
    };
    return colours;
}

void PlaylistTimeline::showTrackMenu (int track)
{
    if (track < 0 || track >= arrangement.trackCount())
        return;
    juce::PopupMenu colours;
    const auto current = arrangement.playlistTracks[static_cast<size_t> (track)].colour;
    colours.addItem (100, "None", true, current == 0);
    const auto& list = trackColours();
    for (size_t i = 0; i < list.size(); ++i)
    {
        juce::PopupMenu::Item item (list[i].first);
        item.itemID = 101 + static_cast<int> (i);
        item.colour = juce::Colour (list[i].second);
        item.isTicked = current == list[i].second;
        colours.addItem (item);
    }
    juce::PopupMenu menu;
    menu.addSectionHeader (trackName (track));
    menu.addItem (1, "Rename...");
    menu.addSubMenu ("Colour", colours);
    {
        const auto& t = arrangement.playlistTracks[static_cast<size_t> (track)];
        menu.addItem (2, "Default name and colour", ! t.name.empty() || t.colour != 0);
    }
    menu.addSeparator();
    menu.addItem (3, "Record on this track", onTrackArmToggled != nullptr, recordArms[static_cast<size_t> (track)]);
    menu.addItem (4, "Recording input...", onTrackInputMenu != nullptr);
    menu.addSeparator();
    menu.addItem (5, "Add a track at the end", arrangement.trackCount() < kMaxPlaylistTracks);
    menu.addItem (6, "Delete this track", arrangement.trackCount() > 1);
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                        [safe = juce::Component::SafePointer<PlaylistTimeline> (this), track] (int choice)
                        {
                            if (safe == nullptr || choice == 0)
                                return;
                            if (choice == 1)
                                safe->renameTrack (track);
                            else if (choice == 3 && safe->onTrackArmToggled)
                                safe->onTrackArmToggled (track);
                            else if (choice == 4 && safe->onTrackInputMenu)
                                safe->onTrackInputMenu (track);
                            else if (choice == 5)
                                safe->addTrack();
                            else if (choice == 6)
                                safe->removeTrack (track);
                            else if (choice == 2)
                            {
                                safe->beginEdit();
                                auto& t = safe->arrangement.playlistTracks[static_cast<size_t> (track)];
                                t.name.clear(); // a pattern's track stays that pattern's
                                t.colour = 0;
                                safe->commitPendingUndo();
                                safe->notifyChanged();
                            }
                            else if (choice == 100)
                                safe->setTrackColour (track, 0);
                            else if (choice > 100 && choice - 101 < static_cast<int> (trackColours().size()))
                                safe->setTrackColour (track, trackColours()[static_cast<size_t> (choice - 101)].second);
                        });
}

void PlaylistTimeline::setTrackColour (int track, std::uint32_t colour)
{
    if (track < 0 || track >= arrangement.trackCount() || arrangement.playlistTracks[static_cast<size_t> (track)].colour == colour)
        return;
    beginEdit();
    arrangement.playlistTracks[static_cast<size_t> (track)].colour = colour;
    commitPendingUndo();
    notifyChanged();
}

void PlaylistTimeline::renameTrack (int track)
{
    if (track < 0 || track >= arrangement.trackCount())
        return;
    const auto& own = arrangement.playlistTracks[static_cast<size_t> (track)];
    const auto defaultName = own.pattern >= 0 && own.pattern < static_cast<int> (arrangement.patterns.size())
                                 ? "\"" + juce::String (arrangement.patterns[static_cast<size_t> (own.pattern)].name) + "\", its pattern's name"
                                 : "\"Track " + juce::String (track + 1) + "\"";
    auto* window = new juce::AlertWindow ("Rename track", "A name for the playlist track (empty: " + defaultName + ").",
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", juce::String (arrangement.playlistTracks[static_cast<size_t> (track)].name));
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create (
                                       [safe = juce::Component::SafePointer<PlaylistTimeline> (this), window, track] (int result)
                                       {
                                           if (safe == nullptr || result != 1)
                                               return;
                                           const auto name = window->getTextEditorContents ("name").trim().substring (0, 32).toStdString();
                                           auto& t = safe->arrangement.playlistTracks[static_cast<size_t> (track)];
                                           if (name == t.name)
                                               return;
                                           safe->beginEdit();
                                           t.name = name;
                                           safe->commitPendingUndo();
                                           safe->notifyChanged();
                                       }),
                             true);
    if (auto* editor = window->getTextEditor ("name"))
        editor->grabKeyboardFocus();
}

PlaylistTimeline::Snapshot PlaylistTimeline::takeSnapshot() const
{
    return { arrangement.clips, arrangement.audioClips, arrangement.automationClips, arrangement.playlistTracks };
}

void PlaylistTimeline::restore (const Snapshot& s)
{
    arrangement.clips = s.clips;
    arrangement.audioClips = s.audioClips;
    arrangement.automationClips = s.automationClips;
    arrangement.playlistTracks = s.tracks.empty() ? std::vector<PlaylistTrack> (1) : s.tracks;
    tracksChanged();
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
        const int start = c.startTick;
        const int length = c.lengthTicks;
        const double repeats = length / static_cast<double> (std::max (1, arrangement.patternTicks));
        const juce::String name = c.pattern >= 0 && c.pattern < static_cast<int> (arrangement.patterns.size())
                                      ? juce::String (arrangement.patterns[static_cast<size_t> (c.pattern)].name) : juce::String ("?");
        return name + "   start " + jstr (formatBarsBeatsTicks (start)) + " (" + jstr (formatSeconds (ticksToSeconds (start, bpm)))
             + ")   length " + jstr (formatLengthBarsBeatsTicks (length)) + " = " + juce::String (repeats, std::abs (repeats - std::round (repeats)) < 1.0e-9 ? 0 : 2)
             + (std::abs (repeats - 1.0) < 1.0e-9 ? " pattern" : " patterns")
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
    if (x >= kNameWidth && inTracks (y))
    {
        const int tick = xToTick (x);
        position = "Position " + jstr (formatBarsBeatsTicks (tick)) + "  =  " + jstr (formatSeconds (tickToSeconds (arrangement.tempoMarkers, bpm, tick)))
                 + "   " + trackName (yToTrack (y));
    }
    else if (x >= kNameWidth && inLanes (y))
    {
        const int tick = xToTick (x);
        position = "Position " + jstr (formatBarsBeatsTicks (tick)) + "  =  " + jstr (formatSeconds (tickToSeconds (arrangement.tempoMarkers, bpm, tick)))
                 + "   Automation lane " + juce::String (yToLane (y) + 1);
    }
    else if (x >= kNameWidth && y >= 0 && y < kRulerHeight)
    {
        const int tick = snapTick (xToTick (x), false);
        position = "Click: play from " + jstr (formatBarsBeatsTicks (tick)) + "   Right drag: loop region";
        if (onLoopRegion (x))
            position = "Loop " + jstr (formatBarsBeatsTicks (arrangement.loopStartTick)) + " to " + jstr (formatBarsBeatsTicks (arrangement.loopEndTick))
                     + "   Right click: Move / Resize / Delete, right drag: move (edge: resize)";
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
    const int pattern = arrangement.clipAt (track, tick);
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

    const int pattern = arrangement.clipAt (track, tick);
    if (pattern >= 0)
    {
        const int boundary = snapTick (tick, noSnap);
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
        c.track = std::clamp (track, 0, trackCount() - 1);
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
        c.track = std::clamp (track, 0, trackCount() - 1);
        c.startTick = std::clamp (tick, 0, std::max (0, arrangement.timelineTicks() - c.lengthTicks));
        if (! arrangement.canPlace (c))
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
        pasteAt (clipboard.pattern.track, clipboard.pattern.endTick());
    clipboard = saved;
}

void PlaylistTimeline::editAudioProperties (int index)
{
    if (index < 0 || index >= static_cast<int> (arrangement.audioClips.size()))
        return;

    const auto clip = arrangement.audioClips[static_cast<size_t> (index)];
    auto* window = new juce::AlertWindow ("Audio clip", juce::String (clip.name) + "\nStart, length and fades in seconds, gain in dB, pan from -100 (left) to 100 (right).\nTempo "
                                                          + juce::String (bpm, 1) + " BPM: one beat = " + juce::String (60.0 / bpm, 3) + " s.",
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("start", juce::String (ticksToSeconds (clip.startTick, bpm), 4), "Start (seconds)");
    window->addTextEditor ("length", juce::String (ticksToSeconds (clip.lengthTicks, bpm), 4), "Length (seconds)");
    window->addTextEditor ("gain", juce::String (juce::Decibels::gainToDecibels (clip.gain), 1), "Gain (dB, at most +6)");
    window->addTextEditor ("pan", juce::String (juce::roundToInt (clip.pan * 100.0f)), "Pan (-100 left, 0 centre, 100 right)");
    window->addTextEditor ("fadeIn", juce::String (clip.fadeInSeconds, 3), "Fade in (seconds, 0 = none)");
    window->addTextEditor ("fadeOut", juce::String (clip.fadeOutSeconds, 3), "Fade out (seconds, 0 = none)");
    window->addComboBox ("direction", { "Forwards", "Reversed (the same part of the audio, backwards)" }, "Direction");
    if (auto* direction = window->getComboBoxComponent ("direction"))
        direction->setSelectedItemIndex (clip.reverse ? 1 : 0, juce::dontSendNotification);
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
                                     const double panNumber = window->getTextEditorContents ("pan").getDoubleValue();
                                     edited.pan = std::isfinite (panNumber) ? static_cast<float> (std::clamp (panNumber, -100.0, 100.0) / 100.0) : 0.0f;
                                     edited.fadeInSeconds = window->getTextEditorContents ("fadeIn").getDoubleValue();
                                     edited.fadeOutSeconds = window->getTextEditorContents ("fadeOut").getDoubleValue();
                                     if (auto* direction = window->getComboBoxComponent ("direction"))
                                     {
                                         // turned around with the length it has now, then trimmed to what that audio allows
                                         const int wantedLength = edited.lengthTicks;
                                         edited.lengthTicks = clip.lengthTicks;
                                         setAudioReverse (edited, direction->getSelectedItemIndex() == 1, bpmNow);
                                         edited.lengthTicks = std::clamp (wantedLength, 1, std::max (1, edited.naturalLengthTicks (bpmNow)));
                                     }
                                     limitFades (edited, bpmNow);

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

// The clip plays the same part of its audio the other way round (0.27.0).
void PlaylistTimeline::toggleAudioReverse (int index)
{
    if (index < 0 || index >= static_cast<int> (arrangement.audioClips.size()))
        return;
    beginEdit();
    commitPendingUndo();
    auto& clip = arrangement.audioClips[static_cast<size_t> (index)];
    setAudioReverse (clip, ! clip.reverse, bpm);
    notifyChanged();
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

juce::String PlaylistTimeline::useEditedAudio (int index, const std::shared_ptr<const SampleData>& expected, std::shared_ptr<const SampleData> edited,
                                               const std::vector<double>& sliceSeconds)
{
    Arrangement trial = arrangement; // try it first: a refusal must leave the playlist and the undo history alone
    const auto error = sls::useEditedAudio (trial, index, expected, std::move (edited), sliceSeconds, bpm);
    if (error.empty())
    {
        beginEdit();
        commitPendingUndo();
        arrangement = std::move (trial);
        clearSelection();
        notifyChanged();
    }
    return juce::String (error);
}

juce::String PlaylistTimeline::replaceAudioClip (int index, const std::shared_ptr<const SampleData>& expected, const std::vector<AudioClip>& with)
{
    if (index < 0 || index >= static_cast<int> (arrangement.audioClips.size()) || arrangement.audioClips[static_cast<size_t> (index)].data != expected)
        return "The clip changed meanwhile: nothing was replaced.";
    if (arrangement.audioClips.size() - 1 + with.size() > static_cast<size_t> (kMaxAudioClips))
        return "There is no room for more audio clips.";
    beginEdit();
    commitPendingUndo();
    arrangement.audioClips.erase (arrangement.audioClips.begin() + index);
    for (const auto& c : with)
        arrangement.audioClips.push_back (c);
    clearSelection();
    notifyChanged();
    return {};
}

void PlaylistTimeline::showClipMenu (const juce::MouseEvent& e)
{
    const int track = yToTrack (e.y);
    const int tick = xToTick (e.x);
    const int audio = arrangement.audioClipAt (track, tick);
    const int pattern = audio >= 0 ? -1 : arrangement.clipAt (track, tick);
    if (audio < 0 && pattern < 0)
        return;

    select (audio >= 0 ? Kind::Audio : Kind::Pattern, audio >= 0 ? audio : pattern);

    juce::PopupMenu menu;
    menu.addItem (1, "Split here");
    menu.addItem (2, "Duplicate (Ctrl+D)");
    menu.addItem (3, "Copy (Ctrl+C)");
    if (audio >= 0)
    {
        const auto& clip = arrangement.audioClips[static_cast<size_t> (audio)];
        menu.addItem (4, "Position, gain, pan and fades...");
        menu.addItem (9, "Reverse", true, clip.reverse);
        menu.addItem (7, "Stretch and pitch...");
        menu.addItem (8, "Edit sample...");
        menu.addItem (10, "Edit pitch...", onEditPitch != nullptr);
        menu.addItem (11, "Split into stems...", onSplitStems != nullptr);
        // Loop recording (0.47.0): the passes of the loop region that the clip's file holds.
        if (const int takes = takeCount (clip); takes > 1)
        {
            juce::PopupMenu takeMenu;
            const int now = currentTake (clip);
            for (int k = 0; k < takes && k < 99; ++k)
                takeMenu.addItem (100 + k, "Take " + juce::String (k + 1), true, k == now);
            menu.addSubMenu ("Take (" + juce::String (now + 1) + " of " + juce::String (takes) + ")", takeMenu);
        }
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
                                case 9: safe->toggleAudioReverse (audio); break;
                                case 8: if (safe->onEditSample) safe->onEditSample (audio); break;
                                case 10: if (safe->onEditPitch) safe->onEditPitch (audio); break;
                                case 11: if (safe->onSplitStems) safe->onSplitStems (audio); break;
                                case 5:
                                    if (safe->selection.kind == Kind::Pattern && safe->selection.index >= 0
                                        && safe->selection.index < static_cast<int> (safe->arrangement.clips.size()) && safe->onPatternPicked)
                                        safe->onPatternPicked (safe->arrangement.clips[static_cast<size_t> (safe->selection.index)].pattern);
                                    break;
                                case 6: safe->deleteSelected(); break;
                                default:
                                    if (result >= 100 && result < 200 && audio >= 0 && audio < static_cast<int> (safe->arrangement.audioClips.size())
                                        && chooseTake (safe->arrangement.audioClips[static_cast<size_t> (audio)], result - 100))
                                    {
                                        safe->notifyChanged();
                                        safe->repaint();
                                    }
                                    break;
                            }
                        });
}

bool PlaylistTimeline::handleKey (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const auto code = key.getKeyCode();
    const bool command = mods.isCommandDown();

    if (loopMode != LoopMode::None && code == juce::KeyPress::escapeKey)
    {
        endLoopMode (false);
        return true;
    }

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
    lastMouseX = e.x;
    lastMouseY = e.y;
    if (loopMode != LoopMode::None)
    {
        followLoopMode (e.x);
        return;
    }
    updateReadout (e.x, e.y);

    if (e.x >= kNameWidth && e.y < kRulerHeight && tool != PlaylistTool::Zoom)
    {
        setHover (-1, 0);
        setMouseCursor (loopEdgeAt (e.x) != 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::IBeamCursor);
        return;
    }

    if (e.x >= kNameWidth && inTracks (e.y))
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
            const int pattern = arrangement.clipAt (hoverTrack, xToTick (e.x));
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

    if (loopMode != LoopMode::None)
    {
        endLoopMode (! e.mods.isPopupMenu()); // a left click places the region, a right click puts it back
        return;
    }

    // The ruler: left click moves the song position (while playing too), right button makes or edits the loop region.
    if (e.x >= kNameWidth && e.y < kRulerHeight && (tool != PlaylistTool::Zoom || e.mods.isPopupMenu()))
    {
        const bool noSnap = e.mods.isAltDown();
        if (e.mods.isPopupMenu())
        {
            pressX = e.x;
            if (onLoopRegion (e.x))
            {
                drag = Drag::LoopPress; // a click shows the menu; a drag moves the region or, from an edge, resizes it
                loopOriginalStart = arrangement.loopStartTick;
                loopOriginalEnd = arrangement.loopEndTick;
                loopGrab = xToTick (e.x) - arrangement.loopStartTick;
                loopResizeEdge = loopEdgeAt (e.x);
            }
            else
            {
                drag = Drag::LoopCreate;
                const int step = loopStep();
                loopAnchor = std::clamp ((xToTick (e.x) / step) * step, 0, arrangement.timelineTicks() - step);
                if (noSnap)
                    loopAnchor = std::clamp (xToTick (e.x), 0, arrangement.timelineTicks() - step);
                placeLoop (loopAnchor, loopAnchor + step);
            }
            return;
        }
        drag = Drag::Seek;
        const int tick = std::clamp (snapTick (xToTick (e.x), noSnap), 0, arrangement.timelineTicks() - 1);
        setPlayheadTick (tick);
        if (onSeek)
            onSeek (tick);
        return;
    }

    if (e.x < kNameWidth && inTracks (e.y) && e.mods.isPopupMenu())
    {
        showTrackMenu (yToTrack (e.y)); // rename, colour (0.33.0)
        return;
    }
    if (e.x < kNameWidth && inTracks (e.y))
    {
        const int track = yToTrack (e.y);
        if (track >= 0 && track < arrangement.trackCount() && (e.x >= kNameWidth - 20 || e.mods.isCommandDown()))
        {
            if (onTrackArmToggled)
                onTrackArmToggled (track); // the record dot (0.36.0)
            return;
        }
        setSelectedTrack (track);
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
    if (e.x < kNameWidth || e.y < kRulerHeight || ! inTracks (e.y))
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
    const bool noSnap = e.mods.isAltDown();
    const int hit = arrangement.clipAt (track, tick);
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
            dragStartTick = dragEndTick = snapTick (tick, noSnap);
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
        // A new clip: one pattern long, on the grid square clicked (anywhere with Alt) - not on whole pattern lengths (0.42.0).
        const int at = std::clamp (noSnap ? tick : floorToGrid (tick), 0, std::max (0, arrangement.timelineTicks() - arrangement.patternTicks));
        // On a track dedicated to a pattern that pattern goes there (0.44.0); on a free track, the one selected in the pattern box.
        const int ownPattern = track >= 0 && track < arrangement.trackCount() ? arrangement.playlistTracks[static_cast<size_t> (track)].pattern : -1;
        const Clip c { ownPattern >= 0 && ownPattern < static_cast<int> (arrangement.patterns.size()) ? ownPattern : selectedPattern, track, at,
                       arrangement.patternTicks, 0 };
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
        grabOffsetTicks = tick - arrangement.clips[static_cast<size_t> (hit)].startTick;
    }
}

template <class Clip>
void PlaylistTimeline::storeDragged (Clip& stored, const Clip& clip, const juce::MouseEvent& e)
{
    commitPendingUndo();
    stored = clip;
    dragChanged = true;
    repaint();
    updateReadout (e.x, e.y);
    if (onChanged)
        onChanged();
}

void PlaylistTimeline::mouseDrag (const juce::MouseEvent& e)
{
    lastMouseX = e.x;
    lastMouseY = e.y;
    const bool noSnap = e.mods.isAltDown();

    if (drag == Drag::Seek)
    {
        const int tick = std::clamp (snapTick (xToTick (e.x), noSnap), 0, arrangement.timelineTicks() - 1);
        if (tick != playheadTick)
        {
            setPlayheadTick (tick);
            if (onSeek)
                onSeek (tick);
        }
        return;
    }
    if (drag == Drag::LoopCreate)
    {
        const int mouse = loopSnap (xToTick (e.x), noSnap);
        const int step = loopStep();
        if (mouse >= loopAnchor)
            placeLoop (loopAnchor, std::max (mouse, loopAnchor + step));
        else
            placeLoop (mouse, loopAnchor + step);
        return;
    }
    if (drag == Drag::LoopPress && std::abs (e.x - pressX) > 4 && arrangement.hasLoop())
        drag = loopResizeEdge < 0 ? Drag::LoopResizeStart : loopResizeEdge > 0 ? Drag::LoopResizeEnd : Drag::LoopMove;
    if (drag == Drag::LoopMove || drag == Drag::LoopResizeStart || drag == Drag::LoopResizeEnd)
    {
        const int mouseTick = xToTick (e.x);
        const int length = loopOriginalEnd - loopOriginalStart;
        if (drag == Drag::LoopMove)
        {
            const int start = std::clamp (loopSnap (mouseTick - loopGrab, noSnap), 0, arrangement.timelineTicks() - length);
            placeLoop (start, start + length);
        }
        else if (drag == Drag::LoopResizeStart)
            placeLoop (std::min (loopSnap (mouseTick, noSnap), loopOriginalEnd - loopStep()), loopOriginalEnd);
        else
            placeLoop (loopOriginalStart, std::max (loopSnap (mouseTick, noSnap), loopOriginalStart + loopStep()));
        return;
    }

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
        dragEndTick = snapTick (xToTick (e.x), noSnap); // audio and pattern clips alike, since 0.42.0
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
            storeDragged (stored, clip, e);
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
            storeDragged (stored, clip, e);
        }
        return;
    }

    if ((drag != Drag::MovePattern && drag != Drag::ResizePattern) || dragClip < 0 || dragClip >= static_cast<int> (arrangement.clips.size()))
        return;

    auto clip = arrangement.clips[static_cast<size_t> (dragClip)];

    // Pattern clips move and resize on the playlist's grid (Alt: freely), like audio clips (0.42.0).
    const int mouseTick = xToTick (e.x);
    if (drag == Drag::MovePattern)
    {
        clip.startTick = std::clamp (snapTick (mouseTick - grabOffsetTicks, noSnap), 0, std::max (0, arrangement.timelineTicks() - clip.lengthTicks));
        clip.track = yToTrack (e.y);
    }
    else
    {
        const int step = std::max (1, effectiveSnap (noSnap));
        clip.lengthTicks = std::clamp (snapTick (mouseTick, noSnap) - clip.startTick, step, arrangement.timelineTicks() - clip.startTick);
    }

    auto& stored = arrangement.clips[static_cast<size_t> (dragClip)];
    if (! (clip.startTick == stored.startTick && clip.track == stored.track && clip.lengthTicks == stored.lengthTicks)
        && arrangement.canPlace (clip, dragClip))
    {
        storeDragged (stored, clip, e);
    }
}

void PlaylistTimeline::mouseUp (const juce::MouseEvent& e)
{
    if (drag == Drag::LoopPress)
    {
        drag = Drag::None;
        showLoopMenu();
        return;
    }
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
                result = cutPatternRange (arrangement, dragClip, from, to);
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
    if (e.x < kNameWidth && inTracks (e.y) && ! e.mods.isPopupMenu())
    {
        renameTrack (yToTrack (e.y)); // a double click on a track's name renames it (0.33.0)
        return;
    }
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

    const int hit = arrangement.clipAt (track, xToTick (e.x));
    if (hit >= 0 && onPatternPicked)
        onPatternPicked (arrangement.clips[static_cast<size_t> (hit)].pattern);
}

void PlaylistTimeline::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // Over the track names, or with Alt anywhere, the wheel scrolls the tracks up and down (0.44.0).
    if ((e.x < kNameWidth && inTracks (e.y)) || e.mods.isAltDown())
    {
        scrollY -= static_cast<int> (std::lround (wheel.deltaY * 3.0f * static_cast<float> (trackHeight())));
        clampScrollY();
        updateTrackScrollBar();
        repaint();
        return;
    }
    // The wheel zooms in (up) and out (down) around the mouse, anywhere on the playlist (0.20.0). Shift+wheel, or a sideways
    // wheel / touchpad swipe, scrolls along the song.
    const bool sideways = std::abs (wheel.deltaX) > std::abs (wheel.deltaY);
    if (! sideways && ! e.mods.isShiftDown())
    {
        zoomBy (std::pow (1.25, static_cast<double> (wheel.deltaY) * 4.0), e.x);
        return;
    }

    const float delta = sideways ? wheel.deltaX : wheel.deltaY;
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
