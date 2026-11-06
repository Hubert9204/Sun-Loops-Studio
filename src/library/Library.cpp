#include "library/Library.h"

#include <algorithm>

#include "audio/BuiltinSamples.h"
#include "audio/CyberHub.h"
#include "library/ChannelPreset.h"

namespace sls
{
namespace
{
bool matches (const juce::String& fileName, NodeKind kind, const juce::String& extension)
{
    if (kind == NodeKind::AudioFile)
        return isAudioFileName (fileName) || fileName.endsWithIgnoreCase (".sf2"); // SoundFonts go on sampler channels
    return fileName.endsWithIgnoreCase (extension);
}

std::string displayName (const juce::File& file, NodeKind kind)
{
    // Presets show without their extension; audio keeps its extension so the format is visible.
    return (kind == NodeKind::Preset ? file.getFileNameWithoutExtension() : file.getFileName()).toStdString();
}

BrowserNode folder (std::string name)
{
    BrowserNode n;
    n.name = std::move (name);
    n.kind = NodeKind::Folder;
    return n;
}

BrowserNode leaf (std::string name, NodeKind kind, std::string payload)
{
    BrowserNode n;
    n.name = std::move (name);
    n.kind = kind;
    n.payload = std::move (payload);
    return n;
}

BrowserNode builtinSamples (const char* title)
{
    auto f = folder (title);
    for (const auto& id : builtinSampleIds())
    {
        const auto sample = makeBuiltinSample (id);
        f.children.push_back (leaf (sample != nullptr ? sample->name : id, NodeKind::BuiltinSample, id));
    }
    return f;
}

// The Cyber-Hub theme: its sounds filed by what they are good for, and the start-up sound.
BrowserNode cyberHubFolder()
{
    auto root = folder ("Cyber-Hub");
    auto hits = folder ("Hits and sub");
    auto tones = folder ("Tones");
    auto longs = folder ("Long and ambient");
    for (const auto& s : cyberHubSounds())
    {
        auto& target = s.role == CyberRole::Hit ? hits : (s.role == CyberRole::Tone ? tones : longs);
        target.children.push_back (leaf (s.title, NodeKind::BuiltinSample, s.id));
    }
    root.children.push_back (std::move (hits));
    root.children.push_back (std::move (tones));
    root.children.push_back (std::move (longs));
    root.children.push_back (leaf ("Sun Loops intro", NodeKind::BuiltinSample, kStartupSampleId));
    root.children.push_back (leaf (kCyberHubCredit, NodeKind::Note, {}));
    return root;
}

BrowserNode cyberHubKitFolder()
{
    auto kit = folder ("Cyber-Hub Kit");
    for (const auto& piece : cyberHubKit())
    {
        std::string title = piece.id;
        for (const auto& s : cyberHubSounds())
            if (s.id == piece.id)
                title = s.title;
        kit.children.push_back (leaf (std::string (piece.label) + "  (" + title + ")", NodeKind::BuiltinSample, piece.id));
    }
    return kit;
}

BrowserNode userContent (const char* title, const juce::File& directory, NodeKind kind, const juce::String& extension,
                         int& budget)
{
    auto n = scanDirectory (directory, kind, extension, kMaxBrowserDepth, budget);
    n.name = title;
    return n;
}
} // namespace

LibraryPaths LibraryPaths::forRoot (const juce::File& root)
{
    LibraryPaths p;
    p.root = root;
    p.samples = root.getChildFile ("Samples");
    p.drumKits = root.getChildFile ("Drum Kits");
    p.presets = root.getChildFile ("Presets");
    p.projects = root.getChildFile ("Projects");
    return p;
}

LibraryPaths LibraryPaths::defaults()
{
    return forRoot (juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Sun Loops Studio"));
}

bool LibraryPaths::ensureExists() const
{
    bool ok = true;
    for (const auto& dir : { samples, drumKits, presets, projects })
        ok = ok && (dir.isDirectory() || dir.createDirectory().wasOk());
    return ok;
}

bool isAudioFileName (const juce::String& fileName)
{
    for (const char* ext : { ".wav", ".flac", ".ogg", ".oga", ".aif", ".aiff", ".mp3", ".aac", ".m4a" })
        if (fileName.endsWithIgnoreCase (ext))
            return true;
    return false;
}

BrowserNode scanDirectory (const juce::File& directory, NodeKind leafKind, const juce::String& extension,
                           int maxDepth, int& budget)
{
    auto result = folder (directory.getFileName().toStdString());
    if (! directory.isDirectory() || maxDepth < 0)
        return result;

    auto entries = directory.findChildFiles (juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles, false);

    // Folders first, then files, each sorted by name without regard to case.
    std::sort (entries.begin(), entries.end(), [] (const juce::File& a, const juce::File& b)
    {
        if (a.isDirectory() != b.isDirectory())
            return a.isDirectory();
        return a.getFileName().compareIgnoreCase (b.getFileName()) < 0;
    });

    bool truncated = false;
    for (const auto& entry : entries)
    {
        if (budget <= 0)
        {
            truncated = true;
            break;
        }

        if (entry.isDirectory())
        {
            if (entry.isSymbolicLink())
                continue; // never follow links: they can loop back
            auto sub = scanDirectory (entry, leafKind, extension, maxDepth - 1, budget);
            if (! sub.children.empty())
            {
                --budget;
                result.children.push_back (std::move (sub));
            }
        }
        else if (matches (entry.getFileName(), leafKind, extension))
        {
            --budget;
            result.children.push_back (leaf (displayName (entry, leafKind), leafKind, entry.getFullPathName().toStdString()));
        }
    }

    if (truncated)
        result.children.push_back (leaf ("... more files not shown", NodeKind::Note, {}));

    return result;
}

BrowserNode buildBrowserTree (const LibraryPaths& paths, const std::vector<PluginInfo>& plugins, PluginScanState scanState)
{
    int budget = kMaxBrowserNodes;
    auto root = folder ("Browser");

    // Samples: the built-in sounds, then whatever is in the user's Samples folder.
    {
        auto samples = folder ("Samples");
        samples.children.push_back (builtinSamples ("Built-in"));
        if (! cyberHubSounds().empty())
            samples.children.push_back (cyberHubFolder());
        auto user = userContent ("My samples", paths.samples, NodeKind::AudioFile, {}, budget);
        if (user.children.empty())
            user.children.push_back (leaf ("Put audio files in " + paths.samples.getFullPathName().toStdString(), NodeKind::Note, {}));
        samples.children.push_back (std::move (user));
        root.children.push_back (std::move (samples));
    }

    // Drum kits: a built-in kit, then one entry per sub-folder of the user's Drum Kits folder.
    {
        auto kits = folder ("Drum Kits");
        kits.children.push_back (builtinSamples ("Sun Loops Basic Kit"));
        if (! cyberHubSounds().empty())
            kits.children.push_back (cyberHubKitFolder());
        auto user = scanDirectory (paths.drumKits, NodeKind::AudioFile, {}, kMaxBrowserDepth, budget);
        for (auto& child : user.children)
            kits.children.push_back (std::move (child));
        if (user.children.empty())
            kits.children.push_back (leaf ("Make a folder per kit in " + paths.drumKits.getFullPathName().toStdString(),
                                           NodeKind::Note, {}));
        root.children.push_back (std::move (kits));
    }

    // Instruments: what can make the sound of a channel.
    {
        auto instruments = folder ("Instruments");
        instruments.children.push_back (leaf ("Synth", NodeKind::Instrument, "synth"));
        instruments.children.push_back (leaf ("Sampler", NodeKind::Instrument, "sampler"));
        instruments.children.push_back (leaf ("Drum machine", NodeKind::Instrument, "drums"));
        root.children.push_back (std::move (instruments));
    }

    // Presets: factory ones, then the user's own (saved from a channel's right-click menu).
    {
        auto presets = folder ("Presets");
        auto factory = folder ("Factory");
        for (const auto& p : factoryPresets())
            factory.children.push_back (leaf (p.name, NodeKind::Preset, "builtin:" + p.id));
        presets.children.push_back (std::move (factory));

        auto user = userContent ("My presets", paths.presets, NodeKind::Preset, kPresetExtension, budget);
        if (user.children.empty())
            user.children.push_back (leaf ("Right-click a channel > Save as preset...", NodeKind::Note, {}));
        presets.children.push_back (std::move (user));
        root.children.push_back (std::move (presets));
    }

    // Plugin: the VST3 / LV2 / LADSPA plugins found by the last scan, instruments and effects apart. Drag an instrument onto the rack
    // (or double-click it) for a plugin channel; an effect goes into a mixer slot (drag it onto a channel: its track's first free slot).
    {
        auto plugin = folder ("Plugin");
        switch (scanState)
        {
            case PluginScanState::NotScanned:
                plugin.children.push_back (leaf ("Scan for plugins (VST3, LV2, LADSPA)...", NodeKind::Action, "scan-plugins"));
                break;
            case PluginScanState::Scanning:
                plugin.children.push_back (leaf ("Scanning... (this can take a while)", NodeKind::Note, {}));
                break;
            case PluginScanState::Done:
                plugin.children.push_back (leaf ("Scan again...", NodeKind::Action, "scan-plugins"));
                if (plugins.empty())
                    plugin.children.push_back (leaf ("No plugins found", NodeKind::Note, {}));
                break;
        }
        if (! plugins.empty())
        {
            auto instruments = folder ("Instruments");
            auto effects = folder ("Effects");
            for (const auto& p : plugins)
            {
                const auto label = (p.name + "  (" + p.format + (p.manufacturer.isNotEmpty() ? ", " + p.manufacturer : juce::String()) + ")").toStdString();
                (p.isInstrument ? instruments : effects)
                    .children.push_back (leaf (label, NodeKind::Plugin, ((p.isInstrument ? "instrument|" : "effect|") + p.identifier).toStdString()));
            }
            if (! instruments.children.empty())
                plugin.children.push_back (std::move (instruments));
            if (! effects.children.empty())
                plugin.children.push_back (std::move (effects));
        }
        root.children.push_back (std::move (plugin));
    }

    return root;
}
} // namespace sls
