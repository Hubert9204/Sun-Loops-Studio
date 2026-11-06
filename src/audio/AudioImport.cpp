#include "audio/AudioImport.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

#include "audio/AudioExport.h"
#include "audio/BuiltinSamples.h"

namespace sls
{
namespace
{
juce::String ffmpegMissingMessage (const juce::File& file)
{
    return "Reading " + file.getFileName()
         + " needs the ffmpeg program, which was not found. Install it (on Fedora / Nobara: sudo dnf install ffmpeg).";
}

bool hasExtensionIn (const juce::File& file, const juce::StringArray& extensions)
{
    return extensions.contains (file.getFileExtension().toLowerCase());
}

// Plain decoding with JUCE's own readers.
ImportResult decodeNative (const juce::File& file)
{
    ImportResult result;

    std::unique_ptr<juce::AudioFormatReader> reader;
    if (file.hasFileExtension ("oga")) // the format manager only knows .ogg; the content is the same
    {
        juce::OggVorbisAudioFormat ogg;
        if (auto stream = file.createInputStream())
            reader.reset (ogg.createReaderFor (stream.release(), true));
    }
    else
    {
        juce::AudioFormatManager manager;
        manager.registerBasicFormats();
        reader.reset (manager.createReaderFor (file));
    }

    if (reader == nullptr)
    {
        result.error = "Cannot read " + file.getFileName();
        return result;
    }
    if (reader->numChannels < 1 || reader->sampleRate < 1000.0)
    {
        result.error = file.getFileName() + " has no usable audio";
        return result;
    }

    const auto maxFrames = static_cast<juce::int64> (reader->sampleRate * kMaxImportSeconds);
    const int frames = static_cast<int> (std::min (reader->lengthInSamples, maxFrames));
    if (frames < 2)
    {
        result.error = file.getFileName() + " is empty";
        return result;
    }
    if (reader->lengthInSamples > maxFrames)
        result.warning = file.getFileName() + " is longer than " + juce::String (static_cast<int> (kMaxImportSeconds / 60.0))
                       + " minutes: only the first " + juce::String (static_cast<int> (kMaxImportSeconds / 60.0)) + " minutes were read.";

    juce::AudioBuffer<float> buffer (2, frames);
    buffer.clear();
    if (! reader->read (&buffer, 0, frames, 0, true, true))
    {
        result.error = "Could not decode " + file.getFileName();
        return result;
    }
    if (reader->numChannels == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, frames);

    auto data = std::make_shared<SampleData>();
    data->left.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + frames);
    data->right.assign (buffer.getReadPointer (1), buffer.getReadPointer (1) + frames);
    data->sampleRate = reader->sampleRate;
    data->name = file.getFileNameWithoutExtension().toStdString();
    data->source = file.getFullPathName().toStdString();

    for (float s : data->left)
        if (! std::isfinite (s))
        {
            result.error = file.getFileName() + " contains damaged audio";
            return {};
        }

    result.data = std::move (data);
    result.file = file;
    return result;
}

// ffmpeg writes the sound of `input` to `wav` (16 bit, stereo, at most kMaxImportSeconds + a little).
juce::Result runFfmpegToWav (const juce::File& ffmpeg, const juce::File& input, const juce::File& wav)
{
    const juce::StringArray args { ffmpeg.getFullPathName(), "-hide_banner", "-loglevel", "error", "-nostdin", "-y",
                                   "-i", input.getFullPathName(), "-vn", "-map", "0:a:0", "-t",
                                   juce::String (kMaxImportSeconds + 1.0), "-ac", "2", "-c:a", "pcm_s16le", "-f", "wav",
                                   wav.getFullPathName() };

    juce::ChildProcess process;
    if (! process.start (args))
        return juce::Result::fail ("Could not start ffmpeg (" + ffmpeg.getFullPathName() + ")");

    const auto output = process.readAllProcessOutput();
    if (! process.waitForProcessToFinish (180 * 1000))
    {
        process.kill();
        return juce::Result::fail ("ffmpeg took too long reading " + input.getFileName() + " and was stopped");
    }
    if (process.getExitCode() != 0)
    {
        const auto text = output.trim();
        if (text.containsIgnoreCase ("matches no streams") || text.containsIgnoreCase ("does not contain any stream"))
            return juce::Result::fail (input.getFileName() + " has no audio track");
        return juce::Result::fail ("ffmpeg could not read " + input.getFileName() + ": " + text.substring (0, 300));
    }
    return juce::Result::ok();
}

juce::File uniqueFile (const juce::File& folder, const juce::String& baseName, const juce::String& extension)
{
    auto file = folder.getChildFile (baseName + extension);
    for (int n = 2; file.exists(); ++n)
        file = folder.getChildFile (baseName + " (" + juce::String (n) + ")" + extension);
    return file;
}
} // namespace

const juce::StringArray& importableAudioExtensions()
{
    static const juce::StringArray list { ".wav", ".aiff", ".aif", ".flac", ".ogg", ".oga", ".mp3", ".aac", ".m4a" };
    return list;
}

const juce::StringArray& importableVideoExtensions()
{
    static const juce::StringArray list { ".mp4", ".m4v", ".mkv", ".mov", ".avi", ".webm", ".wmv", ".flv", ".mpg", ".mpeg", ".ogv", ".3gp" };
    return list;
}

FileKind classifyMediaFile (const juce::File& file)
{
    if (hasExtensionIn (file, importableAudioExtensions()))
        return FileKind::Audio;
    if (hasExtensionIn (file, importableVideoExtensions()))
        return FileKind::Video;
    return FileKind::Unsupported;
}

ImportResult importAudioFile (const juce::File& file, const juce::File& ffmpegOverride)
{
    if (! file.existsAsFile())
    {
        ImportResult r;
        r.error = file.getFileName() + " does not exist";
        return r;
    }

    const bool nativeFormat = file.hasFileExtension ("wav;aiff;aif;flac;ogg;oga;mp3");
    if (nativeFormat)
    {
        auto native = decodeNative (file);
        if (native.data != nullptr)
            return native;
    }

    // AAC and anything the built-in readers could not handle: let ffmpeg turn it into a WAV first.
    const auto ffmpeg = ffmpegOverride != juce::File() ? ffmpegOverride : findFfmpeg();
    if (! ffmpeg.existsAsFile())
    {
        ImportResult r;
        r.error = nativeFormat ? "Cannot read " + file.getFileName() : ffmpegMissingMessage (file);
        return r;
    }

    const auto wav = juce::File::createTempFile (".wav");
    const auto converted = runFfmpegToWav (ffmpeg, file, wav);
    ImportResult result;
    if (converted.failed())
        result.error = converted.getErrorMessage();
    else
    {
        result = decodeNative (wav);
        if (result.data != nullptr)
        {
            auto data = std::make_shared<SampleData> (*result.data);
            data->name = file.getFileNameWithoutExtension().toStdString();
            data->source = file.getFullPathName().toStdString();
            result.data = std::move (data);
            result.file = file;
        }
    }
    wav.deleteFile();
    return result;
}

ImportResult importVideoAudio (const juce::File& video, const juce::File& destinationFolder, const juce::File& ffmpegOverride)
{
    ImportResult result;
    if (! video.existsAsFile())
    {
        result.error = video.getFileName() + " does not exist";
        return result;
    }

    const auto ffmpeg = ffmpegOverride != juce::File() ? ffmpegOverride : findFfmpeg();
    if (! ffmpeg.existsAsFile())
    {
        result.error = ffmpegMissingMessage (video);
        return result;
    }
    if (! destinationFolder.createDirectory().wasOk())
    {
        result.error = "Cannot create the folder " + destinationFolder.getFullPathName();
        return result;
    }

    const auto target = uniqueFile (destinationFolder, video.getFileNameWithoutExtension(), ".wav");
    const auto partial = destinationFolder.getChildFile (target.getFileName() + ".part");
    const auto converted = runFfmpegToWav (ffmpeg, video, partial);
    if (converted.failed())
    {
        partial.deleteFile();
        result.error = converted.getErrorMessage();
        return result;
    }
    if (! partial.moveFileTo (target))
    {
        partial.deleteFile();
        result.error = "Cannot write " + target.getFullPathName();
        return result;
    }

    result = decodeNative (target);
    if (result.data == nullptr)
    {
        target.deleteFile(); // do not leave an unusable file in the library
        return result;
    }
    auto data = std::make_shared<SampleData> (*result.data);
    data->name = video.getFileNameWithoutExtension().toStdString();
    result.data = std::move (data);
    return result;
}

ImportResult importMediaFile (const juce::File& file, const juce::File& videoDestinationFolder, const juce::File& ffmpegOverride)
{
    switch (classifyMediaFile (file))
    {
        case FileKind::Audio:       return importAudioFile (file, ffmpegOverride);
        case FileKind::Video:       return importVideoAudio (file, videoDestinationFolder, ffmpegOverride);
        case FileKind::Unsupported: break;
    }
    ImportResult r;
    r.error = file.getFileName() + " is not an audio or video file this program can read";
    return r;
}

// After a project was opened its audio clips only know the path of their file: read the audio now (and stretch it again
// if the clip was stretched; clips with the same file and settings share one rendering). A file that is
// gone leaves a silent clip that keeps the path, so saving the project again does not lose it.
void loadAudioForClips (Arrangement& arrangement, juce::StringArray& problems)
{
    std::map<std::string, std::shared_ptr<const SampleData>> loaded;
    std::map<std::tuple<std::string, double, double, bool>, std::shared_ptr<const SampleData>> stretched;
    for (auto& clip : arrangement.audioClips)
    {
        if (clip.data == nullptr || clip.data->numFrames() > 0)
            continue;

        const auto source = clip.data->source;
        auto found = loaded.find (source);
        if (found == loaded.end())
        {
            std::shared_ptr<const SampleData> data;
            if (source.rfind ("builtin:", 0) == 0) // a sound built into the program (Cyber-Hub, the intro)
                data = makeBuiltinSample (source.substr (8));
            else
                data = importAudioFile (juce::File (juce::String (source))).data;
            if (data == nullptr)
                problems.add ("Audio file not found or unreadable: " + juce::String (source));
            found = loaded.emplace (source, data != nullptr ? data : clip.data).first;
        }
        const auto base = found->second;
        if (base != nullptr && base->numFrames() > 0 && ! clip.stretch.isNeutral())
        {
            const auto key = std::make_tuple (source, clip.stretch.lengthRatio, clip.stretch.semitones, clip.stretch.keepTimbre);
            auto done = stretched.find (key);
            if (done == stretched.end())
                done = stretched.emplace (key, stretchSample (base, clip.stretch)).first;
            if (done->second != nullptr)
            {
                clip.original = base;
                clip.data = done->second;
                continue;
            }
            problems.add ("Could not stretch the audio of " + juce::String (clip.name) + " (too long?): it plays unchanged.");
            clip.stretch = {};
        }
        clip.data = base;
    }
}

} // namespace sls
