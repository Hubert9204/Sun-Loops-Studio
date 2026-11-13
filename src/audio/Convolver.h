#pragma once

#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "audio/SampleData.h"

namespace sls
{
// A convolution reverb (0.62.0, the counterpart of Fruity Convolver): the sound is convolved with an impulse response, either one of
// the built-in spaces (made here, so the program ships no recordings) or an audio file. The convolution has no latency: the first
// kBlock taps are applied directly, the rest in the frequency domain, a block at a time (uniform partitions with overlap-save, the
// spectrum of each input block is kept and multiplied with every partition of the response).
//
// One object holds both the response and the state of the convolution, so it belongs to one effect slot. It is made on the message
// thread (make()) and used by the audio thread (process(), clearState()); process() never allocates.
class Convolver
{
public:
    enum Space { Room, Hall, Plate, Spring, Cathedral, File };

    static constexpr int kBlock = 256;         // the size of a partition
    static constexpr float kMaxSeconds = 4.0f; // of a response (plus the pre-delay)

    struct Settings
    {
        int space = Hall;
        float seconds = 1.5f;     // length of the response (a file is cut and faded there)
        float brightness = 0.6f;  // 0 dark (the high end dies quickly) .. 1 bright
        float preDelayMs = 0.0f;
        double sampleRate = 48000.0;

        bool operator== (const Settings& o) const noexcept
        {
            return space == o.space && std::abs (seconds - o.seconds) < 1.0e-4f && std::abs (brightness - o.brightness) < 1.0e-4f
                && std::abs (preDelayMs - o.preDelayMs) < 1.0e-3f && std::abs (sampleRate - o.sampleRate) < 0.5;
        }
    };

    // `file` is the response of the File space (ignored by the others, but kept so switching back finds it again).
    static std::shared_ptr<Convolver> make (const Settings& settings, std::shared_ptr<const SampleData> file = nullptr);

    const Settings& settings() const noexcept { return config; }
    const std::shared_ptr<const SampleData>& file() const noexcept { return source; }
    int impulseFrames() const noexcept { return static_cast<int> (impulse[0].size()); }
    const std::vector<float>& impulseResponse (int channel) const noexcept { return impulse[static_cast<size_t> (channel & 1)]; }

    void clearState() noexcept;
    // Replaces left / right with their convolution (the left input with the response's left channel, the right with its right).
    void process (float* left, float* right, int numSamples) noexcept;

private:
    Convolver() = default;
    void computeTail() noexcept;

    Settings config;
    std::shared_ptr<const SampleData> source;
    std::array<std::vector<float>, 2> impulse; // the whole response (for tests and displays)

    int partitions = 0; // of the part after the first kBlock taps
    std::array<std::array<float, kBlock>, 2> head {};          // the first taps, applied directly
    std::array<std::vector<float>, 2> hRe, hIm;                // partitions x (kBlock + 1) bins
    std::array<std::vector<float>, 2> xRe, xIm;                // the spectra of the last `partitions` input blocks (a ring)
    std::array<std::array<float, 2 * kBlock>, 2> history {};   // the last kBlock inputs, twice (so the direct part reads one run)
    std::array<std::array<float, kBlock>, 2> blockIn {}, blockBefore {}, tailOut {};
    int pos = 0, ring = 0;
};

// The response of a built-in space (one channel; the two channels differ, which is what makes the reverb wide). Exposed for tests.
std::vector<float> makeImpulseResponse (const Convolver::Settings& settings, int channel);
} // namespace sls
