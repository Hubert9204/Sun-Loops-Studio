#include "ui/MainComponent.h"

#include "audio/ClipEdit.h"
#include "audio/SampleEdit.h"

namespace sls
{
// "Edit sample..." on an audio clip: the editor opens on exactly the part of the audio the clip plays (so what you see is what you
// hear), works on a copy, and on Apply writes the result to a NEW WAV file in the Samples folder (the original is never touched)
// and puts it on the clip, cut at the slice markers if there are any. The clip then plays the whole edited audio from its start.
void MainComponent::openSampleEditorForClip (int index)
{
    if (index < 0 || index >= static_cast<int> (arrangement.audioClips.size()))
        return;
    const auto clip = arrangement.audioClips[static_cast<size_t> (index)];
    if (clip.data == nullptr || clip.data->numFrames() < 2)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Sample editor", "This clip has no audio (its file is missing).");
        return;
    }
    if (! clip.stretch.isNeutral())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Sample editor",
                                                "This clip is stretched or pitch-shifted. Edit the sound first: choose Stretch and pitch... > Back to the original, "
                                                "edit it here, then stretch it again.");
        return;
    }
    if (sampleEditorWindow != nullptr)
    {
        sampleEditorWindow->toFront (true);
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Sample editor", "The sample editor is already open: apply or cancel that edit first.");
        return;
    }

    // The part the clip plays.
    const double rate = clip.data->sampleRate;
    const int first = static_cast<int> (std::lround (clip.offsetSeconds * rate));
    const int count = std::max (2, static_cast<int> (std::lround (ticksToSeconds (clip.lengthTicks, bpmSlider.getValue()) * rate)));
    auto region = cropToRange (clip.data, { first, first + count });
    if (region == nullptr)
        return;
    auto copy = std::make_shared<SampleData> (*region);
    copy->name = clip.name;
    copy->source.clear();

    sampleEditorWindow = std::make_unique<SampleEditorWindow> (engine);
    auto* window = sampleEditorWindow.get();
    window->setName ("Sample editor - " + juce::String (clip.name));
    window->onClose = [safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->sampleEditorWindow.reset(); });
    };
    window->editor().onClose = window->onClose; // the editor's own Apply and Cancel buttons close the window the same way
    const auto expected = clip.data;
    const auto name = clip.name;
    const auto folder = library.samples.getChildFile ("Edited");
    window->editor().onApply = [safe = juce::Component::SafePointer<MainComponent> (this), index, expected, name, folder] (const SamplePtr& edited, const std::vector<double>& slices) -> juce::String
    {
        if (safe == nullptr)
            return "The program is closing.";
        const auto file = uniqueEditedFile (folder, name);
        auto out = std::make_shared<SampleData> (*edited);
        out->name = name;
        out->source = file.getFullPathName().toStdString();
        const auto written = writeSampleWav (*out, file);
        if (written.failed())
            return written.getErrorMessage();
        const auto error = safe->playlistView.useEditedAudio (index, expected, out, slices);
        if (error.isEmpty())
            safe->statusLabel.setText ("Saved " + file.getFullPathName() + (slices.empty() ? "" : " and cut the clip into " + juce::String (static_cast<int> (slices.size()) + 1) + " pieces"),
                                       juce::dontSendNotification);
        return error;
    };
    window->editor().onSlicesToSampler = [safe = juce::Component::SafePointer<MainComponent> (this), name] (const SamplePtr& edited, const std::vector<double>& markers) -> juce::String
    {
        return safe != nullptr ? safe->slicesToSampler (edited, markers, juce::String (name)) : juce::String ("The program is closing.");
    };
    window->editor().setSample (copy, true, "Save as new file and use");
    window->setVisible (true);
    window->toFront (true);
}

// "Edit sample..." on a sampler channel: the same editor on the channel's whole sample (no slicing); Apply writes a new WAV file and
// the channel plays that.
void MainComponent::openSampleEditorForChannel (int channelIndex)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int> (channels.size()))
        return;
    const auto sample = channels[static_cast<size_t> (channelIndex)].sample;
    if (sample == nullptr || sample->numFrames() < 2)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Sample editor", "This channel has no sample.");
        return;
    }
    if (sampleEditorWindow != nullptr)
    {
        sampleEditorWindow->toFront (true);
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Sample editor", "The sample editor is already open: apply or cancel that edit first.");
        return;
    }

    const auto channelName = juce::String (channels[static_cast<size_t> (channelIndex)].name);
    const auto baseName = sample->name.empty() ? channelName : juce::String (sample->name);
    sampleEditorWindow = std::make_unique<SampleEditorWindow> (engine);
    auto* window = sampleEditorWindow.get();
    window->setName ("Sample editor - " + baseName);
    window->onClose = [safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->sampleEditorWindow.reset(); });
    };
    window->editor().onClose = window->onClose;
    const auto folder = library.samples.getChildFile ("Edited");
    window->editor().onApply = [safe = juce::Component::SafePointer<MainComponent> (this), channelIndex, sample, baseName, folder] (const SamplePtr& edited, const std::vector<double>&) -> juce::String
    {
        if (safe == nullptr)
            return "The program is closing.";
        if (channelIndex >= static_cast<int> (safe->channels.size()) || safe->channels[static_cast<size_t> (channelIndex)].sample != sample)
            return "The channel's sample was changed while the editor was open, so the edit could not be put back. Close the editor and start again.";
        const auto file = uniqueEditedFile (folder, baseName);
        auto out = std::make_shared<SampleData> (*edited);
        out->name = baseName.toStdString();
        out->source = file.getFullPathName().toStdString();
        const auto written = writeSampleWav (*out, file);
        if (written.failed())
            return written.getErrorMessage();
        safe->channels[static_cast<size_t> (channelIndex)].sample = out;
        safe->strips[static_cast<size_t> (channelIndex)]->refreshFromChannel();
        safe->markDirty();
        safe->publishProject();
        safe->statusLabel.setText ("Saved " + file.getFullPathName() + " and loaded it into the channel", juce::dontSendNotification);
        return {};
    };
    window->editor().onSlicesToSampler = [safe = juce::Component::SafePointer<MainComponent> (this), baseName] (const SamplePtr& edited, const std::vector<double>& markers) -> juce::String
    {
        return safe != nullptr ? safe->slicesToSampler (edited, markers, baseName) : juce::String ("The program is closing.");
    };
    window->editor().setSample (std::make_shared<SampleData> (*sample), false, "Save as new file and use");
    window->setVisible (true);
    window->toFront (true);
}
} // namespace sls
