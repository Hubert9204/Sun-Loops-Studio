#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

#include "audio/Arrangement.h"
#include "audio/Project.h"

namespace sls
{
// Everything that makes up a song, in a form that can be written to and read from a file.
// Stored as JSON in a ".slsproj" file. Samples are not embedded: a channel refers to its sample by
// "builtin:<id>" or by absolute file path, and the caller loads it (a missing file is reported, not fatal).
constexpr const char* kProjectExtension = ".slsproj";
constexpr int kProjectFormatVersion = 1;
constexpr const char* kProjectFormatName = "sun-loops-studio-project";

struct ProjectParseResult;

struct ProjectDocument
{
    std::vector<Channel> channels;           // 1..kMaxChannels; `notes` and `sample` are empty (see below)
    std::vector<std::string> sampleSources;  // one per channel: "builtin:<id>", an absolute path, or empty
    std::vector<MixerTrack> tracks;          // exactly kMaxTracks
    std::array<EffectSlot, kSlotsPerTrack> masterSlots {}; // effects on the master bus
    Arrangement arrangement;                 // the notes of every pattern live here
    int currentPattern = 0;
    bool songMode = false;
    double bpm = 120.0;
    float masterVolume = 0.8f;

    // A new, empty project: one synth channel, one empty pattern, an empty playlist.
    static ProjectDocument blank();

    // Channel notes belong to the patterns, so a document never stores `channels[i].notes`.
    juce::String toJson() const;

    // Strict about the structure (right format name, a version this program understands, the expected types and
    // counts); lenient about values: numbers are clamped into their valid range, NaN becomes the default, notes that
    // do not fit in their pattern and clips that cannot be placed are dropped (and reported in `warnings`).
    static ProjectParseResult fromJson (const juce::String& text);
};

struct ProjectParseResult
{
    std::optional<ProjectDocument> document;
    std::string error;                  // set when `document` is empty
    std::vector<std::string> warnings;  // things that were repaired or dropped while reading
};

// A sample that could not be found when a project was opened. It holds no audio (a sampler channel with it is
// silent) but remembers where the file was, so saving the project again keeps the reference instead of losing it.
std::shared_ptr<const SampleData> makeMissingSample (const std::string& source);

// Reads a project file. Files over kMaxProjectFileBytes are refused (a project is a few hundred KB at most).
constexpr juce::int64 kMaxProjectFileBytes = 32LL * 1024 * 1024;
ProjectParseResult readProjectFile (const juce::File& file);

// Writes the project so that an existing file is never left half-written: the text goes to a temporary file
// next to the target, which then replaces it. Returns an error message, empty on success.
juce::String writeProjectFile (const juce::File& file, const ProjectDocument& document);

// "My Song" -> "My Song.slsproj"; names that already end in the extension are left alone.
juce::File withProjectExtension (const juce::File& file);

// "Untitled 2026-11-05 21-54-07.slsproj" in `folder`, never an existing file.
juce::File automaticProjectFile (const juce::File& folder, juce::Time now);
} // namespace sls
