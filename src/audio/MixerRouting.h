#pragma once

#include <vector>

#include "audio/Project.h"

namespace sls
{
// Which mixer tracks have a channel on them. A track is "free" when no channel is routed to it. Used by the
// "Assign free mixer track" dialog; plain C++ so it can be tested.

// Number of channels routed to each track. Out-of-range track numbers count as the nearest valid one, as Project::make
// does. `ignoring` is a channel index to leave out (-1: none): the channel being moved does not occupy its own track.
inline std::array<int, kMaxTracks> channelsPerTrack (const std::vector<Channel>& channels, int ignoring = -1)
{
    std::array<int, kMaxTracks> count {};
    for (size_t i = 0; i < channels.size(); ++i)
        if (static_cast<int> (i) != ignoring)
            ++count[static_cast<size_t> (std::clamp (channels[i].mixerTrack, 0, kMaxTracks - 1))];
    return count;
}

inline bool isTrackFree (const std::vector<Channel>& channels, int track, int ignoring = -1)
{
    return track >= 0 && track < kMaxTracks && channelsPerTrack (channels, ignoring)[static_cast<size_t> (track)] == 0;
}

// The lowest-numbered free track, or -1 when every track has a channel on it.
inline int firstFreeTrack (const std::vector<Channel>& channels, int ignoring = -1)
{
    const auto count = channelsPerTrack (channels, ignoring);
    for (int t = 0; t < kMaxTracks; ++t)
        if (count[static_cast<size_t> (t)] == 0)
            return t;
    return -1;
}
} // namespace sls
