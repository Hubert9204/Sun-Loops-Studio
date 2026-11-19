#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "audio/HostedPlugin.h"

namespace sls
{
// Plugins in a process of their own (0.85.0, as FL Studio's "bridged" plugins): a sandbox. The program starts itself again with
// "--plugin-sandbox <file> <pid>"; that process (the worker) loads the plugin as usual and plays it, and shows its window. The two share
// a memory-mapped file: audio in and out, MIDI, automation, the transport, and a mailbox for the slower requests (load, state, names,
// programs, the window). If the plugin crashes, only the worker goes: the program hears silence from it (an effect lets the sound
// through), says so, and can start it again with its last state. A worker whose program is gone ends itself.
namespace sandbox
{
constexpr std::uint32_t kMagic = 0x534c5342u; // "SLSB"
constexpr std::uint32_t kVersion = 1;
constexpr int kMaxBlock = 1024;
constexpr int kMaxChannels = 32;
constexpr int kMaxEvents = 512;
constexpr int kMaxParams = 4096;
constexpr std::size_t kMailboxBytes = 64u * 1024u * 1024u; // a plugin's state, its parameters' names

enum Command : std::int32_t
{
    None,
    Load,          // in: description XML; rate, block. out: name and info
    Prepare,       // rate, block
    GetState,      // out: the state
    SetState,      // in: the state
    ParameterInfo, // out: JSON [ { name, default } ... ]
    ParameterText, // index, value. out: text
    Programs,      // out: JSON { names: [...], current }
    SetProgram,    // index
    ShowEditor,    // flag
    Quit
};

struct ParamChange
{
    std::int32_t index = 0;
    float value = 0.0f;
};

// The shared memory. Plain data at fixed places; the counters are lock-free atomics, which work across processes.
struct Shared
{
    std::uint32_t magic, version;
    std::atomic<std::uint32_t> childHeartbeat; // the worker counts while it lives
    std::atomic<std::uint32_t> ready;          // 1 once the worker has mapped the memory and waits for requests

    // the mailbox (message threads)
    std::atomic<std::uint32_t> requestSerial, doneSerial;
    std::int32_t command, result, intArg, flagArg;
    float floatArg;
    double rate;
    std::int32_t block;
    std::uint64_t size; // bytes in `mailbox`

    // what the worker says about the plugin (after Load and Prepare)
    std::int32_t latency, mainIns, mainOuts, sideChannels, extraOuts, makesMidi, isInstrument, numParams;

    // what it reports while it runs
    std::atomic<std::uint32_t> changes;
    std::atomic<std::int32_t> lastTouched;
    std::atomic<std::uint32_t> editorOpen;
    std::array<std::atomic<float>, kMaxParams> paramValues;

    // one chunk of audio (the audio threads)
    std::atomic<std::uint32_t> audioRequest, audioDone;
    std::int32_t numSamples, numEvents, numParamChanges, numMidiOut, wantExtra, wantMidiOut, hasSide;
    double bpm, quarters, barStart;
    std::int32_t playing, numerator, denominator;
    std::array<std::array<float, kMaxBlock>, kMaxChannels> audioIn, audioOut; // in: main pair, then the sidechain; out: main pair, then the rest
    std::array<PluginMidiEvent, kMaxEvents> events, midiOut;
    std::array<ParamChange, kMaxEvents> paramChanges;

    char mailbox[kMailboxBytes];
};
} // namespace sandbox

// The program's side of one sandboxed plugin.
class SandboxClient
{
public:
    ~SandboxClient();

    // Message thread: starts a worker (`executable --plugin-sandbox ...`) and loads the plugin in it. nullptr and `error` on failure.
    static std::unique_ptr<SandboxClient> start (const juce::File& executable, const juce::PluginDescription& description, double rate, int block,
                                                 juce::String& error);

    // Audio thread. Never waits longer than a few milliseconds: a worker that does not answer in time is heard as silence (an effect: as
    // the sound it got), and the chunks after it are skipped until it answers again; a worker that is gone is not asked at all.
    void process (float* left, float* right, int numSamples, const PluginMidiEvent* events, int numEvents, PluginIo* io, bool isEffect) noexcept;
    void setParameter (int index, float value) noexcept; // audio thread: sent with the next chunk
    void setTransport (double bpm, double quarters, bool playing) noexcept { tBpm = bpm; tQuarters = quarters; tPlaying = playing; }
    void setMeter (int numerator, int denominator, double barStart) noexcept { tNum = numerator; tDen = denominator; tBar = barStart; }

    // Message thread (each waits for the worker, a few seconds at most).
    bool prepare (double rate, int block);
    juce::MemoryBlock getState();
    bool setState (const juce::MemoryBlock& state);
    juce::String parameterText (int index, float value);
    juce::StringArray programNames (int& current);
    bool setProgram (int index);
    void showEditor (bool show);

    // Any thread.
    bool crashed() const noexcept { return gone.load (std::memory_order_acquire); }
    bool stalled() const noexcept { return late.load (std::memory_order_relaxed); }
    std::uint32_t changeCount() const noexcept { return shared->changes.load (std::memory_order_acquire); }
    int lastTouched() const noexcept { return shared->lastTouched.load (std::memory_order_relaxed); }
    float parameterValue (int index) const noexcept;

    // What it said when it loaded.
    const juce::String& pluginName() const noexcept { return name; }
    int latency() const noexcept { return shared->latency; }
    bool hasSidechain() const noexcept { return shared->sideChannels > 0; }
    int extraOutputs() const noexcept { return shared->extraOuts; }
    bool makesMidi() const noexcept { return shared->makesMidi != 0; }
    const std::vector<std::pair<std::string, float>>& parameters() const noexcept { return params; } // name, default

    // Message thread: whether the worker still runs (it is asked of the system; sets crashed() when it does not).
    bool checkAlive();
    void kill() { child.kill(); } // the tests' crash
    // The state it had when last asked (to start it again after a crash).
    const juce::MemoryBlock& lastKnownState() const noexcept { return lastState; }

private:
    SandboxClient() = default;
    bool request (sandbox::Command command, int timeoutMs);

    juce::File file;
    std::unique_ptr<juce::MemoryMappedFile> map;
    sandbox::Shared* shared = nullptr;
    juce::ChildProcess child;
    juce::String name;
    std::vector<std::pair<std::string, float>> params;
    juce::MemoryBlock lastState;
    std::atomic<bool> gone { false }, late { false };
    std::uint32_t serial = 0;
    // the audio thread's own
    std::array<sandbox::ParamChange, sandbox::kMaxEvents> pendingParams {};
    int numPending = 0;
    double tBpm = 120.0, tQuarters = 0.0, tBar = 0.0;
    bool tPlaying = false;
    int tNum = 4, tDen = 4;
};

// The worker's side. `arguments` are the program's command-line arguments; true when they ask for a sandbox (it then runs one and the
// caller keeps the message loop going until it calls `onFinished`).
class PluginSandboxWorker
{
public:
    static bool isSandboxCommandLine (const juce::StringArray& arguments);
    static std::unique_ptr<PluginSandboxWorker> start (const juce::StringArray& arguments, std::function<void()> onFinished);
    ~PluginSandboxWorker();

    struct Impl;

private:
    std::unique_ptr<Impl> impl;
};

// For a plain main() (the tests): runs a worker with its own message loop; returns when it ends.
int runPluginSandbox (const juce::StringArray& arguments);
} // namespace sls
