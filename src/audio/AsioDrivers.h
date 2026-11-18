#pragma once

#include <juce_core/juce_core.h>

namespace sls
{
// The order ASIO drivers are tried in for the default audio device on Windows (0.77.0): the drivers of audio interfaces first,
// in the order they are installed; then the generic ones that only wrap Windows's own audio or another driver (ASIO4ALL,
// FlexASIO, Steinberg's Generic Low Latency, ASIO2WASAPI...) and virtual mixers (Voicemeeter, JACK). Pure: tested on every system.
juce::StringArray rankAsioDrivers (const juce::StringArray& drivers);
} // namespace sls
