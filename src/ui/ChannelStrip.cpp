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
    return std::clamp (static_cast<int> (static_cast<float> (x) / stepWidth()), 0, stepCount() - 1);
}

bool StepGrid::overEdge (int x) const noexcept
{
    if (! showsSteps())
        return false;
    const float edge = static_cast<float> (activeSteps()) * stepWidth();
    return std::abs (static_cast<float> (x) - edge) <= std::max (3.0f, std::min (6.0f, stepWidth() * 0.35f));
}

void StepGrid::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const int stepTotal = stepCount();
    const float w = bounds.getWidth() / static_cast<float> (stepTotal);
    const int playingStep = playheadTick >= 0 ? playheadTick / kStepTicks : -1;

    if (showsSteps())
    {
        // Each square is drawn from the notes on every paint (nothing is cached), so what is shown is always what plays. The steps
        // after the active part are greyed out: they play the active ones again.
        const int pitch = steps::stepPitch (channel);
        const int active = activeSteps();
        for (int s = 0; s < stepTotal; ++s)
        {
            const bool inActive = s < active;
            const bool on = inActive ? steps::stepOn (channel.notes, s, pitch) : steps::repeatedStepOn (channel.notes, s, active, pitch);
            const auto r = juce::Rectangle<float> (bounds.getX() + static_cast<float> (s) * w, bounds.getY(), w, bounds.getHeight()).reduced (std::min (1.5f, w * 0.12f));
            auto colour = on ? kStepOn : ((s / 4) % 2 == 0 ? kStepOff : kStepOffAlt);
            if (! inActive)
                colour = on ? kStepOn.withMultipliedSaturation (0.35f).withMultipliedBrightness (0.55f) : colour.withMultipliedBrightness (0.6f);
            if (s == playingStep)
                colour = colour.brighter (on ? 0.5f : 0.35f);
            g.setColour (colour);
            g.fillRoundedRectangle (r, 2.0f);
            if (! inActive)
            {
                g.setColour (juce::Colours::black.withAlpha (0.25f));
                g.drawRoundedRectangle (r, 2.0f, 1.0f);
            }
        }

        // The handle at the end of the active part: drag it to make the sequence longer or shorter.
        const float edge = bounds.getX() + static_cast<float> (active) * w;
        g.setColour (hoverEdge || drag == Drag::Edge ? kPlayhead : juce::Colour (0xffb8bec9).withAlpha (0.8f));
        g.fillRect (juce::Rectangle<float> (std::min (edge, bounds.getRight() - 3.0f) - 1.0f, bounds.getY(), 3.0f, bounds.getHeight()));
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

void StepGrid::paintStep (int step, bool on)
{
    if (step == lastStep)
        return;
    lastStep = step;
    if (steps::setStep (channel.notes, channel.steps, step, on, loopLengthTicks, steps::stepPitch (channel)))
    {
        repaint();
        if (onStepsEdited)
            onStepsEdited();
    }
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    drag = Drag::None;
    lastStep = -1;
    askedForLongerPattern = false;

    if (! showsSteps())
    {
        // The preview of the piano roll: right click is the channel's menu, left click opens (or brings forward) the editor.
        if (e.mods.isPopupMenu())
        {
            if (onContextMenu)
                onContextMenu();
        }
        else if (onOpenEditor)
            onOpenEditor();
        return;
    }

    if (! e.mods.isPopupMenu() && overEdge (e.x))
    {
        drag = Drag::Edge;
        repaint();
        return;
    }
    // Squares: the left button writes steps, the right button erases them (both also while dragging across).
    drag = e.mods.isPopupMenu() ? Drag::Erase : Drag::Paint;
    paintStep (stepAt (e.x), drag == Drag::Paint);
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::Paint || drag == Drag::Erase)
    {
        if (e.x >= 0 && e.x < getWidth())
            paintStep (stepAt (e.x), drag == Drag::Paint);
        return;
    }
    if (drag != Drag::Edge)
        return;

    // Past the end of the pattern: the pattern gets a bar longer (once per drag), and the channel takes it in.
    if (e.x > getWidth() + 8 && ! askedForLongerPattern && activeSteps() >= stepCount())
    {
        askedForLongerPattern = true;
        if (onWantsLongerPattern)
            onWantsLongerPattern();
    }

    // Whole bars of 16 steps (4 quarter notes); with Alt held, single steps.
    const float position = static_cast<float> (e.x) / stepWidth();
    int wanted = e.mods.isAltDown() ? juce::roundToInt (position)
                                    : juce::roundToInt (position / static_cast<float> (steps::kBarSteps)) * steps::kBarSteps;
    wanted = std::clamp (wanted, std::min (e.mods.isAltDown() ? 1 : steps::kBarSteps, stepCount()), stepCount());
    if (steps::resize (channel.notes, channel.steps, wanted, loopLengthTicks, steps::stepPitch (channel)))
    {
        repaint();
        if (onStepsEdited)
            onStepsEdited();
    }
}

void StepGrid::mouseUp (const juce::MouseEvent&)
{
    drag = Drag::None;
    lastStep = -1;
    repaint();
}

void StepGrid::mouseMove (const juce::MouseEvent& e)
{
    const bool over = overEdge (e.x);
    if (over != hoverEdge)
    {
        hoverEdge = over;
        repaint();
    }
    setMouseCursor (over ? juce::MouseCursor::LeftRightResizeCursor
                         : (showsSteps() ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor));
}

void StepGrid::mouseExit (const juce::MouseEvent&)
{
    if (hoverEdge)
    {
        hoverEdge = false;
        repaint();
    }
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
    grid.onOpenEditor = [this]
    {
        if (channel.instrument == Instrument::Drums && onOpenDrumMachine)
            onOpenDrumMachine (index);
        else if (onOpenPianoRoll)
            onOpenPianoRoll (index);
    };
    grid.onWantsLongerPattern = [this] { if (onWantsLongerPattern) onWantsLongerPattern (index); };
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
    swingKnob.setVisible (channel.instrument != Instrument::Drums); // a drum machine has a swing per pad
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
    if (e.mods.isPopupMenu())
        return;
    if (channel.instrument == Instrument::Plugin && onOpenPlugin)
        onOpenPlugin (index);
    else if (channel.instrument == Instrument::Drums && onOpenDrumMachine)
        onOpenDrumMachine (index);
    else if (onOpenPianoRoll)
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
    instrument.addItem (9, "Drum machine (16 pads)", true, channel.instrument == Instrument::Drums);
    const auto plugins = listInstrumentPlugins ? listInstrumentPlugins() : std::vector<std::pair<juce::String, juce::String>> {};
    if (plugins.empty())
        instrument.addItem (17, "Plugin instruments (scan the browser's Plugin folder first)", false);
    else
    {
        juce::PopupMenu pluginMenu;
        for (size_t p = 0; p < plugins.size(); ++p)
            pluginMenu.addItem (1000 + static_cast<int> (p), plugins[p].first);
        instrument.addSubMenu ("Plugin instruments", pluginMenu, true, nullptr, channel.instrument == Instrument::Plugin);
    }

    juce::PopupMenu menu;
    menu.addItem (18, "Plugin window...", channel.instrument == Instrument::Plugin && channel.plugin != nullptr);
    menu.addItem (10, "Drum machine...", channel.instrument == Instrument::Drums);
    menu.addItem (1, "Piano roll");
    menu.addSeparator();
    menu.addSubMenu ("Instrument", instrument);
    menu.addItem (4, "Load sample...", channel.instrument == Instrument::Sampler);
    menu.addItem (8, "Edit sample...", channel.instrument == Instrument::Sampler && channel.sample != nullptr && channel.sample->numFrames() > 1);
    menu.addItem (15, "Sampler settings...", channel.instrument == Instrument::Sampler);
    menu.addItem (19, "Synth settings...", channel.instrument == Instrument::Synth);
    menu.addItem (16, "Load SoundFont (.sf2)...", channel.instrument != Instrument::Drums);
    menu.addItem (5, "Rename...");
    menu.addItem (11, "Clone channel");
    menu.addItem (12, "Move up", index > 0);
    menu.addItem (13, "Move down");
    menu.addItem (14, "Delete channel...");
    menu.addSeparator();
    menu.addItem (7, "Assign free mixer track");
    menu.addItem (6, "Save as preset...", channel.instrument != Instrument::Drums && channel.instrument != Instrument::Plugin);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [safe = juce::Component::SafePointer<ChannelStrip> (this), plugins] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;

                            const int i = safe->index;
                            if (result >= 1000 && result < 1000 + static_cast<int> (plugins.size()))
                            {
                                if (safe->onPluginChosen)
                                    safe->onPluginChosen (i, plugins[static_cast<size_t> (result - 1000)].second);
                                return;
                            }
                            switch (result)
                            {
                                case 1: if (safe->onOpenPianoRoll) safe->onOpenPianoRoll (i); break;
                                case 2: if (safe->onInstrumentChosen) safe->onInstrumentChosen (i, Instrument::Synth); break;
                                case 3: if (safe->onInstrumentChosen) safe->onInstrumentChosen (i, Instrument::Sampler); break;
                                case 4: if (safe->onLoadSample) safe->onLoadSample (i); break;
                                case 8: if (safe->onEditSample) safe->onEditSample (i); break;
                                case 9: if (safe->onInstrumentChosen) safe->onInstrumentChosen (i, Instrument::Drums); break;
                                case 10: if (safe->onOpenDrumMachine) safe->onOpenDrumMachine (i); break;
                                case 5: if (safe->onRename) safe->onRename (i); break;
                                case 6: if (safe->onSavePreset) safe->onSavePreset (i); break;
                                case 7: if (safe->onAssignTrack) safe->onAssignTrack (i); break;
                                case 11: if (safe->onClone) safe->onClone (i); break;
                                case 12: if (safe->onMove) safe->onMove (i, -1); break;
                                case 13: if (safe->onMove) safe->onMove (i, 1); break;
                                case 14: if (safe->onDelete) safe->onDelete (i); break;
                                case 15: if (safe->onSamplerSettings) safe->onSamplerSettings (i); break;
                                case 16: if (safe->onLoadSoundFont) safe->onLoadSoundFont (i); break;
                                case 18: if (safe->onOpenPlugin) safe->onOpenPlugin (i); break;
                                case 19: if (safe->onSynthSettings) safe->onSynthSettings (i); break;
                                default: break;
                            }
                        });
}
} // namespace sls
