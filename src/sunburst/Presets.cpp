#include "sunburst/Presets.h"

#include <algorithm>
#include <map>
#include <cstdint>

#include "sunburst/SunBurstParams.h"

namespace sunburst
{
namespace
{
// Indices of the choice lists (see SunBurstParams.cpp).
enum W : std::uint8_t { Sine = 0, Tri, Saw, Sqr, Noise, Table };
enum T : std::uint8_t { Morph = 0, Pulse, Sweep, Formant, Bells, Organ, Sync, Growl, Brass, FmBell, Wavefold, VowelBass, ChordSaw, Metallic };
enum F : std::uint8_t { LP12 = 0, LP24, HP12, HP24, BP12, BP24, Notch, Peak };
enum L : std::uint8_t { LSine = 0, LTri, LSawUp, LSawDown, LSquare, LHold, LSmooth };
enum Div : std::uint8_t { Bars4 = 0, Bars2, Bar1, Half, HalfT, QuarterD, Quarter, QuarterT, EighthD, Eighth, EighthT, SixteenthD, Sixteenth, SixteenthT, ThirtySecond };
enum S : std::uint8_t { None = 0, Lfo1, Lfo2, Lfo3, Env2, Env3, Vel, Wheel, Touch, Bend, Key, Rnd, Mac1, Mac2, Mac3, Mac4 };
enum D : std::uint8_t
{
    DNone = 0, O1Pitch, O1Pos, O1Width, O1Level, O1Detune, O1Pan, O2Pitch, O2Pos, O2Width, O2Level, O2Detune, O2Pan,
    O3Pitch, O3Pos, O3Width, O3Level, O3Detune, O3Pan, AllPitch, SubLvl, NoiseLvl, Cutoff, Reso, Drive, Amp, Pan,
    L1Rate, L2Rate, L3Rate, FxDrive, FxCut, ChoMix, DlyMix, RevMix
};
enum Dist : std::uint8_t { Soft = 0, Hard, Tube, Fold, Crush };
enum Arp : std::uint8_t { Up = 0, Down, UpDown, Random, Played, Chord };
enum ArpRate : std::uint8_t { A4 = 0, A8, A8T, A16, A16T, A32 };
enum Mode : std::uint8_t { Poly = 0, Mono, Legato };

// The master volume of each factory preset (0.49.0), measured so that a four-note chord has about the same loudness in all
// of them (within -15 .. +3 dB; slow risers and sweeps stay a little quieter than they sound once they open).
float levelFor (const juce::String& name)
{
    static const std::map<juce::String, float> levels {
        { "Acid Bass", -13.7f },
        { "Alarm", -8.2f },
        { "Ambient Wash", 3.0f },
        { "Analog Brass Pad", -8.6f },
        { "Bell Arp", 3.0f },
        { "Bell Pluck", -2.4f },
        { "Brass Lead", -4.3f },
        { "Bubbles", -6.0f },
        { "Cello Section", -8.9f },
        { "Chord Saw Bass", -7.1f },
        { "Chord Stabber", -2.0f },
        { "Church Organ", -11.8f },
        { "Cinematic Strings", -0.9f },
        { "Classic Pluck", -1.5f },
        { "Clav", 1.1f },
        { "Dark Choir", 1.2f },
        { "Deep House Bass", -0.8f },
        { "Digital Keys", 1.9f },
        { "Down Arp", 3.0f },
        { "Dream Pad", 3.0f },
        { "Electric Piano", -4.3f },
        { "Evolving Pad", 3.0f },
        { "FM Arp", 2.9f },
        { "FM Bass", 0.7f },
        { "FM Electric Piano", -2.2f },
        { "FM Glass", -10.5f },
        { "Fat Saw Bass", -10.3f },
        { "Fifth Lead", -3.6f },
        { "Formant Talker", 3.0f },
        { "Funk Slap Bass", 3.0f },
        { "Future Pluck", -0.9f },
        { "Glass Pad", -2.8f },
        { "Glass Pluck", 2.5f },
        { "Glitch Arp", 3.0f },
        { "Growl Bass", -11.7f },
        { "Guitar Pluck", -2.6f },
        { "Hard Sync Bass", -9.5f },
        { "Harp", -0.5f },
        { "Hoover Lead", -1.8f },
        { "House Pluck", -0.7f },
        { "Impact Boom", -12.4f },
        { "Kalimba", -1.6f },
        { "Koto", -0.5f },
        { "Laser Zap", -1.9f },
        { "Lofi Lead", -2.0f },
        { "Marimba", -2.3f },
        { "Mellow Keys", 0.7f },
        { "Metallic Pad", -2.2f },
        { "Moog-ish Bass", -3.5f },
        { "Neuro Bass", -11.4f },
        { "Noise Sweep", 3.0f },
        { "Organ", -9.4f },
        { "Pitch Fall", -4.7f },
        { "Pizzicato", 2.4f },
        { "Played Order Arp", 2.9f },
        { "Pluck Arp", 3.0f },
        { "Pluck Bass", 3.0f },
        { "Poly Keys", -4.7f },
        { "Pulse Lead", -6.3f },
        { "Pulse Pad", -6.7f },
        { "Random Sequence", 0.4f },
        { "Reese Bass", -9.9f },
        { "Rhythmic Gate Pad", -4.9f },
        { "Riser", 3.0f },
        { "Robot Voice", 3.0f },
        { "Rock Organ", -12.6f },
        { "Saw Mono", -9.5f },
        { "Sci-Fi Sweep", -7.8f },
        { "Screech", 3.0f },
        { "Siren", -4.3f },
        { "Solo Violin", -2.9f },
        { "Space Drone", 3.0f },
        { "Square Chip", -2.7f },
        { "Staccato Strings", 2.4f },
        { "Steel Drum", -4.0f },
        { "Strings Ensemble", -5.2f },
        { "Sub Bass", -6.6f },
        { "Sun Lead", -4.1f },
        { "Supersaw Lead", -10.4f },
        { "Sweep Pad", -3.1f },
        { "Sync Screamer", -10.8f },
        { "Synth Strings 80s", -6.2f },
        { "Synthwave Arp", -2.7f },
        { "Synthwave Chords", -6.9f },
        { "Talking Bass", -9.9f },
        { "Toy Piano", -0.7f },
        { "Trance Arp", 3.0f },
        { "Trap 808", -8.1f },
        { "Tremolo Strings", -8.3f },
        { "Triplet Arp", -2.2f },
        { "Vibraphone", -5.3f },
        { "Vowel Bass", -7.8f },
        { "Warm Choir", 3.0f },
        { "Warm Pad", -2.4f },
        { "Wavefold Lead", -2.5f },
        { "Wavefold Pluck", -1.9f },
        { "Whistle", -4.0f },
        { "White Noise Riser", 3.0f },
        { "Wind Pad", 3.0f },
        { "Wobble Bass", -11.6f },
        { "Wurly", -6.2f }
    };
    const auto it = levels.find (name);
    return it != levels.end() ? it->second : -3.0f;
}

class B
{
public:
    B (const char* n, const char* c) : preset { n, c, {} } {}

    B& set (const juce::String& id, float v) { preset.values.push_back ({ id, v }); return *this; }
    // Oscillator `i` (1..3) on, with its wave and level; unison voices, detune and blend.
    B& osc (int i, int wave, float level, int voices = 1, float detune = 0.25f, float blend = 0.5f)
    {
        const int o = i - 1;
        set (oscId (o, "on"), 1).set (oscId (o, "wave"), static_cast<float> (wave)).set (oscId (o, "level"), level);
        set (oscId (o, "voices"), static_cast<float> (voices)).set (oscId (o, "detune"), detune).set (oscId (o, "blend"), blend);
        return *this;
    }
    B& tune (int i, int octave, int semitones = 0, float cents = 0.0f)
    {
        return set (oscId (i - 1, "oct"), static_cast<float> (octave)).set (oscId (i - 1, "semi"), static_cast<float> (semitones)).set (oscId (i - 1, "fine"), cents);
    }
    B& table (int i, int t, float position) { return set (oscId (i - 1, "table"), static_cast<float> (t)).set (oscId (i - 1, "pos"), position); }
    B& osc (int i, const char* name, float v) { return set (oscId (i - 1, name), v); }
    B& filter (int type, float cutoff, float res, float env = 0.0f, float key = 0.5f)
    {
        return set ("flt_type", static_cast<float> (type)).set ("flt_cut", cutoff).set ("flt_res", res).set ("flt_env", env).set ("flt_key", key);
    }
    B& noFilter() { return set ("flt_on", 0); }
    B& env (int e, float a, float d, float s, float r)
    {
        const int i = e - 1;
        return set (envId (i, "a"), a).set (envId (i, "d"), d).set (envId (i, "s"), s).set (envId (i, "r"), r);
    }
    B& lfo (int l, int shape, float rate, bool retrigger = true, float fade = 0.0f, bool unipolar = false)
    {
        const int i = l - 1;
        return set (lfoId (i, "shape"), static_cast<float> (shape)).set (lfoId (i, "rate"), rate).set (lfoId (i, "retrig"), retrigger ? 1.0f : 0.0f)
            .set (lfoId (i, "fade"), fade).set (lfoId (i, "uni"), unipolar ? 1.0f : 0.0f);
    }
    B& lfoSync (int l, int division, int shape, bool retrigger = true, bool unipolar = false)
    {
        const int i = l - 1;
        return set (lfoId (i, "shape"), static_cast<float> (shape)).set (lfoId (i, "sync"), 1).set (lfoId (i, "div"), static_cast<float> (division))
            .set (lfoId (i, "retrig"), retrigger ? 1.0f : 0.0f).set (lfoId (i, "uni"), unipolar ? 1.0f : 0.0f);
    }
    B& mx (int source, int dest, float amount)
    {
        const int slot = next++;
        return set (matrixId (slot, "src"), static_cast<float> (source)).set (matrixId (slot, "dst"), static_cast<float> (dest)).set (matrixId (slot, "amt"), amount);
    }
    B& mode (int m, float glide = 0.0f) { return set ("mode", static_cast<float> (m)).set ("glide", glide); }
    B& dist (int type, float drive, float mix = 1.0f) { return set ("dist_on", 1).set ("dist_type", static_cast<float> (type)).set ("dist_drive", drive).set ("dist_mix", mix); }
    B& eq (float low, float mid, float midFreq, float high) { return set ("eq_on", 1).set ("eq_low", low).set ("eq_mid", mid).set ("eq_midf", midFreq).set ("eq_high", high); }
    B& chorus (float rate, float depth, float mix) { return set ("cho_on", 1).set ("cho_rate", rate).set ("cho_depth", depth).set ("cho_mix", mix); }
    B& delay (int division, float feedback, float mix, bool pingPong = true, float tone = 0.6f)
    {
        return set ("dly_on", 1).set ("dly_sync", 1).set ("dly_div", static_cast<float> (division)).set ("dly_fb", feedback).set ("dly_mix", mix)
            .set ("dly_pp", pingPong ? 1.0f : 0.0f).set ("dly_tone", tone);
    }
    B& reverb (float size, float mix, float damping = 0.4f, float predelay = 10.0f)
    {
        return set ("rev_on", 1).set ("rev_size", size).set ("rev_mix", mix).set ("rev_damp", damping).set ("rev_pre", predelay);
    }
    B& arp (int m, int rate, int octaves, float gate) { return set ("arp_on", 1).set ("arp_mode", static_cast<float> (m)).set ("arp_rate", static_cast<float> (rate)).set ("arp_oct", static_cast<float> (octaves)).set ("arp_gate", gate); }

    // The performance controls every factory preset shares: the mod wheel opens the filter, Macro 1 = brightness,
    // Macro 2 = space (reverb), Macro 3 = width of the unison, Macro 4 = drive.
    FactoryPreset done()
    {
        set ("master", levelFor (preset.name));
        next = kMatrixSlots - 5;
        mx (Wheel, Cutoff, 0.35f).mx (Mac1, Cutoff, 0.6f).mx (Mac2, RevMix, 0.5f).mx (Mac3, O1Detune, 0.6f).mx (Mac4, Drive, 0.8f);
        return preset;
    }

private:
    FactoryPreset preset;
    int next = 0;
};


// 0.49.0: the library grows to about a hundred sounds, using the six new wavetables too.
void addMore (std::vector<FactoryPreset>& p)
{
    // --- Leads ---
    p.push_back (B ("Hoover Lead", "Lead").osc (1, Table, 0.75f, 7, 0.55f, 0.7f).table (1, Pulse, 0.6f).osc (2, Saw, 0.5f, 5, 0.5f).tune (2, -1)
                     .filter (LP24, 4500, 0.2f).lfo (1, LTri, 0.4f, false).mx (Lfo1, O1Pos, 0.3f).mode (Legato, 0.15f).set ("pitch_env", -2.0f)
                     .env (3, 0.0f, 0.25f, 0.0f, 0.2f).chorus (0.4f, 0.7f, 0.4f).reverb (0.5f, 0.2f).done());
    p.push_back (B ("Brass Lead", "Lead").osc (1, Table, 0.8f, 3, 0.15f).table (1, Brass, 0.2f).filter (LP24, 2200, 0.15f, 0.35f).env (2, 0.06f, 0.4f, 0.5f, 0.3f)
                     .env (3, 0.06f, 0.4f, 0.5f, 0.3f).mx (Env3, O1Pos, 0.6f).mx (Vel, Cutoff, 0.25f).mode (Legato, 0.05f)
                     .lfo (1, LSine, 5.0f, true, 0.8f).mx (Lfo1, AllPitch, 0.01f).reverb (0.5f, 0.25f).done());
    p.push_back (B ("Wavefold Lead", "Lead").osc (1, Table, 0.75f).table (1, Wavefold, 0.3f).osc (2, Sine, 0.4f).tune (2, -1).noFilter()
                     .lfo (1, LTri, 0.3f, false).mx (Lfo1, O1Pos, 0.4f).mx (Vel, O1Pos, 0.3f).mode (Legato, 0.04f).delay (EighthD, 0.35f, 0.2f).done());
    p.push_back (B ("Saw Mono", "Lead").osc (1, Saw, 0.8f).osc (2, Saw, 0.7f).tune (2, 0, 0, 7.0f).set ("sub_level", 0.4f).filter (LP24, 1800, 0.35f, 0.3f)
                     .env (2, 0.01f, 0.5f, 0.4f, 0.2f).mode (Legato, 0.07f).dist (Soft, 0.2f).done());
    p.push_back (B ("Pulse Lead", "Lead").osc (1, Sqr, 0.7f, 2, 0.12f).osc (1, "pw", 0.35f).lfo (1, LSine, 0.6f, false).mx (Lfo1, O1Width, 0.3f)
                     .filter (LP24, 3800, 0.25f, 0.2f).mode (Mono, 0.03f).delay (EighthD, 0.3f, 0.18f).reverb (0.45f, 0.18f).done());
    p.push_back (B ("Whistle", "Lead").osc (1, Sine, 0.8f).set ("noise_level", 0.06f).set ("noise_colour", 0.6f).noFilter().env (1, 0.05f, 0.2f, 0.9f, 0.25f)
                     .lfo (1, LSine, 5.5f, true, 0.5f).mx (Lfo1, AllPitch, 0.01f).mode (Legato, 0.08f).reverb (0.6f, 0.3f).done());
    p.push_back (B ("Screech", "Lead").osc (1, Table, 0.8f, 3, 0.2f).table (1, Sync, 0.7f).filter (BP24, 2500, 0.55f).lfoSync (1, Sixteenth, LSquare)
                     .mx (Lfo1, Cutoff, 0.25f).dist (Hard, 0.4f).mode (Mono).delay (Eighth, 0.3f, 0.15f).done());
    p.push_back (B ("Lofi Lead", "Lead").osc (1, Tri, 0.8f).osc (2, Sqr, 0.25f).tune (2, 1).filter (LP12, 2600, 0.1f).dist (Crush, 0.55f, 0.6f)
                     .lfo (1, LSine, 4.0f, true, 0.4f).mx (Lfo1, AllPitch, 0.008f).mode (Legato, 0.05f).delay (Quarter, 0.35f, 0.2f, false, 0.3f).done());

    // --- Pads ---
    p.push_back (B ("Analog Brass Pad", "Pad").osc (1, Table, 0.7f, 6, 0.25f).table (1, Brass, 0.4f).osc (2, Saw, 0.4f, 4, 0.2f).tune (2, -1)
                     .filter (LP24, 1500, 0.1f, 0.25f).env (2, 0.8f, 1.5f, 0.5f, 1.5f).env (1, 0.6f, 1.0f, 0.9f, 1.8f).chorus (0.4f, 0.5f, 0.35f)
                     .reverb (0.75f, 0.4f).done());
    p.push_back (B ("FM Glass", "Pad").osc (1, Table, 0.7f, 3, 0.1f).table (1, FmBell, 0.3f).osc (2, Sine, 0.3f).tune (2, 1).filter (LP12, 9000, 0.1f)
                     .env (1, 0.4f, 1.5f, 0.7f, 3.0f).lfo (1, LTri, 0.12f, false).mx (Lfo1, O1Pos, 0.25f).delay (QuarterD, 0.4f, 0.2f)
                     .reverb (0.85f, 0.5f).done());
    p.push_back (B ("Dream Pad", "Pad").osc (1, Saw, 0.6f, 9, 0.35f, 0.8f).osc (2, Table, 0.4f, 4, 0.2f).table (2, Formant, 0.3f).tune (2, 1)
                     .filter (LP24, 2200, 0.15f).env (1, 1.8f, 1.0f, 0.9f, 4.0f).lfo (1, LSmooth, 0.2f, false).mx (Lfo1, O2Pos, 0.4f)
                     .chorus (0.3f, 0.7f, 0.45f).delay (Half, 0.5f, 0.25f).reverb (0.95f, 0.55f).done());
    p.push_back (B ("Metallic Pad", "Pad").osc (1, Table, 0.6f, 5, 0.2f).table (1, Metallic, 0.4f).osc (2, Saw, 0.3f, 3, 0.2f).tune (2, -1)
                     .filter (LP24, 3000, 0.2f).env (1, 1.0f, 1.0f, 0.85f, 3.0f).lfo (1, LTri, 0.05f, false).mx (Lfo1, O1Pos, 0.5f)
                     .reverb (0.9f, 0.5f).done());
    p.push_back (B ("Pulse Pad", "Pad").osc (1, Sqr, 0.6f, 4, 0.2f).lfo (1, LSine, 0.3f, false).mx (Lfo1, O1Width, 0.35f).osc (2, Saw, 0.35f, 3, 0.2f)
                     .filter (LP24, 1600, 0.15f, 0.2f).env (1, 0.7f, 1.0f, 0.9f, 2.0f).chorus (0.5f, 0.5f, 0.4f).reverb (0.7f, 0.35f).done());
    p.push_back (B ("Rhythmic Gate Pad", "Pad").osc (1, Saw, 0.7f, 7, 0.3f).osc (2, Saw, 0.4f, 5, 0.25f).tune (2, 1).filter (LP24, 3000, 0.2f)
                     .lfoSync (1, Sixteenth, LSquare, false, true).mx (Lfo1, Amp, -0.9f).env (1, 0.2f, 1.0f, 0.9f, 0.8f)
                     .delay (EighthD, 0.4f, 0.2f).reverb (0.6f, 0.3f).done());
    p.push_back (B ("Warm Choir", "Pad").osc (1, Table, 0.7f, 6, 0.18f, 0.7f).table (1, Formant, 0.1f).osc (2, Table, 0.4f, 4, 0.15f).table (2, Formant, 0.9f)
                     .filter (LP12, 3500, 0.1f).env (1, 0.9f, 1.0f, 0.9f, 2.5f).lfo (1, LSine, 4.8f, true, 1.2f).mx (Lfo1, AllPitch, 0.005f)
                     .chorus (0.5f, 0.6f, 0.4f).reverb (0.85f, 0.45f).done());
    p.push_back (B ("Wind Pad", "Pad").set (oscId (0, "on"), 0).set ("noise_level", 0.6f).set ("noise_colour", 0.3f).osc (2, Sine, 0.25f).filter (BP12, 900, 0.65f)
                     .lfo (1, LSmooth, 0.15f, false).mx (Lfo1, Cutoff, 0.5f).env (1, 2.0f, 1.0f, 1.0f, 3.0f).reverb (0.95f, 0.5f).done());

    // --- Strings ---
    p.push_back (B ("Solo Violin", "Strings").osc (1, Saw, 0.8f).osc (2, Saw, 0.3f).tune (2, 0, 0, 6.0f).filter (LP12, 3200, 0.2f, 0.15f, 0.8f)
                     .env (1, 0.12f, 0.5f, 0.9f, 0.35f).lfo (1, LSine, 5.8f, true, 0.6f).mx (Lfo1, AllPitch, 0.012f).mode (Legato, 0.06f)
                     .eq (-4, 3, 2500, 1).reverb (0.6f, 0.3f).done());
    p.push_back (B ("Pizzicato", "Strings").osc (1, Saw, 0.6f, 4, 0.15f).osc (2, Tri, 0.4f).filter (LP24, 2000, 0.15f, 0.4f).env (2, 0.0f, 0.15f, 0.0f, 0.1f)
                     .env (1, 0.0f, 0.3f, 0.0f, 0.25f).reverb (0.6f, 0.35f).done());
    p.push_back (B ("Cello Section", "Strings").osc (1, Saw, 0.7f, 5, 0.2f, 0.7f).tune (1, -1).osc (2, Table, 0.4f, 3, 0.15f).table (2, Brass, 0.2f).tune (2, -1)
                     .filter (LP24, 1800, 0.1f, 0.1f, 0.6f).env (1, 0.25f, 0.5f, 0.9f, 0.8f).lfo (1, LSine, 5.0f, true, 0.8f).mx (Lfo1, AllPitch, 0.006f)
                     .reverb (0.75f, 0.35f).done());
    p.push_back (B ("Synth Strings 80s", "Strings").osc (1, Saw, 0.7f, 4, 0.25f).osc (2, Sqr, 0.4f, 2, 0.2f).osc (2, "pw", 0.3f).filter (LP24, 3000, 0.1f)
                     .env (1, 0.3f, 0.5f, 0.9f, 1.0f).chorus (0.8f, 0.8f, 0.6f).reverb (0.7f, 0.35f).done());
    p.push_back (B ("Tremolo Strings", "Strings").osc (1, Saw, 0.7f, 6, 0.22f, 0.7f).filter (LP12, 3800, 0.1f).env (1, 0.2f, 0.5f, 0.9f, 0.8f)
                     .lfo (1, LSine, 7.0f).mx (Lfo1, Amp, 0.4f).chorus (0.5f, 0.5f, 0.35f).reverb (0.75f, 0.4f).done());

    // --- Bass ---
    p.push_back (B ("Vowel Bass", "Bass").osc (1, Table, 0.8f).table (1, VowelBass, 0.2f).osc (2, Sine, 0.5f).tune (2, -1).noFilter()
                     .env (3, 0.0f, 0.3f, 0.1f, 0.2f).mx (Env3, O1Pos, 0.7f).mode (Mono).dist (Soft, 0.25f).done());
    p.push_back (B ("Talking Bass", "Bass").osc (1, Table, 0.8f).table (1, VowelBass, 0.5f).lfoSync (1, Eighth, LTri).mx (Lfo1, O1Pos, 0.5f)
                     .set ("sub_level", 0.5f).noFilter().mode (Mono).dist (Tube, 0.3f).done());
    p.push_back (B ("FM Bass", "Bass").osc (1, Table, 0.8f).table (1, FmBell, 0.45f).env (3, 0.0f, 0.2f, 0.0f, 0.1f).mx (Env3, O1Pos, 0.5f)
                     .osc (2, Sine, 0.5f).tune (2, -1).noFilter().env (1, 0.001f, 0.4f, 0.6f, 0.1f).mode (Mono).done());
    p.push_back (B ("Moog-ish Bass", "Bass").osc (1, Saw, 0.7f).osc (2, Sqr, 0.6f).tune (2, -1).filter (LP24, 450, 0.4f, 0.4f).set ("flt_drive", 0.3f)
                     .env (2, 0.0f, 0.3f, 0.15f, 0.2f).env (1, 0.002f, 0.4f, 0.8f, 0.1f).mode (Legato, 0.05f).done());
    p.push_back (B ("Hard Sync Bass", "Bass").osc (1, Table, 0.8f).table (1, Sync, 0.2f).env (3, 0.0f, 0.35f, 0.0f, 0.2f).mx (Env3, O1Pos, 0.6f)
                     .set ("sub_level", 0.5f).filter (LP24, 3000, 0.2f).mode (Mono).dist (Soft, 0.3f).done());
    p.push_back (B ("Deep House Bass", "Bass").osc (1, Sine, 0.8f).osc (2, Tri, 0.3f).tune (2, 1).filter (LP12, 900, 0.1f).env (1, 0.003f, 0.35f, 0.4f, 0.12f)
                     .mode (Mono).dist (Tube, 0.2f, 0.6f).done());
    p.push_back (B ("Neuro Bass", "Bass").osc (1, Table, 0.8f, 3, 0.15f).table (1, Wavefold, 0.5f).osc (2, Table, 0.5f).table (2, Growl, 0.6f)
                     .lfoSync (1, Sixteenth, LSmooth).mx (Lfo1, O1Pos, 0.4f).mx (Lfo1, O2Pos, -0.4f).filter (LP24, 4000, 0.3f)
                     .set ("sub_level", 0.5f).mode (Mono).dist (Fold, 0.25f).done());
    p.push_back (B ("Trap 808", "Bass").osc (1, Sine, 0.9f).noFilter().env (1, 0.001f, 2.5f, 0.0f, 0.4f).env (3, 0.0f, 0.06f, 0.0f, 0.05f)
                     .set ("pitch_env", 7.0f).mode (Legato, 0.1f).dist (Soft, 0.35f).done());
    p.push_back (B ("Funk Slap Bass", "Bass").osc (1, Saw, 0.6f).osc (2, Sqr, 0.5f).osc (2, "pw", 0.3f).filter (LP24, 800, 0.3f, 0.45f)
                     .env (2, 0.0f, 0.12f, 0.1f, 0.1f).set ("flt_vel", 0.5f).env (1, 0.001f, 0.35f, 0.3f, 0.1f).mode (Mono).done());
    p.push_back (B ("Chord Saw Bass", "Bass").osc (1, Table, 0.7f, 3, 0.2f).table (1, ChordSaw, 0.5f).osc (2, Sine, 0.5f).tune (2, -1)
                     .filter (LP24, 1200, 0.25f, 0.35f).env (2, 0.0f, 0.3f, 0.2f, 0.2f).mode (Mono).dist (Soft, 0.2f).done());

    // --- Arps ---
    p.push_back (B ("Pluck Arp", "Arp").osc (1, Saw, 0.7f, 3, 0.15f).filter (LP24, 1500, 0.3f, 0.5f).env (2, 0.0f, 0.15f, 0.0f, 0.1f)
                     .env (1, 0.0f, 0.25f, 0.0f, 0.2f).arp (UpDown, A16, 2, 0.5f).delay (EighthD, 0.4f, 0.25f).reverb (0.5f, 0.3f).done());
    p.push_back (B ("FM Arp", "Arp").osc (1, Table, 0.7f).table (1, FmBell, 0.5f).env (3, 0.0f, 0.2f, 0.0f, 0.1f).mx (Env3, O1Pos, 0.4f)
                     .env (1, 0.0f, 0.4f, 0.0f, 0.3f).noFilter().arp (Up, A16, 3, 0.4f).delay (EighthD, 0.45f, 0.3f).reverb (0.55f, 0.3f).done());
    p.push_back (B ("Triplet Arp", "Arp").osc (1, Sqr, 0.6f).osc (2, Saw, 0.4f, 3, 0.2f).filter (LP24, 2200, 0.2f, 0.3f).env (2, 0.0f, 0.2f, 0.1f, 0.1f)
                     .arp (Up, A16T, 2, 0.5f).delay (Quarter, 0.35f, 0.2f).reverb (0.5f, 0.3f).done());
    p.push_back (B ("Down Arp", "Arp").osc (1, Table, 0.7f, 3, 0.2f).table (1, Morph, 0.5f).filter (LP24, 2500, 0.25f, 0.3f).env (2, 0.0f, 0.2f, 0.1f, 0.1f)
                     .arp (Down, A16, 2, 0.6f).delay (EighthD, 0.4f, 0.25f).reverb (0.5f, 0.3f).done());
    p.push_back (B ("Played Order Arp", "Arp").osc (1, Saw, 0.6f, 5, 0.3f).filter (LP24, 2000, 0.3f, 0.35f).env (2, 0.0f, 0.25f, 0.1f, 0.15f)
                     .arp (Played, A8, 1, 0.7f).chorus (0.5f, 0.5f, 0.3f).delay (EighthD, 0.4f, 0.25f).reverb (0.55f, 0.3f).done());
    p.push_back (B ("Glitch Arp", "Arp").osc (1, Table, 0.7f).table (1, Metallic, 0.6f).lfoSync (1, ThirtySecond, LHold).mx (Lfo1, O1Pos, 0.6f)
                     .filter (BP12, 3000, 0.4f).dist (Crush, 0.5f, 0.5f).arp (Random, A32, 2, 0.4f).delay (Sixteenth, 0.4f, 0.25f).done());
    p.push_back (B ("Synthwave Chords", "Arp").osc (1, Saw, 0.6f, 5, 0.25f).osc (2, Sqr, 0.4f).tune (2, -1).filter (LP24, 1800, 0.2f, 0.25f)
                     .env (2, 0.0f, 0.3f, 0.2f, 0.2f).arp (Chord, A8T, 1, 0.5f).chorus (0.5f, 0.6f, 0.4f).reverb (0.6f, 0.35f).done());

    // --- Plucks ---
    p.push_back (B ("Bell Pluck", "Pluck").osc (1, Table, 0.7f).table (1, FmBell, 0.6f).env (3, 0.0f, 0.4f, 0.0f, 0.3f).mx (Env3, O1Pos, 0.4f)
                     .noFilter().env (1, 0.0f, 0.9f, 0.0f, 0.7f).delay (EighthD, 0.35f, 0.2f).reverb (0.6f, 0.35f).done());
    p.push_back (B ("House Pluck", "Pluck").osc (1, Saw, 0.7f, 5, 0.3f).osc (2, Sqr, 0.3f).tune (2, 1).filter (LP24, 1200, 0.25f, 0.6f)
                     .env (2, 0.0f, 0.12f, 0.0f, 0.1f).env (1, 0.0f, 0.2f, 0.0f, 0.15f).reverb (0.5f, 0.25f).delay (Eighth, 0.3f, 0.15f).done());
    p.push_back (B ("Guitar Pluck", "Pluck").osc (1, Saw, 0.6f).osc (2, Sqr, 0.4f).osc (2, "pw", 0.2f).tune (2, 0, 0, 4.0f).filter (LP24, 2800, 0.1f, 0.35f, 0.7f)
                     .env (2, 0.0f, 0.5f, 0.0f, 0.4f).env (1, 0.0f, 1.2f, 0.0f, 0.5f).dist (Tube, 0.15f, 0.5f).reverb (0.5f, 0.25f).done());
    p.push_back (B ("Koto", "Pluck").osc (1, Tri, 0.6f).osc (2, Table, 0.4f).table (2, Metallic, 0.2f).tune (2, 1).filter (LP12, 5000, 0.15f, 0.2f)
                     .env (1, 0.0f, 0.8f, 0.0f, 0.6f).env (3, 0.0f, 0.04f, 0.0f, 0.03f).set ("pitch_env", 0.7f).reverb (0.55f, 0.3f).done());
    p.push_back (B ("Glass Pluck", "Pluck").osc (1, Table, 0.6f, 3, 0.15f).table (1, Bells, 0.7f).osc (2, Sine, 0.4f).tune (2, 1).filter (LP12, 8000, 0.1f)
                     .env (1, 0.0f, 0.6f, 0.0f, 0.8f).delay (QuarterD, 0.45f, 0.25f).reverb (0.75f, 0.4f).done());
    p.push_back (B ("Wavefold Pluck", "Pluck").osc (1, Table, 0.75f).table (1, Wavefold, 0.0f).env (3, 0.0f, 0.25f, 0.0f, 0.2f).mx (Env3, O1Pos, 0.8f)
                     .noFilter().env (1, 0.0f, 0.4f, 0.0f, 0.3f).delay (EighthD, 0.35f, 0.2f).reverb (0.5f, 0.25f).done());
    p.push_back (B ("Steel Drum", "Pluck").osc (1, Sine, 0.7f).osc (2, Sine, 0.35f).tune (2, 1, 0, 30.0f).osc (3, Sine, 0.2f).tune (3, 2, 2)
                     .noFilter().env (1, 0.0f, 0.6f, 0.0f, 0.5f).env (3, 0.0f, 0.05f, 0.0f, 0.05f).set ("pitch_env", 0.5f).reverb (0.5f, 0.3f).done());

    // --- Keys ---
    p.push_back (B ("FM Electric Piano", "Keys").osc (1, Table, 0.7f).table (1, FmBell, 0.2f).osc (2, Sine, 0.4f).noFilter().env (3, 0.0f, 0.8f, 0.0f, 0.4f)
                     .mx (Env3, O1Pos, 0.5f).mx (Vel, O1Pos, 0.3f).env (1, 0.0f, 2.0f, 0.25f, 0.5f).set ("amp_vel", 0.7f)
                     .chorus (0.6f, 0.4f, 0.3f).reverb (0.45f, 0.25f).done());
    p.push_back (B ("Rock Organ", "Keys").osc (1, Table, 0.75f).table (1, Organ, 0.85f).noFilter().env (1, 0.003f, 0.1f, 1.0f, 0.06f)
                     .lfo (1, LSine, 6.8f, false).mx (Lfo1, AllPitch, 0.005f).dist (Tube, 0.4f, 0.8f).chorus (6.5f, 0.35f, 0.4f).reverb (0.4f, 0.2f).done());
    p.push_back (B ("Church Organ", "Keys").osc (1, Table, 0.7f).table (1, Organ, 1.0f).osc (2, Table, 0.4f).table (2, Organ, 0.0f).tune (2, -1)
                     .noFilter().env (1, 0.08f, 0.1f, 1.0f, 1.2f).reverb (1.0f, 0.55f, 0.3f, 30.0f).done());
    p.push_back (B ("Wurly", "Keys").osc (1, Tri, 0.7f).osc (2, Sqr, 0.15f).osc (2, "pw", 0.3f).tune (2, 1).filter (LP12, 3500, 0.15f, 0.2f)
                     .env (1, 0.0f, 1.4f, 0.35f, 0.4f).set ("amp_vel", 0.7f).lfo (1, LSine, 5.5f, false).mx (Lfo1, Amp, 0.15f).dist (Tube, 0.25f, 0.5f)
                     .reverb (0.45f, 0.2f).done());
    p.push_back (B ("Vibraphone", "Keys").osc (1, Sine, 0.75f).osc (2, Sine, 0.2f).tune (2, 2).noFilter().env (1, 0.0f, 2.5f, 0.0f, 1.0f)
                     .lfo (1, LSine, 5.0f, false).mx (Lfo1, Amp, 0.3f).reverb (0.6f, 0.35f).done());
    p.push_back (B ("Toy Piano", "Keys").osc (1, Table, 0.7f).table (1, Metallic, 0.15f).osc (2, Sine, 0.3f).tune (2, 2).noFilter()
                     .env (1, 0.0f, 0.7f, 0.0f, 0.4f).reverb (0.45f, 0.25f).done());
    p.push_back (B ("Mellow Keys", "Keys").osc (1, Saw, 0.5f, 2, 0.08f).osc (2, Sine, 0.5f).filter (LP24, 1200, 0.1f, 0.3f, 0.8f).env (2, 0.0f, 1.0f, 0.2f, 0.5f)
                     .env (1, 0.003f, 1.5f, 0.4f, 0.5f).set ("flt_vel", 0.5f).chorus (0.4f, 0.5f, 0.3f).reverb (0.5f, 0.25f).done());

    // --- FX ---
    p.push_back (B ("White Noise Riser", "FX").osc (1, Noise, 0.7f).filter (HP12, 300, 0.3f).env (3, 8.0f, 1.0f, 1.0f, 1.0f).mx (Env3, Cutoff, 0.8f)
                     .env (1, 4.0f, 1.0f, 1.0f, 2.0f).reverb (0.8f, 0.45f).delay (Eighth, 0.4f, 0.2f).done());
    p.push_back (B ("Impact Boom", "FX").osc (1, Sine, 0.9f).tune (1, -1).osc (3, Noise, 0.4f).filter (LP24, 1500, 0.2f, -0.3f).env (2, 0.0f, 0.4f, 0.0f, 0.4f)
                     .env (3, 0.0f, 0.5f, 0.0f, 0.5f).set ("pitch_env", 12.0f).env (1, 0.0f, 2.5f, 0.0f, 1.5f).dist (Soft, 0.3f).reverb (0.9f, 0.5f).done());
    p.push_back (B ("Sci-Fi Sweep", "FX").osc (1, Table, 0.7f, 4, 0.3f).table (1, Metallic, 0.5f).lfo (1, LSawUp, 0.25f).mx (Lfo1, O1Pos, 0.5f)
                     .mx (Lfo1, AllPitch, 0.5f).filter (LP24, 3000, 0.5f).delay (EighthD, 0.6f, 0.35f).reverb (0.8f, 0.4f).done());
    p.push_back (B ("Robot Voice", "FX").osc (1, Table, 0.8f).table (1, Formant, 0.5f).lfoSync (1, Sixteenth, LHold).mx (Lfo1, O1Pos, 0.5f)
                     .noFilter().dist (Crush, 0.4f, 0.6f).mode (Mono).done());
    p.push_back (B ("Bubbles", "FX").osc (1, Sine, 0.7f).lfo (1, LHold, 12.0f).mx (Lfo1, AllPitch, 0.4f).env (1, 0.0f, 0.15f, 0.6f, 0.2f)
                     .noFilter().delay (Sixteenth, 0.5f, 0.35f).reverb (0.6f, 0.35f).done());
    p.push_back (B ("Siren", "FX").osc (1, Saw, 0.5f, 2, 0.1f).filter (LP12, 4000, 0.1f).lfo (1, LTri, 0.5f).mx (Lfo1, AllPitch, 0.3f)
                     .delay (Eighth, 0.3f, 0.2f).reverb (0.6f, 0.3f).done());
}

std::vector<FactoryPreset> build()
{
    std::vector<FactoryPreset> p;
    // --- Leads -------------------------------------------------------------------------------------------------------
    p.push_back (B ("Sun Lead", "Lead").osc (1, Saw, 0.8f, 5, 0.3f).osc (2, Sqr, 0.35f).tune (2, 1).filter (LP24, 3500, 0.3f, 0.25f)
                     .env (2, 0.003f, 0.5f, 0.4f, 0.3f).lfo (1, LSine, 5.5f, true, 0.6f).mx (Lfo1, AllPitch, 0.012f).mode (Legato, 0.06f)
                     .delay (EighthD, 0.35f, 0.2f).reverb (0.5f, 0.2f).done());
    p.push_back (B ("Supersaw Lead", "Lead").osc (1, Saw, 0.85f, 9, 0.45f, 0.6f).osc (2, Saw, 0.35f, 7, 0.4f).tune (2, 1).filter (LP12, 9000, 0.1f)
                     .env (1, 0.003f, 0.4f, 0.9f, 0.35f).delay (EighthD, 0.4f, 0.25f).reverb (0.65f, 0.3f).done());
    p.push_back (B ("Square Chip", "Lead").osc (1, Sqr, 0.7f).osc (1, "pw", 0.25f).noFilter().lfo (1, LTri, 0.4f, false).mx (Lfo1, O1Width, 0.4f)
                     .mode (Mono, 0.03f).env (1, 0.001f, 0.2f, 0.8f, 0.08f).delay (Sixteenth, 0.3f, 0.15f, false).done());
    p.push_back (B ("Sync Screamer", "Lead").osc (1, Table, 0.8f, 3, 0.15f).table (1, Sync, 0.3f).osc (2, Saw, 0.4f, 3, 0.2f).filter (LP24, 6000, 0.2f)
                     .env (3, 0.003f, 0.6f, 0.0f, 0.3f).mx (Env3, O1Pos, 0.6f).dist (Tube, 0.35f).mode (Legato, 0.08f)
                     .delay (Eighth, 0.3f, 0.15f).reverb (0.5f, 0.2f).done());
    p.push_back (B ("Fifth Lead", "Lead").osc (1, Saw, 0.7f, 3, 0.2f).osc (2, Saw, 0.55f, 3, 0.2f).tune (2, 0, 7).filter (LP24, 2500, 0.35f, 0.35f)
                     .env (2, 0.003f, 0.4f, 0.3f, 0.3f).mode (Mono, 0.04f).delay (EighthD, 0.4f, 0.22f).reverb (0.5f, 0.25f).done());
    p.push_back (B ("Formant Talker", "Lead").osc (1, Table, 0.8f, 2, 0.1f).table (1, Formant, 0.5f).osc (2, Saw, 0.3f).filter (LP12, 5000, 0.15f)
                     .lfo (1, LTri, 0.8f).mx (Lfo1, O1Pos, 0.5f).mode (Legato, 0.05f).reverb (0.55f, 0.3f).done());

    // --- Pads ---------------------------------------------------------------------------------------------------------
    p.push_back (B ("Warm Pad", "Pad").osc (1, Saw, 0.7f, 7, 0.3f).osc (2, Table, 0.5f).table (2, Morph, 0.2f).tune (2, -1).filter (LP24, 1800, 0.1f, 0.2f)
                     .env (1, 1.2f, 1.0f, 0.85f, 2.5f).env (2, 1.5f, 2.0f, 0.5f, 2.0f).lfo (1, LSine, 0.2f, false).mx (Lfo1, Cutoff, 0.12f)
                     .chorus (0.5f, 0.6f, 0.4f).reverb (0.8f, 0.4f).done());
    p.push_back (B ("Glass Pad", "Pad").osc (1, Table, 0.7f, 4, 0.15f).table (1, Bells, 0.4f).osc (2, Sine, 0.3f).tune (2, 1).filter (LP12, 7000, 0.1f)
                     .env (1, 0.8f, 1.0f, 0.8f, 3.0f).lfo (1, LTri, 0.15f, false).mx (Lfo1, O1Pos, 0.3f).delay (QuarterD, 0.45f, 0.2f)
                     .reverb (0.8f, 0.5f).done());
    p.push_back (B ("Evolving Pad", "Pad").osc (1, Table, 0.7f, 3, 0.2f).table (1, Sweep, 0.2f).osc (2, Table, 0.5f, 3, 0.2f).table (2, Formant, 0.5f)
                     .filter (LP24, 4000, 0.15f).env (1, 2.0f, 1.0f, 0.9f, 4.0f).lfo (1, LTri, 0.08f, false, 0.0f, true).mx (Lfo1, O1Pos, 0.6f)
                     .lfo (2, LSine, 0.11f, false).mx (Lfo2, O2Pos, 0.4f).chorus (0.3f, 0.5f, 0.3f).delay (Half, 0.4f, 0.2f).reverb (0.9f, 0.5f).done());
    p.push_back (B ("Dark Choir", "Pad").osc (1, Table, 0.7f, 5, 0.15f).table (1, Formant, 0.7f).osc (2, Table, 0.5f).table (2, Formant, 0.2f).tune (2, -1)
                     .filter (LP12, 2500, 0.1f).env (1, 1.0f, 1.0f, 0.9f, 2.5f).lfo (1, LSine, 0.1f, false).mx (Lfo1, O1Pos, 0.25f)
                     .reverb (0.85f, 0.45f).done());
    p.push_back (B ("Sweep Pad", "Pad").osc (1, Saw, 0.7f, 7, 0.3f).osc (2, Saw, 0.4f, 5, 0.25f).tune (2, -1).filter (LP24, 400, 0.45f)
                     .env (1, 0.6f, 1.0f, 0.9f, 2.0f).lfoSync (1, Bars4, LTri, false, true).mx (Lfo1, Cutoff, 0.55f)
                     .chorus (0.4f, 0.5f, 0.35f).reverb (0.6f, 0.35f).done());
    p.push_back (B ("Ambient Wash", "Pad").osc (1, Table, 0.6f, 8, 0.35f, 0.8f).table (1, Morph, 0.1f).osc (3, Noise, 0.08f).filter (LP24, 1500, 0.2f)
                     .env (1, 3.0f, 2.0f, 0.9f, 5.0f).lfo (1, LSmooth, 0.15f, false).mx (Lfo1, Cutoff, 0.25f).mx (Lfo1, O1Pos, 0.3f)
                     .delay (Half, 0.55f, 0.3f).reverb (1.0f, 0.6f, 0.3f, 40.0f).done());

    // --- Strings ------------------------------------------------------------------------------------------------------
    p.push_back (B ("Strings Ensemble", "Strings").osc (1, Saw, 0.7f, 6, 0.22f, 0.7f).osc (2, Saw, 0.45f, 4, 0.2f).tune (2, -1).filter (LP12, 4200, 0.05f, 0.0f, 0.6f)
                     .env (1, 0.35f, 0.5f, 0.9f, 0.9f).lfo (1, LSine, 5.0f, true, 1.0f).mx (Lfo1, AllPitch, 0.006f).chorus (0.6f, 0.6f, 0.5f)
                     .eq (-3, 0, 1000, 2).reverb (0.7f, 0.35f).done());
    p.push_back (B ("Cinematic Strings", "Strings").osc (1, Saw, 0.65f, 8, 0.25f, 0.7f).osc (2, Table, 0.5f, 4, 0.2f).table (2, Pulse, 0.3f)
                     .filter (LP24, 3000, 0.1f, 0.1f).env (1, 1.5f, 1.0f, 0.9f, 2.0f).lfo (2, LSmooth, 0.3f, false).mx (Lfo2, O2Pos, 0.3f)
                     .lfo (1, LSine, 4.5f, true, 1.5f).mx (Lfo1, AllPitch, 0.005f).reverb (0.9f, 0.45f, 0.5f, 25.0f).done());
    p.push_back (B ("Staccato Strings", "Strings").osc (1, Saw, 0.7f, 5, 0.2f, 0.7f).osc (2, Saw, 0.4f, 3, 0.2f).tune (2, 1).filter (LP12, 3800, 0.1f, 0.2f)
                     .env (1, 0.01f, 0.25f, 0.0f, 0.25f).env (2, 0.005f, 0.2f, 0.3f, 0.2f).chorus (0.6f, 0.5f, 0.4f).reverb (0.6f, 0.3f).done());

    // --- Bass ---------------------------------------------------------------------------------------------------------
    p.push_back (B ("Fat Saw Bass", "Bass").osc (1, Saw, 0.75f, 7, 0.35f, 0.5f).osc (1, "spread", 0.3f).osc (2, Saw, 0.55f).tune (2, -1).set ("sub_level", 0.6f)
                     .filter (LP24, 900, 0.25f, 0.45f).env (2, 0.0f, 0.35f, 0.1f, 0.2f).env (1, 0.002f, 0.3f, 0.9f, 0.1f).mode (Legato, 0.03f)
                     .dist (Soft, 0.25f).eq (2, 0, 1000, 0).done());
    p.push_back (B ("Reese Bass", "Bass").osc (1, Saw, 0.7f, 2, 0.15f, 1.0f).osc (2, Saw, 0.6f).tune (2, 0, 0, 12.0f).set ("sub_level", 0.4f)
                     .filter (LP24, 600, 0.2f).lfo (1, LSine, 0.25f, false).mx (Lfo1, Cutoff, 0.2f).mode (Mono).dist (Tube, 0.3f).done());
    p.push_back (B ("Sub Bass", "Bass").osc (1, Sine, 0.9f).osc (2, Tri, 0.15f).tune (2, 1).noFilter().env (1, 0.005f, 0.3f, 1.0f, 0.15f)
                     .mode (Mono, 0.02f).dist (Soft, 0.1f, 0.5f).done());
    p.push_back (B ("Acid Bass", "Bass").osc (1, Saw, 0.8f).filter (LP24, 300, 0.78f, 0.55f).set ("flt_drive", 0.4f).set ("flt_vel", 0.4f)
                     .env (2, 0.0f, 0.25f, 0.0f, 0.2f).env (1, 0.002f, 0.3f, 1.0f, 0.08f).mode (Legato, 0.08f).dist (Soft, 0.3f)
                     .delay (EighthD, 0.3f, 0.15f).done());
    p.push_back (B ("Growl Bass", "Bass").osc (1, Table, 0.8f, 2, 0.1f).table (1, Growl, 0.4f).osc (2, Sine, 0.5f).tune (2, -1).filter (LP24, 2500, 0.3f)
                     .lfoSync (1, Eighth, LSine).mx (Lfo1, O1Pos, 0.5f).mx (Lfo1, Cutoff, 0.3f).dist (Fold, 0.2f).mode (Mono).done());
    p.push_back (B ("Pluck Bass", "Bass").osc (1, Sqr, 0.6f).osc (2, Saw, 0.5f).filter (LP24, 500, 0.3f, 0.5f).env (2, 0.0f, 0.2f, 0.0f, 0.15f)
                     .env (1, 0.001f, 0.35f, 0.0f, 0.15f).mode (Mono).done());
    p.push_back (B ("Wobble Bass", "Bass").osc (1, Saw, 0.7f, 3, 0.2f).osc (2, Sqr, 0.5f).tune (2, -1).filter (LP24, 400, 0.5f)
                     .lfoSync (1, EighthT, LSine, true, true).mx (Lfo1, Cutoff, 0.55f).dist (Soft, 0.4f).mode (Mono).done());

    // --- Arps -----------------------------------------------------------------------------------------------------------
    p.push_back (B ("Trance Arp", "Arp").osc (1, Saw, 0.8f, 5, 0.3f).filter (LP24, 1800, 0.3f, 0.4f).env (2, 0.0f, 0.2f, 0.1f, 0.2f)
                     .env (1, 0.002f, 0.25f, 0.3f, 0.2f).arp (Up, A16, 2, 0.5f).delay (EighthD, 0.45f, 0.3f).reverb (0.5f, 0.3f).done());
    p.push_back (B ("Bell Arp", "Arp").osc (1, Table, 0.7f).table (1, Bells, 0.6f).osc (2, Sine, 0.2f).tune (2, 2).env (1, 0.0f, 0.6f, 0.0f, 0.6f)
                     .filter (LP12, 9000, 0.1f).arp (UpDown, A16, 3, 0.4f).delay (EighthD, 0.4f, 0.3f).reverb (0.6f, 0.4f).done());
    p.push_back (B ("Synthwave Arp", "Arp").osc (1, Saw, 0.7f, 3, 0.2f).osc (2, Sqr, 0.5f).tune (2, -1).filter (LP24, 1200, 0.25f, 0.35f)
                     .env (2, 0.0f, 0.3f, 0.2f, 0.2f).arp (Up, A16, 1, 0.6f).chorus (0.5f, 0.5f, 0.4f).delay (Eighth, 0.35f, 0.25f).reverb (0.5f, 0.3f).done());
    p.push_back (B ("Random Sequence", "Arp").osc (1, Table, 0.75f).table (1, Morph, 0.6f).filter (LP24, 2000, 0.5f, 0.2f).lfoSync (1, Sixteenth, LHold)
                     .mx (Lfo1, O1Pos, 0.4f).mx (Lfo1, Cutoff, 0.2f).arp (Random, A16, 2, 0.5f).delay (EighthD, 0.5f, 0.3f).reverb (0.5f, 0.3f).done());
    p.push_back (B ("Chord Stabber", "Arp").osc (1, Saw, 0.7f, 4, 0.2f).osc (2, Saw, 0.4f).tune (2, 1).filter (LP24, 2500, 0.2f, 0.3f)
                     .env (2, 0.0f, 0.15f, 0.0f, 0.1f).env (1, 0.001f, 0.2f, 0.0f, 0.1f).arp (Chord, A8, 1, 0.3f).reverb (0.55f, 0.35f).done());

    // --- Plucks ---------------------------------------------------------------------------------------------------------
    p.push_back (B ("Classic Pluck", "Pluck").osc (1, Saw, 0.7f, 3, 0.15f).osc (2, Sqr, 0.3f).tune (2, 1).filter (LP24, 2200, 0.25f, 0.45f)
                     .env (2, 0.0f, 0.25f, 0.0f, 0.2f).env (1, 0.0f, 0.4f, 0.0f, 0.3f).delay (EighthD, 0.4f, 0.25f).reverb (0.5f, 0.3f).done());
    p.push_back (B ("Future Pluck", "Pluck").osc (1, Saw, 0.8f, 7, 0.4f, 0.7f).filter (LP24, 1500, 0.2f, 0.55f).env (2, 0.0f, 0.18f, 0.0f, 0.2f)
                     .env (1, 0.0f, 0.3f, 0.0f, 0.25f).dist (Soft, 0.15f).delay (Quarter, 0.35f, 0.2f).reverb (0.6f, 0.3f).done());
    p.push_back (B ("Kalimba", "Pluck").osc (1, Sine, 0.8f).osc (2, Sine, 0.15f).tune (2, 2, 7).osc (3, Noise, 0.05f).filter (LP12, 6000, 0.1f)
                     .env (1, 0.0f, 0.7f, 0.0f, 0.6f).reverb (0.5f, 0.35f).done());
    p.push_back (B ("Marimba", "Pluck").osc (1, Sine, 0.8f).osc (2, Tri, 0.25f).tune (2, 2).noFilter().env (1, 0.0f, 0.45f, 0.0f, 0.35f)
                     .env (3, 0.0f, 0.03f, 0.0f, 0.03f).set ("pitch_env", 2.0f).reverb (0.45f, 0.25f).done());
    p.push_back (B ("Harp", "Pluck").osc (1, Tri, 0.8f).osc (2, Saw, 0.2f).tune (2, 1).filter (LP12, 3500, 0.1f, 0.3f).env (2, 0.0f, 0.6f, 0.0f, 0.5f)
                     .env (1, 0.0f, 1.4f, 0.0f, 1.0f).reverb (0.6f, 0.4f).done());

    // --- Keys -----------------------------------------------------------------------------------------------------------
    p.push_back (B ("Electric Piano", "Keys").osc (1, Sine, 0.8f).osc (2, Sine, 0.3f).tune (2, 1).osc (3, Tri, 0.08f).tune (3, 2).noFilter()
                     .env (1, 0.0f, 1.6f, 0.3f, 0.5f).set ("amp_vel", 0.7f).lfo (1, LSine, 4.0f, false).mx (Lfo1, Pan, 0.25f)
                     .eq (0, 0, 1000, 2).reverb (0.45f, 0.25f).done());
    p.push_back (B ("Organ", "Keys").osc (1, Table, 0.75f).table (1, Organ, 0.4f).noFilter().env (1, 0.005f, 0.1f, 1.0f, 0.08f)
                     .lfo (1, LSine, 6.5f, false).mx (Lfo1, AllPitch, 0.004f).chorus (6.0f, 0.3f, 0.3f).dist (Tube, 0.15f, 0.5f).reverb (0.4f, 0.2f).done());
    p.push_back (B ("Poly Keys", "Keys").osc (1, Saw, 0.6f, 2, 0.1f).osc (2, Sqr, 0.45f).osc (2, "pw", 0.4f).filter (LP24, 2500, 0.15f, 0.3f, 0.7f)
                     .env (2, 0.003f, 0.6f, 0.3f, 0.4f).env (1, 0.005f, 0.8f, 0.6f, 0.4f).chorus (0.5f, 0.5f, 0.3f).reverb (0.5f, 0.2f).done());
    p.push_back (B ("Clav", "Keys").osc (1, Sqr, 0.7f).osc (1, "pw", 0.15f).filter (BP12, 1500, 0.4f, 0.4f).env (2, 0.0f, 0.15f, 0.2f, 0.1f)
                     .env (1, 0.001f, 0.4f, 0.2f, 0.1f).dist (Soft, 0.2f).done());
    p.push_back (B ("Digital Keys", "Keys").osc (1, Table, 0.7f).table (1, Bells, 0.25f).osc (2, Sine, 0.3f).tune (2, -1).filter (LP12, 8000, 0.1f)
                     .env (1, 0.001f, 1.0f, 0.2f, 0.5f).delay (EighthD, 0.3f, 0.15f).reverb (0.5f, 0.25f).done());

    // --- FX ---------------------------------------------------------------------------------------------------------------
    p.push_back (B ("Riser", "FX").osc (1, Noise, 0.5f).osc (2, Saw, 0.5f, 7, 0.4f).filter (LP12, 200, 0.4f).env (3, 8.0f, 1.0f, 1.0f, 2.0f)
                     .mx (Env3, Cutoff, 0.7f).set ("pitch_env", 12.0f).env (1, 6.0f, 1.0f, 1.0f, 2.0f).delay (Eighth, 0.4f, 0.2f).reverb (0.7f, 0.4f).done());
    p.push_back (B ("Pitch Fall", "FX").osc (1, Saw, 0.7f, 5, 0.5f).osc (3, Noise, 0.15f).filter (LP24, 5000, 0.2f).env (3, 0.0f, 4.0f, 0.0f, 1.0f)
                     .set ("pitch_env", 24.0f).env (1, 0.01f, 4.0f, 0.0f, 1.0f).reverb (0.8f, 0.45f).done());
    p.push_back (B ("Laser Zap", "FX").osc (1, Sqr, 0.6f).noFilter().env (3, 0.0f, 0.15f, 0.0f, 0.1f).set ("pitch_env", 48.0f)
                     .env (1, 0.0f, 0.3f, 0.0f, 0.1f).delay (Sixteenth, 0.5f, 0.3f).done());
    p.push_back (B ("Noise Sweep", "FX").osc (1, Noise, 0.8f).osc (1, "level", 0.8f).filter (BP24, 1000, 0.6f).lfo (1, LTri, 0.1f, false)
                     .mx (Lfo1, Cutoff, 0.6f).env (1, 0.5f, 1.0f, 1.0f, 1.5f).reverb (0.6f, 0.4f).done());
    p.push_back (B ("Alarm", "FX").osc (1, Sqr, 0.5f).noFilter().lfo (1, LSquare, 4.0f).mx (Lfo1, AllPitch, 0.25f).delay (Sixteenth, 0.3f, 0.2f).done());
    p.push_back (B ("Space Drone", "FX").osc (1, Table, 0.7f, 6, 0.5f).table (1, Sync, 0.5f).tune (1, -1).filter (LP24, 900, 0.4f)
                     .lfo (1, LSmooth, 0.2f, false).mx (Lfo1, O1Pos, 0.5f).lfo (2, LSine, 0.07f, false).mx (Lfo2, Cutoff, 0.3f)
                     .env (1, 2.0f, 1.0f, 1.0f, 4.0f).delay (Half, 0.5f, 0.4f).reverb (0.95f, 0.6f).done());
    addMore (p);
    // The browser lists the presets by category, in the order of presetCategories(); within one, as written here.
    std::stable_sort (p.begin(), p.end(), [] (const FactoryPreset& a, const FactoryPreset& b)
                      { return presetCategories().indexOf (a.category) < presetCategories().indexOf (b.category); });
    return p;
}
} // namespace

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets = build();
    return presets;
}

const juce::StringArray& presetCategories()
{
    static const juce::StringArray categories { "Lead", "Pad", "Strings", "Bass", "Arp", "Pluck", "Keys", "FX" };
    return categories;
}
} // namespace sunburst
