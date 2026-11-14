#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// SunRiff's parameters (0.68.0): FL Slayer's controls, one parameter each, so the host can automate all of them.
namespace sunriff
{
namespace id
{
// play
inline constexpr const char* mode = "mode", *speed = "speed", *hold = "hold", *gliss = "gliss", *bend2 = "bend2";
// string and pickup
inline constexpr const char* string = "string", *coil = "coil", *pickup = "pickup", *tone = "tone", *slap = "slap", *fret = "fret",
                             *harmonic = "harmonic", *vel = "vel", *damping = "damping", *dampVel = "dampvel";
// amp
inline constexpr const char* amp = "amp", *cabinet = "cabinet", *drive = "drive", *presence = "presence", *feedback = "feedback",
                             *low = "low", *mid = "mid", *high = "high";
// effects and output
inline constexpr const char* fx = "fx", *fx1 = "fx1", *fx2 = "fx2", *master = "master";
} // namespace id

const juce::StringArray& modeNames();
const juce::StringArray& stringNames();
const juce::StringArray& coilNames();
const juce::StringArray& ampNames();
const juce::StringArray& cabinetNames();

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
} // namespace sunriff
