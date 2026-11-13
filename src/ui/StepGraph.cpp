#include "ui/StepGraph.h"

#include "audio/StepSequence.h"

namespace sls
{
namespace
{
constexpr int kStepTicks = steps::kStepTicks;
constexpr float kFineRange = 100.0f; // cents either way
} // namespace

float StepGraph::valueOf (const Note& n, Property property) noexcept
{
    switch (property)
    {
        case Property::Pan:       return 0.5f + 0.5f * std::clamp (n.pan, -1.0f, 1.0f);
        case Property::Release:   return std::clamp (n.release, 0.0f, 1.0f);
        case Property::FinePitch: return 0.5f + 0.5f * std::clamp (n.finePitch / kFineRange, -1.0f, 1.0f);
        case Property::Velocity:  break;
    }
    return std::clamp (n.velocity, 0.0f, 1.0f);
}

void StepGraph::setValue (Note& n, Property property, float value01) noexcept
{
    const float v = std::clamp (value01, 0.0f, 1.0f);
    switch (property)
    {
        case Property::Pan:       n.pan = std::abs (v - 0.5f) < 0.03f ? 0.0f : v * 2.0f - 1.0f; break; // (the middle catches)
        case Property::Release:   n.release = v; break;
        case Property::FinePitch: n.finePitch = std::abs (v - 0.5f) < 0.03f ? 0.0f : (v * 2.0f - 1.0f) * kFineRange; break;
        case Property::Velocity:  n.velocity = std::max (0.02f, v); break; // (a note of no velocity would be a rest)
    }
}

StepGraph::StepGraph()
{
    propertyBox.addItem ("Velocity", 1);
    propertyBox.addItem ("Pan", 2);
    propertyBox.addItem ("Release", 3);
    propertyBox.addItem ("Fine pitch", 4);
    propertyBox.setSelectedId (1, juce::dontSendNotification);
    propertyBox.setTooltip ("What the bars show and set for the notes of each step");
    propertyBox.onChange = [this] { setProperty (static_cast<Property> (propertyBox.getSelectedId() - 1)); };
    addAndMakeVisible (propertyBox);
    nameLabel.setFont (juce::FontOptions (12.0f));
    nameLabel.setColour (juce::Label::textColourId, juce::Colour (0xffd0d4dc));
    addAndMakeVisible (nameLabel);
}

void StepGraph::setChannel (Channel* channelToEdit, int patternTicks)
{
    channel = channelToEdit;
    lengthTicks = std::max (kStepTicks, patternTicks);
    nameLabel.setText (channel != nullptr ? "Graph editor: " + juce::String (channel->name) : juce::String ("Graph editor"), juce::dontSendNotification);
    repaint();
}

void StepGraph::setProperty (Property p)
{
    shown = p;
    propertyBox.setSelectedId (static_cast<int> (p) + 1, juce::dontSendNotification);
    repaint();
}

int StepGraph::stepCount() const noexcept { return std::max (1, lengthTicks / kStepTicks); }

juce::Rectangle<int> StepGraph::graphArea() const noexcept
{
    // The bars line up with the step buttons of the rows above: the same left edge and right margin as a ChannelStrip's grid.
    return getLocalBounds().withTrimmedLeft (533).withTrimmedRight (8).withTrimmedTop (6).withTrimmedBottom (6);
}

void StepGraph::resized()
{
    auto left = getLocalBounds().removeFromLeft (525).reduced (8, 6);
    nameLabel.setBounds (left.removeFromTop (20));
    left.removeFromTop (4);
    propertyBox.setBounds (left.removeFromTop (24).removeFromLeft (150));
}

void StepGraph::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1e24));
    const auto area = graphArea().toFloat();
    if (area.getWidth() < 8.0f)
        return;
    g.setColour (juce::Colour (0xff14161a));
    g.fillRect (area);
    const bool centred = shown == Property::Pan || shown == Property::FinePitch;
    if (centred)
    {
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());
    }
    const int count = stepCount();
    const float stepWidth = area.getWidth() / static_cast<float> (count);
    for (int s = 0; s < count; ++s)
    {
        if (s % 4 == 0)
        {
            g.setColour (juce::Colours::white.withAlpha (s % 16 == 0 ? 0.16f : 0.07f));
            g.drawVerticalLine (juce::roundToInt (area.getX() + static_cast<float> (s) * stepWidth), area.getY(), area.getBottom());
        }
        if (channel == nullptr)
            continue;
        for (const auto& n : channel->notes)
        {
            if (n.startTick / kStepTicks != s)
                continue;
            const float v = valueOf (n, shown);
            const float x = area.getX() + static_cast<float> (s) * stepWidth + 1.0f;
            const float w = std::max (2.0f, stepWidth - 2.0f);
            const float y = area.getBottom() - v * area.getHeight();
            g.setColour (n.muted ? juce::Colour (0xff6a6f78) : juce::Colour (0xff4cc38a).withAlpha (0.85f));
            if (centred)
                g.fillRect (juce::Rectangle<float> (x, std::min (y, area.getCentreY()), w, std::max (2.0f, std::abs (y - area.getCentreY()))));
            else
                g.fillRect (juce::Rectangle<float> (x, y, w, area.getBottom() - y));
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> (x, y - 1.0f, w, 2.0f));
            break; // one bar per step: the first note that starts there (a drag sets them all)
        }
    }
    if (channel != nullptr && channel->notes.empty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("No notes in this pattern yet: switch some steps on above", area, juce::Justification::centred);
    }
}

void StepGraph::edit (const juce::MouseEvent& e)
{
    const auto area = graphArea();
    if (channel == nullptr || area.getWidth() < 8)
        return;
    const int step = std::clamp ((e.x - area.getX()) * stepCount() / area.getWidth(), 0, stepCount() - 1);
    // A right click puts the step back to the default of its property.
    const float defaults[] = { 0.8f, 0.5f, 0.5f, 0.5f };
    const float value = e.mods.isPopupMenu() ? defaults[static_cast<int> (shown)]
                                             : 1.0f - static_cast<float> (e.y - area.getY()) / static_cast<float> (std::max (1, area.getHeight()));
    bool changed = false;
    for (auto& n : channel->notes)
        if (n.startTick / kStepTicks == step)
        {
            const Note before = n;
            setValue (n, shown, value);
            changed = changed || std::abs (before.velocity - n.velocity) > 0.0f || std::abs (before.pan - n.pan) > 0.0f
                      || std::abs (before.release - n.release) > 0.0f || std::abs (before.finePitch - n.finePitch) > 0.0f;
        }
    if (changed)
    {
        repaint();
        if (onEdited)
            onEdited();
    }
}

void StepGraph::mouseDown (const juce::MouseEvent& e) { edit (e); }
void StepGraph::mouseDrag (const juce::MouseEvent& e) { edit (e); }
} // namespace sls
