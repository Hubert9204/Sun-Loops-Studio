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
#include "audio/SoundLibrary.h"
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
// Scans for plugins (VST3, LV2, LADSPA) off the message thread. Each candidate is loaded in this process to read its description, so it
// only runs when the user asks for it. A plugin that crashes the scan is remembered (the "dead man's pedal" file) and skipped next time.
class PluginScanThread : public juce::Thread
{
public:
    PluginScanThread (PluginHost& hostToUse, juce::File pedalFile, std::function<void (const juce::String&)> onProgress, std::function<void (int)> whenDone)
        : juce::Thread ("Plugin scan"), host (hostToUse), pedal (std::move (pedalFile)), progress (std::move (onProgress)), done (std::move (whenDone))
    {
    }

    void run() override
    {
        const int count = host.scan (&PluginHost::distributionPaths, pedal, [this] (const juce::String& file)
        {
            juce::MessageManager::callAsync ([callback = progress, file] { callback (file); });
        });
        if (threadShouldExit())
            return;
        juce::MessageManager::callAsync ([callback = done, count] { callback (count); });
    }

private:
    PluginHost& host;
    juce::File pedal;
    std::function<void (const juce::String&)> progress;
    std::function<void (int)> done;
};
} // namespace

void MainComponent::startPluginScan()
{
    if (pluginScanState == PluginScanState::Scanning)
        return;

    pluginScanState = PluginScanState::Scanning;
    rebuildBrowser();
    statusLabel.setText ("Scanning for plugins (" + pluginHost->formatNames().joinIntoString (", ") + ")...", juce::dontSendNotification);

    using Safe = juce::Component::SafePointer<MainComponent>;
    pluginScanThread = std::make_unique<PluginScanThread> (
        *pluginHost, pluginSettingsFile ("plugin-scan-crashed.txt"),
        [safe = Safe (this)] (const juce::String& file)
        {
            if (safe != nullptr && safe->pluginScanState == PluginScanState::Scanning)
                safe->statusLabel.setText ("Scanning " + file.fromLastOccurrenceOf ("/", false, false), juce::dontSendNotification);
        },
        [safe = Safe (this)] (int)
        {
            if (safe == nullptr)
                return;
            safe->pluginHost->saveKnownList (safe->pluginSettingsFile ("plugins.xml"));
            safe->refreshFoundPlugins();
            safe->pluginScanState = PluginScanState::Done;
            safe->rebuildBrowser();
            int instruments = 0;
            for (const auto& p : safe->foundPlugins)
                instruments += p.isInstrument ? 1 : 0;
            safe->statusLabel.setText (juce::String (safe->foundPlugins.size()) + " plugin(s) found: " + juce::String (instruments) + " instrument(s), "
                                           + juce::String (static_cast<int> (safe->foundPlugins.size()) - instruments) + " effect(s)",
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
        {
            channel.sample = std::move (sample);
            channel.program = nullptr;
        }
        else
            sampleOk = false; // keep whatever sample the channel had
    }
    if (preset.instrument == Instrument::Sampler && ! preset.programSource.empty())
    {
        // A built-in instrument (grand piano, bass...): every zone is decoded now, which can take a moment the first time.
        juce::MouseCursor::showWaitCursor();
        std::string error;
        auto program = makeLibraryProgram (preset.programSource.substr (std::string (kProgramSourcePrefix).size()), nullptr, error);
        juce::MouseCursor::hideWaitCursor();
        if (program != nullptr)
            channel.program = std::move (program);
        else
            sampleOk = false;
    }

    strips[static_cast<size_t> (channelIndex)]->refreshFromChannel();
    updateTrackRouting();
    publishProject();
    return sampleOk;
}

void MainComponent::applyBrowserItem (int channelIndex, const drag::Item& item)
{
    if (item.kind == "plugin")
    {
        // "instrument|<id>": a new channel (dropped on empty space or double-clicked) or this channel's instrument (dropped on it).
        // "effect|<id>": the first free slot of the channel's mixer track.
        const auto id = item.payload.fromFirstOccurrenceOf ("|", false, false);
        if (item.payload.startsWith ("instrument|"))
            setPluginInstrument (channelIndex, id);
        else
            addPluginEffectForChannel (channelIndex, id);
        return;
    }

    if (channelIndex < 0)
    {
        if (channels.size() >= static_cast<size_t> (kMaxChannels))
        {
            statusLabel.setText ("All " + juce::String (kMaxChannels) + " channels are in use", juce::dontSendNotification);
            return;
        }
        const bool wantsSynth = (item.kind == "instrument" && item.payload == "synth")
                             || (item.kind == "preset" && item.payload == "builtin:lead");
        const bool wantsDrums = item.kind == "instrument" && item.payload == "drums";
        channelIndex = addChannel (wantsDrums ? Instrument::Drums : wantsSynth ? Instrument::Synth : Instrument::Sampler, false);
        if (channelIndex < 0)
            return;
    }

    if (channelIndex >= static_cast<int> (channels.size()))
        return;

    selectChannel (channelIndex);
    auto& channel = channels[static_cast<size_t> (channelIndex)];

    if (channel.instrument == Instrument::Drums && (item.kind == "file" || item.kind == "builtin"))
    {
        // A sound dropped on a drum channel would turn it into a sampler and lose the kit: it belongs on a pad.
        statusLabel.setText ("Drop sounds on a pad of the drum machine (double-click the channel to open it)", juce::dontSendNotification);
        return;
    }

    if (item.kind == "file" && item.payload.endsWithIgnoreCase (".sf2"))
    {
        chooseSoundFontPreset (channelIndex, juce::File (item.payload));
    }
    else if (item.kind == "file" && item.payload.endsWithIgnoreCase (".sfz"))
    {
        applySfz (channelIndex, juce::File (item.payload));
    }
    else if (item.kind == "file")
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
            channel.program = nullptr;
            strips[static_cast<size_t> (channelIndex)]->refreshFromChannel();
            updateTrackRouting();
            publishProject();
            statusLabel.setText ("Loaded " + juce::String (channel.name), juce::dontSendNotification);
        }
    }
    else if (item.kind == "instrument")
    {
        setInstrument (channelIndex, item.payload == "sampler" ? Instrument::Sampler : item.payload == "drums" ? Instrument::Drums : Instrument::Synth);
        if (item.payload == "drums")
            openDrumMachine (channelIndex);
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

// Export options (0.45.0): sample rate, bit depth (lossless) or bit rate (lossy), title and artist, and stems. Asked before the file
// chooser and remembered in the settings.
void MainComponent::exportAs (ExportFormat format)
{
    const auto& info = exportFormatInfo (format);
    auto* window = new juce::AlertWindow ("Export options", juce::String ("Export the ") + (songMode ? "song" : "pattern") + " as "
                                              + juce::String (info.label).upToFirstOccurrenceOf ("  (", false, false),
                                          juce::MessageBoxIconType::NoIcon);
    const juce::StringArray rates { "44100 Hz", "48000 Hz", "88200 Hz", "96000 Hz" };
    const auto savedRate = settings != nullptr ? settings->getIntValue ("export.rate", 48000) : 48000;
    window->addComboBox ("rate", rates, "Sample rate");
    if (auto* box = window->getComboBoxComponent ("rate"))
        box->setSelectedItemIndex (std::max (0, rates.indexOf (juce::String (savedRate) + " Hz")));
    if (info.lossy)
    {
        const juce::StringArray rateChoices { "96 kbit/s", "128 kbit/s", "192 kbit/s", "256 kbit/s", "320 kbit/s" };
        window->addComboBox ("kbps", rateChoices, "Quality");
        if (auto* box = window->getComboBoxComponent ("kbps"))
            box->setSelectedItemIndex (std::max (0, rateChoices.indexOf (juce::String (settings != nullptr ? settings->getIntValue ("export.kbps", 192) : 192) + " kbit/s")));
    }
    else
    {
        juce::StringArray depths { "16-bit (dithered)", "24-bit" };
        if (format == ExportFormat::Wav)
            depths.add ("32-bit float");
        window->addComboBox ("bits", depths, "Bit depth");
        const int bits = settings != nullptr ? settings->getIntValue ("export.bits", 24) : 24;
        if (auto* box = window->getComboBoxComponent ("bits"))
            box->setSelectedItemIndex (bits == 16 ? 0 : (bits == 32 && format == ExportFormat::Wav ? 2 : 1));
    }
    window->addTextEditor ("title", settings != nullptr ? settings->getValue ("export.title") : juce::String(), "Title (written into WAV, Ogg, MP3, AAC)");
    window->addTextEditor ("artist", settings != nullptr ? settings->getValue ("export.artist") : juce::String(), "Artist");
    auto stems = std::make_unique<juce::ToggleButton> ("One file per mixer track (stems)");
    stems->setSize (320, 24);
    stems->setName ({}); // the window would show the name as a label above it too
    stems->setToggleState (settings != nullptr && settings->getBoolValue ("export.stems", false), juce::dontSendNotification);
    auto* stemsToggle = stems.get();
    window->addCustomComponent (stems.release());
    window->addButton ("Choose file...", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer<MainComponent> (this), window, stemsToggle, format, lossy = info.lossy] (int result)
        {
            std::unique_ptr<juce::Component> toggleOwner (stemsToggle); // custom components are not deleted by the window
            if (safe == nullptr || result != 1)
                return;
            RenderOptions options;
            ExportSettings exportSettings;
            options.sampleRate = window->getComboBoxComponent ("rate")->getText().getDoubleValue();
            if (lossy)
                exportSettings.kbps = window->getComboBoxComponent ("kbps")->getText().getIntValue();
            else
            {
                const int index = window->getComboBoxComponent ("bits")->getSelectedItemIndex();
                options.bitDepth = index == 0 ? 16 : (index == 2 ? 32 : 24);
            }
            exportSettings.title = window->getTextEditorContents ("title").trim().toStdString();
            exportSettings.artist = window->getTextEditorContents ("artist").trim().toStdString();
            const bool wantStems = stemsToggle->getToggleState();
            if (auto* s = safe->settings.get())
            {
                s->setValue ("export.rate", static_cast<int> (options.sampleRate));
                s->setValue ("export.bits", options.bitDepth);
                s->setValue ("export.kbps", exportSettings.kbps);
                s->setValue ("export.title", juce::String (exportSettings.title));
                s->setValue ("export.artist", juce::String (exportSettings.artist));
                s->setValue ("export.stems", wantStems);
                s->saveIfNeeded();
            }
            safe->exportWithOptions (format, options, exportSettings, wantStems);
        }),
        true);
}

void MainComponent::exportWithOptions (ExportFormat format, RenderOptions chosen, const ExportSettings& exportSettings, bool stems)
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
                              [safe = juce::Component::SafePointer<MainComponent> (this), format, extension, chosen, exportSettings, stems] (const juce::FileChooser& chooser)
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

        auto options = chosen;
        options.bpm = safe->bpmSlider.getValue();

        safe->beginPluginExport (options.sampleRate); // plugins render the export; the live engine is silent meanwhile
        std::vector<juce::File> written;
        const auto result = stems ? exportStems (safe->compileCurrent(), options, file, format, written, {}, exportSettings)
                                  : exportAudio (safe->compileCurrent(), options, file, format, {}, exportSettings);
        safe->endPluginExport();

        safe->statusLabel.setText (result.failed() ? "Export failed: " + result.getErrorMessage()
                                                   : stems ? "Exported " + juce::String (static_cast<int> (written.size())) + " stems next to " + file.getFileName()
                                                           : "Exported " + file.getFileName(),
                                   juce::dontSendNotification);
        if (result.failed())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export failed",
                                                    result.getErrorMessage(), "OK", safe.getComponent());
    });
}

// One click on a sound in the browser plays it once through the preview player (the sample editor's), at its own speed, whether
// the song is playing or not. Clicking it again starts it again; another sound replaces it. Long files play their first 30
// seconds (as when loaded into a channel); very large files are not decoded just to listen.
void MainComponent::previewBrowserItem (const drag::Item& item)
{
    if (sampleEditorWindow != nullptr)
        return; // the editor's own preview is using the player
    const auto key = item.kind + "|" + item.payload;
    if (key != previewKey || previewSound == nullptr)
    {
        std::shared_ptr<const SampleData> sound;
        if (item.kind == "builtin")
            sound = makeBuiltinSample (item.payload.toStdString());
        else if (item.kind == "file")
        {
            const juce::File file (item.payload);
            if (file.getSize() > juce::int64 { 200 } * 1024 * 1024)
            {
                statusLabel.setText (file.getFileName() + " is too large to preview: drag it onto a channel or the playlist", juce::dontSendNotification);
                return;
            }
            juce::String error;
            sound = decodeSample (formats, file, error);
            if (sound == nullptr)
            {
                statusLabel.setText (error, juce::dontSendNotification);
                return;
            }
        }
        if (sound == nullptr)
            return;
        previewKey = key;
        previewSound = std::move (sound);
    }
    engine.auditioner().play (previewSound, 0.0, 0.0, false);
}

} // namespace sls
