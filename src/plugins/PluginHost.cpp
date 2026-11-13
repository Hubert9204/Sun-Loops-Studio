#include "plugins/PluginHost.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sls
{
// ------------------------------------------------------------------------- JucePlugin

JucePlugin::JucePlugin (std::unique_ptr<juce::AudioPluginInstance> instanceToUse, const juce::PluginDescription& d)
    : instance (std::move (instanceToUse)), description (d)
{
    name = (instance != nullptr ? instance->getName() : d.name).toStdString();
    instrument = d.isInstrument;
    midi.ensureSize (8192);
    if (instance != nullptr)
    {
        for (auto* p : instance->getParameters())
            if (p != nullptr && static_cast<int> (parameters.size()) < kMaxPluginParams)
                parameters.push_back (p);
        automated = std::make_unique<std::atomic<float>[]> (std::max<size_t> (1, parameters.size()));
        for (size_t i = 0; i < parameters.size(); ++i)
            automated[i].store (std::numeric_limits<float>::quiet_NaN(), std::memory_order_relaxed);
        instance->addListener (this);
        // Instruments are told the song's tempo and position (arpeggiators, synced LFOs). Effects are not yet: with a play head set,
        // a VST3 effect's parameter set from the host did not reach its saved state in the tests (to be looked at).
        if (instrument)
            instance->setPlayHead (&head);
    }
}

bool JucePlugin::selectProgram (const std::string& programName)
{
    if (instance == nullptr)
        return false;
    for (int i = 0; i < instance->getNumPrograms(); ++i)
        if (instance->getProgramName (i).toStdString() == programName)
        {
            instance->setCurrentProgram (i);
            return true;
        }
    return false;
}

void JucePlugin::setParameter (int index, float value) noexcept
{
    if (index < 0 || index >= static_cast<int> (parameters.size()) || ! std::isfinite (value))
        return;
    value = std::clamp (value, 0.0f, 1.0f);
    auto& last = automated[static_cast<size_t> (index)];
    const float previous = last.load (std::memory_order_relaxed);
    if (std::isfinite (previous) && std::abs (previous - value) < 1.0e-6f)
        return;
    last.store (value, std::memory_order_relaxed);
    // JUCE's hosted parameters take a value from any thread without locking (VST3 and LV2 queue it for the plugin's next block).
    settingFromAutomation.store (true, std::memory_order_relaxed);
    parameters[static_cast<size_t> (index)]->setValue (value);
    settingFromAutomation.store (false, std::memory_order_relaxed);
}

float JucePlugin::parameterValue (int index) const noexcept
{
    return index >= 0 && index < static_cast<int> (parameters.size()) ? parameters[static_cast<size_t> (index)]->getValue() : 0.0f;
}

float JucePlugin::parameterDefault (int index) const noexcept
{
    return index >= 0 && index < static_cast<int> (parameters.size()) ? parameters[static_cast<size_t> (index)]->getDefaultValue() : 0.0f;
}

std::string JucePlugin::parameterName (int index) const
{
    if (index < 0 || index >= static_cast<int> (parameters.size()))
        return {};
    const auto n = parameters[static_cast<size_t> (index)]->getName (64).trim();
    return (n.isEmpty() ? "Parameter " + juce::String (index + 1) : n).toStdString();
}

std::string JucePlugin::parameterText (int index, float value) const
{
    if (index < 0 || index >= static_cast<int> (parameters.size()))
        return {};
    auto* p = parameters[static_cast<size_t> (index)];
    const auto text = p->getText (std::clamp (value, 0.0f, 1.0f), 32).trim();
    const auto label = p->getLabel().trim();
    return (text + (label.isNotEmpty() && ! text.endsWith (label) ? " " + label : juce::String())).toStdString();
}

std::shared_ptr<JucePlugin> JucePlugin::placeholder (const juce::PluginDescription& d, juce::MemoryBlock savedState, bool isInstrumentPlugin)
{
    std::shared_ptr<JucePlugin> p (new JucePlugin());
    p->description = d;
    p->keptState = std::move (savedState);
    p->name = d.name.toStdString();
    p->instrument = isInstrumentPlugin;
    return p;
}

JucePlugin::~JucePlugin()
{
    if (instance != nullptr)
    {
        instance->removeListener (this);
        instance->releaseResources();
    }
}

void JucePlugin::prepare (double sampleRate, int maxBlock)
{
    rate = sampleRate;
    block = std::max (1, maxBlock);
    if (instance == nullptr)
        return;
    instance->releaseResources();
    instance->enableAllBuses();
    const int channels = std::max ({ 2, instance->getTotalNumInputChannels(), instance->getTotalNumOutputChannels() });
    buffer.setSize (channels, block, false, true, false);
    instance->setRateAndBufferSizeDetails (sampleRate, block);
    instance->prepareToPlay (sampleRate, block);
}

void JucePlugin::process (float* left, float* right, int numSamples, const PluginMidiEvent* events, int numEvents) noexcept
{
    if (instance == nullptr || numSamples <= 0 || rate <= 0.0)
        return; // a placeholder: an effect leaves the signal alone, an instrument stays silent
    numSamples = std::min (numSamples, block);

    // The plugin's own lock is only held by the plugin or the host while it is being reconfigured: never wait for it here.
    const juce::ScopedTryLock lock (instance->getCallbackLock());
    if (! lock.isLocked() || instance->isSuspended())
        return;

    const juce::ScopedNoDenormals noDenormals;
    const int channels = buffer.getNumChannels();
    for (int ch = 0; ch < channels; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        if (ch == 0)
            std::copy (left, left + numSamples, d);
        else if (ch == 1)
            std::copy (right, right + numSamples, d);
        else
            std::fill (d, d + numSamples, 0.0f);
    }
    juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), channels, numSamples); // no allocation: refers to `buffer`

    midi.clear();
    for (int i = 0; i < numEvents; ++i)
        midi.addEvent (events[i].bytes, 3, std::clamp (events[i].offset, 0, numSamples - 1));

    instance->processBlock (view, midi);

    const int outs = instance->getTotalNumOutputChannels();
    std::copy (view.getReadPointer (0), view.getReadPointer (0) + numSamples, left);
    std::copy (view.getReadPointer (outs >= 2 ? 1 : 0), view.getReadPointer (outs >= 2 ? 1 : 0) + numSamples, right);
}

juce::MemoryBlock JucePlugin::getState() const
{
    if (instance == nullptr)
        return keptState;
    juce::MemoryBlock state;
    instance->getStateInformation (state);
    return state;
}

bool JucePlugin::setState (const juce::MemoryBlock& state)
{
    if (instance == nullptr)
    {
        keptState = state;
        return false;
    }
    if (state.getSize() > 0)
        instance->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    changed.store (false, std::memory_order_release); // what was just loaded is not an edit
    return true;
}

// ------------------------------------------------------------------------- PluginHost

PluginHost::PluginHost()
{
    formatManager.addFormat (new juce::VST3PluginFormat());
#if JUCE_PLUGINHOST_AU && JUCE_MAC
    formatManager.addFormat (new juce::AudioUnitPluginFormat()); // 0.63.0: macOS's own plugin format
#endif
#if JUCE_PLUGINHOST_LV2
    formatManager.addFormat (new juce::LV2PluginFormat());
#endif
#if JUCE_PLUGINHOST_LADSPA
    formatManager.addFormat (new juce::LADSPAPluginFormat());
#endif
}

juce::StringArray PluginHost::formatNames() const
{
    juce::StringArray names;
    for (auto* f : formatManager.getFormats())
        names.add (f->getName());
    return names;
}

int PluginHost::scan (const std::function<juce::FileSearchPath (const juce::String&)>& extraPaths, const juce::File& deadMansPedal,
                      const std::function<void (const juce::String&)>& progress)
{
    for (auto* format : formatManager.getFormats())
    {
        auto paths = format->getDefaultLocationsToSearch();
        if (extraPaths)
        {
            const auto extra = extraPaths (format->getName());
            for (int i = 0; i < extra.getNumPaths(); ++i)
                paths.addIfNotAlreadyThere (extra[i]);
        }
        if (format->getName() == "LV2")
        {
            lv2Folders = paths;
            lv2FoldersRead = true; // the scanner reads them now
        }
        juce::PluginDirectoryScanner scanner (knownPlugins, *format, paths, true, deadMansPedal, false);
        juce::String current;
        for (;;)
        {
            const auto next = scanner.getNextPluginFileThatWillBeScanned();
            if (progress && next.isNotEmpty())
                progress (format->getName() + ": " + next);
            if (! scanner.scanNextFile (true, current))
                break;
        }
    }
    deadMansPedal.deleteFile();
    return knownPlugins.getNumTypes();
}

juce::FileSearchPath PluginHost::bundledPaths()
{
    juce::FileSearchPath paths;
    juce::Array<juce::File> prefixes;
    // Inside an AppImage its runtime names the mounted folder; otherwise the folder above the program's own (bin/..), found both
    // as JUCE sees it and through /proc/self/exe (the real file, whatever symbolic link started it).
    if (const auto appDir = juce::SystemStats::getEnvironmentVariable ("APPDIR", {}); appDir.isNotEmpty())
        prefixes.add (juce::File (appDir).getChildFile ("usr"));
    const auto executable = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    prefixes.add (executable.getParentDirectory().getParentDirectory());
#if JUCE_MAC
    // 0.63.0: Sun Loops Studio.app/Contents/MacOS/Sun Loops Studio. The bundled plugins are in Contents/PlugIns/VST3; a
    // development build's .app sits in build/SunLoopsStudio_artefacts/Release, as the Linux program does.
    const auto contents = executable.getParentDirectory().getParentDirectory();
    prefixes.add (contents.getParentDirectory().getParentDirectory().getParentDirectory());
#else
    const auto self = juce::File ("/proc/self/exe");
    if (self.isSymbolicLink())
        prefixes.add (self.getLinkedTarget().getParentDirectory().getParentDirectory());
#endif
    for (const auto& prefix : prefixes)
    {
        juce::Array<juce::File> dirs { prefix.getChildFile ("lib/vst3"), prefix.getChildFile ("lib64/vst3"), prefix.getChildFile ("PlugIns/VST3") };
        // A development build: build/SunLoopsStudio_artefacts/Release/Sun Loops Studio -> build/<Plugin>_artefacts/Release/VST3
        for (const char* name : { "SunBurst", "HexForge" })
            for (const char* config : { "Release", "Debug" })
                dirs.add (prefix.getParentDirectory().getChildFile (juce::String (name) + "_artefacts/" + config + "/VST3"));
        for (const auto& dir : dirs)
            if (dir.getChildFile ("SunBurst.vst3").isDirectory() || dir.getChildFile ("HexForge.vst3").isDirectory())
                paths.addIfNotAlreadyThere (dir);
    }
    return paths;
}

int PluginHost::addBundledPlugins()
{
    int added = 0;
    const auto paths = bundledPaths();
    // An AppImage is mounted somewhere else each time it runs: forget bundled plugins listed from a place that is gone.
    for (const auto& t : knownPlugins.getTypes())
        if (t.pluginFormatName == "VST3" && (t.name == "SunBurst" || t.name == "HexForge") && ! juce::File (t.fileOrIdentifier).exists())
            knownPlugins.removeType (t);
    for (auto* format : formatManager.getFormats())
        if (format->getName() == "VST3")
            for (int i = 0; i < paths.getNumPaths(); ++i)
                for (const auto& bundle : paths[i].findChildFiles (juce::File::findDirectories, false, "*.vst3"))
                {
                    juce::OwnedArray<juce::PluginDescription> types;
                    format->findAllTypesForFile (types, bundle.getFullPathName());
                    for (auto* t : types)
                        if (knownPlugins.addType (*t))
                            ++added;
                }
    return added;
}

juce::FileSearchPath PluginHost::distributionPaths (const juce::String& formatName)
{
    juce::FileSearchPath paths;
    const auto addIfThere = [&paths] (const char* path)
    {
        const juce::File f (path); // JUCE expands a leading "~" to the home folder
        if (f.isDirectory())
            paths.add (f);
    };
    if (formatName == "VST3")
    {
        for (const char* p : { "/usr/lib64/vst3", "/usr/local/lib64/vst3" })
            addIfThere (p);
        const auto bundled = bundledPaths();
        for (int i = 0; i < bundled.getNumPaths(); ++i)
            paths.addIfNotAlreadyThere (bundled[i]);
    }
    else if (formatName == "LV2")
        for (const char* p : { "/usr/lib/lv2", "/usr/lib64/lv2", "/usr/local/lib/lv2", "/usr/local/lib64/lv2" })
            addIfThere (p);
    else if (formatName == "LADSPA")
        for (const char* p : { "/usr/lib/ladspa", "/usr/lib64/ladspa", "/usr/local/lib/ladspa", "/usr/local/lib64/ladspa", "~/.ladspa" })
            addIfThere (p);
    return paths;
}

void PluginHost::loadKnownList (const juce::File& file)
{
    if (auto xml = juce::parseXML (file))
    {
        knownPlugins.recreateFromXml (*xml);
        lv2Folders = juce::FileSearchPath (xml->getStringAttribute ("slsLv2Folders"));
        lv2FoldersRead = false;
    }
}

void PluginHost::readLv2Folders()
{
    if (lv2FoldersRead)
        return;
    lv2FoldersRead = true;
    for (auto* format : formatManager.getFormats())
        if (format->getName() == "LV2")
        {
            auto folders = lv2Folders;
            const auto defaults = format->getDefaultLocationsToSearch();
            const auto extra = distributionPaths ("LV2");
            for (int i = 0; i < defaults.getNumPaths(); ++i)
                folders.addIfNotAlreadyThere (defaults[i]);
            for (int i = 0; i < extra.getNumPaths(); ++i)
                folders.addIfNotAlreadyThere (extra[i]);
            format->searchPathsForPlugins (folders, true, false);
        }
}

void PluginHost::saveKnownList (const juce::File& file) const
{
    if (auto xml = knownPlugins.createXml())
    {
        xml->setAttribute ("slsLv2Folders", lv2Folders.toString());
        xml->writeTo (file);
    }
}

std::shared_ptr<JucePlugin> PluginHost::create (const juce::PluginDescription& description, double sampleRate, int maxBlock, juce::String& error)
{
    if (description.pluginFormatName == "LV2")
        readLv2Folders();
    auto instance = formatManager.createPluginInstance (description, sampleRate, maxBlock, error);
    if (instance == nullptr)
    {
        if (error.isEmpty())
            error = "The plugin " + description.name + " could not be loaded.";
        return nullptr;
    }
    auto plugin = std::make_shared<JucePlugin> (std::move (instance), description);
    plugin->prepare (sampleRate, maxBlock);
    registerPlugin (plugin);
    return plugin;
}

void PluginHost::registerPlugin (const std::shared_ptr<JucePlugin>& plugin)
{
    const std::lock_guard<std::mutex> guard (registryLock);
    registry.erase (std::remove_if (registry.begin(), registry.end(), [] (const std::weak_ptr<JucePlugin>& w) { return w.expired(); }), registry.end());
    registry.push_back (plugin);
}

std::vector<std::shared_ptr<JucePlugin>> PluginHost::livePlugins()
{
    const std::lock_guard<std::mutex> guard (registryLock);
    std::vector<std::shared_ptr<JucePlugin>> list;
    for (const auto& w : registry)
        if (auto p = w.lock())
            list.push_back (std::move (p));
    return list;
}

void PluginHost::prepareAll (double sampleRate, int maxBlock)
{
    for (const auto& p : livePlugins())
        p->prepare (sampleRate, maxBlock);
}

juce::String PluginHost::descriptionToText (const juce::PluginDescription& d)
{
    if (auto xml = d.createXml())
        return xml->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
    return {};
}

bool PluginHost::descriptionFromText (const juce::String& text, juce::PluginDescription& d)
{
    if (auto xml = juce::parseXML (text))
        return d.loadFromXml (*xml);
    return false;
}
} // namespace sls
