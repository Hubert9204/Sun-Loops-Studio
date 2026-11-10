#pragma once

#include <algorithm>
#include <vector>

#include "audio/Project.h"

namespace sls
{
// Which mixer tracks have a channel on them. A track is "free" when no channel is routed to it. Used by the
// "Assign free mixer track" dialog; plain C++ so it can be tested. Since 0.29.0 the number of tracks is the project's own
// (`trackCount`, kDefaultTracks .. kMaxTracks).

// Number of channels routed to each of the `trackCount` tracks. Out-of-range track numbers count as the nearest valid one, as
// Project::make does. `ignoring` is a channel index to leave out (-1: none): the channel being moved does not occupy its own track.
inline std::vector<int> channelsPerTrack (const std::vector<Channel>& channels, int trackCount, int ignoring = -1)
{
    trackCount = std::clamp (trackCount, 1, kMaxTracks);
    std::vector<int> count (static_cast<size_t> (trackCount), 0);
    for (size_t i = 0; i < channels.size(); ++i)
        if (static_cast<int> (i) != ignoring)
            ++count[static_cast<size_t> (std::clamp (channels[i].mixerTrack, 0, trackCount - 1))];
    return count;
}

inline bool isTrackFree (const std::vector<Channel>& channels, int trackCount, int track, int ignoring = -1)
{
    return track >= 0 && track < std::clamp (trackCount, 1, kMaxTracks)
           && channelsPerTrack (channels, trackCount, ignoring)[static_cast<size_t> (track)] == 0;
}

// The lowest-numbered free track, or -1 when every track has a channel on it.
inline int firstFreeTrack (const std::vector<Channel>& channels, int trackCount, int ignoring = -1)
{
    const auto count = channelsPerTrack (channels, trackCount, ignoring);
    for (size_t t = 0; t < count.size(); ++t)
        if (count[t] == 0)
            return static_cast<int> (t);
    return -1;
}
} // namespace sls
