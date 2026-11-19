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
#include "plugins/PluginSandbox.h"

namespace sls
{
// Hosting third-party plugins (0.17.0): VST3, LV2 and (when the LADSPA header was found at build time) LADSPA, as instruments and as
// effects. Plugins run in this process; 0.85.0: or each in a process of its own (a sandbox, see PluginSandbox.h), where a crash takes only
// the plugin.

// One plugin instance. It processes in blocks of at most `maxBlock` samples on the audio thread (Player calls it once per 64-sample
// chunk), stereo in and out (a mono plugin gets the left channel and feeds both). A placeholder (no instance) keeps the description and
// the saved state of a plugin that could not be loaded, so saving the project keeps them.
class JucePlugin final : public HostedPlugin, private juce::AudioProcessorListener
{
public:
    JucePlugin (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::PluginDescription& description);
    JucePlugin (std::unique_ptr<SandboxClient> sandboxed, const juce::PluginDescription& description); // 0.85.0
    static std::shared_ptr<JucePlugin> placeholder (const juce::PluginDescription& description, juce::MemoryBlock savedState, bool instrument);
    ~JucePlugin() override;

    void process (float* left, float* right, int numSamples, const PluginMidiEvent* events, int numEvents, PluginIo* io = nullptr) noexcept override;
    bool hasSidechain() const noexcept override { return bridge != nullptr ? bridge->hasSidechain() : sideChannels > 0; }
    int extraOutputChannels() const noexcept override { return bridge != nullptr ? bridge->extraOutputs() : extraOuts; }
    bool sendsMidi() const noexcept override { return bridge != nullptr ? bridge->makesMidi() : makesMidi; }
    bool isInstrument() const noexcept override { return instrument; }
    int latencySamples() const noexcept override
    {
        return bridge != nullptr ? bridge->latency() : instance != nullptr ? instance->getLatencySamples() : 0;
    }
    void setTransport (double bpm, double quarters, bool playing) noexcept override
    {
        if (bridge != nullptr)
            bridge->setTransport (bpm, quarters, playing);
        head.bpm = bpm;
        head.quarters = quarters;
        head.playing = playing;
    }
    void setMeter (int numerator, int denominator, double barStartQuarters) noexcept override
    {
        if (bridge != nullptr)
            bridge->setMeter (numerator, denominator, barStartQuarters);
        head.numerator = numerator;
        head.denominator = denominator;
        head.barStart = barStartQuarters;
    }
    bool isLoaded() const noexcept override { return instance != nullptr || bridge != nullptr; }
    const std::string& displayName() const noexcept override { return name; }

    int parameterCount() const noexcept override { return bridge != nullptr ? static_cast<int> (bridge->parameters().size()) : static_cast<int> (parameters.size()); }
    void setParameter (int index, float value) noexcept override;
    float parameterValue (int index) const noexcept override;
    float parameterDefault (int index) const noexcept override;
    std::string parameterName (int index) const override;
    std::string parameterText (int index, float value) const override;
    int lastTouchedParameter() const noexcept override { return bridge != nullptr ? bridge->lastTouched() : lastTouched.load (std::memory_order_relaxed); }
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
    bool takeChanged() noexcept
    {
        if (bridge != nullptr)
        {
            const auto now = bridge->changeCount();
            const bool any = now != takenChanges;
            takenChanges = now;
            return any;
        }
        return changed.exchange (false, std::memory_order_acq_rel);
    }
    // 0.85.0: how many changes it reported so far (its window's undo watches it), and its programs (factory presets)
    std::uint32_t changeCount() const noexcept { return bridge != nullptr ? bridge->changeCount() : changes.load (std::memory_order_acquire); }

    // 0.85.0: the sandbox. None: it runs in this process; Running; Stalled: it did not answer in time; Crashed: its process is gone.
    enum class SandboxState { None, Running, Stalled, Crashed };
    bool isSandboxed() const noexcept { return bridge != nullptr; }
    bool wantsSandbox() const noexcept { return bridge != nullptr || sandboxWanted; } // (a placeholder keeps what the project said)
    void setWantsSandbox (bool want) noexcept { sandboxWanted = want; }
    SandboxState sandboxState() const noexcept
    {
        if (bridge == nullptr)
            return SandboxState::None;
        return bridge->crashed() ? SandboxState::Crashed : bridge->stalled() ? SandboxState::Stalled : SandboxState::Running;
    }
    void showSandboxEditor (bool show) { if (bridge != nullptr && ! bridge->crashed()) bridge->showEditor (show); }
    bool checkSandbox() { return bridge == nullptr || bridge->checkAlive(); } // message thread: false once its process is gone
    void killSandboxForTesting() { if (bridge != nullptr) bridge->kill(); }
    juce::StringArray programNames() const;
    int currentProgram() const;
    bool setProgram (int index);

private:
    JucePlugin() = default;

    std::unique_ptr<juce::AudioPluginInstance> instance;
    std::unique_ptr<SandboxClient> bridge; // 0.85.0: the plugin runs there instead
    std::uint32_t takenChanges = 0;
    bool sandboxWanted = false;
    juce::PluginDescription description;
    juce::MemoryBlock keptState; // placeholders only
    std::string name;
    bool instrument = false;
    double rate = 0.0;
    int block = 0;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    int mainIns = 2, mainOuts = 2, sideChannels = 0, extraOuts = 0; // 0.85.0: the plugin's channels, bus by bus (set by prepare)
    bool makesMidi = false;
    std::atomic<bool> changed { false };
    std::atomic<std::uint32_t> changes { 0 }; // 0.85.0
    void markChanged() noexcept
    {
        changed.store (true, std::memory_order_release);
        changes.fetch_add (1, std::memory_order_acq_rel);
    }

    // What the plugin is told about the song (set and read on the audio thread).
    struct Head final : juce::AudioPlayHead
    {
        double bpm = 120.0, quarters = 0.0;
        bool playing = false;
        int numerator = 4, denominator = 4; // 0.83.0
        double barStart = 0.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            info.setPpqPosition (quarters);
            info.setIsPlaying (playing);
            info.setTimeSignature (juce::AudioPlayHead::TimeSignature { numerator, denominator });
            info.setPpqPositionOfLastBarStart (barStart);
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
        markChanged();
        lastTouched.store (index, std::memory_order_relaxed);
    }
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int index) override { lastTouched.store (index, std::memory_order_relaxed); }
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& d) override
    {
        if (d.parameterInfoChanged || d.programChanged || d.nonParameterStateChanged)
            markChanged();
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
    // nullptr and a message on failure. 0.85.0: `sandboxed`: in a process of its own (see PluginSandbox.h).
    std::shared_ptr<JucePlugin> create (const juce::PluginDescription& description, double sampleRate, int maxBlock, juce::String& error,
                                        bool sandboxed = false);
    // The program a sandbox runs (this one, started again; set by the tests to their own).
    static juce::File& sandboxExecutable();

    // 0.85.0: the user's presets of a plugin (see PluginWindow): their folder under `root`, writing one, reading one into a plugin.
    static juce::File presetsFolder (const juce::File& root, const juce::PluginDescription& d);
    static juce::Result savePreset (const JucePlugin& p, const juce::File& file);
    static juce::Result loadPreset (JucePlugin& p, const juce::File& file);
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
