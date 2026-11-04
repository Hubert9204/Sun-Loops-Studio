#include "PluginScanner.h"
#include <juce_audio_processors/format_types/juce_VST3PluginFormat.h>

// =============================================================================
// PluginScanner implementation
// Scansione plugin VST3 con JUCE (non real-time)
// =============================================================================

std::vector<juce::PluginDescription> PluginScanner::scanDirectory(const std::string& path) {
    plugins_.clear();

    // Usa JUCE per scansionare una directory
    juce::File dir(path.c_str());
    if (!dir.isDirectory()) {
        return plugins_;
    }

    juce::AudioPluginFormatManager formatManager;
    auto* vst3Format = formatManager.addFormat(new juce::VST3PluginFormat());
    if (vst3Format == nullptr) {
        return plugins_;
    }

    // Scansione ricorsiva
    juce::StringArray foundFiles;
    dir.findChildFiles(foundFiles, juce::File::findFiles, true);

    for (const auto& file : foundFiles) {
        auto pluginDescs = vst3Format->findAllPluginTypes(file.toStdString());
        for (auto& desc : pluginDescs) {
            plugins_.push_back(desc);
        }
    }

    return plugins_;
}