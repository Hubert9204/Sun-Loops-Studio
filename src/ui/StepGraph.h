#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/Project.h"

namespace sls
{
// The Channel Rack's graph editor (0.62.0, as FL Studio's): one bar per step of the selected channel for a property of the notes
// that start there - velocity, pan, release or fine pitch. Dragging over the bars sets them; a right click puts one back.
class StepGraph : public juce::Component
{
public:
    enum class Property { Velocity, Pan, Release, FinePitch };

    StepGraph();

    void setChannel (Channel* channelToEdit, int patternTicks);
    void setProperty (Property p);
    Property property() const noexcept { return shown; }

    std::function<void()> onEdited; // a value changed

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    static constexpr int kHeight = 112;

    // The value of `property` of a note as the graph shows it (0..1 from the bottom), and the note with that value set.
    static float valueOf (const Note& n, Property property) noexcept;
    static void setValue (Note& n, Property property, float value01) noexcept;

private:
    juce::Rectangle<int> graphArea() const noexcept;
    int stepCount() const noexcept;
    void edit (const juce::MouseEvent&);

    Channel* channel = nullptr;
    int lengthTicks = 4 * kTicksPerBeat;
    Property shown = Property::Velocity;
    juce::ComboBox propertyBox;
    juce::Label nameLabel;
};
} // namespace sls
