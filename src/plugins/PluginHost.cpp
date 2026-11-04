#include "PluginHost.h"
#include "plugins/PluginScanner.h"
#include <juce_audio_processors/format_types/juce_VST3PluginFormat.h>

// =============================================================================
// PluginHost implementation
// =============================================================================

PluginHost::PluginHost() {
    // Inizializza il format manager
    formatManager_.addFormat(new juce::VST3PluginFormat());
}

PluginHost::~PluginHost() = default;

bool PluginHost::scanAndLoadPlugins(const std::string& path) {
    PluginScanner scanner;
    auto pluginList = scanner.scanDirectory(path);

    for (const auto& desc : pluginList) {
        // Per ora, carichiamo il plugin con il file path del plugin stesso
        std::unique_ptr<PluginProcessor> processor = std::make_unique<PluginProcessor>();
        if (processor->loadPlugin(desc.fileOrIdentifier.toStdString())) {
            plugins_.push_back(std::move(processor));
        }
    }

    return !plugins_.empty();
}

bool PluginHost::loadPlugin(const std::string& path) {
    std::unique_ptr<PluginProcessor> processor = std::make_unique<PluginProcessor>();
    if (processor->loadPlugin(path)) {
        plugins_.push_back(std::move(processor));
        return true;
    }
    return false;
}

void PluginHost::process(float* const* inputs,
                         float* const* outputs,
                         size_t numSamples) noexcept {
    if (activePlugin_ == nullptr) return;

    activePlugin_->process(inputs, outputs, numSamples);
}

void PluginHost::setActivePlugin(int index) {
    if (index < 0 || index >= static_cast<int>(plugins_.size())) {
        activePlugin_ = nullptr;
        return;
    }

    activePlugin_ = plugins_[index].get();
}