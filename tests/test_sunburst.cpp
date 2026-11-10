// SunBurst, the synthesizer plugin (0.48.0): headless tests of its engine through the AudioProcessor itself.
#include <cmath>
#include <iostream>
#include <set>

#include "sunburst/Presets.h"
#include "sunburst/SunBurstProcessor.h"
#include "sunburst/Wavetables.h"

void supportCheck (bool ok, const char* name); // tests/test_core.cpp

namespace
{
using sunburst::SunBurstProcessor;
constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

struct Event
{
    int sample;
    juce::MidiMessage message;
};

juce::AudioBuffer<float> run (SunBurstProcessor& p, const std::vector<Event>& events, int total)
{
    juce::AudioBuffer<float> out (2, total);
    juce::AudioBuffer<float> block (2, kBlock);
    for (int start = 0; start < total; start += kBlock)
    {
        const int n = std::min (kBlock, total - start);
        juce::MidiBuffer midi;
        for (const auto& e : events)
            if (e.sample >= start && e.sample < start + n)
                midi.addEvent (e.message, e.sample - start);
        block.setSize (2, n, false, false, true);
        p.processBlock (block, midi);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, start, block, ch, 0, n);
    }
    return out;
}

void set (SunBurstProcessor& p, const char* id, float value)
{
    auto* param = p.state.getParameter (id);
    if (param == nullptr)
        supportCheck (false, id); // no such parameter
    else
        param->setValueNotifyingHost (param->convertTo0to1 (value));
}

bool finite (const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (ch, i)))
                return false;
    return true;
}

float sideRatio (const juce::AudioBuffer<float>& b, int from, int to)
{
    double mid = 0.0, side = 0.0;
    for (int i = from; i < to; ++i)
    {
        const double l = b.getSample (0, i), r = b.getSample (1, i);
        mid += (l + r) * (l + r);
        side += (l - r) * (l - r);
    }
    return mid > 0.0 ? static_cast<float> (std::sqrt (side / mid)) : 0.0f;
}

// Energy of the first difference (a crude measure of brightness) relative to the signal's own energy.
float brightness (const juce::AudioBuffer<float>& b, int from, int to)
{
    double e = 0.0, d = 0.0;
    for (int i = from + 1; i < to; ++i)
    {
        const double x = b.getSample (0, i), y = b.getSample (0, i - 1);
        e += x * x;
        d += (x - y) * (x - y);
    }
    return e > 0.0 ? static_cast<float> (d / e) : 0.0f;
}

std::unique_ptr<SunBurstProcessor> make()
{
    auto p = std::make_unique<SunBurstProcessor>();
    p->setPlayConfigDetails (0, 2, kRate, kBlock);
    p->prepareToPlay (kRate, kBlock);
    return p;
}

std::vector<Event> chord (std::initializer_list<int> notes, int on, int off)
{
    std::vector<Event> e;
    for (const int n : notes)
    {
        e.push_back ({ on, juce::MidiMessage::noteOn (1, n, 0.8f) });
        e.push_back ({ off, juce::MidiMessage::noteOff (1, n) });
    }
    return e;
}
} // namespace

void testSunBurst()
{
    std::cout << "SunBurst synthesizer (0.48.0)\n";

    // Band-limited tables: the highest mip level of a saw is a single sine; the fullest one peaks at 1.
    {
        const auto& t = sunburst::Wavetables::get();
        const float* top = t.frame (0, 4, sunburst::kTableLevels - 1); // Analog Morph, a saw-ish frame
        double dot = 0.0, norm = 0.0, sineNorm = 0.0;
        float peak = 0.0f;
        for (int i = 0; i < sunburst::kTableSize; ++i)
        {
            const double s = std::sin (6.283185307179586 * i / sunburst::kTableSize);
            dot += top[i] * s;
            norm += top[i] * top[i];
            sineNorm += s * s;
            peak = std::max (peak, std::abs (t.frame (0, 4, 0)[i]));
        }
        supportCheck (std::abs (dot) / std::sqrt (norm * sineNorm) > 0.99 && std::abs (peak - 1.0f) < 1.0e-3f,
                      "SunBurst: wavetables are band-limited (the top mip level is one harmonic) and normalised");
        supportCheck (sunburst::Wavetables::levelFor (440.0 / kRate) < sunburst::Wavetables::levelFor (5000.0 / kRate)
                          && (1024 >> sunburst::Wavetables::levelFor (5000.0 / kRate)) * 5000.0 <= kRate / 2,
                      "SunBurst: higher notes use tables with fewer harmonics, none above Nyquist");
    }

    // The default sound plays, stays finite and below full scale.
    {
        auto p = make();
        const auto out = run (*p, chord ({ 60 }, 0, 24000), 48000);
        supportCheck (finite (out) && out.getMagnitude (0, 1000, 20000) > 0.05f && out.getMagnitude (0, 48000) <= 1.0f,
                      "SunBurst: a note plays (finite, audible, below full scale)");
        supportCheck (out.getMagnitude (0, 46000, 2000) < out.getMagnitude (0, 1000, 20000) * 0.5f, "SunBurst: and fades after the key is released");
    }

    // Every factory preset makes sound with a chord, finite and within full scale.
    {
        const auto& presets = sunburst::factoryPresets();
        int good = 0;
        juce::StringArray bad;
        std::set<juce::String> categories;
        for (size_t i = 0; i < presets.size(); ++i)
        {
            auto p = make();
            p->loadFactoryPreset (static_cast<int> (i));
            const auto out = run (*p, chord ({ 48, 60, 64, 67 }, 0, 38400), 57600);
            const float level = out.getMagnitude (0, 0, 57600);
            if (finite (out) && level > 1.0e-3f && level <= 1.0f)
                ++good;
            else
                bad.add (presets[i].name + " (" + juce::String (level) + ")");
            categories.insert (presets[i].category);
        }
        supportCheck (good == static_cast<int> (presets.size()) && presets.size() >= 40,
                      ("SunBurst: all " + juce::String (static_cast<int> (presets.size())) + " factory presets play a chord" + (bad.isEmpty() ? "" : ": " + bad.joinIntoString (", ")))
                          .toRawUTF8());
        supportCheck (categories.size() == 8, "SunBurst: presets in 8 categories (leads, pads, strings, basses, arps, plucks, keys, FX)");
    }

    // Unison: more voices with stereo spread widen the sound; one voice is centred.
    {
        auto p = make();
        p->initPatch();
        set (*p, "flt_on", 0);
        const auto narrow = run (*p, chord ({ 57 }, 0, 40000), 24000);
        set (*p, "o1_voices", 7);
        set (*p, "o1_spread", 1.0f);
        set (*p, "o1_detune", 0.5f);
        auto p2 = make();
        p2->initPatch();
        set (*p2, "flt_on", 0);
        set (*p2, "o1_voices", 7);
        set (*p2, "o1_spread", 1.0f);
        set (*p2, "o1_detune", 0.5f);
        const auto wide = run (*p2, chord ({ 57 }, 0, 40000), 24000);
        supportCheck (sideRatio (narrow, 2000, 24000) < 0.01f && sideRatio (wide, 2000, 24000) > 0.2f, "SunBurst: unison voices spread across the stereo field");
    }

    // The filter: a low cutoff makes the sound darker.
    {
        const auto render = [] (float cutoff)
        {
            auto p = make();
            p->initPatch();
            set (*p, "flt_cut", cutoff);
            set (*p, "flt_key", 0.0f);
            return run (*p, chord ({ 48 }, 0, 40000), 24000);
        };
        const auto bright = render (12000.0f), dark = render (300.0f);
        supportCheck (brightness (dark, 2000, 24000) < brightness (bright, 2000, 24000) * 0.3f, "SunBurst: the low-pass filter darkens the sound");
    }

    // The modulation matrix: LFO 1 on the cutoff makes the level move; without it the level is steady.
    {
        const auto wobble = [] (bool routed)
        {
            auto p = make();
            p->initPatch();
            set (*p, "flt_cut", 600.0f);
            set (*p, "flt_res", 0.3f);
            set (*p, "lfo1_rate", 4.0f);
            if (routed)
            {
                set (*p, "mx1_src", 1.0f); // LFO 1
                set (*p, "mx1_dst", static_cast<float> (sunburst::Dest::Cutoff));
                set (*p, "mx1_amt", 0.8f);
            }
            const auto out = run (*p, chord ({ 48 }, 0, 96000), 96000);
            float lo = 1.0e9f, hi = 0.0f;
            for (int b = 10; b < 90; ++b)
            {
                const float r = out.getRMSLevel (0, b * 1000, 1000);
                lo = std::min (lo, r);
                hi = std::max (hi, r);
            }
            return hi / std::max (lo, 1.0e-6f);
        };
        supportCheck (wobble (true) > 2.0f && wobble (false) < 1.3f, "SunBurst: a matrix slot (LFO 1 -> cutoff) modulates the sound");
    }

    // The arpeggiator: three held keys at 120 BPM, 1/16 notes up, two octaves: 8 notes a second in order.
    {
        sunburst::Arpeggiator arp;
        arp.reset();
        sunburst::Arpeggiator::Settings s;
        s.mode = sunburst::ArpMode::Up;
        s.stepBeats = 0.25;
        s.octaves = 2;
        juce::MidiBuffer out;
        out.ensureSize (8192);
        std::vector<int> played;
        for (int start = 0; start + kBlock <= 48000; start += kBlock) // 93 blocks: just under a second
        {
            juce::MidiBuffer in;
            if (start == 0)
                for (const int n : { 64, 60, 67 })
                    in.addEvent (juce::MidiMessage::noteOn (1, n, 0.8f), 0);
            arp.process (in, out, kBlock, kRate, 120.0, s);
            for (const auto m : out)
                if (m.getMessage().isNoteOn())
                    played.push_back (m.getMessage().getNoteNumber());
        }
        const std::vector<int> expected { 60, 64, 67, 72, 76, 79, 60, 64 };
        supportCheck (played == expected, "SunBurst: the arpeggiator plays the held notes upwards over two octaves, 8 steps a second at 120 BPM");
    }

    // Voice modes and polyphony.
    {
        auto p = make();
        p->initPatch();
        set (*p, "mode", 2.0f); // legato
        auto events = chord ({ 60 }, 0, 20000);
        events.push_back ({ 5000, juce::MidiMessage::noteOn (1, 64, 0.8f) });
        run (*p, events, 10000);
        const int legatoVoices = p->activeVoices();
        auto q = make();
        q->initPatch();
        set (*q, "voices", 4.0f);
        run (*q, chord ({ 48, 52, 55, 59, 62, 65, 69, 72 }, 0, 40000), 10000);
        supportCheck (legatoVoices == 1 && q->activeVoices() <= 4, "SunBurst: legato uses one voice, the polyphony limit holds");
    }

    // Sustain pedal: a released key keeps sounding until the pedal is lifted.
    {
        auto p = make();
        p->initPatch();
        std::vector<Event> e { { 0, juce::MidiMessage::controllerEvent (1, 64, 127) }, { 0, juce::MidiMessage::noteOn (1, 60, 0.8f) },
                               { 2000, juce::MidiMessage::noteOff (1, 60) } };
        const auto held = run (*p, e, 24000);
        const bool sounding = held.getMagnitude (0, 20000, 4000) > 0.05f;
        run (*p, { { 0, juce::MidiMessage::controllerEvent (1, 64, 0) } }, 48000);
        supportCheck (sounding && p->activeVoices() == 0, "SunBurst: the sustain pedal holds notes and releases them when lifted");
    }

    // State and presets: saved and restored; a user preset file round trip.
    {
        auto p = make();
        p->loadFactoryPreset (3);
        set (*p, "flt_cut", 1234.0f);
        juce::MemoryBlock saved;
        p->getStateInformation (saved);
        const auto name = p->getPresetName();
        auto q = make();
        q->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        supportCheck (std::abs (q->state.getRawParameterValue ("flt_cut")->load() - 1234.0f) < 1.0f && q->getPresetName() == name,
                      "SunBurst: the plugin state (parameters and preset name) is saved and restored");

        const auto file = p->savePreset ("SLS test preset", "Bass");
        auto r = make();
        const bool loaded = file.existsAsFile() && r->loadPresetFile (file);
        bool listed = false;
        for (const auto& entry : r->presets())
            listed = listed || (entry.file == file && entry.category == "Bass");
        supportCheck (loaded && std::abs (r->state.getRawParameterValue ("flt_cut")->load() - 1234.0f) < 1.0f && listed
                          && r->getPresetName() == "SLS test preset",
                      "SunBurst: a user preset is saved, listed in its category and loaded");
        file.deleteFile();
    }
}
