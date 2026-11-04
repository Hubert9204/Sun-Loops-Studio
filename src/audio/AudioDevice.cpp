#include "AudioDevice.h"
#include <iostream>
#include <sstream>

// =============================================================================
// AudioDevice implementation
// Su Nobara Linux 44 (Fedora-based), JUCE usa automaticamente PipeWire
// come backend audio quando disponibile.
// =============================================================================

AudioDevice::AudioDevice()
    : deviceManager_(nullptr, nullptr, nullptr, false) {

    // Registra i tipi di dispositivo supportati
    // Order matters: preferisce PipeWire/Pulse su Fedora, poi JACK
    deviceManager_.createDefaultDevices();
}

AudioDevice::~AudioDevice() {
    stop();
}

bool AudioDevice::initialize(AudioCallback* callback,
                             int sampleRate,
                             int bufferSize) {
    if (callback == nullptr) {
        std::cerr << "[AudioDevice] Callback non fornito\n";
        return false;
    }

    callback_ = callback;
    currentSampleRate_ = sampleRate;
    currentBufferSize_ = bufferSize;

    // Configura le impostazioni audio
    auto* settings = deviceManager_.getAudioSettings();
    settings->setOption(juce::AudioDeviceManager::optionPreferredSampleRate,
                        static_cast<int>(sampleRate));
    settings->setOption(juce::AudioDeviceManager::optionPreferredBufferSize,
                        static_cast<int>(bufferSize));

    // Se JACK è abilitato, prova a usarlo
#ifdef ENABLE_JACK_BACKEND
    if (settings->getJackClientName().isEmpty()) {
        settings->setConnectedProcessors(true);
    }
#endif

    std::cout << "[AudioDevice] Device initialized:\n"
              << "  Sample Rate: " << sampleRate << " Hz\n"
              << "  Buffer Size: " << bufferSize << " samples\n"
              << "  Latency:     "
              << (bufferSize * 1000.0 / sampleRate)
              << " ms\n";

    return true;
}

void AudioDevice::start() {
    if (isStarted_) return;

    // Usa l'output stereo come default
    auto audioOutputs = deviceManager_.getCurrentAudioDevice()
                            ->getSupportedSampleRates();

    std::cout << "[AudioDevice] Starting audio output (stereo)...\n";

    transport_ = deviceManager_.createAndStartAudioTransportComponent(
        [this](float* const* inputs,
               float* const* outputs,
               int numSamples) {
            if (callback_) {
                callback_(inputs, outputs, static_cast<size_t>(numSamples));
            }
        });

    isStarted_ = true;
}

void AudioDevice::stop() {
    if (!isStarted_) return;

    std::cout << "[AudioDevice] Stopping audio output...\n";
    deviceManager_.setCurrentAudioDevice(nullptr);
    transport_.reset();
    isStarted_ = false;
}

bool AudioDevice::setBufferSize(int bufferSize) {
    if (bufferSize < FLStudioClone::kMinBufferSize ||
        bufferSize > FLStudioClone::kMaxBufferSize) {
        std::cerr << "[AudioDevice] Buffer size out of range: "
                  << bufferSize << "\n";
        return false;
    }

    bool wasStarted = isStarted_;
    if (wasStarted) stop();

    currentBufferSize_ = bufferSize;
    auto* settings = deviceManager_.getAudioSettings();
    settings->setOption(juce::AudioDeviceManager::optionPreferredBufferSize,
                        static_cast<int>(bufferSize));

    if (wasStarted) {
        start();
    }

    std::cout << "[AudioDevice] Buffer size changed to: "
              << bufferSize << " samples\n";
    return true;
}

std::string AudioDevice::listDevices() const {
    std::ostringstream oss;
    auto* device = deviceManager_.getCurrentAudioDevice();
    if (device) {
        oss << "Active: " << device->getName() << "\n"
            << "  Sample rates: ";
        auto rates = device->getSupportedSampleRates();
        for (size_t i = 0; i < rates.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << rates[i];
        }
        oss << "\n";
    } else {
        oss << "No active audio device\n";
    }
    return oss.str();
}

void AudioDevice::audioCallbackWrapper(float* const* inputs,
                                       float* const* outputs,
                                       int numSamples) {
    // Questo è statico ma viene chiamato dal transport component
    // Il callback reale è nel lambda passato a createAndStartAudioTransportComponent
    (void)inputs;
    (void)outputs;
    (void)numSamples;
}