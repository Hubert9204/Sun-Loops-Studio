#include "audio/AsioDrivers.h"

#include <algorithm>
#include <iterator>

namespace sls
{
juce::StringArray rankAsioDrivers (const juce::StringArray& drivers)
{
    static const char* const generic[] = { "asio4all", "flexasio", "generic low latency", "asio2wasapi", "asio2ks",
                                           "voicemeeter", "jack", "realtek asio" };
    juce::StringArray first, later;
    for (const auto& name : drivers)
    {
        const auto lower = name.toLowerCase();
        const bool isGeneric = std::any_of (std::begin (generic), std::end (generic), [&] (const char* g) { return lower.contains (g); });
        (isGeneric ? later : first).add (name);
    }
    first.addArray (later);
    return first;
}
} // namespace sls
