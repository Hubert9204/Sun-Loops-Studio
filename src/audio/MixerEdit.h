#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include "audio/Arrangement.h"
#include "audio/Effects.h"
#include "audio/Project.h"

namespace sls
{
// 0.79.0: deleting any mixer track, not only the last one. Everything that names mixer tracks by number follows: the tracks
// after it move down one place, and so do the channels routed to them, the other tracks' sends to them, the effects that point at
// them (a compressor's, a gate's or a vocoder's sidechain, a controller's targets: every parameter in "track"), the automation
// clips that move them, and the playlist tracks whose
// audio plays through them (which then name their mixer track: see PlaylistTrack::mixerTrack). What played through the deleted
// track itself goes to its heir (see mixerTrackHeir). Plain C++, tested.

inline bool isTrackAutomation (AutomationKind k) noexcept
{
    return k == AutomationKind::TrackVolume || k == AutomationKind::TrackPan || k == AutomationKind::TrackEffect
        || k == AutomationKind::TrackPlugin || k == AutomationKind::TrackSend || k == AutomationKind::TrackMute || k == AutomationKind::TrackSolo;
}

// What uses mixer track `k`: the question asked before it is deleted lists these.
struct MixerTrackUse
{
    std::vector<int> channels;        // channels routed to it
    std::vector<int> playlistTracks;  // playlist tracks holding audio clips that play through it
    int effects = 0;                  // effect slots in use on it
    int automationClips = 0;          // automation clips that move it
    std::vector<int> feeders;         // other mixer tracks sending to it
    int listeners = 0;                // effects (anywhere) pointing at this track: a sidechain, a controller's target
    int pads = 0;                     // 0.84.0: drum pads with it as their own output (0.85.0: and plugin outputs after the main pair)

    bool empty() const noexcept
    {
        return channels.empty() && playlistTracks.empty() && effects == 0 && automationClips == 0 && feeders.empty() && listeners == 0 && pads == 0;
    }
};

namespace detail
{
// 0.84.0: gives every drum pad with its own mixer track the track `renumber` says (-1: back to its channel's track). A kit is shared
// and never changed in place: a channel whose pads change gets a copy.
template <typename Renumber>
void renumberPadOutputs (std::vector<Channel>& channels, Renumber renumber)
{
    for (auto& c : channels)
    {
        for (auto& out : c.pluginOutputs) // 0.85.0: a plugin's other outputs, the same way
            if (out >= 0)
                out = renumber (out);
        if (c.kit == nullptr)
            continue;
        auto kit = *c.kit;
        bool changed = false;
        for (auto& p : kit.pads)
            if (p.outputTrack >= 0)
            {
                const int now = renumber (p.outputTrack);
                changed = changed || now != p.outputTrack;
                p.outputTrack = now;
            }
        if (changed)
            c.kit = std::make_shared<const DrumKit> (std::move (kit));
    }
}
} // namespace detail

namespace detail
{
// Every effect slot of the mixer (the tracks', the aux buses', the master's), the effects inside a Patcher included.
template <typename Fn>
void forEachSlot (std::vector<MixerTrack>& tracks, Console& console, std::array<EffectSlot, kSlotsPerTrack>& masterSlots, Fn&& fn)
{
    const auto visit = [&] (EffectSlot& slot)
    {
        fn (slot);
        if (slot.type == EffectType::Patcher && slot.patch != nullptr)
        {
            auto copy = *slot.patch;
            bool changed = false;
            for (auto& sub : copy)
            {
                const auto before = sub.params;
                fn (sub);
                changed = changed || before != sub.params;
            }
            if (changed) // (the same effects: the Patcher's state still fits them)
                slot.patch = std::make_shared<const std::vector<EffectSlot>> (std::move (copy));
        }
    };
    for (auto& t : tracks)
        for (auto& slot : t.slots)
            visit (slot);
    for (auto& bus : console.aux)
        for (auto& slot : bus.slots)
            visit (slot);
    for (auto& slot : masterSlots)
        visit (slot);
}

// The parameters that name a mixer track (unit "track": 1-based, 0 = off / this track): a sidechain, a controller's targets.
// (0.85.0: a plugin's slot has one, its sidechain source, in params[0])
inline int trackParamCount (EffectType type) noexcept { return type == EffectType::Plugin ? 1 : effectParamCount (type); }
inline bool isTrackParam (EffectType type, int param) noexcept
{
    if (type == EffectType::Plugin)
        return param == 0;
    return param >= 0 && param < effectParamCount (type) && std::string_view (effectParamInfo (type, param).unit) == "track";
}
// The track (0-based) such a parameter names, or -1 (off, this track, or not a number).
inline int trackParamValue (const EffectSlot& slot, int param) noexcept
{
    const float v = slot.params[static_cast<size_t> (param)];
    return std::isfinite (v) ? static_cast<int> (std::lround (v)) - 1 : -1;
}
// How many of a slot's track parameters name track `k`.
inline int pointsAt (const EffectSlot& slot, int k) noexcept
{
    int n = 0;
    for (int i = 0; i < trackParamCount (slot.type); ++i)
        n += isTrackParam (slot.type, i) && trackParamValue (slot, i) == k ? 1 : 0;
    return n;
}
} // namespace detail

inline MixerTrackUse mixerTrackUse (const std::vector<MixerTrack>& tracks, const std::vector<Channel>& channels, const Arrangement& arrangement,
                                    const Console& console, const std::array<EffectSlot, kSlotsPerTrack>& masterSlots, int k)
{
    MixerTrackUse use;
    const int count = static_cast<int> (tracks.size());
    if (k < 0 || k >= count)
        return use;
    for (size_t c = 0; c < channels.size(); ++c)
    {
        if (std::clamp (channels[c].mixerTrack, 0, count - 1) == k)
            use.channels.push_back (static_cast<int> (c));
        if (channels[c].kit != nullptr)
            for (const auto& p : channels[c].kit->pads)
                use.pads += p.outputTrack == k ? 1 : 0;
        for (const int out : channels[c].pluginOutputs)
            use.pads += out == k ? 1 : 0;
    }
    for (int t = 0; t < arrangement.trackCount(); ++t)
        if (std::clamp (arrangement.mixerTrackOf (t), 0, count - 1) == k
            && std::any_of (arrangement.audioClips.begin(), arrangement.audioClips.end(), [t] (const AudioClip& a) { return a.track == t; }))
            use.playlistTracks.push_back (t);
    for (const auto& slot : tracks[static_cast<size_t> (k)].slots)
        use.effects += slot.type != EffectType::None ? 1 : 0;
    for (const auto& a : arrangement.automationClips)
        use.automationClips += isTrackAutomation (a.target.kind) && a.target.index == k ? 1 : 0;
    for (int t = 0; t < count; ++t)
        if (t != k && tracks[static_cast<size_t> (t)].sendsTo (k))
            use.feeders.push_back (t);
    auto tracksCopy = tracks;
    auto consoleCopy = console;
    auto masterCopy = masterSlots;
    detail::forEachSlot (tracksCopy, consoleCopy, masterCopy, [&] (EffectSlot& slot) { use.listeners += detail::pointsAt (slot, k) > 0 ? 1 : 0; });
    return use;
}

// Where what played through track `k` goes when it is deleted, as a track number before the deletion: the first other track that
// nothing uses (no channel, no audio, no effects, nothing sent to it), else the track before it (the one after it for the first).
inline int mixerTrackHeir (const std::vector<MixerTrack>& tracks, const std::vector<Channel>& channels, const Arrangement& arrangement, int k)
{
    const int count = static_cast<int> (tracks.size());
    for (int t = 0; t < count; ++t)
    {
        if (t == k)
            continue;
        bool used = std::any_of (tracks[static_cast<size_t> (t)].slots.begin(), tracks[static_cast<size_t> (t)].slots.end(),
                                 [] (const EffectSlot& s) { return s.type != EffectType::None; });
        for (const auto& c : channels)
        {
            used = used || std::clamp (c.mixerTrack, 0, count - 1) == t;
            if (c.kit != nullptr)
                for (const auto& p : c.kit->pads)
                    used = used || p.outputTrack == t; // (0.84.0)
            for (const int out : c.pluginOutputs)
                used = used || out == t; // (0.85.0)
        }
        for (int p = 0; p < arrangement.trackCount() && ! used; ++p)
            used = std::clamp (arrangement.mixerTrackOf (p), 0, count - 1) == t
                && std::any_of (arrangement.audioClips.begin(), arrangement.audioClips.end(), [p] (const AudioClip& a) { return a.track == p; });
        for (int o = 0; o < count && ! used; ++o)
            used = o != k && tracks[static_cast<size_t> (o)].sendsTo (t);
        if (! used)
            return t;
    }
    return k > 0 ? k - 1 : std::min (1, count - 1);
}

// Deletes mixer track `k` (see the top of this file). False (and nothing changes) for a bad index or when the mixer has only
// kDefaultTracks tracks. `heir` is mixerTrackHeir's answer (or any other track but `k`), in the numbers before the deletion.
inline bool deleteMixerTrack (std::vector<MixerTrack>& tracks, std::vector<Channel>& channels, Arrangement& arrangement, Console& console,
                              std::array<EffectSlot, kSlotsPerTrack>& masterSlots, int k, int heir)
{
    const int count = static_cast<int> (tracks.size());
    if (k < 0 || k >= count || count <= kDefaultTracks || heir < 0 || heir >= count || heir == k)
        return false;
    const auto renumber = [k, heir] (int old) { return old == k ? (heir < k ? heir : heir - 1) : (old < k ? old : old - 1); };

    for (auto& c : channels)
        c.mixerTrack = renumber (std::clamp (c.mixerTrack, 0, count - 1));
    // 0.84.0: a pad whose own track goes plays through its channel again
    detail::renumberPadOutputs (channels, [k, count] (int old) { return old == k || old >= count ? -1 : (old < k ? old : old - 1); });

    // the playlist's audio: each playlist track keeps its mixer track (named when the same number no longer gives it)
    const int newLast = count - 2;
    for (int t = 0; t < arrangement.trackCount(); ++t)
    {
        auto& pt = arrangement.playlistTracks[static_cast<size_t> (t)];
        const int now = renumber (std::clamp (arrangement.mixerTrackOf (t), 0, count - 1));
        pt.mixerTrack = std::clamp (t, 0, newLast) == now ? -1 : now;
    }

    for (int t = 0; t < count; ++t)
    {
        if (t == k)
            continue;
        auto& track = tracks[static_cast<size_t> (t)];
        bool sends = false, removed = false;
        for (auto& s : track.trackSends)
        {
            if (s.target == k)
            {
                s = {};
                removed = true;
            }
            else if (s.target > k)
                --s.target;
            sends = sends || s.target >= 0;
        }
        if (removed && ! sends && ! track.toMaster)
            track.toMaster = true; // it went only to the deleted track: now to the master, not nowhere
        if (t > k && track.name == "Track " + std::to_string (t + 1))
            track.name = "Track " + std::to_string (t); // a default name follows the number
    }

    detail::forEachSlot (tracks, console, masterSlots,
                         [k] (EffectSlot& slot)
                         {
                             const int target = effectControllerTarget (slot.type), target2 = effectControllerTarget2 (slot.type);
                             for (int i = 0; i < detail::trackParamCount (slot.type); ++i)
                             {
                                 if (! detail::isTrackParam (slot.type, i))
                                     continue;
                                 const int named = detail::trackParamValue (slot, i);
                                 if (named < k)
                                     continue;
                                 auto& value = slot.params[static_cast<size_t> (i)];
                                 if (named > k)
                                 {
                                     value = static_cast<float> (named); // one track lower (1-based: named - 1 + 1)
                                     continue;
                                 }
                                 value = 0.0f; // a sidechain: off
                                 if (i == target)
                                     slot.bypass = true; // a controller's target is gone (0 would be "this track": another effect)
                                 else if (i == target2 && i + 1 < effectParamCount (slot.type))
                                     slot.params[static_cast<size_t> (i + 1)] = 0.0f; // its second target: none (slot 0)
                             }
                         });

    auto& clips = arrangement.automationClips;
    clips.erase (std::remove_if (clips.begin(), clips.end(),
                                 [k] (AutomationClip& c)
                                 {
                                     if (! isTrackAutomation (c.target.kind))
                                         return false;
                                     if (c.target.index == k)
                                         return true;
                                     if (c.target.index > k)
                                         --c.target.index;
                                     return false;
                                 }),
                 clips.end());

    tracks.erase (tracks.begin() + k);
    return true;
}
// 0.82.0: moves mixer track `from` to place `to` (dragging its strip, as in FL Studio): the tracks between move one place, and
// everything that names tracks by number follows (channels, sends, the effects that point at tracks, automation, the playlist's
// audio, default names). Nothing is lost. False (and nothing changes) for a bad index.
inline bool moveMixerTrack (std::vector<MixerTrack>& tracks, std::vector<Channel>& channels, Arrangement& arrangement, Console& console,
                            std::array<EffectSlot, kSlotsPerTrack>& masterSlots, int from, int to)
{
    const int count = static_cast<int> (tracks.size());
    if (from < 0 || from >= count || to < 0 || to >= count || from == to)
        return false;
    const auto renumber = [from, to] (int t)
    {
        if (t == from)
            return to;
        if (from < to && t > from && t <= to)
            return t - 1;
        if (to < from && t >= to && t < from)
            return t + 1;
        return t;
    };
    for (auto& c : channels)
        c.mixerTrack = renumber (std::clamp (c.mixerTrack, 0, count - 1));
    detail::renumberPadOutputs (channels, [&renumber, count] (int old) { return old >= count ? -1 : renumber (old); }); // 0.84.0
    const int last = count - 1;
    for (int t = 0; t < arrangement.trackCount(); ++t)
    {
        auto& pt = arrangement.playlistTracks[static_cast<size_t> (t)];
        const int now = renumber (std::clamp (arrangement.mixerTrackOf (t), 0, last));
        pt.mixerTrack = std::clamp (t, 0, last) == now ? -1 : now;
    }
    for (auto& track : tracks)
        for (auto& s : track.trackSends)
            if (s.target >= 0 && s.target < count)
                s.target = renumber (s.target);
    detail::forEachSlot (tracks, console, masterSlots,
                         [&renumber, count] (EffectSlot& slot)
                         {
                             for (int i = 0; i < detail::trackParamCount (slot.type); ++i)
                                 if (detail::isTrackParam (slot.type, i))
                                     if (const int named = detail::trackParamValue (slot, i); named >= 0 && named < count)
                                         slot.params[static_cast<size_t> (i)] = static_cast<float> (renumber (named) + 1);
                         });
    for (auto& c : arrangement.automationClips)
        if (isTrackAutomation (c.target.kind) && c.target.index >= 0 && c.target.index < count)
            c.target.index = renumber (c.target.index);
    // default names follow the numbers ("Track 3" taken to the first place is "Track 1")
    for (int t = 0; t < count; ++t)
        if (tracks[static_cast<size_t> (t)].name == "Track " + std::to_string (t + 1))
            tracks[static_cast<size_t> (t)].name = "Track " + std::to_string (renumber (t) + 1);
    const auto moving = std::move (tracks[static_cast<size_t> (from)]);
    tracks.erase (tracks.begin() + from);
    tracks.insert (tracks.begin() + to, moving);
    return true;
}
} // namespace sls
