#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "library/FlpFile.h"
#include "library/ProjectFile.h"

namespace sls
{
// FL Studio projects to and from Sun Loops Studio projects (0.34.0). The native format stays .slp; a .flp is opened as a new,
// untitled project (saved as .slp) and written only by "Export as FL Studio project".
//
// What carries over: the tempo, the channels' names and sample files, every pattern's notes (pitch, position, length,
// velocity), the playlist's pattern and audio clips on their tracks, and the tracks' names and colours. What does not, and is
// reported: FL Studio's own synths and plugins (a default synth plays their notes here; in the other direction synths, drum
// machines and plugins become empty Sampler channels that keep their notes), effects and the mixer, automation, audio clips'
// trims, fades, stretching and reversing, and arrangements after the first.

struct FlpImportResult
{
    std::optional<ProjectDocument> document;
    std::string error;
    std::vector<std::string> warnings;
};

// `folder` is where the .flp is: a sample whose stored path does not exist here (a Windows path, FL Studio's own folders) is
// looked for there by its file name.
FlpImportResult projectFromFlp (const flp::Song& song, const juce::File& folder = {});
FlpImportResult importFlpFile (const juce::File& file);

struct FlpExportResult
{
    std::vector<std::uint8_t> bytes;
    std::vector<std::string> warnings;
};

FlpExportResult projectToFlp (const ProjectDocument& doc);
} // namespace sls
