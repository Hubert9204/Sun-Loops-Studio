// Part of MainComponent: the Slicer (0.37.0), a loop cut into slices as in FL Studio's Slicex.
#include "ui/MainComponent.h"

#include "audio/SampleEdit.h"
#include "audio/Slicer.h"

namespace sls
{
namespace
{
// The program a Slicer state makes (its source is the loop's file, so the project can load it again).
std::shared_ptr<const SamplerProgram> programFromSlicer (const SlicerState& s, const std::string& name)
{
    auto program = std::make_shared<SamplerProgram> (makeSliceProgram (s.whole, s.starts, s.firstKey, name, s.settings).sanitised());
    program->source = s.whole != nullptr ? s.whole->source : std::string();
    program->sliceCut = s.cut;
    program->sliceBpm = s.loopBpm;
    return program;
}

// A channel's slices as the Slicer shows them; false when the channel is not a sampler, or plays a SoundFont or a built-in
// instrument (which have no loop to slice).
bool slicerStateOf (const Channel& channel, SlicerState& out)
{
    if (channel.instrument != Instrument::Sampler)
        return false;
    out = {};
    if (channel.program != nullptr)
    {
        const auto& p = *channel.program;
        if (p.sliceStarts.empty() || p.preset >= 0)
            return false;
        // The loop: the channel's own sample when it is the program's source (it always is for slices made here), otherwise the
        // slices put back together.
        if (channel.sample != nullptr && channel.sample->source == p.source && ! p.source.empty())
            out.whole = channel.sample;
        else
        {
            auto whole = std::make_shared<SampleData>();
            for (const auto& z : p.zones)
            {
                whole->left.insert (whole->left.end(), z.sample->left.begin(), z.sample->left.end());
                whole->right.insert (whole->right.end(), z.sample->right.begin(), z.sample->right.end());
                whole->sampleRate = z.sample->sampleRate;
            }
            whole->source = p.source;
            whole->name = p.name;
            out.whole = whole;
        }
        out.starts = p.sliceStarts;
        out.settings = p.sliceSettings;
        out.cut = p.sliceCut;
        out.loopBpm = p.sliceBpm;
        out.firstKey = p.sliceFirstKey;
        return out.whole != nullptr && out.whole->numFrames() >= 2;
    }
    if (channel.sample == nullptr || channel.sample->numFrames() < 2)
        return false;
    out.whole = channel.sample; // a plain sample: sliced where its sounds start
    out.starts = { 0 };
    for (const double t : detectOnsets (channel.sample, 0.5))
        if (const int f = static_cast<int> (std::lround (t * channel.sample->sampleRate)); f > 1 && out.starts.size() < 128)
            out.starts.push_back (f);
    std::sort (out.starts.begin(), out.starts.end());
    out.starts.erase (std::unique (out.starts.begin(), out.starts.end()), out.starts.end());
    return true;
}
} // namespace

bool MainComponent::canSlice (int channelIndex) const
{
    return channelIndex >= 0 && channelIndex < static_cast<int> (channels.size())
        && channels[static_cast<size_t> (channelIndex)].instrument == Instrument::Sampler
        && (channels[static_cast<size_t> (channelIndex)].program == nullptr
                ? channels[static_cast<size_t> (channelIndex)].sample != nullptr
                : ! channels[static_cast<size_t> (channelIndex)].program->sliceStarts.empty());
}

void MainComponent::openSlicer (int channelIndex)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int> (channels.size()))
        return;
    auto& channel = channels[static_cast<size_t> (channelIndex)];
    SlicerState state;
    if (! slicerStateOf (channel, state))
    {
        statusLabel.setText ("The Slicer needs a sampler channel with a sample (not a SoundFont or a built-in instrument)", juce::dontSendNotification);
        return;
    }
    if (state.loopBpm <= 0.0)
        state.loopBpm = slicer::guessLoopBpm (state.whole->numFrames(), state.whole->sampleRate, bpmSlider.getValue());
    if (state.loopBpm <= 0.0)
        state.loopBpm = bpmSlider.getValue();

    // A plain sample becomes a slice program as soon as the Slicer opens on it.
    if (channel.program == nullptr)
    {
        state.cut = true;
        channel.program = programFromSlicer (state, channel.name + " slices");
        strips[static_cast<size_t> (channelIndex)]->refreshFromChannel();
        publishProject();
    }

    if (slicerWindow == nullptr)
    {
        slicerWindow = std::make_unique<SlicerWindow>();
        slicerWindow->onClose = [safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeSlicer(); });
        };
        auto& panel = slicerWindow->panel();
        panel.onChanged = [safe = juce::Component::SafePointer<MainComponent> (this)] (const SlicerState& s)
        {
            if (safe == nullptr)
                return;
            const int i = safe->slicerWindowChannel;
            if (i < 0 || i >= static_cast<int> (safe->channels.size()) || s.whole == nullptr)
                return;
            auto& ch = safe->channels[static_cast<size_t> (i)];
            ch.program = programFromSlicer (s, ch.name + " slices");
            safe->strips[static_cast<size_t> (i)]->refreshFromChannel();
            safe->publishProject();
            safe->statusLabel.setText (juce::String (static_cast<int> (s.starts.size())) + " slices on the keys from "
                                           + juce::String (theory::noteName (s.firstKey)),
                                       juce::dontSendNotification);
        };
        panel.onPlayKey = [safe = juce::Component::SafePointer<MainComponent> (this)] (int key)
        {
            if (safe != nullptr && safe->slicerWindowChannel >= 0)
                safe->engine.liveNoteOn (safe->slicerWindowChannel, key);
        };
        panel.onStopKey = [safe = juce::Component::SafePointer<MainComponent> (this)] (int key)
        {
            if (safe != nullptr && safe->slicerWindowChannel >= 0)
                safe->engine.liveNoteOff (safe->slicerWindowChannel, key);
        };
        panel.onWriteScore = [safe = juce::Component::SafePointer<MainComponent> (this)] (const SlicerState&)
        {
            if (safe != nullptr && safe->slicerWindowChannel >= 0)
                safe->writeSlicerScore (safe->slicerWindowChannel);
        };
    }
    slicerWindowChannel = channelIndex;
    slicerWindow->setName ("Slicer - " + juce::String (channel.name));
    slicerWindow->panel().setState (state);
    slicerWindow->setVisible (true);
    slicerWindow->toFront (true);
}

void MainComponent::closeSlicer()
{
    slicerWindow.reset();
    slicerWindowChannel = -1;
}

// One note per slice in the current pattern, where the slice was in the loop at the loop's tempo (Slicex's "dump score").
void MainComponent::writeSlicerScore (int channelIndex)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int> (channels.size()))
        return;
    auto& channel = channels[static_cast<size_t> (channelIndex)];
    if (channel.program == nullptr || channel.program->zones.empty() || channel.program->zones.front().sample == nullptr)
        return;
    const double rate = channel.program->zones.front().sample->sampleRate;
    const double loopBpm = channel.program->sliceBpm > 0.0 ? channel.program->sliceBpm : bpmSlider.getValue();
    channel.notes = slicer::score (*channel.program, rate, loopBpm, arrangement.patternTicks);
    channel.steps = 0;
    strips[static_cast<size_t> (channelIndex)]->refreshFromChannel();
    selectChannel (channelIndex);
    publishProject();
    statusLabel.setText (juce::String (static_cast<int> (channel.notes.size())) + " notes written into "
                             + juce::String (arrangement.patterns[static_cast<size_t> (currentPattern)].name) + " (the loop at "
                             + juce::String (loopBpm, 1) + " BPM; it follows the project's tempo)",
                         juce::dontSendNotification);
}

// "+ Add channel" > Slicer: a loop chosen from disk becomes a new channel sliced where its sounds start, its notes in the current
// pattern, and the Slicer opens on it.
void MainComponent::chooseLoopForSlicer()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Slice a loop", library.samples.isDirectory() ? library.samples : juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                       "*.wav;*.flac;*.ogg;*.aif;*.aiff;*.mp3");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::FileChooser& chooser)
                              {
                                  const auto file = chooser.getResult();
                                  if (safe != nullptr && file != juce::File())
                                      safe->sliceLoopFile (file);
                              });
}

bool MainComponent::sliceLoopFile (const juce::File& file)
{
    juce::String error;
    auto sample = decodeSample (formats, file, error);
    if (sample == nullptr)
    {
        statusLabel.setText (error, juce::dontSendNotification);
        return false;
    }
    const int index = addChannel (Instrument::Sampler, false);
    if (index < 0)
    {
        statusLabel.setText ("All " + juce::String (kMaxChannels) + " channels are in use.", juce::dontSendNotification);
        return false;
    }
    auto& channel = channels[static_cast<size_t> (index)];
    channel.sample = std::move (sample);
    channel.program = nullptr;
    channel.rootPitch = 60;
    channel.name = file.getFileNameWithoutExtension().substring (0, 12).toStdString();
    openSlicer (index); // slices it and makes the program
    writeSlicerScore (index);
    return true;
}
} // namespace sls
