#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <thread>

#include "audio/Project.h"

namespace sls
{
// Plays a stretch of one sample straight into the output, for the sample editor's preview: from any frame, optionally in a
// loop, at the sample's real speed on any device rate (linear interpolation). Message thread: play() / stop() / position();
// audio thread: mixInto(), which never allocates or locks. The sample is held by this object; stop() waits until the audio
// thread has left mixInto(), so the sample can then be replaced safely (the same hand-over the recorder uses).
class Auditioner
{
public:
    // Starts playing frames [beginFrame, endFrame) of `sound` (endFrame <= 0 means "to the end").
    void play (std::shared_ptr<const SampleData> sound, double beginFrame, double endFrame, bool loopIt)
    {
        stop();
        if (sound == nullptr || sound->numFrames() < 2)
            return;
        held = std::move (sound);
        const double n = held->numFrames();
        begin = std::clamp (std::isfinite (beginFrame) ? beginFrame : 0.0, 0.0, n - 1.0);
        end = endFrame > begin + 1.0 ? std::min (endFrame, n) : n;
        loop = loopIt;
        position.store (begin, std::memory_order_relaxed);
        playing.store (true, std::memory_order_release);
    }

    void stop() noexcept
    {
        playing.store (false, std::memory_order_seq_cst);
        while (inMix.load (std::memory_order_seq_cst))
            std::this_thread::yield();
    }

    // Stops and lets go of the sample (message thread).
    void release() noexcept
    {
        stop();
        held.reset();
    }

    bool isPlaying() const noexcept { return playing.load (std::memory_order_acquire); }
    double positionFrames() const noexcept { return position.load (std::memory_order_relaxed); }

    // Audio thread: adds the next `numSamples` to `out` (even channels get the left side, odd the right).
    void mixInto (float* const* out, int numChannels, int numSamples, double deviceRate) noexcept
    {
        if (! playing.load (std::memory_order_acquire) || deviceRate <= 0.0)
            return;
        inMix.store (true, std::memory_order_seq_cst);
        if (! playing.load (std::memory_order_seq_cst))
        {
            inMix.store (false, std::memory_order_seq_cst);
            return;
        }

        const SampleData& s = *held;
        const int n = s.numFrames();
        const double step = s.sampleRate / deviceRate;
        double pos = position.load (std::memory_order_relaxed);
        bool finished = false;
        for (int i = 0; i < numSamples; ++i, pos += step)
        {
            if (pos >= end)
            {
                if (! loop || end - begin < 2.0)
                {
                    finished = true;
                    break;
                }
                pos = begin + std::fmod (pos - begin, end - begin);
            }
            const int i0 = static_cast<int> (pos);
            const float frac = static_cast<float> (pos - i0);
            const int i1 = std::min (i0 + 1, n - 1);
            const float l = s.left[static_cast<size_t> (i0)] + (s.left[static_cast<size_t> (i1)] - s.left[static_cast<size_t> (i0)]) * frac;
            const float r = s.right[static_cast<size_t> (i0)] + (s.right[static_cast<size_t> (i1)] - s.right[static_cast<size_t> (i0)]) * frac;
            for (int ch = 0; ch < numChannels; ++ch)
                if (out[ch] != nullptr)
                    out[ch][i] += (ch % 2 == 0) ? l : r;
        }
        position.store (pos, std::memory_order_relaxed);
        if (finished)
            playing.store (false, std::memory_order_release);
        inMix.store (false, std::memory_order_seq_cst);
    }

private:
    std::shared_ptr<const SampleData> held;
    double begin = 0.0, end = 0.0;
    bool loop = false;
    std::atomic<bool> playing { false };
    std::atomic<bool> inMix { false };
    std::atomic<double> position { 0.0 };
};
} // namespace sls
