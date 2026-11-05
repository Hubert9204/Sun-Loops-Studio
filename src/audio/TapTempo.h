#pragma once

#include <algorithm>
#include <array>

namespace sls
{
// Tap tempo: call tap() with the time (in seconds) of every tap; it returns the tempo in BPM once there are at least
// two taps close enough together, 0 otherwise. A pause of more than `kResetSeconds` starts a new series. The tempo is
// the average of the last few intervals, clamped to the range of the BPM control.
class TapTempo
{
public:
    static constexpr double kResetSeconds = 2.0;
    static constexpr int kIntervals = 4;
    static constexpr double kMinBpm = 40.0, kMaxBpm = 240.0;

    double tap (double timeSeconds) noexcept
    {
        if (count > 0 && (timeSeconds - last > kResetSeconds || timeSeconds <= last))
            count = 0; // a new series (or the clock went backwards)

        if (count > 0)
        {
            intervals[static_cast<size_t> (filled % kIntervals)] = timeSeconds - last;
            ++filled;
        }
        else
            filled = 0;

        last = timeSeconds;
        ++count;

        const int used = std::min (filled, kIntervals);
        if (used == 0)
            return 0.0;

        double sum = 0.0;
        for (int i = 0; i < used; ++i)
            sum += intervals[static_cast<size_t> (i)];
        return std::clamp (60.0 * used / sum, kMinBpm, kMaxBpm);
    }

    void reset() noexcept { count = 0; filled = 0; }

private:
    std::array<double, kIntervals> intervals {};
    double last = 0.0;
    int count = 0;
    int filled = 0;
};
} // namespace sls
