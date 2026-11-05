#include "plugins/PluginScanner.h"

namespace sls
{
juce::FileSearchPath PluginScanner::defaultSearchPaths()
{
    juce::VST3PluginFormat format;
    return format.getDefaultLocationsToSearch();
}

std::vector<PluginInfo> PluginScanner::scan (const juce::FileSearchPath& searchPaths, bool recursive)
{
    std::vector<PluginInfo> result;

    juce::VST3PluginFormat format;
    juce::KnownPluginList knownPlugins;

    const auto deadMansPedal = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                   .getChildFile ("sun-loops-studio-plugin-scan.txt");

    juce::PluginDirectoryScanner scanner (knownPlugins, format, searchPaths, recursive, deadMansPedal, false);

    juce::String currentFile;
    while (scanner.scanNextFile (true, currentFile))
    {
    }

    deadMansPedal.deleteFile();

    for (const auto& description : knownPlugins.getTypes())
    {
        PluginInfo info;
        info.name = description.name;
        info.manufacturer = description.manufacturerName;
        info.path = description.fileOrIdentifier;
        info.isInstrument = description.isInstrument;
        info.numInputChannels = description.numInputChannels;
        info.numOutputChannels = description.numOutputChannels;
        result.push_back (std::move (info));
    }

    return result;
}
} // namespace sls
