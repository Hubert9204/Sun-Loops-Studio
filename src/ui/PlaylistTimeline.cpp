#include "ui/PlaylistTimeline.h"
#include "ui/Icons.h"
#include "ui/TextPrompt.h"

#include <limits>

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
const juce::Colour kSectionBar { 0xff1d2026 }; // 0.69.0: the sections' bars and the borders between them
const juce::Colour kSplitter { 0xff15171b };

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
    // 0.69.0: each section scrolls on its own and has its own - / + in its bar.
    for (int s = 0; s < kSectionCount; ++s)
    {
        auto& bar = sectionScrollBar (s);
        bar.addListener (this);
        bar.setAutoHide (false);
        addChildComponent (bar);
    }
    static const char* const what[] = { "audio track", "pattern track", "automation lane" };
    for (int s = 0; s < kSectionCount; ++s)
    {
        auto& remove = removeButtons[static_cast<size_t> (s)];
        auto& add = addButtons[static_cast<size_t> (s)];
        remove.setButtonText ("-");
        add.setButtonText ("+");
        remove.setTooltip (s == kPatternSection
                               ? juce::String ("Delete a pattern with its track (the last one with no clips and no notes, else the last one; after asking)")
                               : juce::String ("Remove the last empty ") + what[s] + " of this section (when every one holds clips, the last one, after asking)");
        add.setTooltip (s == kPatternSection ? juce::String ("Add a new empty pattern, with its own track at the end of this section")
                                             : juce::String ("Add an ") + what[s] + " at the end of this section");
        remove.onClick = [this, s] { removeFromSection (s); };
        add.onClick = [this, s] { addToSection (s); };
        for (auto* b : { &remove, &add })
        {
            b->setMouseClickGrabsKeyboardFocus (false);
            b->setWantsKeyboardFocus (false);
            b->setConnectedEdges (b == &remove ? juce::Button::ConnectedOnRight : juce::Button::ConnectedOnLeft);
            addAndMakeVisible (b);
        }
    }
    unitWidth = 120.0;
}

PlaylistTimeline::~PlaylistTimeline()
{
    scrollBar.removeListener (this);
    for (int s = 0; s < kSectionCount; ++s)
        sectionScrollBar (s).removeListener (this);
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
    return unitWidth / kPlaylistUnitTicks;
}

// ------------------------------------------------------------------------- the three sections (0.69.0)

juce::ScrollBar& PlaylistTimeline::sectionScrollBar (int section) noexcept
{
    return section == kAudioSection ? audioScrollBar : section == kPatternSection ? patternScrollBar : laneScrollBar;
}

// The bodies' heights: the room left by the ruler, the bars, the borders and the scroll bar, shared in the proportions of
// sectionShares; none is lower than kMinSectionBody while there is room for that.
std::array<int, 3> PlaylistTimeline::bodyHeights() const noexcept
{
    const int total = std::max (0, getHeight() - kRulerHeight - kScrollBarHeight - kSectionCount * kSectionBarHeight - (kSectionCount - 1) * kSplitterHeight);
    std::array<int, 3> h {};
    double shares = 0.0;
    for (const double share : sectionShares)
        shares += std::max (0.0, share);
    if (shares <= 0.0)
        shares = 1.0;
    h[0] = static_cast<int> (std::lround (total * std::max (0.0, sectionShares[0]) / shares));
    h[1] = static_cast<int> (std::lround (total * std::max (0.0, sectionShares[1]) / shares));
    h[0] = std::clamp (h[0], 0, total);
    h[1] = std::clamp (h[1], 0, total - h[0]);
    h[2] = total - h[0] - h[1];
    const int minimum = std::min (kMinSectionBody, total / kSectionCount);
    for (size_t i = 0; i < h.size(); ++i)
    {
        int need = minimum - h[i];
        while (need > 0)
        {
            size_t richest = i == 0 ? 1 : 0;
            for (size_t j = 0; j < h.size(); ++j)
                if (j != i && h[j] > h[richest])
                    richest = j;
            const int take = std::min (need, h[richest] - minimum);
            if (take <= 0)
                break;
            h[richest] -= take;
            h[i] += take;
            need -= take;
        }
    }
    return h;
}

PlaylistTimeline::SectionArea PlaylistTimeline::sectionArea (int section) const noexcept
{
    const auto h = bodyHeights();
    int y = kRulerHeight;
    for (int s = 0; s < section; ++s)
        y += kSectionBarHeight + h[static_cast<size_t> (s)] + kSplitterHeight;
    SectionArea a;
    a.barTop = y;
    a.bodyTop = y + kSectionBarHeight;
    a.bodyHeight = h[static_cast<size_t> (std::clamp (section, 0, kSectionCount - 1))];
    return a;
}

juce::Rectangle<int> PlaylistTimeline::sectionBody (int section, bool timelineOnly) const noexcept
{
    const auto a = sectionArea (section);
    const int x = timelineOnly ? kNameWidth : 0;
    return { x, a.bodyTop, std::max (0, getWidth() - x), a.bodyHeight };
}

int PlaylistTimeline::splitterTop (int splitter) const noexcept
{
    return sectionArea (splitter).bodyBottom();
}

int PlaylistTimeline::splitterAt (int y) const noexcept
{
    for (int k = 0; k < kSectionCount - 1; ++k)
    {
        const int top = splitterTop (k);
        if (y >= top - 1 && y < top + kSplitterHeight + 1) // a pixel of slack on both sides
            return k;
    }
    return -1;
}

int PlaylistTimeline::sectionAt (int y) const noexcept
{
    for (int s = 0; s < kSectionCount; ++s)
    {
        const auto a = sectionArea (s);
        if (y >= a.bodyTop && y < a.bodyBottom())
            return s;
    }
    return -1;
}

int PlaylistTimeline::sectionRows (int section) const noexcept
{
    if (section == kAutomationSection)
        return arrangement.automationLanes;
    int rows = 0;
    for (int t = sectionFirstTrack (section); t < sectionEndTrack (section); ++t)
        rows += arrangement.trackHidden (t) ? 0 : 1;
    return rows;
}

// 0.62.0: rows and tracks differ once a group is folded. 0.69.0: rows count inside the track's own section.
int PlaylistTimeline::rowOf (int track) const noexcept
{
    int row = 0;
    for (int t = sectionFirstTrack (sectionOfTrack (track)); t < track && t < arrangement.trackCount(); ++t)
        row += arrangement.trackHidden (t) ? 0 : 1;
    return row;
}

// 0.72.0: how far down its section a track starts (the tracks above it have their own heights)
int PlaylistTimeline::rowOffset (int track) const noexcept
{
    int y = 0;
    for (int t = sectionFirstTrack (sectionOfTrack (track)); t < track && t < arrangement.trackCount(); ++t)
        y += arrangement.trackHidden (t) ? 0 : trackHeight (t);
    return y;
}

int PlaylistTimeline::trackHeight (int track) const noexcept
{
    static constexpr int kHeights[] = { 22, kRowHeight, 58, 86 }; // small, normal, large, extra large
    if (track < 0 || track >= arrangement.trackCount())
        return kRowHeight;
    return kHeights[std::min<size_t> (arrangement.playlistTracks[static_cast<size_t> (track)].height, 3)];
}

int PlaylistTimeline::trackAtRow (int section, int row) const noexcept
{
    int last = -1;
    for (int t = sectionFirstTrack (section); t < sectionEndTrack (section); ++t)
    {
        if (arrangement.trackHidden (t))
            continue;
        last = t;
        if (row-- <= 0)
            return t;
    }
    return last;
}

int PlaylistTimeline::trackTop (int track) const noexcept
{
    constexpr int offScreen = -100000;
    if (track < 0 || track >= arrangement.trackCount() || arrangement.trackHidden (track))
        return offScreen;
    const int section = sectionOfTrack (track);
    const auto a = sectionArea (section);
    const int y = a.bodyTop + rowOffset (track) - sectionScroll[static_cast<size_t> (section)];
    return y + trackHeight (track) <= a.bodyTop || y >= a.bodyBottom() ? offScreen : y;
}

bool PlaylistTimeline::inTracks (int y) const noexcept
{
    const int section = sectionAt (y);
    if (section != kAudioSection && section != kPatternSection)
        return false;
    const auto a = sectionArea (section);
    return y - a.bodyTop + sectionScroll[static_cast<size_t> (section)] < sectionContentHeight (section);
}

bool PlaylistTimeline::inLanes (int y) const noexcept
{
    if (sectionAt (y) != kAutomationSection)
        return false;
    const auto a = sectionArea (kAutomationSection);
    return (y - a.bodyTop + sectionScroll[kAutomationSection]) / kLaneHeight < arrangement.automationLanes;
}

int PlaylistTimeline::yToLane (int y) const noexcept
{
    const auto a = sectionArea (kAutomationSection);
    const int inside = std::clamp (y, a.bodyTop, std::max (a.bodyTop, a.bodyBottom() - 1));
    return std::clamp ((inside - a.bodyTop + sectionScroll[kAutomationSection]) / kLaneHeight, 0, std::max (0, arrangement.automationLanes - 1));
}

juce::Rectangle<int> PlaylistTimeline::foldRect (int track) const noexcept
{
    return { 5, trackTop (track) + (trackHeight (track) - 12) / 2, 12, 12 };
}

void PlaylistTimeline::toggleTrackFlag (int track, int which)
{
    if (track < 0 || track >= arrangement.trackCount())
        return;
    beginEdit();
    auto& t = arrangement.playlistTracks[static_cast<size_t> (track)];
    if (which == 0)
        t.locked = ! t.locked;
    else if (which == 1 && track > 0 && sectionOfTrack (track - 1) == sectionOfTrack (track)) // 0.69.0: inside a section
        t.grouped = ! t.grouped;
    else if (which == 2)
        t.collapsed = ! t.collapsed;
    commitPendingUndo();
    clampScrollY();
    updateTrackScrollBar();
    notifyChanged();
    repaint();
}

int PlaylistTimeline::lanesTop() const noexcept
{
    return sectionArea (kAutomationSection).bodyTop - sectionScroll[kAutomationSection];
}

void PlaylistTimeline::clampScrollY()
{
    for (int s = 0; s < kSectionCount; ++s)
        sectionScroll[static_cast<size_t> (s)] = std::clamp (sectionScroll[static_cast<size_t> (s)], 0,
                                                             std::max (0, sectionContentHeight (s) - sectionArea (s).bodyHeight));
}

void PlaylistTimeline::updateTrackScrollBar()
{
    for (int s = 0; s < kSectionCount; ++s)
    {
        const auto a = sectionArea (s);
        const int content = sectionContentHeight (s);
        auto& bar = sectionScrollBar (s);
        bar.setVisible (content > a.bodyHeight && a.bodyHeight > 0);
        bar.setBounds (getWidth() - kTrackScrollWidth, a.bodyTop, kTrackScrollWidth, a.bodyHeight);
        bar.setRangeLimits (0.0, std::max (content, a.bodyHeight));
        bar.setCurrentRange (sectionScroll[static_cast<size_t> (s)], a.bodyHeight, juce::dontSendNotification);
    }
}

void PlaylistTimeline::layoutSections()
{
    for (int s = 0; s < kSectionCount; ++s)
    {
        const int y = sectionArea (s).barTop + 2;
        removeButtons[static_cast<size_t> (s)].setBounds (4, y, 20, kSectionBarHeight - 4);
        addButtons[static_cast<size_t> (s)].setBounds (24, y, 20, kSectionBarHeight - 4);
    }
    clampScrollY();
    updateTrackScrollBar();
}

void PlaylistTimeline::updateSectionButtons()
{
    for (int s = 0; s < kSectionCount; ++s)
    {
        const bool lanes = s == kAutomationSection;
        const int count = lanes ? arrangement.automationLanes : sectionEndTrack (s) - sectionFirstTrack (s);
        removeButtons[static_cast<size_t> (s)].setEnabled (count > 1 && (lanes || arrangement.trackCount() > 1));
        addButtons[static_cast<size_t> (s)].setEnabled (lanes                  ? arrangement.automationLanes < kMaxAutomationLanes
                                                        : s == kPatternSection ? arrangement.canAddPattern() && onAddPattern != nullptr // 0.79.0
                                                                               : arrangement.trackCount() < kMaxPlaylistTracks);
    }
}

void PlaylistTimeline::scrollToTrack (int track)
{
    if (track < 0 || track >= arrangement.trackCount())
        return;
    const int section = sectionOfTrack (track);
    auto& scroll = sectionScroll[static_cast<size_t> (section)];
    const int view = sectionArea (section).bodyHeight;
    const int top = rowOffset (track);
    if (top < scroll)
        scroll = top;
    else if (top + trackHeight (track) > scroll + view)
        scroll = top + trackHeight (track) - view;
    clampScrollY();
    updateTrackScrollBar();
    repaint();
}

void PlaylistTimeline::scrollToLane (int lane)
{
    auto& scroll = sectionScroll[kAutomationSection];
    const int view = sectionArea (kAutomationSection).bodyHeight;
    const int top = lane * kLaneHeight;
    if (top < scroll)
        scroll = top;
    else if (top + kLaneHeight > scroll + view)
        scroll = top + kLaneHeight - view;
    clampScrollY();
    updateTrackScrollBar();
    repaint();
}

void PlaylistTimeline::tracksChanged()
{
    armedTrack = std::clamp (armedTrack, 0, trackCount() - 1);
    updateSectionButtons();
    clampScrollY();
    updateTrackScrollBar();
    repaint();
}

void PlaylistTimeline::addTrack()
{
    addToSection (kAudioSection);
}

void PlaylistTimeline::addToSection (int section)
{
    if (section == kAutomationSection)
    {
        beginEdit();
        if (! arrangement.addAutomationLane())
        {
            pendingUndoValid = false;
            return;
        }
        commitPendingUndo();
        tracksChanged();
        scrollToLane (arrangement.automationLanes - 1);
        notifyChanged();
        return;
    }
    if (section == kPatternSection) // 0.79.0: a pattern track comes with its pattern
    {
        if (onAddPattern != nullptr && arrangement.canAddPattern())
            onAddPattern();
        return;
    }
    if (arrangement.trackCount() >= kMaxPlaylistTracks)
        return;
    beginEdit();
    const int track = arrangement.insertTrack (trackSectionOf (section));
    if (track < 0)
    {
        pendingUndoValid = false;
        return;
    }
    commitPendingUndo();
    // the clips' indices did not change, only their tracks: the selection stays; the selected track follows its track
    if (armedTrack >= track && armedTrack + 1 < arrangement.trackCount())
        ++armedTrack;
    for (int t = kMaxPlaylistTracks - 1; t > track; --t)
        recordArms[static_cast<size_t> (t)] = recordArms[static_cast<size_t> (t - 1)];
    recordArms[static_cast<size_t> (track)] = false;
    tracksChanged();
    scrollToTrack (track);
    if (onTrackInserted)
        onTrackInserted (track);
    notifyChanged();
}

void PlaylistTimeline::removeFromSection (int section)
{
    if (section == kAutomationSection)
    {
        if (arrangement.automationLanes <= 1)
            return;
        int lane = arrangement.automationLanes - 1;
        for (int l = arrangement.automationLanes - 1; l >= 0; --l)
            if (arrangement.clipsOnLane (l) == 0)
            {
                lane = l;
                break;
            }
        removeLane (lane);
        return;
    }
    const int first = sectionFirstTrack (section), end = sectionEndTrack (section);
    if (end - first <= 1 || arrangement.trackCount() <= 1)
        return;
    int track = end - 1;
    const auto patternIsEmpty = [this] (int t) // 0.79.0: a pattern track goes with its pattern: one with no notes is picked first
    {
        const int p = arrangement.playlistTracks[static_cast<size_t> (t)].pattern;
        if (p < 0 || p >= static_cast<int> (arrangement.patterns.size()))
            return true;
        const auto& notes = arrangement.patterns[static_cast<size_t> (p)].notes;
        return std::all_of (notes.begin(), notes.end(), [] (const auto& list) { return list.empty(); })
            && std::none_of (arrangement.clips.begin(), arrangement.clips.end(), [p] (const Clip& c) { return c.pattern == p; });
    };
    for (int t = end - 1; t >= first; --t)
        if (arrangement.clipsOnTrack (t) == 0 && patternIsEmpty (t))
        {
            track = t;
            break;
        }
    removeTrack (track);
}

void PlaylistTimeline::removeLane (int lane)
{
    if (lane < 0 || lane >= arrangement.automationLanes || arrangement.automationLanes <= 1)
        return;
    const auto doRemove = [safe = juce::Component::SafePointer<PlaylistTimeline> (this), lane]
    {
        if (safe == nullptr)
            return;
        safe->beginEdit();
        if (! safe->arrangement.removeAutomationLane (lane))
        {
            safe->pendingUndoValid = false;
            return;
        }
        safe->commitPendingUndo();
        safe->clearSelection(); // the automation clips' indices changed
        safe->tracksChanged();
        safe->notifyChanged();
    };
    const int clipsThere = arrangement.clipsOnLane (lane);
    if (clipsThere == 0)
    {
        doRemove();
        return;
    }
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Delete automation lane " + juce::String (lane + 1) + "?",
                                        "The lane holds " + juce::String (clipsThere) + (clipsThere == 1 ? " automation clip, which is" : " automation clips, which are")
                                            + " deleted with it (Undo brings them back).",
                                        "Delete the lane", "Cancel", this,
                                        juce::ModalCallbackFunction::create ([doRemove] (int result) { if (result != 0) doRemove(); }));
}

void PlaylistTimeline::removeTrack (int track)
{
    if (track < 0 || track >= arrangement.trackCount() || arrangement.trackCount() <= 1)
        return;
    // 0.79.0: a pattern's own track is deleted with its pattern (the program asks first and keeps everything in step)
    if (const int p = arrangement.playlistTracks[static_cast<size_t> (track)].pattern; p >= 0 && p < static_cast<int> (arrangement.patterns.size()))
    {
        if (onDeletePattern != nullptr)
            onDeletePattern (p);
        return;
    }
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

// Clamped to the tracks: y in a track section's body picks the row there; elsewhere the nearer track section's nearest row.
int PlaylistTimeline::yToTrack (int y) const noexcept
{
    const auto audio = sectionArea (kAudioSection), patterns = sectionArea (kPatternSection);
    int section = y < (audio.bodyBottom() + patterns.bodyTop) / 2 ? kAudioSection : kPatternSection;
    if (sectionRows (section) == 0)
        section = section == kAudioSection ? kPatternSection : kAudioSection;
    const int rows = sectionRows (section);
    if (rows == 0)
        return 0;
    const auto a = sectionArea (section);
    const int inside = std::clamp (y, a.bodyTop, std::max (a.bodyTop, a.bodyBottom() - 1));
    // 0.72.0: the tracks have their own heights: the one whose rows reach past the point
    const int offset = inside - a.bodyTop + sectionScroll[static_cast<size_t> (section)];
    int last = -1;
    for (int t = sectionFirstTrack (section); t < sectionEndTrack (section); ++t)
    {
        if (arrangement.trackHidden (t))
            continue;
        last = t;
        if (offset < rowOffset (t) + trackHeight (t))
            return std::clamp (t, 0, trackCount() - 1);
    }
    return std::clamp (last, 0, trackCount() - 1);
}

juce::Rectangle<int> PlaylistTimeline::clipRect (const Clip& c) const noexcept
{
    const int x0 = tickToX (c.startTick);
    const int x1 = tickToX (c.endTick());
    return { x0, trackTop (c.track), std::max (2, x1 - x0), trackHeight (c.track) };
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
    if (arrangement.meterMarkers.empty())
        return static_cast<int> (std::lround (static_cast<double> (tick) / step)) * step;
    // 0.83.0: with time signature changes the grid starts again at each change, and "bar" means the bar where the tick is
    const auto seg = meterSegmentAt (arrangement.meterMarkers, interfaceMeter(), std::max (0, tick));
    const int unit = snapBars == 1 ? seg.meter.barTicks() : snapBars == 2 ? std::max (1, seg.meter.barTicks() / 2) : step;
    const long long inside = static_cast<long long> (std::llround (static_cast<double> (tick - seg.startTick) / unit)) * unit;
    long long snapped = seg.startTick + inside;
    if (seg.endTick != INT_MAX && snapped > seg.endTick)
        snapped = seg.endTick; // (the next change is a bar line too)
    if (seg.startTick > 0 && tick - seg.startTick < unit) // (the bar line before a change belongs to the segment before)
    {
        const auto prev = meterSegmentAt (arrangement.meterMarkers, interfaceMeter(), seg.startTick - 1);
        const int prevUnit = snapBars == 1 ? prev.meter.barTicks() : snapBars == 2 ? std::max (1, prev.meter.barTicks() / 2) : step;
        const long long candidate = seg.startTick - prevUnit;
        if (std::llabs (candidate - tick) < std::llabs (snapped - tick) && candidate >= prev.startTick)
            snapped = candidate;
    }
    return static_cast<int> (snapped);
}

juce::Rectangle<int> PlaylistTimeline::audioClipRect (const AudioClip& c) const noexcept
{
    const int x0 = tickToX (c.startTick);
    const int x1 = tickToX (c.endTick());
    return { x0, trackTop (c.track), std::max (1, x1 - x0), trackHeight (c.track) };
}

double PlaylistTimeline::totalWidth() const noexcept
{
    return (arrangement.timelineTicks() / static_cast<double> (kPlaylistUnitTicks)) * unitWidth;
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
    for (int section = 0; section < kSectionCount; ++section)
        if (bar == &sectionScrollBar (section))
        {
            sectionScroll[static_cast<size_t> (section)] = static_cast<int> (std::lround (newRangeStart));
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
    const double minWidth = std::max (6.0, visible / (arrangement.timelineTicks() / static_cast<double> (kPlaylistUnitTicks))); // the whole timeline fits
    const double maxWidth = kPlaylistUnitTicks * 1.6;          // 1 tick = 1.6 pixels
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
    const double units = std::max (1.0, arrangement.songLengthTicks() / static_cast<double> (kPlaylistUnitTicks));
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
    setUnitWidth (visible / (toTick - fromTick) * kPlaylistUnitTicks, kNameWidth);
    scrollX = fromTick * getPixelsPerTick();
    clampScroll();
    updateScrollBar();
    repaint();
}

juce::MouseCursor PlaylistTimeline::toolCursor() const
{
    switch (tool)
    {
        case PlaylistTool::Draw:   return juce::MouseCursor::NormalCursor;
        case PlaylistTool::Split:  return juce::MouseCursor::IBeamCursor;
        case PlaylistTool::Cut:    return juce::MouseCursor::CrosshairCursor;
        case PlaylistTool::Erase:  return juce::MouseCursor::PointingHandCursor;
        case PlaylistTool::Zoom:   return juce::MouseCursor::CrosshairCursor;
        case PlaylistTool::Select: return juce::MouseCursor::CrosshairCursor;
        case PlaylistTool::Mute:   return juce::MouseCursor::PointingHandCursor;
        case PlaylistTool::Slip:   return juce::MouseCursor::LeftRightResizeCursor;
        case PlaylistTool::Paint:  return juce::MouseCursor::CrosshairCursor;
        case PlaylistTool::Playback: return juce::MouseCursor::LeftRightResizeCursor;
    }
    return juce::MouseCursor::NormalCursor;
}

void PlaylistTimeline::setTool (PlaylistTool newTool)
{
    tool = newTool;
    setMouseCursor (toolCursor());
    repaint();
}

void PlaylistTimeline::setSnapTicks (int ticks, int bars)
{
    snapTicks = std::max (1, ticks);
    snapBars = bars;
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
    // 0.81.0: a long file brings its overview (made while it was decoded): its pages are not all read again from the disk here
    static_assert (kFramesPerPeak == kOverviewFramesPerPeak);
    if (data->overview != nullptr && static_cast<int> (data->overview->lows.size()) == bins && static_cast<int> (data->overview->highs.size()) == bins)
    {
        p.lows = data->overview->lows;
        p.highs = data->overview->highs;
        waveformCache.push_back (std::move (p));
        return waveformCache.back();
    }
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
        const double audioBpm = clip.audioBpm (bpm); // 0.83.0: a clip that follows the tempo shows its beats on the song's
        const double framesPerPixel = (audioBpm > 0.0 && pxPerTick > 0.0)
                                          ? clip.data->sampleRate * 60.0 / (audioBpm * kTicksPerBeat * pxPerTick)
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

    if (clip.muted || ! arrangement.trackPlays (clip.track)) // 0.60.0: greyed, as a muted pattern clip
    {
        g.setColour (kTrackA.withAlpha (0.62f));
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
        g.setColour (kText.withAlpha (0.55f));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.0f);
        if (clip.muted && r.getWidth() > 18 && r.getHeight() > 12)
        {
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText ("M", r.withTrimmedLeft (r.getWidth() - 16).withHeight (12), juce::Justification::centred);
        }
    }

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
    layoutSections();
    updateSectionButtons();
}

void PlaylistTimeline::paintGrid (juce::Graphics& g)
{
    const int top = kRulerHeight;
    const int bottom = getHeight() - kScrollBarHeight; // 0.69.0: down the three sections (their bars are drawn over it)
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
    // 0.83.0: beats and bars stretch by stretch of the song, each in its own time signature (the bars count on across them)
    for (const auto& seg : meterSegments (arrangement.meterMarkers, interfaceMeter()))
    {
        if (seg.endTick <= firstTick || seg.startTick > lastTick)
            continue;
        const auto segmentLines = [&] (int step, juce::Colour colour, long long barPhase)
        {
            if (step * ppt < 9.0)
                return;
            g.setColour (colour);
            const int from = std::max (firstTick, seg.startTick), to = std::min (lastTick, seg.endTick == INT_MAX ? lastTick : seg.endTick);
            // every 4 bars counts from bar 1 of the song, not from the change
            const long long phase = barPhase > 0 ? (4 - seg.firstBar % 4) % 4 * seg.meter.barTicks() : 0;
            long long t = seg.startTick + phase;
            if (from > t)
                t += (from - t) / step * step;
            for (; t <= to; t += step)
                g.drawVerticalLine (tickToX (static_cast<int> (t)), static_cast<float> (top), static_cast<float> (bottom));
        };
        segmentLines (seg.meter.beatTicks(), kGridBeat.brighter (0.1f), 0);
        segmentLines (seg.meter.barTicks(), kGridBar, 0);
        segmentLines (4 * seg.meter.barTicks(), kGridUnit, 1); // every 4 bars (before 0.56.0: every pattern length, when all were one)
    }
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
    // The time signature (0.46.0); 0.83.0: segment by segment, with the song's time signature changes (numbered on across them)
    for (const auto& seg : meterSegments (arrangement.meterMarkers, interfaceMeter()))
    {
        if (seg.endTick <= firstTick || seg.startTick > lastTick)
            continue;
        const int beatTicks = seg.meter.beatTicks(), barTicks = seg.meter.barTicks();
        const int segEnd = seg.endTick == INT_MAX ? lastTick : std::min (lastTick, seg.endTick - 1);
        const auto segmentMarks = [&] (int step, int height, juce::Colour colour)
        {
            if (step * ppt < 6.0)
                return;
            g.setColour (colour);
            for (long long t = seg.startTick + std::max (0, firstTick - seg.startTick) / step * step; t <= segEnd; t += step)
                g.drawVerticalLine (tickToX (static_cast<int> (t)), static_cast<float> (rowH - height), static_cast<float> (rowH));
        };
        segmentMarks (beatTicks, 8, kTextDim);
        segmentMarks (barTicks, rowH - 2, kText);

        const double pxPerBar = barTicks * ppt;
        const int every = barLabelEvery (pxPerBar, 34.0);
        g.setColour (kText);
        const long long firstInside = std::max (0, firstTick - seg.startTick) / barTicks;
        for (long long b = firstInside; seg.startTick + b * barTicks <= segEnd; ++b)
        {
            const long long bar = seg.firstBar + b;
            if (bar % every != 0 && b != 0)
                continue;
            g.drawText (juce::String (bar + 1), tickToX (static_cast<int> (seg.startTick + b * barTicks)) + 3, 0, 42, rowH,
                        juce::Justification::centredLeft);
        }

        if (beatTicks * ppt >= 44.0) // beat numbers "bar.beat" when there is room
        {
            g.setColour (kTextDim);
            g.setFont (juce::FontOptions (10.0f));
            for (long long t = seg.startTick + std::max (0, firstTick - seg.startTick) / beatTicks * beatTicks; t <= segEnd; t += beatTicks)
            {
                const long long inside = t - seg.startTick;
                if (inside % barTicks != 0)
                    g.drawText (juce::String (seg.firstBar + inside / barTicks + 1) + "." + juce::String ((inside % barTicks) / beatTicks + 1),
                                tickToX (static_cast<int> (t)) + 3, 0, 36, rowH, juce::Justification::centredLeft);
            }
            g.setFont (juce::FontOptions (11.0f));
        }
    }

    // 0.83.0: time signature changes: a blue flag with the new signature at the top right of its bar line
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    for (const auto& m : arrangement.meterMarkers)
    {
        if (m.tick < firstTick || m.tick > lastTick)
            continue;
        const int x = tickToX (m.tick);
        const auto box = juce::Rectangle<int> (x + 2, 1, 30, rowH - 2);
        g.setColour (juce::Colour (0xff5aa0e8));
        g.fillRect (x, 0, 2, rowH);
        g.fillRect (box);
        g.setColour (juce::Colours::black);
        g.drawText (juce::String (m.meter.text()), box, juce::Justification::centred, false);
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

    // Tempo changes (0.46.0): an orange flag with the new tempo. 0.83.0: a ramp is drawn as a slope from the marker before.
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    for (size_t i = 0; i < tempos.size(); ++i)
    {
        const auto& m = tempos[i];
        const int before = i == 0 ? 0 : tempos[i - 1].tick;
        if (m.ramp && m.tick >= firstTick && before <= lastTick)
        {
            const double from = i == 0 ? bpm : tempos[i - 1].bpm;
            const float yFrom = m.bpm >= from ? static_cast<float> (kRulerHeight - 2) : static_cast<float> (rowH + 2);
            const float yTo = m.bpm >= from ? static_cast<float> (rowH + 2) : static_cast<float> (kRulerHeight - 2);
            g.setColour (juce::Colour (0xfff0a33a).withAlpha (0.85f));
            g.drawLine (static_cast<float> (tickToX (before)), yFrom, static_cast<float> (tickToX (m.tick)), yTo, 2.0f);
        }
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

    // Time markers (0.61.0): a teal flag with the name in the upper row, and a faint line down the tracks.
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    for (const auto& m : arrangement.markers)
    {
        if (m.tick < firstTick || m.tick > lastTick)
            continue;
        const int x = tickToX (m.tick);
        // 0.62.0: a marker with an action says so, in its own colour
        static const char* const kActionText[] = { "", " [loop]", " [skip]", " [pause]", " [start]" };
        static constexpr std::uint32_t kActionColour[] = { 0xff3fc1b0, 0xffe8a33a, 0xffb07ae8, 0xffe0605a, 0xff7fd04a };
        const auto colour = juce::Colour (kActionColour[static_cast<int> (m.action)]);
        const auto text = (m.name.empty() ? juce::String ("Marker") : juce::String (m.name)) + kActionText[static_cast<int> (m.action)];
        g.setColour (colour);
        g.fillRect (x, 0, 2, rowH);
        const int width = std::clamp (juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), text) + 10, 24, 180);
        const auto box = juce::Rectangle<int> (x + 2, 1, width, rowH - 2);
        g.fillRoundedRectangle (box.toFloat(), 2.0f);
        g.setColour (juce::Colours::black);
        g.drawText (text, box.reduced (4, 0), juce::Justification::centredLeft, true);
        g.setColour (colour.withAlpha (0.25f));
        g.drawVerticalLine (x, static_cast<float> (kRulerHeight), static_cast<float> (getHeight()));
    }
}

// ------------------------------------------------------------------------- time markers (0.61.0)

int PlaylistTimeline::markerAt (int x) const noexcept
{
    for (int i = static_cast<int> (arrangement.markers.size()) - 1; i >= 0; --i)
    {
        const int mx = tickToX (arrangement.markers[static_cast<size_t> (i)].tick);
        if (x >= mx - 2 && x <= mx + 40)
            return i;
    }
    return -1;
}

void PlaylistTimeline::addMarker (int tick)
{
    tick = std::clamp (tick, 0, std::max (0, arrangement.timelineTicks() - 1));
    promptForText (*this, "Add a time marker", "Name of the marker at " + jstr (formatBarsBeatsTicks (tick)) + ":",
                   "Marker " + juce::String (arrangement.markers.size() + 1), "Add",
                   [this, tick] (const juce::String& typed)
                   {
                       beginEdit();
                       arrangement.markers.push_back ({ tick, typed.trim().substring (0, 64).toStdString() });
                       arrangement.tidyMarkers();
                       commitPendingUndo();
                       notifyChanged();
                   });
}

void PlaylistTimeline::jumpToMarker (int direction)
{
    const int from = playheadTick >= 0 ? playheadTick : lastSeekTick;
    const int i = direction > 0 ? arrangement.nextMarker (from) : arrangement.previousMarker (from);
    if (i < 0)
        return;
    const int tick = arrangement.markers[static_cast<size_t> (i)].tick;
    lastSeekTick = tick;
    setPlayheadTick (tick);
    if (onSeek)
        onSeek (tick);
}

void PlaylistTimeline::showMarkerMenu (int marker)
{
    if (marker < 0 || marker >= static_cast<int> (arrangement.markers.size()))
        return;
    juce::PopupMenu menu;
    menu.addSectionHeader (arrangement.markers[static_cast<size_t> (marker)].name.empty() ? "Marker" : juce::String (arrangement.markers[static_cast<size_t> (marker)].name));
    menu.addItem (1, "Jump here");
    menu.addItem (2, "Rename...");
    {
        // 0.62.0: what the transport does when it gets here, as the actions of FL Studio's time markers
        const auto action = arrangement.markers[static_cast<size_t> (marker)].action;
        juce::PopupMenu actions;
        actions.addItem (10, "None", true, action == MarkerAction::None);
        actions.addItem (11, "Loop: go back to the marker before this one", true, action == MarkerAction::Loop);
        actions.addItem (12, "Skip: go on from the marker after this one", true, action == MarkerAction::Skip);
        actions.addItem (13, "Pause: stop here", true, action == MarkerAction::Pause);
        actions.addItem (14, "Start: Play starts the song from here", true, action == MarkerAction::Start);
        actions.addItem (15, "Punch in: recordings keep what comes after this marker", true, action == MarkerAction::PunchIn); // 0.71.0
        actions.addItem (16, "Punch out: recordings stop at this marker", true, action == MarkerAction::PunchOut);
        menu.addSubMenu ("Action", actions);
    }
    menu.addItem (3, "Delete marker");
    menu.showMenuAsync (juce::PopupMenu::Options(), [safe = juce::Component::SafePointer<PlaylistTimeline> (this), marker] (int choice)
    {
        if (safe == nullptr || choice == 0 || marker >= static_cast<int> (safe->arrangement.markers.size()))
            return;
        auto& m = safe->arrangement.markers[static_cast<size_t> (marker)];
        if (choice >= 10 && choice <= 16)
        {
            safe->beginEdit();
            const auto action = static_cast<MarkerAction> (choice - 10);
            if (action == MarkerAction::Start || action == MarkerAction::PunchIn || action == MarkerAction::PunchOut) // one of each
                for (auto& other : safe->arrangement.markers)
                    if (other.action == action)
                        other.action = MarkerAction::None;
            m.action = action;
            safe->commitPendingUndo();
            safe->notifyChanged();
            safe->repaint();
        }
        else if (choice == 1)
        {
            safe->lastSeekTick = m.tick;
            safe->setPlayheadTick (m.tick);
            if (safe->onSeek)
                safe->onSeek (m.tick);
        }
        else if (choice == 3)
        {
            safe->beginEdit();
            safe->arrangement.markers.erase (safe->arrangement.markers.begin() + marker);
            safe->commitPendingUndo();
            safe->notifyChanged();
        }
        else if (choice == 2)
        {
            promptForText (*safe, "Rename the marker", "Name:", juce::String (m.name), "Rename",
                           [safe, marker] (const juce::String& typed)
                           {
                               if (safe == nullptr || marker >= static_cast<int> (safe->arrangement.markers.size()))
                                   return;
                               safe->beginEdit();
                               safe->arrangement.markers[static_cast<size_t> (marker)].name = typed.trim().substring (0, 64).toStdString();
                               safe->commitPendingUndo();
                               safe->notifyChanged();
                           });
        }
    });
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
    const int bottom = getHeight() - kScrollBarHeight; // 0.69.0: down the three sections
    g.fillRect (a, kRulerHeight, b - a, bottom - kRulerHeight);
    g.setColour (kLoop.withAlpha (0.45f));
    g.drawVerticalLine (a, static_cast<float> (kRulerHeight), static_cast<float> (bottom));
    g.drawVerticalLine (b - 1, static_cast<float> (kRulerHeight), static_cast<float> (bottom));
    if (b - a > 44)
    {
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText ("LOOP", band.reduced (4, 0), juce::Justification::centredLeft, false);
    }
}

void PlaylistTimeline::paintOverChildren (juce::Graphics& g)
{
    // 0.82.0: a track dragged by its name: a line where it would go (above the track it is over when moving up, under it moving down)
    // (0.86.0: where it really goes - a group moves whole and lands beside another group, not inside it)
    if (drag != Drag::TrackMove || trackMoveFrom < 0 || trackMoveTo < 0 || trackMoveTo == trackMoveFrom)
        return;
    const int before = arrangement.trackMoveLandsBefore (trackMoveFrom, trackMoveTo);
    if (before < 0)
        return;
    int y = 0;
    if (before < arrangement.trackCount() && sectionOfTrack (before) == sectionOfTrack (trackMoveFrom))
        y = trackTop (before);
    else
    {
        int last = before - 1; // after the last track of the section (a hidden one: after the visible track it is folded under)
        while (last > 0 && arrangement.trackHidden (last))
            --last;
        y = trackTop (last) + trackHeight (last);
    }
    if (y < -1000)
        return;
    g.setColour (juce::Colour (0xff4cc38a));
    g.fillRect (0, y - 1, getWidth(), 3);
}

void PlaylistTimeline::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);

    // Timeline area (clipped so scrolled content never covers the name column or the ruler).
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (kNameWidth, 0, getWidth() - kNameWidth, getHeight());

        // The rows of each section (0.69.0), each kept inside its section's body.
        for (const int section : { kAudioSection, kPatternSection })
        {
            juce::Graphics::ScopedSaveState rows (g);
            g.reduceClipRegion (sectionBody (section, true));
            for (int t = sectionFirstTrack (section); t < sectionEndTrack (section); ++t)
            {
                const int top = trackTop (t), th = trackHeight (t);
                if (top < -th)
                    continue;
                auto row = rowOf (t) % 2 == 0 ? kTrackA : kTrackB;
                if (const auto colour = arrangement.playlistTracks[static_cast<size_t> (t)].colour; colour != 0)
                    row = row.interpolatedWith (juce::Colour (colour).withAlpha (1.0f), 0.14f); // a coloured track (0.33.0)
                g.setColour (row);
                g.fillRect (kNameWidth, top, getWidth() - kNameWidth, th);
            }
        }
        {
            juce::Graphics::ScopedSaveState lanes (g);
            g.reduceClipRegion (sectionBody (kAutomationSection, true));
            for (int lane = 0; lane < arrangement.automationLanes; ++lane)
            {
                g.setColour (lane % 2 == 0 ? kLaneA : kLaneB);
                g.fillRect (kNameWidth, lanesTop() + lane * kLaneHeight, getWidth() - kNameWidth, kLaneHeight);
            }
        }

        paintGrid (g);
        paintRuler (g);
        paintLoopRegion (g);

        // End of the song: song mode loops here.
        const int songTicks = arrangement.songLengthTicks();
        if (songTicks > 0)
        {
            g.setColour (kPlayhead.withAlpha (0.35f));
            g.drawVerticalLine (tickToX (songTicks), static_cast<float> (kRulerHeight), static_cast<float> (getHeight() - kScrollBarHeight));
        }

        // The tracks' clips, kept inside their section's body when the tracks scroll (0.44.0; 0.69.0: one section at a time).
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

        pruneWaveformCache();
        for (const int section : { kAudioSection, kPatternSection })
        {
            juce::Graphics::ScopedSaveState inSection (g);
            g.reduceClipRegion (sectionBody (section, true));
            const auto visibleClip = g.getClipBounds();
            for (int i = 0; i < static_cast<int> (arrangement.clips.size()); ++i)
            {
                const auto& clip = arrangement.clips[static_cast<size_t> (i)];
                if (sectionOfTrack (clip.track) != section)
                    continue;
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
                        const int length = pattern.length();
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
                    const int length = arrangement.patternLength (clip.pattern);
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

                if (clip.muted || ! arrangement.trackPlays (clip.track)) // 0.60.0: a muted clip (or one on a silent track) is greyed
                {
                    g.setColour (kTrackA.withAlpha (0.62f));
                    g.fillRoundedRectangle (r.toFloat(), 3.0f);
                    g.setColour (kText.withAlpha (0.55f));
                    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.0f);
                    if (clip.muted && r.getWidth() > 18 && r.getHeight() > 12)
                    {
                        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
                        g.drawText ("M", r.withTrimmedLeft (r.getWidth() - 16).withHeight (12), juce::Justification::centred);
                    }
                }

                if (isSelected (Kind::Pattern, i))
                {
                    g.setColour (kSelect);
                    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.5f);
                }
            }

            for (int i = 0; i < static_cast<int> (arrangement.audioClips.size()); ++i)
                if (sectionOfTrack (arrangement.audioClips[static_cast<size_t> (i)].track) == section)
                    paintAudioClip (g, arrangement.audioClips[static_cast<size_t> (i)], isSelected (Kind::Audio, i));

            // Drop preview (files or sounds dragged over the timeline).
            if (dropTrack >= 0 && sectionOfTrack (dropTrack) == section)
            {
                const auto r = juce::Rectangle<int> (tickToX (dropTick), trackTop (dropTrack), std::max (6, tickToX (dropTick + 4 * kTicksPerBeat) - tickToX (dropTick)), trackHeight (dropTrack));
                g.setColour (kAudio.withAlpha (0.35f));
                g.fillRect (r);
                g.setColour (kAudio);
                g.drawRect (r, 2);
            }

            // Tool overlays.
            if (tool == PlaylistTool::Split && hoverTrack >= 0 && drag == Drag::None && sectionOfTrack (hoverTrack) == section)
            {
                const bool onClip = arrangement.audioClipAt (hoverTrack, hoverTick) >= 0 || arrangement.clipAt (hoverTrack, hoverTick) >= 0;
                if (onClip)
                {
                    const int tick = snapTick (hoverTick, false);
                    g.setColour (kSelect);
                    g.drawVerticalLine (tickToX (tick), static_cast<float> (trackTop (hoverTrack)), static_cast<float> (trackTop (hoverTrack) + trackHeight (hoverTrack)));
                }
            }
            if (drag == Drag::CutRange && sectionOfTrack (dragTrack) == section)
            {
                const int from = std::min (dragStartTick, dragEndTick), to = std::max (dragStartTick, dragEndTick);
                const auto r = juce::Rectangle<int> (tickToX (from), trackTop (dragTrack), std::max (2, tickToX (to) - tickToX (from)), trackHeight (dragTrack));
                g.setColour (kCut.withAlpha (0.35f));
                g.fillRect (r);
                g.setColour (kCut);
                g.drawRect (r, 2);
            }
        }

        if (drag == Drag::ZoomRange)
        {
            const auto r = juce::Rectangle<int> (std::min (dragStartX, dragEndX), kRulerHeight, std::abs (dragEndX - dragStartX),
                                                getHeight() - kScrollBarHeight - kRulerHeight);
            g.setColour (kSelect.withAlpha (0.12f));
            g.fillRect (r);
            g.setColour (kSelect.withAlpha (0.8f));
            g.drawRect (r, 1);
        }


        {
            juce::Graphics::ScopedSaveState lanes (g); // 0.69.0: inside the automation section's body
            g.reduceClipRegion (sectionBody (kAutomationSection, true));
            for (int i = 0; i < static_cast<int> (arrangement.automationClips.size()); ++i)
                paintAutomationClip (g, arrangement.automationClips[static_cast<size_t> (i)], isSelected (Kind::Automation, i));
        }

        // The selection rectangle (0.57.0), over the tracks and the automation lanes.
        if (drag == Drag::RubberBand && bandActive)
        {
            juce::Graphics::ScopedSaveState band (g);
            g.reduceClipRegion (kNameWidth, kRulerHeight, getWidth() - kNameWidth, getHeight() - kScrollBarHeight - kRulerHeight);
            const auto r = bandRect();
            g.setColour (kSelect.withAlpha (0.14f));
            g.fillRect (r);
            g.setColour (kSelect.withAlpha (0.9f));
            g.drawRect (r, 1);
        }

        paintSectionBars (g); // 0.69.0: the sections' bars and borders, over the grid

        if (playheadTick >= 0)
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
    for (const int section : { kAudioSection, kPatternSection }) // 0.69.0: each section's headers inside its body
    {
        juce::Graphics::ScopedSaveState inSection (g);
        g.reduceClipRegion (sectionBody (section, false).withWidth (kNameWidth));
        for (int t = sectionFirstTrack (section); t < sectionEndTrack (section); ++t)
        {
            const int th = trackHeight (t); // (0.72.0: its own)
            if (trackTop (t) < -th)
                continue;
            {
                // 0.60.0: the mute and solo switches of the track, as in FL Studio's track headers (lit when on).
                const auto& own = arrangement.playlistTracks[static_cast<size_t> (t)];
                g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
                for (const bool solo : { false, true })
                {
                    const auto box = (solo ? soloSwitchRect (t) : muteSwitchRect (t)).toFloat();
                    const bool on = solo ? own.solo : own.mute;
                    g.setColour (on ? (solo ? juce::Colour (0xffe0b030) : juce::Colour (0xffd9534f)) : kText.withAlpha (0.12f));
                    g.fillRoundedRectangle (box, 2.0f);
                    g.setColour (on ? juce::Colours::black.withAlpha (0.85f) : kText.withAlpha (0.55f));
                    g.drawText (solo ? "S" : "M", box, juce::Justification::centred);
                }
                g.setFont (juce::FontOptions (12.0f));
            }
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
            // 0.62.0: a group's head has a fold triangle, a grouped track is set in, a locked track says so
            const bool head = ! own.grouped && t + 1 < arrangement.trackCount() && arrangement.playlistTracks[static_cast<size_t> (t) + 1].grouped;
            int nameX = 8;
            if (head)
            {
                const auto box = foldRect (t).toFloat();
                juce::Path triangle;
                if (own.collapsed)
                    triangle.addTriangle (box.getX() + 3.0f, box.getY() + 2.0f, box.getX() + 3.0f, box.getBottom() - 2.0f, box.getRight() - 2.0f, box.getCentreY());
                else
                    triangle.addTriangle (box.getX() + 2.0f, box.getY() + 3.0f, box.getRight() - 2.0f, box.getY() + 3.0f, box.getCentreX(), box.getBottom() - 2.0f);
                g.setColour (kText.withAlpha (0.85f));
                g.fillPath (triangle);
                nameX = 20;
            }
            else if (own.grouped)
            {
                g.setColour (kText.withAlpha (0.3f));
                g.fillRect (9, trackTop (t), 2, th);
                nameX = 18;
            }
            if (own.icon != 0) // 0.72.0: the track's icon before its name
            {
                drawIcon (g, own.icon, juce::Rectangle<float> (static_cast<float> (nameX), static_cast<float> (trackTop (t)), 16.0f, static_cast<float> (th)).withSizeKeepingCentre (15.0f, 15.0f),
                          kText.withAlpha (arrangement.trackPlays (t) ? 0.85f : 0.4f));
                nameX += 19;
            }
            g.setColour (! arrangement.trackPlays (t) ? kText.withAlpha (0.45f) : t == armedTrack ? juce::Colours::white : kText);
            g.drawText ((own.locked ? juce::String::fromUTF8 ("\xf0\x9f\x94\x92 ") : juce::String()) + trackName (t)
                            + (head && own.collapsed ? " +" + juce::String (arrangement.groupEnd (t) - t) : juce::String()),
                        nameX, trackTop (t), kNameWidth - 14 - nameX - 2 * (kHeaderSwitch + 2), th, juce::Justification::centredLeft, true);
            g.setColour (kText.withAlpha (0.08f));
            g.drawHorizontalLine (trackTop (t) + th - 1, 0.0f, static_cast<float> (kNameWidth));
        }
    }
    {
        juce::Graphics::ScopedSaveState inSection (g);
        g.reduceClipRegion (sectionBody (kAutomationSection, false).withWidth (kNameWidth));
        g.setFont (juce::FontOptions (12.0f));
        for (int lane = 0; lane < arrangement.automationLanes; ++lane)
        {
            const int top = lanesTop() + lane * kLaneHeight;
            g.setColour (kAutomationColour.withAlpha (0.8f));
            g.fillRect (0, top + 1, 4, kLaneHeight - 2);
            g.setColour (kAutomationColour.brighter (0.3f));
            g.drawText ("Auto " + juce::String (lane + 1), 8, top, kNameWidth - 12, kLaneHeight, juce::Justification::centredLeft);
            g.setColour (kText.withAlpha (0.08f));
            g.drawHorizontalLine (top + kLaneHeight - 1, 0.0f, static_cast<float> (kNameWidth));
        }
    }

    {
        juce::Graphics::ScopedSaveState names (g);
        g.reduceClipRegion (0, kRulerHeight, kNameWidth, getHeight() - kRulerHeight);
        paintSectionBars (g);
    }
}

// 0.69.0: each section's bar (its name in the name column, beside the - / + buttons; how many rows it has over the timeline)
// and the borders between the sections, lit while the mouse is over one or drags it.
void PlaylistTimeline::paintSectionBars (juce::Graphics& g)
{
    static const char* const titles[] = { "Audio", "Patterns", "Automation" };
    static const char* const rows[] = { "track", "track", "lane" };
    for (int s = 0; s < kSectionCount; ++s)
    {
        const auto a = sectionArea (s);
        const juce::Rectangle<int> bar (0, a.barTop, getWidth(), kSectionBarHeight);
        g.setColour (kSectionBar);
        g.fillRect (bar);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.drawHorizontalLine (bar.getBottom() - 1, 0.0f, static_cast<float> (getWidth()));
        g.setColour (s == kAutomationSection ? kAutomationColour.brighter (0.4f) : kText);
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (titles[s], 50, bar.getY(), kNameWidth - 52, kSectionBarHeight, juce::Justification::centredLeft, true);
        const int count = s == kAutomationSection ? arrangement.automationLanes : sectionEndTrack (s) - sectionFirstTrack (s);
        g.setColour (kTextDim);
        g.setFont (juce::FontOptions (10.5f));
        g.drawText (juce::String (count) + " " + rows[s] + (count == 1 ? "" : "s"), kNameWidth + 8, bar.getY(), 120, kSectionBarHeight,
                    juce::Justification::centredLeft, false);
    }
    for (int k = 0; k < kSectionCount - 1; ++k)
    {
        const int top = splitterTop (k);
        const bool hot = splitterDrag == k || splitterHover == k;
        g.setColour (hot ? kLoop.withAlpha (0.6f) : kSplitter);
        g.fillRect (0, top, getWidth(), kSplitterHeight);
        g.setColour (hot ? kLoop : kText.withAlpha (0.1f));
        g.drawHorizontalLine (top, 0.0f, static_cast<float> (getWidth()));
        // a grip in the middle of the name column, so the border reads as something to drag
        g.setColour (kText.withAlpha (hot ? 0.9f : 0.4f));
        const float cy = static_cast<float> (top) + 0.5f * static_cast<float> (kSplitterHeight);
        for (int i = -2; i <= 2; ++i)
            g.fillEllipse (0.5f * static_cast<float> (kNameWidth) + 6.0f * static_cast<float> (i) - 1.0f, cy - 1.0f, 2.0f, 2.0f);
    }
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
        juce::PopupMenu icons, heights; // 0.72.0
        addIconItems (icons, 200, t.icon);
        menu.addSubMenu ("Icon", icons);
        static const char* const kHeightNames[] = { "Small", "Normal", "Large", "Extra large" };
        for (int h = 0; h < 4; ++h)
            heights.addItem (300 + h, kHeightNames[h], true, t.height == h);
        menu.addSubMenu ("Height", heights);
        menu.addItem (2, "Default name and colour", ! t.name.empty() || t.colour != 0);
    }
    menu.addSeparator();
    {
        const auto& t = arrangement.playlistTracks[static_cast<size_t> (track)]; // 0.60.0: as the M / S switches of the header
        menu.addItem (7, "Mute", true, t.mute);
        menu.addItem (8, "Solo", true, t.solo);
        // 0.62.0: locking and grouping, as in FL Studio's track menu
        menu.addItem (9, "Lock (its clips cannot be changed)", true, t.locked);
        menu.addSeparator();
        menu.addItem (10, "Group with the track above", track > 0 && sectionOfTrack (track - 1) == sectionOfTrack (track), t.grouped);
        const bool head = ! t.grouped && track + 1 < arrangement.trackCount() && arrangement.playlistTracks[static_cast<size_t> (track) + 1].grouped;
        menu.addItem (11, t.collapsed ? "Unfold the group" : "Fold the group", head, false);
        menu.addSeparator();
        menu.addItem (12, "Consolidate this track's pattern clips to audio", onConsolidate != nullptr);
    }
    menu.addSeparator();
    menu.addItem (3, "Record on this track", onTrackArmToggled != nullptr, recordArms[static_cast<size_t> (track)]);
    menu.addItem (4, "Recording input...", onTrackInputMenu != nullptr);
    menu.addSeparator();
    {
        const auto& t = arrangement.playlistTracks[static_cast<size_t> (track)];
        const bool ofPattern = t.pattern >= 0 && t.pattern < static_cast<int> (arrangement.patterns.size()); // 0.79.0
        if (sectionOfTrack (track) == kPatternSection)
            menu.addItem (5, "Add a pattern (with its own track)", arrangement.canAddPattern() && onAddPattern != nullptr);
        else
            menu.addItem (5, "Add an audio track", arrangement.trackCount() < kMaxPlaylistTracks);
        if (ofPattern)
            menu.addItem (6, "Delete track (and its pattern, " + juce::String (arrangement.patterns[static_cast<size_t> (t.pattern)].name) + ")...",
                          arrangement.patterns.size() > 1 && onDeletePattern != nullptr);
        else // (each section keeps at least one track, as with its - button)
            menu.addItem (6, "Delete track", arrangement.trackCount() > 1 && sectionEndTrack (sectionOfTrack (track)) - sectionFirstTrack (sectionOfTrack (track)) > 1);
        menu.addItem (13, "Audio plays through mixer track " + juce::String (arrangement.mixerTrackOf (track) + 1), false); // 0.79.0
    }
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
                                safe->addToSection (safe->sectionOfTrack (track)); // 0.69.0: at the end of the track's section
                            else if (choice == 6)
                                safe->removeTrack (track);
                            else if (choice >= 9 && choice <= 11)
                                safe->toggleTrackFlag (track, choice - 9);
                            else if (choice == 12 && safe->onConsolidate)
                                safe->onConsolidate (track, 0, -1);
                            else if (choice == 7)
                                safe->toggleTrackMute (track);
                            else if (choice == 8)
                                safe->toggleTrackSolo (track);
                            else if (choice == 2)
                            {
                                safe->beginEdit();
                                auto& t = safe->arrangement.playlistTracks[static_cast<size_t> (track)];
                                t.name.clear(); // a pattern's track stays that pattern's
                                t.colour = 0;
                                safe->commitPendingUndo();
                                safe->notifyChanged();
                            }
                            else if (choice >= 200 && choice <= 200 + kIconCount) // 0.72.0
                            {
                                safe->beginEdit();
                                safe->arrangement.playlistTracks[static_cast<size_t> (track)].icon = static_cast<std::uint8_t> (choice - 200);
                                safe->commitPendingUndo();
                                safe->notifyChanged();
                                safe->repaint();
                            }
                            else if (choice >= 300 && choice <= 303)
                            {
                                safe->beginEdit();
                                safe->arrangement.playlistTracks[static_cast<size_t> (track)].height = static_cast<std::uint8_t> (choice - 300);
                                safe->commitPendingUndo();
                                safe->notifyChanged();
                                safe->resized(); // (the sections' scroll ranges)
                                safe->repaint();
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
    return { arrangement.clips, arrangement.audioClips, arrangement.automationClips, arrangement.playlistTracks, arrangement.automationLanes };
}

void PlaylistTimeline::restore (const Snapshot& s)
{
    arrangement.clips = s.clips;
    arrangement.audioClips = s.audioClips;
    arrangement.automationClips = s.automationClips;
    arrangement.playlistTracks = s.tracks.empty() ? std::vector<PlaylistTrack> (1) : s.tracks;
    arrangement.automationLanes = std::clamp (s.automationLanes, 1, kMaxAutomationLanes);
    arrangement.syncPatternTracks(); // 0.79.0: a step from before a pattern was added still gives every pattern its track
    arrangement.dropInvalidClips();
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
    group.clear();
    if (kind == Kind::Pattern)
        group.patterns.push_back (index);
    else if (kind == Kind::Audio)
        group.audio.push_back (index);
    else if (kind == Kind::Automation)
        group.automation.push_back (index);
    repaint();
    updateReadout (lastMouseX, lastMouseY);
}

bool PlaylistTimeline::isSelected (Kind kind, int index) const noexcept
{
    const auto& list = kind == Kind::Pattern ? group.patterns : kind == Kind::Audio ? group.audio : group.automation;
    return kind != Kind::None && std::find (list.begin(), list.end(), index) != list.end();
}

// Several clips at once (0.57.0): indices outside the arrangement are dropped; the first clip becomes the single selection
// (what the readout describes when there is only one).
void PlaylistTimeline::selectGroup (Group g)
{
    const auto tidy = [] (std::vector<int>& list, size_t count)
    {
        list.erase (std::remove_if (list.begin(), list.end(), [count] (int i) { return i < 0 || static_cast<size_t> (i) >= count; }), list.end());
        std::sort (list.begin(), list.end());
        list.erase (std::unique (list.begin(), list.end()), list.end());
    };
    tidy (g.patterns, arrangement.clips.size());
    tidy (g.audio, arrangement.audioClips.size());
    tidy (g.automation, arrangement.automationClips.size());
    if (! g.patterns.empty())
        selection = { Kind::Pattern, g.patterns.front() };
    else if (! g.audio.empty())
        selection = { Kind::Audio, g.audio.front() };
    else if (! g.automation.empty())
        selection = { Kind::Automation, g.automation.front() };
    else
        selection = {};
    group = std::move (g);
    repaint();
    updateReadout (lastMouseX, lastMouseY);
}

void PlaylistTimeline::selectAll()
{
    Group g;
    for (int i = 0; i < static_cast<int> (arrangement.clips.size()); ++i)
        g.patterns.push_back (i);
    for (int i = 0; i < static_cast<int> (arrangement.audioClips.size()); ++i)
        g.audio.push_back (i);
    for (int i = 0; i < static_cast<int> (arrangement.automationClips.size()); ++i)
        g.automation.push_back (i);
    selectGroup (std::move (g));
}

juce::Rectangle<int> PlaylistTimeline::bandRect() const noexcept
{
    return juce::Rectangle<int>::leftTopRightBottom (std::min (bandStartX, bandEndX), std::min (bandStartY, bandEndY),
                                                     std::max (bandStartX, bandEndX) + 1, std::max (bandStartY, bandEndY) + 1);
}

// The clips a rectangle in view coordinates touches: pattern and audio clips where it covers the visible tracks, automation clips
// where it covers the lanes (a clip on a track scrolled out of sight is not picked up).
PlaylistTimeline::Group PlaylistTimeline::groupInRect (juce::Rectangle<int> r) const
{
    Group g;
    // 0.69.0: each clip counts where the rectangle covers its own section's body
    const auto audioArea = r.getIntersection (sectionBody (kAudioSection, true));
    const auto patternArea = r.getIntersection (sectionBody (kPatternSection, true));
    const auto lanesArea = r.getIntersection (sectionBody (kAutomationSection, true));
    const auto areaOf = [&] (int track) -> const juce::Rectangle<int>& { return sectionOfTrack (track) == kPatternSection ? patternArea : audioArea; };
    for (int i = 0; i < static_cast<int> (arrangement.clips.size()); ++i)
    {
        const auto& c = arrangement.clips[static_cast<size_t> (i)];
        if (const auto& area = areaOf (c.track); ! area.isEmpty() && clipRect (c).intersects (area))
            g.patterns.push_back (i);
    }
    for (int i = 0; i < static_cast<int> (arrangement.audioClips.size()); ++i)
    {
        const auto& c = arrangement.audioClips[static_cast<size_t> (i)];
        if (const auto& area = areaOf (c.track); ! area.isEmpty() && audioClipRect (c).intersects (area))
            g.audio.push_back (i);
    }
    if (! lanesArea.isEmpty())
        for (int i = 0; i < static_cast<int> (arrangement.automationClips.size()); ++i)
            if (automationClipRect (arrangement.automationClips[static_cast<size_t> (i)]).intersects (lanesArea))
                g.automation.push_back (i);
    return g;
}

PlaylistTimeline::ClipSet PlaylistTimeline::groupClips() const
{
    ClipSet set;
    for (const int i : group.patterns)
        if (i >= 0 && i < static_cast<int> (arrangement.clips.size()))
            set.patterns.push_back (arrangement.clips[static_cast<size_t> (i)]);
    for (const int i : group.audio)
        if (i >= 0 && i < static_cast<int> (arrangement.audioClips.size()))
            set.audio.push_back (arrangement.audioClips[static_cast<size_t> (i)]);
    for (const int i : group.automation)
        if (i >= 0 && i < static_cast<int> (arrangement.automationClips.size()))
            set.automation.push_back (arrangement.automationClips[static_cast<size_t> (i)]);
    return set;
}

namespace
{
// The extent of a set of clips: earliest start, latest end, and the first and last track (-1 when only automation).
struct Extent
{
    int start = std::numeric_limits<int>::max(), end = 0, firstTrack = std::numeric_limits<int>::max(), lastTrack = -1;
};
template <class Set>
Extent extentOf (const Set& set)
{
    Extent x;
    const auto take = [&x] (const auto& c)
    {
        x.start = std::min (x.start, c.startTick);
        x.end = std::max (x.end, c.endTick());
    };
    const auto takeTrack = [&x, &take] (const auto& c)
    {
        take (c);
        x.firstTrack = std::min (x.firstTrack, c.track);
        x.lastTrack = std::max (x.lastTrack, c.track);
    };
    for (const auto& c : set.patterns)
        takeTrack (c);
    for (const auto& c : set.audio)
        takeTrack (c);
    for (const auto& c : set.automation)
        take (c);
    if (x.start == std::numeric_limits<int>::max())
        x.start = 0;
    return x;
}
} // namespace

// Adds copies of `set` moved by `deltaTicks` and `deltaTracks` (kept inside the timeline and the tracks); automation clips keep their
// lanes. Clips that do not fit (a full lane, the clip limits) are left out. The new clips become the selection.
bool PlaylistTimeline::pasteSet (const ClipSet& set, int deltaTicks, int deltaTracks)
{
    if (set.empty())
        return false;
    const auto x = extentOf (set);
    deltaTicks = std::clamp (deltaTicks, -x.start, std::max (-x.start, arrangement.timelineTicks() - x.end));
    deltaTracks = x.lastTrack < 0 ? 0 : std::clamp (deltaTracks, -x.firstTrack, std::max (-x.firstTrack, trackCount() - 1 - x.lastTrack));

    beginEdit();
    Group added;
    for (auto c : set.patterns)
    {
        c.startTick += deltaTicks;
        c.track += deltaTracks;
        if (arrangement.canPlace (c))
        {
            arrangement.clips.push_back (c);
            added.patterns.push_back (static_cast<int> (arrangement.clips.size()) - 1);
        }
    }
    for (auto c : set.audio)
    {
        c.startTick += deltaTicks;
        c.track += deltaTracks;
        if (arrangement.audioClips.size() < static_cast<size_t> (kMaxAudioClips) && arrangement.canPlaceAudio (c))
        {
            arrangement.audioClips.push_back (c);
            added.audio.push_back (static_cast<int> (arrangement.audioClips.size()) - 1);
        }
    }
    for (auto c : set.automation)
    {
        c.startTick += deltaTicks;
        if (arrangement.automationClips.size() < static_cast<size_t> (kMaxAutomationClips) && arrangement.canPlaceAutomation (c))
        {
            arrangement.automationClips.push_back (c);
            added.automation.push_back (static_cast<int> (arrangement.automationClips.size()) - 1);
        }
    }
    if (added.empty())
    {
        pendingUndoValid = false;
        return false;
    }
    commitPendingUndo();
    selectGroup (std::move (added));
    notifyChanged();
    return true;
}

// A drag on one of several selected clips moves them all (0.57.0): in time by the grabbed clip's snapped move, across tracks by
// the tracks the mouse crossed (automation clips keep their lanes). A move that would put an automation clip over another is skipped.
void PlaylistTimeline::startGroupMove (Kind kind, int index, int tick)
{
    groupOriginal = groupClips();
    groupMoving = group;
    groupGrabKind = kind;
    groupGrabIndex = index;
    if (kind == Kind::Pattern)
    {
        const auto& c = arrangement.clips[static_cast<size_t> (index)];
        groupGrabStart = c.startTick;
        groupGrabTrack = c.track;
    }
    else if (kind == Kind::Audio)
    {
        const auto& c = arrangement.audioClips[static_cast<size_t> (index)];
        groupGrabStart = c.startTick;
        groupGrabTrack = c.track;
    }
    else
    {
        groupGrabStart = arrangement.automationClips[static_cast<size_t> (index)].startTick;
        groupGrabTrack = -1;
    }
    grabOffsetTicks = tick - groupGrabStart;
    drag = Drag::MoveGroup;
    beginEdit();
}

void PlaylistTimeline::moveGroup (const juce::MouseEvent& e)
{
    if (groupMoving.patterns.size() != groupOriginal.patterns.size() || groupMoving.audio.size() != groupOriginal.audio.size()
        || groupMoving.automation.size() != groupOriginal.automation.size())
        return;
    const auto x = extentOf (groupOriginal);
    int deltaTicks = snapTick (xToTick (e.x) - grabOffsetTicks, e.mods.isAltDown()) - groupGrabStart;
    deltaTicks = std::clamp (deltaTicks, -x.start, std::max (-x.start, arrangement.timelineTicks() - x.end));
    int deltaTracks = groupGrabTrack >= 0 && x.lastTrack >= 0 && inTracks (e.y) ? yToTrack (e.y) - groupGrabTrack : 0;
    if (x.lastTrack >= 0)
        deltaTracks = std::clamp (deltaTracks, -x.firstTrack, std::max (-x.firstTrack, trackCount() - 1 - x.lastTrack));

    // Where the automation clips would go: clear of every automation clip that is not moving.
    std::vector<AutomationClip> movedAutomation;
    for (auto c : groupOriginal.automation)
    {
        c.startTick += deltaTicks;
        for (int i = 0; i < static_cast<int> (arrangement.automationClips.size()); ++i)
        {
            const auto& o = arrangement.automationClips[static_cast<size_t> (i)];
            if (std::find (groupMoving.automation.begin(), groupMoving.automation.end(), i) == groupMoving.automation.end()
                && o.lane == c.lane && c.startTick < o.endTick() && o.startTick < c.endTick())
                return;
        }
        movedAutomation.push_back (c);
    }

    bool changed = false;
    for (size_t k = 0; k < groupMoving.patterns.size(); ++k)
    {
        auto c = groupOriginal.patterns[k];
        c.startTick += deltaTicks;
        c.track += deltaTracks;
        auto& stored = arrangement.clips[static_cast<size_t> (groupMoving.patterns[k])];
        changed = changed || c.startTick != stored.startTick || c.track != stored.track;
        stored = c;
    }
    for (size_t k = 0; k < groupMoving.audio.size(); ++k)
    {
        auto c = groupOriginal.audio[k];
        c.startTick += deltaTicks;
        c.track += deltaTracks;
        auto& stored = arrangement.audioClips[static_cast<size_t> (groupMoving.audio[k])];
        changed = changed || c.startTick != stored.startTick || c.track != stored.track;
        stored = c;
    }
    for (size_t k = 0; k < groupMoving.automation.size(); ++k)
    {
        auto& stored = arrangement.automationClips[static_cast<size_t> (groupMoving.automation[k])];
        changed = changed || movedAutomation[k].startTick != stored.startTick;
        stored = movedAutomation[k];
    }
    if (! changed)
        return;
    commitPendingUndo();
    dragChanged = true;
    repaint();
    updateReadout (e.x, e.y);
    if (onChanged)
        onChanged();
}

void PlaylistTimeline::clearSelection()
{
    select (Kind::None, -1);
}

// ------------------------------------------------------------------------- readouts

juce::String PlaylistTimeline::describeSelection() const
{
    if (group.size() > 1)
    {
        juce::StringArray parts;
        if (! group.patterns.empty())
            parts.add (juce::String (static_cast<int> (group.patterns.size())) + (group.patterns.size() == 1 ? " pattern clip" : " pattern clips"));
        if (! group.audio.empty())
            parts.add (juce::String (static_cast<int> (group.audio.size())) + (group.audio.size() == 1 ? " audio clip" : " audio clips"));
        if (! group.automation.empty())
            parts.add (juce::String (static_cast<int> (group.automation.size())) + (group.automation.size() == 1 ? " automation clip" : " automation clips"));
        return juce::String (group.size()) + " clips selected (" + parts.joinIntoString (", ")
             + ")   drag one to move them all, Delete removes them, Ctrl+C / Ctrl+X / Ctrl+D";
    }
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
        const double repeats = length / static_cast<double> (arrangement.patternLength (c.pattern));
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

// Every clip under the point (x, y) of the view, layered ones too: pattern and audio clips on the tracks, automation clips on the
// lanes (0.57.0).
bool PlaylistTimeline::eraseUnder (int x, int y)
{
    if (x < kNameWidth)
        return false;
    bool any = false;
    if (inTracks (y))
    {
        const int track = yToTrack (y), tick = xToTick (x);
        while (! arrangement.trackLocked (track) && eraseAt (track, tick)) // 0.62.0: not on a locked track
            any = true;
    }
    else if (inLanes (y))
    {
        const int lane = yToLane (y), tick = xToTick (x);
        for (int hit = arrangement.automationClipAt (lane, tick); hit >= 0; hit = arrangement.automationClipAt (lane, tick))
        {
            commitPendingUndo();
            arrangement.automationClips.erase (arrangement.automationClips.begin() + hit);
            clearSelection();
            any = true;
        }
    }
    return any;
}

bool PlaylistTimeline::eraseAlong (int x0, int y0, int x1, int y1)
{
    const int steps = std::max (1, static_cast<int> (std::ceil (std::hypot (x1 - x0, y1 - y0) / 3.0)));
    bool any = false;
    for (int s = 0; s <= steps; ++s)
    {
        const double t = static_cast<double> (s) / steps;
        any = eraseUnder (juce::roundToInt (x0 + (x1 - x0) * t), juce::roundToInt (y0 + (y1 - y0) * t)) || any;
    }
    return any;
}

// The Mute tool (0.60.0): every pattern and audio clip under a point of the view gets `muted`; true when one changed.
bool PlaylistTimeline::muteUnder (int x, int y, bool muted)
{
    if (x < kNameWidth || ! inTracks (y))
        return false;
    const int track = yToTrack (y), tick = xToTick (x);
    bool any = false;
    for (auto& c : arrangement.clips)
        if (c.track == track && tick >= c.startTick && tick < c.endTick() && c.muted != muted)
        {
            commitPendingUndo();
            c.muted = muted;
            any = true;
        }
    for (auto& a : arrangement.audioClips)
        if (a.track == track && tick >= a.startTick && tick < a.endTick() && a.muted != muted)
        {
            commitPendingUndo();
            a.muted = muted;
            any = true;
        }
    return any;
}

bool PlaylistTimeline::muteAlong (int x0, int y0, int x1, int y1, bool muted)
{
    const int steps = std::max (1, static_cast<int> (std::ceil (std::hypot (x1 - x0, y1 - y0) / 3.0)));
    bool any = false;
    for (int s = 0; s <= steps; ++s)
    {
        const double t = static_cast<double> (s) / steps;
        any = muteUnder (juce::roundToInt (x0 + (x1 - x0) * t), juce::roundToInt (y0 + (y1 - y0) * t), muted) || any;
    }
    return any;
}

// The Slip tool (0.60.0): the clip stays where it is; what plays inside it moves by `deltaTicks` (dragged right: the content
// starts later, so the clip plays from earlier in its pattern or file).
void PlaylistTimeline::applySlip (int deltaTicks)
{
    if (dragClip < 0)
        return;
    if (slipKind == Kind::Pattern && dragClip < static_cast<int> (arrangement.clips.size()))
    {
        auto& c = arrangement.clips[static_cast<size_t> (dragClip)];
        const int length = arrangement.patternLength (c.pattern);
        const int wanted = (((slipOriginalOffsetTicks - deltaTicks) % length) + length) % length;
        if (wanted != c.offsetTicks)
        {
            c.offsetTicks = wanted;
            dragChanged = true;
            repaint();
            updateReadout (lastMouseX, lastMouseY);
        }
    }
    else if (slipKind == Kind::Audio && dragClip < static_cast<int> (arrangement.audioClips.size()))
    {
        auto& a = arrangement.audioClips[static_cast<size_t> (dragClip)];
        if (a.data == nullptr || a.data->sampleRate <= 0.0 || bpm <= 0.0)
            return;
        const double fileSeconds = a.data->numFrames() / a.data->sampleRate;
        const double audioBpm = a.audioBpm (bpm); // 0.83.0
        const double clipSeconds = a.lengthTicks * 60.0 / (audioBpm * kTicksPerBeat);
        const double deltaSeconds = deltaTicks * 60.0 / (audioBpm * kTicksPerBeat);
        const double wanted = std::clamp (slipOriginalOffsetSeconds - deltaSeconds, 0.0, std::max (0.0, fileSeconds - clipSeconds));
        if (std::abs (wanted - a.offsetSeconds) > 1.0e-9)
        {
            a.offsetSeconds = wanted;
            dragChanged = true;
            repaint();
            updateReadout (lastMouseX, lastMouseY);
        }
    }
}

// The M / S switches of a track header (0.60.0), left of the record dot.
juce::Rectangle<int> PlaylistTimeline::muteSwitchRect (int track) const noexcept
{
    const int th = trackHeight (track);
    return { kNameWidth - 20 - 2 * (kHeaderSwitch + 2), trackTop (track) + (th - kHeaderSwitch) / 2, kHeaderSwitch, kHeaderSwitch };
}

juce::Rectangle<int> PlaylistTimeline::soloSwitchRect (int track) const noexcept
{
    const int th = trackHeight (track);
    return { kNameWidth - 20 - (kHeaderSwitch + 2), trackTop (track) + (th - kHeaderSwitch) / 2, kHeaderSwitch, kHeaderSwitch };
}

void PlaylistTimeline::toggleTrackMute (int track)
{
    if (track < 0 || track >= arrangement.trackCount())
        return;
    beginEdit();
    auto& t = arrangement.playlistTracks[static_cast<size_t> (track)];
    t.mute = ! t.mute;
    commitPendingUndo();
    notifyChanged();
}

void PlaylistTimeline::toggleTrackSolo (int track)
{
    if (track < 0 || track >= arrangement.trackCount())
        return;
    beginEdit();
    auto& t = arrangement.playlistTracks[static_cast<size_t> (track)];
    t.solo = ! t.solo;
    commitPendingUndo();
    notifyChanged();
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
    if (group.size() > 1)
    {
        // Every selected clip, as one undo step (0.57.0); from the last index down, so the others keep theirs.
        auto g = group;
        const auto eraseAll = [] (auto& clips, std::vector<int> list)
        {
            std::sort (list.begin(), list.end(), std::greater<>());
            list.erase (std::unique (list.begin(), list.end()), list.end());
            for (const int i : list)
                if (i >= 0 && i < static_cast<int> (clips.size()))
                    clips.erase (clips.begin() + i);
        };
        beginEdit();
        commitPendingUndo();
        eraseAll (arrangement.clips, g.patterns);
        eraseAll (arrangement.audioClips, g.audio);
        eraseAll (arrangement.automationClips, g.automation);
        clearSelection();
        notifyChanged();
        return;
    }
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
    if (group.size() > 1)
    {
        clipboard.kind = Kind::None;
        clipboard.group = groupClips();
        return;
    }
    if (selection.kind == Kind::Audio || selection.kind == Kind::Pattern)
        clipboard.group = {};
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
    if (clipboard.kind == Kind::None && ! clipboard.group.empty())
    {
        // Several clips: the earliest one goes to the mouse, the topmost track to the track under it; the rest keep their places
        // around them (0.57.0).
        const auto x = extentOf (clipboard.group);
        pasteSet (clipboard.group, snapTick (tick, false) - x.start, x.lastTrack >= 0 ? track - x.firstTrack : 0);
        return;
    }
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
    if (group.size() > 1)
    {
        const auto set = groupClips(); // right after the selection, on the same tracks and lanes (0.57.0)
        const auto x = extentOf (set);
        pasteSet (set, x.end - x.start, 0);
        return;
    }
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

// 0.83.0: a clip that follows the song's tempo (FL Studio's "Stretch" mode): the tempo of its audio is asked (guessed from the name, or
// the song's), after which it is stretched as it plays; a second time it plays at its own speed again.
void PlaylistTimeline::toggleFollowTempo (int index)
{
    if (index < 0 || index >= static_cast<int> (arrangement.audioClips.size()))
        return;
    const auto apply = [this] (int which, double tempo)
    {
        if (which >= static_cast<int> (arrangement.audioClips.size()))
            return;
        auto edited = arrangement.audioClips[static_cast<size_t> (which)];
        if (! setFollowTempo (edited, tempo, bpm) || ! arrangement.canPlaceAudio (edited, which))
            return;
        beginEdit();
        commitPendingUndo();
        arrangement.audioClips[static_cast<size_t> (which)] = edited;
        notifyChanged();
    };
    const auto& clip = arrangement.audioClips[static_cast<size_t> (index)];
    if (clip.followsTempo())
    {
        apply (index, 0.0);
        return;
    }
    const double guess = tempoFromName (clip.name);
    promptForText (*this, "Follow the song's tempo",
                   "The tempo the audio was recorded at (BPM, 20 - 400)" + juce::String (guess > 0.0 ? ", read from its name" : "")
                       + ". The clip is stretched as it plays, keeping its pitch, so that it stays in time with the song at any tempo, "
                         "through tempo changes and ramps too.",
                   juce::String (guess > 0.0 ? guess : bpm, 2), "Follow",
                   [safe = juce::Component::SafePointer<PlaylistTimeline> (this), index, apply] (const juce::String& typed)
                   {
                       const double tempo = typed.trim().getDoubleValue();
                       if (safe != nullptr && tempo >= kMinBpm && tempo <= kMaxBpm)
                           apply (index, tempo);
                   });
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

void PlaylistTimeline::showClipMenu (int x, int y)
{
    const int track = yToTrack (y);
    const int tick = xToTick (x);
    const int audio = arrangement.audioClipAt (track, tick);
    const int pattern = audio >= 0 ? -1 : arrangement.clipAt (track, tick);
    if (audio < 0 && pattern < 0)
        return;

    select (audio >= 0 ? Kind::Audio : Kind::Pattern, audio >= 0 ? audio : pattern);

    juce::PopupMenu menu;
    menu.addItem (1, "Split here");
    {
        juce::PopupMenu chop; // 0.70.0
        chop.addItem (20, "Every bar");
        chop.addItem (21, "Every beat");
        chop.addItem (22, "Every half beat (8ths)");
        chop.addItem (23, "Every quarter beat (16ths)");
        menu.addSubMenu ("Chop into pieces", chop);
    }
    menu.addItem (2, "Duplicate (Ctrl+D)");
    menu.addItem (3, "Copy (Ctrl+C)");
    if (audio >= 0)
    {
        const auto& clip = arrangement.audioClips[static_cast<size_t> (audio)];
        menu.addItem (4, "Position, gain, pan and fades...");
        menu.addItem (9, "Reverse", true, clip.reverse);
        menu.addItem (7, "Stretch and pitch...");
        menu.addItem (15, clip.followsTempo() ? "Follow the song's tempo (audio at " + juce::String (clip.tempoBpm, 1) + " BPM)"
                                              : juce::String ("Follow the song's tempo..."), true, clip.followsTempo()); // 0.83.0
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
    {
        menu.addItem (5, "Open this pattern");
        menu.addItem (13, "Make unique (this clip gets its own copy of the pattern)", onMakeUnique != nullptr); // 0.62.0
        menu.addItem (14, "Consolidate to audio (this clip)", onConsolidate != nullptr);
    }
    menu.addSeparator();
    {
        const bool muted = audio >= 0 ? arrangement.audioClips[static_cast<size_t> (audio)].muted : arrangement.clips[static_cast<size_t> (pattern)].muted;
        menu.addItem (12, "Mute clip", true, muted); // 0.60.0: as the Mute tool
    }
    menu.addItem (6, "Delete (Delete)");

    menu.showMenuAsync (juce::PopupMenu::Options(),
                        [safe = juce::Component::SafePointer<PlaylistTimeline> (this), track, tick, audio, pattern] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            switch (result)
                            {
                                case 1: if (safe->splitAt (track, tick, false)) safe->notifyChanged(); break;
                                case 2: safe->duplicateSelected(); break;
                                case 20: case 21: case 22: case 23: // 0.70.0: chop
                                {
                                    const int piece = result == 20 ? interfaceMeter().barTicks() : kTicksPerBeat >> (result - 21);
                                    safe->beginEdit();
                                    const int cuts = audio >= 0 ? chopAudioClip (safe->arrangement, audio, piece, safe->bpm)
                                                                : chopPatternClip (safe->arrangement, pattern, piece);
                                    if (cuts > 0)
                                    {
                                        safe->commitPendingUndo();
                                        safe->notifyChanged();
                                    }
                                    else
                                        safe->pendingUndoValid = false;
                                    break;
                                }
                                case 3: safe->copySelected(); break;
                                case 4: safe->editAudioProperties (audio); break;
                                case 7: safe->editAudioStretch (audio); break;
                                case 15: safe->toggleFollowTempo (audio); break; // 0.83.0
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
                                case 13:
                                    if (pattern >= 0 && pattern < static_cast<int> (safe->arrangement.clips.size()) && safe->onMakeUnique)
                                        safe->onMakeUnique (pattern);
                                    break;
                                case 14:
                                    if (pattern >= 0 && pattern < static_cast<int> (safe->arrangement.clips.size()) && safe->onConsolidate)
                                    {
                                        const auto c = safe->arrangement.clips[static_cast<size_t> (pattern)];
                                        safe->onConsolidate (c.track, c.startTick, c.endTick());
                                    }
                                    break;
                                case 12:
                                    if (audio >= 0 && audio < static_cast<int> (safe->arrangement.audioClips.size()))
                                    {
                                        safe->beginEdit();
                                        auto& c = safe->arrangement.audioClips[static_cast<size_t> (audio)];
                                        c.muted = ! c.muted;
                                        safe->commitPendingUndo();
                                        safe->notifyChanged();
                                    }
                                    else if (pattern >= 0 && pattern < static_cast<int> (safe->arrangement.clips.size()))
                                    {
                                        safe->beginEdit();
                                        auto& c = safe->arrangement.clips[static_cast<size_t> (pattern)];
                                        c.muted = ! c.muted;
                                        safe->commitPendingUndo();
                                        safe->notifyChanged();
                                    }
                                    break;
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
        if (letter == 'A') { selectAll(); return true; } // 0.57.0
        if (letter == 'X') { copySelected(); deleteSelected(); return true; }
        if (letter == 'V') { pasteAt (track, hoverTrack >= 0 ? hoverTick : 0); return true; }
        if (letter == 'D') { duplicateSelected(); return true; }
        return false;
    }

    if (code == juce::KeyPress::escapeKey && ! group.empty())
    {
        clearSelection();
        return true;
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
        case '1': changeTool (PlaylistTool::Draw); return true;
        case '2': changeTool (PlaylistTool::Split); return true;
        case '3': changeTool (PlaylistTool::Cut); return true;
        case '4': changeTool (PlaylistTool::Erase); return true;
        case '5': changeTool (PlaylistTool::Zoom); return true;
        case '6': changeTool (PlaylistTool::Select); return true;
        case '7': changeTool (PlaylistTool::Mute); return true;   // 0.60.0
        case '8': changeTool (PlaylistTool::Slip); return true;
        case '9': changeTool (PlaylistTool::Paint); return true; // 0.61.0
        case '0': changeTool (PlaylistTool::Playback); return true; // 0.71.0
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

    // 0.69.0: the borders between the sections can be dragged up and down; over the bars, the plain arrow.
    if (const int splitter = splitterAt (e.y); splitter >= 0 || splitterHover >= 0)
    {
        if (splitter != splitterHover)
        {
            splitterHover = splitter;
            repaint();
        }
        if (splitter >= 0)
        {
            setHover (-1, 0);
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
            return;
        }
        setMouseCursor (toolCursor());
    }
    if (e.y >= kRulerHeight && sectionAt (e.y) < 0)
    {
        setHover (-1, 0);
        setMouseCursor (juce::MouseCursor::NormalCursor);
        return;
    }
    if (e.y >= kRulerHeight && tool != PlaylistTool::Draw)
        setMouseCursor (toolCursor()); // back from a bar, a border or the ruler (the Draw tool sets its own below)

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

    if (tool == PlaylistTool::Draw && e.x >= kNameWidth && inLanes (e.y))
    {
        const int hitClip = arrangement.automationClipAt (yToLane (e.y), xToTick (e.x));
        const bool edge = hitClip >= 0 && e.x >= automationClipRect (arrangement.automationClips[static_cast<size_t> (hitClip)]).getRight() - kEdgeGrab;
        setMouseCursor (edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        return;
    }

    // Resize cursor over the edge of an audio clip (Draw tool).
    if (tool == PlaylistTool::Draw && hoverTrack >= 0)
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
    if (splitterHover >= 0 && splitterDrag < 0)
    {
        splitterHover = -1;
        setMouseCursor (toolCursor());
        repaint();
    }
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

    // 0.69.0: a border between two sections: dragging it gives one more height and the other less.
    if (const int splitter = splitterAt (e.y); splitter >= 0 && e.y >= kRulerHeight)
    {
        if (! e.mods.isPopupMenu())
        {
            splitterDrag = splitter;
            splitterGrabY = e.y;
            splitterStartHeights = bodyHeights();
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
            repaint();
        }
        return;
    }
    if (e.y >= kRulerHeight && sectionAt (e.y) < 0)
        return; // a section's bar (its buttons are components of their own)

    // The ruler: left click moves the song position (while playing too), right button makes or edits the loop region.
    if (e.x >= kNameWidth && e.y < kRulerHeight && (tool != PlaylistTool::Zoom || e.mods.isPopupMenu()))
    {
        const bool noSnap = e.mods.isAltDown();
        if (e.y < kRulerHeight / 2 && markerAt (e.x) >= 0) // 0.61.0: a marker's flag: its menu
        {
            showMarkerMenu (markerAt (e.x));
            return;
        }
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
        lastSeekTick = tick; // 0.61.0: where "+ Marker" puts a marker while nothing plays
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
        if (track >= 0 && track < arrangement.trackCount() && foldRect (track).expanded (2).contains (e.getPosition())
            && ! arrangement.playlistTracks[static_cast<size_t> (track)].grouped && track + 1 < arrangement.trackCount()
            && arrangement.playlistTracks[static_cast<size_t> (track) + 1].grouped)
        {
            toggleTrackFlag (track, 2); // 0.62.0: fold / unfold the group
            return;
        }
        if (track >= 0 && track < arrangement.trackCount() && muteSwitchRect (track).expanded (1).contains (e.getPosition()))
        {
            toggleTrackMute (track); // 0.60.0
            return;
        }
        if (track >= 0 && track < arrangement.trackCount() && soloSwitchRect (track).expanded (1).contains (e.getPosition()))
        {
            toggleTrackSolo (track);
            return;
        }
        if (track >= 0 && track < arrangement.trackCount() && (e.x >= kNameWidth - 20 || e.mods.isCommandDown()))
        {
            if (onTrackArmToggled)
                onTrackArmToggled (track); // the record dot (0.36.0)
            return;
        }
        setSelectedTrack (track);
        if (onTrackSelected)
            onTrackSelected (track);
        if (track >= 0 && track < arrangement.trackCount()) // 0.82.0: dragging the name up or down moves the track (in its section)
        {
            drag = Drag::TrackMove;
            trackMoveFrom = trackMoveTo = track;
        }
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
        if (e.mods.isPopupMenu())
        {
            // A right click shows the clip's menu when the button comes up; a right drag erases (0.57.0).
            drag = Drag::RightPress;
            rightPressX = e.x;
            rightPressY = e.y;
            return;
        }
        if (tool == PlaylistTool::Erase)
        {
            beginEdit();
            drag = Drag::EraseSweep;
            eraseLastX = e.x;
            eraseLastY = e.y;
            if (eraseUnder (e.x, e.y))
                notifyChanged();
            return;
        }
        if (tool == PlaylistTool::Select)
        {
            selectToolPress (Kind::Automation, hitClip, tick, e);
            return;
        }
        if (hitClip < 0)
        {
            clearSelection();
            return;
        }
        if (tool != PlaylistTool::Draw)
        {
            select (Kind::Automation, hitClip);
            return;
        }
        const bool atEdge = e.x >= automationClipRect (arrangement.automationClips[static_cast<size_t> (hitClip)]).getRight() - kEdgeGrab;
        if (group.size() > 1 && isSelected (Kind::Automation, hitClip) && ! atEdge)
        {
            startGroupMove (Kind::Automation, hitClip, tick);
            return;
        }

        select (Kind::Automation, hitClip);
        dragClip = hitClip;
        dragAutomationOriginal = arrangement.automationClips[static_cast<size_t> (hitClip)];
        beginEdit();
        if (atEdge)
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

    // 0.62.0: a locked track's clips stay as they are (its header menu unlocks it)
    if (arrangement.trackLocked (track))
    {
        if (onStatus)
            onStatus (trackName (track) + " is locked: right-click its name to unlock it");
        return;
    }

    if (tool == PlaylistTool::Playback && ! e.mods.isPopupMenu()) // 0.71.0: play from here while the mouse is held
    {
        playbackDrag = true;
        if (onPlaybackStart)
            onPlaybackStart (std::max (0, xToTick (e.x)));
        return;
    }

    if (e.mods.isPopupMenu())
    {
        // A right click shows the clip's menu when the button comes up; a right drag erases every clip it passes (0.57.0).
        drag = Drag::RightPress;
        rightPressX = e.x;
        rightPressY = e.y;
        return;
    }

    if (tool == PlaylistTool::Erase)
    {
        beginEdit();
        drag = Drag::EraseSweep;
        eraseLastX = e.x;
        eraseLastY = e.y;
        if (eraseUnder (e.x, e.y))
            notifyChanged();
        return;
    }

    if (tool == PlaylistTool::Mute) // 0.60.0: the clip under the mouse flips; a sweep gives the others its new state
    {
        if (audioHit < 0 && hit < 0)
            return;
        muteSweepState = audioHit >= 0 ? ! arrangement.audioClips[static_cast<size_t> (audioHit)].muted
                                       : ! arrangement.clips[static_cast<size_t> (hit)].muted;
        beginEdit();
        drag = Drag::MuteSweep;
        eraseLastX = e.x;
        eraseLastY = e.y;
        if (muteUnder (e.x, e.y, muteSweepState))
            notifyChanged();
        return;
    }

    if (tool == PlaylistTool::Paint) // 0.61.0: as FL Studio's Paint: one clip, then more while the mouse moves along the track
    {
        if (hit >= 0 || audioHit >= 0)
            return;
        if (placeClipAt (track, tick, noSnap))
        {
            paintTrack = track;
            drag = Drag::PaintSweep;
        }
        return;
    }

    if (tool == PlaylistTool::Slip) // 0.60.0
    {
        if (audioHit < 0 && hit < 0)
            return;
        slipKind = audioHit >= 0 ? Kind::Audio : Kind::Pattern;
        dragClip = audioHit >= 0 ? audioHit : hit;
        slipStartTick = tick;
        if (slipKind == Kind::Audio)
            slipOriginalOffsetSeconds = arrangement.audioClips[static_cast<size_t> (dragClip)].offsetSeconds;
        else
            slipOriginalOffsetTicks = arrangement.clips[static_cast<size_t> (dragClip)].offsetTicks;
        select (slipKind, dragClip);
        beginEdit();
        drag = Drag::Slip;
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

    if (tool == PlaylistTool::Select)
    {
        if (audioHit >= 0)
            selectToolPress (Kind::Audio, audioHit, tick, e);
        else
            selectToolPress (Kind::Pattern, hit, tick, e);
        return;
    }

    // Draw tool.
    if (audioHit >= 0)
    {
        const auto& clip = arrangement.audioClips[static_cast<size_t> (audioHit)];
        const auto r = audioClipRect (clip);
        const bool leftEdge = r.getWidth() >= 3 * kEdgeGrab && e.x <= r.getX() + kEdgeGrab;
        const bool rightEdge = e.x >= r.getRight() - kEdgeGrab;
        if (group.size() > 1 && isSelected (Kind::Audio, audioHit) && ! leftEdge && ! rightEdge)
        {
            startGroupMove (Kind::Audio, audioHit, tick);
            return;
        }
        dragClip = audioHit;
        select (Kind::Audio, audioHit);
        beginEdit();
        if (leftEdge)
            drag = Drag::TrimAudio;
        else if (rightEdge)
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
        // Empty space: a new clip there; dragging right away lengthens it, in the same undo step (as before 0.57.0).
        if (placeClipAt (track, tick, noSnap))
        {
            dragClip = static_cast<int> (arrangement.clips.size()) - 1;
            drag = Drag::ResizePattern;
        }
        return;
    }

    const auto r = clipRect (arrangement.clips[static_cast<size_t> (hit)]);
    const bool rightEdge = e.x >= r.getRight() - kEdgeGrab;
    if (group.size() > 1 && isSelected (Kind::Pattern, hit) && ! rightEdge)
    {
        startGroupMove (Kind::Pattern, hit, tick);
        return;
    }
    dragClip = hit;
    select (Kind::Pattern, hit);
    beginEdit();
    if (rightEdge)
        drag = Drag::ResizePattern;
    else
    {
        drag = Drag::MovePattern;
        grabOffsetTicks = tick - arrangement.clips[static_cast<size_t> (hit)].startTick;
    }
}

// A new clip: one pattern long, on the grid square clicked (anywhere with Alt) - not on whole pattern lengths (0.42.0).
// On a track dedicated to a pattern that pattern goes there (0.44.0); on a free track, the one selected in the pattern box.
// The Select tool (0.59.0): on empty space a press starts the selection rectangle (a click selects nothing); on a clip, Shift adds it
// to the selection or takes it out, otherwise the press selects it (or keeps the selection it belongs to) and a drag moves them all.
void PlaylistTimeline::selectToolPress (Kind kind, int index, int tick, const juce::MouseEvent& e)
{
    if (index < 0)
    {
        drag = Drag::RubberBand;
        bandStartX = bandEndX = e.x;
        bandStartY = bandEndY = e.y;
        bandActive = false;
        bandAdds = e.mods.isShiftDown();
        bandBase = bandAdds ? group : Group {};
        return;
    }
    if (e.mods.isShiftDown())
    {
        toggleInGroup (kind, index);
        return;
    }
    if (! isSelected (kind, index))
        select (kind, index);
    startGroupMove (kind, index, tick);
}

bool PlaylistTimeline::placeClipAt (int track, int tick, bool noSnap)
{
    const int ownPattern = track >= 0 && track < arrangement.trackCount() ? arrangement.playlistTracks[static_cast<size_t> (track)].pattern : -1;
    const int placed = ownPattern >= 0 && ownPattern < static_cast<int> (arrangement.patterns.size()) ? ownPattern : selectedPattern;
    const int length = arrangement.patternLength (placed); // that pattern's own length (0.56.0)
    const int at = std::clamp (noSnap ? tick : floorToGrid (tick), 0, std::max (0, arrangement.timelineTicks() - length));
    const Clip c { placed, track, at, length, 0 };
    if (! arrangement.canPlace (c))
    {
        clearSelection();
        return false;
    }
    beginEdit();
    commitPendingUndo();
    arrangement.clips.push_back (c);
    select (Kind::Pattern, static_cast<int> (arrangement.clips.size()) - 1);
    notifyChanged();
    return true;
}

void PlaylistTimeline::toggleInGroup (Kind kind, int index)
{
    auto g = group;
    auto& list = kind == Kind::Pattern ? g.patterns : kind == Kind::Audio ? g.audio : g.automation;
    if (const auto it = std::find (list.begin(), list.end(), index); it != list.end())
        list.erase (it);
    else
        list.push_back (index);
    selectGroup (std::move (g));
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
    if (playbackDrag) // 0.71.0
    {
        if (onPlaybackMove)
            onPlaybackMove (std::max (0, xToTick (e.x)));
        return;
    }
    const bool noSnap = e.mods.isAltDown();

    if (drag == Drag::TrackMove) // 0.82.0
    {
        if (std::abs (e.getDistanceFromDragStartY()) < 4 || trackMoveFrom < 0 || trackMoveFrom >= arrangement.trackCount())
            return;
        const int section = sectionOfTrack (trackMoveFrom);
        const int first = sectionFirstTrack (section), last = sectionEndTrack (section) - 1;
        int target = trackMoveTo;
        if (e.y < sectionArea (section).bodyTop)
            target = first;
        else if (e.y >= sectionArea (section).bodyBottom())
            target = last;
        else if (inTracks (e.y))
            target = std::clamp (yToTrack (e.y), first, last);
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        if (target != trackMoveTo)
        {
            trackMoveTo = target;
            repaint();
        }
        return;
    }

    if (splitterDrag >= 0) // 0.69.0: the two sections beside the border share their heights anew
    {
        const auto upper = static_cast<size_t> (splitterDrag), lower = upper + 1;
        const int both = splitterStartHeights[upper] + splitterStartHeights[lower];
        const int minimum = std::min (kMinSectionBody, both / 2);
        const int above = std::clamp (splitterStartHeights[upper] + e.y - splitterGrabY, minimum, both - minimum);
        auto heights = splitterStartHeights;
        heights[upper] = above;
        heights[lower] = both - above;
        const int total = heights[0] + heights[1] + heights[2];
        if (total > 0)
            for (size_t i = 0; i < heights.size(); ++i)
                sectionShares[i] = static_cast<double> (heights[i]) / total;
        layoutSections();
        repaint();
        return;
    }

    if (drag == Drag::Seek)
    {
        const int tick = std::clamp (snapTick (xToTick (e.x), noSnap), 0, arrangement.timelineTicks() - 1);
        lastSeekTick = tick;
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

    // The right-button eraser (0.57.0): past a few pixels a right press becomes a stroke that deletes every clip it crosses.
    if (drag == Drag::RightPress && std::hypot (e.x - rightPressX, e.y - rightPressY) > 4.0)
    {
        drag = Drag::RightErase;
        beginEdit(); // the whole stroke is one undo step
        eraseLastX = rightPressX;
        eraseLastY = rightPressY;
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
    }
    if (drag == Drag::RightErase || drag == Drag::EraseSweep)
    {
        if (eraseAlong (eraseLastX, eraseLastY, e.x, e.y))
            notifyChanged();
        eraseLastX = e.x;
        eraseLastY = e.y;
        return;
    }
    if (drag == Drag::MuteSweep) // 0.60.0
    {
        if (muteAlong (eraseLastX, eraseLastY, e.x, e.y, muteSweepState))
            notifyChanged();
        eraseLastX = e.x;
        eraseLastY = e.y;
        return;
    }
    if (drag == Drag::Slip) // 0.60.0
    {
        const int delta = snapTick (xToTick (e.x), noSnap) - snapTick (slipStartTick, noSnap);
        applySlip (delta);
        return;
    }
    if (drag == Drag::PaintSweep) // 0.61.0: a clip wherever the mouse is over empty space on the track
    {
        const int tick = xToTick (e.x);
        if (tick >= 0 && tick < arrangement.timelineTicks() && arrangement.clipAt (paintTrack, tick) < 0 && arrangement.audioClipAt (paintTrack, tick) < 0)
            placeClipAt (paintTrack, tick, noSnap);
        return;
    }
    if (drag == Drag::RubberBand)
    {
        bandEndX = e.x;
        bandEndY = e.y;
        bandActive = bandActive || std::hypot (e.x - bandStartX, e.y - bandStartY) > 4.0;
        if (bandActive)
        {
            // The clips under the rectangle are selected while it is drawn (with Shift, on top of what was selected before).
            auto g = groupInRect (bandRect());
            for (const int i : bandBase.patterns)
                g.patterns.push_back (i);
            for (const int i : bandBase.audio)
                g.audio.push_back (i);
            for (const int i : bandBase.automation)
                g.automation.push_back (i);
            selectGroup (std::move (g));
        }
        repaint();
        return;
    }
    if (drag == Drag::MoveGroup)
    {
        moveGroup (e);
        return;
    }

    if (drag == Drag::ZoomRange)
    {
        dragEndX = e.x;
        repaint();
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
            if (! arrangement.trackLocked (yToTrack (e.y))) // 0.62.0: nothing moves onto a locked track
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
        if (! arrangement.trackLocked (yToTrack (e.y))) // 0.62.0
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
    if (drag == Drag::TrackMove) // 0.82.0: the dragged track goes where it was let go
    {
        drag = Drag::None;
        const int from = trackMoveFrom, to = trackMoveTo;
        trackMoveFrom = trackMoveTo = -1;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
        if (from >= 0 && to >= 0 && from != to)
        {
            beginEdit();
            const auto newIndex = arrangement.moveTracks (from, to); // (a group's head takes its group along: 0.86.0)
            if (newIndex.empty())
            {
                pendingUndoValid = false;
                return;
            }
            commitPendingUndo();
            // the record switches and the selected track go with their tracks
            const auto arms = recordArms;
            for (size_t t = 0; t < newIndex.size() && t < arms.size(); ++t)
                recordArms[static_cast<size_t> (newIndex[t])] = arms[t];
            const int landed = newIndex[static_cast<size_t> (from)];
            clearSelection(); // (the clips' indices did not change, but a group may now span other tracks)
            tracksChanged();
            setSelectedTrack (landed);
            if (onTrackMoved)
                onTrackMoved (newIndex);
            if (onTrackSelected)
                onTrackSelected (landed);
            notifyChanged();
        }
        return;
    }

    if (playbackDrag) // 0.71.0
    {
        playbackDrag = false;
        if (onPlaybackEnd)
            onPlaybackEnd();
        return;
    }
    if (splitterDrag >= 0)
    {
        splitterDrag = -1;
        splitterHover = splitterAt (e.y);
        setMouseCursor (splitterHover >= 0 ? juce::MouseCursor (juce::MouseCursor::UpDownResizeCursor) : toolCursor());
        repaint();
        return;
    }
    if (drag == Drag::RightPress)
    {
        drag = Drag::None;
        if (inLanes (rightPressY))
        {
            if (const int hit = arrangement.automationClipAt (yToLane (rightPressY), xToTick (rightPressX)); hit >= 0)
                showAutomationMenu (hit);
        }
        else if (inTracks (rightPressY))
            showClipMenu (rightPressX, rightPressY);
        return;
    }
    if (drag == Drag::RightErase)
        setMouseCursor (juce::MouseCursor::NormalCursor);
    if (drag == Drag::RubberBand)
    {
        drag = Drag::None;
        if (bandActive)
        {
            bandActive = false;
            repaint();
        }
        else if (! bandAdds)
            clearSelection(); // a click on empty space with the Select tool: nothing selected
        pendingUndoValid = false;
        return;
    }
    if (drag == Drag::MoveGroup && ! dragChanged)
    {
        // A click on one of the selected clips: that clip alone.
        drag = Drag::None;
        pendingUndoValid = false;
        select (groupGrabKind, groupGrabIndex);
        return;
    }
    if (drag == Drag::LoopPress)
    {
        drag = Drag::None;
        showLoopMenu();
        return;
    }
    if (drag == Drag::Slip) // 0.60.0: one undo step when the content moved
    {
        if (dragChanged)
            commitPendingUndo();
        drag = Drag::None;
        dragClip = -1;
        pendingUndoValid = false;
        notifyChanged();
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
    if (e.x < kNameWidth || e.y < kRulerHeight || (tool != PlaylistTool::Draw && tool != PlaylistTool::Select))
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
    // Over the track names, or with Alt anywhere, the wheel scrolls the tracks up and down (0.44.0); 0.69.0: the section under the
    // mouse (the lanes too).
    if (const int section = sectionAt (e.y); section >= 0 && (e.x < kNameWidth || e.mods.isAltDown()))
    {
        sectionScroll[static_cast<size_t> (section)] -= static_cast<int> (std::lround (wheel.deltaY * 3.0f * static_cast<float> (kRowHeight)));
        clampScrollY();
        updateTrackScrollBar();
        repaint();
        return;
    }
    if (e.y >= kRulerHeight && sectionAt (e.y) < 0)
        return; // a bar or a border
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
