#include "ui/MixerPanel.h"
#include "ui/Icons.h"
#include "ui/PatcherWindow.h"

#include <cmath>

#include "audio/AudioImport.h"

namespace sls
{
namespace
{
const juce::Colour kStripBg (0xff343841);
const juce::Colour kStripSelectedBg (0xff434a58);
const juce::Colour kAccent (0xffe8943a);
const juce::Colour kPanelBg (0xff262a31);

juce::String shortEffectName (EffectType t)
{
    switch (t)
    {
        case EffectType::Equalizer:  return "EQ";
        case EffectType::Reverb:     return "Reverb";
        case EffectType::Delay:      return "Delay";
        case EffectType::Compressor: return "Comp";
        case EffectType::Plugin:     return "Plugin";
        case EffectType::Overdrive:  return "Drive";
        case EffectType::Chorus:     return "Chorus";
        case EffectType::Phaser:     return "Phaser";
        case EffectType::Filter:     return "Filter";
        case EffectType::ParametricEq: return "P-EQ";
        case EffectType::Maximizer:  return "Maxim";
        case EffectType::Transient:  return "Trans";
        case EffectType::Imager:     return "Imager";
        case EffectType::TimeShaper: return "Time";
        case EffectType::PitchCorrector: return "Pitch";
        case EffectType::Flanger:    return "Flang"; // 0.61.0
        case EffectType::Gate:       return "Gate";
        case EffectType::BassBoost:  return "Bass+";
        case EffectType::Panomatic:  return "Pano";
        case EffectType::Squeeze:    return "Squeez";
        case EffectType::Vibrato:    return "Vibr";
        case EffectType::ParametricEq2: return "P-EQ2";
        case EffectType::MultibandComp: return "MB-Cmp";
        case EffectType::WaveShaper:   return "Shaper";
        case EffectType::Vocoder:      return "Vocod";
        case EffectType::PeakController: return "PeakCt";
        case EffectType::Convolver:    return "Convol";
        case EffectType::FrequencyShifter: return "FShift"; // 0.70.0
        case EffectType::VintageChorus: return "V-Chor";
        case EffectType::VintagePhaser: return "V-Phas";
        case EffectType::Effector:     return "Effect";
        case EffectType::Grains:       return "Grains";
        case EffectType::EnvelopeController: return "EnvCt";
        case EffectType::FormulaController: return "FormCt";
        case EffectType::HyperChorus:  return "HypCh"; // 0.73.0
        case EffectType::Scratcher:    return "Scratch";
        case EffectType::Patcher:      return "Patch";
        case EffectType::None:       break;
    }
    return juce::String::fromUTF8 ("\xe2\x80\x94");
}

void styleSmallLabel (juce::Label& l, float size, bool bold, juce::Justification j)
{
    l.setFont (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    l.setJustificationType (j);
    l.setInterceptsMouseClicks (false, false);
}
} // namespace

// ------------------------------------------------------------------------- SlotButton

void SlotButton::set (const EffectSlot& slot, bool isSelected, bool showFullName)
{
    if (! (slot == slotCopy) || isSelected != selected || showFullName != fullName)
    {
        slotCopy = slot;
        selected = isSelected;
        fullName = showFullName;
        repaint();
    }
}

void SlotButton::paint (juce::Graphics& g)
{
    const bool empty = slotCopy.type == EffectType::None;
    const bool active = ! empty && ! slotCopy.bypass;
    auto r = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (empty ? juce::Colour (0xff262a31) : (active ? juce::Colour (0xff2f5e4c) : juce::Colour (0xff3b3d43)));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (selected ? kAccent : juce::Colour (0xff4a505c));
    g.drawRoundedRectangle (r, 3.0f, selected ? 1.6f : 1.0f);

    const bool isPlugin = slotCopy.type == EffectType::Plugin && slotCopy.plugin != nullptr;
    const juce::String pluginName = isPlugin ? juce::String (slotCopy.plugin->displayName()) : juce::String();
    juce::String text = fullName ? (empty ? juce::String ("Empty - click to add an effect") : isPlugin ? pluginName + "  (plugin)" : juce::String (effectName (slotCopy.type)))
                                 : (isPlugin ? pluginName.substring (0, 7) : shortEffectName (slotCopy.type));
    if (slotCopy.bypass && ! empty)
        text += fullName ? "  (bypassed)" : "";

    g.setColour (empty ? juce::Colour (0xff7d8491) : (active ? juce::Colours::white : juce::Colour (0xff9aa0ab)));
    g.setFont (juce::FontOptions (fullName ? 12.0f : 10.0f));
    g.drawText (text, getLocalBounds().reduced (6, 0), fullName ? juce::Justification::centredLeft : juce::Justification::centred, true);
}

void SlotButton::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onMenu)
            onMenu();
    }
    else if (onClick)
        onClick();
}

// ------------------------------------------------------------------------- MixerStrip

juce::String MixerStrip::volumeToText (float volume)
{
    if (volume <= 0.0001f)
        return "-inf dB";
    return juce::String (20.0f * std::log10 (volume), 1) + " dB";
}

juce::String MixerStrip::panToText (float pan)
{
    const int percent = juce::roundToInt (std::abs (pan) * 100.0f);
    if (percent == 0)
        return "C";
    return (pan < 0.0f ? "L " : "R ") + juce::String (percent);
}

namespace
{
// The bottom of a strip, shared by the track strips and the master strip: the dB read-out, then the fader with its
// meter on the right.
void layoutFaderColumn (juce::Rectangle<int> area, juce::Component& dbLabel, juce::Component& meter, juce::Component& fader)
{
    dbLabel.setBounds (area.removeFromBottom (16));
    area.removeFromBottom (2);

    auto faderArea = area.withSizeKeepingCentre (std::min (area.getWidth(), 56), area.getHeight());
    meter.setBounds (faderArea.removeFromRight (18));
    faderArea.removeFromRight (4);
    fader.setBounds (faderArea);
}

} // namespace

MixerStrip::MixerStrip (MixerTrack& trackToControl, int trackIndex, bool isAuxBus)
    : track (trackToControl), index (trackIndex), isAux (isAuxBus)
{
    styleSmallLabel (nameLabel, 12.0f, true, juce::Justification::centred);
    styleSmallLabel (routedLabel, 10.0f, false, juce::Justification::centred);
    routedLabel.setColour (juce::Label::textColourId, juce::Colour (0xff9aa0ab));
    styleSmallLabel (panLabel, 10.0f, false, juce::Justification::centred);
    styleSmallLabel (dbLabel, 11.0f, false, juce::Justification::centred);

    for (size_t s = 0; s < slotButtons.size(); ++s)
    {
        slotButtons[s].onClick = [this, s] { if (onSlotClicked) onSlotClicked (index, static_cast<int> (s)); };
        slotButtons[s].onMenu = [this, s] { if (onSlotMenu) onSlotMenu (index, static_cast<int> (s)); };
        addAndMakeVisible (slotButtons[s]);
    }

    for (size_t a = 0; a < sendSliders.size(); ++a)
    {
        auto& slider = sendSliders[a];
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange (0.0, 1.5, 0.01);
        slider.setDoubleClickReturnValue (true, 0.0);
        slider.setTooltip ("Send to Aux " + juce::String::charToString (static_cast<juce::juce_wchar> ('A' + static_cast<int> (a)))
                           + " (all the way left: off; double-click: off; right click: automate)");
        slider.onAutomate = [this, a] { if (onAutomate && ! isAux) onAutomate ({ AutomationKind::TrackSend, index, static_cast<int> (a), 0 }); };
        slider.onValueChange = [this, a]
        {
            track.sends[a] = static_cast<float> (sendSliders[a].getValue());
            if (onChanged) onChanged (index);
        };
        styleSmallLabel (sendLabels[a], 11.0f, true, juce::Justification::centred);
        sendLabels[a].setBorderSize ({ 0, 0, 0, 0 });
        sendLabels[a].setText (juce::String::charToString (static_cast<juce::juce_wchar> ('A' + static_cast<int> (a))), juce::dontSendNotification);
        sendLabels[a].setColour (juce::Label::textColourId, juce::Colour (0xff7bc47f));
    }

    preButton.setClickingTogglesState (true);
    preButton.setTooltip ("Pre-fader sends: the sends take the sound before the fader, balance and VCA (they still follow mute and solo)");
    preButton.setMouseClickGrabsKeyboardFocus (false);
    preButton.onClick = [this]
    {
        track.sendsPreFader = preButton.getToggleState();
        if (onChanged) onChanged (index);
    };
    // 0.61.0: polarity, swap and stereo separation, as on an FL Studio mixer track
    phaseButton.setClickingTogglesState (true);
    phaseButton.setTooltip ("Invert polarity: both channels of this track are inverted (after its effects)");
    phaseButton.setMouseClickGrabsKeyboardFocus (false);
    phaseButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe8943a));
    phaseButton.onClick = [this]
    {
        track.invertPhase = phaseButton.getToggleState();
        if (onChanged) onChanged (index);
    };
    swapButton.setClickingTogglesState (true);
    swapButton.setTooltip ("Swap left and right");
    swapButton.setMouseClickGrabsKeyboardFocus (false);
    swapButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe8943a));
    swapButton.onClick = [this]
    {
        track.swapStereo = swapButton.getToggleState();
        if (onChanged) onChanged (index);
    };
    separationSlider.setRange (-1.0, 1.0, 0.01);
    separationSlider.setDoubleClickReturnValue (true, 0.0);
    separationSlider.setTooltip ("Stereo separation: left of the middle widens the stereo image, right narrows it down to mono (double-click: middle)");
    separationSlider.setMouseClickGrabsKeyboardFocus (false);
    separationSlider.onValueChange = [this]
    {
        track.separation = static_cast<float> (separationSlider.getValue());
        if (onChanged) onChanged (index);
    };
    // 0.62.0: free routing, as the cables at the bottom of FL Studio's mixer
    routeButton.setTooltip ("Routing: where this track goes - the master, other mixer tracks (each with its own level), or both");
    routeButton.setMouseClickGrabsKeyboardFocus (false);
    routeButton.onClick = [this] { if (onRouteMenu) onRouteMenu (index, routeButton); };
    if (! isAux) // the aux buses have none of these
    {
        addAndMakeVisible (phaseButton);
        addAndMakeVisible (swapButton);
        addAndMakeVisible (separationSlider);
        addAndMakeVisible (routeButton);
    }

    for (size_t g = 0; g < vcaButtons.size(); ++g)
    {
        auto& b = vcaButtons[g];
        b.setButtonText ("V" + juce::String (static_cast<int> (g) + 1));
        b.setClickingTogglesState (true);
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4a8fd0));
        b.setTooltip ("Member of VCA " + juce::String (static_cast<int> (g) + 1));
        b.setMouseClickGrabsKeyboardFocus (false);
        b.onClick = [this, g]
        {
            const auto bit = static_cast<std::uint8_t> (1u << g);
            track.vcaMask = vcaButtons[g].getToggleState() ? static_cast<std::uint8_t> (track.vcaMask | bit)
                                                           : static_cast<std::uint8_t> (track.vcaMask & ~bit);
            if (onChanged) onChanged (index);
        };
    }

    panSlider.setRange (-1.0, 1.0, 0.01);
    panSlider.setDoubleClickReturnValue (true, 0.0);
    panSlider.setTooltip ("Stereo balance (double-click: centre; right click: automate)");
    panSlider.onAutomate = [this]
    {
        if (onAutomate)
            onAutomate (isAux ? AutomationTarget { AutomationKind::AuxPan, 0, index - kAuxIndexBase, 0 } : AutomationTarget { AutomationKind::TrackPan, index, 0, 0 });
    };
    panSlider.onValueChange = [this]
    {
        track.pan = static_cast<float> (panSlider.getValue());
        panLabel.setText (panToText (track.pan), juce::dontSendNotification);
        if (onChanged) onChanged (index);
    };

    muteButton.setClickingTogglesState (true);
    muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd9534f));
    muteButton.setMouseClickGrabsKeyboardFocus (false);
    muteButton.onClick = [this]
    {
        track.mute = muteButton.getToggleState();
        if (onChanged) onChanged (index);
    };

    soloButton.setClickingTogglesState (true);
    soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0b030));
    soloButton.setMouseClickGrabsKeyboardFocus (false);
    soloButton.onClick = [this]
    {
        track.solo = soloButton.getToggleState();
        if (onChanged) onChanged (index);
    };

    fader.setRange (0.0, 1.5, 0.01);
    fader.setDoubleClickReturnValue (true, 1.0);
    fader.setTooltip (isAux ? "Bus volume (double-click: 0 dB; right click: automate)" : "Track volume (double-click: 0 dB; right click: automate)");
    fader.onAutomate = [this]
    {
        if (onAutomate)
            onAutomate (isAux ? AutomationTarget { AutomationKind::AuxVolume, 0, index - kAuxIndexBase, 0 } : AutomationTarget { AutomationKind::TrackVolume, index, 0, 0 });
    };
    fader.onValueChange = [this]
    {
        track.volume = static_cast<float> (fader.getValue());
        dbLabel.setText (volumeToText (track.volume), juce::dontSendNotification);
        if (onChanged) onChanged (index);
    };

    for (auto* c : std::initializer_list<juce::Component*> { &nameLabel, &routedLabel, &panSlider, &panLabel,
                                                              &muteButton, &fader, &dbLabel, &meter })
        addAndMakeVisible (c);

    if (! isAux)
    {
        addAndMakeVisible (soloButton);
        addAndMakeVisible (preButton);
        for (size_t a = 0; a < sendSliders.size(); ++a)
        {
            addAndMakeVisible (sendSliders[a]);
            addAndMakeVisible (sendLabels[a]);
        }
        for (auto& b : vcaButtons)
            addAndMakeVisible (b);
    }
    else
    {
        routedLabel.setText ("return", juce::dontSendNotification);
    }

    refreshFromTrack();
}

void MixerStrip::showAutomated (AutomationKind kind, int slot, float value)
{
    switch (kind)
    {
        case AutomationKind::TrackVolume:
        case AutomationKind::AuxVolume:
            fader.setValue (value, juce::dontSendNotification);
            dbLabel.setText (volumeToText (value), juce::dontSendNotification);
            break;
        case AutomationKind::TrackPan:
        case AutomationKind::AuxPan:
            panSlider.setValue (value, juce::dontSendNotification);
            panLabel.setText (panToText (value), juce::dontSendNotification);
            break;
        case AutomationKind::TrackSend:
            if (slot >= 0 && slot < static_cast<int> (sendSliders.size()))
                sendSliders[static_cast<size_t> (slot)].setValue (value, juce::dontSendNotification);
            break;
        case AutomationKind::ChannelVolume:
        case AutomationKind::ChannelPan:
        case AutomationKind::TrackEffect:
        case AutomationKind::MasterEffect:
        case AutomationKind::ChannelPlugin:
        case AutomationKind::TrackPlugin:
        case AutomationKind::MasterPlugin:
        case AutomationKind::VcaLevel: break;
    }
}

void MixerStrip::refreshFromTrack()
{
    nameLabel.setText (isAux ? juce::String (track.name) : "Track " + juce::String (index + 1), juce::dontSendNotification);
    // (0.70.0: dark text on a coloured strip's name)
    nameLabel.setColour (juce::Label::textColourId, track.colour != 0 && ! isAux ? juce::Colour (0xff15171a) : juce::Colour (0xffe6e8ec));
    repaint();
    for (size_t a = 0; a < sendSliders.size(); ++a)
        sendSliders[a].setValue (track.sends[a], juce::dontSendNotification);
    preButton.setToggleState (track.sendsPreFader, juce::dontSendNotification);
    phaseButton.setToggleState (track.invertPhase, juce::dontSendNotification);
    swapButton.setToggleState (track.swapStereo, juce::dontSendNotification);
    separationSlider.setValue (track.separation, juce::dontSendNotification);
    routeButton.setButtonText (routingText (track));
    routeButton.setColour (juce::TextButton::buttonColourId, track.toMaster && routingText (track) == "> M" ? juce::Colour (0xff2e323b) : juce::Colour (0xff5a4a2a));
    for (size_t g = 0; g < vcaButtons.size(); ++g)
        vcaButtons[g].setToggleState (((track.vcaMask >> g) & 1u) != 0, juce::dontSendNotification);
    panSlider.setValue (track.pan, juce::dontSendNotification);
    panLabel.setText (panToText (track.pan), juce::dontSendNotification);
    muteButton.setToggleState (track.mute, juce::dontSendNotification);
    soloButton.setToggleState (track.solo, juce::dontSendNotification);
    fader.setValue (track.volume, juce::dontSendNotification);
    dbLabel.setText (volumeToText (track.volume), juce::dontSendNotification);
    refreshSlots();
}

void MixerStrip::refreshSlots()
{
    for (size_t s = 0; s < slotButtons.size(); ++s)
        slotButtons[s].set (track.slots[s], selected && selectedSlot == static_cast<int> (s), false);
}

void MixerStrip::setRoutedChannels (const juce::String& text)
{
    routed = text;
    routedLabel.setText (text.isEmpty() ? juce::String ("no channel") : text, juce::dontSendNotification);
    routedLabel.setTooltip (text);
}

void MixerStrip::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        refreshSlots();
        repaint();
    }
}

void MixerStrip::paint (juce::Graphics& g)
{
    g.setColour (selected ? kStripSelectedBg : kStripBg);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);

    if (track.colour != 0 && ! isAux) // 0.70.0: the strip's own colour, behind its name and as a wash over the strip
    {
        const juce::Colour own (track.colour);
        g.setColour (own.withAlpha (0.10f));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);
        g.setColour (own.withAlpha (0.85f));
        g.fillRoundedRectangle (3.0f, 5.0f, static_cast<float> (getWidth()) - 6.0f, 20.0f, 3.0f);
    }
    if (selected || isAux)
    {
        g.setColour (isAux ? juce::Colour (0xff7bc47f) : kAccent);
        g.fillRoundedRectangle (4.0f, 1.0f, static_cast<float> (getWidth()) - 8.0f, 3.0f, 1.5f);
    }
    if (! isAux && track.icon != 0) // 0.72.0: the strip's icon, in the corner of its name
        drawIcon (g, track.icon, juce::Rectangle<float> (4.0f, 6.0f, 14.0f, 14.0f),
                  track.colour != 0 ? juce::Colour (0xff15171a) : juce::Colours::white.withAlpha (0.8f));
    if (! isAux && track.dock != 0) // and a mark for a docked strip
    {
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        const float y = static_cast<float> (getHeight()) - 6.0f;
        g.fillRect (track.dock == 1 ? 4.0f : static_cast<float> (getWidth()) - 14.0f, y, 10.0f, 2.0f);
    }
}

void MixerStrip::resized()
{
    auto area = getLocalBounds().reduced (5, 6);

    nameLabel.setBounds (area.removeFromTop (18));
    routedLabel.setBounds (area.removeFromTop (14));
    area.removeFromTop (4);

    for (auto& b : slotButtons) // 10 slots (0.45.0): compact
    {
        b.setBounds (area.removeFromTop (14));
        area.removeFromTop (1);
    }
    area.removeFromTop (4);

    if (! isAux)
    {
        for (size_t a = 0; a < sendSliders.size(); ++a)
        {
            auto row = area.removeFromTop (16);
            sendLabels[a].setBounds (row.removeFromLeft (12));
            sendSliders[a].setBounds (row);
        }
        area.removeFromTop (2);
        preButton.setBounds (area.removeFromTop (18).reduced (1, 0));
        area.removeFromTop (2);
        auto vcaRow = area.removeFromTop (18);
        vcaButtons[0].setBounds (vcaRow.removeFromLeft (vcaRow.getWidth() / 2).reduced (1, 0));
        vcaButtons[1].setBounds (vcaRow.reduced (1, 0));
        area.removeFromTop (6);
    }

    panSlider.setBounds (area.removeFromTop (16));
    panLabel.setBounds (area.removeFromTop (12));
    area.removeFromTop (2);
    if (! isAux)
    {
        // 0.61.0: polarity, swap, separation
        separationSlider.setBounds (area.removeFromTop (14));
        area.removeFromTop (2);
        auto row = area.removeFromTop (16);
        phaseButton.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (2, 0));
        swapButton.setBounds (row.reduced (2, 0));
        area.removeFromTop (2);
        routeButton.setBounds (area.removeFromTop (16).reduced (2, 0)); // 0.62.0
    }
    area.removeFromTop (4);

    auto buttons = area.removeFromTop (22);
    if (isAux)
        muteButton.setBounds (buttons.reduced (2, 0));
    else
    {
        muteButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2, 0));
        soloButton.setBounds (buttons.reduced (2, 0));
    }
    area.removeFromTop (4);

    layoutFaderColumn (area, dbLabel, meter, fader);
}

juce::String MixerStrip::routingText (const MixerTrack& track)
{
    int sends = 0, first = -1;
    for (const auto& s : track.trackSends)
        if (s.target >= 0 && s.level > 0.0f)
        {
            ++sends;
            first = first < 0 ? s.target : first;
        }
    if (track.toMaster)
        return sends == 0 ? juce::String ("> M") : "> M+" + juce::String (sends);
    if (sends == 0)
        return "> -";
    return sends == 1 ? "> " + juce::String (first + 1) : "> " + juce::String (first + 1) + "+" + juce::String (sends - 1);
}

void MixerStrip::mouseDown (const juce::MouseEvent&)
{
    if (onSelect)
        onSelect (index);
}

// ------------------------------------------------------------------------- VcaStrip

void VcaStrip::showAutomated (float value)
{
    fader.setValue (value, juce::dontSendNotification);
    dbLabel.setText (MixerStrip::volumeToText (value), juce::dontSendNotification);
}

VcaStrip::VcaStrip (Console& consoleToControl, int groupIndex) : console (consoleToControl), group (groupIndex)
{
    styleSmallLabel (nameLabel, 12.0f, true, juce::Justification::centred);
    nameLabel.setText ("VCA " + juce::String (group + 1), juce::dontSendNotification);
    styleSmallLabel (membersLabel, 10.0f, false, juce::Justification::centred);
    membersLabel.setColour (juce::Label::textColourId, juce::Colour (0xff9aa0ab));
    styleSmallLabel (dbLabel, 11.0f, false, juce::Justification::centred);

    fader.setRange (0.0, 1.5, 0.01);
    fader.setDoubleClickReturnValue (true, 1.0);
    fader.setTooltip ("VCA level: scales the faders of its member tracks (double-click: 0 dB; right click: automate)");
    fader.onAutomate = [this] { if (onAutomate) onAutomate ({ AutomationKind::VcaLevel, 0, group, 0 }); };
    fader.onValueChange = [this]
    {
        console.vcaLevel[static_cast<size_t> (group)] = static_cast<float> (fader.getValue());
        dbLabel.setText (MixerStrip::volumeToText (console.vcaLevel[static_cast<size_t> (group)]), juce::dontSendNotification);
        if (onChanged) onChanged();
    };

    muteButton.setClickingTogglesState (true);
    muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd9534f));
    muteButton.setTooltip ("Mute every member track of this VCA");
    muteButton.setMouseClickGrabsKeyboardFocus (false);
    muteButton.onClick = [this]
    {
        console.vcaMute[static_cast<size_t> (group)] = muteButton.getToggleState();
        if (onChanged) onChanged();
    };

    for (auto* c : std::initializer_list<juce::Component*> { &nameLabel, &membersLabel, &muteButton, &fader, &dbLabel })
        addAndMakeVisible (c);
    refresh();
}

void VcaStrip::refresh()
{
    fader.setValue (console.vcaLevel[static_cast<size_t> (group)], juce::dontSendNotification);
    dbLabel.setText (MixerStrip::volumeToText (console.vcaLevel[static_cast<size_t> (group)]), juce::dontSendNotification);
    muteButton.setToggleState (console.vcaMute[static_cast<size_t> (group)], juce::dontSendNotification);
}

void VcaStrip::setMemberCount (int count)
{
    membersLabel.setText (count == 0 ? juce::String ("no tracks") : juce::String (count) + (count == 1 ? " track" : " tracks"),
                          juce::dontSendNotification);
}

void VcaStrip::paint (juce::Graphics& g)
{
    g.setColour (kStripBg);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);
    g.setColour (juce::Colour (0xff4a8fd0));
    g.fillRoundedRectangle (4.0f, 1.0f, static_cast<float> (getWidth()) - 8.0f, 3.0f, 1.5f);
}

void VcaStrip::resized()
{
    auto area = getLocalBounds().reduced (5, 6);
    nameLabel.setBounds (area.removeFromTop (18));
    membersLabel.setBounds (area.removeFromTop (14));
    area.removeFromTop (4);
    muteButton.setBounds (area.removeFromTop (22).reduced (2, 0));
    area.removeFromTop (4);
    dbLabel.setBounds (area.removeFromBottom (16));
    area.removeFromBottom (2);
    fader.setBounds (area.withSizeKeepingCentre (std::min (area.getWidth(), 40), area.getHeight()));
}

// ------------------------------------------------------------------------- MasterStrip

MasterStrip::MasterStrip()
{
    styleSmallLabel (nameLabel, 12.0f, true, juce::Justification::centred);
    nameLabel.setText ("Master", juce::dontSendNotification);
    styleSmallLabel (dbLabel, 11.0f, false, juce::Justification::centred);

    for (size_t s = 0; s < slotButtons.size(); ++s)
    {
        slotButtons[s].onClick = [this, s] { if (onSlotClicked) onSlotClicked (static_cast<int> (s)); };
        slotButtons[s].onMenu = [this, s] { if (onSlotMenu) onSlotMenu (static_cast<int> (s)); };
        addAndMakeVisible (slotButtons[s]);
    }

    fader.setRange (0.0, 1.0, 0.01);
    fader.setDoubleClickReturnValue (true, 0.8);
    fader.setTooltip ("Master volume (same control as in the toolbar)");
    fader.onValueChange = [this]
    {
        dbLabel.setText (MixerStrip::volumeToText (static_cast<float> (fader.getValue())), juce::dontSendNotification);
        if (onVolumeChanged)
            onVolumeChanged (fader.getValue());
    };

    for (auto* c : std::initializer_list<juce::Component*> { &nameLabel, &fader, &dbLabel, &meter })
        addAndMakeVisible (c);

    setVolume (0.8);
}

void MasterStrip::setMasterTrack (MixerTrack& masterTrack)
{
    master = &masterTrack;
    refreshSlots();
}

void MasterStrip::refreshSlots()
{
    for (size_t s = 0; s < slotButtons.size(); ++s)
        slotButtons[s].set (master != nullptr ? master->slots[s] : EffectSlot {}, selected && selectedSlot == static_cast<int> (s), false);
}

void MasterStrip::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        refreshSlots();
        repaint();
    }
}

void MasterStrip::setVolume (double volume)
{
    fader.setValue (volume, juce::dontSendNotification);
    dbLabel.setText (MixerStrip::volumeToText (static_cast<float> (volume)), juce::dontSendNotification);
}

void MasterStrip::paint (juce::Graphics& g)
{
    g.setColour (selected ? kStripSelectedBg : juce::Colour (0xff3a3f4a));
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);
    g.setColour (kAccent);
    g.fillRoundedRectangle (4.0f, 1.0f, static_cast<float> (getWidth()) - 8.0f, 3.0f, 1.5f);
}

void MasterStrip::resized()
{
    auto area = getLocalBounds().reduced (5, 6);
    nameLabel.setBounds (area.removeFromTop (18));
    area.removeFromTop (14 + 4); // the height of a track's "routed channels" line, so the slots line up

    for (auto& b : slotButtons)
    {
        b.setBounds (area.removeFromTop (14));
        area.removeFromTop (1);
    }
    area.removeFromTop (4);

    layoutFaderColumn (area, dbLabel, meter, fader);
}

void MasterStrip::mouseDown (const juce::MouseEvent&)
{
    if (onSelect)
        onSelect();
}

// ------------------------------------------------------------------------- FxPanel

void XyPad::setPosition (float x, float y)
{
    px = juce::jlimit (0.0f, 1.0f, x);
    py = juce::jlimit (0.0f, 1.0f, y);
    repaint();
}

void XyPad::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xff1e2126));
    g.fillRoundedRectangle (area, 4.0f);
    g.setColour (juce::Colour (0xff3a3f48));
    for (int k = 1; k < 4; ++k)
    {
        g.drawVerticalLine (juce::roundToInt (area.getX() + area.getWidth() * static_cast<float> (k) / 4.0f), area.getY(), area.getBottom());
        g.drawHorizontalLine (juce::roundToInt (area.getY() + area.getHeight() * static_cast<float> (k) / 4.0f), area.getX(), area.getRight());
    }
    const float x = area.getX() + px * area.getWidth(), y = area.getBottom() - py * area.getHeight();
    g.setColour (kAccent.withAlpha (0.35f));
    g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
    g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());
    g.setColour (kAccent);
    g.fillEllipse (x - 6.0f, y - 6.0f, 12.0f, 12.0f);
    g.setColour (juce::Colour (0xff9aa0ab));
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("X", getLocalBounds().reduced (6, 3), juce::Justification::bottomRight);
    g.drawText ("Y", getLocalBounds().reduced (6, 3), juce::Justification::topLeft);
}

void XyPad::mouseDrag (const juce::MouseEvent& e)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);
    if (area.getWidth() <= 0.0f || area.getHeight() <= 0.0f)
        return;
    setPosition ((e.position.x - area.getX()) / area.getWidth(), (area.getBottom() - e.position.y) / area.getHeight());
    if (onMove)
        onMove (px, py);
}

FxPanel::FxPanel()
{
    styleSmallLabel (titleLabel, 13.0f, true, juce::Justification::centredLeft);
    styleSmallLabel (effectLabel, 12.0f, true, juce::Justification::centredLeft);
    effectLabel.setColour (juce::Label::textColourId, kAccent);
    styleSmallLabel (notePlaceholder, 11.0f, false, juce::Justification::topLeft);
    notePlaceholder.setColour (juce::Label::textColourId, juce::Colour (0xff9aa0ab));
    notePlaceholder.setText ("Click a slot to add an effect to this track's chain "
                             "(EQ and filters, dynamics, drive, modulation, reverb, delay, time, pitch, stereo). Effects run in order, before the fader.\n\n"
                             "Plugin effects (VST3, LV2, LADSPA) are in the slot menu once the browser's Plugin folder was scanned.",
                             juce::dontSendNotification);

    addAndMakeVisible (titleLabel);
    chainButton.setTooltip ("Save this track's effect chain as a preset, load one, or clear every slot (0.45.0)");
    chainButton.onClick = [this]
    {
        if (track != nullptr && onChainMenu)
            onChainMenu (chainButton, *track, trackIndex);
    };
    addAndMakeVisible (chainButton);
    addChildComponent (patchButton); // 0.73.0
    patchButton.setTooltip ("Open the Patcher's window: its own ten slots, in series or in parallel (Routing)");
    patchButton.onClick = [this] { openPatcher(); };
    addChildComponent (irButton); // 0.62.0
    irButton.setTooltip ("Use an audio file as the impulse response (the Space becomes File)");
    hintLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    hintLabel.setColour (juce::Label::textColourId, juce::Colour (0xffe8b25a));
    hintLabel.setMinimumHorizontalScale (0.8f);
    addChildComponent (hintLabel);
    addAndMakeVisible (effectLabel);
    addAndMakeVisible (notePlaceholder);
    addChildComponent (pluginButton);
    addChildComponent (automatePluginButton);
    styleSmallLabel (timeShaperNotice, 11.0f, false, juce::Justification::topLeft);
    timeShaperNotice.setColour (juce::Label::textColourId, juce::Colour (0xffe8943a));
    addChildComponent (timeShaperNotice);
    addChildComponent (presetButton);
    presetButton.setTooltip ("Ready-made settings of this effect");

    // 0.70.0
    styleSmallLabel (mixLabel, 11.0f, false, juce::Justification::centredLeft);
    mixLabel.setText ("Slot mix", juce::dontSendNotification);
    addChildComponent (mixLabel);
    mixSlider.setRange (0.0, 100.0, 1.0);
    mixSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    mixSlider.setTextValueSuffix (" %");
    mixSlider.setDoubleClickReturnValue (true, 100.0);
    mixSlider.setTooltip ("The slot's own mix: how much of the effect's sound against the sound before it (100 %: only the effect's, as before 0.70.0)");
    mixSlider.onValueChange = [this]
    {
        if (track == nullptr)
            return;
        track->slots[static_cast<size_t> (selectedSlot)].mix = static_cast<float> (mixSlider.getValue() * 0.01);
        if (onChanged)
            onChanged (trackIndex);
    };
    addChildComponent (mixSlider);
    styleSmallLabel (formulaLabel, 11.0f, false, juce::Justification::centredLeft);
    formulaLabel.setText ("Formula", juce::dontSendNotification);
    addChildComponent (formulaLabel);
    formulaEditor.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain)));
    formulaEditor.setTextToShowWhenEmpty ("a   (for example: 0.5 + 0.5 * sin(2 * pi * beat) * a)", juce::Colour (0xff7a808a));
    formulaEditor.setTooltip ("The controller's value, 0..1. Names: a b c (the knobs), t (seconds), beat (song position), level (this track's "
                              "peak), pi. Functions: sin cos tan abs sqrt exp log floor ceil round frac tri saw sqr rnd min max pow clamp if "
                              "lerp. Enter applies it.");
    formulaEditor.onReturnKey = [this] { applyFormula(); };
    formulaEditor.onFocusLost = [this] { applyFormula(); };
    addChildComponent (formulaEditor);
    effectorPad.onMove = [this] (float x, float y)
    {
        if (track == nullptr || track->slots[static_cast<size_t> (selectedSlot)].type != EffectType::Effector)
            return;
        params[1].slider.setValue (x, juce::sendNotificationSync); // (the sliders write the slot and tell the engine)
        params[2].slider.setValue (y, juce::sendNotificationSync);
    };
    addChildComponent (effectorPad);

    for (size_t s = 0; s < slotButtons.size(); ++s)
    {
        slotButtons[s].onClick = [this, s]
        {
            setSelectedSlot (static_cast<int> (s));
            if (onSelectedSlotChanged) onSelectedSlotChanged (static_cast<int> (s));
            if (track != nullptr && track->slots[s].type == EffectType::None)
                showSlotMenu (static_cast<int> (s));
        };
        slotButtons[s].onMenu = [this, s]
        {
            setSelectedSlot (static_cast<int> (s));
            if (onSelectedSlotChanged) onSelectedSlotChanged (static_cast<int> (s));
            showSlotMenu (static_cast<int> (s));
        };
        addAndMakeVisible (slotButtons[s]);

        powerButtons[s].setButtonText ("ON");
        powerButtons[s].setClickingTogglesState (true);
        powerButtons[s].setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2f9e6e));
        powerButtons[s].setMouseClickGrabsKeyboardFocus (false);
        powerButtons[s].setTooltip ("Switch this effect on or off (bypass)");
        powerButtons[s].onClick = [this, s]
        {
            if (track == nullptr)
                return;
            track->slots[s].bypass = ! powerButtons[s].getToggleState();
            refresh();
            if (onChanged) onChanged (trackIndex);
            if (onSlotsChanged) onSlotsChanged (trackIndex);
        };
        addAndMakeVisible (powerButtons[s]);
    }

    for (auto& row : params)
    {
        row.name.setFont (juce::FontOptions (12.0f));
        row.name.setInterceptsMouseClicks (false, false);
        row.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
        paramsInner.addChildComponent (row.name);
        paramsInner.addChildComponent (row.slider);
        paramsInner.addChildComponent (row.choice);
    }
    paramsViewport.setViewedComponent (&paramsInner, false);
    paramsViewport.setScrollBarsShown (true, false);
    paramsViewport.setScrollBarThickness (8);
    addAndMakeVisible (paramsViewport);

    refresh();
}

FxPanel::~FxPanel() = default;

void FxPanel::setTrack (MixerTrack* newTrack, int newIndex)
{
    if (newTrack != track || newIndex != trackIndex)
        patcherWindow.reset(); // 0.73.0: a Patcher's window belongs to the slot shown
    track = newTrack;
    trackIndex = newIndex;
    refresh();
}

// 0.73.0: the window of the selected Patcher slot; what changes there goes back into the slot.
void FxPanel::openPatcher()
{
    if (track == nullptr || track->slots[static_cast<size_t> (selectedSlot)].type != EffectType::Patcher)
        return;
    auto& slot = track->slots[static_cast<size_t> (selectedSlot)];
    patcherWindow = std::make_unique<PatcherWindow> ("Patcher - " + (trackIndex == kMasterIndex ? juce::String ("Master") : "Track " + juce::String (trackIndex + 1))
                                                     + ", slot " + juce::String (selectedSlot + 1));
    for (size_t k = 0; k < static_cast<size_t> (kPatcherSlots); ++k)
        patcherWindow->inner.slots[k] = slot.patch != nullptr && k < slot.patch->size() ? (*slot.patch)[k] : EffectSlot {};
    auto& inner = patcherWindow->panel();
    inner.trackNameOf = trackNameOf;
    inner.trackCount = trackCount;
    inner.trackAt = trackAt;
    inner.refresh();
    patcherWindow->onEdited = [this, slotIndex = selectedSlot, owner = trackIndex]
    {
        if (track == nullptr || trackIndex != owner || patcherWindow == nullptr)
            return;
        auto& outer = track->slots[static_cast<size_t> (slotIndex)];
        if (outer.type != EffectType::Patcher)
            return;
        outer.patch = std::make_shared<const std::vector<EffectSlot>> (patcherWindow->inner.slots.begin(), patcherWindow->inner.slots.begin() + kPatcherSlots);
        if (onChanged)
            onChanged (trackIndex);
    };
    patcherWindow->onClose = [safe = juce::Component::SafePointer<FxPanel> (this)]
    {
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->patcherWindow.reset(); });
    };
    patcherWindow->setVisible (true);
    patcherWindow->toFront (true);
}

void FxPanel::setSelectedSlot (int slot)
{
    selectedSlot = std::clamp (slot, 0, kSlotsPerTrack - 1);
    refresh();
}

void FxPanel::refresh()
{
    const bool isMaster = trackIndex == kMasterIndex;
    const bool isAuxBus = trackIndex >= kAuxIndexBase;
    titleLabel.setText (isMaster ? juce::String ("Effects - Master")
                                 : (isAuxBus ? "Effects - " + (track != nullptr ? juce::String (track->name) : juce::String ("Aux"))
                                             : "Effects - Track " + juce::String (trackIndex + 1)),
                        juce::dontSendNotification);
    notePlaceholder.setText (isAuxBus
        ? "Click a slot to add an effect to this aux bus (EQ and filters, dynamics, drive, modulation, reverb, delay, time, pitch, stereo). "
          "The bus processes what the tracks send to it (set the send level on each track's strip); its output goes to the master.\n\n"
          "Tip: a reverb or delay here, set to 100 % mix, gives every track the same space.\n"
          "Effect parameters of aux buses cannot be automated yet."
        : (isMaster
        ? "Click a slot to add an effect to the master chain (EQ and filters, dynamics, drive, modulation, reverb, delay, time, pitch, stereo). "
          "These effects process the sum of all tracks, in order, before the master fader.\n\n"
          "Plugin effects (VST3, LV2, LADSPA) are in the slot menu once the browser's Plugin folder was scanned."
        : "Click a slot to add an effect to this track's chain "
          "(EQ and filters, dynamics, drive, modulation, reverb, delay, time, pitch, stereo). Effects run in order, before the fader.\n\n"
          "Plugin effects (VST3, LV2, LADSPA) are in the slot menu once the browser's Plugin folder was scanned."),
        juce::dontSendNotification);

    for (size_t s = 0; s < slotButtons.size(); ++s)
    {
        const EffectSlot slot = track != nullptr ? track->slots[s] : EffectSlot {};
        slotButtons[s].set (slot, static_cast<int> (s) == selectedSlot, true);
        powerButtons[s].setEnabled (slot.type != EffectType::None);
        powerButtons[s].setToggleState (slot.type != EffectType::None && ! slot.bypass, juce::dontSendNotification);
    }
    rebuildParameters();
}

void FxPanel::setTimeShaperShortfall (int count)
{
    if (count == timeShaperShortfall)
        return;
    timeShaperShortfall = count;
    rebuildParameters();
}

void FxPanel::rebuildParameters()
{
    const EffectSlot slot = track != nullptr ? track->slots[static_cast<size_t> (selectedSlot)] : EffectSlot {};
    const int count = effectParamCount (slot.type);

    const bool isPlugin = slot.type == EffectType::Plugin && slot.plugin != nullptr;
    effectLabel.setText (slot.type == EffectType::None ? juce::String()
                                                       : "Slot " + juce::String (selectedSlot + 1) + ": "
                                                             + (isPlugin ? juce::String (slot.plugin->displayName()) + " (plugin)" : juce::String (effectName (slot.type))),
                         juce::dontSendNotification);
    notePlaceholder.setVisible (slot.type == EffectType::None);
    timeShaperNotice.setVisible (slot.type == EffectType::TimeShaper && timeShaperShortfall > 0);
    timeShaperNotice.setText ("Only four Time Shapers in a project get a recording of the sound. "
                                  + (timeShaperShortfall == 1 ? juce::String ("One has none and only shapes the volume")
                                                              : juce::String (timeShaperShortfall) + " have none and only shape the volume")
                                  + " (the ones added first keep theirs). Remove one to give its recording to another.",
                              juce::dontSendNotification);
    pluginButton.setVisible (isPlugin);
    presetButton.setVisible (! effectPresets (slot.type).empty());
    presetButton.onClick = [this] { showPresetMenu(); };
    if (isPlugin)
        pluginButton.onClick = [this, plugin = slot.plugin] { if (openPluginWindow) openPluginWindow (plugin); };
    automatePluginButton.setVisible (isPlugin && trackIndex < kAuxIndexBase && slot.plugin->parameterCount() > 0);
    automatePluginButton.onClick = [this]
    {
        if (onAutomate && trackIndex < kAuxIndexBase)
            onAutomate (trackIndex == kMasterIndex ? AutomationTarget { AutomationKind::MasterPlugin, 0, selectedSlot, -1 }
                                                   : AutomationTarget { AutomationKind::TrackPlugin, trackIndex, selectedSlot, -1 });
    };

    for (int i = 0; i < kMaxEffectParams; ++i)
    {
        auto& row = params[static_cast<size_t> (i)];
        row.slider.onValueChange = nullptr; // not while reconfiguring
        row.choice.onChange = nullptr;
        const bool used = i < count;
        const auto info = effectParamInfo (slot.type, i);
        const juce::String unit (info.unit);
        row.name.setVisible (used);
        row.slider.setVisible (used && unit != "track");
        row.choice.setVisible (used && unit == "track");
        if (! used)
            continue;
        if (unit == "track")
        {
            // A mixer track (0.45.0: the compressor's sidechain): "Off" or one of the project's tracks, by name.
            row.name.setText (info.name, juce::dontSendNotification);
            row.choice.clear (juce::dontSendNotification);
            row.choice.addItem (effectIsController (slot.type) ? "This track"
                                    : slot.type == EffectType::Vocoder ? "None (left = voice, right = carrier)" : "Off (its own sound)", 1);
            const int tracks = trackCount ? trackCount() : 8;
            for (int t = 0; t < tracks; ++t)
                row.choice.addItem (trackNameOf ? trackNameOf (t) : "Track " + juce::String (t + 1), t + 2);
            const int current = juce::roundToInt (slot.params[static_cast<size_t> (i)]);
            if (current > tracks)
                row.choice.addItem ("Track " + juce::String (current) + " (not in the mixer)", current + 1);
            row.choice.setSelectedId (current + 1, juce::dontSendNotification);
            row.choice.setTooltip (slot.type == EffectType::Gate
                                       ? "The gate opens when the chosen track plays (its sound before its own effects and fader)"
                                       : slot.type == EffectType::Vocoder
                                             ? "The voice: the level of each band of the chosen track's sound sets the level of the same band of this track"
                                             : effectIsController (slot.type)
                                                   ? "The mixer track whose effect this controller drives"
                                                   : "The compressor turns this track down when the chosen track plays (its sound before its own effects and fader)");
            row.choice.onChange = [this, i]
            {
                if (track == nullptr)
                    return;
                track->slots[static_cast<size_t> (selectedSlot)].params[static_cast<size_t> (i)]
                    = static_cast<float> (std::max (0, params[static_cast<size_t> (i)].choice.getSelectedId() - 1));
                updateHint();
                if (onChanged)
                    onChanged (trackIndex);
            };
            continue;
        }
        const double range = static_cast<double> (info.max) - static_cast<double> (info.min);
        const int decimals = range >= 100.0 ? 0 : (range >= 10.0 ? 1 : 2);

        row.name.setText (info.name, juce::dontSendNotification);
        row.slider.setSkewFactor (1.0);
        if (info.choices != nullptr)
        {
            // A choice (Type, Mode, Scale...): whole steps, shown by name in a wider box (names like "Major pentatonic").
            row.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 118, 20);
            row.slider.setRange (info.min, info.max, 1.0);
            const auto names = juce::StringArray::fromTokens (info.choices, "|", "");
            const auto first = static_cast<int> (info.min);
            row.slider.textFromValueFunction = [names, first] (double v) { return names[juce::roundToInt (v) - first]; };
            row.slider.valueFromTextFunction = [names, first] (const juce::String& text)
            {
                const int found = names.indexOf (text.trim(), true);
                return static_cast<double> (found >= 0 ? found + first : first);
            };
        }
        else
        {
            row.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
            // (0.62.0: the Peak Controller's target slot and parameter are whole numbers)
            // (and 0.70.0 the other controllers')
            const int controllerTarget = effectControllerTarget (slot.type), controllerTarget2 = effectControllerTarget2 (slot.type);
            const bool whole = controllerTarget >= 0 && (i == controllerTarget + 1 || i == controllerTarget + 2 || i == controllerTarget2 + 1
                                                         || i == controllerTarget2 + 2);
            row.slider.setRange (info.min, info.max, whole ? 1.0 : 0.0);
            if ((unit == "Hz" || unit == "ms" || unit == ":1") && info.min > 0.0f)
                row.slider.setSkewFactorFromMidPoint (std::sqrt (static_cast<double> (info.min) * static_cast<double> (info.max)));
            else if (unit == "Hz" || unit == "ms")
                row.slider.setSkewFactorFromMidPoint (std::max (1.0, static_cast<double> (info.max) / 8.0));
            if (whole)
                row.slider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)); };
            else
                row.slider.textFromValueFunction = [decimals, unit] (double v)
                {
                    return juce::String (v, decimals) + (unit.isEmpty() ? juce::String() : " " + unit);
                };
            row.slider.valueFromTextFunction = nullptr;
        }
        row.slider.setDoubleClickReturnValue (true, info.def);
        row.slider.setValue (slot.params[static_cast<size_t> (i)], juce::dontSendNotification);
        row.slider.updateText();
        row.slider.onAutomate = [this, i]
        {
            if (onAutomate && trackIndex < kAuxIndexBase)
                onAutomate (trackIndex == kMasterIndex ? AutomationTarget { AutomationKind::MasterEffect, 0, selectedSlot, i }
                                                       : AutomationTarget { AutomationKind::TrackEffect, trackIndex, selectedSlot, i });
        };
        row.slider.onValueChange = [this, i]
        {
            if (track == nullptr)
                return;
            auto& changed = track->slots[static_cast<size_t> (selectedSlot)];
            changed.params[static_cast<size_t> (i)] = static_cast<float> (params[static_cast<size_t> (i)].slider.getValue());
            if (changed.type == EffectType::Effector && (i == 1 || i == 2))
                effectorPad.setPosition (changed.params[1], changed.params[2]);
            updateHint();
            if (onChanged)
                onChanged (trackIndex);
        };
    }
    irButton.setVisible (slot.type == EffectType::Convolver);
    patchButton.setVisible (slot.type == EffectType::Patcher); // 0.73.0
    irButton.onClick = [this] { chooseImpulseResponse(); };
    // 0.70.0
    const bool mixable = slot.type != EffectType::None && ! effectIsController (slot.type);
    mixLabel.setVisible (mixable);
    mixSlider.setVisible (mixable);
    mixSlider.setValue (std::round (slot.mix * 100.0f), juce::dontSendNotification);
    const bool isFormula = slot.type == EffectType::FormulaController;
    formulaLabel.setVisible (isFormula);
    formulaEditor.setVisible (isFormula);
    if (isFormula && ! formulaEditor.hasKeyboardFocus (false))
    {
        formulaEditor.setText (slot.formula != nullptr ? juce::String::fromUTF8 (slot.formula->text().c_str()) : juce::String(), false);
        formulaError.clear();
    }
    effectorPad.setVisible (slot.type == EffectType::Effector);
    if (slot.type == EffectType::Effector)
        effectorPad.setPosition (slot.params[1], slot.params[2]);
    updateHint();
    resized();
}

// 0.62.0: one line under the effect's name - what a Peak Controller drives, which file a Convolver's File space uses.
void FxPanel::updateHint()
{
    juce::String text;
    if (track != nullptr)
    {
        const auto& slot = track->slots[static_cast<size_t> (selectedSlot)];
        if (const int at = effectControllerTarget (slot.type); at >= 0) // 0.70.0: any of the three controllers
        {
            // what one of its targets is (0.71.0: the second one too)
            const auto describe = [&] (size_t first) -> juce::String
            {
                const int targetTrack = juce::roundToInt (slot.params[first]);
                const int targetSlot = juce::roundToInt (slot.params[first + 1]) - 1;
                const int targetParam = juce::roundToInt (slot.params[first + 2]) - 1;
                const MixerTrack* target = targetTrack <= 0 ? track : (trackAt ? trackAt (targetTrack - 1) : nullptr);
                const auto where = targetTrack <= 0 ? juce::String ("this track") : "track " + juce::String (targetTrack);
                const auto place = " (" + where + ", slot " + juce::String (targetSlot + 1) + ")";
                if (target == nullptr || targetSlot < 0 || targetSlot >= kSlotsPerTrack)
                    return "nothing: no such track";
                const auto& driven = target->slots[static_cast<size_t> (targetSlot)];
                if (driven.type == EffectType::None)
                    return "nothing yet: empty slot" + place;
                if (driven.type == EffectType::Plugin && driven.plugin != nullptr)
                    return juce::String (driven.plugin->displayName()) + " > "
                         + (targetParam < driven.plugin->parameterCount() ? juce::String (driven.plugin->parameterName (targetParam)) : juce::String ("(no such parameter)"))
                         + place;
                if (effectIsController (driven.type) || targetParam >= effectParamCount (driven.type)
                    || juce::String (effectParamInfo (driven.type, targetParam).unit) == "track")
                    return "nothing: " + juce::String (effectName (driven.type)) + " has no parameter " + juce::String (targetParam + 1);
                return juce::String (effectName (driven.type)) + " > " + effectParamInfo (driven.type, targetParam).name + place;
            };
            text = "Drives " + describe (static_cast<size_t> (at));
            if (const int second = effectControllerTarget2 (slot.type); second >= 0 && juce::roundToInt (slot.params[static_cast<size_t> (second) + 1]) >= 1)
                text += "; and " + describe (static_cast<size_t> (second))
                      + (slot.params[static_cast<size_t> (second) + 3] > 0.5f ? juce::String (" (inverted)") : juce::String());
            if (slot.type == EffectType::FormulaController && formulaError.isNotEmpty())
                text = "Formula not applied: " + formulaError;
        }
        else if (slot.type == EffectType::Convolver)
        {
            const bool fileSpace = effectChoice (EffectType::Convolver, 0, slot.params[0]) == Convolver::File;
            const auto file = slot.convolver != nullptr ? slot.convolver->file() : nullptr;
            if (file != nullptr)
                text = (fileSpace ? "Impulse response: " : "File (not in use): ") + juce::File (file->source).getFileName();
            else if (fileSpace)
                text = "No impulse response yet: Load IR... picks an audio file";
        }
    }
    hintLabel.setText (text, juce::dontSendNotification);
    hintLabel.setVisible (text.isNotEmpty());
}

// 0.70.0: the text of the formula box becomes the Formula Controller's formula (empty: the value is knob A again); text that
// cannot be read leaves the formula as it was and says why in the hint line.
void FxPanel::applyFormula()
{
    if (track == nullptr)
        return;
    auto& slot = track->slots[static_cast<size_t> (selectedSlot)];
    if (slot.type != EffectType::FormulaController)
        return;
    const auto text = formulaEditor.getText().trim();
    const auto before = slot.formula != nullptr ? juce::String::fromUTF8 (slot.formula->text().c_str()) : juce::String();
    if (text == before)
        return;
    if (text.isEmpty())
    {
        slot.formula = nullptr;
        formulaError.clear();
    }
    else
    {
        std::string why;
        if (auto made = Formula::compile (text.toStdString(), &why))
        {
            slot.formula = std::move (made);
            formulaError.clear();
        }
        else
            formulaError = juce::String::fromUTF8 (why.c_str());
    }
    updateHint();
    resized();
    if (formulaError.isEmpty() && onChanged)
        onChanged (trackIndex);
}

void FxPanel::chooseImpulseResponse()
{
    if (track == nullptr)
        return;
    irChooser = std::make_unique<juce::FileChooser> ("Impulse response (an audio file: the recording of a space, a spring, a cabinet...)",
                                                     juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    irChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                            [safe = juce::Component::SafePointer<FxPanel> (this), slotIndex = selectedSlot] (const juce::FileChooser& chooser)
                            {
                                const auto file = chooser.getResult();
                                if (safe == nullptr || safe->track == nullptr || ! file.existsAsFile())
                                    return;
                                auto& slot = safe->track->slots[static_cast<size_t> (slotIndex)];
                                if (slot.type != EffectType::Convolver)
                                    return;
                                const auto loaded = importAudioFile (file);
                                if (loaded.data == nullptr)
                                {
                                    safe->hintLabel.setText ("Not loaded: " + loaded.error, juce::dontSendNotification);
                                    safe->hintLabel.setVisible (true);
                                    return;
                                }
                                // The File space, with a length that takes the whole file (up to the convolver's limit); the response
                                // itself is made when the project is published (prepareConvolver sees there is none for these settings).
                                Convolver::Settings none;
                                none.sampleRate = 0.0;
                                none.seconds = 0.05f;
                                none.space = Convolver::Room;
                                slot.convolver = Convolver::make (none, loaded.data);
                                slot.params[0] = static_cast<float> (Convolver::File);
                                slot.params[1] = std::clamp (static_cast<float> (loaded.data->numFrames() / loaded.data->sampleRate), 0.1f, Convolver::kMaxSeconds);
                                safe->rebuildParameters();
                                if (safe->onChanged)
                                    safe->onChanged (safe->trackIndex);
                            });
}

void FxPanel::showPresetMenu()
{
    if (track == nullptr)
        return;
    const auto current = track->slots[static_cast<size_t> (selectedSlot)];
    const auto& presets = effectPresets (current.type);
    if (presets.empty())
        return;
    juce::PopupMenu menu;
    menu.addSectionHeader (juce::String (effectName (current.type)) + " presets");
    for (size_t i = 0; i < presets.size(); ++i)
        menu.addItem (static_cast<int> (i) + 1, presets[i].name, true, presets[i].params == current.params);
    menu.addSeparator();
    menu.addItem (1000, "Default settings");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton),
                        [safe = juce::Component::SafePointer<FxPanel> (this), slot = selectedSlot, current] (int result)
                        {
                            if (safe == nullptr || result == 0 || safe->track == nullptr
                                || safe->track->slots[static_cast<size_t> (slot)].type != current.type)
                                return;
                            auto s = EffectSlot::make (current.type);
                            s.bypass = current.bypass;
                            const auto& list = effectPresets (current.type);
                            if (result >= 1 && result <= static_cast<int> (list.size()))
                                s.params = list[static_cast<size_t> (result - 1)].params;
                            if (current.type == EffectType::Compressor || current.type == EffectType::Gate)
                                s.params[5] = current.params[5]; // a preset does not change where the sidechain comes from
                            safe->applySlot (slot, s.sanitised());
                        });
}

void FxPanel::applySlot (int slot, const EffectSlot& newSlot)
{
    if (track == nullptr)
        return;

    track->slots[static_cast<size_t> (slot)] = newSlot;
    refresh();
    if (onChanged) onChanged (trackIndex);
    if (onSlotsChanged) onSlotsChanged (trackIndex);
}

void FxPanel::showSlotMenu (int slot)
{
    if (track == nullptr)
        return;

    const auto current = track->slots[static_cast<size_t> (slot)];

    juce::PopupMenu menu;
    menu.addSectionHeader ("Slot " + juce::String (slot + 1));
    for (const auto& entry : kEffectMenu)
    {
        // (0.73.0: inside a Patcher, no controllers and no Patcher)
        if (nested && (effectIsController (entry.type) || entry.type == EffectType::Patcher))
            continue;
        if (entry.group != nullptr)
            menu.addSectionHeader (entry.group);
        menu.addItem (static_cast<int> (entry.type), effectName (entry.type), true, current.type == entry.type);
    }
    menu.addSeparator();
    const auto plugins = listEffectPlugins && ! nested ? listEffectPlugins() : std::vector<PluginChoice> {};
    juce::PopupMenu pluginMenu;
    for (size_t i = 0; i < plugins.size(); ++i)
        pluginMenu.addItem (1000 + static_cast<int> (i), plugins[i].name);
    if (! nested && plugins.empty())
        menu.addItem (100, "Plugin effects (scan the browser's Plugin folder first)", false);
    else if (! nested)
        menu.addSubMenu ("Plugin effects", pluginMenu);
    if (! nested)
        menu.addItem (103, "Open plugin window", current.type == EffectType::Plugin && current.plugin != nullptr);
    menu.addSeparator();
    menu.addItem (101, "Bypass", current.type != EffectType::None, current.bypass);
    menu.addItem (102, "Remove effect", current.type != EffectType::None);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slotButtons[static_cast<size_t> (slot)]),
                        [safe = juce::Component::SafePointer<FxPanel> (this), slot, current, plugins] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;

                            if (result >= 1000 && result < 1000 + static_cast<int> (plugins.size()))
                            {
                                auto plugin = safe->createEffectPlugin ? safe->createEffectPlugin (plugins[static_cast<size_t> (result - 1000)].identifier) : nullptr;
                                if (plugin != nullptr)
                                {
                                    EffectSlot s;
                                    s.type = EffectType::Plugin;
                                    s.plugin = std::move (plugin);
                                    safe->applySlot (slot, s);
                                    if (safe->openPluginWindow)
                                        safe->openPluginWindow (s.plugin);
                                }
                            }
                            else if (result == 103)
                            {
                                if (safe->openPluginWindow)
                                    safe->openPluginWindow (current.plugin);
                            }
                            else if (result >= 1 && result < kEffectTypeCount && static_cast<EffectType> (result) != EffectType::Plugin)
                            {
                                const auto type = static_cast<EffectType> (result);
                                if (type != current.type)
                                {
                                    auto made = EffectSlot::make (type);
                                    if (const int at = effectControllerTarget (type); at >= 0) // 0.62.0: a controller drives the slot after its own to begin with
                                        made.params[static_cast<size_t> (at) + 1] = static_cast<float> (slot + 1 < kSlotsPerTrack ? slot + 2 : 1);
                                    if (type == EffectType::Patcher) // 0.73.0: ten empty slots of its own
                                        made.patch = std::make_shared<const std::vector<EffectSlot>> (static_cast<size_t> (kPatcherSlots));
                                    safe->applySlot (slot, made);
                                }
                            }
                            else if (result == 101)
                            {
                                auto s = current;
                                s.bypass = ! s.bypass;
                                safe->applySlot (slot, s);
                            }
                            else if (result == 102)
                            {
                                safe->applySlot (slot, EffectSlot {});
                            }
                        });
}

void FxPanel::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff2f333b));
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);
}

void FxPanel::resized()
{
    auto area = getLocalBounds().reduced (8, 8);
    titleLabel.setBounds (area.removeFromTop (22));
    area.removeFromTop (4);

    for (size_t s = 0; s < slotButtons.size(); ++s)
    {
        auto row = area.removeFromTop (20);
        powerButtons[s].setBounds (row.removeFromLeft (36));
        row.removeFromLeft (4);
        slotButtons[s].setBounds (row);
        area.removeFromTop (2);
    }

    area.removeFromTop (6);
    {
        auto head = area.removeFromTop (22);
        chainButton.setBounds (head.removeFromRight (64));
        head.removeFromRight (4);
        if (presetButton.isVisible())
            presetButton.setBounds (head.removeFromRight (82));
        if (patchButton.isVisible()) // 0.73.0
        {
            head.removeFromRight (4);
            patchButton.setBounds (head.removeFromRight (60));
        }
        if (irButton.isVisible())
        {
            head.removeFromRight (4);
            irButton.setBounds (head.removeFromRight (78));
        }
        effectLabel.setBounds (head);
    }
    if (hintLabel.isVisible())
        hintLabel.setBounds (area.removeFromTop (18));
    if (formulaEditor.isVisible()) // 0.70.0
    {
        area.removeFromTop (2);
        auto row = area.removeFromTop (22);
        formulaLabel.setBounds (row.removeFromLeft (64));
        formulaEditor.setBounds (row);
    }
    if (mixSlider.isVisible())
    {
        area.removeFromTop (2);
        auto row = area.removeFromTop (22);
        mixLabel.setBounds (row.removeFromLeft (64));
        mixSlider.setBounds (row);
    }
    if (effectorPad.isVisible())
    {
        effectorPad.setBounds (area.removeFromBottom (std::min (150, std::max (60, area.getHeight() / 3))));
        area.removeFromBottom (4);
    }
    area.removeFromTop (4);
    timeShaperNotice.setBounds (area.removeFromBottom (60));
    notePlaceholder.setBounds (area);
    pluginButton.setBounds (area.getX(), area.getY(), std::min (200, area.getWidth()), 26);
    automatePluginButton.setBounds (area.getX(), area.getY() + 32, std::min (200, area.getWidth()), 26);

    paramsViewport.setBounds (area);
    layoutParams();
}

// The parameter rows in their scrolling pane (0.61.0); called after a resize and after the rows change.
void FxPanel::layoutParams()
{
    int shown = 0;
    for (auto& row : params)
        if (row.slider.isVisible() || row.choice.isVisible())
            ++shown;
    const int needed = shown * 25;
    const bool scrolls = needed > paramsViewport.getHeight();
    const int width = paramsViewport.getWidth() - (scrolls ? paramsViewport.getScrollBarThickness() : 0);
    paramsInner.setSize (std::max (10, width), std::max (paramsViewport.getHeight(), needed));
    auto area = paramsInner.getLocalBounds();
    for (auto& row : params)
    {
        if (! row.slider.isVisible() && ! row.choice.isVisible())
            continue;
        auto r = area.removeFromTop (23);
        row.name.setBounds (r.removeFromLeft (64));
        row.slider.setBounds (r);
        row.choice.setBounds (r.reduced (0, 1));
        area.removeFromTop (2);
    }
}

// ------------------------------------------------------------------------- MixerPanel

MixerPanel::MixerPanel()
{
    viewport.setViewedComponent (&inner, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);
    addAndMakeVisible (masterStrip);
    addAndMakeVisible (fxPanel);

    masterStrip.onVolumeChanged = [this] (double v) { if (onMasterVolumeChanged) onMasterVolumeChanged (v); };
    fxPanel.onChanged = [this] (int track) { if (onTrackChanged) onTrackChanged (track); };
    fxPanel.trackCount = [this] { return trackData != nullptr ? static_cast<int> (trackData->size()) : 8; };
    fxPanel.trackAt = [this] (int t) -> const MixerTrack*
    {
        return trackData != nullptr && t >= 0 && t < static_cast<int> (trackData->size()) ? &(*trackData)[static_cast<size_t> (t)] : nullptr;
    };
    fxPanel.trackNameOf = [this] (int t)
    {
        const auto number = "Track " + juce::String (t + 1);
        if (trackData == nullptr || t < 0 || t >= static_cast<int> (trackData->size()))
            return number + " (not in the mixer)";
        const auto& name = (*trackData)[static_cast<size_t> (t)].name;
        auto text = name.empty() || juce::String (name) == number ? number : number + ": " + juce::String (name);
        if (t < static_cast<int> (strips.size()) && strips[static_cast<size_t> (t)]->routedChannels().isNotEmpty())
            text += " (" + strips[static_cast<size_t> (t)]->routedChannels() + ")";
        return text;
    };
    fxPanel.onAutomate = [this] (const AutomationTarget& t) { if (onAutomate) onAutomate (t); };
    fxPanel.listEffectPlugins = [this] { return listEffectPlugins ? listEffectPlugins() : std::vector<FxPanel::PluginChoice> {}; };
    fxPanel.onChainMenu = [this] (juce::Component& b, MixerTrack& t, int i) { if (onChainMenu) onChainMenu (b, t, i); };
    fxPanel.createEffectPlugin = [this] (const juce::String& id) { return createEffectPlugin ? createEffectPlugin (id) : std::shared_ptr<HostedPlugin>(); };
    fxPanel.openPluginWindow = [this] (const std::shared_ptr<HostedPlugin>& p) { if (openPluginWindow) openPluginWindow (p); };
    fxPanel.onSlotsChanged = [this] (int track) { refreshTrack (track); };
    fxPanel.onSelectedSlotChanged = [this] (int slot)
    {
        if (selectedTrack == kMasterIndex)
            masterStrip.setSelectedSlot (slot);
        else if (selectedTrack >= kAuxIndexBase)
        {
            const auto a = static_cast<size_t> (selectedTrack - kAuxIndexBase);
            if (a < auxStrips.size())
                auxStrips[a]->setSelectedSlot (slot);
        }
        else if (selectedTrack >= 0 && selectedTrack < static_cast<int> (strips.size()))
            strips[static_cast<size_t> (selectedTrack)]->setSelectedSlot (slot);
    };

    masterStrip.onSelect = [this] { selectMaster(); };
    masterStrip.onSlotClicked = [this] (int slot)
    {
        selectMaster();
        fxPanel.setSelectedSlot (slot);
        masterStrip.setSelectedSlot (slot);
        if (masterData != nullptr && masterData->slots[static_cast<size_t> (slot)].type == EffectType::None)
            fxPanel.showSlotMenu (slot);
    };
    masterStrip.onSlotMenu = [this] (int slot)
    {
        selectMaster();
        fxPanel.setSelectedSlot (slot);
        masterStrip.setSelectedSlot (slot);
        fxPanel.showSlotMenu (slot);
    };
}

void MixerPanel::selectAux (int aux)
{
    if (consoleData == nullptr || aux < 0 || aux >= static_cast<int> (auxStrips.size()))
        return;

    selectedTrack = kAuxIndexBase + aux;
    for (auto& s : strips)
        s->setSelected (false);
    masterStrip.setSelected (false);
    for (size_t a = 0; a < auxStrips.size(); ++a)
        auxStrips[a]->setSelected (static_cast<int> (a) == aux);
    fxPanel.setTrack (&consoleData->aux[static_cast<size_t> (aux)], selectedTrack);
    auxStrips[static_cast<size_t> (aux)]->setSelectedSlot (fxPanel.getSelectedSlot());
}

void MixerPanel::setConsole (Console& consoleToControl)
{
    consoleData = &consoleToControl;
    auxStrips.clear();
    vcaStrips.clear();

    for (int a = 0; a < kAuxBuses; ++a)
    {
        auto strip = std::make_unique<MixerStrip> (consoleToControl.aux[static_cast<size_t> (a)], kAuxIndexBase + a, true);
        strip->onSelect = [this] (int idx) { selectAux (idx - kAuxIndexBase); };
        strip->onChanged = [this] (int idx) { if (onTrackChanged) onTrackChanged (idx); };
        strip->onAutomate = [this] (const AutomationTarget& t) { if (onAutomate) onAutomate (t); };
        strip->onSlotClicked = [this] (int idx, int slot)
        {
            selectAux (idx - kAuxIndexBase);
            fxPanel.setSelectedSlot (slot);
            auxStrips[static_cast<size_t> (idx - kAuxIndexBase)]->setSelectedSlot (slot);
            if (consoleData->aux[static_cast<size_t> (idx - kAuxIndexBase)].slots[static_cast<size_t> (slot)].type == EffectType::None)
                fxPanel.showSlotMenu (slot);
        };
        strip->onSlotMenu = [this] (int idx, int slot)
        {
            selectAux (idx - kAuxIndexBase);
            fxPanel.setSelectedSlot (slot);
            auxStrips[static_cast<size_t> (idx - kAuxIndexBase)]->setSelectedSlot (slot);
            fxPanel.showSlotMenu (slot);
        };
        inner.addAndMakeVisible (*strip);
        auxStrips.push_back (std::move (strip));
    }

    for (int g = 0; g < kVcaGroups; ++g)
    {
        auto strip = std::make_unique<VcaStrip> (consoleToControl, g);
        strip->onChanged = [this] { if (onTrackChanged) onTrackChanged (kMasterIndex); };
        strip->onAutomate = [this] (const AutomationTarget& t) { if (onAutomate) onAutomate (t); };
        inner.addAndMakeVisible (*strip);
        vcaStrips.push_back (std::move (strip));
    }

    refreshConsole();
    layoutInner();
}

void MixerPanel::refreshConsole()
{
    for (auto& s : auxStrips)
        s->refreshFromTrack();
    for (auto& s : vcaStrips)
        s->refresh();
    for (auto& s : strips)
        s->refreshFromTrack();
    updateVcaMembers();
    if (selectedTrack >= kAuxIndexBase)
        fxPanel.refresh();
}

void MixerPanel::updateVcaMembers()
{
    if (trackData == nullptr)
        return;
    for (size_t g = 0; g < vcaStrips.size(); ++g)
    {
        int count = 0;
        for (const auto& t : *trackData)
            count += ((t.vcaMask >> g) & 1u) != 0 ? 1 : 0;
        vcaStrips[g]->setMemberCount (count);
    }
}

void MixerPanel::setAuxLevels (int aux, float peak, float rms)
{
    if (aux >= 0 && aux < static_cast<int> (auxStrips.size()))
        auxStrips[static_cast<size_t> (aux)]->setLevels (peak, rms);
}

void MixerPanel::setMaster (MixerTrack& masterTrack)
{
    masterData = &masterTrack;
    masterStrip.setMasterTrack (masterTrack);
    if (selectedTrack == kMasterIndex)
        selectMaster();
}

void MixerPanel::selectMaster()
{
    if (masterData == nullptr)
        return;

    selectedTrack = kMasterIndex;
    for (auto& s : strips)
        s->setSelected (false);
    for (auto& s : auxStrips)
        s->setSelected (false);
    masterStrip.setSelected (true);
    fxPanel.setTrack (masterData, kMasterIndex);
    masterStrip.setSelectedSlot (fxPanel.getSelectedSlot());
}

void MixerPanel::setTracks (std::vector<MixerTrack>& tracks, size_t stripCount)
{
    if (trackData != &tracks)
        strips.clear();
    trackData = &tracks;
    const size_t wanted = std::min (stripCount, tracks.size());
    while (strips.size() > wanted)
        strips.pop_back();
    for (size_t i = strips.size(); i < wanted; ++i)
        addStrip (i);

    if (addTrackButton.getParentComponent() == nullptr)
    {
        addTrackButton.setTooltip ("Add a mixer track (right-click: add more, or remove the last one). Up to " + juce::String (kMaxTracks) + ".");
        addTrackButton.onClick = [this]
        {
            if (onAddTracks)
                onAddTracks (1);
        };
        addTrackButton.onRightClick = [this] { showAddTrackMenu(); };
        inner.addAndMakeVisible (addTrackButton);
    }
    addTrackButton.setEnabled (tracks.size() < static_cast<size_t> (kMaxTracks));

    if (selectedTrack != kMasterIndex && selectedTrack < kMaxTracks) // (an aux bus keeps its selection)
        selectedTrack = std::clamp (selectedTrack, 0, std::max (0, static_cast<int> (strips.size()) - 1));
    layoutInner(); // first: selecting scrolls to the strip where it now is
    setSelectedTrack (selectedTrack);
}

void MixerPanel::showAddTrackMenu()
{
    juce::PopupMenu menu;
    const int room = kMaxTracks - static_cast<int> (trackData != nullptr ? trackData->size() : 0);
    menu.addItem (1, "Add a track", room > 0);
    menu.addItem (10, "Add 10 tracks", room >= 10);
    menu.addItem (100, "Add 100 tracks", room >= 100);
    menu.addSeparator();
    menu.addItem (-1, "Remove the last track (if nothing uses it)", trackData != nullptr && trackData->size() > static_cast<size_t> (kDefaultTracks));
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addTrackButton),
                        [safe = juce::Component::SafePointer<MixerPanel> (this)] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result < 0)
                            {
                                if (safe->onRemoveLastTrack)
                                    safe->onRemoveLastTrack();
                            }
                            else if (safe->onAddTracks)
                                safe->onAddTracks (result);
                        });
}

void MixerPanel::addStrip (size_t i)
{
    auto& tracks = *trackData;
    {
        auto strip = std::make_unique<MixerStrip> (tracks[i], static_cast<int> (i));
        strip->onSelect = [this] (int idx) { setSelectedTrack (idx); };
        strip->onChanged = [this] (int idx) { updateVcaMembers(); if (onTrackChanged) onTrackChanged (idx); };
        strip->onAutomate = [this] (const AutomationTarget& t) { if (onAutomate) onAutomate (t); };
        strip->onRouteMenu = [this] (int idx, juce::Component& button) { showRouteMenu (idx, button); };
        strip->onSlotClicked = [this] (int idx, int slot)
        {
            setSelectedTrack (idx);
            fxPanel.setSelectedSlot (slot);
            strips[static_cast<size_t> (idx)]->setSelectedSlot (slot);
            if ((*trackData)[static_cast<size_t> (idx)].slots[static_cast<size_t> (slot)].type == EffectType::None)
                fxPanel.showSlotMenu (slot);
        };
        strip->onSlotMenu = [this] (int idx, int slot)
        {
            setSelectedTrack (idx);
            fxPanel.setSelectedSlot (slot);
            strips[static_cast<size_t> (idx)]->setSelectedSlot (slot);
            fxPanel.showSlotMenu (slot);
        };
        inner.addAndMakeVisible (*strip);
        strips.push_back (std::move (strip));
    }
}

// 0.70.0: a track delay typed in (the routing menu's Track delay > Other...).
void MixerPanel::askTrackDelay (int index)
{
    if (trackData == nullptr || index < 0 || index >= static_cast<int> (trackData->size()))
        return;
    auto* window = new juce::AlertWindow ("Track " + juce::String (index + 1) + ": delay",
                                          "Milliseconds by which this track's sound is delayed (0 to " + juce::String (kMaxTrackDelayMs, 0)
                                              + "), on top of the automatic delay compensation.",
                                          juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("ms", juce::String ((*trackData)[static_cast<size_t> (index)].delayMs, 1), "Delay (ms)");
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<MixerPanel> (this), window, index] (int result)
    {
        if (result != 1 || safe == nullptr || safe->trackData == nullptr || index >= static_cast<int> (safe->trackData->size()))
            return;
        auto& t = (*safe->trackData)[static_cast<size_t> (index)];
        t.delayMs = juce::jlimit (0.0f, kMaxTrackDelayMs, window->getTextEditorContents ("ms").replaceCharacter (',', '.').getFloatValue());
        safe->strips[static_cast<size_t> (index)]->refreshFromTrack();
        if (safe->onTrackChanged)
            safe->onTrackChanged (index);
    }),
                             true);
}

// 0.62.0: where a track goes. FL Studio draws cables from the selected track to the others; here it is a menu on the track.
void MixerPanel::showRouteMenu (int index, juce::Component& button)
{
    if (trackData == nullptr || index < 0 || index >= static_cast<int> (trackData->size()))
        return;
    const auto& track = (*trackData)[static_cast<size_t> (index)];
    const int count = static_cast<int> (trackData->size());
    const auto nameOf = [this] (int t)
    {
        const auto number = "Track " + juce::String (t + 1);
        const auto& name = (*trackData)[static_cast<size_t> (t)].name;
        return name.empty() || juce::String (name) == number ? number : number + ": " + juce::String (name);
    };
    juce::PopupMenu menu;
    menu.addSectionHeader ("Track " + juce::String (index + 1) + " goes to");
    menu.addItem (1, "The master", true, track.toMaster);
    juce::PopupMenu sendTo, onlyTo;
    int used = 0;
    for (const auto& s : track.trackSends)
        used += s.target >= 0 ? 1 : 0;
    for (int t = 0; t < count; ++t)
    {
        if (t == index)
            continue;
        const bool sending = track.sendsTo (t);
        sendTo.addItem (1000 + t, nameOf (t), sending || used < kTrackSends, sending);
        onlyTo.addItem (3000 + t, nameOf (t));
    }
    menu.addSubMenu ("Also send to", sendTo);
    menu.addSubMenu ("Route to this track only", onlyTo);
    static constexpr int kLevels[] = { 25, 50, 75, 100, 125, 150 };
    bool header = false;
    for (size_t s = 0; s < track.trackSends.size(); ++s)
    {
        const auto& send = track.trackSends[s];
        if (send.target < 0 || send.target >= count)
            continue;
        if (! header)
            menu.addSeparator();
        header = true;
        juce::PopupMenu levels;
        for (size_t l = 0; l < std::size (kLevels); ++l)
            levels.addItem (5000 + static_cast<int> (s) * 10 + static_cast<int> (l), juce::String (kLevels[l]) + " %", true,
                            juce::roundToInt (send.level * 100.0f) == kLevels[l]);
        levels.addSeparator();
        levels.addItem (5000 + static_cast<int> (s) * 10 + 8, "Before the fader (pre-fader)", true, send.preFader); // 0.70.0
        levels.addItem (5000 + static_cast<int> (s) * 10 + 9, "Remove this send");
        menu.addSubMenu ("Level to " + nameOf (send.target) + "  (" + juce::String (juce::roundToInt (send.level * 100.0f)) + " %"
                             + (send.preFader ? ", pre-fader)" : ")"),
                         levels);
    }
    menu.addSeparator();
    menu.addItem (2, "Reset (the master only)", ! track.toMaster || used > 0);
    // 0.70.0: the track's own delay and its colour
    static constexpr float kDelays[] = { 0.0f, 1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 30.0f, 50.0f, 100.0f, 200.0f };
    static constexpr std::uint32_t kColours[] = { 0xffd9534f, 0xffe8843a, 0xffe8a93a, 0xffd9c94a, 0xff9ccc4a, 0xff4cc38a,
                                                  0xff4ac9b0, 0xff4a9fd9, 0xff6a7fe0, 0xff9a6fe0, 0xffd06fc8, 0xff9aa0ab };
    static constexpr const char* kColourNames[] = { "Red", "Orange", "Amber", "Yellow", "Lime", "Green", "Teal", "Blue", "Indigo", "Violet", "Pink", "Grey" };
    menu.addSeparator();
    juce::PopupMenu delays;
    bool listed = false;
    for (size_t d = 0; d < std::size (kDelays); ++d)
    {
        const bool current = std::abs (track.delayMs - kDelays[d]) < 0.05f;
        listed = listed || current;
        delays.addItem (7000 + static_cast<int> (d), d == 0 ? juce::String ("None") : juce::String (kDelays[d], 0) + " ms", true, current);
    }
    delays.addSeparator();
    delays.addItem (7100, "Other..." + (listed ? juce::String() : "  (" + juce::String (track.delayMs, 1) + " ms)"), true, ! listed);
    menu.addSubMenu ("Track delay" + (track.delayMs > 0.0f ? "  (" + juce::String (track.delayMs, 1) + " ms)" : juce::String()), delays);
    juce::PopupMenu colours;
    colours.addItem (8000, "Default", true, track.colour == 0);
    for (size_t c = 0; c < std::size (kColours); ++c)
        colours.addItem (8001 + static_cast<int> (c), kColourNames[c], true, track.colour == kColours[c]);
    menu.addSubMenu ("Strip colour", colours);
    // 0.72.0: its icon, where it is docked, a gap before it
    juce::PopupMenu icons, docks;
    addIconItems (icons, 9000, track.icon);
    menu.addSubMenu ("Strip icon", icons);
    docks.addItem (9100, "With the others", true, track.dock == 0);
    docks.addItem (9101, "On the left", true, track.dock == 1);
    docks.addItem (9102, "On the right (before the + button)", true, track.dock == 2);
    menu.addSubMenu ("Dock", docks);
    menu.addItem (9110, "Separator before this strip", true, track.separator);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&button),
                        [safe = juce::Component::SafePointer<MixerPanel> (this), index] (int result)
                        {
                            if (safe == nullptr || result == 0 || safe->trackData == nullptr || index >= static_cast<int> (safe->trackData->size()))
                                return;
                            auto& t = (*safe->trackData)[static_cast<size_t> (index)];
                            const auto freeSend = [&t] () -> TrackSend*
                            {
                                for (auto& s : t.trackSends)
                                    if (s.target < 0)
                                        return &s;
                                return nullptr;
                            };
                            if (result == 1)
                                t.toMaster = ! t.toMaster;
                            else if (result == 2)
                            {
                                t.toMaster = true;
                                t.trackSends = {};
                            }
                            else if (result >= 9000) // 0.72.0
                            {
                                if (result == 9110)
                                    t.separator = ! t.separator;
                                else if (result >= 9100)
                                    t.dock = static_cast<std::uint8_t> (result - 9100);
                                else
                                    t.icon = static_cast<std::uint8_t> (std::clamp (result - 9000, 0, kIconCount));
                                safe->layoutInner();
                                safe->strips[static_cast<size_t> (index)]->repaint();
                            }
                            else if (result >= 8000) // 0.70.0
                                t.colour = result == 8000 ? 0u : kColours[static_cast<size_t> (result - 8001) % std::size (kColours)];
                            else if (result == 7100)
                            {
                                safe->askTrackDelay (index);
                                return;
                            }
                            else if (result >= 7000)
                                t.delayMs = kDelays[static_cast<size_t> (result - 7000) % std::size (kDelays)];
                            else if (result >= 5000)
                            {
                                auto& send = t.trackSends[static_cast<size_t> ((result - 5000) / 10) % t.trackSends.size()];
                                const int choice = (result - 5000) % 10;
                                if (choice == 8)
                                    send.preFader = ! send.preFader;
                                else if (choice == 9)
                                    send = {};
                                else
                                    send.level = static_cast<float> (kLevels[static_cast<size_t> (choice) % std::size (kLevels)]) / 100.0f;
                            }
                            else if (result >= 3000)
                            {
                                t.toMaster = false;
                                t.trackSends = {};
                                t.trackSends[0] = { result - 3000, 1.0f, false };
                            }
                            else if (result >= 1000)
                            {
                                const int target = result - 1000;
                                bool removed = false;
                                for (auto& s : t.trackSends)
                                    if (s.target == target)
                                    {
                                        s = {};
                                        removed = true;
                                    }
                                if (! removed)
                                    if (auto* s = freeSend())
                                        *s = { target, 1.0f, false };
                            }
                            safe->strips[static_cast<size_t> (index)]->refreshFromTrack();
                            if (safe->onTrackChanged)
                                safe->onTrackChanged (index);
                        });
}

void MixerPanel::refreshTrack (int index)
{
    if (index == kMasterIndex)
    {
        masterStrip.refreshSlots();
        if (selectedTrack == kMasterIndex)
            fxPanel.refresh();
        return;
    }
    if (index >= kAuxIndexBase)
    {
        const auto a = static_cast<size_t> (index - kAuxIndexBase);
        if (a < auxStrips.size())
            auxStrips[a]->refreshFromTrack();
        if (index == selectedTrack)
            fxPanel.refresh();
        return;
    }
    if (index >= 0 && index < static_cast<int> (strips.size()))
        strips[static_cast<size_t> (index)]->refreshFromTrack();
    if (index == selectedTrack)
        fxPanel.refresh();
}

void MixerPanel::setSelectedTrack (int index)
{
    if (trackData == nullptr || trackData->empty())
        return;

    if (index == kMasterIndex)
    {
        selectMaster();
        return;
    }
    if (index >= kAuxIndexBase)
    {
        selectAux (index - kAuxIndexBase);
        return;
    }

    masterStrip.setSelected (false);
    for (auto& s : auxStrips)
        s->setSelected (false);
    selectedTrack = std::clamp (index, 0, static_cast<int> (trackData->size()) - 1);
    for (auto& s : strips)
        s->setSelected (s->getIndex() == selectedTrack);

    fxPanel.setTrack (&(*trackData)[static_cast<size_t> (selectedTrack)], selectedTrack);
    if (selectedTrack < static_cast<int> (strips.size()))
    {
        auto& strip = *strips[static_cast<size_t> (selectedTrack)];
        strip.setSelectedSlot (fxPanel.getSelectedSlot());
        // scrolled into view if it is not (a track just added past the visible ones, or chosen elsewhere)
        const auto view = viewport.getViewArea();
        if (strip.getRight() > view.getRight() || strip.getX() < view.getX())
            viewport.setViewPosition (std::max (0, strip.getX() - (view.getWidth() - strip.getWidth()) / 2), view.getY());
    }
}

void MixerPanel::setTrackChannels (int index, const juce::String& channelNames)
{
    if (index >= 0 && index < static_cast<int> (strips.size()))
        strips[static_cast<size_t> (index)]->setRoutedChannels (channelNames);
}

void MixerPanel::setTrackLevels (int index, float peak, float rms)
{
    if (index >= 0 && index < static_cast<int> (strips.size()))
        strips[static_cast<size_t> (index)]->setLevels (peak, rms);
}

void MixerPanel::paint (juce::Graphics& g)
{
    g.fillAll (kPanelBg);
}

void MixerPanel::resized()
{
    auto area = getLocalBounds().reduced (4);
    fxPanel.setBounds (area.removeFromRight (kFxPanelWidth));
    area.removeFromRight (4);
    masterStrip.setBounds (area.removeFromRight (MixerStrip::kWidth));
    area.removeFromRight (4);
    viewport.setBounds (area);
    layoutInner();
}

void MixerPanel::layoutInner()
{
    constexpr int kGap = 10; // between the tracks, the aux buses and the VCA groups
    const int count = static_cast<int> (strips.size() + auxStrips.size() + vcaStrips.size());
    int separators = 0; // (0.72.0)
    if (trackData != nullptr)
        for (const auto& t : *trackData)
            separators += t.separator ? 1 : 0;
    const int needed = count * MixerStrip::kWidth + kAddButtonWidth + (auxStrips.empty() ? 0 : kGap) + (vcaStrips.empty() ? 0 : kGap) + separators * 8;
    const int height = std::max (1, viewport.getMaximumVisibleHeight());
    inner.setSize (std::max (needed, viewport.getMaximumVisibleWidth()), height);

    // 0.72.0: the strips docked on the left first, then the others, then those docked on the right; a separator leaves a gap
    constexpr int kSeparator = 8;
    int x = 0;
    const auto place = [&] (int dock)
    {
        for (size_t i = 0; i < strips.size(); ++i)
        {
            const auto* data = trackData != nullptr && i < trackData->size() ? &(*trackData)[i] : nullptr;
            if ((data != nullptr ? data->dock : 0) != dock)
                continue;
            if (data != nullptr && data->separator && x > 0)
                x += kSeparator;
            strips[i]->setBounds (x, 0, MixerStrip::kWidth, height);
            x += MixerStrip::kWidth;
        }
    };
    place (1);
    place (0);
    place (2);
    addTrackButton.setBounds (x + 2, 6, kAddButtonWidth - 4, 28);
    x += kAddButtonWidth;
    if (! auxStrips.empty())
        x += kGap;
    for (auto& s : auxStrips)
    {
        s->setBounds (x, 0, MixerStrip::kWidth, height);
        x += MixerStrip::kWidth;
    }
    if (! vcaStrips.empty())
        x += kGap;
    for (auto& s : vcaStrips)
    {
        s->setBounds (x, 0, MixerStrip::kWidth, height);
        x += MixerStrip::kWidth;
    }
}
} // namespace sls

namespace sls
{
void MixerPanel::showAutomated (const AutomationTarget& target, float value)
{
    const auto in = [] (const auto& v, int i) { return i >= 0 && i < static_cast<int> (v.size()); };
    switch (target.kind)
    {
        case AutomationKind::TrackVolume:
        case AutomationKind::TrackPan:
        case AutomationKind::TrackSend:
            if (in (strips, target.index))
                strips[static_cast<size_t> (target.index)]->showAutomated (target.kind, target.slot, value);
            break;
        case AutomationKind::AuxVolume:
        case AutomationKind::AuxPan:
            if (in (auxStrips, target.slot))
                auxStrips[static_cast<size_t> (target.slot)]->showAutomated (target.kind, 0, value);
            break;
        case AutomationKind::VcaLevel:
            if (in (vcaStrips, target.slot))
                vcaStrips[static_cast<size_t> (target.slot)]->showAutomated (value);
            break;
        case AutomationKind::ChannelVolume:
        case AutomationKind::ChannelPan:
        case AutomationKind::TrackEffect:
        case AutomationKind::MasterEffect:
        case AutomationKind::ChannelPlugin:
        case AutomationKind::TrackPlugin:
        case AutomationKind::MasterPlugin: break;
    }
}

void MixerPanel::refreshControls()
{
    for (auto& s : strips)
        s->refreshFromTrack();
    for (auto& s : auxStrips)
        s->refreshFromTrack();
    for (auto& s : vcaStrips)
        s->refresh();
}
} // namespace sls
