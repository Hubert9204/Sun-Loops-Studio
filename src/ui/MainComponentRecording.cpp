// Part of MainComponent: recording audio from the input.
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
// ------------------------------------------------------------------------- Recording

void MainComponent::updateRecordButton()
{
    recordButton.setToggleState (recording, juce::dontSendNotification);
    recordButton.setButtonText (recording ? "Stop rec" : "Record");
}

void MainComponent::setRecordTrack (int track)
{
    recordTrack = std::clamp (track, 0, kPlaylistTracks - 1);
    playlistView.setArmedTrack (recordTrack);
    if (settings != nullptr)
    {
        settings->setValue ("record.track", recordTrack);
        settings->saveIfNeeded();
    }
    if (! recording)
        statusLabel.setText ("Record will write to Track " + juce::String (recordTrack + 1), juce::dontSendNotification);
}

void MainComponent::openRecordMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("The recording becomes");
    menu.addItem (40, "A clip on a playlist track (choose it below)", ! recording, recordTarget == RecordTarget::PlaylistTrack);
    menu.addItem (41, "A new sampler channel (play it from the piano roll)", ! recording, recordTarget == RecordTarget::SamplerChannel);
    menu.addSeparator();
    menu.addSectionHeader ("Playlist track");
    for (int t = 0; t < kPlaylistTracks; ++t)
        menu.addItem (10 + t, "Track " + juce::String (t + 1), ! recording, t == recordTrack);
    menu.addSeparator();
    menu.addSectionHeader ("Input");
    menu.addItem (30, "Mono: first input (microphone, guitar)", ! recording, recordChannels == InputRecorder::Channels::Mono);
    menu.addItem (31, "Stereo: inputs 1 and 2", ! recording, recordChannels == InputRecorder::Channels::Stereo);
    menu.addSeparator();
    menu.addItem (32, "Hear the input while recording (use headphones)", true, monitorInput);
    menu.addItem (33, "Audio and MIDI devices...");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&recordMenuButton),
                        [safe = juce::Component::SafePointer<MainComponent> (this)] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result >= 10 && result < 10 + kPlaylistTracks)
                                safe->setRecordTrack (result - 10);
                            else if (result == 30 || result == 31)
                            {
                                safe->recordChannels = result == 31 ? InputRecorder::Channels::Stereo : InputRecorder::Channels::Mono;
                                if (safe->settings != nullptr)
                                {
                                    safe->settings->setValue ("record.stereo", result == 31);
                                    safe->settings->saveIfNeeded();
                                }
                            }
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

    // A take that goes to the playlist starts at the start of its track, so the track must be free there.
    if (recordTarget == RecordTarget::PlaylistTrack && arrangement.maxAudioLengthAt (recordTrack, 0, arrangement.timelineTicks()) < kTicksPerBeat)
    {
        fail ("Track " + juce::String (recordTrack + 1) + " is not empty at the start of the playlist. Click the name of an empty "
              "track in the playlist to choose where to record.");
        return;
    }

    if (const auto error = engine.enableDefaultInput(); error.isNotEmpty())
    {
        fail (error);
        return;
    }

    library.ensureExists();
    const auto folder = library.samples.getChildFile ("Recordings");
    auto file = folder.getChildFile ("Recording " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S") + ".wav");
    for (int n = 2; file.exists(); ++n)
        file = folder.getChildFile ("Recording " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S") + " (" + juce::String (n) + ").wav");

    if (recordTarget == RecordTarget::PlaylistTrack && ! songMode)
        setSongMode (true); // the playlist is what plays while recording, and the take is part of it
    engine.setInputMonitoring (monitorInput);

    if (const auto error = engine.startRecording (file, recordChannels); error.isNotEmpty())
    {
        fail (error);
        return;
    }

    recording = true;
    playButton.setToggleState (true, juce::dontSendNotification);
    playButton.setButtonText ("Stop");
    updateRecordButton();
    showRecordProgress();
}

void MainComponent::showRecordProgress()
{
    const int seconds = static_cast<int> (engine.getRecordedSeconds());
    const float peak = engine.consumeInputPeak();
    const auto level = peak > 0.0f ? juce::String (juce::Decibels::gainToDecibels (peak), 0) + " dB" : juce::String ("silence");
    statusLabel.setText ("Recording on Track " + juce::String (recordTrack + 1) + "   " + juce::String (seconds / 60) + ":"
                             + juce::String (seconds % 60).paddedLeft ('0', 2) + "   input " + level + (peak >= 0.98f ? "  (clipping!)" : ""),
                         juce::dontSendNotification);

    // A take cannot go past the end of the playlist (or the longest audio the program keeps in memory).
    const double bpm = std::max (1.0, bpmSlider.getValue());
    const double limit = std::min (kMaxImportSeconds, arrangement.timelineTicks() * 60.0 / (bpm * kTicksPerBeat));
    if (engine.getRecordedSeconds() >= limit)
        finishRecording ("The take reached the end of the playlist.");
}

void MainComponent::finishRecording (const juce::String& why)
{
    if (! recording)
        return;
    recording = false;

    const auto take = engine.stopRecording();
    updateRecordButton();
    setPlaying (false);

    if (take.file == juce::File() || take.frames < 2)
    {
        take.file.deleteFile();
        statusLabel.setText ("Nothing was recorded", juce::dontSendNotification);
        return;
    }

    auto result = importAudioFile (take.file);
    if (result.data == nullptr)
    {
        statusLabel.setText ("The take was saved as " + take.file.getFullPathName() + " but could not be placed: " + result.error,
                             juce::dontSendNotification);
        return;
    }

    auto data = std::make_shared<SampleData> (*result.data);
    data->name = take.file.getFileNameWithoutExtension().replace ("Recording ", "Rec ").toStdString();

    // A new sampler channel: the take is the channel's sample, ready to be played from the piano roll.
    juce::String note;
    if (recordTarget == RecordTarget::SamplerChannel)
    {
        const int index = addChannel (Instrument::Sampler, false);
        if (index >= 0)
        {
            auto& channel = channels[static_cast<size_t> (index)];
            channel.sample = data;
            channel.name = juce::Time::getCurrentTime().formatted ("Rec %H-%M-%S").toStdString();
            channel.rootPitch = 60;
            strips[static_cast<size_t> (index)]->refreshFromChannel();
            selectChannel (index);
            publishProject();
            auto status = "Recorded " + juce::String (take.frames / take.sampleRate, 1) + " s into the new sampler channel \""
                        + juce::String (channel.name) + "\": open its piano roll and play it, or draw notes";
            if (take.droppedBlocks > 0)
                status += ". Warning: some audio was lost because the disk was too slow.";
            statusLabel.setText (status, juce::dontSendNotification);
            return;
        }
        note = "There is no free channel, so the take was put on the playlist instead.";
    }

    // Exactly at the start of the track, as long as the take (cut to the free room if the playlist changed meanwhile).
    AudioClip clip;
    clip.data = data;
    clip.name = data->name;
    clip.track = recordTrack;
    clip.startTick = 0;
    const int natural = clip.naturalLengthTicks (bpmSlider.getValue());
    clip.lengthTicks = arrangement.maxAudioLengthAt (recordTrack, 0, natural);

    bool placed = false;
    if (clip.lengthTicks >= 1 && arrangement.audioClips.size() < static_cast<size_t> (kMaxAudioClips))
    {
        arrangement.audioClips.push_back (clip);
        placed = true;
    }
    else
        placed = addAudioClip (data, recordTrack, 0, note);

    auto status = placed ? "Recorded " + juce::String (take.frames / take.sampleRate, 1) + " s on Track " + juce::String (recordTrack + 1)
                         : "The take was saved as " + take.file.getFullPathName() + " but did not fit in the playlist";
    if (why.isNotEmpty())
        status += ". " + why;
    if (take.droppedBlocks > 0)
        status += " Warning: some audio was lost because the disk was too slow.";
    if (note.isNotEmpty())
        status += " " + note;
    statusLabel.setText (status, juce::dontSendNotification);

    if (placed)
    {
        playlistView.clearHistory(); // the arrangement was changed from outside: undo could not restore it safely
        playlistView.repaint();
        publishProject();
    }
}

} // namespace sls
