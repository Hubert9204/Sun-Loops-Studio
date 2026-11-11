#pragma once

#include <memory>

#include <juce_audio_formats/juce_audio_formats.h>

#include "audio/Arrangement.h"
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

// Reads the first `frames` frames of `reader` as stereo audio (mono is copied to both sides); null when reading fails (0.51.0:
// the import, the sound library and the built-in sounds share it).
std::shared_ptr<SampleData> readSampleData (juce::AudioFormatReader& reader, int frames, std::string name, std::string source);

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

// After a project was opened its audio clips only know the path of their file: reads the audio now (a file path, or
// "builtin:<id>" for a sound inside the program) and stretches it again if the clip was stretched (clips with the same
// file and settings share one rendering). A file that is gone leaves a silent clip that keeps the path, so saving the
// project again does not lose it; every problem is added to `problems`.
void loadAudioForClips (Arrangement& arrangement, juce::StringArray& problems);
} // namespace sls
