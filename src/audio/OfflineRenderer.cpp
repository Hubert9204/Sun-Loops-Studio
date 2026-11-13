#include "audio/OfflineRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "audio/Player.h"

namespace sls
{
juce::AudioBuffer<float> renderToBuffer (const Project& published, const RenderOptions& options)
{
    // 0.62.0: the render has its own convolution reverbs, made for its sample rate (those of the project belong to the live engine).
    Project project = published;
    prepareConvolvers (project.tracks, project.masterSlots, project.console.aux, options.sampleRate, true);

    const double bpm = std::clamp (options.bpm, 20.0, 400.0);
    const double secondsPerTick = 60.0 / (bpm * kTicksPerBeat);

    // With tempo changes (0.46.0) the song lasts as long as its tempos say.
    const double loopSeconds = project.tempoMarkers.empty() ? project.lengthTicks * secondsPerTick
                                                            : tickToSeconds (project.tempoMarkers, bpm, project.lengthTicks);
    const auto loopSamples = static_cast<int> (std::llround (loopSeconds * options.sampleRate));
    const auto tailSamples = static_cast<int> (std::llround (std::max (0.0, options.tailSeconds) * options.sampleRate));
    const int total = loopSamples + tailSamples;

    juce::AudioBuffer<float> buffer (2, std::max (1, total));
    buffer.clear();

    Player player;
    player.prepare (options.sampleRate);
    player.setReleaseSamplesOnStop (false); // let drum tails ring through the tail
    player.setMakesOwnMemory (true);        // offline: every reverb and delay gets its memory, however many there are

    constexpr int blockSize = 512;
    int pos = 0;

    auto renderRange = [&] (int end, bool playing)
    {
        while (pos < end)
        {
            const int n = std::min (blockSize, end - pos);
            float* channels[2] = { buffer.getWritePointer (0, pos), buffer.getWritePointer (1, pos) };
            player.render (project, bpm, playing, channels, 2, n);
            pos += n;
        }
    };

    renderRange (loopSamples, true);
    // 0.62.0: effects that look ahead (and the delay compensation that lines the other tracks up with them) make the whole mix
    // late by a known number of samples: that many more are rendered and as many dropped from the start.
    const int latency = std::clamp (player.outputLatencySamples(), 0, static_cast<int> (options.sampleRate));
    if (latency > 0)
        buffer.setSize (2, total + latency, true, true);
    renderRange (total + latency, false);
    if (latency > 0)
        for (int ch = 0; ch < 2; ++ch)
            std::memmove (buffer.getWritePointer (ch), buffer.getReadPointer (ch) + latency, sizeof (float) * static_cast<size_t> (total));

    buffer.setSize (2, total, true);
    return buffer;
}

juce::Result renderToWavFile (const Project& project, const RenderOptions& options, const juce::File& file)
{
    if (options.bitDepth != 16 && options.bitDepth != 24)
        return juce::Result::fail ("Unsupported bit depth");

    auto buffer = renderToBuffer (project, options);

    file.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return juce::Result::fail ("Cannot open " + file.getFullPathName() + " for writing");

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), options.sampleRate, 2,
                                                                          options.bitDepth, {}, 0));
    if (writer == nullptr)
        return juce::Result::fail ("Cannot create WAV writer");

    [[maybe_unused]] auto* owned = stream.release(); // the writer owns the stream now

    if (! writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
        return juce::Result::fail ("Failed while writing audio data");

    return juce::Result::ok();
}
} // namespace sls
