// Part of MainComponent: splitting an audio clip into stems (0.39.0), by filters, without AI.
#include "ui/MainComponent.h"

#include "audio/ClipEdit.h"
#include "audio/SampleEdit.h"
#include "audio/StemSplit.h"

namespace sls
{
namespace
{
// The split runs on its own thread behind a progress window (it takes a few seconds per minute of audio); `done` gets the stems on
// the message thread (empty when cancelled), and the job deletes itself.
class StemJob : public juce::ThreadWithProgressWindow
{
public:
    StemJob (std::shared_ptr<const SampleData> in, std::function<void (Stems)> whenDone)
        : juce::ThreadWithProgressWindow ("Splitting into stems...", true, true), input (std::move (in)), done (std::move (whenDone))
    {
    }
    void run() override
    {
        stems = splitStems (*input, {}, [this] (double p)
        {
            setProgress (p);
            return ! threadShouldExit();
        });
    }
    void threadComplete (bool cancelled) override
    {
        auto callback = std::move (done);
        auto result = cancelled ? Stems {} : std::move (stems);
        juce::MessageManager::callAsync ([this, callback = std::move (callback), result = std::move (result)]() mutable
        {
            if (callback)
                callback (std::move (result));
            delete this;
        });
    }

private:
    std::shared_ptr<const SampleData> input;
    std::function<void (Stems)> done;
    Stems stems;
};
} // namespace

// "Split into stems..." on an audio clip: Drums, Bass and Other as three new WAV files and three clips where the original was - Drums
// on the clip's own track (the original is removed, Undo brings it back), Bass and Other on the next tracks (or the ones above, at the
// bottom of the playlist). They add up to the original.
void MainComponent::splitClipIntoStems (int index)
{
    if (index < 0 || index >= static_cast<int> (arrangement.audioClips.size()))
        return;
    const auto clip = arrangement.audioClips[static_cast<size_t> (index)];
    if (clip.data == nullptr || clip.data->numFrames() < 2)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Stems", "This clip has no audio (its file is missing).");
        return;
    }
    if (! clip.stretch.isNeutral() || clip.reverse)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Stems",
                                                "Split the clip before stretching, pitching or reversing it: choose Stretch and pitch... > Back to "
                                                "the original and switch Reverse off first.");
        return;
    }

    // The part of the audio the clip plays.
    const double rate = clip.data->sampleRate;
    const int first = static_cast<int> (std::lround (clip.offsetSeconds * rate));
    const int count = std::max (2, static_cast<int> (std::lround (ticksToSeconds (clip.lengthTicks, bpmSlider.getValue()) * rate)));
    auto region = cropToRange (clip.data, { first, first + count });
    if (region == nullptr)
        return;
    auto input = std::make_shared<SampleData> (*region);
    input->name = clip.name;

    auto* job = new StemJob (input, [safe = juce::Component::SafePointer<MainComponent> (this), index, clip] (Stems stems)
    {
        if (safe != nullptr)
            safe->useStems (index, clip, std::move (stems));
    });
    job->launchThread();
}

void MainComponent::useStems (int index, const AudioClip& clip, Stems stems)
{
    if (stems.drums == nullptr)
    {
        statusLabel.setText ("The split was stopped", juce::dontSendNotification);
        return;
    }

    // Three WAV files in Samples/Stems.
    const auto folder = library.samples.getChildFile ("Stems");
    folder.createDirectory();
    const std::pair<const char*, std::shared_ptr<SampleData>> parts[] = { { "drums", stems.drums }, { "bass", stems.bass }, { "other", stems.other } };
    std::vector<std::shared_ptr<const SampleData>> saved;
    for (const auto& [what, data] : parts)
    {
        const auto file = folder.getNonexistentChildFile (juce::String (clip.name) + "-" + what, ".wav", false);
        data->name = clip.name + " " + what;
        data->source = file.getFullPathName().toStdString();
        if (const auto written = writeSampleWav (*data, file); written.failed())
        {
            statusLabel.setText ("Could not write " + file.getFullPathName() + ": " + written.getErrorMessage(), juce::dontSendNotification);
            return;
        }
        saved.push_back (data);
    }

    // The clips: Drums replaces the original on its track, Bass and Other below it (above it at the bottom of the playlist).
    // Tracks are added at the end of the playlist when there are not two below it (0.44.0).
    while (clip.track + 2 >= arrangement.trackCount() && arrangement.addTrack() >= 0) {}
    playlistView.tracksChanged();
    const int count = arrangement.trackCount();
    const int targets[3] = { clip.track, clip.track + 1 < count ? clip.track + 1 : clip.track - 2,
                            clip.track + 2 < count ? clip.track + 2 : clip.track - 1 };
    std::vector<AudioClip> made;
    for (size_t i = 0; i < 3; ++i)
    {
        AudioClip c = clip;
        c.data = saved[i];
        c.name = saved[i]->name;
        c.offsetSeconds = 0.0;
        c.track = std::clamp (targets[i], 0, count - 1);
        made.push_back (std::move (c));
    }
    const auto error = playlistView.replaceAudioClip (index, clip.data, made);
    if (error.isNotEmpty())
    {
        statusLabel.setText (error, juce::dontSendNotification);
        return;
    }
    publishProject();
    statusLabel.setText ("Split " + juce::String (clip.name) + " into drums, bass and other (in " + folder.getFullPathName() + "); Undo brings the original back",
                         juce::dontSendNotification);
}
} // namespace sls
