#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace sls
{
// A note played by hand (a click on the piano roll's keyboard, or the note being drawn): it starts when the
// button goes down and stops when it comes up, independently of the transport and of the notes in the pattern.
struct LiveNoteEvent
{
    bool on = false;
    std::uint8_t channel = 0;
    std::uint8_t pitch = 60;
    float velocity = 0.8f;
    bool fromMidi = false; // came from a MIDI keyboard (and so can be recorded into the piano roll), not from the mouse
};

// Hand-over of live note events from the UI thread (one producer) to the audio thread (one consumer).
// Fixed capacity, wait-free, never allocates. When it is full push() refuses the event; the Player also ends any
// held note on its own after kMaxLiveSeconds, so a lost "off" can never leave a note sounding for ever.
class LiveNoteQueue
{
public:
    static constexpr std::uint32_t kCapacity = 64; // power of two

    bool push (const LiveNoteEvent& e) noexcept
    {
        const auto w = writeIndex.load (std::memory_order_relaxed);
        const auto r = readIndex.load (std::memory_order_acquire);
        if (w - r >= kCapacity)
            return false;
        slots[w % kCapacity] = e;
        writeIndex.store (w + 1, std::memory_order_release);
        return true;
    }

    bool pop (LiveNoteEvent& e) noexcept
    {
        const auto r = readIndex.load (std::memory_order_relaxed);
        const auto w = writeIndex.load (std::memory_order_acquire);
        if (r == w)
            return false;
        e = slots[r % kCapacity];
        readIndex.store (r + 1, std::memory_order_release);
        return true;
    }

private:
    static_assert ((kCapacity & (kCapacity - 1)) == 0, "capacity must be a power of two");

    std::array<LiveNoteEvent, kCapacity> slots {};
    std::atomic<std::uint32_t> writeIndex { 0 };
    std::atomic<std::uint32_t> readIndex { 0 };
};
} // namespace sls
