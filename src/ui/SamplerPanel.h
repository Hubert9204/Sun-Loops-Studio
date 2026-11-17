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
    std::function<void()> onSpeech;           // the "Speech..." button (0.74.0: the speech synthesizer)

    void refresh(); // show the channel as it is now

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void write (const std::function<void (Channel&)>& change);
    juce::Slider& slider (juce::Slider& s, double lo, double hi, double step, const juce::String& suffix);

    juce::Label sourceLabel, programInfo;
    juce::TextButton loadSampleButton { "Load sample..." }, loadFontButton { "Load SoundFont (.sf2)..." }, clearProgramButton { "Use the sample" },
        speechButton { "Speech..." };
    juce::Slider rootSlider;
    juce::ToggleButton envelopeToggle { "Envelope (notes sustain while held)" };
    juce::Slider attackSlider, decaySlider, sustainSlider, releaseSlider;
    juce::ComboBox loopBox, filterBox;
    juce::Slider loopStartSlider, loopEndSlider, cutoffSlider, resonanceSlider;
    // 0.61.0, as FL Studio's sampler channel: Reverse, SMP START, the IN and OUT fades
    juce::ToggleButton reverseToggle { "Reverse" };
    juce::Slider startSlider, fadeInSlider, fadeOutSlider;
    juce::Label startLabel { {}, "Start" }, fadeInLabel { {}, "Fade in" }, fadeOutLabel { {}, "Fade out" };
    // 0.62.0: loop crossfade, the envelope's hold on the cutoff, the LFO and the pitch envelope
    juce::Slider crossfadeSlider, cutoffEnvSlider, lfoRateSlider, lfoDepthSlider, pitchEnvSlider, pitchEnvTimeSlider;
    juce::ComboBox lfoBox;
    // 0.70.0: the envelope's delay and hold, and a second LFO
    juce::Slider delaySlider, holdSlider, lfo2RateSlider, lfo2DepthSlider;
    juce::ComboBox lfo2Box;
    juce::Label delayLabel { {}, "Delay" }, holdLabel { {}, "Hold" }, lfo2Label { {}, "LFO 2" }, lfo2RateLabel { {}, "Rate" }, lfo2DepthLabel { {}, "Depth" };
    // 0.71.0: the envelopes of pan, cutoff, resonance and pitch (one shown at a time, as FL Studio's tabs)
    juce::Label envLabel { {}, "Envelopes" }, envALabel { {}, "A" }, envDLabel { {}, "D" }, envSLabel { {}, "S" }, envRLabel { {}, "R" }, envAmountLabel { {}, "Amount" };
    juce::ComboBox envTargetBox;
    juce::ToggleButton envOnToggle { "On" };
    juce::Slider envAttackSlider, envDecaySlider, envSustainSlider, envReleaseSlider, envAmountSlider;
    int envShown = 0;
    void writeEnvelope (const std::function<void (SamplerSettings::ModEnvelope&)>& change);
    juce::Label crossfadeLabel { {}, "Crossfade" }, cutoffEnvLabel { {}, "Envelope" }, lfoLabel { {}, "LFO" }, lfoRateLabel { {}, "Rate" },
        lfoDepthLabel { {}, "Depth" }, pitchEnvLabel { {}, "Pitch env." }, pitchEnvTimeLabel { {}, "Time" };
    juce::Label rootLabel { {}, "Root key" }, attackLabel { {}, "Attack" }, decayLabel { {}, "Decay" }, sustainLabel { {}, "Sustain" },
        releaseLabel { {}, "Release" }, loopLabel { {}, "Loop" }, loopStartLabel { {}, "Loop start" }, loopEndLabel { {}, "Loop end" },
        filterLabel { {}, "Filter" }, cutoffLabel { {}, "Cutoff" }, resonanceLabel { {}, "Resonance" };
    // 0.74.0: the play mode (normal, grains as the Granulizer, a travel curve as Wave Traveller) and its settings
    juce::ComboBox playModeBox, curveBox;
    juce::Slider grainSlider, densitySlider, travelTimeSlider, grainPositionSlider, grainSpeedSlider, grainSpreadSlider, grainPanSlider;
    juce::Label playModeLabel { {}, "Play mode" }, grainLabel { {}, "Grain" }, densityLabel { {}, "Density" }, curveLabel { {}, "Curve" },
        travelTimeLabel { {}, "Time" }, grainPositionLabel { {}, "Pos" }, grainSpeedLabel { {}, "Speed" }, grainSpreadLabel { {}, "Spread" },
        grainPanLabel { {}, "Pan" };
    std::array<juce::Rectangle<int>, 6> headers; // SOUND, ENVELOPE, LOOP, FILTER, MODULATION (0.62.0), PLAY MODE (0.74.0)
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
    juce::Viewport scroller; // 0.70.0: the panel scrolls on a screen that is not tall enough for it
};
} // namespace sls
