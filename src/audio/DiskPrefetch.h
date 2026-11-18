#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include <juce_core/juce_core.h>

#include "audio/Arrangement.h"
#include "audio/TempoMap.h"

namespace sls
{
// 0.81.0: long audio files are read from the audio cache (see AudioImport), mapped into memory: the operating system reads their
// pages from the disk when they are first touched. So that the audio thread never waits for the disk, this thread touches, a few
// times a second, the audio the playlist's mapped clips will play in the next seconds from where the song is (as FL Studio's
// "keep on disk" streams ahead). It works on copies (setClips on the message thread), never on the audio thread's data.
class DiskPrefetch : private juce::Thread
{
public:
    struct Clip
    {
        std::shared_ptr<const SampleData> data;
        int startTick = 0, endTick = 0;
        double offsetSeconds = 0.0;
        bool reverse = false;
    };

    DiskPrefetch();
    ~DiskPrefetch() override;

    // The mapped clips of the arrangement (others need no help) and the tempo; starts the thread when there are any.
    void setArrangement (const Arrangement& arrangement, double bpm);
    // Where the song is (ticks); any thread.
    void setPosition (double tick) noexcept { position.store (tick, std::memory_order_relaxed); }

    static constexpr double kAheadSeconds = 3.0;
    static constexpr double kBehindSeconds = 0.25;

    // For the tests: touches what the clips play between `fromTick` and kAheadSeconds after it, now, on the calling thread;
    // returns how many pages it touched.
    static int touchAhead (const std::vector<Clip>& clips, const std::vector<TempoMarker>& tempos, double bpm, double fromTick);

private:
    void run() override;

    std::mutex lock;
    std::vector<Clip> clips;
    std::vector<TempoMarker> tempos;
    double bpm = 120.0;
    std::atomic<double> position { 0.0 };
};
} // namespace sls
