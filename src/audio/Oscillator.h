#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#include "common/Constants.h"

// =============================================================================
// Oscillatore sinusoidale real-time-safe
// Niente allocazioni dopo la costruzione.
// Usa lookup table per evitare cosf() nel thread audio.
// =============================================================================

class Oscillator {
public:
    Oscillator() noexcept = default;

    /** Imposta la frequenza in Hz. Deve essere > 0 e <= sampleRate/2 */
    void setFrequency(double frequency, double sampleRate) noexcept;

    /**
     * Genera 'count' campioni nell'output buffer.
     * Real-time-safe: zero allocazioni, zero lock.
     */
    void generate(float* output, size_t count) noexcept;

    /** Resetta la fase a zero */
    void resetPhase() noexcept;

    /** @return frequenza corrente in Hz */
    double getFrequency() const noexcept { return frequency_; }

private:
    static constexpr size_t LUT_SIZE = 4096;
    static_assert((LUT_SIZE & (LUT_SIZE - 1)) == 0, "LUT_SIZE deve essere potenza di 2");

    void buildLookupTable(double sampleRate) noexcept {
        lut_.resize(LUT_SIZE);
        lutMask_ = LUT_SIZE - 1;

        for (size_t i = 0; i < LUT_SIZE; ++i) {
            // Genera mezz'onda seno e specularizza per evitare cos()
            double angle = static_cast<double>(i) / static_cast<double>(LUT_SIZE) * 2.0 * M_PI;
            lut_[i] = static_cast<float>(std::sin(angle));
        }
    }

    double frequency_       = 440.0;   // Hz
    double phaseIncrement_  = 0.0;
    double phase_           = 0.0;
    std::vector<float> lut_;
    size_t lutMask_         = LUT_SIZE - 1;
};