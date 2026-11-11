#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iterator>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

// The building blocks that SunBurst and HexForge share for their parameters (0.51.0): reading a parameter on the audio thread,
// adding parameters to a layout, and the lists of names both synthesizers show.
namespace synthkit
{
inline float get (const std::atomic<float>* p) noexcept { return p != nullptr ? p->load (std::memory_order_relaxed) : 0.0f; }
inline int geti (const std::atomic<float>* p) noexcept { return static_cast<int> (std::lround (get (p))); }
inline bool getb (const std::atomic<float>* p) noexcept { return get (p) > 0.5f; }

inline juce::NormalisableRange<float> skewed (float lo, float hi, float centre, float step = 0.0f)
{
    juce::NormalisableRange<float> r (lo, hi, step);
    r.setSkewForCentre (centre);
    return r;
}

// Adds parameters to a layout's list.
struct Adder
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>>& out;

    void real (const juce::String& id, const juce::String& name, const juce::NormalisableRange<float>& range, float def, const juce::String& unit = {},
               int decimals = 2)
    {
        auto attributes = juce::AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction (
            [decimals, unit] (float v, int) { return juce::String (v, decimals) + (unit.isEmpty() ? juce::String() : " " + unit); });
        out.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, def, attributes));
    }
    void integer (const juce::String& id, const juce::String& name, int lo, int hi, int def)
    {
        out.push_back (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id, 1 }, name, lo, hi, def));
    }
    void choice (const juce::String& id, const juce::String& name, const juce::StringArray& items, int def)
    {
        out.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, def));
    }
    void toggle (const juce::String& id, const juce::String& name, bool def)
    {
        out.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
    }
};

inline const juce::StringArray& lfoShapeNames()
{
    static const juce::StringArray names { "Sine", "Triangle", "Saw up", "Saw down", "Square", "Sample & hold", "Smooth random" };
    return names;
}
// Tempo divisions for the LFOs and the delay.
inline const juce::StringArray& syncNames()
{
    static const juce::StringArray names { "4 bars", "2 bars", "1 bar", "1/2", "1/2T", "1/4.", "1/4", "1/4T", "1/8.", "1/8", "1/8T", "1/16.", "1/16", "1/16T", "1/32" };
    return names;
}
// The length of a division in quarter notes.
inline double syncBeats (int index)
{
    static constexpr double beats[] { 16.0, 8.0, 4.0, 2.0, 4.0 / 3.0, 1.5, 1.0, 2.0 / 3.0, 0.75, 0.5, 1.0 / 3.0, 0.375, 0.25, 1.0 / 6.0, 0.125 };
    return beats[std::clamp (index, 0, static_cast<int> (std::size (beats)) - 1)];
}
inline const juce::StringArray& distNames()
{
    static const juce::StringArray names { "Soft", "Hard", "Tube", "Fold", "Crush" };
    return names;
}
} // namespace synthkit
