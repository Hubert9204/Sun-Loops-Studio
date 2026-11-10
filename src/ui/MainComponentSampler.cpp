// Part of MainComponent: the sampler settings window, SoundFonts and slices turned into a sampler channel (0.16.0).
#include "ui/MainComponent.h"

#include "audio/SampleEdit.h"
#include "audio/ClipEdit.h"
#include "library/SoundFont.h"

#include <map>

namespace sls
{
void MainComponent::openSamplerSettings (int channelIndex)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int> (channels.size()) || channels[static_cast<size_t> (channelIndex)].instrument != Instrument::Sampler)
        return;
    if (samplerWindow == nullptr)
    {
        samplerWindow = std::make_unique<SamplerWindow>();
        samplerWindow->onClose = [safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeSamplerSettings(); });
        };
        auto& panel = samplerWindow->panel();
        panel.getChannel = [safe = juce::Component::SafePointer<MainComponent> (this)]() -> Channel*
        {
            if (safe == nullptr)
                return nullptr;
            const int i = safe->samplerWindowChannel;
            if (i < 0 || i >= static_cast<int> (safe->channels.size()) || safe->channels[static_cast<size_t> (i)].instrument != Instrument::Sampler)
                return nullptr;
            return &safe->channels[static_cast<size_t> (i)];
        };
        panel.onChanged = [safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe == nullptr)
                return;
            const int i = safe->samplerWindowChannel;
            if (i >= 0 && i < static_cast<int> (safe->strips.size()))
                safe->strips[static_cast<size_t> (i)]->refreshFromChannel();
            safe->publishProject();
        };
        panel.onLoadSample = [safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe != nullptr && safe->samplerWindowChannel >= 0)
                safe->chooseSampleFor (safe->samplerWindowChannel);
        };
        panel.onLoadSoundFont = [safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe != nullptr && safe->samplerWindowChannel >= 0)
                safe->chooseSoundFontFor (safe->samplerWindowChannel);
        };
        panel.onClearProgram = [safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe == nullptr)
                return;
            const int i = safe->samplerWindowChannel;
            if (i < 0 || i >= static_cast<int> (safe->channels.size()))
                return;
            safe->channels[static_cast<size_t> (i)].program = nullptr;
            safe->strips[static_cast<size_t> (i)]->refreshFromChannel();
            safe->publishProject();
            safe->statusLabel.setText ("The channel plays its own sample again", juce::dontSendNotification);
        };
    }
    samplerWindowChannel = channelIndex;
    samplerWindow->setName ("Sampler - " + juce::String (channels[static_cast<size_t> (channelIndex)].name));
    samplerWindow->panel().refresh();
    samplerWindow->setVisible (true);
    samplerWindow->toFront (true);
}

void MainComponent::closeSamplerSettings()
{
    samplerWindow.reset();
    samplerWindowChannel = -1;
}

void MainComponent::chooseSoundFontFor (int channelIndex)
{
    library.ensureExists();
    fileChooser = std::make_unique<juce::FileChooser> ("Load a SoundFont", library.samples, "*.sf2");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<MainComponent> (this), channelIndex] (const juce::FileChooser& chooser)
    {
        if (safe != nullptr && chooser.getResult() != juce::File())
            safe->chooseSoundFontPreset (channelIndex, chooser.getResult());
    });
}

// A menu of the file's presets (by bank), or the only preset at once.
void MainComponent::chooseSoundFontPreset (int channelIndex, const juce::File& file)
{
    juce::String error;
    statusLabel.setText ("Reading " + file.getFileName() + "...", juce::dontSendNotification);
    const auto presets = listSoundFontPresets (file, error);
    if (presets.empty())
    {
        statusLabel.setText (error, juce::dontSendNotification);
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "SoundFont", error);
        return;
    }
    if (presets.size() == 1)
    {
        applySoundFont (channelIndex, file, presets.front().index);
        return;
    }

    juce::PopupMenu menu;
    menu.addSectionHeader (file.getFileName());
    std::map<int, juce::PopupMenu> banks;
    for (const auto& p : presets)
        banks[p.bank].addItem (p.index + 1, juce::String (p.number).paddedLeft ('0', 3) + "  " + juce::String (p.name));
    if (banks.size() == 1)
    {
        for (const auto& p : presets)
            menu.addItem (p.index + 1, juce::String (p.number).paddedLeft ('0', 3) + "  " + juce::String (p.name));
    }
    else
        for (auto& [bank, sub] : banks)
            menu.addSubMenu ("Bank " + juce::String (bank), sub);
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                        [safe = juce::Component::SafePointer<MainComponent> (this), channelIndex, file] (int choice)
                        {
                            if (safe != nullptr && choice > 0)
                                safe->applySoundFont (channelIndex, file, choice - 1);
                        });
}

void MainComponent::applySoundFont (int channelIndex, const juce::File& file, int presetIndex)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int> (channels.size()))
        return;
    juce::String error;
    SamplerSettings suggested;
    auto program = loadSoundFont (file, presetIndex, error, &suggested);
    if (program == nullptr)
    {
        statusLabel.setText (error, juce::dontSendNotification);
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "SoundFont", error);
        return;
    }
    auto& channel = channels[static_cast<size_t> (channelIndex)];
    channel.instrument = Instrument::Sampler;
    channel.name = (juce::String (program->name).substring (0, 16)).toStdString();
    channel.sampler = suggested;
    const auto zones = program->zones.size();
    channel.program = std::move (program);
    strips[static_cast<size_t> (channelIndex)]->refreshFromChannel();
    updateTrackRouting();
    selectChannel (channelIndex);
    publishProject();
    statusLabel.setText ("Loaded " + juce::String (channel.name) + " from " + file.getFileName() + " (" + juce::String (static_cast<int> (zones)) + " zones)",
                         juce::dontSendNotification);
    if (samplerWindow != nullptr && samplerWindowChannel == channelIndex)
        samplerWindow->panel().refresh();
}

// The sample editor's markers as a new sampler channel: the edited audio is written to a new WAV file, slice n plays on key C4 + n, and the
// current pattern gets one note per slice where the slice was (so the pattern plays the loop as it was, and the notes can be moved around).
juce::String MainComponent::slicesToSampler (const std::shared_ptr<const SampleData>& edited, const std::vector<double>& markers, const juce::String& name)
{
    if (edited == nullptr || edited->numFrames() < 2)
        return "There is no audio.";
    if (channels.size() >= static_cast<size_t> (kMaxChannels))
        return "All " + juce::String (kMaxChannels) + " channels are in use.";

    const auto file = uniqueEditedFile (library.samples.getChildFile ("Edited"), name + "-slices");
    auto out = std::make_shared<SampleData> (*edited);
    out->name = name.toStdString();
    out->source = file.getFullPathName().toStdString();
    const auto written = writeSampleWav (*out, file);
    if (written.failed())
        return written.getErrorMessage();

    std::vector<int> starts { 0 };
    for (const double s : markers)
        starts.push_back (static_cast<int> (std::lround (s * out->sampleRate)));
    auto program = std::make_shared<SamplerProgram> (makeSliceProgram (out, starts, 60, (name + " slices").toStdString()).sanitised());
    program->source = out->source;
    if (program->zones.empty())
        return "No slices could be made.";

    const int index = addChannel (Instrument::Sampler, false);
    if (index < 0)
        return "No channel could be added.";
    auto& channel = channels[static_cast<size_t> (index)];
    channel.name = (name.substring (0, 10) + " sl").toStdString();
    channel.sample = out;
    channel.program = program;
    channel.rootPitch = 60;

    // One note per slice, at the slice's own time (as far as the pattern reaches).
    const double bpm = bpmSlider.getValue();
    channel.notes.clear();
    int frame = 0;
    for (size_t i = 0; i < program->zones.size(); ++i)
    {
        const int length = program->zones[i].sample->numFrames();
        const int start = static_cast<int> (std::lround (secondsToTicks (frame / out->sampleRate, bpm)));
        const int ticks = std::max (1, static_cast<int> (std::lround (secondsToTicks (length / out->sampleRate, bpm))));
        frame += length;
        if (start >= arrangement.patternTicks)
            break;
        channel.notes.push_back ({ program->zones[i].loKey, start, std::min (ticks, arrangement.patternTicks - start), 0.8f });
    }
    strips[static_cast<size_t> (index)]->refreshFromChannel();
    selectChannel (index);
    publishProject();
    statusLabel.setText ("New sampler channel with " + juce::String (static_cast<int> (program->zones.size())) + " slices on the keys from C5, and one note per slice in "
                             + juce::String (arrangement.patterns[static_cast<size_t> (currentPattern)].name),
                         juce::dontSendNotification);
    return {};
}
} // namespace sls
