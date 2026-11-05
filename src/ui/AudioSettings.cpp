#include "ui/AudioSettings.h"

namespace sls
{
namespace
{
constexpr int kSettleTicks = 5;    // 100 ms ticks: let the restarted device settle before measuring
constexpr int kMeasureTicks = 20;  // ... then watch for two seconds
constexpr float kAutoStress = 0.35f; // extra load while measuring: headroom for a heavier song or a busier computer
const juce::Colour kBackground { 0xff2a2d34 };
const juce::Colour kText { 0xffe6e8ec };
const juce::Colour kMuted { 0xffa9aeb9 };

juce::String sizeText (int size, double sampleRate)
{
    return juce::String (size) + " samples  (" + juce::String (bufferMilliseconds (size, sampleRate), 1) + " ms)";
}

void styleLabel (juce::Label& label, juce::Colour colour, float fontHeight = 14.0f)
{
    label.setColour (juce::Label::textColourId, colour);
    label.setFont (juce::FontOptions (fontHeight));
    label.setMinimumHorizontalScale (1.0f);
}
} // namespace

AudioSettingsComponent::AudioSettingsComponent (AudioEngine& e, std::function<void (bool)> playing)
    : engine (e),
      setPlaying (std::move (playing)),
      selector (e.getDeviceManager(), 0, 0, 2, 2, false, false, true, true)
{
    addAndMakeVisible (selector);

    for (auto* l : { &bufferLabel, &delayLabel, &autoStatus, &loadLabel, &helpLabel })
        addAndMakeVisible (l);

    styleLabel (bufferLabel, kText, 15.0f);
    styleLabel (delayLabel, kMuted);
    styleLabel (autoStatus, kText);
    styleLabel (loadLabel, kMuted);
    styleLabel (helpLabel, kMuted, 13.0f);
    autoStatus.setJustificationType (juce::Justification::topLeft);
    helpLabel.setJustificationType (juce::Justification::topLeft);
    helpLabel.setText ("A bigger buffer is safer: it stops crackling and dropouts but adds delay between pressing a "
                       "note and hearing it. A smaller one is more responsive but needs a faster, less busy "
                       "computer. If you hear crackling, choose a bigger size, or press Auto and the program will "
                       "play your song at several sizes and pick one that this computer handles with room to spare.",
                       juce::dontSendNotification);

    addAndMakeVisible (sizeBox);
    sizeBox.onChange = [this]
    {
        if (! updatingMenu && sizeBox.getSelectedId() > 0)
            chooseSize (sizeBox.getSelectedId());
    };

    addAndMakeVisible (autoButton);
    autoButton.onClick = [this]
    {
        if (tuner != nullptr)
            stopAuto (true);
        else
            startAuto();
    };

    engine.getDeviceManager().addChangeListener (this);
    refreshSizes();
    refreshReadout();
    setSize (kWidth, kHeight);
    startTimer (100);
}

AudioSettingsComponent::~AudioSettingsComponent()
{
    stopTimer();
    if (tuner != nullptr)
        stopAuto (true); // never leave the device at a size that was only being tried
    engine.getDeviceManager().removeChangeListener (this);
}

int AudioSettingsComponent::deviceBufferSize() const
{
    auto* device = engine.getDeviceManager().getCurrentAudioDevice();
    return device != nullptr ? device->getCurrentBufferSizeSamples() : 0;
}

double AudioSettingsComponent::deviceSampleRate() const
{
    auto* device = engine.getDeviceManager().getCurrentAudioDevice();
    return device != nullptr ? device->getCurrentSampleRate() : 48000.0;
}

void AudioSettingsComponent::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);
    g.setColour (juce::Colour (0xff3d424d));
    g.drawHorizontalLine (selector.getBottom() + 6, 12.0f, static_cast<float> (getWidth() - 12));
}

void AudioSettingsComponent::resized()
{
    auto area = getLocalBounds().reduced (14);
    auto lower = area.removeFromBottom (250);
    selector.setBounds (area.withTrimmedBottom (12));

    lower.removeFromTop (12);
    auto row = lower.removeFromTop (28);
    bufferLabel.setBounds (row.removeFromLeft (100));
    sizeBox.setBounds (row.removeFromLeft (230));
    delayLabel.setBounds (row.withTrimmedLeft (8));

    lower.removeFromTop (8);
    autoButton.setBounds (lower.removeFromTop (30).removeFromLeft (260));
    lower.removeFromTop (6);
    autoStatus.setBounds (lower.removeFromTop (54));
    loadLabel.setBounds (lower.removeFromTop (24));
    lower.removeFromTop (4);
    helpLabel.setBounds (lower);
}

void AudioSettingsComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshSizes(); // the device, its sample rate or its buffer size changed (here, or in the selector above)
}

void AudioSettingsComponent::refreshSizes()
{
    const juce::ScopedValueSetter<bool> guard (updatingMenu, true);
    sizeBox.clear (juce::dontSendNotification);

    auto* device = engine.getDeviceManager().getCurrentAudioDevice();
    if (device == nullptr)
    {
        sizeBox.setEnabled (false);
        autoButton.setEnabled (false);
        delayLabel.setText ({}, juce::dontSendNotification);
        return;
    }

    const double rate = device->getCurrentSampleRate();
    const int current = device->getCurrentBufferSizeSamples();
    auto sizes = device->getAvailableBufferSizes();
    if (! sizes.contains (current))
        sizes.add (current);
    sizes.sort();

    // ALSA offers hundreds of sizes in small steps; the menu shows the usual ones (powers of two) and the current one.
    for (const int size : sizes)
        if (size == current || (size >= 64 && size <= 8192 && (size & (size - 1)) == 0))
            sizeBox.addItem (sizeText (size, rate), size);

    sizeBox.setSelectedId (current, juce::dontSendNotification);
    delayLabel.setText ("= " + juce::String (bufferMilliseconds (current, rate), 1) + " ms of delay",
                        juce::dontSendNotification);

    const bool choosable = sizes.size() > 1; // JACK, for instance, fixes the buffer in the server
    sizeBox.setEnabled (choosable && tuner == nullptr);
    autoButton.setEnabled (choosable);
    if (! choosable && tuner == nullptr)
        autoStatus.setText ("This audio system fixes the buffer size itself (for example JACK: change it there).",
                            juce::dontSendNotification);
}

void AudioSettingsComponent::chooseSize (int size)
{
    auto& manager = engine.getDeviceManager();
    auto setup = manager.getAudioDeviceSetup();
    if (setup.bufferSize == size && deviceBufferSize() == size)
        return;

    setup.bufferSize = size;
    const auto error = manager.setAudioDeviceSetup (setup, true);
    if (error.isNotEmpty())
        autoStatus.setText ("Could not use " + juce::String (size) + " samples: " + error, juce::dontSendNotification);
    refreshSizes();
}

void AudioSettingsComponent::refreshReadout()
{
    const int xruns = engine.getXRunCount();
    juce::String text = "Computer load now: " + juce::String (juce::roundToInt (engine.getDeviceManager().getCpuUsage() * 100.0))
                      + " %   |   busiest block: " + juce::String (juce::roundToInt (shownPeak * 100.0f)) + " % of its time   |   dropped blocks: "
                      + (xruns >= 0 ? juce::String (xruns) : juce::String ("not reported"));
    loadLabel.setText (text, juce::dontSendNotification);
}

void AudioSettingsComponent::startAuto()
{
    auto* device = engine.getDeviceManager().getCurrentAudioDevice();
    if (device == nullptr)
        return;

    std::vector<int> candidates;
    for (const int size : device->getAvailableBufferSizes())
        if (size >= 256 && size <= 4096 && (size & (size - 1)) == 0)
            candidates.push_back (size);

    if (candidates.empty())
    {
        autoStatus.setText ("This audio device does not offer buffer sizes between 256 and 4096 samples.",
                            juce::dontSendNotification);
        return;
    }

    sizeBeforeAuto = deviceBufferSize();
    wasPlaying = engine.isPlaying();
    if (! wasPlaying)
        setPlaying (true); // the song itself is the test load

    tuner = std::make_unique<BufferTuner> (std::move (candidates));
    engine.setLoadTest (kAutoStress);
    sizeBox.setEnabled (false);
    autoButton.setButtonText ("Cancel");
    beginTrial();
}

void AudioSettingsComponent::beginTrial()
{
    chooseSize (tuner->sizeToTry());
    phase = Phase::settling;
    phaseTicks = 0;
    trialPeak = 0.0f;
    autoStatus.setText ("Testing " + sizeText (tuner->sizeToTry(), deviceSampleRate()) + " ...  ("
                        + juce::String (tuner->triedCount() + 1) + " of at most " + juce::String (tuner->candidateCount())
                        + ")\nThe song plays, with extra load added, while the computer is measured.", juce::dontSendNotification);
}

void AudioSettingsComponent::finishTrial()
{
    const int now = engine.getXRunCount();
    const int xruns = now >= 0 ? juce::jmax (0, now - trialXrunBase) : -1;
    tuner->addTrial (xruns, static_cast<double> (trialPeak));

    if (! tuner->finished())
    {
        beginTrial();
        return;
    }

    const int result = tuner->result();
    const bool struggling = tuner->struggling();
    juce::String summary;
    for (const auto& t : tuner->trials())
        summary << (summary.isEmpty() ? "" : "   ") << t.bufferSize << (isClean (t) ? " ok" : " drops");

    stopAuto (false);
    chooseSize (result);

    const auto size = sizeText (result, deviceSampleRate());
    autoStatus.setText ((struggling ? "Even the biggest buffer drops blocks: set to " + size
                                      + ". The computer is very busy; close other programs.\n"
                                    : "Set to " + size + ": this computer handled it with room to spare.\n")
                            + "Tested: " + summary
                            + (engine.getXRunCount() < 0 ? "\n(This audio system does not report dropped blocks: only computing time was measured.)" : ""),
                        juce::dontSendNotification);
}

void AudioSettingsComponent::stopAuto (bool restore)
{
    engine.setLoadTest (0.0f);
    tuner.reset();
    phase = Phase::idle;
    autoButton.setButtonText ("Auto: set for this computer");
    setPlaying (wasPlaying);
    if (restore)
    {
        chooseSize (sizeBeforeAuto);
        autoStatus.setText ("Auto cancelled: buffer back at " + juce::String (sizeBeforeAuto) + " samples.",
                            juce::dontSendNotification);
    }
    refreshSizes();
}

void AudioSettingsComponent::timerCallback()
{
    const float p = engine.consumePeakLoad();
    shownPeak = juce::jmax (shownPeak, p);

    if (phase == Phase::settling)
    {
        if (++phaseTicks >= kSettleTicks)
        {
            engine.consumePeakLoad(); // forget the start-up blocks
            trialXrunBase = juce::jmax (0, engine.getXRunCount());
            trialPeak = 0.0f;
            phase = Phase::measuring;
            phaseTicks = 0;
        }
    }
    else if (phase == Phase::measuring)
    {
        trialPeak = juce::jmax (trialPeak, p);
        if (++phaseTicks >= kMeasureTicks)
            finishTrial();
    }

    if (++sinceReadout >= 3)
    {
        sinceReadout = 0;
        refreshReadout();
        shownPeak = 0.0f;
    }
}
} // namespace sls
