#include "ui/ChannelStrip.h"

#include <algorithm>

namespace sls
{
namespace
{
const juce::Colour kStripBackground { 0xff30343c };
const juce::Colour kStripSelected { 0xff3b4150 };
const juce::Colour kStepOff { 0xff22252b };
const juce::Colour kStepOffAlt { 0xff2a2e36 };
const juce::Colour kStepOn { 0xffe8795a };
const juce::Colour kPreview { 0xff4cc38a };
const juce::Colour kPlayhead { 0xffffb347 };
} // namespace

// ----------------------------------------------------------------------------- StepGrid

void StepGrid::setPlayheadTick (int tick)
{
    if (tick != playheadTick)
    {
        playheadTick = tick;
        repaint();
    }
}

int StepGrid::stepAt (int x) const noexcept
{
    const float w = static_cast<float> (getWidth()) / static_cast<float> (stepCount());
    return std::clamp (static_cast<int> (static_cast<float> (x) / w), 0, stepCount() - 1);
}

void StepGrid::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const int steps = stepCount();
    const float w = bounds.getWidth() / static_cast<float> (steps);

    if (channel.instrument == Instrument::Sampler)
    {
        for (int s = 0; s < steps; ++s)
        {
            bool on = false;
            for (const auto& n : channel.notes)
                if (n.startTick == s * kStepTicks && n.pitch == channel.rootPitch)
                    on = true;

            const auto r = juce::Rectangle<float> (bounds.getX() + s * w, bounds.getY(), w, bounds.getHeight()).reduced (1.5f);
            g.setColour (on ? kStepOn : ((s / 4) % 2 == 0 ? kStepOff : kStepOffAlt));
            g.fillRoundedRectangle (r, 2.0f);
        }
    }
    else
    {
        g.setColour (kStepOff);
        g.fillRoundedRectangle (bounds, 3.0f);

        if (! channel.notes.empty())
        {
            int lo = 127, hi = 0;
            for (const auto& n : channel.notes)
            {
                lo = std::min (lo, n.pitch);
                hi = std::max (hi, n.pitch);
            }
            const float span = static_cast<float> (std::max (12, hi - lo + 1));
            const float rowH = std::min (6.0f, (bounds.getHeight() - 4.0f) / span);

            g.setColour (kPreview);
            for (const auto& n : channel.notes)
            {
                const float x = bounds.getX() + static_cast<float> (n.startTick) / static_cast<float> (loopLengthTicks) * bounds.getWidth();
                const float nw = std::max (2.0f, static_cast<float> (n.lengthTicks) / static_cast<float> (loopLengthTicks) * bounds.getWidth() - 1.0f);
                const float y = bounds.getBottom() - 2.0f - (static_cast<float> (n.pitch - lo) + 1.0f) * rowH;
                g.fillRect (x, y, nw, std::max (2.0f, rowH - 1.0f));
            }
        }
    }

    if (playheadTick >= 0)
    {
        const float x = bounds.getX() + static_cast<float> (playheadTick) / static_cast<float> (loopLengthTicks) * bounds.getWidth();
        g.setColour (kPlayhead);
        g.drawVerticalLine (static_cast<int> (x), bounds.getY(), bounds.getBottom());
    }
}

void StepGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Synth channels show a read-only note preview: double-clicking it opens the piano roll.
    // Sampler channels toggle steps on every click, so a double click must not be interpreted here.
    if (channel.instrument != Instrument::Sampler && ! e.mods.isPopupMenu() && onDoubleClicked)
        onDoubleClicked();
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onContextMenu)
            onContextMenu();
        return;
    }

    if (channel.instrument != Instrument::Sampler)
    {
        if (onClickedPreview)
            onClickedPreview();
        return;
    }

    const int tick = stepAt (e.x) * kStepTicks;
    auto it = std::find_if (channel.notes.begin(), channel.notes.end(), [&] (const Note& n)
                            { return n.startTick == tick && n.pitch == channel.rootPitch; });

    if (it != channel.notes.end())
    {
        channel.notes.erase (it);
    }
    else
    {
        Note n;
        n.pitch = channel.rootPitch;
        n.startTick = tick;
        n.lengthTicks = kStepTicks;
        n.velocity = 0.9f;
        channel.notes.push_back (n);
    }

    repaint();
    if (onStepsEdited)
        onStepsEdited();
}

// ------------------------------------------------------------------------- ChannelStrip

namespace
{
juce::String pitchText (double semitones)
{
    return (semitones > 0.0 ? "+" : "") + juce::String (semitones, 1) + " st";
}

juce::String swingText (double swing)
{
    return juce::String (juce::roundToInt (swing * 100.0)) + " %";
}
} // namespace

ChannelStrip::ChannelStrip (Channel& channelToControl, int channelIndex)
    : channel (channelToControl), index (channelIndex),
      pitchKnob ("PITCH", -12.0, 12.0, 0.1, 0.0, pitchText),
      swingKnob ("SWING", 0.0, 1.0, 0.01, 0.0, swingText),
      grid (channelToControl)
{
    nameLabel.setFont (juce::FontOptions (13.0f));
    nameLabel.setJustificationType (juce::Justification::centredLeft);
    nameLabel.setInterceptsMouseClicks (false, false);

    muteButton.setClickingTogglesState (true);
    muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd9534f));
    muteButton.onClick = [this]
    {
        channel.mute = muteButton.getToggleState();
        if (onChanged) onChanged (index);
    };

    soloButton.setClickingTogglesState (true);
    soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0b030));
    soloButton.onClick = [this]
    {
        channel.solo = soloButton.getToggleState();
        if (onChanged) onChanged (index);
    };

    volumeSlider.setRange (0.0, 1.5, 0.01);
    volumeSlider.setDoubleClickReturnValue (true, 0.8);
    volumeSlider.setTooltip ("Volume (right click: automate)");
    volumeSlider.onAutomate = [this] { if (onAutomate) onAutomate (index, AutomationKind::ChannelVolume); };
    volumeSlider.onValueChange = [this]
    {
        channel.volume = static_cast<float> (volumeSlider.getValue());
        if (onChanged) onChanged (index);
    };

    panSlider.setRange (-1.0, 1.0, 0.01);
    panSlider.setDoubleClickReturnValue (true, 0.0);
    panSlider.setTooltip ("Pan (right click: automate)");
    panSlider.onAutomate = [this] { if (onAutomate) onAutomate (index, AutomationKind::ChannelPan); };
    panSlider.onValueChange = [this]
    {
        channel.pan = static_cast<float> (panSlider.getValue());
        if (onChanged) onChanged (index);
    };

    trackSelector.onChange = [this] (int track)
    {
        channel.mixerTrack = track;
        if (onChanged) onChanged (index);
    };

    pitchKnob.setTooltipText ("Pitch: tunes every note of this channel, in semitones (double-click: 0)");
    pitchKnob.onValueChange = [this] (double v)
    {
        channel.pitch = static_cast<float> (v);
        if (onChanged) onChanged (index);
    };

    swingKnob.setTooltipText ("Swing: delays the off-beat 16th notes (double-click: 0)");
    swingKnob.onValueChange = [this] (double v)
    {
        channel.swing = static_cast<float> (v);
        if (onChanged) onChanged (index);
    };

    loadButton.onClick = [this] { if (onLoadSample) onLoadSample (index); };

    grid.onStepsEdited = [this] { if (onChanged) onChanged (index); };
    grid.onClickedPreview = [this] { if (onSelect) onSelect (index); };
    grid.onDoubleClicked = [this] { if (onOpenPianoRoll) onOpenPianoRoll (index); };
    grid.onContextMenu = [this] { showContextMenu(); };

    for (auto* c : std::initializer_list<juce::Component*> { &nameLabel, &trackSelector, &muteButton, &soloButton,
                                                              &volumeSlider, &panSlider, &pitchKnob, &swingKnob,
                                                              &loadButton, &meter, &grid })
        addAndMakeVisible (c);

    refreshFromChannel();
}

void ChannelStrip::refreshFromChannel()
{
    nameLabel.setText (juce::String (channel.name), juce::dontSendNotification);
    muteButton.setToggleState (channel.mute, juce::dontSendNotification);
    soloButton.setToggleState (channel.solo, juce::dontSendNotification);
    volumeSlider.setValue (channel.volume, juce::dontSendNotification);
    panSlider.setValue (channel.pan, juce::dontSendNotification);
    trackSelector.setTrack (channel.mixerTrack);
    pitchKnob.setValue (channel.pitch);
    swingKnob.setValue (channel.swing);
    loadButton.setVisible (channel.instrument == Instrument::Sampler);
    grid.repaint();
}

void ChannelStrip::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        repaint();
    }
}

void ChannelStrip::paint (juce::Graphics& g)
{
    g.setColour (selected ? kStripSelected : kStripBackground);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);

    if (selected)
    {
        g.setColour (kPreview);
        g.fillRoundedRectangle (1.0f, 4.0f, 3.0f, static_cast<float> (getHeight()) - 8.0f, 1.5f);
    }
}

void ChannelStrip::resized()
{
    auto area = getLocalBounds().reduced (8, 6);

    nameLabel.setBounds (area.removeFromLeft (80));
    area.removeFromLeft (2);
    trackSelector.setBounds (area.removeFromLeft (62).withSizeKeepingCentre (62, 22));
    area.removeFromLeft (6);
    muteButton.setBounds (area.removeFromLeft (26).withSizeKeepingCentre (26, 24));
    area.removeFromLeft (4);
    soloButton.setBounds (area.removeFromLeft (26).withSizeKeepingCentre (26, 24));
    area.removeFromLeft (8);
    volumeSlider.setBounds (area.removeFromLeft (72));
    panSlider.setBounds (area.removeFromLeft (54));
    area.removeFromLeft (6);
    pitchKnob.setBounds (area.removeFromLeft (38));
    area.removeFromLeft (2);
    swingKnob.setBounds (area.removeFromLeft (38));
    area.removeFromLeft (6);
    meter.setBounds (area.removeFromLeft (10));
    area.removeFromLeft (8);
    loadButton.setBounds (area.removeFromLeft (46).withSizeKeepingCentre (46, 24));
    area.removeFromLeft (8);
    grid.setBounds (area);
}

// ------------------------------------------------------------------ drag and drop from the browser

bool ChannelStrip::isInterestedInDragSource (const SourceDetails& details)
{
    drag::Item item;
    return drag::Item::parse (details.description, item);
}

void ChannelStrip::setDropHighlight (bool on)
{
    if (dropHighlight != on)
    {
        dropHighlight = on;
        repaint();
    }
}

void ChannelStrip::itemDropped (const SourceDetails& details)
{
    setDropHighlight (false);

    drag::Item item;
    if (drag::Item::parse (details.description, item) && onItemDropped)
        onItemDropped (index, item);
}

void ChannelStrip::paintOverChildren (juce::Graphics& g)
{
    if (! dropHighlight)
        return;

    g.setColour (kPreview.withAlpha (0.18f));
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);
    g.setColour (kPreview);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.5f), 4.0f, 2.0f);
}

void ChannelStrip::mouseDown (const juce::MouseEvent& e)
{
    if (onSelect)
        onSelect (index);

    if (e.mods.isPopupMenu())
        showContextMenu();
}

void ChannelStrip::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() && onOpenPianoRoll)
        onOpenPianoRoll (index);
}

// Right click on a channel, like FL Studio: open its piano roll, choose what makes the sound, rename.
void ChannelStrip::showContextMenu()
{
    if (onSelect)
        onSelect (index);

    juce::PopupMenu instrument;
    instrument.addItem (2, "Synth", true, channel.instrument == Instrument::Synth);
    instrument.addItem (3, "Sampler (WAV / FLAC / OGG / AIFF)", true, channel.instrument == Instrument::Sampler);

    juce::PopupMenu menu;
    menu.addItem (1, "Piano roll");
    menu.addSeparator();
    menu.addSubMenu ("Instrument", instrument);
    menu.addItem (4, "Load sample...", channel.instrument == Instrument::Sampler);
    menu.addItem (8, "Edit sample...", channel.instrument == Instrument::Sampler && channel.sample != nullptr && channel.sample->numFrames() > 1);
    menu.addItem (5, "Rename...");
    menu.addSeparator();
    menu.addItem (7, "Assign free mixer track");
    menu.addItem (6, "Save as preset...");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [safe = juce::Component::SafePointer<ChannelStrip> (this)] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;

                            const int i = safe->index;
                            switch (result)
                            {
                                case 1: if (safe->onOpenPianoRoll) safe->onOpenPianoRoll (i); break;
                                case 2: if (safe->onInstrumentChosen) safe->onInstrumentChosen (i, Instrument::Synth); break;
                                case 3: if (safe->onInstrumentChosen) safe->onInstrumentChosen (i, Instrument::Sampler); break;
                                case 4: if (safe->onLoadSample) safe->onLoadSample (i); break;
                                case 8: if (safe->onEditSample) safe->onEditSample (i); break;
                                case 5: if (safe->onRename) safe->onRename (i); break;
                                case 6: if (safe->onSavePreset) safe->onSavePreset (i); break;
                                case 7: if (safe->onAssignTrack) safe->onAssignTrack (i); break;
                                default: break;
                            }
                        });
}
} // namespace sls
