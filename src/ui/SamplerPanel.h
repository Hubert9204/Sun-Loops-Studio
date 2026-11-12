#pragma once

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/Project.h"

namespace sls
{
// The settings of one sampler channel (0.16.0): what it plays (its sample, or a SoundFont preset / slice program), the root key, the
// envelope (on: notes sustain while held and fade with the release; off: one-shot), the loop, and the filter.
// Like the drum machine it never keeps a copy of the channel: `getChannel` returns the live channel (nullptr when it is gone or is no
// longer a sampler) and every change is written to it and announced with `onChanged`.
class SamplerPanel : public juce::Component
{
public:
    SamplerPanel();

    std::function<Channel*()> getChannel;
    std::function<void()> onChanged;          // a setting changed: publish the project
    std::function<void()> onLoadSample;       // the "Load sample..." button
    std::function<void()> onLoadSoundFont;    // the "Load SoundFont..." button
    std::function<void()> onClearProgram;     // the "Use the sample" button (forget the program)

    void refresh(); // show the channel as it is now

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void write (const std::function<void (Channel&)>& change);
    juce::Slider& slider (juce::Slider& s, double lo, double hi, double step, const juce::String& suffix);

    juce::Label sourceLabel, programInfo;
    juce::TextButton loadSampleButton { "Load sample..." }, loadFontButton { "Load SoundFont (.sf2)..." }, clearProgramButton { "Use the sample" };
    juce::Slider rootSlider;
    juce::ToggleButton envelopeToggle { "Envelope (notes sustain while held)" };
    juce::Slider attackSlider, decaySlider, sustainSlider, releaseSlider;
    juce::ComboBox loopBox, filterBox;
    juce::Slider loopStartSlider, loopEndSlider, cutoffSlider, resonanceSlider;
    // 0.61.0, as FL Studio's sampler channel: Reverse, SMP START, the IN and OUT fades
    juce::ToggleButton reverseToggle { "Reverse" };
    juce::Slider startSlider, fadeInSlider, fadeOutSlider;
    juce::Label startLabel { {}, "Start" }, fadeInLabel { {}, "Fade in" }, fadeOutLabel { {}, "Fade out" };
    juce::Label rootLabel { {}, "Root key" }, attackLabel { {}, "Attack" }, decayLabel { {}, "Decay" }, sustainLabel { {}, "Sustain" },
        releaseLabel { {}, "Release" }, loopLabel { {}, "Loop" }, loopStartLabel { {}, "Loop start" }, loopEndLabel { {}, "Loop end" },
        filterLabel { {}, "Filter" }, cutoffLabel { {}, "Cutoff" }, resonanceLabel { {}, "Resonance" };
    std::array<juce::Rectangle<int>, 4> headers; // SOUND, ENVELOPE, LOOP, FILTER
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplerPanel)
};

class SamplerWindow : public juce::DocumentWindow
{
public:
    SamplerWindow();
    ~SamplerWindow() override { clearContentComponent(); }
    SamplerPanel& panel() noexcept { return content; }
    std::function<void()> onClose;
    void closeButtonPressed() override { if (onClose) onClose(); }

private:
    SamplerPanel content;
};
} // namespace sls
