// Part of MainComponent: the sampler settings window, SoundFonts and slices turned into a sampler channel (0.16.0).
#include "ui/MainComponent.h"

#include <thread>

#include "audio/SampleEdit.h"
#include "audio/ClipEdit.h"
#include "library/SoundFont.h"
#include "audio/SpeechSynth.h"

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
        panel.onSpeech = [safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe != nullptr && safe->samplerWindowChannel >= 0)
                safe->openSpeechDialog (safe->samplerWindowChannel);
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

// The speech synthesizer (0.74.0): a text, a voice, its pitch, speed and intonation; the channel then plays the text, spoken. The
// dialog opens with what the channel says already, when it is a spoken sample.
void MainComponent::openSpeechDialog (int channelIndex)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int> (channels.size()))
        return;
    std::string text = "Ciao, benvenuto in Sun Loops Studio!";
    SpeechSettings spoken;
    if (const auto& sample = channels[static_cast<size_t> (channelIndex)].sample; sample != nullptr && sample->source.rfind ("builtin:", 0) == 0)
    {
        std::string said;
        if (SpeechSettings got; parseSpeechId (sample->source.substr (8), said, got))
        {
            text = said;
            spoken = got;
        }
    }
    auto* window = new juce::AlertWindow ("Speech synthesizer",
                                          "Type what the channel should say. It is read the Italian way (every letter as written, the stress on "
                                          "the second-last vowel unless an accent says otherwise; numbers as Italian words); a comma pauses, a "
                                          "full stop ends a sentence, a question mark makes it rise.",
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("text", juce::String::fromUTF8 (text.c_str()), "Text");
    juce::StringArray voices;
    for (int v = 0; v < kSpeechVoices; ++v)
        voices.add (speechVoiceName (static_cast<SpeechVoice> (v)));
    window->addComboBox ("voice", voices, "Voice");
    juce::StringArray pitches;
    for (int st = -12; st <= 12; ++st)
        pitches.add ((st > 0 ? "+" : "") + juce::String (st) + " semitones");
    window->addComboBox ("pitch", pitches, "Pitch");
    window->addComboBox ("speed", { "Very slow (50 %)", "Slow (75 %)", "Normal", "Fast (125 %)", "Faster (150 %)", "Very fast (200 %)" }, "Speed");
    window->addComboBox ("intonation", { "Flat (a monotone)", "Calm", "Normal", "Lively" }, "Intonation");
    window->getComboBoxComponent ("voice")->setSelectedItemIndex (static_cast<int> (spoken.voice), juce::dontSendNotification);
    window->getComboBoxComponent ("pitch")->setSelectedItemIndex (juce::roundToInt (spoken.pitch) + 12, juce::dontSendNotification);
    static constexpr float kSpeeds[] = { 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
    static constexpr float kIntonations[] = { 0.0f, 0.3f, 0.5f, 0.85f };
    int speed = 2, intonation = 2;
    for (int i = 0; i < 6; ++i)
        if (std::abs (kSpeeds[i] - spoken.speed) < std::abs (kSpeeds[speed] - spoken.speed))
            speed = i;
    for (int i = 0; i < 4; ++i)
        if (std::abs (kIntonations[i] - spoken.intonation) < std::abs (kIntonations[intonation] - spoken.intonation))
            intonation = i;
    window->getComboBoxComponent ("speed")->setSelectedItemIndex (speed, juce::dontSendNotification);
    window->getComboBoxComponent ("intonation")->setSelectedItemIndex (intonation, juce::dontSendNotification);
    window->addButton ("Speak", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create (
                                       [safe = juce::Component::SafePointer<MainComponent> (this), window, channelIndex] (int result)
                                       {
                                           if (safe == nullptr || result != 1 || channelIndex >= static_cast<int> (safe->channels.size()))
                                               return;
                                           SpeechSettings s;
                                           s.voice = static_cast<SpeechVoice> (std::max (0, window->getComboBoxComponent ("voice")->getSelectedItemIndex()));
                                           s.pitch = static_cast<float> (std::max (0, window->getComboBoxComponent ("pitch")->getSelectedItemIndex()) - 12);
                                           s.speed = kSpeeds[std::clamp (window->getComboBoxComponent ("speed")->getSelectedItemIndex(), 0, 5)];
                                           s.intonation = kIntonations[std::clamp (window->getComboBoxComponent ("intonation")->getSelectedItemIndex(), 0, 3)];
                                           const auto said = window->getTextEditorContents ("text").trim().substring (0, 600).toStdString();
                                           auto sample = speak (said, s);
                                           if (sample == nullptr)
                                           {
                                               safe->statusLabel.setText ("Nothing to say: type some words", juce::dontSendNotification);
                                               return;
                                           }
                                           auto& channel = safe->channels[static_cast<size_t> (channelIndex)];
                                           channel.instrument = Instrument::Sampler;
                                           channel.name = juce::String::fromUTF8 (said.c_str()).substring (0, 16).toStdString();
                                           channel.sample = std::move (sample);
                                           channel.program = nullptr;
                                           safe->strips[static_cast<size_t> (channelIndex)]->refreshFromChannel();
                                           safe->updateTrackRouting();
                                           safe->publishProject();
                                           if (safe->samplerWindow != nullptr)
                                               safe->samplerWindow->panel().refresh();
                                           safe->statusLabel.setText ("The channel says: " + juce::String::fromUTF8 (said.c_str()) + "  ("
                                                                          + juce::String (speechPhonemes (said)) + ")",
                                                                      juce::dontSendNotification);
                                       }),
                             true);
}

void MainComponent::closeSamplerSettings()
{
    samplerWindow.reset();
    samplerWindowChannel = -1;
}

void MainComponent::chooseSoundFontFor (int channelIndex)
{
    library.ensureExists();
    fileChooser = std::make_unique<juce::FileChooser> ("Load a SoundFont or an SFZ instrument", library.samples, "*.sf2;*.sfz");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<MainComponent> (this), channelIndex] (const juce::FileChooser& chooser)
    {
        if (safe == nullptr || chooser.getResult() == juce::File())
            return;
        if (chooser.getResult().hasFileExtension ("sfz"))
            safe->applySfz (channelIndex, chooser.getResult());
        else
            safe->chooseSoundFontPreset (channelIndex, chooser.getResult());
    });
}

// An SFZ instrument on a sampler channel (0.41.0): its regions become the channel's zones; what it uses that this sampler does not
// have is listed in the status line.
void MainComponent::applySfz (int channelIndex, const juce::File& file)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int> (channels.size()))
        return;
    juce::String error;
    juce::StringArray notes;
    statusLabel.setText ("Reading " + file.getFileName() + "...", juce::dontSendNotification);
    auto program = loadSfz (file, [this] (const juce::File& f, juce::String& e) { return decodeSample (formats, f, e); }, error, &notes);
    if (program == nullptr)
    {
        statusLabel.setText (error, juce::dontSendNotification);
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "SFZ", error);
        return;
    }
    auto& channel = channels[static_cast<size_t> (channelIndex)];
    channel.instrument = Instrument::Sampler;
    channel.name = (juce::String (program->name).substring (0, 16)).toStdString();
    const auto zones = program->zones.size();
    channel.sampler.envelope = true; // an SFZ instrument plays while the key is held, like a SoundFont
    channel.program = std::move (program);
    strips[static_cast<size_t> (channelIndex)]->refreshFromChannel();
    updateTrackRouting();
    selectChannel (channelIndex);
    publishProject();
    statusLabel.setText ("Loaded " + file.getFileName() + " (" + juce::String (static_cast<int> (zones)) + " zones)"
                             + (notes.isEmpty() ? juce::String() : ". " + notes.joinIntoString ("; ")),
                         juce::dontSendNotification);
    if (samplerWindow != nullptr && samplerWindowChannel == channelIndex)
        samplerWindow->panel().refresh();
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
    if (soundFontLoading)
    {
        statusLabel.setText ("A SoundFont is still loading; try again when it is ready.", juce::dontSendNotification);
        return;
    }
    // A large SoundFont takes a while to read (0.47.0): it is read on its own thread, so the window keeps responding.
    soundFontLoading = true;
    statusLabel.setText ("Loading " + file.getFileName() + "...", juce::dontSendNotification);
    std::thread ([safe = juce::Component::SafePointer<MainComponent> (this), channelIndex, file, presetIndex]
    {
        auto error = std::make_shared<juce::String>();
        auto suggested = std::make_shared<SamplerSettings>();
        auto program = loadSoundFont (file, presetIndex, *error, suggested.get());
        juce::MessageManager::callAsync ([safe, channelIndex, file, program, error, suggested]
        {
            if (safe == nullptr)
                return;
            safe->soundFontLoading = false;
            safe->useSoundFont (channelIndex, file, program, *error, *suggested);
        });
    }).detach();
}

void MainComponent::useSoundFont (int channelIndex, const juce::File& file, std::shared_ptr<const SamplerProgram> program, const juce::String& error,
                                  const SamplerSettings& suggested)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int> (channels.size()))
        return;
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
    program->sliceCut = true;                    // as in the Slicer (0.37.0)
    program->sliceBpm = bpmSlider.getValue();    // the notes below are timed at this tempo
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
        if (start >= currentPatternTicks())
            break;
        channel.notes.push_back ({ program->zones[i].loKey, start, std::min (ticks, currentPatternTicks() - start), 0.8f });
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
