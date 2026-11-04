#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>
#include <string>
#include "common/Constants.h"

// =============================================================================
// AudioDevice: astrazione del dispositivo audio backend.
// Usa JUCE AudioDeviceManager che su Nobara/Fedora usa PipeWire come default.
// Supporta anche JACK se abilitato con -DENABLE_JACK=ON.
// =============================================================================

class AudioDevice {
public:
    using AudioCallback = void(float* const* inputs,
                               float* const* outputs,
                               size_t numSamples);

    AudioDevice();
    ~AudioDevice();

    // Non copiabile (JUCE non lo è)
    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    /**
     * Inizializza il dispositivo audio.
     * @param callback Funzione chiamata dal thread audio (real-time-safe)
     * @param sampleRate Target sample rate (default 48000 Hz)
     * @param bufferSize Target buffer size in samples (default 128)
     * @return true se inizializzazione riuscita
     */
    bool initialize(AudioCallback* callback,
                    int sampleRate = FLStudioClone::kDefaultSampleRate,
                    int bufferSize = FLStudioClone::kDefaultBufferSize);

    /** Avvia il flusso audio */
    void start();

    /** Ferma il flusso audio */
    void stop();

    /** @return true se il dispositivo è attivo */
    bool isActive() const noexcept { return isStarted_; }

    /** @return sample rate corrente */
    int getSampleRate() const noexcept { return currentSampleRate_; }

    /** @return buffer size corrente in samples */
    int getBufferSize() const noexcept { return currentBufferSize_; }

    /** Imposta il buffer size (richiede riavvio) */
    bool setBufferSize(int bufferSize);

    /** @return lista dei dispositivi audio disponibili */
    std::string listDevices() const;

private:
    juce::AudioDeviceManager deviceManager_;
    std::unique_ptr<juce::AudioIODeviceTransport> transport_;
    AudioCallback* callback_ = nullptr;

    bool isStarted_ = false;
    int currentSampleRate_ = 0;
    int currentBufferSize_ = 0;

    // Callback wrapper JUCE -> nostro callback
    static void audioCallbackWrapper(float* const* inputs,
                                     float* const* outputs,
                                     int numSamples);
};