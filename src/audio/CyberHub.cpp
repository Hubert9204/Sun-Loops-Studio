#include "audio/CyberHub.h"

#include <algorithm>
#include <map>
#include <mutex>

#include <juce_audio_formats/juce_audio_formats.h>

#include "SunLoopsSoundData.h"

namespace sls
{
namespace
{
struct Entry
{
    const char* file;
    CyberRole role;
};

// Roles from measuring the sounds (see docs/cyber-hub.md): length, spectral centroid, noisiness, pitch.
constexpr Entry kEntries[] = {
    { "alarm-clock-elapsed.oga", CyberRole::Long },   { "audio-volume-change.oga", CyberRole::Hit },
    { "battery-caution.oga", CyberRole::Hit },        { "battery-low.oga", CyberRole::Tone },
    { "bell-window-system.oga", CyberRole::Hit },     { "button-pressed-modifier.oga", CyberRole::Hit },
    { "button-pressed.oga", CyberRole::Hit },         { "completion-fail.oga", CyberRole::Tone },
    { "completion-partial.oga", CyberRole::Tone },    { "completion-rotation.oga", CyberRole::Tone },
    { "completion-success.oga", CyberRole::Tone },    { "desktop-login.oga", CyberRole::Long },
    { "desktop-logout.oga", CyberRole::Long },        { "device-added.oga", CyberRole::Hit },
    { "device-removed.oga", CyberRole::Hit },         { "dialog-error-serious.oga", CyberRole::Tone },
    { "dialog-error.oga", CyberRole::Tone },          { "dialog-information.oga", CyberRole::Hit },
    { "dialog-question.oga", CyberRole::Hit },        { "dialog-warning-auth.oga", CyberRole::Hit },
    { "dialog-warning.oga", CyberRole::Hit },         { "message-new-instant.oga", CyberRole::Long },
    { "outcome-failure.oga", CyberRole::Tone },       { "outcome-success.oga", CyberRole::Tone },
    { "phone-incoming-call.oga", CyberRole::Long },   { "power-plug.oga", CyberRole::Hit },
    { "power-unplug.oga", CyberRole::Hit },           { "service-login.oga", CyberRole::Long },
    { "service-logout.oga", CyberRole::Long },        { "trash-empty.oga", CyberRole::Hit },
};

std::string titleFor (const std::string& baseName)
{
    std::string t = baseName;
    std::replace (t.begin(), t.end(), '-', ' ');
    if (! t.empty())
        t[0] = static_cast<char> (std::toupper (static_cast<unsigned char> (t[0])));
    return t;
}

// Finds a built-in file by its original name (BinaryData renames "dialog-error.oga" to "dialog_error_oga").
const char* findResource (const char* fileName, int& size)
{
    for (int i = 0; i < SunLoopsSoundData::namedResourceListSize; ++i)
        if (juce::String (SunLoopsSoundData::originalFilenames[i]) == fileName)
            return SunLoopsSoundData::getNamedResource (SunLoopsSoundData::namedResourceList[i], size);
    size = 0;
    return nullptr;
}

std::shared_ptr<const SampleData> decode (const char* fileName, const std::string& name, const std::string& source, bool vorbis)
{
    int size = 0;
    const char* data = findResource (fileName, size);
    if (data == nullptr || size <= 0)
        return nullptr;

    std::unique_ptr<juce::AudioFormatReader> reader;
    auto* stream = new juce::MemoryInputStream (data, static_cast<size_t> (size), false);
    if (vorbis)
        reader.reset (juce::OggVorbisAudioFormat().createReaderFor (stream, true));
    else
        reader.reset (juce::WavAudioFormat().createReaderFor (stream, true));
    if (reader == nullptr || reader->numChannels < 1 || reader->lengthInSamples < 2)
        return nullptr;

    const int frames = static_cast<int> (reader->lengthInSamples);
    juce::AudioBuffer<float> buffer (2, frames);
    buffer.clear();
    if (! reader->read (&buffer, 0, frames, 0, true, true))
        return nullptr;
    if (reader->numChannels == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, frames);

    auto sample = std::make_shared<SampleData>();
    sample->left.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + frames);
    sample->right.assign (buffer.getReadPointer (1), buffer.getReadPointer (1) + frames);
    sample->sampleRate = reader->sampleRate;
    sample->name = name;
    sample->source = source;
    return sample;
}
} // namespace

const std::vector<CyberSound>& cyberHubSounds()
{
    static const std::vector<CyberSound> sounds = []
    {
        std::vector<CyberSound> list;
        for (const auto& e : kEntries)
        {
            int size = 0;
            if (findResource (e.file, size) == nullptr)
                continue;
            const std::string file = e.file;
            const auto base = file.substr (0, file.rfind ('.'));
            list.push_back ({ "cyber:" + base, titleFor (base), file, e.role });
        }
        return list;
    }();
    return sounds;
}

bool isCyberHubId (const std::string& id)
{
    return id.rfind ("cyber:", 0) == 0;
}

std::shared_ptr<const SampleData> makeCyberHubSample (const std::string& id)
{
    static std::mutex lock;
    static std::map<std::string, std::shared_ptr<const SampleData>> cache;

    const std::lock_guard<std::mutex> guard (lock);
    if (const auto found = cache.find (id); found != cache.end())
        return found->second;

    for (const auto& s : cyberHubSounds())
        if (s.id == id)
        {
            auto sample = decode (s.fileName.c_str(), s.title, "builtin:" + s.id, true);
            if (sample != nullptr)
                cache[id] = sample;
            return sample;
        }
    return nullptr;
}

const std::vector<CyberKitPiece>& cyberHubKit()
{
    // power-plug / power-unplug: mono pure sub (50 Hz) hits; button-pressed: low mono thump (66 Hz, 0.45 s);
    // audio-volume-change: 0.16 s of bright noise; trash-empty: noisy crunch; button-pressed-modifier: 130 Hz thud.
    static const std::vector<CyberKitPiece> kit {
        { "Kick", "cyber:button-pressed" },
        { "Sub", "cyber:power-plug" },
        { "Hat", "cyber:audio-volume-change" },
        { "Clap", "cyber:trash-empty" },
        { "Tom", "cyber:button-pressed-modifier" },
    };
    return kit;
}

std::shared_ptr<const SampleData> makeStartupSample()
{
    return decode ("intro.wav", "Sun Loops intro", std::string ("builtin:") + kStartupSampleId, false);
}
} // namespace sls
