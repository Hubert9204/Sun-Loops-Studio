#pragma once

#include <utility>
#include <vector>

#include <juce_core/juce_core.h>

namespace sunburst
{
// A factory preset: the parameters that differ from their defaults, in their own units (a choice by its index).
struct FactoryPreset
{
    juce::String name, category;
    std::vector<std::pair<juce::String, float>> values;
};

const std::vector<FactoryPreset>& factoryPresets();
const juce::StringArray& presetCategories(); // in the order the browser shows them
} // namespace sunburst
