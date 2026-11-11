#pragma once

#include <utility>
#include <vector>

#include "synthkit/SynthProcessor.h"

namespace hexforge
{
using FactoryPreset = synthkit::FactoryPreset; // the parameters that differ from their defaults, in their own units

const std::vector<FactoryPreset>& factoryPresets();
const juce::StringArray& presetCategories();
juce::String spectralPresetName (int index); // "Saw", "Square", "Odd", "Bright", "Formant", "Metal", "Clear"
} // namespace hexforge
