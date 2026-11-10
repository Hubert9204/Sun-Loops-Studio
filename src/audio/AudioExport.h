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

// Export choices (0.45.0) besides RenderOptions' sample rate and bit depth (16, 24, or 32 = floating point for WAV; AIFF and FLAC
// take 16 or 24): the bit rate of the lossy formats, and a title and artist written into the file where the format has room for
// them (WAV's INFO list, Ogg comments, MP3 / AAC tags through ffmpeg; FLAC and AIFF get none). 16-bit files are dithered (TPDF).
struct ExportSettings
{
    int kbps = 192;    // MP3, AAC, Ogg Vorbis: 96 .. 320
    std::string title, artist;
};

// Renders `project` (one pass plus tail, like renderToWavFile) and writes it as `format` to `file`. The file is
// written under a temporary name and moved into place, so a failed export never leaves a damaged or half-written file
// where a good one was. `ffmpegOverride` is for tests (empty: findFfmpeg()).
juce::Result exportAudio (const Project& project, const RenderOptions& options, const juce::File& file,
                          ExportFormat format, const juce::File& ffmpegOverride = {}, const ExportSettings& settings = {});

// Stems (0.45.0): one file per mixer track that plays something, rendered with only that track soloed (its sends and the master
// effects included, as in the mix), named "<base> - <track>.<ext>" next to `baseFile`. Silent tracks are skipped. `written`
// receives the files made.
juce::Result exportStems (const Project& project, const RenderOptions& options, const juce::File& baseFile, ExportFormat format,
                          std::vector<juce::File>& written, const juce::File& ffmpegOverride = {}, const ExportSettings& settings = {});

// The mixer tracks a project plays through: those of its channels and audio clips (0.45.0, for stems).
std::vector<int> tracksInUse (const Project& project);

// TPDF dither to 16 bits (0.45.0), deterministic for a given seed.
void ditherTo16Bits (juce::AudioBuffer<float>& buffer, std::uint32_t seed = 1);
} // namespace sls
