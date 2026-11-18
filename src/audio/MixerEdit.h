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

    bool empty() const noexcept
    {
        return channels.empty() && playlistTracks.empty() && effects == 0 && automationClips == 0 && feeders.empty() && listeners == 0;
    }
};

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
inline bool isTrackParam (EffectType type, int param) noexcept
{
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
    for (int i = 0; i < effectParamCount (slot.type); ++i)
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
        if (std::clamp (channels[c].mixerTrack, 0, count - 1) == k)
            use.channels.push_back (static_cast<int> (c));
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
            used = used || std::clamp (c.mixerTrack, 0, count - 1) == t;
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
                             for (int i = 0; i < effectParamCount (slot.type); ++i)
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
} // namespace sls
