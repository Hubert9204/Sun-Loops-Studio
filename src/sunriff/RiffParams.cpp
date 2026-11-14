#include "sunriff/RiffParams.h"

#include "sunriff/RiffFx.h"
#include "synthkit/ParamKit.h"

namespace sunriff
{
const juce::StringArray& modeNames()
{
    static const juce::StringArray names { "None", "Autochords", "Powerchords", "Strumming", "SoloFixed", "SoloDynamic" };
    return names;
}
const juce::StringArray& stringNames()
{
    static const juce::StringArray names { "Noise", "6String1", "6String2", "6String3", "Slap", "EBass", "Fretless" };
    return names;
}
const juce::StringArray& coilNames()
{
    static const juce::StringArray names { "None", "Single", "Double" };
    return names;
}
const juce::StringArray& ampNames()
{
    static const juce::StringArray names { "Dry", "Tube", "EQ", "Bandpass" };
    return names;
}
const juce::StringArray& cabinetNames()
{
    static const juce::StringArray names { "Dry", "British", "Combo", "StaX" };
    return names;
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    synthkit::Adder add { params };
    const juce::NormalisableRange<float> unit (0.0f, 1.0f), bipolar (-1.0f, 1.0f);
    // play
    add.choice (id::mode, "Playing mode", modeNames(), 0);
    add.real (id::speed, "Speed", unit, 0.5f);
    add.toggle (id::hold, "Hold", false);
    add.choice (id::gliss, "Glissando", { "PB1", "PB2" }, 0);
    add.real (id::bend2, "Pitch Bend 2", bipolar, 0.0f);
    // string and pickup
    add.choice (id::string, "String", stringNames(), 1);
    add.choice (id::coil, "Coil", coilNames(), 1);
    add.real (id::pickup, "Pickup position", unit, 0.25f);
    add.real (id::tone, "Tone", unit, 0.5f);
    add.real (id::slap, "Slap", unit, 0.0f);
    add.real (id::fret, "Fret", unit, 0.25f);
    add.real (id::harmonic, "Harmonic", unit, 0.55f);
    add.real (id::vel, "Vel", unit, 0.6f);
    add.real (id::damping, "Damping", unit, 0.35f);
    add.real (id::dampVel, "Damping Vel", unit, 0.3f);
    // amp
    add.choice (id::amp, "Amp", ampNames(), 1);
    add.choice (id::cabinet, "Cabinet", cabinetNames(), 1);
    add.real (id::drive, "Drive", unit, 0.35f);
    add.real (id::presence, "Presence", bipolar, 0.0f);
    add.real (id::feedback, "Feedback", unit, 0.0f);
    add.real (id::low, "Low", bipolar, 0.0f);
    add.real (id::mid, "Mid", bipolar, 0.0f);
    add.real (id::high, "High", bipolar, 0.0f);
    // effects and output
    add.choice (id::fx, "Effect", fxNames(), 0);
    add.real (id::fx1, "Effect param 1", unit, 0.5f);
    add.real (id::fx2, "Effect param 2", unit, 0.5f);
    add.real (id::master, "Master", juce::NormalisableRange<float> (0.0f, 1.5f), 0.8f);
    return { params.begin(), params.end() };
}
} // namespace sunriff
