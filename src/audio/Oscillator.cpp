#include "Oscillator.h"

// =============================================================================
// Oscillator implementation
// Lookup table approach: pre-calcola 4096 punti del seno una volta,
// nel thread audio usa solo accesso array + addizione.
// Zero chiamate a cosf()/sinf() nel callback real-time.
// =============================================================================

Oscillator::Oscillator() {
    // Default: A4 = 440 Hz, sample rate standard
    setFrequency(440.0, static_cast<double>(FLStudioClone::kDefaultSampleRate));
}

void Oscillator::setFrequency(double frequency, double /*sampleRate*/) noexcept {
    frequency_ = std::clamp(frequency, 20.0, 24000.0);
    phaseIncrement_ = frequency_ / FLStudioClone::kDefaultSampleRate;
    buildLookupTable(FLStudioClone::kDefaultSampleRate);
}

void Oscillator::generate(float* output, size_t count) noexcept {
    if (count == 0 || output == nullptr) return;

    for (size_t i = 0; i < count; ++i) {
        // Maschera per wrap-around efficiente (potenza di 2)
        size_t idx = static_cast<size_t>(phase_) & lutMask_;
        output[i] = lut_[idx];

        phase_ += phaseIncrement_;
        if (phase_ >= 1.0) {
            phase_ -= 1.0;
        }
    }
}

void Oscillator::resetPhase() noexcept {
    phase_ = 0.0;
}
