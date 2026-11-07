#include "audio/SoundLibrary.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <thread>

#include <juce_audio_formats/juce_audio_formats.h>

// The archive, linked in by SoundLibraryData.S (see CMakeLists.txt).
extern "C" const char sls_sound_library[];
extern "C" const char sls_sound_library_end[];

namespace sls
{
namespace
{
std::mutex& zipLock()
{
    static std::mutex m;
    return m;
}

// The archive; only used under zipLock().
juce::ZipFile* archive()
{
    static std::unique_ptr<juce::ZipFile> zip = []
    {
        const auto size = static_cast<size_t> (sls_sound_library_end - sls_sound_library);
        if (size < 22) // smaller than an empty zip
            return std::unique_ptr<juce::ZipFile>();
        return std::make_unique<juce::ZipFile> (new juce::MemoryInputStream (sls_sound_library, size, false), true);
    }();
    return zip.get();
}

bool readEntry (const juce::String& path, juce::MemoryBlock& out)
{
    const std::lock_guard<std::mutex> guard (zipLock());
    auto* zip = archive();
    if (zip == nullptr)
        return false;
    const int index = zip->getIndexOfFileName (path);
    if (index < 0)
        return false;
    std::unique_ptr<juce::InputStream> in (zip->createStreamForEntry (index));
    if (in == nullptr)
        return false;
    out.reset();
    in->readIntoMemoryBlock (out);
    return out.getSize() > 0;
}

juce::StringArray entriesIn (const juce::String& folder, const juce::String& extension)
{
    const std::lock_guard<std::mutex> guard (zipLock());
    juce::StringArray names;
    if (auto* zip = archive())
        for (int i = 0; i < zip->getNumEntries(); ++i)
            if (const auto* e = zip->getEntry (i); e != nullptr && e->filename.startsWith (folder) && e->filename.endsWith (extension))
                names.add (e->filename);
    names.sort (true);
    return names;
}

juce::var readJson (const juce::String& path)
{
    juce::MemoryBlock block;
    if (! readEntry (path, block))
        return {};
    return juce::JSON::parse (block.toString());
}

std::string str (const juce::var& v, const char* key)
{
    return v.getProperty (key, {}).toString().toStdString();
}

int integer (const juce::var& v, const char* key, int fallback, int lo, int hi)
{
    const auto p = v.getProperty (key, fallback);
    return std::clamp (p.isInt() || p.isInt64() || p.isDouble() ? static_cast<int> (p) : fallback, lo, hi);
}

float number (const juce::var& v, const char* key, float fallback, float lo, float hi)
{
    const auto p = v.getProperty (key, fallback);
    return std::clamp (p.isInt() || p.isInt64() || p.isDouble() ? finiteOr (static_cast<float> (static_cast<double> (p)), fallback) : fallback, lo, hi);
}

std::string stemOf (const juce::String& path)
{
    return path.fromLastOccurrenceOf ("/", false, false).upToLastOccurrenceOf (".", false, false).toStdString();
}

const char* const kNoteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

std::string noteName (int key)
{
    return std::string (kNoteNames[key % 12]) + std::to_string (key / 12 - 1);
}

// The titles of every sound named by a program or a kit ("Grand Piano C4 3", "Studio Kick 2").
const std::map<std::string, std::string>& titles();

std::shared_ptr<const SampleData> decode (const juce::MemoryBlock& block, const std::string& name, const std::string& source)
{
    std::unique_ptr<juce::AudioFormatReader> reader (
        juce::OggVorbisAudioFormat().createReaderFor (new juce::MemoryInputStream (block.getData(), block.getSize(), false), true));
    if (reader == nullptr || reader->numChannels < 1 || reader->lengthInSamples < 2 || reader->lengthInSamples > (1 << 26))
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

SamplerSettings settingsFrom (const juce::var& v)
{
    SamplerSettings s;
    if (! v.isObject())
        return s;
    s.envelope = static_cast<bool> (v.getProperty ("envelope", false));
    s.attack = number (v, "attack", s.attack, 0.0f, 10.0f);
    s.decay = number (v, "decay", s.decay, 0.0f, 10.0f);
    s.sustain = number (v, "sustain", s.sustain, 0.0f, 1.0f);
    s.release = number (v, "release", s.release, 0.0f, 10.0f);
    return s.sanitised();
}

const juce::String kProgramFolder = "programs/";
const juce::String kKitFolder = "kits/";
} // namespace

bool soundLibraryAvailable()
{
    const std::lock_guard<std::mutex> guard (zipLock());
    return archive() != nullptr && archive()->getNumEntries() > 0;
}

bool isLibraryId (const std::string& id)
{
    return id.rfind ("lib:", 0) == 0;
}

bool isLibraryProgramSource (const std::string& source)
{
    return source.rfind (kProgramSourcePrefix, 0) == 0;
}

const std::vector<LibraryProgramInfo>& libraryPrograms()
{
    static const std::vector<LibraryProgramInfo> programs = []
    {
        std::vector<LibraryProgramInfo> list;
        for (const auto& path : entriesIn (kProgramFolder, ".json"))
        {
            const auto json = readJson (path);
            if (! json.isObject())
                continue;
            LibraryProgramInfo info;
            info.id = stemOf (path);
            info.name = str (json, "name");
            info.credit = str (json, "credit");
            info.category = str (json, "category");
            if (info.name.empty())
                info.name = info.id;
            list.push_back (std::move (info));
        }
        return list;
    }();
    return programs;
}

const std::vector<LibraryKitInfo>& libraryKits()
{
    static const std::vector<LibraryKitInfo> kits = []
    {
        std::vector<LibraryKitInfo> list;
        for (const auto& path : entriesIn (kKitFolder, ".json"))
        {
            const auto json = readJson (path);
            const auto* pads = json.getProperty ("pads", {}).getArray();
            if (! json.isObject() || pads == nullptr)
                continue;
            LibraryKitInfo kit;
            kit.id = stemOf (path);
            kit.name = str (json, "name");
            kit.credit = str (json, "credit");
            for (const auto& p : *pads)
            {
                if (kit.pads.size() >= 16)
                    break;
                LibraryPad pad;
                pad.name = str (p, "name");
                pad.chokeGroup = integer (p, "choke", 0, 0, 8);
                pad.pan = number (p, "pan", 0.0f, -1.0f, 1.0f);
                pad.volume = number (p, "volume", 1.0f, 0.0f, 1.5f);
                if (const auto* layers = p.getProperty ("layers", {}).getArray())
                    for (const auto& l : *layers)
                        if (pad.layers.size() < 4)
                            pad.layers.emplace_back ("lib:" + str (l, "sample"), integer (l, "minVelocity", 1, 1, 127));
                kit.pads.push_back (std::move (pad));
            }
            list.push_back (std::move (kit));
        }
        return list;
    }();
    return kits;
}

namespace
{
const std::map<std::string, std::string>& titles()
{
    static const std::map<std::string, std::string> map = []
    {
        std::map<std::string, std::string> m;
        for (const auto& kit : libraryKits())
            for (const auto& pad : kit.pads)
                for (size_t i = 0; i < pad.layers.size(); ++i)
                    m[pad.layers[i].first] = kit.name + " " + pad.name + (pad.layers.size() > 1 ? " " + std::to_string (i + 1) : std::string());
        for (const auto& info : libraryPrograms())
        {
            const auto json = readJson (kProgramFolder + juce::String (info.id) + ".json");
            if (const auto* zones = json.getProperty ("zones", {}).getArray())
            {
                std::map<int, int> layerOfKey; // count the layers of every root key, in order
                for (const auto& z : *zones)
                {
                    const int root = integer (z, "root", 60, 0, 127);
                    const int layer = ++layerOfKey[root];
                    m["lib:" + str (z, "sample")] = info.name + " " + noteName (root) + " " + std::to_string (layer);
                }
            }
        }
        return m;
    }();
    return map;
}
} // namespace

std::string librarySampleTitle (const std::string& id)
{
    const auto& m = titles();
    if (const auto found = m.find (id); found != m.end())
        return found->second;
    return stemOf (juce::String (id));
}

std::shared_ptr<const SampleData> makeLibrarySample (const std::string& id)
{
    if (! isLibraryId (id))
        return nullptr;

    static std::mutex lock;
    static std::map<std::string, std::weak_ptr<const SampleData>> cache;
    {
        const std::lock_guard<std::mutex> guard (lock);
        if (const auto found = cache.find (id); found != cache.end())
            if (auto alive = found->second.lock())
                return alive;
    }

    juce::MemoryBlock block;
    if (! readEntry (juce::String (id.substr (4)) + ".ogg", block))
        return nullptr;
    auto sample = decode (block, librarySampleTitle (id), "builtin:" + id);
    if (sample != nullptr)
    {
        const std::lock_guard<std::mutex> guard (lock);
        cache[id] = sample;
    }
    return sample;
}

SamplerSettings libraryProgramSettings (const std::string& id)
{
    return settingsFrom (readJson (kProgramFolder + juce::String (id) + ".json").getProperty ("settings", {}));
}

std::shared_ptr<const SamplerProgram> makeLibraryProgram (const std::string& id, SamplerSettings* settings, std::string& error)
{
    static std::mutex lock;
    static std::map<std::string, std::weak_ptr<const SamplerProgram>> cache;

    const auto json = readJson (kProgramFolder + juce::String (id) + ".json");
    const auto* zones = json.getProperty ("zones", {}).getArray();
    if (! json.isObject() || zones == nullptr)
    {
        error = "The built-in instrument \"" + id + "\" is not part of this version.";
        return nullptr;
    }
    if (settings != nullptr)
        *settings = settingsFrom (json.getProperty ("settings", {}));

    const std::lock_guard<std::mutex> guard (lock); // one program decoded at a time
    if (const auto found = cache.find (id); found != cache.end())
        if (auto alive = found->second.lock())
            return alive;

    auto program = std::make_shared<SamplerProgram>();
    program->name = str (json, "name");
    program->source = kProgramSourcePrefix + id;
    for (const auto& z : *zones)
    {
        SampleZone zone;
        zone.rootKey = integer (z, "root", 60, 0, 127);
        zone.loKey = integer (z, "loKey", 0, 0, 127);
        zone.hiKey = integer (z, "hiKey", 127, 0, 127);
        zone.loVel = integer (z, "loVel", 1, 1, 127);
        zone.hiVel = integer (z, "hiVel", 127, 1, 127);
        zone.tune = number (z, "tune", 0.0f, -48.0f, 48.0f);
        zone.gain = number (z, "gain", 1.0f, 0.0f, 4.0f);
        zone.pan = number (z, "pan", 0.0f, -1.0f, 1.0f);
        program->zones.push_back (std::move (zone));
    }

    // Decode the sounds on a few threads (a piano has over a hundred); reading the archive itself is serialised by zipLock().
    const auto count = program->zones.size();
    std::atomic<size_t> next { 0 };
    const auto work = [&]
    {
        for (size_t i = next++; i < count; i = next++)
            program->zones[i].sample = makeLibrarySample ("lib:" + str ((*zones)[static_cast<int> (i)], "sample"));
    };
    const auto threads = std::clamp<size_t> (std::thread::hardware_concurrency(), 1, 8) - 1;
    std::vector<std::thread> pool;
    for (size_t t = 0; t < threads && t + 1 < count; ++t)
        pool.emplace_back (work);
    work();
    for (auto& t : pool)
        t.join();
    for (size_t i = 0; i < count; ++i)
        if (program->zones[i].sample == nullptr)
        {
            error = "A sound of the built-in instrument \"" + id + "\" cannot be read (" + str ((*zones)[static_cast<int> (i)], "sample") + ").";
            return nullptr;
        }
    auto ready = std::make_shared<const SamplerProgram> (program->sanitised());
    cache[id] = ready;
    return ready;
}
} // namespace sls
