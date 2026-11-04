#pragma once

#include <atomic>
#include <cmath>
#include <bit>

// =============================================================================
// AtomicFloat: float thread-safe tramite reinterpret_cast a uint32_t atomic.
// Usato per comunicare valori dall'audio callback al thread UI.
// Attenzione: non è un float atomico nativo (C++20 non lo ha), 
// ma garantisce che la lettura sia atominamente consistente.
// =============================================================================

class AtomicFloat {
public:
    AtomicFloat() noexcept = default;
    explicit AtomicFloat(float value) noexcept : bits_(floatToBits(value)) {}

    void store(float value) noexcept {
        bits_.store(floatToBits(value), std::memory_order_release);
    }

    float load() const noexcept {
        return bitsToFloat(bits_.load(std::memory_order_acquire));
    }

    operator float() const noexcept { return load(); }

private:
    static uint32_t floatToBits(float f) noexcept {
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof(bits));
        return bits;
    }

    static float bitsToFloat(uint32_t bits) noexcept {
        float f;
        std::memcpy(&f, &bits, sizeof(f));
        return f;
    }

    std::atomic<uint32_t> bits_{0};
};