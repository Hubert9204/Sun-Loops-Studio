#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>
#include <string>
#include "plugins/PluginProcessor.h"

// =============================================================================
// PluginHost: gestore principale per plugin VST3.
// Carica, inizializza e processa plugin all'interno del motore audio.
// =============================================================================

class PluginHost {
public:
    PluginHost();
    ~PluginHost();

    // Non copiabile
    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;

    /** Scansiona e carica plugin da una directory */
    bool scanAndLoadPlugins(const std::string& path);

    /** Carica un plugin specifico (per test) */
    bool loadPlugin(const std::string& path);

    /** @return lista dei plugin caricati */
    const std::vector<std::unique_ptr<PluginProcessor>>& getPlugins() const noexcept { return plugins_; }

    /** Processa un blocco di campioni tramite tutti i plugin attivi */
    void process(float* const* inputs,
                 float* const* outputs,
                 size_t numSamples) noexcept;

    /** Imposta il plugin da usare (0 = nessuno, 1..N = plugin) */
    void setActivePlugin(int index);

    /** @return plugin attivo o nullptr se nessuno */
    PluginProcessor* getActivePlugin() const noexcept { return activePlugin_; }

private:
    std::vector<std::unique_ptr<PluginProcessor>> plugins_;
    PluginProcessor* activePlugin_ = nullptr;
    juce::AudioPluginFormatManager formatManager_;
};