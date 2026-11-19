#include "audio/AudioImport.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <tuple>

#include "audio/AudioExport.h"
#include "audio/BuiltinSamples.h"

// How to install ffmpeg on this system (0.75.0).
#if defined(_WIN32)
 #define SLS_FFMPEG_HOW "on Windows: winget install Gyan.FFmpeg, then start Sun Loops Studio again"
#elif defined(__APPLE__)
 #define SLS_FFMPEG_HOW "on a Mac: brew install ffmpeg"
#else
 #define SLS_FFMPEG_HOW "on Fedora / Nobara: sudo dnf install ffmpeg"
#endif

namespace sls
{
namespace
{
juce::String ffmpegMissingMessage (const juce::File& file)
{
    return "Reading " + file.getFileName()
         + " needs the ffmpeg program, which was not found. Install it (" SLS_FFMPEG_HOW ").";
}

bool hasExtensionIn (const juce::File& file, const juce::StringArray& extensions)
{
    return extensions.contains (file.getFileExtension().toLowerCase());
}

// Plain decoding with JUCE's own readers.
// The built-in reader for `file` (null when it cannot read it).
std::unique_ptr<juce::AudioFormatReader> openNativeReader (const juce::File& file)
{
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
    return reader;
}

// The key a long file's cache is found by (its path, size, date and length) and the cache file of a key.
juce::String audioCacheKey (const juce::File& file, int frames)
{
    return file.getFullPathName() + "|" + juce::String (file.getSize()) + "|" + juce::String (file.getLastModificationTime().toMilliseconds())
         + "|" + juce::String (frames);
}

juce::File audioCacheFileFor (const juce::String& key, bool mono)
{
    return audioCacheFolder().getChildFile (juce::String::toHexString (key.hashCode64()) + (mono ? ".m" : ".s") + ".f32");
}

// How many frames of `reader` are read (at most kMaxImportSeconds).
int framesToRead (const juce::AudioFormatReader& reader)
{
    const auto maxFrames = std::min (static_cast<juce::int64> (reader.sampleRate * kMaxImportSeconds), static_cast<juce::int64> (std::numeric_limits<int>::max() - 1));
    return static_cast<int> (std::min (reader.lengthInSamples, maxFrames));
}

ImportResult decodeNative (const juce::File& file)
{
    ImportResult result;

    auto reader = openNativeReader (file);

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

    const auto maxFrames = std::min (static_cast<juce::int64> (reader->sampleRate * kMaxImportSeconds), static_cast<juce::int64> (std::numeric_limits<int>::max() - 1));
    const int frames = static_cast<int> (std::min (reader->lengthInSamples, maxFrames));
    if (frames < 2)
    {
        result.error = file.getFileName() + " is empty";
        return result;
    }
    if (reader->lengthInSamples > maxFrames)
        result.warning = file.getFileName() + " is longer than " + juce::String (static_cast<int> (kMaxImportSeconds / 3600.0))
                       + " hours: only the first " + juce::String (static_cast<int> (kMaxImportSeconds / 3600.0)) + " hours were read.";

    const auto name = file.getFileNameWithoutExtension().toStdString();
    const auto source = file.getFullPathName().toStdString();
    std::shared_ptr<SampleData> data;
    if (frames > reader->sampleRate * kMaxInMemorySeconds) // 0.81.0: a long file is read from the audio cache
    {
        const juce::String key = audioCacheKey (file, frames);
        juce::String error;
        data = readSampleDataToCache (*reader, frames, name, source, key, error);
        if (data == nullptr)
        {
            result.error = error.isNotEmpty() ? error : "Could not decode " + file.getFileName();
            return result;
        }
    }
    else
    {
        data = readSampleData (*reader, frames, name, source);
        if (data == nullptr)
        {
            result.error = "Could not decode " + file.getFileName();
            return result;
        }
        for (float s : data->left)
            if (! std::isfinite (s))
            {
                result.error = file.getFileName() + " contains damaged audio";
                return {};
            }
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

std::shared_ptr<SampleData> readSampleData (juce::AudioFormatReader& reader, int frames, std::string name, std::string source)
{
    juce::AudioBuffer<float> buffer (2, frames);
    buffer.clear();
    if (! reader.read (&buffer, 0, frames, 0, true, true))
        return nullptr;
    if (reader.numChannels == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, frames);

    auto data = std::make_shared<SampleData>();
    data->left.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + frames);
    data->right.assign (buffer.getReadPointer (1), buffer.getReadPointer (1) + frames);
    data->sampleRate = reader.sampleRate;
    data->name = std::move (name);
    data->source = std::move (source);
    return data;
}

// ---- 0.81.0: the audio cache ------------------------------------------------------------------------------------------

namespace
{
juce::CriticalSection cacheLock;
juce::File cacheFolderSetting;

// A cache file mapped into memory, kept alive by the buffers that read it.
std::shared_ptr<const void> mapFloatFile (const juce::File& file, juce::int64 expectedBytes, const float*& samples)
{
    samples = nullptr;
    auto map = std::make_shared<juce::MemoryMappedFile> (file, juce::MemoryMappedFile::readOnly);
    if (map->getData() == nullptr || static_cast<juce::int64> (map->getSize()) != expectedBytes)
        return nullptr;
    samples = static_cast<const float*> (map->getData());
    return map;
}

// The overview of `frames` frames of a left and a right channel (the same for mono), into `o` from bin `firstBin` on.
void addOverview (SampleOverview& o, const float* l, const float* r, int frames, size_t firstBin)
{
    for (int start = 0; start < frames; start += kOverviewFramesPerPeak)
    {
        float lo = 0.0f, hi = 0.0f;
        const int end = std::min (frames, start + kOverviewFramesPerPeak);
        for (int i = start; i < end; ++i)
        {
            const float v = 0.5f * (l[i] + r[i]);
            lo = std::min (lo, v);
            hi = std::max (hi, v);
        }
        const auto b = firstBin + static_cast<size_t> (start / kOverviewFramesPerPeak);
        o.lows[b] = lo;
        o.highs[b] = hi;
    }
}

bool readOverview (const juce::File& file, size_t bins, SampleOverview& o)
{
    if (file.getSize() != static_cast<juce::int64> (bins * 2 * sizeof (float)))
        return false;
    juce::FileInputStream in (file);
    o.lows.assign (bins, 0.0f);
    o.highs.assign (bins, 0.0f);
    const auto bytes = static_cast<int> (bins * sizeof (float));
    return in.openedOk() && in.read (o.lows.data(), bytes) == bytes && in.read (o.highs.data(), bytes) == bytes;
}

void writeOverview (const juce::File& file, const SampleOverview& o)
{
    const auto part = file.withFileExtension (".peaks-part");
    {
        juce::FileOutputStream out (part);
        if (! out.openedOk() || ! out.setPosition (0) || ! out.truncate().wasOk())
            return;
        out.write (o.lows.data(), o.lows.size() * sizeof (float));
        out.write (o.highs.data(), o.highs.size() * sizeof (float));
        out.flush();
        if (out.getStatus().failed())
            return;
    }
    part.moveFileTo (file);
}
} // namespace

void setAudioCacheFolder (const juce::File& folder)
{
    const juce::ScopedLock lock (cacheLock);
    cacheFolderSetting = folder;
}

juce::File audioCacheFolder()
{
    const juce::ScopedLock lock (cacheLock);
    if (cacheFolderSetting != juce::File())
        return cacheFolderSetting;
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Sun Loops Studio").getChildFile ("Cache").getChildFile ("Audio");
}

void pruneAudioCache (juce::int64 maxBytes)
{
    auto files = audioCacheFolder().findChildFiles (juce::File::findFiles, false, "*.f32");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getLastModificationTime() > b.getLastModificationTime(); });
    juce::int64 total = 0;
    for (const auto& f : files)
    {
        total += f.getSize();
        if (total > maxBytes && f.deleteFile()) // (a file still mapped stays, where the system does not allow deleting it)
            f.withFileExtension (".peaks").deleteFile();
    }
}

std::shared_ptr<SampleData> readSampleDataToCache (juce::AudioFormatReader& reader, int frames, std::string name, std::string source,
                                                   const juce::String& key, juce::String& error)
{
    const bool mono = reader.numChannels == 1;
    const int channels = mono ? 1 : 2;
    const auto bytes = static_cast<juce::int64> (frames) * channels * static_cast<juce::int64> (sizeof (float));
    const auto folder = audioCacheFolder();
    const auto file = audioCacheFileFor (key, mono);

    const float* samples = nullptr;
    auto map = file.existsAsFile() ? mapFloatFile (file, bytes, samples) : nullptr;
    const auto bins = static_cast<size_t> ((frames + kOverviewFramesPerPeak - 1) / kOverviewFramesPerPeak);
    auto overview = std::make_shared<SampleOverview>();
    const auto overviewFile = file.withFileExtension (".peaks");
    if (map == nullptr)
    {
        overview->lows.assign (bins, 0.0f);
        overview->highs.assign (bins, 0.0f);
        if (! folder.createDirectory())
        {
            error = "Cannot create the audio cache folder " + folder.getFullPathName();
            return nullptr;
        }
        if (folder.getBytesFreeOnVolume() < bytes + (64 << 20))
        {
            error = "Not enough room on the disk for the audio cache of " + juce::String (name) + " ("
                  + juce::String (static_cast<double> (bytes) / (1 << 30), 1) + " GB are needed in " + folder.getFullPathName() + ")";
            return nullptr;
        }
        // Left then right, each in one piece: written in blocks, so the whole file is never in memory.
        const auto part = file.withFileExtension (".part");
        {
            juce::FileOutputStream out (part);
            if (! out.openedOk() || ! out.setPosition (0) || ! out.truncate().wasOk())
            {
                error = "Cannot write the audio cache file " + part.getFullPathName();
                return nullptr;
            }
            constexpr int kBlock = 1 << 16;
            juce::AudioBuffer<float> buffer (2, kBlock);
            for (int done = 0; done < frames; done += kBlock)
            {
                const int n = std::min (kBlock, frames - done);
                buffer.clear();
                if (! reader.read (&buffer, 0, n, done, true, ! mono))
                {
                    error = "Could not decode " + juce::String (name);
                    break;
                }
                for (int c = 0; c < channels; ++c)
                {
                    const float* x = buffer.getReadPointer (c);
                    for (int i = 0; i < n; ++i)
                        if (! std::isfinite (x[i]))
                        {
                            error = juce::String (name) + " contains damaged audio";
                            break;
                        }
                    if (error.isNotEmpty())
                        break;
                    if (! out.setPosition ((static_cast<juce::int64> (c) * frames + done) * static_cast<juce::int64> (sizeof (float)))
                        || ! out.write (x, static_cast<size_t> (n) * sizeof (float)))
                    {
                        error = "Could not write the audio cache file (is the disk full?)";
                        break;
                    }
                }
                if (error.isNotEmpty())
                    break;
                addOverview (*overview, buffer.getReadPointer (0), buffer.getReadPointer (mono ? 0 : 1), n,
                             static_cast<size_t> (done / kOverviewFramesPerPeak));
            }
            out.flush();
            if (error.isEmpty() && out.getStatus().failed())
                error = "Could not write the audio cache file: " + out.getStatus().getErrorMessage();
        }
        if (error.isNotEmpty() || ! part.moveFileTo (file))
        {
            part.deleteFile();
            if (error.isEmpty())
                error = "Could not write the audio cache file " + file.getFullPathName();
            return nullptr;
        }
        map = mapFloatFile (file, bytes, samples);
        if (map == nullptr)
        {
            error = "Could not read the audio cache file " + file.getFullPathName();
            return nullptr;
        }
        writeOverview (overviewFile, *overview);
    }
    else if (! readOverview (overviewFile, bins, *overview)) // (a cache file whose overview is missing: made again, once)
    {
        overview->lows.assign (bins, 0.0f);
        overview->highs.assign (bins, 0.0f);
        const auto n = static_cast<size_t> (frames);
        constexpr int kBlock = 1 << 16;
        for (int done = 0; done < frames; done += kBlock)
            addOverview (*overview, samples + done, (mono ? samples : samples + n) + done, std::min (kBlock, frames - done),
                         static_cast<size_t> (done / kOverviewFramesPerPeak));
        writeOverview (overviewFile, *overview);
    }
    file.setLastModificationTime (juce::Time::getCurrentTime()); // (recently used: pruneAudioCache keeps it)

    auto data = std::make_shared<SampleData>();
    const auto n = static_cast<size_t> (frames);
    data->left = SampleBuffer::mapped (map, samples, n);
    data->right = SampleBuffer::mapped (map, mono ? samples : samples + n, n);
    data->overview = std::move (overview);
    data->sampleRate = reader.sampleRate;
    data->name = std::move (name);
    data->source = std::move (source);
    return data;
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
            const bool builtin = source.rfind ("builtin:", 0) == 0; // a sound built into the program (the library, the intro)
            if (builtin)
                data = makeBuiltinSample (source.substr (8));
            else
                data = importAudioFile (juce::File (juce::String (source))).data;
            if (data == nullptr)
            {
                const auto removed = builtin ? removedBuiltinMessage (source.substr (8)) : std::string();
                problems.add (! removed.empty() ? juce::String (removed) : "Audio file not found or unreadable: " + juce::String (source));
            }
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

std::vector<juce::File> clipFilesNeedingAudioCache (const Arrangement& arrangement)
{
    std::vector<juce::File> files;
    std::set<std::string> seen;
    for (const auto& clip : arrangement.audioClips)
    {
        if (clip.data == nullptr || clip.data->numFrames() > 0 || clip.data->source.rfind ("builtin:", 0) == 0 || ! seen.insert (clip.data->source).second)
            continue;
        const juce::File file (juce::String (clip.data->source));
        if (! file.existsAsFile() || ! file.hasFileExtension ("wav;aiff;aif;flac;ogg;oga;mp3"))
            continue;
        const auto reader = openNativeReader (file);
        if (reader == nullptr || reader->numChannels < 1 || reader->sampleRate < 1000.0)
            continue;
        const int frames = framesToRead (*reader);
        if (frames > reader->sampleRate * kMaxInMemorySeconds && ! audioCacheFileFor (audioCacheKey (file, frames), reader->numChannels == 1).existsAsFile())
            files.push_back (file);
    }
    return files;
}

bool decodeIntoAudioCache (const juce::File& file, juce::String& error)
{
    const auto result = importAudioFile (file);
    error = result.error;
    return result.data != nullptr;
}
} // namespace sls
