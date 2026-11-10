#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace sls
{
// Hand-over of small plain values from one thread (the single producer) to another (the single consumer): fixed
// capacity, wait-free, never allocates. push() refuses the value when the queue is full.
template <typename T, std::uint32_t Capacity>
class SpscQueue
{
public:
    static_assert ((Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");

    bool push (const T& value) noexcept
    {
        const auto w = writeIndex.load (std::memory_order_relaxed);
        const auto r = readIndex.load (std::memory_order_acquire);
        if (w - r >= Capacity)
            return false;
        slots[w % Capacity] = value;
        writeIndex.store (w + 1, std::memory_order_release);
        return true;
    }

    bool pop (T& value) noexcept
    {
        const auto r = readIndex.load (std::memory_order_relaxed);
        const auto w = writeIndex.load (std::memory_order_acquire);
        if (r == w)
            return false;
        value = slots[r % Capacity];
        readIndex.store (r + 1, std::memory_order_release);
        return true;
    }

    // How many values wait (exact for the producer or the consumer; a snapshot for anyone else).
    std::uint32_t size() const noexcept
    {
        return writeIndex.load (std::memory_order_acquire) - readIndex.load (std::memory_order_acquire);
    }

private:
    std::array<T, Capacity> slots {};
    std::atomic<std::uint32_t> writeIndex { 0 };
    std::atomic<std::uint32_t> readIndex { 0 };
};
} // namespace sls
