#pragma once

#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

#include "audio/OfflineRenderer.h"

namespace sls
{
// Audio file formats the song can be exported to. WAV, AIFF, FLAC and Ogg Vorbis (.ogg / .oga) are written by the
// program itself. MP3 and AAC need the `ffmpeg` program to be installed (they are encoded by it), because JUCE has no
// encoder for them on Linux; the song is rendered to a temporary WAV and ffmpeg converts it.
enum class ExportFormat { Wav, Aiff, Flac, Ogg, Oga, Mp3, Aac };

struct ExportFormatInfo
{
    ExportFormat format;
    const char* extension;   // with the dot, lower case
    const char* label;       // for menus
    bool lossy;
    bool needsFfmpeg;
};

const std::vector<ExportFormatInfo>& exportFormats();
const ExportFormatInfo& exportFormatInfo (ExportFormat format);

// The format a file name asks for (by extension, case-insensitive); empty when it is not one we export.
bool exportFormatForFile (const juce::File& file, ExportFormat& result);

// The ffmpeg program, if there is one: the SLS_FFMPEG environment variable, else the first `ffmpeg` on PATH or in the
// usual system folders. A default-constructed File when none is found.
juce::File findFfmpeg();

// Renders `project` (one pass plus tail, like renderToWavFile) and writes it as `format` to `file`. The file is
// written under a temporary name and moved into place, so a failed export never leaves a damaged or half-written file
// where a good one was. `ffmpegOverride` is for tests (empty: findFfmpeg()).
juce::Result exportAudio (const Project& project, const RenderOptions& options, const juce::File& file,
                          ExportFormat format, const juce::File& ffmpegOverride = {});
} // namespace sls
