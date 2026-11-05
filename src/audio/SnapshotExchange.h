#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace sls
{
// Hands immutable snapshots of type T from the message thread to the audio thread
// without locks or allocation on the audio side. T must have a `std::uint64_t revision`.
//
//  - publish():  message thread only. Allocation and freeing happen here.
//  - acquire():  audio thread only. Wait-free in practice, never allocates or frees.
//
// Safety uses a single hazard pointer: the audio thread announces which snapshot it
// is reading, and the message thread never frees that one.
//
// Anything a snapshot owns (for example sample data) may be freed as soon as the audio
// thread has moved on to a newer snapshot, so the audio thread must not keep pointers
// into an older snapshot across blocks unless the newer one still owns the same object.
template <typename T>
class SnapshotExchange
{
public:
    SnapshotExchange() { publish (std::make_unique<T>()); }

    void publish (std::unique_ptr<T> next)
    {
        next->revision = ++revisionCounter;
        const T* raw = next.get();
        owned.push_back (std::move (next));

        current.store (raw, std::memory_order_seq_cst);

        const T* inUse = hazard.load (std::memory_order_seq_cst);
        owned.erase (std::remove_if (owned.begin(), owned.end(),
                                     [raw, inUse] (const std::unique_ptr<T>& p)
                                     { return p.get() != raw && p.get() != inUse; }),
                     owned.end());
    }

    const T* acquire() noexcept
    {
        const T* p;
        do
        {
            p = current.load (std::memory_order_seq_cst);
            hazard.store (p, std::memory_order_seq_cst);
        } while (p != current.load (std::memory_order_seq_cst));
        return p;
    }

private:
    std::atomic<const T*> current { nullptr };
    std::atomic<const T*> hazard { nullptr };
    std::vector<std::unique_ptr<T>> owned; // message thread only
    std::uint64_t revisionCounter = 0;
};
} // namespace sls
