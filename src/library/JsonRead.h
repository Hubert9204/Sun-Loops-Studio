#pragma once

#include <algorithm>
#include <cmath>
#include <string>

#include <juce_core/juce_core.h>

namespace sls
{
// Small helpers for reading and writing the JSON of project, preset and kit files: numbers are clamped into their range and
// anything of the wrong type becomes the fallback, so a damaged or hostile file cannot put a value out of range.
constexpr size_t kMaxNameLength = 40; // channel, track, pattern, pad and clip names

inline juce::var number (double v) { return juce::var (std::isfinite (v) ? v : 0.0); }

inline double readNumber (const juce::var& v, double fallback, double lo, double hi)
{
    if (! (v.isDouble() || v.isInt() || v.isInt64()))
        return fallback;
    const double d = static_cast<double> (v);
    return std::isfinite (d) ? std::clamp (d, lo, hi) : fallback;
}

inline int readInt (const juce::var& v, int fallback, int lo, int hi)
{
    return static_cast<int> (std::lround (readNumber (v, fallback, lo, hi)));
}

inline bool readBool (const juce::var& v, bool fallback)
{
    return v.isBool() ? static_cast<bool> (v) : fallback;
}

inline std::string readName (const juce::var& v, const std::string& fallback)
{
    if (! v.isString())
        return fallback;
    const auto s = v.toString().trim().substring (0, static_cast<int> (kMaxNameLength));
    return s.isEmpty() ? fallback : s.toStdString();
}
} // namespace sls
