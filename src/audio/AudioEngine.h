#pragma once

#include <atomic>
#include <memory>
#include <vector>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Oscillator.h"
#include "RingBuffer.h"
#include "common/AtomicFloat.h"
#include "common/Constants.h"

// =============================================================================
// PluginHost: gestore plugin VST3 (base).
// Carica e configura un plugin VST3 per l'uso in una DAW.
// Usa JUCE AudioProcessor wrapper per compatibilità cross-platform.
// =============================================================================

class PluginHost {
public:
    PluginHost();
    ~PluginHost();

    // Non copiabile
    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;

    /**
     * Carica un plugin VST3 da file.
     * @param pluginPath Percorso al file .vst3
     * @return true se caricato correttamente
     */
    bool loadPlugin(const juce::File& pluginPath);

    /**
     * Avvia il plugin (se disponibile)
     * @param sampleRate Frequenza campionamento
     * @param bufferSize Dimensione buffer
     * @return true se avviato correttamente
     */
    bool initialize(int sampleRate, int bufferSize);

    /**
     * Processa un blocco di campioni.
     * @param inputs Buffer input (array di puntatori)
     * @param outputs Buffer output (array di puntatori)
     * @param numSamples Numero di campioni
     */
    void processBlock(float* const* inputs,
                      float* const* outputs,
                      size_t numSamples) noexcept;

    /**
     * @return true se il plugin è stato caricato e inizializzato
     */
    bool isInitialized() const noexcept { return processor_ != nullptr; }

    /**
     * @return nome del plugin caricato
     */
    juce::String getPluginName() const noexcept;

private:
    std::unique_ptr<juce::AudioProcessor> processor_;
    std::unique_ptr<juce::AudioProcessorEditor> editor_;
    std::unique_ptr<juce::AudioProcessorGraph::Node> node_;
    juce::AudioProcessor::BusesLayout busesLayout_;
    int sampleRate_ = 0;
    int bufferSize_ = 0;
};

// =============================================================================
// AudioEngine: coordinatore centrale del motore audio.
// Gestisce:
//   - Inizializzazione e lifecycle del dispositivo audio
//   - Generazione del segnale (oscillatore → buffer circolare → output)
//   - Esportazione del livello per il VU meter UI
//   - Plugin host VST3 base
//
// Thread safety:
//   - Audio thread: generateAudio() chiamato dal callback JUCE
//   - UI thread: readPeakLevel(), setFrequency()
// =============================================================================

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    // Non copiabile
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    /**
     * Inizializza il motore audio.
     * @param sampleRate Hz (default 48000)
     * @param bufferSize samples (default 128)
     * @return true se tutto OK
     */
    bool initialize(int sampleRate = FLStudioClone::kDefaultSampleRate,
                    int bufferSize = FLStudioClone::kDefaultBufferSize);

    /** Avvia la riproduzione */
    void start();

    /** Ferma la riproduzione */
    void stop();

    /** @return true se in esecuzione */
    bool isRunning() const noexcept { return running_.load(std::memory_order_relaxed); }

    // --- Controllo oscillatore (chiamabile da UI thread) ---

    /** Imposta frequenza oscillatore (thread-safe via atomic) */
    void setFrequency(float frequency) noexcept;

    /** @return frequenza corrente */
    float getFrequency() const noexcept { return freq_.load(std::memory_order_relaxed); }

    /** Imposta volume master (0.0 = mute, 1.0 = max) */
    void setMasterVolume(float volume) noexcept;

    /** @return volume master corrente */
    float getMasterVolume() const noexcept { return vol_.load(std::memory_order_relaxed); }

    // --- Plugin VST3 ---

    /** Carica un plugin VST3 da file */
    bool loadPlugin(const juce::File& pluginPath);

    /** @return true se il plugin è caricato */
    bool hasPlugin() const noexcept { return pluginHost_.isInitialized(); }

    /** @return nome del plugin caricato */
    juce::String getPluginName() const noexcept;

    // --- Dati per UI (VU meter) ---

    /**
     * Legge il livello di picco del buffer circolare.
     * Calcolato nel thread audio, esportato come AtomicFloat.
     */
    float readPeakLevel() const noexcept;

    /** @return numero di campioni persi (buffer overflow) */
    size_t getDroppedSamples() const noexcept { return dropped_.load(std::memory_order_relaxed); }

private:
    // Callback chiamato dal thread audio JUCE (real-time-safe)
    void onAudioReady(float* const* inputs,
                      float* const* outputs,
                      size_t numSamples) noexcept;

    // Oscillatore: allocato una volta, mai deallocato nel callback
    Oscillator oscillator_;

    // Plugin host VST3
    PluginHost pluginHost_;

    // Dispositivo audio
    juce::AudioDeviceManager device_;

    // Buffer circolare: audio thread scrive, UI thread legge
    RingBuffer<float, FLStudioClone::kRingBufferCapacity> ringBuffer_;

    // Valori thread-safe per UI
    std::atomic<float> freq_{440.0f};
    std::atomic<float> vol_{0.5f};
    AtomicFloat peakLevel_{0.0f};
    std::atomic<size_t> dropped_{0};

    // Stato esecuzione
    std::atomic<bool> running_{false};

    // Sample rate corrente (letto da device)
    int sampleRate_ = 0;
};