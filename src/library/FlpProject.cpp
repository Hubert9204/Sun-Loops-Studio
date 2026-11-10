#include "library/FlpProject.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "audio/StepSequence.h"
#include "library/JsonRead.h"

namespace sls
{
namespace
{
std::string trimName (const std::string& s, size_t limit)
{
    const auto t = juce::String (s).trim().substring (0, static_cast<int> (limit));
    return t.toStdString();
}

int toOurTicks (long long ticks, int ppq)
{
    return static_cast<int> (std::llround (static_cast<double> (ticks) * kTicksPerBeat / std::max (1, ppq)));
}

// A sample's path as stored in the .flp, made into a file here: as it is (with / for \), or the same file name next to the .flp
// (or in a folder beside it), which is how projects travel with their samples.
std::string resolveSample (const std::string& stored, const juce::File& folder)
{
    if (stored.empty())
        return {};
    const auto unixPath = juce::String (stored).replaceCharacter ('\\', '/');
    if (juce::File::isAbsolutePath (unixPath) && juce::File (unixPath).existsAsFile())
        return juce::File (unixPath).getFullPathName().toStdString();
    const auto name = unixPath.fromLastOccurrenceOf ("/", false, false);
    if (folder.isDirectory() && name.isNotEmpty())
    {
        if (const auto here = folder.getChildFile (name); here.existsAsFile())
            return here.getFullPathName().toStdString();
        for (const auto& sub : folder.findChildFiles (juce::File::findDirectories, false))
            if (const auto there = sub.getChildFile (name); there.existsAsFile())
                return there.getFullPathName().toStdString();
    }
    // Not found: keep a usable absolute path so the project reports it as missing (and keeps the reference).
    if (juce::File::isAbsolutePath (unixPath))
        return unixPath.toStdString();
    return (folder.isDirectory() ? folder.getChildFile (name) : juce::File ("/" + name)).getFullPathName().toStdString();
}
} // namespace

FlpImportResult projectFromFlp (const flp::Song& song, const juce::File& folder)
{
    FlpImportResult result;
    auto doc = ProjectDocument::blank();
    doc.channels.clear();
    doc.sampleSources.clear();
    doc.kitSpecs.clear();
    doc.bpm = std::clamp (song.bpm, 10.0, 999.0);
    const int ppq = std::max (1, song.ppq);
    auto& warnings = result.warnings;

    // Which channels the patterns use, and which the playlist uses as audio clips.
    std::set<int> notesUse, itemsUse;
    for (const auto& p : song.patterns)
        for (const auto& n : p.notes)
            notesUse.insert (n.channel);
    for (const auto& item : song.items)
        if (item.pattern == 0)
            itemsUse.insert (item.channel);

    // --- channels
    std::map<int, int> channelOf;            // iid -> our channel
    std::map<int, std::string> audioSource;  // iid -> sample file, for audio clips
    int skippedKinds = 0, overLimit = 0, defaultSynths = 0, emptySamplers = 0;
    std::set<std::string> pluginNames;
    for (const auto& c : song.channels)
    {
        const auto path = resolveSample (c.samplePath, folder);
        if (! path.empty())
            audioSource[c.iid] = path;
        if (c.type == static_cast<int> (flp::ChannelType::Automation) || c.type == static_cast<int> (flp::ChannelType::Layer))
        {
            ++skippedKinds;
            continue;
        }
        if (itemsUse.count (c.iid) > 0 && notesUse.count (c.iid) == 0 && ! path.empty())
            continue; // only an audio clip's source: not a channel of the rack here
        if (doc.channels.size() >= static_cast<size_t> (kMaxChannels))
        {
            ++overLimit;
            continue;
        }
        Channel ch;
        ch.name = trimName (! c.name.empty() ? c.name : (! c.plugin.empty() ? c.plugin : "Channel " + std::to_string (doc.channels.size() + 1)), 16);
        std::string source;
        if (! path.empty())
        {
            ch.instrument = Instrument::Sampler;
            ch.rootPitch = std::clamp (c.rootNote, 0, 127);
            source = path;
        }
        else if (c.type == static_cast<int> (flp::ChannelType::Sampler))
        {
            ch.instrument = Instrument::Sampler;
            ++emptySamplers;
        }
        else
        {
            ch.instrument = Instrument::Synth; // FL Studio's synths and plugins: a default synth plays the notes
            ++defaultSynths;
            if (! c.plugin.empty())
                pluginNames.insert (c.plugin == "Fruity Wrapper" ? c.name : c.plugin);
        }
        ch.mixerTrack = std::min (static_cast<int> (doc.channels.size()), static_cast<int> (doc.tracks.size()) - 1);
        channelOf[c.iid] = static_cast<int> (doc.channels.size());
        doc.channels.push_back (ch);
        doc.sampleSources.push_back (source);
        doc.kitSpecs.push_back (KitSpec::empty());
    }
    if (doc.channels.empty())
    {
        doc.channels.push_back (ProjectDocument::blank().channels.front());
        doc.sampleSources.emplace_back();
        doc.kitSpecs.push_back (KitSpec::empty());
    }
    if (defaultSynths > 0)
    {
        std::string names;
        for (const auto& n : pluginNames)
            names += (names.empty() ? "" : ", ") + n;
        warnings.push_back (std::to_string (defaultSynths) + " channel(s) used FL Studio instruments or plugins" + (names.empty() ? "" : " (" + names + ")")
                            + ": a default synth plays their notes here.");
    }
    if (emptySamplers > 0)
        warnings.push_back (std::to_string (emptySamplers) + " Sampler channel(s) had no sample: they are silent until you load one.");
    if (skippedKinds > 0)
        warnings.push_back (std::to_string (skippedKinds) + " automation or layer channel(s) were left out.");
    if (overLimit > 0)
        warnings.push_back (std::to_string (overLimit) + " channel(s) beyond the first " + std::to_string (kMaxChannels) + " were left out.");

    // --- patterns: one length for all of them here (1, 2, 4 or 8 bars), the shortest that holds the longest
    // (A pattern's own fixed length counts only when it has notes; then the length is the longest of them.)
    int longest = 0;
    for (const auto& p : song.patterns)
    {
        int extent = 0;
        for (const auto& n : p.notes)
            if (channelOf.count (n.channel) > 0)
                extent = std::max (extent, toOurTicks (static_cast<long long> (n.position) + std::max (0, n.length), ppq));
        if (extent > 0)
            longest = std::max ({ longest, extent, toOurTicks (p.length, ppq) });
    }
    constexpr int bar = 4 * kTicksPerBeat;
    int bars = kPatternBarChoices[std::size (kPatternBarChoices) - 1];
    for (const int choice : kPatternBarChoices)
        if (choice * bar >= longest)
        {
            bars = choice;
            break;
        }
    // (The shortest length that holds the notes is also the one that lets the playlist's clips sit where they were.)
    auto& arrangement = doc.arrangement;
    arrangement.patternTicks = bars * bar;
    arrangement.patterns.clear();

    std::map<int, int> patternOf; // FL index -> ours
    int droppedNotes = 0, extraPatterns = 0;
    for (const auto& p : song.patterns)
    {
        if (arrangement.patterns.size() >= static_cast<size_t> (kMaxPatterns))
        {
            ++extraPatterns;
            continue;
        }
        Pattern ours;
        ours.name = trimName (p.name.empty() ? "Pattern " + std::to_string (p.index) : p.name, kMaxNameLength);
        ours.notes.resize (doc.channels.size());
        for (const auto& n : p.notes)
        {
            const auto it = channelOf.find (n.channel);
            if (it == channelOf.end())
                continue;
            Note note;
            note.pitch = std::clamp (n.key, 0, 127);
            note.startTick = toOurTicks (n.position, ppq);
            note.lengthTicks = n.length > 0 ? std::max (1, toOurTicks (n.length, ppq)) : kSixteenthTicks; // 0: a step
            note.velocity = std::clamp (static_cast<float> (n.velocity) / 128.0f, 0.0f, 1.0f);
            if (note.startTick >= arrangement.patternTicks)
            {
                ++droppedNotes;
                continue;
            }
            note.lengthTicks = std::min (note.lengthTicks, arrangement.patternTicks - note.startTick);
            ours.notes[static_cast<size_t> (it->second)].push_back (note);
        }
        for (auto& list : ours.notes)
            std::stable_sort (list.begin(), list.end(), [] (const Note& a, const Note& b) { return a.startTick != b.startTick ? a.startTick < b.startTick : a.pitch < b.pitch; });
        // Every channel plays as written (FL Studio does not repeat a short step sequence the way the rack here does).
        ours.steps.assign (doc.channels.size(), steps::patternSteps (arrangement.patternTicks));
        patternOf[p.index] = static_cast<int> (arrangement.patterns.size());
        arrangement.patterns.push_back (std::move (ours));
    }
    if (arrangement.patterns.empty())
    {
        Pattern empty;
        empty.name = "Pattern 1";
        empty.notes.resize (doc.channels.size());
        arrangement.patterns.push_back (std::move (empty));
    }
    arrangement.ensureChannelCount (doc.channels.size());
    if (droppedNotes > 0)
        warnings.push_back (std::to_string (droppedNotes) + " note(s) past " + std::to_string (bars) + " bars were left out (patterns are at most "
                            + std::to_string (kPatternBarChoices[std::size (kPatternBarChoices) - 1]) + " bars long here).");
    if (extraPatterns > 0)
        warnings.push_back (std::to_string (extraPatterns) + " pattern(s) beyond the first " + std::to_string (kMaxPatterns) + " were left out.");

    // --- the playlist: FL Studio's tracks (up to 500) onto the 8 here, in their order
    std::set<int> usedTracks;
    for (const auto& item : song.items)
        usedTracks.insert (item.track);
    std::map<int, int> trackOf;
    int mergedTracks = 0;
    for (const int t : usedTracks)
    {
        const int ours = std::min (static_cast<int> (trackOf.size()), kPlaylistTracks - 1);
        if (static_cast<int> (trackOf.size()) >= kPlaylistTracks)
            ++mergedTracks;
        trackOf[t] = ours;
    }
    for (const auto& t : song.tracks)
        if (const auto it = trackOf.find (t.index); it != trackOf.end() && static_cast<int> (std::count_if (trackOf.begin(), trackOf.end(), [&] (const auto& kv) { return kv.second == it->second; })) == 1)
        {
            auto& pt = arrangement.playlistTracks[static_cast<size_t> (it->second)];
            pt.name = trimName (t.name, 32);
            if (t.hasColour)
                pt.colour = 0xff000000u | (t.rgb & 0xffffffu);
        }

    const double unitInFlTicks = static_cast<double> (arrangement.patternTicks) * ppq / kTicksPerBeat;
    int droppedItems = 0, droppedAudio = 0;
    for (const auto& item : song.items)
    {
        const int track = trackOf[item.track];
        if (item.pattern > 0)
        {
            const auto it = patternOf.find (item.pattern);
            if (it == patternOf.end())
            {
                ++droppedItems;
                continue;
            }
            Clip clip;
            clip.pattern = it->second;
            clip.track = track;
            // Since 0.42.0 clips go anywhere: FL Studio's position and length are kept (to the nearest tick).
            const double ticksPerFl = static_cast<double> (kTicksPerBeat) / ppq;
            clip.startTick = static_cast<int> (std::lround (item.position * ticksPerFl));
            clip.lengthTicks = std::max (1, static_cast<int> (std::lround ((item.length > 0 ? item.length : unitInFlTicks) * ticksPerFl)));
            if (arrangement.canPlace (clip))
                arrangement.clips.push_back (clip);
            else
                ++droppedItems;
        }
        else
        {
            const auto source = audioSource.find (item.channel);
            if (source == audioSource.end() || arrangement.audioClips.size() >= static_cast<size_t> (kMaxAudioClips))
            {
                ++droppedAudio;
                continue;
            }
            AudioClip clip;
            clip.data = makeMissingSample (source->second); // loaded by the program, like a saved project's
            clip.name = trimName (juce::File (juce::String (source->second)).getFileNameWithoutExtension().toStdString(), kMaxNameLength);
            clip.track = track;
            clip.startTick = toOurTicks (item.position, ppq);
            clip.lengthTicks = std::max (1, toOurTicks (item.length, ppq));
            if (arrangement.canPlaceAudio (clip))
                arrangement.audioClips.push_back (std::move (clip));
            else
                ++droppedAudio;
        }
    }
    if (mergedTracks > 0)
        warnings.push_back (std::to_string (mergedTracks) + " playlist track(s) beyond the first " + std::to_string (kPlaylistTracks)
                            + " used were put on track " + std::to_string (kPlaylistTracks) + ".");
    if (droppedItems > 0)
        warnings.push_back (std::to_string (droppedItems) + " pattern clip(s) did not fit in the playlist and were left out.");
    if (droppedAudio > 0)
        warnings.push_back (std::to_string (droppedAudio) + " audio clip(s) were left out (no sample file, or no room).");
    if (song.arrangements > 1)
        warnings.push_back ("Only the first of the " + std::to_string (song.arrangements) + " arrangements was opened.");
    warnings.push_back ("Not carried over from FL Studio: effects and the mixer, automation, plugin settings, audio clips' trims and fades.");

    doc.songMode = ! arrangement.clips.empty() || ! arrangement.audioClips.empty();
    doc.currentPattern = 0;
    result.document = std::move (doc);
    return result;
}

FlpImportResult importFlpFile (const juce::File& file)
{
    FlpImportResult result;
    if (! file.existsAsFile())
    {
        result.error = "The file does not exist.";
        return result;
    }
    if (file.getSize() > 512LL * 1024 * 1024)
    {
        result.error = "The file is too large to be an FL Studio project.";
        return result;
    }
    juce::MemoryBlock block;
    if (! file.loadFileAsData (block))
    {
        result.error = "The file could not be read.";
        return result;
    }
    const auto parsed = flp::parse (static_cast<const std::uint8_t*> (block.getData()), block.getSize());
    if (! parsed.song.has_value())
    {
        result.error = parsed.error;
        return result;
    }
    return projectFromFlp (*parsed.song, file.getParentDirectory());
}

FlpExportResult projectToFlp (const ProjectDocument& doc)
{
    FlpExportResult result;
    flp::Song song;
    song.bpm = doc.bpm;
    const auto& arrangement = doc.arrangement;
    constexpr int scale = flp::kWrittenPpq / kTicksPerBeat; // 2: exact

    int soundless = 0;
    for (size_t i = 0; i < doc.channels.size(); ++i)
    {
        const auto& c = doc.channels[i];
        flp::Channel out;
        out.iid = static_cast<int> (i);
        out.name = c.name;
        out.type = static_cast<int> (flp::ChannelType::Sampler);
        const auto source = i < doc.sampleSources.size() ? doc.sampleSources[i] : std::string();
        const bool fileSample = c.instrument == Instrument::Sampler && c.program == nullptr && ! source.empty() && source.rfind ("builtin:", 0) != 0;
        if (fileSample)
        {
            out.samplePath = source;
            out.rootNote = c.rootPitch;
        }
        else
            ++soundless;
        song.channels.push_back (out);
    }

    for (size_t p = 0; p < arrangement.patterns.size(); ++p)
    {
        const auto& pattern = arrangement.patterns[p];
        flp::Pattern out;
        out.index = static_cast<int> (p) + 1;
        out.name = pattern.name;
        out.length = arrangement.patternTicks * scale;
        for (size_t c = 0; c < doc.channels.size() && c < pattern.notes.size(); ++c)
            for (const auto& n : steps::playedNotes (pattern.notes[c], arrangement.patternTicks))
            {
                flp::Note note;
                note.position = n.startTick * scale;
                note.length = n.lengthTicks * scale;
                note.channel = static_cast<int> (c);
                note.key = n.pitch;
                note.velocity = static_cast<int> (std::lround (std::clamp (n.velocity, 0.0f, 1.0f) * 128.0f));
                out.notes.push_back (note);
            }
        std::stable_sort (out.notes.begin(), out.notes.end(), [] (const flp::Note& a, const flp::Note& b) { return a.position < b.position; });
        song.patterns.push_back (std::move (out));
    }

    // A pattern clip is one item per repeat (FL Studio's own clips do not repeat a pattern by themselves), the last one cut at the
    // clip's end. A clip that starts inside its pattern (split) cannot say so in this writer: its first item starts the pattern from
    // its beginning.
    int offsetClips = 0;
    for (const auto& clip : arrangement.clips)
    {
        const int length = std::max (1, arrangement.patternTicks);
        const int offset = ((clip.offsetTicks % length) + length) % length;
        offsetClips += offset != 0 ? 1 : 0;
        for (int pass = -offset; pass < clip.lengthTicks; pass += length)
        {
            const int from = std::max (pass, 0);
            const int to = std::min (pass + length, clip.lengthTicks);
            if (to <= from)
                continue;
            flp::Item item;
            item.position = (clip.startTick + from) * scale;
            item.length = (to - from) * scale;
            item.track = clip.track;
            item.pattern = clip.pattern + 1;
            song.items.push_back (item);
        }
    }
    if (offsetClips > 0)
        result.warnings.push_back (std::to_string (offsetClips) + " pattern clip(s) that start inside their pattern start it from its beginning in FL Studio.");

    // Audio clips: one audio clip channel per file, after the rack's channels.
    std::map<std::string, int> audioChannel;
    int changedAudio = 0, missingAudio = 0;
    for (const auto& clip : arrangement.audioClips)
    {
        const std::string source = clip.data != nullptr ? clip.data->source : std::string();
        if (source.empty())
        {
            ++missingAudio;
            continue;
        }
        auto it = audioChannel.find (source);
        if (it == audioChannel.end())
        {
            flp::Channel ch;
            ch.iid = static_cast<int> (song.channels.size());
            ch.type = static_cast<int> (flp::ChannelType::Instrument);
            ch.name = clip.name.empty() ? juce::File (juce::String (source)).getFileNameWithoutExtension().toStdString() : clip.name;
            ch.samplePath = source;
            song.channels.push_back (ch);
            it = audioChannel.emplace (source, ch.iid).first;
        }
        flp::Item item;
        item.position = clip.startTick * scale;
        item.length = clip.lengthTicks * scale;
        item.track = clip.track;
        item.channel = it->second;
        song.items.push_back (item);
        if (clip.offsetSeconds > 0.0 || clip.reverse || clip.fadeInSeconds > 0.0 || clip.fadeOutSeconds > 0.0 || ! clip.stretch.isNeutral()
            || std::abs (clip.pan) > 1.0e-6f || std::abs (clip.gain - 1.0f) > 1.0e-6f)
            ++changedAudio;
    }

    for (int t = 0; t < kPlaylistTracks; ++t)
    {
        const auto& pt = arrangement.playlistTracks[static_cast<size_t> (t)];
        if (pt == PlaylistTrack {})
            continue;
        flp::Track out;
        out.index = t;
        out.name = pt.name;
        out.hasColour = pt.colour != 0;
        out.rgb = pt.colour & 0xffffffu;
        song.tracks.push_back (out);
    }

    if (soundless > 0)
        result.warnings.push_back (std::to_string (soundless) + " channel(s) (synths, drum machines, plugins, built-in or library sounds) become empty "
                                   "Sampler channels in FL Studio: their notes are kept, choose a sound there.");
    if (changedAudio > 0)
        result.warnings.push_back (std::to_string (changedAudio) + " audio clip(s) play their file from the start in FL Studio, without their trim, "
                                   "fades, gain, pan, stretching or reversing.");
    if (missingAudio > 0)
        result.warnings.push_back (std::to_string (missingAudio) + " audio clip(s) without a file were left out.");
    if (! arrangement.automationClips.empty())
        result.warnings.push_back ("Automation clips are not written.");
    result.warnings.push_back ("Not written: effects and the mixer, the channels' sound settings. Step sequences that repeat are written out in full.");

    result.bytes = flp::write (song);
    return result;
}
} // namespace sls
