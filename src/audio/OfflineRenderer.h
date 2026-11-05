#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "audio/Project.h"

namespace sls
{
struct RenderOptions
{
    double sampleRate = 48000.0;
    double bpm = 120.0;
    int bitDepth = 24;          // 16 or 24 for WAV
    double tailSeconds = 1.0;   // extra time after the loop for releases and sample tails
};

// Renders one pass of the loop plus a tail, using the same Player as the live engine.
juce::AudioBuffer<float> renderToBuffer (const Project& project, const RenderOptions& options);

juce::Result renderToWavFile (const Project& project, const RenderOptions& options, const juce::File& file);
} // namespace sls
