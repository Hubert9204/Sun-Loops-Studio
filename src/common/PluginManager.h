#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>
#include <string>

// =============================================================================
// PluginManager: gestore globale dei plugin VST3.
// Carica, scannerizza e fornisce accesso ai plugin disponibili.
// =============================================================================

class PluginManager {
public:
    PluginManager();
    ~PluginManager();

    // Non copiabile
    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    /**
     * Scannerizza i plugin VST3 nella directory specificata.
     * @param path Path alla directory dei plugin (es. /usr/lib/vst3)
     * @return true se scannerizzazione riuscita
     */
    bool scanPlugins(const std::string& path);

    /**
     * Carica un plugin specifico per uso in un canale mixer.
     * @param pluginDesc Descrizione del plugin (ottenuta dal scanner)
     * @return puntatore al processor VST3 o nullptr se fallito
     */
    juce::AudioProcessor* loadPlugin(const juce::PluginDescription& pluginDesc);

    /** @return lista dei plugin trovati */
    const std::vector<juce::PluginDescription>& getPlugins() const noexcept {
        return plugins_;
    }

    /**
     * Libera un plugin caricato (chiamare dopo use).
     * @param processor Puntatore al processor VST3
     */
    void releasePlugin(juce::AudioProcessor* processor);

private:
    std::vector<juce::PluginDescription> plugins_;

    // Scansione in background per non bloccare UI
    juce::ThreadPool threadPool_;
};
