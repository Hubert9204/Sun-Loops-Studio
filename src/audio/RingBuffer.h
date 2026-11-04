#pragma once

#include <atomic>
#include <cstddef>
#include <cstring>
#include <array>
#include <type_traits>

// =============================================================================
// RingBuffer lock-free a singolo produttore (audio callback) e singolo consumatore (UI thread)
// Size è una potenza di 2, quindi usiamo AND invece del modulo.
// Niente allocazioni dopo la costruzione.
// =============================================================================

template <typename T, size_t Capacity>
    requires std::is_trivially_copyable_v<T> && ((Capacity & (Capacity - 1)) == 0)
class RingBuffer {
public:
    static_assert(Capacity >= 2, "Capacity deve essere >= 2");

    RingBuffer() noexcept : writePos_(0), readPos_(0) {}

    // --- Producer side (chiamato dal thread audio — real-time-safe) ---

    /**
     * Scrive fino a 'count' campioni nel buffer.
     * @return numero di campioni effettivamente scritti (potrebbe essere < count se pieno)
     */
    size_t push(const T* data, size_t count) noexcept {
        if (count == 0) return 0;

        const auto currentWrite = writePos_.load(std::memory_order_relaxed);
        const auto currentRead  = readPos_.load(std::memory_order_acquire);

        // Calcola spazio disponibile: capacità - elementi occupati
        size_t occupied = circularDistance(currentRead, currentWrite);
        size_t space    = Capacity - occupied;

        if (space == 0) return 0; // Buffer pieno

        count = std::min(count, space);

        // Scrittura in due blocchi (wrap-around)
        const size_t firstBlock = std::min(count, Capacity - currentWrite);
        const size_t secondBlock = count - firstBlock;

        std::memcpy(buffer_.data() + currentWrite, data, firstBlock * sizeof(T));
        if (secondBlock > 0) {
            std::memcpy(buffer_.data(), data + firstBlock, secondBlock * sizeof(T));
        }

        writePos_.store(currentWrite + count, std::memory_order_release);
        return count;
    }

    // --- Consumer side (chiamato dal thread UI — non real-time) ---

    /**
     * Legge fino a 'count' campioni dal buffer.
     * @return numero di campioni effettivamente letti (potrebbe essere < count se vuoto)
     */
    size_t pop(T* data, size_t count) noexcept {
        if (count == 0) return 0;

        const auto currentRead  = readPos_.load(std::memory_order_relaxed);
        const auto currentWrite = writePos_.load(std::memory_order_acquire);

        // Calcola elementi disponibili
        size_t available = circularDistance(currentRead, currentWrite);

        if (available == 0) return 0; // Buffer vuoto

        count = std::min(count, available);

        // Lettura in due blocchi (wrap-around)
        const size_t firstBlock = std::min(count, Capacity - currentRead);
        const size_t secondBlock = count - firstBlock;

        if (data != nullptr) {
            std::memcpy(data, buffer_.data() + currentRead, firstBlock * sizeof(T));
            if (secondBlock > 0) {
                std::memcpy(data + firstBlock, buffer_.data(), secondBlock * sizeof(T));
            }
        }

        readPos_.store(currentRead + count, std::memory_order_release);
        return count;
    }

    /** @return numero di campioni disponibili per la lettura */
    size_t available() const noexcept {
        const auto read  = readPos_.load(std::memory_order_acquire);
        const auto write = writePos_.load(std::memory_order_acquire);
        return circularDistance(read, write);
    }

    /** @return spazio libero per la scrittura */
    size_t space() const noexcept {
        return Capacity - available();
    }

    /** @return true se il buffer è vuoto */
    bool empty() const noexcept {
        return available() == 0;
    }

    /** @return true se il buffer è pieno */
    bool full() const noexcept {
        return space() == 0;
    }

private:
    static size_t circularDistance(size_t from, size_t to) noexcept {
        // Funziona perché Capacity è potenza di 2 e usiamo modulo implicito
        return (to - from) & (Capacity - 1);
    }

    alignas(64) std::array<T, Capacity> buffer_{}; // Cache-line aligned
    alignas(64) std::atomic<size_t> writePos_{0};
    alignas(64) std::atomic<size_t> readPos_{0};
};