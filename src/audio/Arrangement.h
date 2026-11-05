#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "audio/Project.h"

namespace sls
{
// The song as the user edits it: patterns (note data per channel) and a playlist of clips that place
// patterns on a timeline. It is compiled into a flat Project, so the audio engine and the offline
// renderer know nothing about patterns: they just play notes in a loop.

constexpr int kPlaylistTracks = 8;
constexpr int kPlaylistUnits = 32;     // timeline length; one unit = one pattern length
constexpr int kMaxPatterns = 64;

struct Pattern
{
    std::string name = "Pattern";
    std::vector<std::vector<Note>> notes; // notes[channel], ticks relative to the pattern start
};

// A clip plays `pattern` on `track`, repeating it over `lengthUnits` pattern lengths.
struct Clip
{
    int pattern = 0;
    int track = 0;
    int startUnit = 0;
    int lengthUnits = 1;

    int endUnit() const noexcept { return startUnit + lengthUnits; }
};

struct Arrangement
{
    std::vector<Pattern> patterns;
    std::vector<Clip> clips;
    int patternTicks = 8 * kTicksPerBeat; // length of one pattern = one playlist unit

    // A clip is valid when it lies inside the timeline, refers to an existing pattern and does not
    // overlap another clip on the same track. `ignoreIndex` is the clip being moved/resized (or -1).
    bool canPlace (const Clip& c, int ignoreIndex = -1) const
    {
        if (c.pattern < 0 || c.pattern >= static_cast<int> (patterns.size()))
            return false;
        if (c.track < 0 || c.track >= kPlaylistTracks)
            return false;
        if (c.startUnit < 0 || c.lengthUnits < 1 || c.endUnit() > kPlaylistUnits)
            return false;

        for (int i = 0; i < static_cast<int> (clips.size()); ++i)
        {
            const auto& o = clips[static_cast<size_t> (i)];
            if (i != ignoreIndex && o.track == c.track && c.startUnit < o.endUnit() && o.startUnit < c.endUnit())
                return false;
        }
        return true;
    }

    // Index of the clip covering (track, unit), or -1.
    int clipAt (int track, int unit) const
    {
        for (int i = 0; i < static_cast<int> (clips.size()); ++i)
        {
            const auto& c = clips[static_cast<size_t> (i)];
            if (c.track == track && unit >= c.startUnit && unit < c.endUnit())
                return i;
        }
        return -1;
    }

    // Number of units up to the end of the last clip (0 for an empty playlist).
    int songLengthUnits() const
    {
        int end = 0;
        for (const auto& c : clips)
            end = std::max (end, c.endUnit());
        return end;
    }

    // Make every pattern hold one note list per channel (after channels were added).
    void ensureChannelCount (size_t numChannels)
    {
        for (auto& p : patterns)
            p.notes.resize (numChannels);
    }

    // Remove clips that refer to a pattern that no longer exists.
    void dropInvalidClips()
    {
        clips.erase (std::remove_if (clips.begin(), clips.end(),
                                     [this] (const Clip& c)
                                     { return c.pattern < 0 || c.pattern >= static_cast<int> (patterns.size()); }),
                     clips.end());
    }
};

// Loop of a single pattern: what "pattern mode" plays.
inline Project compilePattern (const std::vector<Channel>& channelSettings, const Arrangement& arrangement,
                               int patternIndex)
{
    auto channels = channelSettings;
    const bool valid = patternIndex >= 0 && patternIndex < static_cast<int> (arrangement.patterns.size());

    for (size_t c = 0; c < channels.size(); ++c)
    {
        channels[c].notes.clear();
        if (valid)
        {
            const auto& notes = arrangement.patterns[static_cast<size_t> (patternIndex)].notes;
            if (c < notes.size())
                channels[c].notes = notes[c];
        }
    }
    return Project::make (std::move (channels), arrangement.patternTicks);
}

// The whole playlist laid out on one timeline: what "song mode" plays. Loops over the end of the last clip
// (an empty playlist is a silent loop of one pattern length).
inline Project compileSong (const std::vector<Channel>& channelSettings, const Arrangement& arrangement)
{
    auto channels = channelSettings;
    for (auto& c : channels)
        c.notes.clear();

    const int units = std::max (1, arrangement.songLengthUnits());

    for (const auto& clip : arrangement.clips)
    {
        if (clip.pattern < 0 || clip.pattern >= static_cast<int> (arrangement.patterns.size()))
            continue;

        const auto& notes = arrangement.patterns[static_cast<size_t> (clip.pattern)].notes;
        for (int repeat = 0; repeat < clip.lengthUnits; ++repeat)
        {
            const int offset = (clip.startUnit + repeat) * arrangement.patternTicks;
            for (size_t c = 0; c < channels.size() && c < notes.size(); ++c)
                for (auto n : notes[c])
                {
                    if (n.startTick < 0 || n.startTick >= arrangement.patternTicks)
                        continue; // outside its pattern: would leak into the next unit
                    n.lengthTicks = std::min (n.lengthTicks, arrangement.patternTicks - n.startTick);
                    n.startTick += offset;
                    channels[c].notes.push_back (n);
                }
        }
    }
    return Project::make (std::move (channels), units * arrangement.patternTicks);
}
} // namespace sls
