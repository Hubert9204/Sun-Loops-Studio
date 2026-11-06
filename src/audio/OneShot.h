#pragma once

#include <cstddef>
#include <memory>

#include "audio/Project.h"

namespace sls
{
// Plays one sample once, straight into the output: used for the start-up sound. The caller owns the audio thread rules:
// nothing here allocates or locks. The sample is read with linear interpolation, so a file at any sample rate plays at
// its real speed on any device.
class OneShot
{
public:
    void setSound (std::shared_ptr<const SampleData> newSound) noexcept { sound = std::move (newSound); active = false; }
    void start() noexcept { active = sound != nullptr; position = 0.0; }
    bool isActive() const noexcept { return active; }

    // Adds the next `numSamples` of the sound to `out[0..numChannels-1]` (even channels get the left side, odd the right)
    // at `deviceRate`, and moves on. Does nothing once the sound has ended.
    void mixInto (float* const* out, int numChannels, int numSamples, double deviceRate) noexcept
    {
        if (! active || sound == nullptr || deviceRate <= 0.0)
            return;
        const double step = sound->sampleRate / deviceRate;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (out[ch] == nullptr)
                continue;
            const auto& src = (ch % 2 == 0) ? sound->left : sound->right;
            const int n = static_cast<int> (src.size());
            double pos = position;
            for (int i = 0; i < numSamples; ++i, pos += step)
            {
                const int i0 = static_cast<int> (pos);
                if (i0 >= n)
                    break;
                const float frac = static_cast<float> (pos - i0);
                const float a = src[static_cast<size_t> (i0)];
                const float b = i0 + 1 < n ? src[static_cast<size_t> (i0 + 1)] : 0.0f;
                out[ch][i] += a + (b - a) * frac;
            }
        }
        position += step * numSamples;
        if (position >= sound->numFrames())
            active = false;
    }

private:
    std::shared_ptr<const SampleData> sound;
    bool active = false;
    double position = 0.0;
};
} // namespace sls
