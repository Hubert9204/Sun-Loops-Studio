#include "audio/AudioExport.h"

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
juce::Result writeNative (const juce::AudioBuffer<float>& buffer, double sampleRate, const juce::File& target, ExportFormat format)
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
            writer.reset (f.createWriterFor (stream.get(), sampleRate, 2, 24, {}, 0));
            break;
        }
        case ExportFormat::Aiff:
        {
            juce::AiffAudioFormat f;
            writer.reset (f.createWriterFor (stream.get(), sampleRate, 2, 24, {}, 0));
            break;
        }
        case ExportFormat::Flac:
        {
            juce::FlacAudioFormat f;
            writer.reset (f.createWriterFor (stream.get(), sampleRate, 2, 24, {}, 5));
            break;
        }
        case ExportFormat::Ogg:
        case ExportFormat::Oga:
        {
            juce::OggVorbisAudioFormat f;
            int quality = 0; // the option closest to 192 kbps
            const auto options = f.getQualityOptions();
            for (int i = 0; i < options.size(); ++i)
                if (options[i].getIntValue() <= 192)
                    quality = i;
            writer.reset (f.createWriterFor (stream.get(), sampleRate, 2, 16, {}, quality));
            break;
        }
        case ExportFormat::Mp3:
        case ExportFormat::Aac:
            return juce::Result::fail ("Not a format this program writes itself");
    }

    if (writer == nullptr)
        return juce::Result::fail ("Cannot create the " + juce::String (exportFormatInfo (format).extension) + " writer");

    stream.release(); // the writer owns the stream now
    if (! writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
        return juce::Result::fail ("Failed while writing audio data");
    writer.reset(); // flush and close before the file is moved
    return juce::Result::ok();
}

juce::Result encodeWithFfmpeg (const juce::File& ffmpeg, const juce::File& wav, const juce::File& target, ExportFormat format)
{
    juce::StringArray args { ffmpeg.getFullPathName(), "-hide_banner", "-loglevel", "error", "-nostdin", "-y",
                             "-i", wav.getFullPathName(), "-vn" };
    if (format == ExportFormat::Mp3)
        args.addArray ({ "-c:a", "libmp3lame", "-b:a", "192k", "-f", "mp3" });
    else
        args.addArray ({ "-c:a", "aac", "-b:a", "192k", "-f", "adts" }); // a plain .aac stream (ADTS)
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

juce::Result exportAudio (const Project& project, const RenderOptions& options, const juce::File& file,
                          ExportFormat format, const juce::File& ffmpegOverride)
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

    const auto buffer = renderToBuffer (project, options);
    juce::TemporaryFile output (file); // keeps the extension, so ffmpeg and the writers know what to make

    juce::Result result = juce::Result::ok();
    if (info.needsFfmpeg)
    {
        const auto wavFile = output.getFile().withFileExtension ("wav");
        result = writeNative (buffer, options.sampleRate, wavFile, ExportFormat::Wav);
        if (result.wasOk())
            result = encodeWithFfmpeg (ffmpeg, wavFile, output.getFile(), format);
        wavFile.deleteFile();
    }
    else
    {
        result = writeNative (buffer, options.sampleRate, output.getFile(), format);
    }

    if (result.failed())
        return result;
    if (! output.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("Could not write " + file.getFullPathName());
    return juce::Result::ok();
}
} // namespace sls
