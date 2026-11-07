#include "library/DrumKitFile.h"

#include <map>

#include "audio/AudioImport.h"
#include "audio/BuiltinSamples.h"
#include "audio/DrumSynth.h"
#include "audio/SoundLibrary.h"
#include "library/JsonRead.h"
#include "library/ProjectFile.h"

namespace sls
{
KitSpec KitSpec::empty()
{
    KitSpec k;
    for (int i = 0; i < kDrumPads; ++i)
        k.pads[static_cast<size_t> (i)].name = "Pad " + std::to_string (i + 1);
    return k;
}

KitSpec specFromKit (const DrumKit& kit)
{
    KitSpec spec;
    spec.name = kit.name;
    for (size_t i = 0; i < kit.pads.size(); ++i)
    {
        const auto& p = kit.pads[i];
        auto& out = spec.pads[i];
        out.name = p.name;
        out.volume = p.volume;
        out.pan = p.pan;
        out.pitch = p.pitch;
        out.swing = p.swing;
        out.chokeGroup = p.chokeGroup;
        out.mute = p.mute;
        out.solo = p.solo;
        for (size_t l = 0; l < p.layers.size(); ++l)
        {
            out.sources[l] = p.layers[l].sample != nullptr ? p.layers[l].sample->source : std::string();
            out.minVelocity[l] = p.layers[l].minVelocity;
        }
    }
    return spec;
}

juce::var kitSpecToVar (const KitSpec& spec)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("name", juce::String (spec.name));
    juce::Array<juce::var> pads;
    for (const auto& p : spec.pads)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", juce::String (p.name));
        juce::Array<juce::var> layers;
        for (size_t l = 0; l < p.sources.size(); ++l)
        {
            if (p.sources[l].empty())
                continue;
            auto* lo = new juce::DynamicObject();
            lo->setProperty ("sample", juce::String (p.sources[l]));
            lo->setProperty ("minVelocity", p.minVelocity[l]);
            layers.add (juce::var (lo));
        }
        o->setProperty ("layers", layers);
        o->setProperty ("volume", number (static_cast<double> (p.volume)));
        o->setProperty ("pan", number (static_cast<double> (p.pan)));
        o->setProperty ("pitch", number (static_cast<double> (p.pitch)));
        o->setProperty ("swing", number (static_cast<double> (p.swing)));
        o->setProperty ("choke", p.chokeGroup);
        o->setProperty ("mute", p.mute);
        o->setProperty ("solo", p.solo);
        pads.add (juce::var (o));
    }
    root->setProperty ("pads", pads);
    return juce::var (root);
}

KitSpec kitSpecFromVar (const juce::var& v)
{
    KitSpec spec = KitSpec::empty();
    auto* root = v.getDynamicObject();
    if (root == nullptr)
        return spec;
    spec.name = readName (root->getProperty ("name"), "Kit");
    auto* pads = root->getProperty ("pads").getArray();
    if (pads == nullptr)
        return spec;
    for (int i = 0; i < std::min (pads->size(), kDrumPads); ++i)
    {
        auto* o = (*pads)[i].getDynamicObject();
        if (o == nullptr)
            continue;
        auto& p = spec.pads[static_cast<size_t> (i)];
        p.name = readName (o->getProperty ("name"), p.name);
        p.volume = static_cast<float> (readNumber (o->getProperty ("volume"), 1.0, 0.0, 1.5));
        p.pan = static_cast<float> (readNumber (o->getProperty ("pan"), 0.0, -1.0, 1.0));
        p.pitch = static_cast<float> (readNumber (o->getProperty ("pitch"), 0.0, -24.0, 24.0));
        p.swing = static_cast<float> (readNumber (o->getProperty ("swing"), 0.0, 0.0, 1.0));
        p.chokeGroup = readInt (o->getProperty ("choke"), 0, 0, kChokeGroups);
        p.mute = readBool (o->getProperty ("mute"), false);
        p.solo = readBool (o->getProperty ("solo"), false);
        if (auto* layers = o->getProperty ("layers").getArray())
            for (int l = 0; l < std::min (layers->size(), kPadLayers); ++l)
                if (auto* lo = (*layers)[l].getDynamicObject())
                {
                    const auto source = lo->getProperty ("sample");
                    if (! source.isString())
                        continue;
                    p.sources[static_cast<size_t> (l)] = source.toString().toStdString();
                    p.minVelocity[static_cast<size_t> (l)] = readInt (lo->getProperty ("minVelocity"), 1, 1, 127);
                }
    }
    return spec;
}

std::shared_ptr<const SampleData> loadKitSample (const std::string& source, juce::String& problem)
{
    if (source.rfind ("builtin:", 0) == 0)
    {
        auto s = makeBuiltinSample (source.substr (8));
        if (s == nullptr)
        {
            const auto removed = removedBuiltinMessage (source.substr (8));
            problem = removed.empty() ? "Built-in sound \"" + juce::String (source.substr (8)) + "\" is not known to this version."
                                      : juce::String (removed);
        }
        return s;
    }
    const auto imported = importAudioFile (juce::File (juce::String (source)));
    if (imported.data == nullptr)
    {
        problem = "Sample not found: " + juce::String (source);
        return nullptr;
    }
    auto sample = imported.data;
    const int maxFrames = static_cast<int> (sample->sampleRate * kMaxPadSampleSeconds);
    if (sample->numFrames() > maxFrames)
    {
        auto cut = std::make_shared<SampleData> (*sample);
        cut->left.resize (static_cast<size_t> (maxFrames));
        cut->right.resize (static_cast<size_t> (maxFrames));
        sample = cut;
    }
    return sample;
}
DrumKit loadKit (const KitSpec& spec, juce::StringArray& problems)
{
    DrumKit kit = DrumKit::empty();
    kit.name = spec.name;
    std::map<std::string, std::shared_ptr<const SampleData>> cache;
    for (size_t i = 0; i < spec.pads.size(); ++i)
    {
        const auto& p = spec.pads[i];
        auto& pad = kit.pads[i];
        pad.name = p.name;
        pad.volume = p.volume;
        pad.pan = p.pan;
        pad.pitch = p.pitch;
        pad.swing = p.swing;
        pad.chokeGroup = p.chokeGroup;
        pad.mute = p.mute;
        pad.solo = p.solo;
        for (size_t l = 0; l < p.sources.size(); ++l)
        {
            const auto& source = p.sources[l];
            if (source.empty())
                continue;
            auto it = cache.find (source);
            if (it == cache.end())
            {
                juce::String problem;
                auto sample = loadKitSample (source, problem);
                if (sample == nullptr)
                {
                    problems.add (problem);
                    sample = makeMissingSample (source); // silent, but saving again keeps the reference
                }
                it = cache.emplace (source, std::move (sample)).first;
            }
            pad.layers[l].sample = it->second;
            pad.layers[l].minVelocity = p.minVelocity[l];
        }
    }
    return kit.sanitised();
}

namespace
{
juce::String relativeOrAbsolute (const std::string& source, const juce::File& folder)
{
    const juce::File f { juce::String (source) };
    if (f.isAChildOf (folder))
        return f.getRelativePathFrom (folder).replaceCharacter ('\\', '/');
    return juce::String (source);
}

std::string resolveAgainst (const std::string& source, const juce::File& folder)
{
    if (source.empty() || source.rfind ("builtin:", 0) == 0 || juce::File::isAbsolutePath (juce::String (source)))
        return source;
    return folder.getChildFile (juce::String (source)).getFullPathName().toStdString();
}
} // namespace

KitSpec resolvedForFolder (const KitSpec& spec, const juce::File& folder)
{
    KitSpec out = spec;
    for (auto& p : out.pads)
        for (auto& s : p.sources)
            s = resolveAgainst (s, folder);
    return out;
}

bool isKitFolder (const juce::File& folder)
{
    return folder.isDirectory() && folder.getChildFile (kKitFileName).existsAsFile();
}

juce::String saveKitFolder (const DrumKit& kit, const juce::File& folder)
{
    if (folder.existsAsFile())
        return "\"" + folder.getFileName() + "\" is a file, not a folder.";
    if (folder.isDirectory() && ! isKitFolder (folder) && folder.getNumberOfChildFiles (juce::File::findFilesAndDirectories) > 0)
        return "The folder \"" + folder.getFileName() + "\" already holds other files. Choose an empty folder or a new name.";
    if (! folder.createDirectory())
        return "Could not create the folder " + folder.getFullPathName();

    auto spec = specFromKit (kit);
    const auto samples = folder.getChildFile ("samples");
    int copied = 0;
    for (size_t i = 0; i < spec.pads.size(); ++i)
        for (size_t l = 0; l < spec.pads[i].sources.size(); ++l)
        {
            auto& source = spec.pads[i].sources[l];
            if (source.empty() || source.rfind ("builtin:", 0) == 0)
                continue; // sounds inside the program stay references
            const juce::File original { juce::String (source) };
            if (original.isAChildOf (folder))
            {
                source = relativeOrAbsolute (source, folder).toStdString();
                continue;
            }
            if (! original.existsAsFile())
                continue; // a missing file stays what it was: a reference the user can fix later
            if (! samples.createDirectory())
                return "Could not create the folder " + samples.getFullPathName();
            const auto padText = juce::String (static_cast<int> (i) + 1).paddedLeft ('0', 2);
            auto target = samples.getChildFile (padText + "-" + juce::String (static_cast<int> (l) + 1) + "-" + original.getFileName());
            if (! original.copyFileTo (target))
                return "Could not copy " + original.getFileName() + " into the kit folder.";
            source = relativeOrAbsolute (target.getFullPathName().toStdString(), folder).toStdString();
            ++copied;
        }

    auto* root = new juce::DynamicObject();
    root->setProperty ("format", kKitFormatName);
    root->setProperty ("version", kKitFormatVersion);
    const auto kitVar = kitSpecToVar (spec);
    if (auto* kitObject = kitVar.getDynamicObject())
    {
        root->setProperty ("name", kitObject->getProperty ("name"));
        root->setProperty ("pads", kitObject->getProperty ("pads"));
    }
    const auto text = juce::JSON::toString (juce::var (root), false);

    const auto temp = folder.getChildFile (juce::String (kKitFileName) + ".tmp");
    if (! temp.replaceWithText (text))
        return "Could not write " + temp.getFullPathName();
    const auto target = folder.getChildFile (kKitFileName);
    if (! temp.moveFileTo (target))
    {
        temp.deleteFile();
        return "Could not write " + target.getFullPathName();
    }
    return {};
}

KitSpec readKitFolder (const juce::File& folder, juce::StringArray& problems)
{
    KitSpec spec = KitSpec::empty();
    spec.name = folder.getFileName().toStdString().substr (0, kMaxNameLength);
    const auto kitFile = folder.getChildFile (kKitFileName);
    if (kitFile.existsAsFile())
    {
        if (kitFile.getSize() > 4LL * 1024 * 1024)
        {
            problems.add ("kit.json is too large to be a kit file.");
            return spec;
        }
        const auto parsed = juce::JSON::parse (kitFile.loadFileAsString());
        auto* root = parsed.getDynamicObject();
        if (root == nullptr || root->getProperty ("format").toString() != kKitFormatName)
        {
            problems.add ("kit.json is not a Sun Loops Studio kit file.");
            return spec;
        }
        if (static_cast<int> (root->getProperty ("version")) > kKitFormatVersion)
            problems.add ("This kit was made by a newer version of the program; some of it may be ignored.");
        return resolvedForFolder (kitSpecFromVar (parsed), folder);
    }

    // A plain folder of audio files: its files in name order fill the pads.
    juce::Array<juce::File> files;
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, true))
        if (classifyMediaFile (f) == FileKind::Audio)
            files.add (f);
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFullPathName().compareNatural (b.getFullPathName()) < 0; });
    if (files.isEmpty())
        problems.add ("There are no audio files in " + folder.getFullPathName());
    if (files.size() > kDrumPads)
        problems.add ("The folder holds " + juce::String (files.size()) + " sounds; the first 16 are used.");
    for (int i = 0; i < std::min (files.size(), kDrumPads); ++i)
    {
        auto& pad = spec.pads[static_cast<size_t> (i)];
        pad.name = files[i].getFileNameWithoutExtension().toStdString().substr (0, 24);
        pad.sources[0] = files[i].getFullPathName().toStdString();
    }
    return spec;
}

KitSpec basicKitSpec()
{
    KitSpec k = KitSpec::empty();
    k.name = "Sun Loops Basic Kit";
    const std::array<std::pair<const char*, const char*>, 3> pieces { { { "Kick", "builtin:kick" }, { "Snare", "builtin:snare" }, { "Hat", "builtin:hat" } } };
    for (size_t i = 0; i < pieces.size(); ++i)
    {
        k.pads[i].name = pieces[i].first;
        k.pads[i].sources[0] = pieces[i].second;
    }
    k.pads[2].chokeGroup = 1;
    return k;
}

const std::vector<BuiltinKit>& builtinKits()
{
    static const std::vector<BuiltinKit> kits = []
    {
        std::vector<BuiltinKit> list;
        for (const auto& k : libraryKits())
            list.push_back ({ "lib:" + k.id, k.name, k.credit });
        for (const auto& k : generatedKits())
            list.push_back ({ "gen:" + k.id, k.name, "Made by the program" });
        list.push_back ({ "basic", "Sun Loops Basic Kit", "Made by the program" });
        return list;
    }();
    return kits;
}

KitSpec builtinKitSpec (const std::string& id)
{
    if (id == "basic")
        return basicKitSpec();
    KitSpec k = KitSpec::empty();
    if (id.rfind ("lib:", 0) == 0)
    {
        for (const auto& kit : libraryKits())
            if ("lib:" + kit.id == id)
            {
                k.name = kit.name;
                for (size_t p = 0; p < kit.pads.size() && p < k.pads.size(); ++p)
                {
                    auto& pad = k.pads[p];
                    pad.name = kit.pads[p].name;
                    pad.chokeGroup = kit.pads[p].chokeGroup;
                    pad.pan = kit.pads[p].pan;
                    pad.volume = kit.pads[p].volume;
                    for (size_t l = 0; l < kit.pads[p].layers.size() && l < pad.sources.size(); ++l)
                    {
                        pad.sources[l] = "builtin:" + kit.pads[p].layers[l].first;
                        pad.minVelocity[l] = kit.pads[p].layers[l].second;
                    }
                }
            }
        return k;
    }
    if (id.rfind ("gen:", 0) == 0)
        for (const auto& kit : generatedKits())
            if ("gen:" + kit.id == id)
            {
                k.name = kit.name;
                for (size_t p = 0; p < kit.pads.size() && p < k.pads.size(); ++p)
                {
                    auto& pad = k.pads[p];
                    pad.name = kit.pads[p].name;
                    pad.chokeGroup = kit.pads[p].chokeGroup;
                    pad.pan = kit.pads[p].pan;
                    for (size_t l = 0; l < pad.sources.size(); ++l)
                    {
                        pad.sources[l] = "builtin:gen:" + kit.id + "/" + kit.pads[p].piece + "/" + std::to_string (l + 1);
                        pad.minVelocity[l] = kGeneratedMinVelocity[l];
                    }
                }
            }
    return k;
}
} // namespace sls
