#include "audio/AudioExport.h"

#include <algorithm>
#include <cmath>

#include <unistd.h>

namespace sls
{
const std::vector<ExportFormatInfo>& exportFormats()
{
    static const std::vector<ExportFormatInfo> formats {
        { ExportFormat::Wav,  ".wav",  "WAV  (.wav)",           false, false },
        { ExportFormat::Mp3,  ".mp3",  "MP3  (.mp3)",           true,  true  },
        { ExportFormat::Ogg,  ".ogg",  "Ogg Vorbis  (.ogg)",    true,  false },
        { ExportFormat::Oga,  ".oga",  "Ogg audio  (.oga)",     true,  false },
        { ExportFormat::Aac,  ".aac",  "AAC  (.aac)",           true,  true  },
        { ExportFormat::Flac, ".flac", "FLAC  (.flac)",         false, false },
        { ExportFormat::Aiff, ".aiff", "AIFF  (.aiff)",         false, false },
    };
    return formats;
}

const ExportFormatInfo& exportFormatInfo (ExportFormat format)
{
    for (const auto& f : exportFormats())
        if (f.format == format)
            return f;
    return exportFormats().front();
}

bool exportFormatForFile (const juce::File& file, ExportFormat& result)
{
    const auto name = file.getFileName();
    for (const auto& f : exportFormats())
        if (name.endsWithIgnoreCase (f.extension))
        {
            result = f.format;
            return true;
        }
    if (name.endsWithIgnoreCase (".aif"))
    {
        result = ExportFormat::Aiff;
        return true;
    }
    return false;
}

juce::File findFfmpeg()
{
    const auto fromEnvironment = juce::SystemStats::getEnvironmentVariable ("SLS_FFMPEG", {});
    if (fromEnvironment.isNotEmpty() && juce::File (fromEnvironment).existsAsFile())
        return juce::File (fromEnvironment);

    juce::StringArray folders;
    folders.addTokens (juce::SystemStats::getEnvironmentVariable ("PATH", {}), ":", "");
    for (const char* usual : { "/usr/bin", "/usr/local/bin", "/bin", "/opt/homebrew/bin" })
        folders.addIfNotAlreadyThere (usual);

    for (const auto& folder : folders)
    {
        if (folder.isEmpty())
            continue;
        const auto candidate = juce::File (folder).getChildFile ("ffmpeg");
        if (candidate.existsAsFile() && ::access (candidate.getFullPathName().toRawUTF8(), X_OK) == 0)
            return candidate;
    }
    return {};
}

namespace
{
// Writes the rendered buffer with one of JUCE's own writers.
juce::Result writeNative (const juce::AudioBuffer<float>& buffer, double sampleRate, const juce::File& target, ExportFormat format,
                          int bitDepth = 24, const ExportSettings& settings = {})
{
    std::unique_ptr<juce::FileOutputStream> stream (target.createOutputStream());
    if (stream == nullptr)
        return juce::Result::fail ("Cannot open " + target.getFullPathName() + " for writing");

    std::unique_ptr<juce::AudioFormatWriter> writer;
    switch (format)
    {
        case ExportFormat::Wav:
        {
            juce::WavAudioFormat f;
            juce::StringPairArray tags;
            if (! settings.title.empty())
                tags.set (juce::WavAudioFormat::riffInfoTitle, juce::String (settings.title));
            if (! settings.artist.empty())
                tags.set (juce::WavAudioFormat::riffInfoArtist, juce::String (settings.artist));
            writer.reset (f.createWriterFor (stream.get(), sampleRate, 2, bitDepth == 16 || bitDepth == 32 ? bitDepth : 24, tags, 0));
            break;
        }
        case ExportFormat::Aiff:
        {
            juce::AiffAudioFormat f;
            writer.reset (f.createWriterFor (stream.get(), sampleRate, 2, bitDepth == 16 ? 16 : 24, {}, 0));
            break;
        }
        case ExportFormat::Flac:
        {
            juce::FlacAudioFormat f;
            writer.reset (f.createWriterFor (stream.get(), sampleRate, 2, bitDepth == 16 ? 16 : 24, {}, 5));
            break;
        }
        case ExportFormat::Ogg:
        case ExportFormat::Oga:
        {
            juce::OggVorbisAudioFormat f;
            int quality = 0; // the highest option not above the chosen bit rate
            const auto options = f.getQualityOptions();
            for (int i = 0; i < options.size(); ++i)
                if (options[i].getIntValue() <= settings.kbps)
                    quality = i;
            juce::StringPairArray tags;
            if (! settings.title.empty())
                tags.set (juce::OggVorbisAudioFormat::id3title, juce::String (settings.title));
            if (! settings.artist.empty())
                tags.set (juce::OggVorbisAudioFormat::id3artist, juce::String (settings.artist));
            writer.reset (f.createWriterFor (stream.get(), sampleRate, 2, 16, tags, quality));
            break;
        }
        case ExportFormat::Mp3:
        case ExportFormat::Aac:
            return juce::Result::fail ("Not a format this program writes itself");
    }

    if (writer == nullptr)
        return juce::Result::fail ("Cannot create the " + juce::String (exportFormatInfo (format).extension) + " writer");

    [[maybe_unused]] auto* owned = stream.release(); // the writer owns the stream now
    if (! writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
        return juce::Result::fail ("Failed while writing audio data");
    writer.reset(); // flush and close before the file is moved
    return juce::Result::ok();
}

juce::Result encodeWithFfmpeg (const juce::File& ffmpeg, const juce::File& wav, const juce::File& target, ExportFormat format,
                               const ExportSettings& settings = {})
{
    juce::StringArray args { ffmpeg.getFullPathName(), "-hide_banner", "-loglevel", "error", "-nostdin", "-y",
                             "-i", wav.getFullPathName(), "-vn" };
    const auto rate = juce::String (std::clamp (settings.kbps, 64, 320)) + "k";
    if (format == ExportFormat::Mp3)
        args.addArray ({ "-c:a", "libmp3lame", "-b:a", rate, "-f", "mp3" });
    else
        args.addArray ({ "-c:a", "aac", "-b:a", rate, "-f", "adts" }); // a plain .aac stream (ADTS)
    if (! settings.title.empty())
        args.addArray ({ "-metadata", "title=" + juce::String (settings.title) });
    if (! settings.artist.empty())
        args.addArray ({ "-metadata", "artist=" + juce::String (settings.artist) });
    args.add (target.getFullPathName());

    juce::ChildProcess process;
    if (! process.start (args))
        return juce::Result::fail ("Could not start ffmpeg (" + ffmpeg.getFullPathName() + ")");

    const auto output = process.readAllProcessOutput(); // ffmpeg only prints errors with -loglevel error
    if (! process.waitForProcessToFinish (120 * 1000))
    {
        process.kill();
        return juce::Result::fail ("ffmpeg took too long and was stopped");
    }
    if (process.getExitCode() != 0)
        return juce::Result::fail ("ffmpeg failed: " + output.trim().substring (0, 300));
    return juce::Result::ok();
}
} // namespace

void ditherTo16Bits (juce::AudioBuffer<float>& buffer, std::uint32_t seed)
{
    // TPDF: the sum of two uniform values of +-half a 16-bit step, then rounded to that step.
    std::uint32_t state = seed == 0 ? 1 : seed;
    const auto next = [&state]
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float> (state) / 4294967296.0f - 0.5f;
    };
    constexpr float step = 1.0f / 32768.0f;
    for (int c = 0; c < buffer.getNumChannels(); ++c)
    {
        auto* d = buffer.getWritePointer (c);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            d[i] = std::round ((d[i] + (next() + next()) * step) / step) * step;
    }
}

std::vector<int> tracksInUse (const Project& project)
{
    std::vector<int> used;
    const auto add = [&used, &project] (int t)
    {
        if (t >= 0 && t < static_cast<int> (project.tracks.size()) && std::find (used.begin(), used.end(), t) == used.end())
            used.push_back (t);
    };
    for (const auto& c : project.channels)
        add (c.mixerTrack);
    for (const auto& e : project.audioEvents)
        add (e.mixerTrack);
    std::sort (used.begin(), used.end());
    return used;
}

namespace
{
// Writes an already rendered buffer as `format` (through a temporary file, as exportAudio).
juce::Result exportBuffer (juce::AudioBuffer<float> buffer, const RenderOptions& options, const juce::File& file, ExportFormat format,
                           const juce::File& ffmpegOverride, const ExportSettings& settings)
{
    const auto& info = exportFormatInfo (format);
    juce::File ffmpeg;
    if (info.needsFfmpeg)
    {
        ffmpeg = ffmpegOverride != juce::File() ? ffmpegOverride : findFfmpeg();
        if (! ffmpeg.existsAsFile())
            return juce::Result::fail (juce::String (info.extension).substring (1).toUpperCase()
                                       + " export needs the ffmpeg program, which was not found. "
                                         "Install it (on Fedora / Nobara: sudo dnf install ffmpeg) or choose another format.");
    }
    if (! file.getParentDirectory().createDirectory())
        return juce::Result::fail ("Cannot create the folder " + file.getParentDirectory().getFullPathName());

    if (! info.lossy && options.bitDepth == 16)
        ditherTo16Bits (buffer);
    juce::TemporaryFile output (file); // keeps the extension, so ffmpeg and the writers know what to make

    juce::Result result = juce::Result::ok();
    if (info.needsFfmpeg)
    {
        const auto wavFile = output.getFile().withFileExtension ("wav");
        result = writeNative (buffer, options.sampleRate, wavFile, ExportFormat::Wav, 32);
        if (result.wasOk())
            result = encodeWithFfmpeg (ffmpeg, wavFile, output.getFile(), format, settings);
        wavFile.deleteFile();
    }
    else
        result = writeNative (buffer, options.sampleRate, output.getFile(), format, options.bitDepth, settings);

    if (result.failed())
        return result;
    if (! output.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("Could not write " + file.getFullPathName());
    return juce::Result::ok();
}
} // namespace

juce::Result exportAudio (const Project& project, const RenderOptions& options, const juce::File& file,
                          ExportFormat format, const juce::File& ffmpegOverride, const ExportSettings& settings)
{
    const auto& info = exportFormatInfo (format);
    if (info.needsFfmpeg && ! (ffmpegOverride != juce::File() ? ffmpegOverride : findFfmpeg()).existsAsFile())
        return exportBuffer ({}, options, file, format, ffmpegOverride, settings); // the message about ffmpeg, without rendering
    return exportBuffer (renderToBuffer (project, options), options, file, format, ffmpegOverride, settings);
}

juce::Result exportStems (const Project& project, const RenderOptions& options, const juce::File& baseFile, ExportFormat format,
                          std::vector<juce::File>& written, const juce::File& ffmpegOverride, const ExportSettings& settings)
{
    written.clear();
    const auto extension = juce::String (exportFormatInfo (format).extension);
    const auto base = baseFile.getFileName().endsWithIgnoreCase (extension) ? baseFile.getFileNameWithoutExtension() : baseFile.getFileName();
    for (const int t : tracksInUse (project))
    {
        auto solo = project;
        for (size_t i = 0; i < solo.tracks.size(); ++i)
            solo.tracks[i].solo = static_cast<int> (i) == t;
        solo.tracks[static_cast<size_t> (t)].mute = false;
        // Silent stems (a muted channel, an empty track) are not written.
        auto rendered = renderToBuffer (solo, options);
        if (rendered.getMagnitude (0, rendered.getNumSamples()) < 1.0e-6f)
            continue;
        const auto name = juce::File::createLegalFileName (base + " - " + juce::String (project.tracks[static_cast<size_t> (t)].name));
        const auto file = baseFile.getParentDirectory().getChildFile (name + extension);
        auto stemSettings = settings;
        if (! stemSettings.title.empty())
            stemSettings.title += " (" + project.tracks[static_cast<size_t> (t)].name + ")";
        const auto result = exportBuffer (std::move (rendered), options, file, format, ffmpegOverride, stemSettings);
        if (result.failed())
            return result;
        written.push_back (file);
    }
    if (written.empty())
        return juce::Result::fail ("No mixer track plays anything: no stems were written.");
    return juce::Result::ok();
}
} // namespace sls
