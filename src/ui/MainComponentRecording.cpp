// Part of MainComponent: recording audio from the input (several tracks at once since 0.36.0).
#include "ui/MainComponent.h"

#include "ui/AudioSettings.h"

#include <algorithm>
#include <cmath>

namespace sls
{
// ------------------------------------------------------------------------- Recording

namespace
{
constexpr int kInputItemBase = 1000;  // menu ids: kInputItemBase + track * kInputItemsPerTrack + 2 * input (+1 for a stereo pair)
constexpr int kInputItemsPerTrack = 2 * kMaxRecordInputs;
} // namespace

void MainComponent::updateRecordButton()
{
    recordButton.setToggleState (recording, juce::dontSendNotification);
    recordButton.setButtonText (recording ? "Stop rec" : "Record");
}

juce::String MainComponent::recordInputLabel (int track) const
{
    const auto& input = recordSetup.tracks[static_cast<size_t> (std::clamp (track, 0, kPlaylistTracks - 1))];
    return input.stereo ? "inputs " + juce::String (input.firstInput + 1) + "+" + juce::String (input.firstInput + 2)
                        : "input " + juce::String (input.firstInput + 1);
}

juce::String MainComponent::armedTracksText() const
{
    juce::StringArray names;
    for (const int t : recordSetup.armedTracks())
        names.add (playlistView.trackName (t) + " (" + recordInputLabel (t) + ")");
    return names.joinIntoString (", ");
}

void MainComponent::applyRecordSetup (bool announce)
{
    std::array<bool, kPlaylistTracks> arms {};
    for (int t = 0; t < kPlaylistTracks; ++t)
        arms[static_cast<size_t> (t)] = recordSetup.tracks[static_cast<size_t> (t)].armed;
    playlistView.setRecordArms (arms);
    if (settings != nullptr)
    {
        settings->setValue ("record.arms", juce::String (recordSetup.toString()));
        settings->saveIfNeeded();
    }
    if (announce && ! recording)
        statusLabel.setText ("Record will write to " + armedTracksText(), juce::dontSendNotification);
}

void MainComponent::selectPlaylistTrack (int track)
{
    // With one track armed, clicking another track's name moves the arm there (as before 0.36.0); with several, it only selects.
    if (recording || recordSetup.armedTracks().size() != 1)
        return;
    recordSetup.armOnly (std::clamp (track, 0, kPlaylistTracks - 1));
    applyRecordSetup();
}

void MainComponent::toggleRecordArm (int track)
{
    if (recording || track < 0 || track >= kPlaylistTracks)
        return;
    recordSetup.toggle (track);
    applyRecordSetup();
}

void MainComponent::addRecordInputItems (juce::PopupMenu& menu, int track)
{
    const auto names = engine.getInputChannelNames();
    const int count = names.isEmpty() ? 8 : std::min (names.size(), kMaxRecordInputs); // no device input known yet: offer eight
    const auto& current = recordSetup.tracks[static_cast<size_t> (track)];
    const int base = kInputItemBase + track * kInputItemsPerTrack;
    const auto nameOf = [&names] (int i)
    {
        const auto name = names[i].trim();
        const auto number = juce::String (i + 1);
        const bool plain = name.isEmpty() || name.equalsIgnoreCase ("Input " + number) || name.equalsIgnoreCase ("channel " + number)
                        || name.equalsIgnoreCase ("In " + number) || name == number;
        return plain ? juce::String() : " (" + name + ")";
    };

    juce::PopupMenu mono, stereo;
    for (int i = 0; i < count; ++i)
        mono.addItem (base + 2 * i, "Input " + juce::String (i + 1) + nameOf (i), ! recording, ! current.stereo && current.firstInput == i);
    for (int i = 0; i + 1 < count; i += 2) // pairs as interfaces label them: 1+2, 3+4...
        stereo.addItem (base + 2 * i + 1, "Inputs " + juce::String (i + 1) + " + " + juce::String (i + 2), ! recording,
                        current.stereo && current.firstInput == i);
    if (current.stereo && current.firstInput % 2 == 1 && current.firstInput + 1 < count) // an odd pair from a saved setting
        stereo.addItem (base + 2 * current.firstInput + 1, "Inputs " + juce::String (current.firstInput + 1) + " + "
                        + juce::String (current.firstInput + 2), ! recording, true);

    const auto heading = juce::String (names.isEmpty() ? " (no input open yet: by number)" : "");
    if (count <= 8)
    {
        menu.addSectionHeader ("Mono" + heading);
        for (juce::PopupMenu::MenuItemIterator it (mono); it.next();)
            menu.addItem (it.getItem());
        if (count >= 2)
        {
            menu.addSectionHeader ("Stereo");
            for (juce::PopupMenu::MenuItemIterator it (stereo); it.next();)
                menu.addItem (it.getItem());
        }
    }
    else
    {
        menu.addSubMenu ("Mono: " + (current.stereo ? juce::String ("one input") : "input " + juce::String (current.firstInput + 1)) + heading,
                         mono, ! recording, nullptr, ! current.stereo);
        menu.addSubMenu ("Stereo: " + (current.stereo ? "inputs " + juce::String (current.firstInput + 1) + " + " + juce::String (current.firstInput + 2)
                                                      : juce::String ("two inputs")),
                         stereo, ! recording, nullptr, current.stereo);
    }
}

bool MainComponent::handleRecordInputItem (int result)
{
    if (result < kInputItemBase || result >= kInputItemBase + kPlaylistTracks * kInputItemsPerTrack || recording)
        return false;
    const int track = (result - kInputItemBase) / kInputItemsPerTrack;
    const int item = (result - kInputItemBase) % kInputItemsPerTrack;
    auto& input = recordSetup.tracks[static_cast<size_t> (track)];
    input.firstInput = item / 2;
    input.stereo = (item % 2) == 1;
    applyRecordSetup();
    return true;
}

void MainComponent::showTrackInputMenu (int track)
{
    if (track < 0 || track >= kPlaylistTracks)
        return;
    juce::PopupMenu menu;
    menu.addSectionHeader ("Recording on " + playlistView.trackName (track));
    menu.addItem (1, "Record on this track", ! recording, recordSetup.tracks[static_cast<size_t> (track)].armed);
    menu.addSeparator();
    addRecordInputItems (menu, track);
    menu.addSeparator();
    menu.addItem (2, "Audio and MIDI devices...");
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                        [safe = juce::Component::SafePointer<MainComponent> (this), track] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result == 1)
                                safe->toggleRecordArm (track);
                            else if (result == 2)
                                safe->openAudioSettings();
                            else
                                safe->handleRecordInputItem (result);
                        });
}

void MainComponent::openRecordMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("The recording becomes");
    menu.addItem (40, "Clips on the armed playlist tracks", ! recording, recordTarget == RecordTarget::PlaylistTrack);
    menu.addItem (41, "New sampler channels (play them from the piano roll)", ! recording, recordTarget == RecordTarget::SamplerChannel);
    menu.addSeparator();
    menu.addSectionHeader ("Armed tracks (all are recorded together)");
    juce::PopupMenu inputs;
    for (int t = 0; t < kPlaylistTracks; ++t)
    {
        menu.addItem (10 + t, playlistView.trackName (t) + "   - " + recordInputLabel (t), ! recording,
                      recordSetup.tracks[static_cast<size_t> (t)].armed);
        juce::PopupMenu one;
        addRecordInputItems (one, t);
        inputs.addSubMenu (playlistView.trackName (t) + "   - " + recordInputLabel (t), one, ! recording);
    }
    menu.addSubMenu ("Input of each track", inputs, ! recording);
    menu.addSeparator();
    menu.addSectionHeader ("The takes start");
    menu.addItem (50, "At the start of the song", ! recording, ! recordFromPlayhead);
    menu.addItem (51, "Where the song is (punch in)", ! recording, recordFromPlayhead);
    menu.addSeparator();
    menu.addItem (32, "Hear the input while recording (use headphones)", true, monitorInput);
    menu.addItem (33, "Audio and MIDI devices...");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&recordMenuButton),
                        [safe = juce::Component::SafePointer<MainComponent> (this)] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result >= 10 && result < 10 + kPlaylistTracks)
                                safe->toggleRecordArm (result - 10);
                            else if (safe->handleRecordInputItem (result))
                                return;
                            else if (result == 32)
                            {
                                safe->monitorInput = ! safe->monitorInput;
                                safe->engine.setInputMonitoring (safe->monitorInput);
                                if (safe->settings != nullptr)
                                {
                                    safe->settings->setValue ("record.monitor", safe->monitorInput);
                                    safe->settings->saveIfNeeded();
                                }
                            }
                            else if (result == 40 || result == 41)
                            {
                                safe->recordTarget = result == 41 ? RecordTarget::SamplerChannel : RecordTarget::PlaylistTrack;
                                if (safe->settings != nullptr)
                                {
                                    safe->settings->setValue ("record.target", result == 41 ? 1 : 0);
                                    safe->settings->saveIfNeeded();
                                }
                            }
                            else if (result == 50 || result == 51)
                            {
                                safe->recordFromPlayhead = result == 51;
                                if (safe->settings != nullptr)
                                {
                                    safe->settings->setValue ("record.fromPlayhead", safe->recordFromPlayhead);
                                    safe->settings->saveIfNeeded();
                                }
                            }
                            else if (result == 33)
                                safe->openAudioSettings();
                        });
}

void MainComponent::toggleRecording()
{
    if (recording)
        finishRecording ({});
    else
        startRecording();
}

void MainComponent::startRecording()
{
    const auto fail = [this] (const juce::String& message)
    {
        statusLabel.setText (message, juce::dontSendNotification);
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Record", message);
    };

    const auto armed = recordSetup.armedTracks();
    if (armed.empty() || armed.size() > static_cast<size_t> (AudioEngine::kMaxRecordStreams))
    {
        fail ("Arm a playlist track first: click the dot after its name.");
        return;
    }

    // A take on the playlist starts at the start of the song or where the song is, and needs some room before the end.
    int from = 0;
    if (recordFromPlayhead)
        from = std::clamp (songMode && engine.isPlaying() ? engine.getPlayheadTick() : songStartTick, 0, arrangement.timelineTicks() - 1);
    if (recordTarget == RecordTarget::PlaylistTrack)
        for (const int t : armed)
            if (arrangement.maxAudioLengthAt (t, from, arrangement.timelineTicks()) < kTicksPerBeat)
            {
                fail ("There is no room on " + playlistView.trackName (t) + " before the end of the playlist. Move the song position "
                      "back, or record from the start of the song.");
                return;
            }

    if (const auto error = engine.enableInputChannels (recordSetup.neededInputs()); error.isNotEmpty())
    {
        fail (error);
        return;
    }

    library.ensureExists();
    const auto folder = library.samples.getChildFile ("Recordings");
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
    std::vector<AudioEngine::RecordStream> streams;
    for (const int t : armed)
    {
        const auto base = "Recording " + stamp + (armed.size() > 1 ? " T" + juce::String (t + 1) : juce::String());
        auto file = folder.getChildFile (base + ".wav");
        for (int n = 2; file.exists(); ++n)
            file = folder.getChildFile (base + " (" + juce::String (n) + ").wav");
        const auto& input = recordSetup.tracks[static_cast<size_t> (t)];
        streams.push_back ({ file, input.firstInput, input.stereo ? InputRecorder::Channels::Stereo : InputRecorder::Channels::Mono });
    }

    if (recordTarget == RecordTarget::PlaylistTrack && ! songMode)
        setSongMode (true); // the playlist is what plays while recording, and the takes are part of it
    engine.setInputMonitoring (monitorInput);

    if (const auto error = engine.startRecording (streams, ! recordFromPlayhead); error.isNotEmpty())
    {
        fail (error);
        return;
    }

    recordingTracks = armed;
    recording = true;
    playButton.setToggleState (true, juce::dontSendNotification);
    playButton.setButtonText ("Stop");
    updateRecordButton();
    showRecordProgress();
}

void MainComponent::showRecordProgress()
{
    const double seconds = engine.getRecordedSeconds();
    const int whole = static_cast<int> (seconds);
    float loudest = 0.0f;
    juce::StringArray clipping;
    for (size_t i = 0; i < recordingTracks.size(); ++i)
    {
        const float peak = engine.consumeStreamPeak (static_cast<int> (i));
        loudest = std::max (loudest, peak);
        if (peak >= 0.98f)
            clipping.add (playlistView.trackName (recordingTracks[i]));
    }
    const auto level = loudest > 0.0f ? juce::String (juce::Decibels::gainToDecibels (loudest), 0) + " dB" : juce::String ("silence");
    juce::StringArray names;
    for (const int t : recordingTracks)
        names.add (playlistView.trackName (t));
    statusLabel.setText ("Recording on " + names.joinIntoString (", ") + "   " + juce::String (whole / 60) + ":"
                             + juce::String (whole % 60).paddedLeft ('0', 2) + "   input " + level
                             + (clipping.isEmpty() ? juce::String() : "  (clipping on " + clipping.joinIntoString (", ") + "!)"),
                         juce::dontSendNotification);

    // A take cannot go past the end of the playlist (or the longest audio the program keeps in memory), and stops if the song
    // jumps back (a loop region): what comes after would no longer line up with the song.
    const double bpm = std::max (1.0, bpmSlider.getValue());
    const double start = engine.getRecordStartTick();
    const double startTick = std::max (0.0, start);
    const double limit = std::min (kMaxImportSeconds, (arrangement.timelineTicks() - startTick) * 60.0 / (bpm * kTicksPerBeat));
    if (seconds >= limit)
        finishRecording ("The takes reached the end of the playlist.");
    else if (start >= 0.0 && engine.getPlayheadTick() + kTicksPerBeat < startTick + seconds * bpm / 60.0 * kTicksPerBeat)
        finishRecording ("The song jumped back (a loop region?), so the takes stop there.");
}

void MainComponent::finishRecording (const juce::String& why)
{
    if (! recording)
        return;
    recording = false;

    const auto takes = engine.stopRecording();
    const int startTick = std::clamp (static_cast<int> (std::lround (std::max (0.0, engine.getRecordStartTick()))), 0,
                                      std::max (0, arrangement.timelineTicks() - 1));
    updateRecordButton();
    setPlaying (false);

    juce::StringArray placedOn, problems;
    double longest = 0.0;
    bool dropped = false;
    bool changedPlaylist = false;
    int firstNewChannel = -1;

    for (size_t i = 0; i < takes.size() && i < recordingTracks.size(); ++i)
    {
        const auto& take = takes[i];
        const int track = recordingTracks[i];
        if (take.frames < 2)
        {
            take.file.deleteFile();
            continue;
        }
        dropped = dropped || take.droppedBlocks > 0;
        longest = std::max (longest, static_cast<double> (take.frames) / take.sampleRate);

        auto result = importAudioFile (take.file);
        if (result.data == nullptr)
        {
            problems.add ("the take of " + playlistView.trackName (track) + " was saved as " + take.file.getFullPathName()
                          + " but could not be read back: " + result.error);
            continue;
        }
        auto data = std::make_shared<SampleData> (*result.data);
        data->name = take.file.getFileNameWithoutExtension().replace ("Recording ", "Rec ").toStdString();

        // New sampler channels: each take is a channel's sample, ready to be played from the piano roll.
        if (recordTarget == RecordTarget::SamplerChannel)
        {
            const int index = addChannel (Instrument::Sampler, false);
            if (index >= 0)
            {
                auto& channel = channels[static_cast<size_t> (index)];
                channel.sample = data;
                channel.name = (juce::Time::getCurrentTime().formatted ("Rec %H-%M-%S")
                                + (takes.size() > 1 ? " T" + juce::String (track + 1) : juce::String())).toStdString();
                channel.rootPitch = 60;
                strips[static_cast<size_t> (index)]->refreshFromChannel();
                if (firstNewChannel < 0)
                    firstNewChannel = index;
                placedOn.add ("channel \"" + juce::String (channel.name) + "\"");
                continue;
            }
            problems.add ("there was no free channel, so the take of " + playlistView.trackName (track) + " went to the playlist");
        }

        // On its track, where the takes started, as long as the take (cut to the room left before the end of the playlist).
        AudioClip clip;
        clip.data = data;
        clip.name = data->name;
        clip.track = track;
        clip.startTick = startTick;
        const int natural = clip.naturalLengthTicks (bpmSlider.getValue());
        clip.lengthTicks = arrangement.maxAudioLengthAt (track, startTick, natural);
        juce::String note;
        if (clip.lengthTicks >= 1 && arrangement.audioClips.size() < static_cast<size_t> (kMaxAudioClips))
        {
            arrangement.audioClips.push_back (clip);
            placedOn.add (playlistView.trackName (track));
            changedPlaylist = true;
        }
        else if (addAudioClip (data, track, startTick, note))
        {
            placedOn.add (playlistView.trackName (track));
            changedPlaylist = true;
        }
        else
            problems.add ("the take of " + playlistView.trackName (track) + " was saved as " + take.file.getFullPathName()
                          + " but did not fit in the playlist");
    }
    recordingTracks.clear();

    juce::String status = placedOn.isEmpty() ? juce::String ("Nothing was recorded")
                                             : "Recorded " + juce::String (longest, 1) + " s on " + placedOn.joinIntoString (", ");
    if (firstNewChannel >= 0)
    {
        selectChannel (firstNewChannel);
        status += ": open a channel's piano roll and play it, or draw notes";
    }
    if (why.isNotEmpty())
        status += ". " + why;
    if (dropped)
        status += " Warning: some audio was lost because the disk was too slow.";
    if (! problems.isEmpty())
        status += " Note: " + problems.joinIntoString ("; ") + ".";
    statusLabel.setText (status, juce::dontSendNotification);

    if (changedPlaylist)
    {
        playlistView.clearHistory(); // the arrangement was changed from outside: undo could not restore it safely
        playlistView.repaint();
    }
    if (changedPlaylist || firstNewChannel >= 0)
        publishProject();
}

} // namespace sls
