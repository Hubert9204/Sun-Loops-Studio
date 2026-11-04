#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include <string>

// =============================================================================
// PluginScanner: cerca plugin VST3 nella directory specificata.
// Usa JUCE per analisi file (non real-time).
// =============================================================================

class PluginScanner {
public:
    using PluginDescription = juce::PluginDescription;

    /** Scansione ricorsiva di una directory */
    std::vector<PluginDescription> scanDirectory(const std::string& path);

    /** @return lista dei plugin VST3 trovati */
    const std::vector<PluginDescription>& getPlugins() const noexcept { return plugins_; }

private:
    std::vector<PluginDescription> plugins_;
};