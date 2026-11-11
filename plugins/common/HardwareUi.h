#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

// The hardware-style controls shared by Sun Loops Studio's plugins (SunBurst 0.48.0, HexForge 0.50.0): metal knobs with a value
// ring, LED switches, combo boxes and titled panels with screws that lay their controls out in a grid. Each plugin picks its colours.
namespace hwui
{
struct Theme
{
    juce::Colour background { 0xff121418 }, panel { 0xff1d2128 }, edge { 0xff313844 }, accent { 0xfff59a2f }, accent2 { 0xff41c3cc },
        text { 0xffdfe4ec }, dim { 0xff8b94a3 }, knobBody { 0xff2b3039 };
};

// The colours in use (one plugin per library, so one theme each); a Look constructed with a theme makes it the current one.
Theme& theme();
void drawScrew (juce::Graphics& g, float cx, float cy);
void styleLabel (juce::Label& l);

// The hardware-style look: metal knobs with a value ring, LED switches, dark panels with screws.
class Look : public juce::LookAndFeel_V4
{
public:
    Look();
    explicit Look (const Theme& t);
    void applyColours();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
};

// A labelled control bound to a parameter.
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& text, bool bipolar = false);
    void resized() override;
    juce::Slider slider;
    juce::Label label;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class Choice : public juce::Component
{
public:
    Choice (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& text);
    void resized() override;
    juce::ComboBox box;
    juce::Label label;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

class Switch : public juce::Component
{
public:
    Switch (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& text);
    void resized() override { button.setBounds (getLocalBounds().withSizeKeepingCentre (getWidth(), 22)); }
    juce::ToggleButton button;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

// A titled panel that lays its controls out in a grid (with an optional display above them and tabs in its title bar).
class Panel : public juce::Component
{
public:
    Panel (const juce::String& title, int columns, int displayHeight = 0);
    void paint (juce::Graphics&) override;
    void resized() override;

    juce::Component* add (std::unique_ptr<juce::Component> c, int tab = 0, int span = 1); // span: columns it takes
    void setPower (std::unique_ptr<Switch> s);
    void setDisplay (juce::Component* d) { display = d; addAndMakeVisible (d); }
    void setTabs (const juce::StringArray& names);
    void showTab (int tab);
    int currentTab() const noexcept { return tab; }
    std::function<void (int)> onTab;

private:
    juce::String title;
    int columns, displayHeight;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<int> itemTab, itemSpan;
    std::unique_ptr<Switch> power;
    juce::Component* display = nullptr;
    juce::OwnedArray<juce::TextButton> tabButtons;
    int tab = 0;
};
} // namespace hwui
