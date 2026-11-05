#pragma once

#include <memory>

#include <juce_audio_formats/juce_audio_formats.h>

#include "audio/Project.h"

namespace sls
{
// Reading audio for the playlist: audio files and the sound track of video files.
//
// WAV, AIFF, FLAC, Ogg Vorbis (.ogg / .oga) and MP3 are decoded by the program itself. AAC (.aac, .m4a) and the audio
// of video files need the `ffmpeg` program (JUCE has no decoder for them on Linux); without it the import fails with
// a message that says so. The audio is kept in memory, so at most kMaxImportSeconds are read from a file; a longer
// file is cut there and the result says so in `warning`.
constexpr double kMaxImportSeconds = 300.0;

enum class FileKind { Unsupported, Audio, Video };

// What a file is, judged by its extension (case-insensitive).
FileKind classifyMediaFile (const juce::File& file);
const juce::StringArray& importableAudioExtensions(); // with the dot, lower case
const juce::StringArray& importableVideoExtensions();

struct ImportResult
{
    std::shared_ptr<const SampleData> data; // null on failure
    juce::File file;                        // the audio file `data` was read from (for a video: the extracted WAV)
    juce::String error;                     // set when `data` is null
    juce::String warning;                   // something was cut or repaired, but the import worked
};

// Decodes an audio file (stereo, at its own sample rate; mono is copied to both sides). `source` of the result is the
// file's absolute path.
ImportResult importAudioFile (const juce::File& file, const juce::File& ffmpegOverride = {});

// Extracts the sound of a video into a new WAV file in `destinationFolder` (named after the video, never replacing a
// file) and decodes it. Fails with a message when ffmpeg is missing or the video has no sound.
ImportResult importVideoAudio (const juce::File& video, const juce::File& destinationFolder,
                               const juce::File& ffmpegOverride = {});

// Either of the above, by the kind of file; Unsupported files fail with a message.
ImportResult importMediaFile (const juce::File& file, const juce::File& videoDestinationFolder,
                              const juce::File& ffmpegOverride = {});
} // namespace sls
