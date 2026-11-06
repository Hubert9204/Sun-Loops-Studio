#pragma once

#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

namespace sls
{
struct PluginInfo
{
    juce::String name;
    juce::String manufacturer;
    juce::String path;
    juce::String identifier; // JUCE's PluginDescription::createIdentifierString(): how the program finds it again
    juce::String format;     // "VST3", "LV2", "LADSPA"
    bool isInstrument = false;
    int numInputChannels = 0;
    int numOutputChannels = 0;
};

// Finds VST3 plugins on disk. Scanning loads each candidate plugin in this process to read its
// description, so it is slow and can in principle crash on a broken plugin: call it from a
// background thread, never from the audio thread. A "dead man's pedal" file is kept so a plugin
// that crashed a previous scan is skipped next time.
class PluginScanner
{
public:
    // ~/.vst3, /usr/lib/vst3, /usr/local/lib/vst3 and so on, as JUCE defines them for this platform.
    static juce::FileSearchPath defaultSearchPaths();

    // Blocking. Returns an empty list for missing or empty directories.
    std::vector<PluginInfo> scan (const juce::FileSearchPath& searchPaths, bool recursive = true);
};
} // namespace sls
