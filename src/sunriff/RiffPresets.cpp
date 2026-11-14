#include "sunriff/RiffPresets.h"

#include "sunriff/RiffFx.h"
#include "sunriff/RiffParams.h"

// SunRiff's factory presets (0.68.0): the parameters that differ from their defaults, in their own units (a choice by its index).
namespace sunriff
{
namespace
{
enum Mode : int { MNone, MAuto, MPower, MStrum, MSoloFixed, MSoloDynamic };
enum Str : int { SNoise, S1, S2, S3, SSlap, SEBass, SFretless };
enum CoilT : int { CNone, CSingle, CDouble };
enum AmpT : int { ADry, ATube, AEq, ABand };
enum Cab : int { CabDry, CabBritish, CabCombo, CabStax };

class B
{
public:
    B (const char* n, const char* c) : preset { n, c, {} } {}
    B& set (const char* id, float v) { preset.values.push_back ({ id, v }); return *this; }
    B& play (int mode, float speed = 0.5f) { return set (id::mode, static_cast<float> (mode)).set (id::speed, speed); }
    B& string (int type, int coil, float pickup) { return set (id::string, static_cast<float> (type)).set (id::coil, static_cast<float> (coil)).set (id::pickup, pickup); }
    B& feel (float tone, float harmonic, float damping, float fret = 0.25f, float vel = 0.6f)
    {
        return set (id::tone, tone).set (id::harmonic, harmonic).set (id::damping, damping).set (id::fret, fret).set (id::vel, vel);
    }
    B& amp (int type, int cab, float drive, float presence = 0.0f, float low = 0.0f, float mid = 0.0f, float high = 0.0f)
    {
        return set (id::amp, static_cast<float> (type)).set (id::cabinet, static_cast<float> (cab)).set (id::drive, drive)
            .set (id::presence, presence).set (id::low, low).set (id::mid, mid).set (id::high, high);
    }
    B& fx (int type, float p1, float p2) { return set (id::fx, static_cast<float> (type)).set (id::fx1, p1).set (id::fx2, p2); }
    operator synthkit::FactoryPreset() const { return preset; }

private:
    synthkit::FactoryPreset preset;
};
} // namespace

const juce::StringArray& presetCategories()
{
    static const juce::StringArray names { "Clean", "Crunch", "Lead", "Metal", "Bass", "FX" };
    return names;
}

const std::vector<synthkit::FactoryPreset>& factoryPresets()
{
    static const std::vector<synthkit::FactoryPreset> presets {
        // Clean
        B ("Clean Strat", "Clean").string (S1, CSingle, 0.55f).feel (0.5f, 0.65f, 0.3f).amp (ATube, CabCombo, 0.12f, 0.2f, 0.0f, -0.1f, 0.2f).fx (FxChorus, 0.3f, 0.35f),
        B ("Jazz Box", "Clean").string (S3, CDouble, 0.9f).feel (0.35f, 0.25f, 0.4f, 0.1f).amp (ATube, CabCombo, 0.08f, -0.4f, 0.25f, 0.0f, -0.35f),
        B ("Funk Chops", "Clean").string (S2, CSingle, 0.15f).feel (0.6f, 0.8f, 0.75f, 0.35f, 0.8f).amp (ATube, CabCombo, 0.15f, 0.35f, -0.2f, 0.2f, 0.25f).fx (FxWah, 0.7f, 0.75f),
        B ("Strummed Chords", "Clean").play (MStrum, 0.6f).string (S1, CSingle, 0.5f).feel (0.5f, 0.6f, 0.3f).amp (ATube, CabCombo, 0.15f, 0.1f).fx (FxChorus, 0.25f, 0.3f),
        B ("Auto Chords", "Clean").play (MAuto, 0.55f).string (S2, CSingle, 0.45f).feel (0.5f, 0.7f, 0.35f).amp (ATube, CabCombo, 0.12f, 0.2f).fx (FxDubDelay, 0.35f, 0.3f),
        B ("Unplugged", "Clean").string (S2, CNone, 0.5f).feel (0.65f, 0.7f, 0.4f, 0.4f).amp (ADry, CabDry, 0.3f, 0.0f, 0.15f, -0.1f, 0.2f),
        B ("Classic Pluck", "Clean").string (SNoise, CNone, 0.5f).feel (0.5f, 0.6f, 0.45f, 0.0f).amp (ADry, CabDry, 0.35f),
        B ("Vibrato Twang", "Clean").string (S2, CSingle, 0.1f).feel (0.55f, 0.85f, 0.3f).amp (ATube, CabCombo, 0.18f, 0.3f, 0.0f, 0.0f, 0.3f).fx (FxTremolo, 0.6f, 0.45f),
        // Crunch
        B ("British Crunch", "Crunch").string (S1, CDouble, 0.2f).feel (0.5f, 0.55f, 0.3f).amp (ATube, CabBritish, 0.45f, 0.2f, 0.1f, 0.2f, 0.1f),
        B ("Blues Breaker", "Crunch").string (S3, CDouble, 0.7f).feel (0.45f, 0.45f, 0.3f).amp (ATube, CabCombo, 0.4f, 0.0f, 0.15f, 0.25f, -0.1f),
        B ("Power Chords", "Crunch").play (MPower, 0.85f).string (S1, CDouble, 0.15f).feel (0.5f, 0.5f, 0.45f, 0.2f, 0.4f).amp (ATube, CabBritish, 0.6f, 0.2f, 0.2f, 0.1f, 0.1f),
        B ("Garage Rock", "Crunch").play (MStrum, 0.75f).string (S1, CSingle, 0.25f).feel (0.55f, 0.6f, 0.4f).amp (ATube, CabCombo, 0.55f, 0.3f, 0.0f, 0.3f, 0.2f),
        B ("Indie Jangle", "Crunch").play (MStrum, 0.55f).string (S2, CSingle, 0.35f).feel (0.55f, 0.75f, 0.3f).amp (ATube, CabCombo, 0.3f, 0.35f).fx (FxChorus, 0.45f, 0.5f),
        // Lead
        B ("Singing Lead", "Lead").play (MSoloDynamic, 0.6f).string (S1, CDouble, 0.15f).feel (0.5f, 0.55f, 0.15f).amp (ATube, CabBritish, 0.72f, 0.2f, 0.0f, 0.3f, 0.05f)
            .set (id::feedback, 0.45f).set (id::master, 0.5f).fx (FxDubDelay, 0.45f, 0.35f),
        B ("Glide Solo", "Lead").play (MSoloFixed, 0.5f).string (S2, CDouble, 0.2f).feel (0.5f, 0.6f, 0.2f).amp (ATube, CabBritish, 0.65f, 0.25f, 0.0f, 0.2f).fx (FxMultitap, 0.55f, 0.3f),
        B ("Feedback Sustain", "Lead").play (MSoloDynamic, 0.65f).string (S1, CDouble, 0.1f).feel (0.5f, 0.5f, 0.1f).amp (ATube, CabStax, 0.85f, 0.3f, 0.1f, 0.2f)
            .set (id::feedback, 0.8f).set (id::hold, 1.0f).set (id::master, 0.45f),
        B ("Wah Lead", "Lead").play (MSoloDynamic, 0.6f).string (S1, CSingle, 0.2f).feel (0.5f, 0.6f, 0.25f).amp (ATube, CabBritish, 0.6f, 0.1f, 0.0f, 0.2f).fx (FxWahLfo, 0.45f, 0.8f),
        B ("Octave Fuzz", "Lead").play (MSoloFixed, 0.55f).string (S1, CDouble, 0.15f).feel (0.5f, 0.5f, 0.2f).amp (ATube, CabCombo, 0.35f).fx (FxHarmonizer, 0.6f, 0.3f),
        // Metal
        B ("Metal Rhythm", "Metal").play (MPower, 0.9f).string (S2, CDouble, 0.0f).feel (0.5f, 0.45f, 0.55f, 0.15f, 0.3f).amp (ATube, CabStax, 0.9f, 0.3f, 0.3f, -0.3f, 0.2f),
        B ("Palm Mute Chug", "Metal").play (MPower, 0.95f).string (S2, CDouble, 0.0f).feel (0.45f, 0.4f, 0.85f, 0.1f, 0.3f).amp (ATube, CabStax, 0.95f, 0.2f, 0.45f, -0.25f, 0.1f),
        B ("Thrash Lead", "Metal").play (MSoloDynamic, 0.75f).string (S2, CDouble, 0.05f).feel (0.5f, 0.5f, 0.2f).amp (ATube, CabStax, 0.95f, 0.35f, 0.1f, 0.0f, 0.2f).fx (FxDubDelay, 0.3f, 0.25f),
        B ("Doom", "Metal").play (MPower, 0.8f).set (id::bend2, -0.17f).set (id::gliss, 1.0f).string (S3, CDouble, 0.3f).feel (0.4f, 0.35f, 0.25f).amp (AEq, CabStax, 0.85f, 0.0f, 0.5f, 0.0f, -0.2f),
        // Bass
        B ("Finger Bass", "Bass").string (SEBass, CSingle, 0.6f).feel (0.45f, 0.4f, 0.35f, 0.15f, 0.7f).amp (ADry, CabDry, 0.45f, 0.0f, 0.3f, 0.0f, -0.2f),
        B ("Slap Bass", "Bass").string (SSlap, CSingle, 0.2f).feel (0.55f, 0.6f, 0.45f, 0.3f, 0.8f).set (id::slap, 0.7f).amp (ATube, CabDry, 0.25f, 0.1f, 0.35f, -0.35f, 0.3f),
        B ("Fretless Bass", "Bass").string (SFretless, CDouble, 0.8f).feel (0.45f, 0.35f, 0.25f, 0.05f, 0.6f).amp (ADry, CabDry, 0.45f, 0.0f, 0.25f, 0.15f, -0.2f).fx (FxChorus, 0.2f, 0.3f),
        B ("Rock Bass", "Bass").string (SEBass, CDouble, 0.3f).feel (0.5f, 0.5f, 0.4f, 0.25f, 0.6f).amp (ATube, CabBritish, 0.35f, 0.1f, 0.4f, 0.1f, 0.0f),
        B ("Fat Bandpass Bass", "Bass").string (SEBass, CDouble, 0.5f).feel (0.5f, 0.4f, 0.4f).amp (ABand, CabCombo, 0.4f, -0.4f, 0.6f, 0.0f, 0.5f),
        // FX
        B ("Talkbox", "FX").play (MSoloDynamic, 0.6f).string (S1, CNone, 0.3f).feel (0.5f, 0.6f, 0.2f).amp (AEq, CabCombo, 0.5f).fx (FxTalkbox, 0.3f, 0.5f),
        B ("Leslie Guitar", "FX").string (S1, CSingle, 0.5f).feel (0.5f, 0.6f, 0.3f).amp (ATube, CabCombo, 0.3f).fx (FxLeslie, 0.8f, 0.7f),
        B ("Ring Bells", "FX").string (S2, CSingle, 0.3f).feel (0.6f, 0.8f, 0.3f, 0.0f).amp (ADry, CabDry, 0.4f).fx (FxRingmod, 0.75f, 0.6f),
        B ("Space Phaser", "FX").play (MStrum, 0.4f).string (S2, CSingle, 0.4f).feel (0.5f, 0.7f, 0.25f).amp (ATube, CabCombo, 0.3f).fx (FxStereoPhaser, 0.25f, 0.6f),
        B ("Jet Flanger", "FX").string (S1, CDouble, 0.2f).feel (0.5f, 0.5f, 0.3f).amp (ATube, CabBritish, 0.55f).fx (FxFlanger, 0.2f, 0.8f),
        B ("Broken Radio", "FX").string (S1, CSingle, 0.2f).feel (0.5f, 0.6f, 0.4f).amp (ATube, CabCombo, 0.3f).fx (FxFuzz, 0.7f, 0.65f),
        B ("Talking Bass", "FX").string (SEBass, CDouble, 0.4f).feel (0.5f, 0.5f, 0.3f).amp (ATube, CabCombo, 0.4f).fx (FxPhaser, 0.4f, 0.7f),
    };
    return presets;
}
} // namespace sunriff
