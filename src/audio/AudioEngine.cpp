#include "AudioEngine.h"
#include <cmath>
#include <iostream>

// =============================================================================
// PluginHost implementation
// =============================================================================

PluginHost::PluginHost() = default;

PluginHost::~PluginHost() = default;

bool PluginHost::loadPlugin(const juce::File& pluginPath) {
    // Carica il plugin VST3 usando JUCE
    juce::AudioPluginFormatManager formatManager;
    formatManager.addFormat(new juce::VST3PluginFormat());

    auto desc = formatManager.findBestFormatForFileType(pluginPath.getFileName().toStdString());
    if (desc == nullptr) {
        std::cerr << "[PluginHost] Could not find format for: "
                  << pluginPath.getFullPathName() << "\n";
        return false;
    }

    // Crea l'istanza del plugin
    processor_ = desc->createInstanceFromDescription(*desc, 48000.0, 128);
    if (processor_ == nullptr) {
        std::cerr << "[PluginHost] Failed to create audio processor\n";
        return false;
    }

    // Imposta la configurazione del plugin
    busesLayout_ = processor_->getBusesLayout();
    std::cout << "[PluginHost] Loaded plugin: " << desc->name.toStdString() << "\n";
    return true;
}

bool PluginHost::initialize(int sampleRate, int bufferSize) {
    if (processor_ == nullptr) {
        std::cerr << "[PluginHost] No plugin loaded\n";
        return false;
    }

    // Inizializza il plugin con i parametri audio
    processor_->setPlayConfigDetails(FLStudioClone::kNumChannels,
                                     FLStudioClone::kNumChannels,
                                     sampleRate,
                                     bufferSize);

    processor_->prepareToPlay(sampleRate, bufferSize);

    sampleRate_ = sampleRate;
    bufferSize_ = bufferSize;

    std::cout << "[PluginHost] Initialized plugin with SR="
              << sampleRate << " Hz, buffer=" << bufferSize << " samples\n";
    return true;
}

void PluginHost::processBlock(float* const* inputs,
                              float* const* outputs,
                              size_t numSamples) noexcept {
    if (processor_ == nullptr || !isReady()) {
        // Se il plugin non è pronto, zero i buffer
        for (size_t ch = 0; ch < FLStudioClone::kNumChannels && ch < 16; ++ch) {
            if (outputs[ch] != nullptr) {
                std::memset(outputs[ch], 0, numSamples * sizeof(float));
            }
        }
        return;
    }

    // Processa il blocco di campioni
    juce::AudioBuffer<float> buffer(outputs, FLStudioClone::kNumChannels, static_cast<int>(numSamples));
    processor_->processBlock(juce::AudioBuffer<float>(buffer), {});
}

juce::String PluginHost::getPluginName() const noexcept {
    if (processor_) {
        return processor_->getName();
    }
    return "No Plugin";
}

// =============================================================================
// AudioEngine implementation
//
// Flusso dati nel thread audio:
//   [Oscillator] → generate() → [RingBuffer push()] → [AudioDevice outputs]
//   [PluginHost] → processBlock() → [RingBuffer push()]
//
// Il callback onAudioReady():
//   1. Genera campioni sinusoidali con Oscillator (lookup table, zero cosf)
//   2. Moltiplica per volume master
//   3. Scrive nel RingBuffer (lock-free push)
//   4. Copia dal RingBuffer all'output audio
// =============================================================================

AudioEngine::AudioEngine()
    : oscillator_(), device_() {}

AudioEngine::~AudioEngine() {
    stop();
}

bool AudioEngine::initialize(int sampleRate, int bufferSize) {
    if (running_.load(std::memory_order_relaxed)) {
        std::cerr << "[AudioEngine] Already running\n";
        return false;
    }

    sampleRate_ = sampleRate;

    // Inizializza l'oscillatore con il sample rate corrente
    oscillator_.setFrequency(440.0, static_cast<double>(sampleRate));

    // Configura e avvia il dispositivo audio (JUCE 9 API)
    device_.initialiseWithDefaultDevices(
        [this](float* const* inputs,
               float* const* outputs,
               int numSamples) {
            this->onAudioReady(inputs, outputs, static_cast<size_t>(numSamples));
        });

    std::cout << "[AudioEngine] Initialized at "
              << sampleRate << " Hz, buffer " << bufferSize << " samples\n";
    return true;
}

void AudioEngine::start() {
    if (running_.load(std::memory_order_relaxed)) return;

    device_.setCurrentAudioDevice("default");
    running_.store(true, std::memory_order_release);
    oscillator_.resetPhase();

    std::cout << "[AudioEngine] Started — playing "
              << freq_.load(std::memory_order_relaxed) << " Hz sine wave\n";
}

void AudioEngine::stop() {
    if (!running_.load(std::memory_order_relaxed)) return;

    device_.setCurrentAudioDevice(nullptr);
    running_.store(false, std::memory_order_release);

    std::cout << "[AudioEngine] Stopped\n";
}

void AudioEngine::setFrequency(float frequency) noexcept {
    freq_.store(std::clamp(frequency, 20.0f, 20000.0f),
                std::memory_order_relaxed);

    // Aggiorna l'oscillatore nel thread UI (non real-time)
    oscillator_.setFrequency(static_cast<double>(frequency),
                             static_cast<double>(sampleRate_));
}

void AudioEngine::setMasterVolume(float volume) noexcept {
    vol_.store(std::clamp(volume, 0.0f, 1.0f),
               std::memory_order_relaxed);
}

float AudioEngine::readPeakLevel() const noexcept {
    return peakLevel_.load();
}

bool AudioEngine::loadPlugin(const juce::File& pluginPath) {
    // Carica il plugin nel PluginHost
    if (pluginHost_.loadPlugin(pluginPath)) {
        // Inizializza il plugin con i parametri correnti
        return pluginHost_.initialize(sampleRate_, FLStudioClone::kDefaultBufferSize);
    }
    return false;
}

juce::String AudioEngine::getPluginName() const noexcept {
    return pluginHost_.getPluginName();
}

// =============================================================================
// Callback audio — REAL-TIME SAFE
// Regole:
//   - Niente allocazioni (new/delete/malloc/free)
//   - Niente lock (std::mutex, std::lock_guard)
//   - Niente I/O stdio (cout/cerr/printf)
//   - Niente chiamate di sistema (syscall)
// =============================================================================
void AudioEngine::onAudioReady(float* const* inputs,
                               float* const* outputs,
                               size_t numSamples) noexcept {
    float frequency = freq_.load(std::memory_order_relaxed);
    float volume = vol_.load(std::memory_order_relaxed);

    // Aggiorna l'oscillatore se la frequenza è cambiata (da UI thread)
    oscillator_.setFrequency(static_cast<double>(frequency),
                             static_cast<double>(sampleRate_));

    // Buffer temporaneo per un canale — allocato nello stack, zero heap
    // 2048 samples max = 42ms a 48kHz — sufficiente per qualsiasi buffer JUCE
    alignas(16) float tempBuffer[2048];
    size_t count = std::min(numSamples, sizeof(tempBuffer) / sizeof(float));

    // 1. Genera onda sinusoidale (lookup table, zero cosf)
    oscillator_.generate(tempBuffer, count);

    // 2. Applica volume master
    for (size_t i = 0; i < count; ++i) {
        tempBuffer[i] *= volume;
    }

    // 3. Processa con il plugin VST3 se caricato
    if (pluginHost_.isInitialized()) {
        pluginHost_.processBlock(inputs, outputs, count);
    } else {
        // Se nessun plugin caricato, copia direttamente
        for (size_t ch = 0; ch < FLStudioClone::kNumChannels && ch < 16; ++ch) {
            if (outputs[ch] != nullptr) {
                std::memcpy(outputs[ch], tempBuffer, count * sizeof(float));
            }
        }
    }

    // 4. Calcola picco per VU meter e scrive nel RingBuffer
    float peak = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        if (std::fabs(tempBuffer[i]) > peak) {
            peak = std::fabs(tempBuffer[i]);
        }
    }

    // Aggiorna il livello di picco (AtomicFloat, lock-free)
    peakLevel_.store(peak);

    // Scrive nel RingBuffer per l'UI
    size_t pushed = ringBuffer_.push(tempBuffer, count);

    // Conta campioni persi se il buffer è pieno
    if (pushed < count) {
        dropped_.fetch_add(count - pushed,
                           std::memory_order_relaxed);
    }

    AUDIO_LOG("Audio callback: %zu samples processed\n", count);
}
