#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "audio/Automation.h"
#include "audio/HostedPlugin.h"

namespace sls
{
// Hosting third-party plugins (0.17.0): VST3, LV2 and (when the LADSPA header was found at build time) LADSPA, as instruments and as
// effects. Plugins run in this process: a plugin that crashes takes the program with it (there is no sandbox).

// One plugin instance. It processes in blocks of at most `maxBlock` samples on the audio thread (Player calls it once per 64-sample
// chunk), stereo in and out (a mono plugin gets the left channel and feeds both). A placeholder (no instance) keeps the description and
// the saved state of a plugin that could not be loaded, so saving the project keeps them.
class JucePlugin final : public HostedPlugin, private juce::AudioProcessorListener
{
public:
    JucePlugin (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::PluginDescription& description);
    static std::shared_ptr<JucePlugin> placeholder (const juce::PluginDescription& description, juce::MemoryBlock savedState, bool instrument);
    ~JucePlugin() override;

    void process (float* left, float* right, int numSamples, const PluginMidiEvent* events, int numEvents) noexcept override;
    bool isInstrument() const noexcept override { return instrument; }
    int latencySamples() const noexcept override { return instance != nullptr ? instance->getLatencySamples() : 0; }
    void setTransport (double bpm, double quarters, bool playing) noexcept override
    {
        head.bpm = bpm;
        head.quarters = quarters;
        head.playing = playing;
    }
    bool isLoaded() const noexcept override { return instance != nullptr; }
    const std::string& displayName() const noexcept override { return name; }

    int parameterCount() const noexcept override { return static_cast<int> (parameters.size()); }
    void setParameter (int index, float value) noexcept override;
    float parameterValue (int index) const noexcept override;
    float parameterDefault (int index) const noexcept override;
    std::string parameterName (int index) const override;
    std::string parameterText (int index, float value) const override;
    int lastTouchedParameter() const noexcept override { return lastTouched.load (std::memory_order_relaxed); }
    bool selectProgram (const std::string& programName) override;

    // Message thread, never while the audio thread may call process() (before the plugin is published, while the device is stopped, or
    // while the engine is suspended).
    void prepare (double sampleRate, int maxBlock);
    double preparedRate() const noexcept { return rate; }

    const juce::PluginDescription& getDescription() const noexcept { return description; }
    juce::AudioPluginInstance* getInstance() noexcept { return instance.get(); }
    juce::MemoryBlock getState() const;              // the plugin's own state (a placeholder returns what it was given)
    bool setState (const juce::MemoryBlock& state);  // message thread

    // True once after the plugin reported a change of its parameters or settings (any thread may set it): the project needs saving.
    bool takeChanged() noexcept { return changed.exchange (false, std::memory_order_acq_rel); }

private:
    JucePlugin() = default;

    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::PluginDescription description;
    juce::MemoryBlock keptState; // placeholders only
    std::string name;
    bool instrument = false;
    double rate = 0.0;
    int block = 0;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    std::atomic<bool> changed { false };

    // What the plugin is told about the song (set and read on the audio thread).
    struct Head final : juce::AudioPlayHead
    {
        double bpm = 120.0, quarters = 0.0;
        bool playing = false;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            info.setPpqPosition (quarters);
            info.setIsPlaying (playing);
            info.setTimeSignature (juce::AudioPlayHead::TimeSignature {});
            return info;
        }
    } head;
    // The plugin's parameters, read once (the instance owns them), and the value automation set last on each (NaN: none yet).
    std::vector<juce::AudioProcessorParameter*> parameters;
    std::unique_ptr<std::atomic<float>[]> automated;
    std::atomic<int> lastTouched { -1 };
    std::atomic<bool> settingFromAutomation { false };

    void audioProcessorParameterChanged (juce::AudioProcessor*, int index, float) override
    {
        // A change the automation made itself (some formats report it back) is neither an edit of the project nor a touch.
        if (settingFromAutomation.load (std::memory_order_relaxed))
            return;
        changed.store (true, std::memory_order_release);
        lastTouched.store (index, std::memory_order_relaxed);
    }
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int index) override { lastTouched.store (index, std::memory_order_relaxed); }
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& d) override
    {
        if (d.parameterInfoChanged || d.programChanged || d.nonParameterStateChanged)
            changed.store (true, std::memory_order_release);
    }
};

// The plugin formats, the list of known plugins (found by a scan and kept between runs) and the creation of instances.
class PluginHost
{
public:
    PluginHost();

    juce::AudioPluginFormatManager& formats() noexcept { return formatManager; }
    juce::KnownPluginList& known() noexcept { return knownPlugins; }

    // The formats this build can host ("VST3", "LV2", maybe "LADSPA").
    juce::StringArray formatNames() const;

    // Blocking: looks for plugins of every format in its usual places (VST3: ~/.vst3, /usr/lib/vst3 ...; LV2: ~/.lv2, /usr/lib/lv2 ...;
    // LADSPA: LADSPA_PATH or /usr/lib/ladspa), plus what `extraPaths` returns for a format's name. Each candidate is loaded in this
    // process to read its description; a plugin that crashed an earlier scan (the "dead man's pedal" file) is skipped. `progress` gets
    // the file being scanned. Returns how many plugins are known afterwards.
    int scan (const std::function<juce::FileSearchPath (const juce::String& formatName)>& extraPaths, const juce::File& deadMansPedal,
              const std::function<void (const juce::String&)>& progress = {});

    // Folders JUCE's own defaults leave out on Fedora / Nobara (64-bit libraries live in lib64), for scan()'s `extraPaths`:
    // VST3 /usr/lib64/vst3 and /usr/local/lib64/vst3; LV2 both lib and lib64 folders; LADSPA /usr/lib64/ladspa, /usr/local/lib64/ladspa, ~/.ladspa.
    static juce::FileSearchPath distributionPaths (const juce::String& formatName);
    // The plugins that come with the program (SunBurst 0.48.0, HexForge 0.50.0): VST3 bundles in ../lib/vst3 or ../lib64/vst3 next to the program
    // (also inside the AppImage), or in the build folder. They are added to the known list at start-up, without a scan.
    static juce::FileSearchPath bundledPaths();
    int addBundledPlugins();

    void loadKnownList (const juce::File& file);
    void saveKnownList (const juce::File& file) const;

    // A new instance, prepared for `sampleRate` and blocks of `maxBlock` samples, and registered so prepareAll() reaches it.
    // nullptr and a message on failure.
    std::shared_ptr<JucePlugin> create (const juce::PluginDescription& description, double sampleRate, int maxBlock, juce::String& error);
    void registerPlugin (const std::shared_ptr<JucePlugin>& plugin);

    // The sample rate changed (a new audio device): every plugin that still exists is prepared again. Same rules as JucePlugin::prepare.
    void prepareAll (double sampleRate, int maxBlock);
    std::vector<std::shared_ptr<JucePlugin>> livePlugins();

    // Descriptions to and from text (for the project file).
    static juce::String descriptionToText (const juce::PluginDescription& d);
    static bool descriptionFromText (const juce::String& text, juce::PluginDescription& d);

private:
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    // LV2 plugins are found by URI in a "world" that must have read their bundles first: the folders of the last scan are kept (also in
    // the saved list) and read once before the first LV2 plugin is made in a program that did not scan.
    juce::FileSearchPath lv2Folders;
    bool lv2FoldersRead = false;
    void readLv2Folders();

    std::mutex registryLock; // message thread only; the audio thread never touches the registry
    std::vector<std::weak_ptr<JucePlugin>> registry;
};
} // namespace sls
