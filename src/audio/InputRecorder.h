#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include <juce_audio_formats/juce_audio_formats.h>

namespace sls
{
// Records what comes in from the audio input into a WAV file (24 bit), without ever blocking the audio thread:
// process() copies the samples into a FIFO and a background thread writes them to disk.
//
// start() and stop() belong to the message thread, process() to the audio thread. stop() waits until the audio
// thread is out of process() before it closes the file, so no lock is needed anywhere on the audio side.
class InputRecorder
{
public:
    enum class Channels { Mono, Stereo }; // Mono: the first input, written as a one-channel file. Stereo: inputs 1 and 2.

    InputRecorder();
    ~InputRecorder();

    // Opens `file` and arms the recorder: from now on process() records. The first `skipFrames` frames are thrown
    // away (the delay between sound and input, so the take lines up with what was playing). Returns an error message
    // (empty on success); on an error no file is left behind.
    juce::String start (const juce::File& file, double sampleRate, Channels channels, int skipFrames);

    struct Take
    {
        juce::File file;                 // empty if nothing was being recorded
        std::int64_t frames = 0;         // frames in the file
        int droppedBlocks = 0;           // blocks that did not fit in the FIFO because the disk was too slow
        double sampleRate = 0.0;
    };

    // Finishes the file (it is complete and playable when this returns).
    Take stop();

    // Audio thread. Cheap and wait-free; safe to call when nothing is being recorded.
    bool isActive() const noexcept { return active.load (std::memory_order_acquire); }
    void process (const float* const* input, int numInputChannels, int numSamples) noexcept;

    // Any thread.
    std::int64_t getFramesRecorded() const noexcept { return framesWritten.load (std::memory_order_relaxed); }
    double getSecondsRecorded() const noexcept
    {
        const double rate = rateHz.load (std::memory_order_relaxed);
        return rate > 0.0 ? static_cast<double> (getFramesRecorded()) / rate : 0.0;
    }
    float consumePeak() noexcept { return peak.exchange (0.0f, std::memory_order_relaxed); } // since the last call

private:
    juce::TimeSliceThread thread { "Sun Loops recorder" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    juce::File target;

    std::atomic<bool> active { false };
    std::atomic<bool> inProcess { false };
    std::atomic<std::int64_t> framesWritten { 0 };
    std::atomic<std::int64_t> skipRemaining { 0 };
    std::atomic<int> dropped { 0 };
    std::atomic<float> peak { 0.0f };
    std::atomic<double> rateHz { 0.0 };
    int numFileChannels = 1;
};
} // namespace sls
