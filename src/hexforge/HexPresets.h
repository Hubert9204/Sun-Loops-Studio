#pragma once

#include <utility>
#include <vector>

#include <juce_core/juce_core.h>

namespace hexforge
{
// A factory preset: the parameters that differ from their defaults, in their own units (a choice by its index). "spectralA" /
// "spectralB" pick a spectral wave by its index in spectralPresetName().
struct FactoryPreset
{
    juce::String name, category;
    std::vector<std::pair<juce::String, float>> values;
};

const std::vector<FactoryPreset>& factoryPresets();
const juce::StringArray& presetCategories();
juce::String spectralPresetName (int index); // "Saw", "Square", "Odd", "Bright", "Formant", "Metal", "Clear"
} // namespace hexforge
