#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

// =============================================================================
// PluginProcessor: wrapper per un plugin VST3 in esecuzione.
// Usa JUCE AudioPluginInstance e AudioProcessorPlayer per gestire il plugin.
// =============================================================================

class PluginProcessor {
public:
    PluginProcessor();
    ~PluginProcessor();

    // Non copiabile
    PluginProcessor(const PluginProcessor&) = delete;
    PluginProcessor& operator=(const PluginProcessor&) = delete;

    /** Carica un plugin VST3 da file */
    bool loadPlugin(const std::string& path);

    /** @return true se il plugin è caricato e pronto */
    bool isLoaded() const noexcept { return pluginInstance_ != nullptr; }

    /** Inizializza il plugin per l'uso audio */
    bool initialize(int sampleRate, int bufferSize);

    /** Avvia la pipeline del plugin (se richiesto) */
    void start();

    /** Ferma la pipeline del plugin */
    void stop();

    /** Processa un blocco di campioni */
    void process(float* const* inputs,
                 float* const* outputs,
                 size_t numSamples) noexcept;

    /** @return nome del plugin */
    std::string getName() const;

    /** @return numero di canali in ingresso */
    int getNumInputChannels() const noexcept { return numInputs_; }

    /** @return numero di canali in uscita */
    int getNumOutputChannels() const noexcept { return numOutputs_; }

private:
    std::unique_ptr<juce::AudioPluginInstance> pluginInstance_;

    int numInputs_ = 0;
    int numOutputs_ = 0;
    bool isInitialized_ = false;
};