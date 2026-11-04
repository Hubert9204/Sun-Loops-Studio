#include "PluginProcessor.h"
#include <juce_audio_processors/format_types/juce_VST3PluginFormat.h>

// =============================================================================
// PluginProcessor implementation
// Usa JUCE AudioPluginInstance per esecuzione plugin VST3
// =============================================================================

PluginProcessor::PluginProcessor() = default;

PluginProcessor::~PluginProcessor() {
    stop();
}

bool PluginProcessor::loadPlugin(const std::string& path) {
    juce::AudioPluginFormatManager formatManager;
    auto* vst3Format = formatManager.addFormat(new juce::VST3PluginFormat());
    if (vst3Format == nullptr) {
        return false;
    }

    // Carica il plugin dal file
    juce::File pluginFile(path.c_str());
    if (!pluginFile.existsAsFile()) {
        return false;
    }

    auto instances = vst3Format->createInstancesFromFile(pluginFile);
    if (instances.isEmpty()) {
        return false;
    }

    pluginInstance_ = std::move(instances[0]);
    numInputs_  = pluginInstance_->getTotalNumInputChannels();
    numOutputs_ = pluginInstance_->getTotalNumOutputChannels();

    return true;
}

bool PluginProcessor::initialize(int sampleRate, int bufferSize) {
    if (!pluginInstance_) return false;

    // Inizializza il plugin
    pluginInstance_->prepareToPlay(static_cast<double>(sampleRate), static_cast<int>(bufferSize));
    isInitialized_ = true;

    return true;
}

void PluginProcessor::start() {
    if (!isInitialized_) return;
}

void PluginProcessor::stop() {
    if (pluginInstance_) {
        pluginInstance_->releaseResources();
    }
    isInitialized_ = false;
}

void PluginProcessor::process(float* const* inputs,
                              float* const* outputs,
                              size_t numSamples) noexcept {
    if (!isInitialized_ || !pluginInstance_) return;

    // Processa i dati direttamente con il plugin instance
    juce::AudioSampleBuffer buffer(outputs, numOutputs_, static_cast<int>(numSamples));
    pluginInstance_->processBlock(inputs, outputs, static_cast<int>(numSamples));
}

std::string PluginProcessor::getName() const {
    if (pluginInstance_) {
        return pluginInstance_->getName().toStdString();
    }
    return "Unknown Plugin";
}