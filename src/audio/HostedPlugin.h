#pragma once

#include <cstdint>
#include <string>

namespace sls
{
// A third-party plugin (VST3, LV2, LADSPA) as the audio engine sees it (0.17.0). The engine knows nothing of JUCE's plugin classes: this
// interface is implemented in src/plugins/PluginHost.cpp. A plugin is shared by the project snapshots that use it (shared_ptr), so it
// is created and destroyed on the message thread; only the audio thread (or an export, while the engine is suspended) calls process().

struct PluginMidiEvent
{
    int offset = 0;                  // sample within the block
    std::uint8_t bytes[3] { 0, 0, 0 }; // a channel-voice MIDI message on MIDI channel 1
};

class HostedPlugin
{
public:
    virtual ~HostedPlugin() = default;

    // Audio thread. An effect processes `left` / `right` in place; an instrument writes its output there (they hold zeros on entry).
    // `numSamples` is at most the block size it was prepared with; `events` are sorted by offset. Never allocates, locks or does I/O
    // (as far as the plugin itself keeps to that). A placeholder (see isLoaded) leaves effects alone and keeps instruments silent.
    virtual void process (float* left, float* right, int numSamples, const PluginMidiEvent* events, int numEvents) noexcept = 0;

    virtual bool isInstrument() const noexcept = 0;

    // Audio thread, before process(): the song's tempo, its position in quarter notes and whether it plays (0.48.0), for plugins that
    // follow the tempo (synced LFOs, delays, arpeggiators).
    virtual void setTransport (double /*bpm*/, double /*quarters*/, bool /*playing*/) noexcept {}

    // Parameters (0.35.0), for automation. Values are the plugin's normalised 0..1. setParameter is for the audio thread (it does
    // nothing when the value is the one it set last); the others are for the message thread. A placeholder has none.
    virtual int parameterCount() const noexcept { return 0; }
    virtual void setParameter (int /*index*/, float /*value*/) noexcept {}
    virtual float parameterValue (int /*index*/) const noexcept { return 0.0f; }
    virtual float parameterDefault (int /*index*/) const noexcept { return 0.0f; }
    virtual std::string parameterName (int /*index*/) const { return {}; }
    virtual std::string parameterText (int /*index*/, float /*value*/) const { return {}; } // the value as the plugin writes it
    virtual int lastTouchedParameter() const noexcept { return -1; } // the one last moved in the plugin's own window, or -1
    virtual bool isLoaded() const noexcept = 0; // false: the plugin could not be found when the project was opened (it keeps its settings)
    virtual const std::string& displayName() const noexcept = 0;
};
} // namespace sls
