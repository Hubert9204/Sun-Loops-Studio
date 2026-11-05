#pragma once

#include <algorithm>
#include <vector>

namespace sls
{
// The audio buffer is a trade-off. A small one gives little delay between pressing a key and hearing it, but the
// computer has little time to prepare each block, so a busy or slow machine drops blocks (heard as crackling). A
// large one is safe but adds delay. These helpers hold the plain logic for choosing; no JUCE, no audio.

constexpr int kDefaultBufferSize = 1024; // samples; about 21 ms at 48 kHz, which is safe on most computers

constexpr double bufferMilliseconds (int bufferSize, double sampleRate) noexcept
{
    return sampleRate > 0.0 ? 1000.0 * bufferSize / sampleRate : 0.0;
}

// What was seen while the program played at one buffer size.
struct BufferTrial
{
    int bufferSize = 0;
    int xruns = 0;           // blocks the audio system reports as dropped during the trial (< 0: it cannot tell)
    double peakLoad = 0.0;   // the longest time one block took to compute, divided by the time the block lasts
};

// A trial is "clean" when nothing was dropped and the worst block used at most half of its time. The other half is
// headroom for the things the program cannot see: other programs, the scheduler, a plugin or a busier song.
constexpr double kMaxCleanLoad = 0.5;

constexpr bool isClean (const BufferTrial& t) noexcept
{
    return t.xruns <= 0 && t.peakLoad <= kMaxCleanLoad;
}

// Finds a buffer size by trying the candidates from the smallest up. It stops at the first size that is clean
// *and* whose next-larger neighbour is clean too, and picks that neighbour: one step of safety margin that was
// itself measured. If only the last candidate is clean it is used; if nothing is clean the largest is used and
// struggling() is true (the computer is too busy even for the biggest buffer offered).
class BufferTuner
{
public:
    explicit BufferTuner (std::vector<int> candidateSizes) : candidates (std::move (candidateSizes))
    {
        std::sort (candidates.begin(), candidates.end());
        candidates.erase (std::unique (candidates.begin(), candidates.end()), candidates.end());
        if (candidates.empty())
            candidates.push_back (kDefaultBufferSize);
    }

    bool finished() const noexcept { return done; }
    int sizeToTry() const noexcept { return candidates[next]; }           // only while !finished()
    int result() const noexcept { return chosen; }                        // only when finished()
    bool struggling() const noexcept { return done && noneClean; }
    const std::vector<BufferTrial>& trials() const noexcept { return history; }
    int candidateCount() const noexcept { return static_cast<int> (candidates.size()); }
    int triedCount() const noexcept { return static_cast<int> (history.size()); }

    // Reports what was seen at sizeToTry().
    void addTrial (int xruns, double peakLoad)
    {
        if (done)
            return;

        const BufferTrial trial { candidates[next], xruns, peakLoad };
        const bool previousClean = ! history.empty() && isClean (history.back());
        history.push_back (trial);
        const bool last = next + 1 >= candidates.size();

        if (isClean (trial) && (previousClean || last))
        {
            chosen = trial.bufferSize;
            done = true;
        }
        else if (last)
        {
            chosen = trial.bufferSize; // the largest offered
            noneClean = ! isClean (trial);
            done = true;
        }
        else
        {
            ++next;
        }
    }

private:
    std::vector<int> candidates;
    std::vector<BufferTrial> history;
    size_t next = 0;
    int chosen = kDefaultBufferSize;
    bool done = false;
    bool noneClean = false;
};
} // namespace sls
