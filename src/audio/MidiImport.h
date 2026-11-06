#pragma once

#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

#include "audio/NoteSequence.h"

namespace sls
{
// Importing a Standard MIDI File (.mid / .midi, format 0 or 1) into patterns. Pure functions, no GUI: the caller (MainComponent)
// applies the plan to the project.

// One MIDI track and channel that has notes. Ticks are Sun Loops Studio ticks (480 per beat), starting at 0.
struct MidiImportTrack
{
    std::string name;       // the track name of the file, or "Track 3"; "Piano (ch 2)" when a track holds several channels
    int midiChannel = 1;    // 1..16
    bool percussion = false; // channel 10 (General MIDI drums: the pitch picks the drum, not a note)
    std::vector<Note> notes; // sorted by start
};

struct MidiImportResult
{
    bool ok = false;
    std::string error;
    double bpm = 0.0;        // the first tempo of the file; 0 if it has none
    bool tempoChanges = false; // the file changes tempo later on (only the first tempo is used)
    int lengthTicks = 0;     // end of the last note
    std::vector<MidiImportTrack> tracks;
};

constexpr std::size_t kMaxMidiFileBytes = 8u * 1024u * 1024u;
constexpr int kMaxMidiNotes = 200000;

inline bool isMidiFileName (const std::string& name)
{
    const auto ends = [&name] (const char* ext)
    {
        const std::string e (ext);
        if (name.size() < e.size())
            return false;
        for (std::size_t i = 0; i < e.size(); ++i)
            if (std::tolower (static_cast<unsigned char> (name[name.size() - e.size() + i])) != e[i])
                return false;
        return true;
    };
    return ends (".mid") || ends (".midi");
}

MidiImportResult parseMidiFile (const void* data, std::size_t size);

// What importing would do: new channels (in order) and, for each new pattern, the notes of each new channel.
struct MidiImportPlan
{
    std::vector<std::string> channelNames;                     // one per new channel
    std::vector<std::vector<std::vector<Note>>> patternNotes;  // [pattern][new channel], ticks relative to the pattern
    int skippedPercussion = 0;  // drum tracks left out
    int skippedTracks = 0;      // tracks that did not fit in the free channels
    int cutNotes = 0;           // notes that began after the room for patterns ran out
    int tiedNotes = 0;          // notes that crossed a pattern boundary and were continued in the next pattern (they sound again)
    int patternCount() const noexcept { return static_cast<int> (patternNotes.size()); }
};

// `freeChannels`: how many channels can still be added; `patternTicks`: the length of one pattern; `maxPatterns`: how many
// patterns may still be added. Notes are cut at pattern boundaries and continued in the next pattern.
MidiImportPlan planMidiImport (const MidiImportResult& file, int freeChannels, int patternTicks, int maxPatterns);

// The building block of the plan: spreads notes over patterns of `patternTicks`. Returns one list per pattern, up to
// `maxPatterns` (counts into tied / cut if given).
std::vector<std::vector<Note>> splitIntoPatterns (const std::vector<Note>& notes, int patternTicks, int maxPatterns,
                                                  int* tied = nullptr, int* cut = nullptr);
} // namespace sls
