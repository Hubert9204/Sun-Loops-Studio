// Part of MainComponent: the pitch editor (0.38.0) on an audio clip.
#include "ui/MainComponent.h"

#include "audio/ClipEdit.h"
#include "audio/SampleEdit.h"

namespace sls
{
// "Edit pitch..." on an audio clip: like the sample editor, it works on the part of the audio the clip plays, and Apply writes a NEW
// WAV file (the original is never touched) that the clip then plays.
void MainComponent::openPitchEditorForClip (int index)
{
    if (index < 0 || index >= static_cast<int> (arrangement.audioClips.size()))
        return;
    const auto clip = arrangement.audioClips[static_cast<size_t> (index)];
    const auto refuse = [] (const juce::String& message)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Pitch editor", message);
    };
    if (clip.data == nullptr || clip.data->numFrames() < 2)
        return refuse ("This clip has no audio (its file is missing).");
    if (! clip.stretch.isNeutral())
        return refuse ("This clip is stretched or pitch-shifted. Choose Stretch and pitch... > Back to the original, edit the pitch here, "
                       "then stretch it again.");
    if (clip.reverse)
        return refuse ("This clip plays its audio backwards. Switch Reverse off (right-click the clip) first.");
    if (pitchEditorWindow != nullptr)
    {
        pitchEditorWindow->toFront (true);
        return refuse ("The pitch editor is already open: apply or cancel that edit first.");
    }

    auto copy = playedPartOf (clip);
    if (copy == nullptr)
        return;

    pitchEditorWindow = std::make_unique<PitchEditorWindow> (engine);
    auto* window = pitchEditorWindow.get();
    window->setName ("Pitch editor - " + juce::String (clip.name));
    window->onClose = [safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->pitchEditorWindow.reset(); });
    };
    window->editor().onClose = window->onClose;
    const auto expected = clip.data;
    const auto name = clip.name;
    const auto folder = library.samples.getChildFile ("Edited");
    window->editor().onApply = [safe = juce::Component::SafePointer<MainComponent> (this), index, expected, name, folder] (const std::shared_ptr<const SampleData>& edited) -> juce::String
    {
        if (safe == nullptr)
            return "The program is closing.";
        if (edited == nullptr)
            return "Nothing to save.";
        const auto file = uniqueEditedFile (folder, name + "-pitch");
        auto out = std::make_shared<SampleData> (*edited);
        out->name = name;
        out->source = file.getFullPathName().toStdString();
        const auto written = writeSampleWav (*out, file);
        if (written.failed())
            return written.getErrorMessage();
        const auto error = safe->playlistView.useEditedAudio (index, expected, out, {});
        if (error.isEmpty())
            safe->statusLabel.setText ("Saved " + file.getFullPathName() + ": the clip plays the tuned audio", juce::dontSendNotification);
        return error;
    };
    window->editor().onNotesToPattern = [safe = juce::Component::SafePointer<MainComponent> (this)] (const std::vector<pitchtrack::Note>& notes) -> juce::String
    {
        return safe != nullptr ? safe->pitchNotesToPattern (notes) : juce::String ("The program is closing.");
    };
    window->editor().setSample (copy);
    window->setVisible (true);
    window->toFront (true);
}

// The notes of a line as the selected channel's notes in the current pattern (pitch to MIDI): each at its nearest key, from the
// pattern's start, its velocity from its level.
juce::String MainComponent::pitchNotesToPattern (const std::vector<pitchtrack::Note>& notes)
{
    if (selected < 0 || selected >= static_cast<int> (channels.size()))
        return "Select a channel in the Channel Rack first.";
    if (notes.empty())
        return "There are no notes.";
    double loudest = 0.0;
    for (const auto& n : notes)
        loudest = std::max (loudest, n.level);
    const double bpm = bpmSlider.getValue();
    std::vector<Note> written;
    for (const auto& n : notes)
    {
        const int start = static_cast<int> (std::lround (secondsToTicks (n.start, bpm)));
        if (start >= currentPatternTicks())
            break;
        const int length = std::max (1, static_cast<int> (std::lround (secondsToTicks (n.end - n.start, bpm))));
        const float velocity = loudest > 0.0 ? static_cast<float> (0.4 + 0.6 * std::sqrt (n.level / loudest)) : 0.8f;
        written.push_back ({ n.nearestKey(), start, std::min (length, currentPatternTicks() - start), std::clamp (velocity, 0.05f, 1.0f) });
    }
    auto& channel = channels[static_cast<size_t> (selected)];
    channel.notes = written;
    channel.steps = 0;
    strips[static_cast<size_t> (selected)]->refreshFromChannel();
    selectChannel (selected); // shows the notes in the piano roll
    publishProject();
    statusLabel.setText (juce::String (static_cast<int> (written.size())) + " notes written for " + juce::String (channel.name) + " in "
                             + juce::String (arrangement.patterns[static_cast<size_t> (currentPattern)].name),
                         juce::dontSendNotification);
    return {};
}
} // namespace sls
