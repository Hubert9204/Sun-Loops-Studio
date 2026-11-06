#pragma once

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "audio/Automation.h"
#include "audio/TimeStretch.h"
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

constexpr int kMaxAudioClips = 64;

// A recording or an imported audio file on a playlist track. Unlike a pattern clip its position (`startTick`) and
// length are in ticks, not in whole patterns: it plays the audio once and is cut at its end.
// The playlist track number is also the mixer track the audio plays through.
struct AudioClip
{
    std::shared_ptr<const SampleData> data; // the audio that plays, after `stretch`; holds the file path in `source` (empty frames = file missing)
    std::shared_ptr<const SampleData> original; // the audio of the file itself; only set while `stretch` is not neutral
    StretchSettings stretch;                    // time-stretch / pitch-shift applied to `original` to make `data`
    std::string name;
    int track = 0;
    int startTick = 0;
    int lengthTicks = kTicksPerBeat;
    float gain = 1.0f; // 0..2
    double offsetSeconds = 0.0; // the clip plays the audio from this point (0 unless the clip was cut or trimmed at its start)

    int endTick() const noexcept { return startTick + lengthTicks; }

    // How much audio there is from the clip's start to the end of the file, in ticks at `bpm`. A clip cannot be
    // made longer than this.
    int naturalLengthTicks (double bpm) const noexcept
    {
        if (data == nullptr || data->sampleRate <= 0.0 || bpm <= 0.0)
            return kTicksPerBeat;
        const double seconds = std::max (0.0, data->numFrames() / data->sampleRate - offsetSeconds);
        return std::max (1, static_cast<int> (std::ceil (seconds * bpm / 60.0 * kTicksPerBeat)));
    }
};

struct Arrangement
{
    std::vector<Pattern> patterns;
    std::vector<Clip> clips;
    std::vector<AudioClip> audioClips;
    int patternTicks = 8 * kTicksPerBeat; // length of one pattern = one playlist unit
    std::vector<AutomationClip> automationClips; // on the lanes under the tracks; they play in Song mode

    int timelineTicks() const noexcept { return kPlaylistUnits * patternTicks; }

    // Does an audio clip fit: inside the timeline, on an existing track, and clear of every other clip
    // (pattern or audio) on that track? (Audio clips start on any tick: the playlist's snap setting decides how coarse
    // the positions you can drag them to are.) `ignoreIndex` is the audio clip being moved/resized (or -1).
    bool canPlaceAudio (const AudioClip& c, int ignoreIndex = -1) const
    {
        if (c.data == nullptr || c.track < 0 || c.track >= kPlaylistTracks)
            return false;
        if (c.startTick < 0 || c.lengthTicks < 1 || c.endTick() > timelineTicks() || ! std::isfinite (c.offsetSeconds) || c.offsetSeconds < 0.0)
            return false;

        for (int i = 0; i < static_cast<int> (audioClips.size()); ++i)
        {
            const auto& o = audioClips[static_cast<size_t> (i)];
            if (i != ignoreIndex && o.track == c.track && c.startTick < o.endTick() && o.startTick < c.endTick())
                return false;
        }
        for (const auto& o : clips)
            if (o.track == c.track && c.startTick < o.endUnit() * patternTicks && o.startUnit * patternTicks < c.endTick())
                return false;
        return true;
    }

    // Does an automation clip fit: on an existing lane, inside the timeline, with at least one point, and clear of the other
    // automation clips on its lane? `ignoreIndex` is the clip being moved or resized (or -1).
    bool canPlaceAutomation (const AutomationClip& c, int ignoreIndex = -1) const
    {
        if (c.lane < 0 || c.lane >= kAutomationLanes || c.points.empty())
            return false;
        if (c.startTick < 0 || c.lengthTicks < 1 || c.endTick() > timelineTicks())
            return false;
        for (int i = 0; i < static_cast<int> (automationClips.size()); ++i)
        {
            const auto& o = automationClips[static_cast<size_t> (i)];
            if (i != ignoreIndex && o.lane == c.lane && c.startTick < o.endTick() && o.startTick < c.endTick())
                return false;
        }
        return true;
    }

    // Index of the automation clip covering (lane, tick), or -1.
    int automationClipAt (int lane, int tick) const
    {
        for (int i = 0; i < static_cast<int> (automationClips.size()); ++i)
        {
            const auto& c = automationClips[static_cast<size_t> (i)];
            if (c.lane == lane && tick >= c.startTick && tick < c.endTick())
                return i;
        }
        return -1;
    }

    // The first lane where an automation clip of `lengthTicks` fits at `startTick`, or -1.
    int findFreeAutomationLane (int startTick, int lengthTicks) const
    {
        AutomationClip probe;
        probe.points = { AutomationPoint {} };
        probe.startTick = startTick;
        probe.lengthTicks = lengthTicks;
        for (int lane = 0; lane < kAutomationLanes; ++lane)
        {
            probe.lane = lane;
            if (canPlaceAutomation (probe))
                return lane;
        }
        return -1;
    }

    // Index of the audio clip covering (track, tick), or -1.
    int audioClipAt (int track, int tick) const
    {
        for (int i = 0; i < static_cast<int> (audioClips.size()); ++i)
        {
            const auto& c = audioClips[static_cast<size_t> (i)];
            if (c.track == track && tick >= c.startTick && tick < c.endTick())
                return i;
        }
        return -1;
    }

    // The longest an audio clip starting at `startTick` on `track` can be (up to `limit` ticks), or 0 if not even a
    // beat fits there.
    int maxAudioLengthAt (int track, int startTick, int limit) const
    {
        AudioClip probe;
        probe.data = std::make_shared<SampleData>();
        probe.track = track;
        probe.startTick = startTick;
        int best = 0;
        for (int length = kTicksPerBeat; length < limit + kTicksPerBeat; length += kTicksPerBeat)
        {
            probe.lengthTicks = std::min (length, limit);
            if (! canPlaceAudio (probe))
                break;
            best = probe.lengthTicks;
            if (length >= limit)
                break;
        }
        return best;
    }

    // `fromTick` itself if an audio clip of `lengthTicks` fits there on `track`, otherwise the first beat after it
    // where one does; -1 if there is no such place.
    int findFreeAudioStart (int track, int fromTick, int lengthTicks) const
    {
        AudioClip probe;
        probe.data = std::make_shared<SampleData>();
        probe.track = track;
        probe.lengthTicks = lengthTicks;
        const int from = std::max (0, fromTick);
        probe.startTick = from;
        if (from + lengthTicks <= timelineTicks() && canPlaceAudio (probe))
            return from;
        for (int t = (from / kTicksPerBeat + 1) * kTicksPerBeat; t + lengthTicks <= timelineTicks(); t += kTicksPerBeat)
        {
            probe.startTick = t;
            if (canPlaceAudio (probe))
                return t;
        }
        return -1;
    }

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
        for (const auto& a : audioClips)
            if (a.track == c.track && c.startUnit * patternTicks < a.endTick() && a.startTick < c.endUnit() * patternTicks)
                return false;
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
        for (const auto& a : audioClips)
            end = std::max (end, (a.endTick() + patternTicks - 1) / patternTicks);
        return end;
    }

    // Make every pattern hold one note list per channel (after channels were added).
    void ensureChannelCount (size_t numChannels)
    {
        for (auto& p : patterns)
            p.notes.resize (numChannels);
    }

    // "Pattern 3": the lowest number that no pattern uses yet, so names stay tidy after patterns were deleted.
    std::string newPatternName() const
    {
        for (int n = 1; n <= kMaxPatterns + 1; ++n)
        {
            const auto name = "Pattern " + std::to_string (n);
            if (std::none_of (patterns.begin(), patterns.end(), [&name] (const Pattern& p) { return p.name == name; }))
                return name;
        }
        return "Pattern";
    }

    // Deletes a pattern together with the playlist clips that play it; later patterns move up one place (clips
    // follow). The last pattern cannot be deleted. Returns false (and changes nothing) for a bad index or the last pattern.
    bool removePattern (int index)
    {
        if (index < 0 || index >= static_cast<int> (patterns.size()) || patterns.size() <= 1)
            return false;

        patterns.erase (patterns.begin() + index);
        clips.erase (std::remove_if (clips.begin(), clips.end(), [index] (const Clip& c) { return c.pattern == index; }), clips.end());
        for (auto& c : clips)
            if (c.pattern > index)
                --c.pattern;
        return true;
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
                               int patternIndex, std::vector<MixerTrack> tracks = {},
                               std::array<EffectSlot, kSlotsPerTrack> masterSlots = {},
                               Console console = Console::defaults())
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
    return Project::make (std::move (channels), arrangement.patternTicks, std::move (tracks), masterSlots, {}, {}, std::move (console));
}

// The whole playlist laid out on one timeline: what "song mode" plays. Loops over the end of the last clip
// (an empty playlist is a silent loop of one pattern length).
inline Project compileSong (const std::vector<Channel>& channelSettings, const Arrangement& arrangement,
                            std::vector<MixerTrack> tracks = {},
                            std::array<EffectSlot, kSlotsPerTrack> masterSlots = {},
                            Console console = Console::defaults())
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

    std::vector<AudioEvent> events;
    for (const auto& a : arrangement.audioClips)
    {
        AudioEvent e;
        e.data = a.data;
        e.startTick = a.startTick;
        e.endTick = a.endTick();
        e.mixerTrack = a.track; // playlist track n plays through mixer track n
        e.gain = a.gain;
        e.offsetSeconds = a.offsetSeconds;
        events.push_back (std::move (e));
    }
    return Project::make (std::move (channels), units * arrangement.patternTicks, std::move (tracks), masterSlots,
                          std::move (events), arrangement.automationClips, std::move (console));
}
} // namespace sls
