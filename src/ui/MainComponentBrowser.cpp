// Part of MainComponent: the browser, presets and export.
#include "ui/MainComponent.h"

#include "ui/AssignTrackDialog.h"
#include "ui/AudioSettings.h"
#include "ui/MetronomePanel.h"
#include "ui/TextPrompt.h"

#include <functional>
#include <map>
#include <optional>

#include <juce_audio_utils/juce_audio_utils.h>

#include "audio/BuiltinSamples.h"
#include "audio/CyberHub.h"
#include "audio/ClipEdit.h"
#include "audio/MidiImport.h"
#include "audio/OfflineRenderer.h"

namespace sls
{
// ------------------------------------------------------------------------- Browser

void MainComponent::setBrowserOpen (bool shouldBeOpen)
{
    browserOpen = shouldBeOpen;
    browser.setVisible (browserOpen);
    browserTab.setVisible (! browserOpen);
    browserToggle.setToggleState (browserOpen, juce::dontSendNotification);

    if (settings != nullptr)
    {
        settings->setValue ("browser.open", browserOpen);
        settings->saveIfNeeded();
    }
    resized();
}

void MainComponent::rebuildBrowser()
{
    library.ensureExists();
    browser.setTree (buildBrowserTree (library, foundPlugins, pluginScanState));
}

namespace
{
// Scans for VST3 plugins off the message thread. It loads each candidate in-process to read its description,
// so it is only started when the user asks for it.
class PluginScanThread : public juce::Thread
{
public:
    explicit PluginScanThread (std::function<void (std::vector<PluginInfo>)> whenDone)
        : juce::Thread ("VST3 scan"), done (std::move (whenDone))
    {
    }

    void run() override
    {
        PluginScanner scanner;
        auto found = scanner.scan (PluginScanner::defaultSearchPaths(), true);
        if (threadShouldExit())
            return;

        juce::MessageManager::callAsync ([callback = done, result = std::move (found)]() mutable { callback (std::move (result)); });
    }

private:
    std::function<void (std::vector<PluginInfo>)> done;
};
} // namespace

void MainComponent::startPluginScan()
{
    if (pluginScanState == PluginScanState::Scanning)
        return;

    pluginScanState = PluginScanState::Scanning;
    rebuildBrowser();
    statusLabel.setText ("Scanning for VST3 plugins...", juce::dontSendNotification);

    pluginScanThread = std::make_unique<PluginScanThread> (
        [safe = juce::Component::SafePointer<MainComponent> (this)] (std::vector<PluginInfo> found)
        {
            if (safe == nullptr)
                return;
            safe->foundPlugins = std::move (found);
            safe->pluginScanState = PluginScanState::Done;
            safe->rebuildBrowser();
            safe->statusLabel.setText (juce::String (safe->foundPlugins.size()) + " VST3 plugin(s) found (listed only: plugin hosting is not implemented yet)",
                                       juce::dontSendNotification);
        });
    pluginScanThread->startThread();
}

bool MainComponent::applyPreset (int channelIndex, const ChannelPreset& preset)
{
    auto& channel = channels[static_cast<size_t> (channelIndex)];
    preset.applyTo (channel);

    bool sampleOk = true;
    if (preset.instrument == Instrument::Sampler && ! preset.sampleSource.empty())
    {
        std::shared_ptr<const SampleData> sample;
        if (preset.sampleSource.rfind ("builtin:", 0) == 0)
            sample = makeBuiltinSample (preset.sampleSource.substr (8));
        else
        {
            juce::String error;
            sample = decodeSample (formats, juce::File (juce::String (preset.sampleSource)), error);
        }

        if (sample != nullptr)
            channel.sample = std::move (sample);
        else
            sampleOk = false; // keep whatever sample the channel had
    }

    strips[static_cast<size_t> (channelIndex)]->refreshFromChannel();
    updateTrackRouting();
    publishProject();
    return sampleOk;
}

void MainComponent::applyBrowserItem (int channelIndex, const drag::Item& item)
{
    if (channelIndex < 0)
    {
        if (channels.size() >= static_cast<size_t> (kMaxChannels))
        {
            statusLabel.setText ("All " + juce::String (kMaxChannels) + " channels are in use", juce::dontSendNotification);
            return;
        }
        const bool wantsSynth = (item.kind == "instrument" && item.payload == "synth")
                             || (item.kind == "preset" && item.payload == "builtin:lead");
        channelIndex = addChannel (wantsSynth ? Instrument::Synth : Instrument::Sampler, false);
        if (channelIndex < 0)
            return;
    }

    if (channelIndex >= static_cast<int> (channels.size()))
        return;

    selectChannel (channelIndex);
    auto& channel = channels[static_cast<size_t> (channelIndex)];

    if (item.kind == "file")
    {
        loadSampleFile (channelIndex, juce::File (item.payload));
    }
    else if (item.kind == "builtin")
    {
        if (auto sample = makeBuiltinSample (item.payload.toStdString()))
        {
            channel.instrument = Instrument::Sampler;
            channel.name = sample->name;
            channel.sample = std::move (sample);
            strips[static_cast<size_t> (channelIndex)]->refreshFromChannel();
            updateTrackRouting();
            publishProject();
            statusLabel.setText ("Loaded " + juce::String (channel.name), juce::dontSendNotification);
        }
    }
    else if (item.kind == "instrument")
    {
        setInstrument (channelIndex, item.payload == "sampler" ? Instrument::Sampler : Instrument::Synth);
    }
    else if (item.kind == "preset")
    {
        std::optional<ChannelPreset> preset;
        if (item.payload.startsWith ("builtin:"))
        {
            const auto id = item.payload.substring (8).toStdString();
            for (const auto& p : factoryPresets())
                if (p.id == id)
                    preset = p;
        }
        else
        {
            preset = ChannelPreset::fromJson (juce::File (item.payload).loadFileAsString());
        }

        if (! preset.has_value())
            statusLabel.setText ("Cannot read preset " + juce::File (item.payload).getFileName(), juce::dontSendNotification);
        else if (applyPreset (channelIndex, *preset))
            statusLabel.setText ("Applied preset " + juce::String (preset->name), juce::dontSendNotification);
        else
            statusLabel.setText ("Applied preset " + juce::String (preset->name) + " (its sample file was not found)",
                                 juce::dontSendNotification);
    }
}

void MainComponent::savePresetFor (int channelIndex)
{
    promptForText (*this, "Save channel preset", "Preset name:", juce::String (channels[static_cast<size_t> (channelIndex)].name), "Save",
                   [safe = juce::Component::SafePointer<MainComponent> (this), channelIndex] (const juce::String& typed)
                   {
                       const auto name = typed.trim().substring (0, 16);
                       const auto fileName = juce::File::createLegalFileName (name);
                       if (safe == nullptr || name.isEmpty() || fileName.isEmpty())
                           return;

                       auto* self = safe.getComponent();
                       const auto preset = ChannelPreset::fromChannel (self->channels[static_cast<size_t> (channelIndex)], name.toStdString());
                       self->library.ensureExists();
                       const auto file = self->library.presets.getChildFile (fileName + kPresetExtension);
                       const bool ok = file.replaceWithText (preset.toJson());
                       self->rebuildBrowser();
                       self->statusLabel.setText (ok ? "Saved preset " + name : "Could not write " + file.getFullPathName(),
                                                  juce::dontSendNotification);
                   });
}

void MainComponent::exportAs (ExportFormat format)
{
    const auto& info = exportFormatInfo (format);
    const juce::String extension (info.extension);
    const auto label = juce::String (info.label).upToFirstOccurrenceOf ("  (", false, false);

    fileChooser = std::make_unique<juce::FileChooser> (
        juce::String (songMode ? "Export song as " : "Export pattern as ") + label,
        juce::File::getSpecialLocation (juce::File::userMusicDirectory)
            .getChildFile (juce::String (songMode ? "sun-loops-song" : "sun-loops-pattern") + extension),
        "*" + extension);

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe = juce::Component::SafePointer<MainComponent> (this), format, extension] (const juce::FileChooser& chooser)
    {
        if (safe == nullptr)
            return;
        auto file = chooser.getResult();
        if (file == juce::File())
            return; // cancelled

        // "My.Song" -> "My.Song.mp3": append unless the name already ends with the extension.
        if (! file.getFileName().endsWithIgnoreCase (extension))
            file = file.getParentDirectory().getChildFile (file.getFileName() + extension);

        safe->statusLabel.setText ("Exporting " + file.getFileName() + " ...", juce::dontSendNotification);
        safe->statusLabel.repaint();

        RenderOptions options;
        options.bpm = safe->bpmSlider.getValue();

        const auto result = exportAudio (safe->compileCurrent(), options, file, format);

        safe->statusLabel.setText (result.wasOk() ? "Exported " + file.getFileName()
                                                  : "Export failed: " + result.getErrorMessage(),
                                   juce::dontSendNotification);
        if (result.failed())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export failed",
                                                    result.getErrorMessage(), "OK", safe.getComponent());
    });
}

} // namespace sls
