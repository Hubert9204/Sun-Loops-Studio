// Part of MainComponent (0.62.0): the Playlist's Make unique and Consolidate, and performance mode.
#include "audio/PerformancePads.h"
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
    statusLabel.setText ("Consolidated to " + file.getFileName() + " (the pattern clips are muted; Undo brings them back)", juce::dontSendNotification);
}

// ------------------------------------------------------------------------- Performance mode
// The engine plays a song like any other: a timeline of pattern clips. In performance mode that timeline is not the Playlist's but
// one written as the user launches clips: a launched clip is put at the next boundary and repeated from there far into the
// future, and whatever played on its track is cut at that boundary. Since every change lies ahead of the playhead, publishing
// the new timeline at once is safe, and the switch itself is exact to the sample.

namespace
{
constexpr int kOpenEnd = std::numeric_limits<int>::max();

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
    const double secondsPerTick = 60.0 / (std::max (1.0, bpmSlider.getValue()) * kTicksPerBeat);
    for (const auto& s : performanceSegments)
    {
        const int end = std::min (s.toTick, performanceHorizon);
        const int period = std::max (1, s.audio ? s.audioClip.lengthTicks : s.clip.lengthTicks);
        // 0.71.0: a clip in sync starts where it would be had it played since the first bar (`phase` ticks into it)
        const int phase = s.phase % period;
        // (repeats that ended before the playhead are left out: they have been played)
        int start = s.fromTick - phase;
        if (now - bar > start)
            start += (now - bar - start) / period * period;
        for (; start < end; start += period)
        {
            const int from = std::max (start, s.fromTick), to = std::min (start + period, end);
            if (to <= from)
                continue;
            if (s.audio)
            {
                AudioClip c = s.audioClip;
                c.offsetSeconds += (from - start) * secondsPerTick * (c.reverse ? -1.0 : 1.0);
                c.offsetSeconds = std::max (0.0, c.offsetSeconds);
                c.startTick = from;
                c.lengthTicks = to - from;
                c.muted = false;
                if (from > start)
                    c.fadeInSeconds = 0.0;
                if (static_cast<int> (a.audioClips.size()) < kMaxAudioClips)
                    a.audioClips.push_back (std::move (c));
            }
            else
            {
                Clip c = s.clip;
                c.offsetTicks += from - start;
                c.startTick = from;
                c.lengthTicks = to - from;
                c.muted = false;
                a.clips.push_back (c);
            }
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
    engine.setPerformancePads (on && performancePads); // 0.70.0
    engine.setPerformancePadLayout (performancePadFirst, performancePadsPerRow);
    engine.requestSeek (0);
    publishProject();
    refreshPerformancePanel();
    statusLabel.setText (on ? "Performance mode is on: click a clip in the Performance window to launch it (the Playlist's arrangement does not play)"
                            : "Performance mode is off: the Playlist plays the song again",
                         juce::dontSendNotification);
}

// 0.71.0: a cell is a pattern clip (its index) or an audio clip (kAudioCell + its index).
void MainComponent::performanceLaunch (int cell)
{
    const bool audio = cell >= kAudioCell;
    const int index = audio ? cell - kAudioCell : cell;
    if (index < 0 || index >= static_cast<int> (audio ? arrangement.audioClips.size() : arrangement.clips.size()))
        return;
    if (! performanceOn)
        setPerformanceMode (true);
    PerformanceSegment segment;
    segment.audio = audio;
    int track = 0, length = 0, mode = 0;
    bool sync = false;
    if (audio)
    {
        segment.audioClip = arrangement.audioClips[static_cast<size_t> (index)];
        track = segment.audioClip.track;
        length = segment.audioClip.lengthTicks;
        mode = segment.audioClip.launchMode;
        sync = segment.audioClip.launchSync;
    }
    else
    {
        segment.clip = arrangement.clips[static_cast<size_t> (index)];
        track = segment.clip.track;
        length = segment.clip.lengthTicks;
        mode = segment.clip.launchMode;
        sync = segment.clip.launchSync;
    }
    if (track < 0 || length <= 0)
        return;
    const bool running = engine.isPlaying();
    if (! running) // a fresh start: nothing of an earlier run is left on the timeline
    {
        performanceSegments.clear();
        performanceHorizon = 0;
        performanceLanes.assign (performanceLanes.size(), PerformanceLane {});
    }
    if (static_cast<size_t> (track) >= performanceLanes.size())
        performanceLanes.resize (static_cast<size_t> (track) + 1);
    const int at = performanceBoundary();
    performanceCut (track, at);
    segment.fromTick = at;
    segment.phase = sync ? at % length : 0;
    segment.toTick = mode == 1 ? at + length - segment.phase : kOpenEnd; // a one-shot plays once (in sync: to the end of its turn)
    performanceSegments.push_back (segment);
    auto& lane = performanceLanes[static_cast<size_t> (track)];
    lane.queuedCell = cell;
    lane.hasQueued = true;
    lane.stopQueued = false;
    lane.queuedAt = at;
    lane.queuedEndsAt = mode == 1 ? segment.toTick : -1;
    publishProject();
    if (! running)
    {
        engine.requestSeek (0);
        setPlaying (true);
    }
    refreshPerformancePanel();
}

// 0.71.0: a cell let go (the mouse or a pad): a clip that plays while held stops at the next boundary.
void MainComponent::performanceRelease (int cell)
{
    const bool audio = cell >= kAudioCell;
    const int index = audio ? cell - kAudioCell : cell;
    if (! performanceOn || index < 0 || index >= static_cast<int> (audio ? arrangement.audioClips.size() : arrangement.clips.size()))
        return;
    const int mode = audio ? arrangement.audioClips[static_cast<size_t> (index)].launchMode : arrangement.clips[static_cast<size_t> (index)].launchMode;
    const int track = audio ? arrangement.audioClips[static_cast<size_t> (index)].track : arrangement.clips[static_cast<size_t> (index)].track;
    if (mode != 2 || track < 0 || static_cast<size_t> (track) >= performanceLanes.size())
        return;
    const auto& lane = performanceLanes[static_cast<size_t> (track)];
    if ((lane.hasQueued && lane.queuedCell == cell) || (lane.hasPlaying && lane.playingCell == cell))
        performanceStop (track);
}

// Whatever is on `track` from `at` on is taken off the timeline.
void MainComponent::performanceCut (int track, int at)
{
    for (auto it = performanceSegments.begin(); it != performanceSegments.end();)
    {
        const int segmentTrack = it->audio ? it->audioClip.track : it->clip.track;
        if (segmentTrack == track && it->fromTick >= at)
            it = performanceSegments.erase (it);
        else
        {
            if (segmentTrack == track)
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

// 0.70.0: the window's rows as the panel shows them: each playlist track that holds pattern or (0.71.0) audio clips, its clips
// in time order, as cells.
std::vector<std::vector<int>> MainComponent::performanceRows (std::vector<int>* rowTracks) const
{
    std::vector<std::vector<int>> rows;
    const auto startOf = [this] (int cell)
    {
        return cell >= kAudioCell ? arrangement.audioClips[static_cast<size_t> (cell - kAudioCell)].startTick : arrangement.clips[static_cast<size_t> (cell)].startTick;
    };
    for (int t = 0; t < arrangement.trackCount(); ++t)
    {
        std::vector<int> onTrack;
        for (int i = 0; i < static_cast<int> (arrangement.clips.size()); ++i)
            if (arrangement.clips[static_cast<size_t> (i)].track == t)
                onTrack.push_back (i);
        for (int i = 0; i < static_cast<int> (arrangement.audioClips.size()); ++i)
            if (arrangement.audioClips[static_cast<size_t> (i)].track == t)
                onTrack.push_back (kAudioCell + i);
        if (onTrack.empty())
            continue;
        std::stable_sort (onTrack.begin(), onTrack.end(), [&startOf] (int a, int b) { return startOf (a) < startOf (b); });
        rows.push_back (std::move (onTrack));
        if (rowTracks != nullptr)
            rowTracks->push_back (t);
    }
    return rows;
}

// 0.70.0: a note of a MIDI pad controller (see audio/PerformancePads.h): launch a cell, or stop a row; 0.71.0: 128 + the note
// when a pad is let go (a clip that plays while held stops).
void MainComponent::performancePadNote (int note)
{
    const bool released = note >= 128;
    const auto action = performancePad (note & 127, performancePadFirst, performancePadsPerRow);
    if (action.kind == PadAction::Kind::None || ! performanceOn)
        return;
    std::vector<int> rowTracks;
    const auto rows = performanceRows (&rowTracks);
    if (action.row >= static_cast<int> (rows.size()))
        return;
    const auto& row = rows[static_cast<size_t> (action.row)];
    if (action.kind == PadAction::Kind::StopRow)
    {
        if (! released)
            performanceStop (rowTracks[static_cast<size_t> (action.row)]);
    }
    else if (action.column < static_cast<int> (row.size()))
    {
        if (released)
            performanceRelease (row[static_cast<size_t> (action.column)]);
        else
            performanceLaunch (row[static_cast<size_t> (action.column)]);
    }
}

void MainComponent::performanceTick()
{
    if (! performanceOn)
        return;
    for (int note = engine.takePadNote(); note >= 0; note = engine.takePadNote()) // 0.70.0
        performancePadNote (note);
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
                lane.playingCell = lane.queuedCell;
                lane.playingEndsAt = lane.queuedEndsAt;
                lane.hasQueued = lane.stopQueued = false;
                changed = true;
            }
            if (lane.hasPlaying && lane.playingEndsAt >= 0 && now >= lane.playingEndsAt) // 0.71.0: a one-shot has played
            {
                lane.hasPlaying = false;
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
    model.pads = performancePads;
    model.padFirst = performancePadFirst;
    model.padsPerRow = performancePadsPerRow;
    std::vector<int> rowTracks;
    const auto rows = performanceRows (&rowTracks);
    for (size_t r = 0; r < rows.size(); ++r)
    {
        const int t = rowTracks[r];
        PerformanceModel::Row row;
        row.track = t;
        row.name = playlistView.trackName (t);
        for (const int cell : rows[r])
        {
            PerformanceModel::Cell c;
            c.clip = cell;
            if (cell >= kAudioCell)
            {
                const auto& a = arrangement.audioClips[static_cast<size_t> (cell - kAudioCell)];
                c.name = juce::String (a.name.empty() ? std::string ("Audio") : a.name);
                c.colour = juce::Colour (0xff3fa7b5);
                c.audio = true;
                c.mode = a.launchMode;
                c.sync = a.launchSync;
            }
            else
            {
                const auto& clip = arrangement.clips[static_cast<size_t> (cell)];
                const bool valid = clip.pattern >= 0 && clip.pattern < static_cast<int> (arrangement.patterns.size());
                c.name = valid ? juce::String (arrangement.patterns[static_cast<size_t> (clip.pattern)].name) : juce::String ("?");
                c.colour = juce::Colour::fromHSV (std::fmod (0.31f + static_cast<float> (clip.pattern) * 0.137f, 1.0f), 0.55f, 0.85f, 1.0f);
                c.mode = clip.launchMode;
                c.sync = clip.launchSync;
            }
            row.cells.push_back (c);
        }
        if (static_cast<size_t> (t) < performanceLanes.size())
        {
            const auto& lane = performanceLanes[static_cast<size_t> (t)];
            row.playing = lane.hasPlaying ? lane.playingCell : -1;
            row.queued = lane.hasQueued ? lane.queuedCell : -1;
            row.stopQueued = lane.stopQueued;
        }
        model.rows.push_back (std::move (row));
    }
    performanceWindow->panel().setModel (std::move (model));
}

// 0.71.0: how a cell launches (the cell's menu in the window): the clip keeps it, and it is saved with the project.
void MainComponent::setPerformanceCellMode (int cell, int mode, bool sync)
{
    const bool audio = cell >= kAudioCell;
    const int index = audio ? cell - kAudioCell : cell;
    if (index < 0 || index >= static_cast<int> (audio ? arrangement.audioClips.size() : arrangement.clips.size()))
        return;
    const auto m = static_cast<std::uint8_t> (std::clamp (mode, 0, 2));
    if (audio)
    {
        arrangement.audioClips[static_cast<size_t> (index)].launchMode = m;
        arrangement.audioClips[static_cast<size_t> (index)].launchSync = sync;
    }
    else
    {
        arrangement.clips[static_cast<size_t> (index)].launchMode = m;
        arrangement.clips[static_cast<size_t> (index)].launchSync = sync;
    }
    markDirty();
    refreshPerformancePanel();
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
    panel.onLaunch = [this] (int cell) { performanceLaunch (cell); };
    panel.onRelease = [this] (int cell) { performanceRelease (cell); }; // 0.71.0
    panel.onCellMode = [this] (int cell, int mode, bool sync) { setPerformanceCellMode (cell, mode, sync); };
    panel.onStopTrack = [this] (int track) { performanceStop (track); };
    panel.onStopAll = [this]
    {
        for (int t = 0; t < static_cast<int> (performanceLanes.size()); ++t)
            if (performanceLanes[static_cast<size_t> (t)].hasPlaying || performanceLanes[static_cast<size_t> (t)].hasQueued)
                performanceStop (t);
    };
    panel.onScene = [this] (int column)
    {
        // the column's clip of every row, all at the same boundary (rows with fewer clips go on as they are)
        for (const auto& row : performanceRows())
            if (column < static_cast<int> (row.size()))
                performanceLaunch (row[static_cast<size_t> (column)]);
    };
    panel.onQuantise = [this] (int id) { performanceQuantise = std::clamp (id, 1, 5); };
    panel.onPads = [this] (bool on) // 0.70.0
    {
        performancePads = on;
        engine.setPerformancePads (performanceOn && on);
        refreshPerformancePanel();
        statusLabel.setText (on ? "MIDI pads: the notes from " + juce::MidiMessage::getMidiNoteName (performancePadFirst, true, true, 3)
                                      + " up launch the Performance window's cells (" + juce::String (performancePadsPerRow)
                                      + " per row; the last pad of a row stops it)"
                                : juce::String ("MIDI pads are off: every note plays the selected instrument again"),
                             juce::dontSendNotification);
    };
    panel.onPadLayout = [this] (int firstNote, int perRow) // 0.71.0
    {
        performancePadFirst = std::clamp (firstNote, 0, 120);
        performancePadsPerRow = std::clamp (perRow, 2, 16);
        engine.setPerformancePadLayout (performancePadFirst, performancePadsPerRow);
        refreshPerformancePanel();
    };
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
