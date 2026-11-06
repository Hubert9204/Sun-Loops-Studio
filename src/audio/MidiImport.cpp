#include "audio/MidiImport.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

#include <juce_audio_basics/juce_audio_basics.h>

namespace sls
{
namespace
{
struct Pending
{
    bool open = false;
    int startTick = 0;
    float velocity = 0.8f;
};
} // namespace

MidiImportResult parseMidiFile (const void* data, std::size_t size)
{
    MidiImportResult result;
    if (data == nullptr || size < 14)
    {
        result.error = "This is not a MIDI file (too short)";
        return result;
    }
    if (size > kMaxMidiFileBytes)
    {
        result.error = "The MIDI file is larger than 8 MB";
        return result;
    }

    juce::MemoryInputStream stream (data, size, false);
    juce::MidiFile file;
    if (! file.readFrom (stream))
    {
        result.error = "This is not a valid MIDI file";
        return result;
    }

    const int division = file.getTimeFormat();
    if (division <= 0)
    {
        result.error = "MIDI files timed in SMPTE frames are not supported (only beats)";
        return result;
    }

    const auto toTick = [division] (double t)
    {
        return static_cast<int> (std::llround (std::max (0.0, t) * kTicksPerBeat / division));
    };

    int noteCount = 0;
    for (int t = 0; t < file.getNumTracks(); ++t)
    {
        const auto* sequence = file.getTrack (t);
        if (sequence == nullptr)
            continue;

        std::string trackName;
        std::array<std::array<Pending, 128>, 16> pending {};
        std::map<int, MidiImportTrack> byChannel; // MIDI channel (0..15) -> notes

        const auto close = [&] (int channel, int pitch, int endTick)
        {
            auto& p = pending[static_cast<size_t> (channel)][static_cast<size_t> (pitch)];
            if (! p.open)
                return;
            p.open = false;
            Note n;
            n.pitch = pitch;
            n.startTick = p.startTick;
            n.lengthTicks = std::max (1, endTick - p.startTick);
            n.velocity = p.velocity;
            auto& track = byChannel[channel];
            track.midiChannel = channel + 1;
            track.percussion = channel == 9;
            track.notes.push_back (n);
            result.lengthTicks = std::max (result.lengthTicks, n.startTick + n.lengthTicks);
            ++noteCount;
        };

        int lastTick = 0;
        for (int i = 0; i < sequence->getNumEvents(); ++i)
        {
            const auto& m = sequence->getEventPointer (i)->message;
            const int tick = toTick (m.getTimeStamp());
            lastTick = std::max (lastTick, tick);

            if (m.isTempoMetaEvent())
            {
                const double spq = m.getTempoSecondsPerQuarterNote();
                if (spq > 0.0)
                {
                    const double bpm = 60.0 / spq;
                    if (result.bpm <= 0.0)
                        result.bpm = bpm;
                    else if (std::abs(bpm - result.bpm) > 0.5)
                        result.tempoChanges = true;
                }
            }
            else if (m.isTrackNameEvent() && trackName.empty())
            {
                trackName = m.getTextFromTextMetaEvent().trim().toStdString();
            }
            else if (m.isNoteOn (false))
            {
                if (noteCount >= kMaxMidiNotes)
                {
                    result.error = "The MIDI file has more than " + std::to_string (kMaxMidiNotes) + " notes";
                    return result;
                }
                const int channel = std::clamp (m.getChannel() - 1, 0, 15);
                const int pitch = m.getNoteNumber();
                if (pitch < 0 || pitch > 127)
                    continue;
                close (channel, pitch, tick); // a repeated note on ends the one that was sounding
                auto& p = pending[static_cast<size_t> (channel)][static_cast<size_t> (pitch)];
                p.open = true;
                p.startTick = tick;
                p.velocity = std::clamp (static_cast<float> (m.getVelocity()) / 127.0f, 0.0f, 1.0f);
            }
            else if (m.isNoteOff (true))
            {
                const int pitch = m.getNoteNumber();
                if (pitch >= 0 && pitch <= 127)
                    close (std::clamp (m.getChannel() - 1, 0, 15), pitch, tick);
            }
        }

        for (int channel = 0; channel < 16; ++channel) // notes never switched off end with the track
            for (int pitch = 0; pitch < 128; ++pitch)
                close (channel, pitch, lastTick);

        for (auto& entry : byChannel)
        {
            auto& track = entry.second;
            std::stable_sort (track.notes.begin(), track.notes.end(),
                              [] (const Note& a, const Note& b) { return a.startTick < b.startTick; });
            std::string base = trackName.empty() ? "Track " + std::to_string (t + 1) : trackName;
            track.name = byChannel.size() > 1 ? base + " (ch " + std::to_string (track.midiChannel) + ")" : base;
            result.tracks.push_back (std::move (track));
        }
    }

    if (result.tracks.empty())
    {
        result.error = "The MIDI file has no notes";
        return result;
    }

    result.ok = true;
    return result;
}

std::vector<std::vector<Note>> splitIntoPatterns (const std::vector<Note>& notes, int patternTicks, int maxPatterns,
                                                  int* tied, int* cut)
{
    std::vector<std::vector<Note>> patterns;
    if (patternTicks <= 0 || maxPatterns <= 0)
        return patterns;

    for (const auto& note : notes)
    {
        if (note.startTick < 0 || note.lengthTicks <= 0)
            continue;

        int start = note.startTick;
        int remaining = note.lengthTicks;
        bool first = true;
        while (remaining > 0)
        {
            const int index = start / patternTicks;
            if (index >= maxPatterns)
            {
                if (first && cut != nullptr)
                    ++*cut;
                break;
            }
            const int local = start - index * patternTicks;
            const int length = std::min (remaining, patternTicks - local);
            if (static_cast<int> (patterns.size()) <= index)
                patterns.resize (static_cast<size_t> (index) + 1);
            Note part = note;
            part.startTick = local;
            part.lengthTicks = length;
            patterns[static_cast<size_t> (index)].push_back (part);
            if (! first && tied != nullptr)
                ++*tied;
            first = false;
            start += length;
            remaining -= length;
        }
    }

    for (auto& p : patterns)
        std::stable_sort (p.begin(), p.end(), [] (const Note& a, const Note& b) { return a.startTick < b.startTick; });
    return patterns;
}

MidiImportPlan planMidiImport (const MidiImportResult& file, int freeChannels, int patternTicks, int maxPatterns)
{
    MidiImportPlan plan;
    std::vector<std::vector<std::vector<Note>>> perChannel; // [channel][pattern]
    for (const auto& track : file.tracks)
    {
        if (track.percussion)
        {
            ++plan.skippedPercussion;
            continue;
        }
        if (static_cast<int> (perChannel.size()) >= freeChannels)
        {
            ++plan.skippedTracks;
            continue;
        }
        plan.channelNames.push_back (track.name);
        perChannel.push_back (splitIntoPatterns (track.notes, patternTicks, maxPatterns, &plan.tiedNotes, &plan.cutNotes));
    }

    size_t patterns = 0;
    for (const auto& c : perChannel)
        patterns = std::max (patterns, c.size());

    plan.patternNotes.assign (patterns, std::vector<std::vector<Note>> (perChannel.size()));
    for (size_t c = 0; c < perChannel.size(); ++c)
        for (size_t p = 0; p < perChannel[c].size(); ++p)
            plan.patternNotes[p][c] = std::move (perChannel[c][p]);
    return plan;
}
} // namespace sls
