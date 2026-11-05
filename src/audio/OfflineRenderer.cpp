#include "audio/OfflineRenderer.h"

#include <algorithm>
#include <cmath>

#include "audio/Player.h"

namespace sls
{
juce::AudioBuffer<float> renderToBuffer (const Project& project, const RenderOptions& options)
{
    const double bpm = std::clamp (options.bpm, 20.0, 400.0);
    const double secondsPerTick = 60.0 / (bpm * kTicksPerBeat);

    const auto loopSamples = static_cast<int> (std::llround (project.lengthTicks * secondsPerTick * options.sampleRate));
    const auto tailSamples = static_cast<int> (std::llround (std::max (0.0, options.tailSeconds) * options.sampleRate));
    const int total = loopSamples + tailSamples;

    juce::AudioBuffer<float> buffer (2, std::max (1, total));
    buffer.clear();

    Player player;
    player.prepare (options.sampleRate);
    player.setReleaseSamplesOnStop (false); // let drum tails ring through the tail

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
    renderRange (total, false);

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

    stream.release(); // the writer owns the stream now

    if (! writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
        return juce::Result::fail ("Failed while writing audio data");

    return juce::Result::ok();
}
} // namespace sls
