#include "audio/DiskPrefetch.h"

#include <algorithm>
#include <cmath>

namespace sls
{
DiskPrefetch::DiskPrefetch() : juce::Thread ("Disk prefetch") {}

DiskPrefetch::~DiskPrefetch()
{
    stopThread (2000);
}

void DiskPrefetch::setArrangement (const Arrangement& arrangement, double newBpm)
{
    std::vector<Clip> mapped;
    for (const auto& a : arrangement.audioClips)
        if (a.data != nullptr && a.data->isMapped() && ! a.muted)
            mapped.push_back ({ a.data, a.startTick, a.endTick(), a.offsetSeconds, a.reverse });
    {
        const std::scoped_lock guard (lock);
        clips = std::move (mapped);
        tempos = arrangement.tempoMarkers;
        bpm = newBpm;
    }
    if (! isThreadRunning())
    {
        const std::scoped_lock guard (lock);
        if (! clips.empty())
            startThread (juce::Thread::Priority::normal);
    }
}

int DiskPrefetch::touchAhead (const std::vector<Clip>& list, const std::vector<TempoMarker>& tempoMarkers, double baseBpm, double fromTick)
{
    const double now = tickToSeconds (tempoMarkers, baseBpm, std::max (0.0, fromTick));
    const double from = std::max (0.0, now - kBehindSeconds), to = now + kAheadSeconds;
    constexpr size_t kPageFloats = 1024; // 4 KB
    volatile float sink = 0.0f;
    int pages = 0;
    for (const auto& c : list)
    {
        if (c.data == nullptr)
            continue;
        const double start = tickToSeconds (tempoMarkers, baseBpm, c.startTick), end = tickToSeconds (tempoMarkers, baseBpm, c.endTick);
        const double a = std::max (from, start), b = std::min (to, end);
        if (b <= a)
            continue;
        const double rate = c.data->sampleRate;
        const auto frames = static_cast<double> (c.data->numFrames());
        double f0 = (c.offsetSeconds + (a - start)) * rate, f1 = (c.offsetSeconds + (b - start)) * rate;
        if (c.reverse) // (the offset counts from the end of the audio)
        {
            const double r0 = frames - 1.0 - f1, r1 = frames - 1.0 - f0;
            f0 = r0;
            f1 = r1;
        }
        const auto first = static_cast<size_t> (std::clamp (f0, 0.0, frames));
        const auto last = static_cast<size_t> (std::clamp (f1 + 1.0, 0.0, frames));
        for (const auto* channel : { &c.data->left, &c.data->right })
        {
            const float* x = channel->data();
            if (x == nullptr)
                continue;
            for (size_t i = first - first % kPageFloats; i < last; i += kPageFloats)
            {
                sink = sink + x[i];
                ++pages;
            }
        }
    }
    juce::ignoreUnused (sink);
    return pages;
}

void DiskPrefetch::run()
{
    while (! threadShouldExit())
    {
        std::vector<Clip> list;
        std::vector<TempoMarker> tempoMarkers;
        double baseBpm = 120.0;
        {
            const std::scoped_lock guard (lock);
            list = clips;
            tempoMarkers = tempos;
            baseBpm = bpm;
        }
        if (! list.empty())
            touchAhead (list, tempoMarkers, baseBpm, position.load (std::memory_order_relaxed));
        wait (100); // ten times a second: 3 s ahead leaves plenty of time for a slow disk
    }
}
} // namespace sls
