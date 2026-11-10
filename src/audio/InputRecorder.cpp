#include "audio/InputRecorder.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace sls
{
InputRecorder::InputRecorder() = default;

InputRecorder::~InputRecorder()
{
    stop();
    thread.stopThread (2000);
}

juce::String InputRecorder::start (const juce::File& file, double sampleRate, Channels channels, int skipFrames, int firstInput)
{
    if (active.load())
        return "Already recording";
    if (sampleRate < 8000.0)
        return "No audio device is running";

    const auto folder = file.getParentDirectory();
    if (! folder.isDirectory() && ! folder.createDirectory().wasOk())
        return "Cannot create the folder " + folder.getFullPathName();

    auto stream = std::make_unique<juce::FileOutputStream> (file);
    if (! stream->openedOk())
        return "Cannot write to " + file.getFullPathName();

    const int numChannels = channels == Channels::Stereo ? 2 : 1;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> fileWriter (wav.createWriterFor (stream.get(), sampleRate, static_cast<unsigned int> (numChannels), 24, {}, 0));
    if (fileWriter == nullptr)
    {
        stream.reset();
        file.deleteFile();
        return "Cannot create a WAV file at " + file.getFullPathName();
    }
    static_cast<void> (stream.release()); // the writer owns it now

    if (! thread.isThreadRunning())
        thread.startThread();

    const int fifoSamples = static_cast<int> (sampleRate * 3.0); // three seconds of slack for a slow disk
    writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (fileWriter.release(), thread, fifoSamples);

    target = file;
    numFileChannels = numChannels;
    inputIndex = std::max (0, firstInput);
    framesWritten.store (0);
    skipRemaining.store (std::max (0, skipFrames));
    dropped.store (0);
    peak.store (0.0f);
    rateHz.store (sampleRate);
    active.store (true, std::memory_order_seq_cst);
    return {};
}

InputRecorder::Take InputRecorder::stop()
{
    Take take;
    if (writer == nullptr)
        return take;

    // Stop the audio thread from using the writer, then wait until it has left process().
    active.store (false, std::memory_order_seq_cst);
    while (inProcess.load (std::memory_order_seq_cst))
        std::this_thread::yield();

    writer.reset(); // writes what is still queued and closes the file

    take.file = target;
    take.frames = framesWritten.load();
    take.droppedBlocks = dropped.load();
    take.sampleRate = rateHz.load();
    return take;
}

void InputRecorder::process (const float* const* input, int numInputChannels, int numSamples) noexcept
{
    inProcess.store (true, std::memory_order_seq_cst);
    if (active.load (std::memory_order_seq_cst) && writer != nullptr && numSamples > 0)
    {
        const float* channels[2] = { nullptr, nullptr };
        const int first = inputIndex;
        if (numInputChannels > first && input != nullptr && input[first] != nullptr)
        {
            channels[0] = input[first];
            channels[1] = (numFileChannels == 2 && numInputChannels > first + 1 && input[first + 1] != nullptr) ? input[first + 1] : input[first];
        }

        if (channels[0] != nullptr)
        {
            float level = 0.0f;
            for (int c = 0; c < numFileChannels; ++c)
                for (int i = 0; i < numSamples; ++i)
                    level = std::max (level, std::fabs (channels[c][i]));
            float seen = peak.load (std::memory_order_relaxed);
            while (level > seen && ! peak.compare_exchange_weak (seen, level, std::memory_order_relaxed))
            {
            }
        }

        // The delay between sound and input: drop that many frames from the start.
        int offset = 0;
        const auto toSkip = skipRemaining.load (std::memory_order_relaxed);
        if (toSkip > 0)
        {
            offset = static_cast<int> (std::min<std::int64_t> (toSkip, numSamples));
            skipRemaining.store (toSkip - offset, std::memory_order_relaxed);
        }

        const int count = numSamples - offset;
        if (count > 0)
        {
            static const float silence[4096] = {};
            const float* source[2] = { channels[0], channels[1] };
            const float* shifted[2] = { nullptr, nullptr };
            bool ok = true;
            for (int c = 0; c < numFileChannels; ++c)
            {
                if (source[c] != nullptr)
                    shifted[c] = source[c] + offset;
                else if (count <= 4096)
                    shifted[c] = silence; // no input at all: record silence of the right length
                else
                    ok = false;
            }

            if (ok && writer->write (shifted, count))
                framesWritten.fetch_add (count, std::memory_order_relaxed);
            else
                dropped.fetch_add (1, std::memory_order_relaxed);
        }
    }
    inProcess.store (false, std::memory_order_seq_cst);
}
} // namespace sls
