#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "audio/Effects.h"
#include "audio/NoteSequence.h"

namespace sls
{
// Automation: a parameter that follows a drawn curve while the song plays. Everything here is free of JUCE and of the GUI.
//
// An automation clip lives on one of kAutomationLanes lanes under the tracks of the playlist, starts at a tick and
// lasts a number of ticks. It holds points (a position inside the clip, a value in the parameter's own unit, and the
// shape of the line that leaves the point). Where clips of the same parameter follow each other, the later one takes
// over at its start; after the last clip of a parameter its final value is held. Automation plays in Song mode only.

constexpr int kAutomationLanes = 4;
constexpr int kMaxAutomationClips = 64;
constexpr int kMaxAutomationPoints = 256;

inline double finiteOrZero (double v) noexcept { return std::isfinite (v) ? v : 0.0; }

// The three plugin kinds (0.35.0) move a parameter of a third-party plugin: the instrument of a channel, or an effect in a mixer
// slot. Their values are the plugin's normalised 0..1 (what the plugin shows for them is the plugin's business).
enum class AutomationKind : std::uint8_t { ChannelVolume, ChannelPan, TrackVolume, TrackPan, TrackEffect, MasterEffect,
                                           ChannelPlugin, TrackPlugin, MasterPlugin };
constexpr int kAutomationKindCount = 9;
constexpr int kMaxPluginParams = 65536; // a parameter number beyond this is not a plugin's

inline bool isPluginAutomation (AutomationKind k) noexcept
{
    return k == AutomationKind::ChannelPlugin || k == AutomationKind::TrackPlugin || k == AutomationKind::MasterPlugin;
}

// What an automation clip moves. `index` is the channel or the mixer track, `slot` and `param` apply to effects and plugins.
struct AutomationTarget
{
    AutomationKind kind = AutomationKind::TrackVolume;
    int index = 0;
    int slot = 0;
    int param = 0;

    bool operator== (const AutomationTarget& o) const noexcept
    {
        return kind == o.kind && index == o.index && slot == o.slot && param == o.param;
    }

    // One number per parameter, for sorting and for finding clips of the same parameter (wide enough for 500 tracks and the
    // parameters of any plugin: before 0.35.0 a track number from 16 up could share its number with another kind's).
    std::int64_t key() const noexcept
    {
        return ((static_cast<std::int64_t> (kind) * 1024 + (index & 1023)) * 64 + (slot & 63)) * kMaxPluginParams + (param & (kMaxPluginParams - 1));
    }
};

struct AutomationRange
{
    float min = 0.0f, max = 1.0f, def = 0.0f;
};

// The range a target moves in (it must match what the controls allow). `effectType` is the effect that sits in the slot
// (needed for the two effect kinds, ignored for the others).
inline AutomationRange automationRange (const AutomationTarget& t, EffectType effectType = EffectType::None) noexcept
{
    switch (t.kind)
    {
        case AutomationKind::ChannelVolume: return { 0.0f, 1.5f, 0.8f };
        case AutomationKind::TrackVolume:   return { 0.0f, 1.5f, 1.0f };
        case AutomationKind::ChannelPan:
        case AutomationKind::TrackPan:      return { -1.0f, 1.0f, 0.0f };
        case AutomationKind::TrackEffect:
        case AutomationKind::MasterEffect:
        {
            const auto info = effectParamInfo (effectType, t.param);
            return { info.min, info.max, info.def };
        }
        case AutomationKind::ChannelPlugin:
        case AutomationKind::TrackPlugin:
        case AutomationKind::MasterPlugin:  return { 0.0f, 1.0f, 0.5f };
    }
    return {};
}

// Is this a parameter that exists (channel and track numbers in range, slot and parameter inside the effect)?
inline bool automationTargetExists (const AutomationTarget& t, int numChannels, int numTracks) noexcept
{
    switch (t.kind)
    {
        case AutomationKind::ChannelVolume:
        case AutomationKind::ChannelPan:    return t.index >= 0 && t.index < numChannels;
        case AutomationKind::TrackVolume:
        case AutomationKind::TrackPan:      return t.index >= 0 && t.index < numTracks;
        case AutomationKind::TrackEffect:   return t.index >= 0 && t.index < numTracks && t.slot >= 0 && t.slot < kSlotsPerTrack
                                                   && t.param >= 0 && t.param < kMaxEffectParams;
        case AutomationKind::MasterEffect:  return t.slot >= 0 && t.slot < kSlotsPerTrack && t.param >= 0 && t.param < kMaxEffectParams;
        case AutomationKind::ChannelPlugin: return t.index >= 0 && t.index < numChannels && t.param >= 0 && t.param < kMaxPluginParams;
        case AutomationKind::TrackPlugin:   return t.index >= 0 && t.index < numTracks && t.slot >= 0 && t.slot < kSlotsPerTrack
                                                   && t.param >= 0 && t.param < kMaxPluginParams;
        case AutomationKind::MasterPlugin:  return t.slot >= 0 && t.slot < kSlotsPerTrack && t.param >= 0 && t.param < kMaxPluginParams;
    }
    return false;
}

enum class AutomationShape : std::uint8_t { Linear, Hold, Smooth };

struct AutomationPoint
{
    double tick = 0.0;  // inside the clip, in ticks from its start; any real number (not forced onto a grid)
    float value = 0.0f; // in the parameter's own unit
    AutomationShape shape = AutomationShape::Linear; // the line from this point to the next
};

struct AutomationClip
{
    AutomationTarget target;
    int lane = 0;
    int startTick = 0;
    int lengthTicks = 8 * kTicksPerBeat; // one pattern
    std::vector<AutomationPoint> points; // sorted by tick

    int endTick() const noexcept { return startTick + lengthTicks; }
};

// The value of a curve at `tick` (from the start of the clip). Before the first point it is the first value, after the
// last point the last value; an empty curve gives `fallback`.
inline float automationValueAt (const std::vector<AutomationPoint>& points, double tick, float fallback) noexcept
{
    if (points.empty())
        return fallback;
    if (! (tick > points.front().tick))
        return points.front().value;
    if (tick >= points.back().tick)
        return points.back().value;

    // The first point after `tick`.
    const auto next = std::upper_bound (points.begin(), points.end(), tick,
                                        [] (double t, const AutomationPoint& p) { return t < p.tick; });
    const auto& a = *(next - 1);
    const auto& b = *next;
    const double span = b.tick - a.tick;
    if (span <= 0.0)
        return b.value;

    const double t = (tick - a.tick) / span;
    switch (a.shape)
    {
        case AutomationShape::Hold:   return a.value;
        case AutomationShape::Smooth: return a.value + static_cast<float> ((3.0 * t * t - 2.0 * t * t * t) * (b.value - a.value));
        case AutomationShape::Linear: break;
    }
    return a.value + static_cast<float> (t * (b.value - a.value));
}

// What the audio thread gets: a clip with validated points, in the order the player walks them.
struct AutomationEvent
{
    AutomationTarget target;
    int startTick = 0;
    int endTick = 0;
    std::vector<AutomationPoint> points;
};

// Sorts by parameter and start, drops what cannot play (missing parameter, no usable point, no length), keeps the points
// in range and in order. `typeAt` says which effect sits in a slot (it gives the range of an effect parameter);
// `pluginParams` how many parameters the plugin a plugin target points at has (0: no plugin there).
template <typename SlotTypeFn, typename PluginParamsFn>
std::vector<AutomationEvent> makeAutomationEvents (const std::vector<AutomationClip>& clips, int numChannels, int numTracks,
                                                   SlotTypeFn typeAt, PluginParamsFn pluginParams)
{
    std::vector<AutomationEvent> out;
    for (const auto& c : clips)
    {
        if (c.startTick < 0 || c.lengthTicks < 1 || ! automationTargetExists (c.target, numChannels, numTracks))
            continue;
        if ((c.target.kind == AutomationKind::TrackEffect || c.target.kind == AutomationKind::MasterEffect)
            && (c.target.param >= effectParamCount (typeAt (c.target)) ))
            continue; // no such parameter in the effect that sits there now
        if (isPluginAutomation (c.target.kind) && c.target.param >= pluginParams (c.target))
            continue; // no plugin there now, or one without that parameter

        const auto range = automationRange (c.target, typeAt (c.target));
        AutomationEvent e;
        e.target = c.target;
        e.startTick = c.startTick;
        e.endTick = c.endTick();
        for (const auto& p : c.points)
        {
            if (! std::isfinite (p.tick) || ! std::isfinite (p.value) || static_cast<int> (p.shape) > 2)
                continue;
            AutomationPoint q = p;
            q.tick = std::clamp (p.tick, 0.0, static_cast<double> (c.lengthTicks));
            q.value = std::clamp (p.value, range.min, range.max);
            e.points.push_back (q);
            if (static_cast<int> (e.points.size()) >= kMaxAutomationPoints)
                break;
        }
        if (e.points.empty())
            continue;
        std::stable_sort (e.points.begin(), e.points.end(),
                          [] (const AutomationPoint& a, const AutomationPoint& b) { return a.tick < b.tick; });
        out.push_back (std::move (e));
    }
    std::stable_sort (out.begin(), out.end(),
                      [] (const AutomationEvent& a, const AutomationEvent& b)
                      { return a.target.key() != b.target.key() ? a.target.key() < b.target.key() : a.startTick < b.startTick; });
    return out;
}

// The value of one parameter at a song position: the clip of that parameter that started last, read at the position (or at
// its end, once the clip is over). Returns false if the parameter has no clip that has started yet. `events` is sorted as
// makeAutomationEvents() leaves it; [first, last) is the range of one parameter.
inline bool automationValueFor (const std::vector<AutomationEvent>& events, size_t first, size_t last, double songTick, float& value) noexcept
{
    const AutomationEvent* active = nullptr;
    for (size_t i = first; i < last; ++i)
    {
        if (static_cast<double> (events[i].startTick) > songTick)
            break;
        active = &events[i];
    }
    if (active == nullptr)
        return false;
    const double rel = std::min (songTick, static_cast<double> (active->endTick)) - static_cast<double> (active->startTick);
    value = automationValueAt (active->points, rel, 0.0f);
    return true;
}
// ---- Editing (used by the automation editor; all of it works on plain data) -------------------------------------------

// A clip that holds `value` all the way: two points, one at each end.
inline AutomationClip makeFlatAutomationClip (const AutomationTarget& target, int lane, int startTick, int lengthTicks, float value)
{
    AutomationClip c;
    c.target = target;
    c.lane = lane;
    c.startTick = startTick;
    c.lengthTicks = std::max (1, lengthTicks);
    c.points = { { 0.0, value, AutomationShape::Linear }, { static_cast<double> (c.lengthTicks), value, AutomationShape::Linear } };
    return c;
}

// Adds a point, keeping the points in order (a point at the same tick as another goes after it). Returns its index, or -1 if
// the clip already has the most points it may have.
inline int insertAutomationPoint (AutomationClip& clip, double tick, float value, const AutomationRange& range,
                                  AutomationShape shape = AutomationShape::Linear)
{
    if (static_cast<int> (clip.points.size()) >= kMaxAutomationPoints)
        return -1;
    AutomationPoint p;
    p.tick = std::clamp (finiteOrZero (tick), 0.0, static_cast<double> (clip.lengthTicks));
    p.value = std::clamp (value, range.min, range.max);
    p.shape = shape;
    const auto at = std::upper_bound (clip.points.begin(), clip.points.end(), p.tick,
                                      [] (double t, const AutomationPoint& q) { return t < q.tick; });
    const int index = static_cast<int> (at - clip.points.begin()); // before inserting: insert() invalidates the iterators
    clip.points.insert (at, p);
    return index;
}

// Moves a point; the points stay in order (it cannot jump over its neighbours: it stops at them). Returns its index.
inline int moveAutomationPoint (AutomationClip& clip, int index, double tick, float value, const AutomationRange& range)
{
    if (index < 0 || index >= static_cast<int> (clip.points.size()))
        return index;
    const double lo = index > 0 ? clip.points[static_cast<size_t> (index) - 1].tick : 0.0;
    const double hi = index + 1 < static_cast<int> (clip.points.size()) ? clip.points[static_cast<size_t> (index) + 1].tick
                                                                          : static_cast<double> (clip.lengthTicks);
    auto& p = clip.points[static_cast<size_t> (index)];
    p.tick = std::clamp (finiteOrZero (tick), lo, hi);
    p.value = std::clamp (value, range.min, range.max);
    return index;
}

// Removes a point; a clip keeps at least one.
inline bool removeAutomationPoint (AutomationClip& clip, int index)
{
    if (index < 0 || index >= static_cast<int> (clip.points.size()) || clip.points.size() <= 1)
        return false;
    clip.points.erase (clip.points.begin() + index);
    return true;
}

// Changes the length of a clip (from its end); points beyond the new end are moved onto it (the last value is kept).
inline void setAutomationLength (AutomationClip& clip, int newLengthTicks)
{
    clip.lengthTicks = std::max (1, newLengthTicks);
    for (auto& p : clip.points)
        p.tick = std::min (p.tick, static_cast<double> (clip.lengthTicks));
}
} // namespace sls
