#pragma once

#include <cmath>
#include <string>
#include <vector>

namespace sls
{
// Decoded audio kept in memory. Immutable once shared.
struct SampleData
{
    std::vector<float> left, right;
    double sampleRate = 44100.0;
    std::string name;
    std::string source; // where it came from: an absolute file path, or "builtin:<id>"; used by channel presets

    int numFrames() const noexcept { return static_cast<int> (left.size()); }
};

inline float finiteOr (float v, float fallback) noexcept { return std::isfinite (v) ? v : fallback; }
} // namespace sls
