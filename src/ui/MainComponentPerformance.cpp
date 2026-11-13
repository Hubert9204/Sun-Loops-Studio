// Part of MainComponent (0.62.0): the Playlist's Make unique and Consolidate, and performance mode.
#include "ui/MainComponent.h"

#include "audio/OfflineRenderer.h"
#include "audio/SampleEdit.h"

namespace sls
{
// ------------------------------------------------------------------------- Make unique
// The clip gets a copy of its pattern (FL Studio's "Make unique"): editing it no longer changes the other clips of that pattern.

void MainComponent::makeClipUnique (int clipIndex)
{
    if (clipIndex < 0 || clipIndex >= static_cast<int> (arrangement.clips.size()))
        return;
    if (arrangement.patterns.size() >= static_cast<size_t> (kMaxPatterns))
    {
        statusLabel.setText ("Make unique: the project already has " + juce::String (kMaxPatterns) + " patterns", juce::dontSendNotification);
        return;
    }
    commitCurrentPattern();
    const int source = arrangement.clips[static_cast<size_t> (clipIndex)].pattern;
    if (source < 0 || source >= static_cast<int> (arrangement.patterns.size()))
        return;
    playlistView.pushUndoPoint();
    Pattern copy = arrangement.patterns[static_cast<size_t> (source)];
    copy.name = arrangement.newPatternName();
    arrangement.patterns.push_back (std::move (copy));
    const int made = static_cast<int> (arrangement.patterns.size()) - 1;
    arrangement.clips[static_cast<size_t> (clipIndex)].pattern = made;
    arrangement.ensureChannelCount (channels.size());
    refreshPatternBox();
    selectPattern (made); // (publishes)
    playlistView.refreshTimeline();
    markDirty();
    statusLabel.setText ("The clip now plays its own pattern, " + juce::String (arrangement.patterns[static_cast<size_t> (made)].name)
                             + " (a copy of " + juce::String (arrangement.patterns[static_cast<size_t> (source)].name) + ")",
                         juce::dontSendNotification);
}

// ------------------------------------------------------------------------- Consolidate
// The pattern clips of one playlist track between two ticks are rendered to an audio file (through their channels' mixer tracks,
// without the master's effects) and replaced by an audio clip of it; the pattern clips stay, muted (FL Studio's "Consolidate").

void MainComponent::consolidateClips (int track, int fromTick, int toTick)
{
    commitCurrentPattern();
    std::vector<int> sources;
    int from = std::numeric_limits<int>::max(), to = 0;
    for (int i = 0; i < static_cast<int> (arrangement.clips.size()); ++i)
    {
        const auto& c = arrangement.clips[static_cast<size_t> (i)];
        if (c.track != track || c.muted || (toTick >= 0 && (c.endTick() <= fromTick || c.startTick >= toTick)))
            continue;
        sources.push_back (i);
        from = std::min (from, c.startTick);
        to = std::max (to, c.endTick());
    }
    if (sources.empty())
    {
        statusLabel.setText ("Consolidate: no pattern clips to render on " + playlistView.trackName (track), juce::dontSendNotification);
        return;
    }
    if (arrangement.audioClips.size() >= static_cast<size_t> (kMaxAudioClips))
    {
        statusLabel.setText ("Consolidate: the playlist already holds " + juce::String (kMaxAudioClips) + " audio clips", juce::dontSendNotification);
        return;
    }

    // The song with only those clips, every track audible, no master effects, no loop.
    Arrangement only = arrangement;
    only.clips.clear();
    for (const int i : sources)
        only.clips.push_back (arrangement.clips[static_cast<size_t> (i)]);
    only.audioClips.clear();
    only.loopStartTick = only.loopEndTick = -1;
    for (auto& t : only.playlistTracks)
        t.mute = t.solo = false;
    prepareConvolvers (tracks, masterTrack.slots, console.aux, engine.getSampleRate());
    auto project = compileSong (channels, only, tracks, {}, console);
    project.masterPitch = masterPitch;

    RenderOptions options;
    options.sampleRate = engine.getSampleRate() > 0.0 ? engine.getSampleRate() : 48000.0;
    options.bpm = bpmSlider.getValue();
    options.tailSeconds = 2.0;
    juce::MouseCursor::showWaitCursor();
    const auto rendered = renderToBuffer (project, options);
    juce::MouseCursor::hideWaitCursor();

    const double fromSeconds = tickToSeconds (arrangement.tempoMarkers, options.bpm, from);
    const int first = std::clamp (static_cast<int> (std::llround (fromSeconds * options.sampleRate)), 0, rendered.getNumSamples());
    int last = rendered.getNumSamples();
    // (the tail is kept only as far as it still sounds)
    const int songEnd = std::clamp (static_cast<int> (std::llround (tickToSeconds (arrangement.tempoMarkers, options.bpm, to) * options.sampleRate)), first, last);
    while (last > songEnd && std::abs (rendered.getSample (0, last - 1)) < 1.0e-4f && std::abs (rendered.getSample (1, last - 1)) < 1.0e-4f)
        --last;
    if (last - first < 16)
    {
        statusLabel.setText ("Consolidate: those clips are silent", juce::dontSendNotification);
        return;
    }
    auto data = std::make_shared<SampleData>();
    data->sampleRate = options.sampleRate;
    data->left.assign (rendered.getReadPointer (0) + first, rendered.getReadPointer (0) + last);
    data->right.assign (rendered.getReadPointer (1) + first, rendered.getReadPointer (1) + last);
    const auto folder = library.samples.getChildFile ("Consolidated");
    folder.createDirectory();
    const auto file = folder.getNonexistentChildFile (playlistView.trackName (track).retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_"), ".wav", false);
    data->name = file.getFileNameWithoutExtension().toStdString();
    data->source = file.getFullPathName().toStdString();
    if (const auto written = writeSampleWav (*data, file); written.failed())
    {
        statusLabel.setText ("Consolidate: could not write " + file.getFullPathName() + ": " + written.getErrorMessage(), juce::dontSendNotification);
        return;
    }

    playlistView.pushUndoPoint();
    for (const int i : sources)
        arrangement.clips[static_cast<size_t> (i)].muted = true;
    AudioClip clip;
    clip.data = data;
    clip.name = data->name;
    clip.track = track;
    clip.startTick = from;
    clip.lengthTicks = std::max (1, std::min (clip.naturalLengthTicks (options.bpm), arrangement.timelineTicks() - from));
    arrangement.audioClips.push_back (std::move (clip));
    playlistView.refreshTimeline();
    publishProject();
    statusLabel.setText ("Consolidated " + juce::String (static_cast<int> (sources.size())) + (sources.size() == 1 ? " clip" : " clips") + " of "
                             + playlistView.trackName (track) + " to " + file.getFileName()
                             + " (it plays through mixer track " + juce::String (track + 1) + "; the pattern clips are muted, Undo brings them back)",
                         juce::dontSendNotification);
}

// ------------------------------------------------------------------------- Performance mode
// The engine plays a song like any other: a timeline of pattern clips. In performance mode that timeline is not the Playlist's but
// one written as the user launches clips: a launched clip is put at the next boundary and repeated from there far into the
// future, and whatever played on its track is cut at that boundary. Since every change lies ahead of the playhead, publishing
// the new timeline at once is safe, and the switch itself is exact to the sample.

namespace
{
constexpr int kOpenEnd = std::numeric_limits<int>::max();
bool sameClip (const Clip& a, const Clip& b) noexcept
{
    return a.pattern == b.pattern && a.track == b.track && a.startTick == b.startTick && a.lengthTicks == b.lengthTicks && a.offsetTicks == b.offsetTicks;
}
} // namespace

int MainComponent::performanceBoundary() const
{
    if (! engine.isPlaying())
        return 0;
    const int bar = std::max (kTicksPerBeat, arrangement.meter.barTicks());
    const int steps[] = { bar, 2 * bar, 4 * bar, kTicksPerBeat, kTicksPerBeat / 4 };
    const int step = steps[std::clamp (performanceQuantise, 1, 5) - 1];
    // (the new timeline has to reach the audio thread before the boundary: a boundary closer than about 80 ms is left for the next)
    const int guard = static_cast<int> (std::ceil (bpmSlider.getValue() * kTicksPerBeat / 60.0 * 0.08));
    return (engine.getPlayheadTick() + guard + step) / step * step;
}

Arrangement MainComponent::performanceArrangement()
{
    const int bar = std::max (kTicksPerBeat, arrangement.meter.barTicks());
    const int now = engine.isPlaying() ? engine.getPlayheadTick() : 0;
    performanceHorizon = std::max (performanceHorizon, now + 128 * bar);
    // what has long finished is forgotten
    performanceSegments.erase (std::remove_if (performanceSegments.begin(), performanceSegments.end(),
                                               [&] (const PerformanceSegment& s) { return s.toTick != kOpenEnd && s.toTick < now - 4 * bar; }),
                               performanceSegments.end());
    Arrangement a = arrangement;
    a.clips.clear();
    a.audioClips.clear();
    a.automationClips.clear();
    a.tempoMarkers.clear();
    a.markers.clear();
    a.loopStartTick = a.loopEndTick = -1;
    for (const auto& s : performanceSegments)
    {
        const int end = std::min (s.toTick, performanceHorizon);
        const int period = std::max (1, s.clip.lengthTicks);
        // (repeats that ended before the playhead are left out: they have been played)
        int start = s.fromTick;
        if (now - bar > start)
            start += (now - bar - start) / period * period;
        for (; start < end; start += period)
        {
            Clip c = s.clip;
            c.startTick = start;
            c.lengthTicks = std::min (period, end - start);
            c.muted = false;
            a.clips.push_back (c);
        }
    }
    // The song lasts to the horizon whatever plays (a muted clip counts for the length and is not played).
    Clip filler;
    filler.pattern = 0;
    filler.track = 0;
    filler.startTick = performanceHorizon - bar;
    filler.lengthTicks = bar;
    filler.muted = true;
    a.clips.push_back (filler);
    return a;
}

void MainComponent::setPerformanceMode (bool on)
{
    if (on == performanceOn)
        return;
    if (recording)
        finishRecording ({});
    setPlaying (false);
    performanceOn = on;
    performanceSegments.clear();
    performanceLanes.assign (static_cast<size_t> (std::max (1, arrangement.trackCount())), PerformanceLane {});
    performanceHorizon = 0;
    engine.requestSeek (0);
    publishProject();
    refreshPerformancePanel();
    statusLabel.setText (on ? "Performance mode is on: click a clip in the Performance window to launch it (the Playlist's arrangement does not play)"
                            : "Performance mode is off: the Playlist plays the song again",
                         juce::dontSendNotification);
}

void MainComponent::performanceLaunch (int clipIndex)
{
    if (clipIndex < 0 || clipIndex >= static_cast<int> (arrangement.clips.size()))
        return;
    if (! performanceOn)
        setPerformanceMode (true);
    const Clip clip = arrangement.clips[static_cast<size_t> (clipIndex)];
    if (clip.track < 0 || clip.lengthTicks <= 0)
        return;
    const bool running = engine.isPlaying();
    if (! running) // a fresh start: nothing of an earlier run is left on the timeline
    {
        performanceSegments.clear();
        performanceHorizon = 0;
        performanceLanes.assign (performanceLanes.size(), PerformanceLane {});
    }
    if (static_cast<size_t> (clip.track) >= performanceLanes.size())
        performanceLanes.resize (static_cast<size_t> (clip.track) + 1);
    const int at = performanceBoundary();
    performanceCut (clip.track, at);
    performanceSegments.push_back ({ clip, at, kOpenEnd });
    auto& lane = performanceLanes[static_cast<size_t> (clip.track)];
    lane.queued = clip;
    lane.hasQueued = true;
    lane.stopQueued = false;
    lane.queuedAt = at;
    publishProject();
    if (! running)
    {
        engine.requestSeek (0);
        setPlaying (true);
    }
    refreshPerformancePanel();
}

// Whatever is on `track` from `at` on is taken off the timeline.
void MainComponent::performanceCut (int track, int at)
{
    for (auto it = performanceSegments.begin(); it != performanceSegments.end();)
    {
        if (it->clip.track == track && it->fromTick >= at)
            it = performanceSegments.erase (it);
        else
        {
            if (it->clip.track == track)
                it->toTick = std::min (it->toTick, at);
            ++it;
        }
    }
}

void MainComponent::performanceStop (int track)
{
    if (! performanceOn || track < 0 || static_cast<size_t> (track) >= performanceLanes.size())
        return;
    const int at = performanceBoundary();
    performanceCut (track, at);
    auto& lane = performanceLanes[static_cast<size_t> (track)];
    lane.hasQueued = false;
    lane.stopQueued = lane.hasPlaying;
    lane.queuedAt = at;
    publishProject();
    refreshPerformancePanel();
}

void MainComponent::performanceTick()
{
    if (! performanceOn)
        return;
    bool changed = false;
    if (! engine.isPlaying())
    {
        for (auto& lane : performanceLanes)
        {
            changed = changed || lane.hasPlaying || lane.hasQueued || lane.stopQueued;
            lane = {};
        }
        if (changed)
        {
            performanceSegments.clear();
            performanceHorizon = 0;
        }
    }
    else
    {
        const int now = engine.getPlayheadTick();
        for (auto& lane : performanceLanes)
        {
            if ((lane.hasQueued || lane.stopQueued) && now >= lane.queuedAt)
            {
                lane.hasPlaying = lane.hasQueued;
                lane.playing = lane.queued;
                lane.hasQueued = lane.stopQueued = false;
                changed = true;
            }
        }
        const int bar = std::max (kTicksPerBeat, arrangement.meter.barTicks());
        if (now > performanceHorizon - 32 * bar)
        {
            performanceHorizon = now + 128 * bar; // the timeline is written further ahead
            publishProject();
        }
    }
    if (changed)
        refreshPerformancePanel();
}

void MainComponent::refreshPerformancePanel()
{
    if (performanceWindow == nullptr)
        return;
    PerformanceModel model;
    model.on = performanceOn;
    model.quantiseId = performanceQuantise;
    const auto indexOf = [this] (const Clip& wanted)
    {
        for (int i = 0; i < static_cast<int> (arrangement.clips.size()); ++i)
            if (sameClip (arrangement.clips[static_cast<size_t> (i)], wanted))
                return i;
        return -1;
    };
    for (int t = 0; t < arrangement.trackCount(); ++t)
    {
        PerformanceModel::Row row;
        row.track = t;
        row.name = playlistView.trackName (t);
        std::vector<int> onTrack;
        for (int i = 0; i < static_cast<int> (arrangement.clips.size()); ++i)
            if (arrangement.clips[static_cast<size_t> (i)].track == t)
                onTrack.push_back (i);
        if (onTrack.empty())
            continue;
        std::sort (onTrack.begin(), onTrack.end(), [this] (int a, int b)
                   { return arrangement.clips[static_cast<size_t> (a)].startTick < arrangement.clips[static_cast<size_t> (b)].startTick; });
        for (const int i : onTrack)
        {
            const auto& c = arrangement.clips[static_cast<size_t> (i)];
            const bool valid = c.pattern >= 0 && c.pattern < static_cast<int> (arrangement.patterns.size());
            row.cells.push_back ({ i, valid ? juce::String (arrangement.patterns[static_cast<size_t> (c.pattern)].name) : juce::String ("?"),
                                   juce::Colour::fromHSV (std::fmod (0.31f + static_cast<float> (c.pattern) * 0.137f, 1.0f), 0.55f, 0.85f, 1.0f) });
        }
        if (static_cast<size_t> (t) < performanceLanes.size())
        {
            const auto& lane = performanceLanes[static_cast<size_t> (t)];
            row.playing = lane.hasPlaying ? indexOf (lane.playing) : -1;
            row.queued = lane.hasQueued ? indexOf (lane.queued) : -1;
            row.stopQueued = lane.stopQueued;
        }
        model.rows.push_back (std::move (row));
    }
    performanceWindow->panel().setModel (std::move (model));
}

void MainComponent::togglePerformanceWindow()
{
    if (performanceWindow != nullptr)
    {
        performanceWindow.reset();
        return;
    }
    performanceWindow = std::make_unique<PerformanceWindow>();
    auto& panel = performanceWindow->panel();
    panel.onToggle = [this] (bool on) { setPerformanceMode (on); };
    panel.onLaunch = [this] (int clip) { performanceLaunch (clip); };
    panel.onStopTrack = [this] (int track) { performanceStop (track); };
    panel.onStopAll = [this]
    {
        for (int t = 0; t < static_cast<int> (performanceLanes.size()); ++t)
            if (performanceLanes[static_cast<size_t> (t)].hasPlaying || performanceLanes[static_cast<size_t> (t)].hasQueued)
                performanceStop (t);
    };
    panel.onScene = [this] (int column)
    {
        // the column's clip of every track, all at the same boundary (tracks with fewer clips go on as they are)
        for (int t = 0; t < arrangement.trackCount(); ++t)
        {
            std::vector<int> onTrack;
            for (int i = 0; i < static_cast<int> (arrangement.clips.size()); ++i)
                if (arrangement.clips[static_cast<size_t> (i)].track == t)
                    onTrack.push_back (i);
            std::sort (onTrack.begin(), onTrack.end(), [this] (int a, int b)
                       { return arrangement.clips[static_cast<size_t> (a)].startTick < arrangement.clips[static_cast<size_t> (b)].startTick; });
            if (column < static_cast<int> (onTrack.size()))
                performanceLaunch (onTrack[static_cast<size_t> (column)]);
        }
    };
    panel.onQuantise = [this] (int id) { performanceQuantise = std::clamp (id, 1, 5); };
    performanceWindow->onClose = [safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        juce::MessageManager::callAsync ([safe]
                                         {
                                             if (safe == nullptr)
                                                 return;
                                             safe->setPerformanceMode (false); // closing the window ends the performance
                                             safe->performanceWindow.reset();
                                         });
    };
    performanceWindow->setVisible (true);
    performanceWindow->toFront (true);
    refreshPerformancePanel();
}
} // namespace sls
