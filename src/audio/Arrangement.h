#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "audio/Automation.h"
#include "audio/TimeStretch.h"
#include "audio/Project.h"
#include "audio/TimeSignature.h"
#include "audio/StepSequence.h"

namespace sls
{
// The song as the user edits it: patterns (note data per channel) and a playlist of clips that place
// patterns on a timeline. It is compiled into a flat Project, so the audio engine and the offline
// renderer know nothing about patterns: they just play notes in a loop.

// Playlist tracks (0.44.0): a project starts with kDefaultGenericTracks free tracks (recordings, audio, anything) plus one track
// dedicated to each pattern; every new pattern adds its own; the - and + buttons remove and add tracks by hand, up to
// kMaxPlaylistTracks. Before 0.44.0 there were always 8.
constexpr int kMaxPlaylistTracks = 128;
constexpr int kDefaultGenericTracks = 4;
constexpr int kLegacyPlaylistTracks = 8;
constexpr int kPlaylistUnits = 32;     // the timeline holds at least this many pattern lengths (see Arrangement::timelineTicks)
constexpr int kPatternBarChoices[] = { 1, 2, 4, 8 }; // the pattern lengths offered in the pattern menu, in bars of 4/4
// The pattern length (0.42.0) is counted in quarter notes: 4 (one bar of 4/4, the default) at least, changed one quarter at a time
// by the Channel Rack's + and - buttons. Since 0.56.0 every pattern has its own length; a new pattern starts at 4 quarters.
constexpr int kMinPatternBeats = 4;
constexpr int kMaxPatternBeats = 64;
constexpr int kDefaultPatternTicks = kMinPatternBeats * kTicksPerBeat;
// The playlist's zoom unit (0.56.0): the width the zoom sets is that of 4 quarters (before, of the one pattern length).
constexpr int kPlaylistUnitTicks = kDefaultPatternTicks;
constexpr int kMaxPatterns = 64;

struct Pattern
{
    std::string name = "Pattern";
    std::vector<std::vector<Note>> notes; // notes[channel], ticks relative to the pattern start
    // The piano roll's loop region of this pattern (0.20.0): Pattern mode repeats only this part. -1 / -1 when there is none.
    int loopStartTick = -1;
    int loopEndTick = -1;
    // The step sequencer's active length of each channel in this pattern (0.31.0), in steps; 0 or missing = not set (see
    // StepSequence.h). Projects saved before 0.31.0 load with every channel set to the whole pattern, so they play as before.
    std::vector<int> steps;
    // This pattern's length in ticks (0.56.0: each pattern has its own; before, one length was shared by all of them).
    int lengthTicks = kDefaultPatternTicks;
    int stepsOf (size_t channel) const noexcept { return channel < steps.size() ? steps[channel] : 0; }
    int length() const noexcept { return std::max (1, lengthTicks); }
    bool hasLoop() const noexcept { return loopStartTick >= 0 && loopEndTick > loopStartTick && loopEndTick <= length(); }
};

// A clip plays `pattern` on `track` from `startTick` for `lengthTicks`, repeating the pattern as often as the length asks
// (0.42.0: positions and lengths in ticks, so clips go anywhere on the playlist's grid, or off it with Alt; before, they were
// whole pattern lengths). `offsetTicks` is where in the pattern the clip starts playing (a clip split in the middle of a
// pattern goes on from there), 0 .. the pattern length.
struct Clip
{
    int pattern = 0;
    int track = 0;
    int startTick = 0;
    int lengthTicks = 4 * kTicksPerBeat;
    int offsetTicks = 0;

    int endTick() const noexcept { return startTick + lengthTicks; }
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
    // 0.27.0: per-clip pan, reverse and fades.
    float pan = 0.0f;           // -1 (left) .. 1 (right): a balance, like a mixer track's (the centre leaves the sound alone)
    bool reverse = false;       // plays the audio backwards; offsetSeconds then counts from the end of the audio (see setAudioReverse)
    double fadeInSeconds = 0.0; // fades over the clip's first and last seconds (sine curves); 0 = none
    double fadeOutSeconds = 0.0;
    // 0.47.0: a loop recording keeps every pass of the loop region in its file; take k (from 0) starts at
    // takeFirstSeconds + k * takeSeconds. takeSeconds 0: not a loop recording.
    double takeFirstSeconds = 0.0;
    double takeSeconds = 0.0;

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

// Loop recording (0.47.0). Takes can be chosen only on a clip that plays its file as it is (not reversed, not stretched).
inline int takeCount (const AudioClip& c) noexcept
{
    if (c.takeSeconds <= 0.0 || c.data == nullptr || c.data->sampleRate <= 0.0 || c.reverse || c.original != nullptr)
        return 0;
    const double file = c.data->numFrames() / c.data->sampleRate;
    return std::max (0, static_cast<int> (std::floor ((file - c.takeFirstSeconds) / c.takeSeconds + 0.01)));
}

inline int currentTake (const AudioClip& c) noexcept
{
    const int n = takeCount (c);
    if (n == 0)
        return -1;
    return std::clamp (static_cast<int> (std::floor ((c.offsetSeconds - c.takeFirstSeconds) / c.takeSeconds + 1.0e-6)), 0, n - 1);
}

// Plays take `take` instead: the same place inside the pass (a clip cut at its start keeps its cut).
inline bool chooseTake (AudioClip& c, int take) noexcept
{
    const int now = currentTake (c);
    if (now < 0 || take < 0 || take >= takeCount (c))
        return false;
    const double inside = std::clamp (c.offsetSeconds - c.takeFirstSeconds - now * c.takeSeconds, 0.0, c.takeSeconds);
    c.offsetSeconds = std::max (0.0, c.takeFirstSeconds + take * c.takeSeconds + inside);
    return true;
}

// Where the passes of a loop recording lie in its file. The take started at `startTick` and lasted `fileSeconds`, while the
// song looped over [loopStart, loopEnd). A pass is complete when the file holds the whole loop region for it; the first pass
// counts only if the take started at or before the region's start.
struct LoopTakes
{
    double firstSeconds = 0.0; // where the first complete pass starts in the file
    double passSeconds = 0.0;  // the loop region's length
    int complete = 0;
};

inline LoopTakes loopTakesOf (const std::vector<TempoMarker>& markers, double bpm, int startTick, int loopStart, int loopEnd, double fileSeconds) noexcept
{
    LoopTakes out;
    if (loopEnd <= loopStart || startTick >= loopEnd || startTick < 0)
        return out;
    const double s0 = tickToSeconds (markers, bpm, startTick), sL = tickToSeconds (markers, bpm, loopStart), sE = tickToSeconds (markers, bpm, loopEnd);
    out.passSeconds = sE - sL;
    if (out.passSeconds <= 0.0)
        return {};
    out.firstSeconds = startTick <= loopStart ? sL - s0 : sE - s0;
    out.complete = std::max (0, static_cast<int> (std::floor ((fileSeconds - out.firstSeconds) / out.passSeconds + 0.002)));
    return out;
}

// A playlist track's own name and colour (0.33.0). An empty name shows "Track N" - or, on a track dedicated to a pattern
// (0.44.0), the pattern's name; colour 0 is the default look. `pattern` is the pattern the track is dedicated to (a click on
// an empty place of the track puts that pattern there), -1 for a free track.
struct PlaylistTrack
{
    std::string name;
    std::uint32_t colour = 0; // 0xAARRGGBB
    int pattern = -1;
    bool operator== (const PlaylistTrack&) const = default;
};

struct Arrangement
{
    std::vector<Pattern> patterns;
    std::vector<Clip> clips;
    std::vector<AudioClip> audioClips;
    std::vector<AutomationClip> automationClips; // on the lanes under the tracks; they play in Song mode
    // The loop region drawn on the playlist's ruler (ticks; -1 / -1 when there is none). In Song mode the transport goes
    // back to its start when it reaches its end.
    int loopStartTick = -1;
    int loopEndTick = -1;
    // Tempo changes in the song (0.46.0, see TempoMap.h); the song starts at the toolbar's BPM.
    std::vector<TempoMarker> tempoMarkers;
    TimeSignature meter; // the time signature (0.46.0)
    // The shortest pattern: 4 quarters, or one bar when the bar is shorter (2/4, 3/4, 6/8) (0.46.0).
    int minPatternBeats() const noexcept { return std::min (kMinPatternBeats, meter.barQuarters()); }
    // The playlist tracks, in order (0.44.0: as many as the project has; names and colours since 0.33.0).
    std::vector<PlaylistTrack> playlistTracks = std::vector<PlaylistTrack> (kDefaultGenericTracks);

    int trackCount() const noexcept { return static_cast<int> (playlistTracks.size()); }

    // The length of pattern `p` (0.56.0); 4 quarters for an index that is not a pattern.
    int patternLength (int p) const noexcept
    {
        return p >= 0 && p < static_cast<int> (patterns.size()) ? patterns[static_cast<size_t> (p)].length() : kDefaultPatternTicks;
    }
    int longestPatternTicks() const noexcept
    {
        int longest = kDefaultPatternTicks;
        for (const auto& p : patterns)
            longest = std::max (longest, p.length());
        return longest;
    }

    // The track dedicated to `pattern`, or -1.
    int dedicatedTrack (int pattern) const noexcept
    {
        for (int t = 0; t < trackCount(); ++t)
            if (playlistTracks[static_cast<size_t> (t)].pattern == pattern)
                return t;
        return -1;
    }

    // A new project's tracks: the free ones, then one per pattern.
    void resetTracks()
    {
        playlistTracks.assign (kDefaultGenericTracks, PlaylistTrack {});
        for (int p = 0; p < static_cast<int> (patterns.size()); ++p)
            addPatternTrack (p);
    }

    // Adds a track at the end (a free one unless `track.pattern` says otherwise). Returns its index, or -1 when there are
    // kMaxPlaylistTracks already.
    int addTrack (PlaylistTrack track = {})
    {
        if (trackCount() >= kMaxPlaylistTracks)
            return -1;
        playlistTracks.push_back (std::move (track));
        return trackCount() - 1;
    }

    // The track for a new pattern: the one it already has, or a new one at the end (-1 when full).
    int addPatternTrack (int pattern)
    {
        if (const int t = dedicatedTrack (pattern); t >= 0)
            return t;
        PlaylistTrack track;
        track.pattern = pattern;
        return addTrack (track);
    }

    // Removes track `t` with every clip on it; the tracks after it move up one place (their clips follow). The last track is
    // kept. Returns false (and changes nothing) for a bad index or the only track.
    bool removeTrack (int t)
    {
        if (t < 0 || t >= trackCount() || trackCount() <= 1)
            return false;
        playlistTracks.erase (playlistTracks.begin() + t);
        const auto onTrack = [t] (const auto& c) { return c.track == t; };
        clips.erase (std::remove_if (clips.begin(), clips.end(), onTrack), clips.end());
        audioClips.erase (std::remove_if (audioClips.begin(), audioClips.end(), onTrack), audioClips.end());
        for (auto& c : clips)
            if (c.track > t)
                --c.track;
        for (auto& c : audioClips)
            if (c.track > t)
                --c.track;
        return true;
    }

    // How many clips (pattern and audio) are on track `t`.
    int clipsOnTrack (int t) const noexcept
    {
        return static_cast<int> (std::count_if (clips.begin(), clips.end(), [t] (const Clip& c) { return c.track == t; })
                                 + std::count_if (audioClips.begin(), audioClips.end(), [t] (const AudioClip& c) { return c.track == t; }));
    }

    bool hasLoop() const noexcept { return loopStartTick >= 0 && loopEndTick > loopStartTick && loopEndTick <= timelineTicks(); }
    // Sets the loop region; false (and no region) when it does not fit in the timeline or is shorter than one 16th.
    bool setLoop (int startTick, int endTick) noexcept
    {
        if (startTick < 0 || endTick - startTick < kSixteenthTicks || endTick > timelineTicks())
        {
            clearLoop();
            return false;
        }
        loopStartTick = startTick;
        loopEndTick = endTick;
        return true;
    }
    void clearLoop() noexcept { loopStartTick = loopEndTick = -1; }

    // The timeline holds at least kPlaylistUnits lengths of the longest pattern and at least 64 bars.
    int timelineTicks() const noexcept { return std::max (kPlaylistUnits * longestPatternTicks(), kPlaylistUnits * 8 * kTicksPerBeat); }

    // Does an audio clip fit: inside the timeline, on an existing track? (Audio clips start on any tick: the playlist's snap
    // setting decides how coarse the positions you can drag them to are.) Since 0.33.0 clips may overlap on a track, pattern and
    // audio clips alike (layering, as in FL Studio): they all play. `ignoreIndex` is kept for the callers (the clip being moved).
    bool canPlaceAudio (const AudioClip& c, [[maybe_unused]] int ignoreIndex = -1) const
    {
        if (c.data == nullptr || c.track < 0 || c.track >= trackCount())
            return false;
        return c.startTick >= 0 && c.lengthTicks >= 1 && c.endTick() <= timelineTicks() && std::isfinite (c.offsetSeconds) && c.offsetSeconds >= 0.0;
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

    // Index of the audio clip covering (track, tick), or -1; of overlapping clips, the one drawn on top (the last).
    int audioClipAt (int track, int tick) const
    {
        for (int i = static_cast<int> (audioClips.size()) - 1; i >= 0; --i)
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

    // A clip is valid when it lies inside the timeline and refers to an existing pattern. Since 0.33.0 it may overlap other
    // clips on its track (layering): they all play. `ignoreIndex` is kept for the callers (the clip being moved).
    bool canPlace (const Clip& c, [[maybe_unused]] int ignoreIndex = -1) const
    {
        if (c.pattern < 0 || c.pattern >= static_cast<int> (patterns.size()))
            return false;
        if (c.track < 0 || c.track >= trackCount())
            return false;
        return c.startTick >= 0 && c.lengthTicks >= 1 && c.offsetTicks >= 0 && c.endTick() <= timelineTicks();
    }

    // Index of the clip covering (track, tick), or -1; of overlapping clips, the one drawn on top (the last).
    int clipAt (int track, int tick) const
    {
        for (int i = static_cast<int> (clips.size()) - 1; i >= 0; --i)
        {
            const auto& c = clips[static_cast<size_t> (i)];
            if (c.track == track && tick >= c.startTick && tick < c.endTick())
                return i;
        }
        return -1;
    }

    // Where pattern `p` is at song tick `songTick` (0.58.0): the tick inside the pattern that its clip on the playlist plays there
    // (of overlapping clips, the one drawn on top), or -1 when no clip of that pattern plays at that tick.
    int patternTickAt (int p, int songTick) const noexcept
    {
        if (songTick < 0)
            return -1;
        const int length = patternLength (p);
        for (int i = static_cast<int> (clips.size()) - 1; i >= 0; --i)
        {
            const auto& c = clips[static_cast<size_t> (i)];
            if (c.pattern == p && songTick >= c.startTick && songTick < c.endTick())
                return ((songTick - c.startTick + c.offsetTicks) % length + length) % length;
        }
        return -1;
    }

    // Where the song ends: the end of the last pattern or audio clip, rounded up to a whole bar (0 for an empty playlist).
    int songLengthTicks() const
    {
        int end = 0;
        for (const auto& c : clips)
            end = std::max (end, c.endTick());
        for (const auto& a : audioClips)
            end = std::max (end, a.endTick());
        constexpr int bar = 4 * kTicksPerBeat;
        return (end + bar - 1) / bar * bar;
    }

    // Pattern `p` gets a new length (0.42.0; only that pattern since 0.56.0): its clips stay where they are; a clip that was a
    // whole number of patterns long keeps that number of repeats, any other keeps its length; offsets stay inside the pattern.
    // The other patterns and their clips do not change. Returns false for a bad index.
    bool setPatternLengthKeepingClips (int p, int newTicks)
    {
        if (p < 0 || p >= static_cast<int> (patterns.size()))
            return false;
        auto& pattern = patterns[static_cast<size_t> (p)];
        const int old = pattern.length();
        newTicks = std::clamp (newTicks, minPatternBeats() * kTicksPerBeat, kMaxPatternBeats * kTicksPerBeat);
        for (auto& c : clips)
        {
            if (c.pattern != p)
                continue;
            if (c.lengthTicks % old == 0)
                c.lengthTicks = c.lengthTicks / old * newTicks;
            c.offsetTicks = std::min (c.offsetTicks, newTicks - 1);
        }
        pattern.lengthTicks = newTicks;
        return true;
    }

    // Every pattern at least `minTicks` long (a shorter bar may allow shorter patterns, a longer one not).
    void lengthenPatternsTo (int minTicks)
    {
        for (int p = 0; p < static_cast<int> (patterns.size()); ++p)
            if (patterns[static_cast<size_t> (p)].length() < minTicks)
                setPatternLengthKeepingClips (p, minTicks);
    }

    // Make every pattern hold one note list per channel (after channels were added).
    void ensureChannelCount (size_t numChannels)
    {
        for (auto& p : patterns)
        {
            p.notes.resize (numChannels);
            p.steps.resize (numChannels, 0);
        }
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

        const auto oldName = patterns[static_cast<size_t> (index)].name;
        patterns.erase (patterns.begin() + index);
        clips.erase (std::remove_if (clips.begin(), clips.end(), [index] (const Clip& c) { return c.pattern == index; }), clips.end());
        for (auto& c : clips)
            if (c.pattern > index)
                --c.pattern;
        // Its own track goes too when nothing else is on it (0.44.0); otherwise it stays as a free track.
        if (const int t = dedicatedTrack (index); t >= 0)
        {
            if (clipsOnTrack (t) == 0 && trackCount() > 1)
                removeTrack (t);
            else
            {
                auto& track = playlistTracks[static_cast<size_t> (t)];
                track.pattern = -1;
                if (track.name.empty())
                    track.name = oldName.substr (0, 32);
            }
        }
        for (auto& track : playlistTracks)
            if (track.pattern > index)
                --track.pattern;
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

    const int length = arrangement.patternLength (patternIndex);
    for (size_t c = 0; c < channels.size(); ++c)
    {
        channels[c].notes.clear();
        if (valid)
        {
            const auto& notes = arrangement.patterns[static_cast<size_t> (patternIndex)].notes;
            if (c < notes.size())
                channels[c].notes = steps::playedNotes (notes[c], length);
        }
    }
    auto project = Project::make (std::move (channels), length, std::move (tracks), masterSlots, {}, {}, std::move (console));
    project.beatsPerBar = arrangement.meter.beats; // the metronome's accent (0.46.0)
    project.beatUnit = arrangement.meter.unit;
    return project;
}

// Channels were deleted, moved or cloned: `newToOld[n]` is the old index of new channel n (an old index may appear twice, a
// clone, or not at all, a deleted channel). Every pattern's notes follow; automation clips that move a channel's volume or pan
// follow its first copy and are removed with a deleted channel.
inline void remapArrangementChannels (Arrangement& arrangement, const std::vector<int>& newToOld, size_t oldCount)
{
    std::vector<int> oldToNew (oldCount, -1);
    for (size_t n = 0; n < newToOld.size(); ++n)
    {
        const int old = newToOld[n];
        if (old >= 0 && static_cast<size_t> (old) < oldCount && oldToNew[static_cast<size_t> (old)] < 0)
            oldToNew[static_cast<size_t> (old)] = static_cast<int> (n);
    }

    for (auto& pattern : arrangement.patterns)
    {
        pattern.notes.resize (std::max (pattern.notes.size(), oldCount));
        std::vector<std::vector<Note>> notes;
        std::vector<int> steps;
        notes.reserve (newToOld.size());
        for (const int old : newToOld)
        {
            const bool kept = old >= 0 && static_cast<size_t> (old) < oldCount;
            notes.push_back (kept ? pattern.notes[static_cast<size_t> (old)] : std::vector<Note> {});
            steps.push_back (kept ? pattern.stepsOf (static_cast<size_t> (old)) : 0);
        }
        pattern.notes = std::move (notes);
        pattern.steps = std::move (steps);
    }

    auto& clips = arrangement.automationClips;
    clips.erase (std::remove_if (clips.begin(), clips.end(),
                                 [&] (AutomationClip& c)
                                 {
                                     if (c.target.kind != AutomationKind::ChannelVolume && c.target.kind != AutomationKind::ChannelPan
                                         && c.target.kind != AutomationKind::ChannelPlugin)
                                         return false;
                                     if (c.target.index < 0 || static_cast<size_t> (c.target.index) >= oldCount
                                         || oldToNew[static_cast<size_t> (c.target.index)] < 0)
                                         return true;
                                     c.target.index = oldToNew[static_cast<size_t> (c.target.index)];
                                     return false;
                                 }),
                 clips.end());
}

// The whole playlist laid out on one timeline: what "song mode" plays. Loops over the end of the last clip
// (an empty playlist is a silent loop of 4 quarters).
inline Project compileSong (const std::vector<Channel>& channelSettings, const Arrangement& arrangement,
                            std::vector<MixerTrack> tracks = {},
                            std::array<EffectSlot, kSlotsPerTrack> masterSlots = {},
                            Console console = Console::defaults())
{
    auto channels = channelSettings;
    for (auto& c : channels)
        c.notes.clear();

    const int songTicks = std::max (kDefaultPatternTicks, arrangement.songLengthTicks());

    for (const auto& clip : arrangement.clips)
    {
        if (clip.pattern < 0 || clip.pattern >= static_cast<int> (arrangement.patterns.size()) || clip.lengthTicks <= 0)
            continue;

        const auto& source = arrangement.patterns[static_cast<size_t> (clip.pattern)];
        const int patternTicks = source.length(); // each clip repeats its own pattern's length (0.56.0)
        // The pattern repeats from `offsetTicks` into it, for the clip's length: pass k starts at clip time k * pattern - offset.
        const int offset = ((clip.offsetTicks % patternTicks) + patternTicks) % patternTicks;
        for (size_t c = 0; c < channels.size() && c < source.notes.size(); ++c)
            for (int passStart = -offset; passStart < clip.lengthTicks; passStart += patternTicks)
                for (auto n : source.notes[c])
                {
                    if (n.startTick < 0 || n.startTick >= patternTicks)
                        continue; // outside its pattern: would leak into the next pass
                    const int at = passStart + n.startTick; // relative to the clip's start
                    if (at < 0 || at >= clip.lengthTicks)
                        continue;
                    n.lengthTicks = std::min ({ n.lengthTicks, patternTicks - n.startTick, clip.lengthTicks - at });
                    n.startTick = clip.startTick + at;
                    channels[c].notes.push_back (n);
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
        e.pan = a.pan;
        e.reverse = a.reverse;
        e.fadeInSeconds = a.fadeInSeconds;
        e.fadeOutSeconds = a.fadeOutSeconds;
        events.push_back (std::move (e));
    }
    auto project = Project::make (std::move (channels), songTicks, std::move (tracks), masterSlots,
                                  std::move (events), arrangement.automationClips, std::move (console));
    project.tempoMarkers = tidyTempoMarkers (arrangement.tempoMarkers); // 0.46.0
    project.beatsPerBar = arrangement.meter.beats;
    project.beatUnit = arrangement.meter.unit;
    return project;
}
} // namespace sls
