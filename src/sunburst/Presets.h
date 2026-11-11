#pragma once

#include <utility>
#include <vector>

#include "synthkit/SynthProcessor.h"

namespace sunburst
{
using FactoryPreset = synthkit::FactoryPreset; // the parameters that differ from their defaults, in their own units

const std::vector<FactoryPreset>& factoryPresets();
const juce::StringArray& presetCategories(); // in the order the browser shows them
} // namespace sunburst
