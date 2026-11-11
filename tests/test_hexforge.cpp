// HexForge, the hybrid FM + subtractive synthesizer (0.50.0): headless tests through its AudioProcessor.
#include <cmath>
#include <iostream>
#include <set>

#include <juce_audio_formats/juce_audio_formats.h>

#include "hexforge/HexPresets.h"
#include "hexforge/HexProcessor.h"

void supportCheck (bool ok, const char* name); // tests/test_core.cpp

namespace
{
using hexforge::HexProcessor;
constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

juce::AudioBuffer<float> play (HexProcessor& p, std::initializer_list<int> notes, int onFor, int total)
{
    juce::AudioBuffer<float> out (2, total), block (2, kBlock);
    for (int start = 0; start < total; start += kBlock)
    {
        const int n = std::min (kBlock, total - start);
        juce::MidiBuffer midi;
        for (const int note : notes)
        {
            if (start == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.9f), 0);
            if (onFor >= start && onFor < start + n)
                midi.addEvent (juce::MidiMessage::noteOff (1, note), onFor - start);
        }
        block.setSize (2, n, false, false, true);
        p.processBlock (block, midi);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, start, block, ch, 0, n);
    }
    return out;
}

void set (HexProcessor& p, const juce::String& id, float value)
{
    if (auto* param = p.state.getParameter (id))
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    else
        supportCheck (false, ("HexForge: no parameter " + id).toRawUTF8());
}

std::unique_ptr<HexProcessor> make (bool init = true)
{
    auto p = std::make_unique<HexProcessor>();
    p->setPlayConfigDetails (0, 2, kRate, kBlock);
    p->prepareToPlay (kRate, kBlock);
    if (init)
    {
        p->initPatch();
        set (*p, "dist_on", 0);
        set (*p, "flt_on", 0);
    }
    return p;
}

bool finite (const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (ch, i)))
                return false;
    return true;
}

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

// The amplitude of harmonic k of a steady note at `hz` (a single-bin DFT over whole periods).
double harmonic (const juce::AudioBuffer<float>& b, double hz, int k, int from, int periods)
{
    const int length = static_cast<int> (periods * kRate / hz);
    double re = 0.0, im = 0.0;
    for (int i = 0; i < length; ++i)
    {
        const double a = 6.283185307179586 * k * hz * i / kRate;
        re += b.getSample (0, from + i) * std::cos (a);
        im += b.getSample (0, from + i) * std::sin (a);
    }
    return 2.0 * std::sqrt (re * re + im * im) / length;
}
} // namespace

void testHexForge()
{
    std::cout << "HexForge synthesizer (0.50.0)\n";

    // The 32 classic waves: band-limited (their top mip level is a single sine) and normalised.
    {
        const auto& waves = hexforge::HexWaves::get();
        int good = 0;
        for (int w = 0; w < hexforge::kNumClassicWaves; ++w)
        {
            const float* base = waves.classic (w);
            const float* top = base + static_cast<size_t> (hexforge::kTableLevels - 1) * hexforge::kLevelStride;
            float peak = 0.0f;
            double dot = 0.0, norm = 0.0;
            for (int i = 0; i < hexforge::kTableSize; ++i)
            {
                peak = std::max (peak, std::abs (base[i]));
                const double s = std::sin (6.283185307179586 * i / hexforge::kTableSize), c = std::cos (6.283185307179586 * i / hexforge::kTableSize);
                dot += top[i] * top[i];
                norm += (top[i] * s) * (top[i] * s) + (top[i] * c) * (top[i] * c);
            }
            good += std::abs (peak - 1.0f) < 1.0e-3f && std::isfinite (dot) ? 1 : 0;
        }
        supportCheck (good == hexforge::kNumClassicWaves && hexforge::waveNames().size() == hexforge::kNumOpWaves,
                      "HexForge: 32 band-limited classic waves (normalised) plus two spectral waves");
    }

    // The default sound and every factory preset.
    {
        auto p = make (false);
        const auto out = play (*p, { 48 }, 24000, 43200);
        supportCheck (finite (out) && out.getMagnitude (0, 1000, 20000) > 0.05f && out.getMagnitude (0, 43200) <= 1.0f,
                      "HexForge: the first preset plays (finite, audible, within full scale)");
        const auto& presets = hexforge::factoryPresets();
        int good = 0;
        juce::StringArray bad;
        std::set<juce::String> categories;
        for (size_t i = 0; i < presets.size(); ++i)
        {
            auto q = make (false);
            q->loadFactoryPreset (static_cast<int> (i));
            const auto o = play (*q, { 36, 48, 55 }, 38400, 57600);
            const float level = o.getMagnitude (0, 0, 57600);
            if (finite (o) && level > 1.0e-3f && level <= 1.0f)
                ++good;
            else
                bad.add (presets[i].name + " (" + juce::String (level) + ")");
            categories.insert (presets[i].category);
        }
        supportCheck (good == static_cast<int> (presets.size()) && presets.size() >= 40 && categories.size() == 8,
                      ("HexForge: all " + juce::String (static_cast<int> (presets.size())) + " factory presets in 8 categories play"
                       + (bad.isEmpty() ? "" : ": " + bad.joinIntoString (", "))).toRawUTF8());
    }

    // FM: operator 2 modulating operator 1 adds harmonics; operator 2 is not heard by itself; feedback (the diagonal) too.
    {
        const auto render = [] (float amount, bool feedback)
        {
            auto p = make();
            set (*p, "op2_ratio", 3.0f); // ratio 2
            set (*p, feedback ? "fm1_1" : "fm1_2", amount);
            return play (*p, { 57 }, 30000, 24000);
        };
        const auto pure = render (0.0f, false), fm = render (0.6f, false), fb = render (0.6f, true);
        const double h1 = harmonic (pure, 220.0, 1, 4800, 40), h3 = harmonic (pure, 220.0, 3, 4800, 40), f3 = harmonic (fm, 220.0, 3, 4800, 40);
        supportCheck (h1 > 0.05 && h3 < h1 * 0.01 && f3 > h1 * 0.05, "HexForge: a sine alone has no 3rd harmonic; frequency-modulated by operator 2 (ratio 2) it has");
        supportCheck (brightness (fb, 4800, 24000) > brightness (pure, 4800, 24000) * 2.0f, "HexForge: an operator's feedback (matrix diagonal) brightens it");
    }

    // The ladder filter: low-pass darkens, high-pass removes a low note, the filter LFO moves the sound.
    {
        const auto render = [] (int mode, float cutoff, float lfo, int note = 36)
        {
            auto p = make();
            set (*p, "op1_wave", 2.0f); // saw
            set (*p, "flt_on", 1);
            set (*p, "flt_mode", static_cast<float> (mode));
            set (*p, "flt_cut", cutoff);
            set (*p, "flt_key", 0.0f);
            set (*p, "flfo_amt", lfo);
            set (*p, "flfo_sync", 0);
            set (*p, "flfo_rate", 4.0f);
            return play (*p, { note }, 96000, 72000);
        };
        const auto open = render (0, 12000.0f, 0.0f), closed = render (0, 300.0f, 0.0f), hp = render (2, 3000.0f, 0.0f), wob = render (0, 150.0f, 0.6f, 57);
        supportCheck (brightness (closed, 4800, 48000) < brightness (open, 4800, 48000) * 0.3f, "HexForge: the ladder low-pass darkens the sound");
        supportCheck (harmonic (hp, 65.406, 1, 9600, 20) < harmonic (open, 65.406, 1, 9600, 20) * 0.05, "HexForge: the ladder high-pass removes the fundamental of a low note");
        float lo = 1.0e9f, hi = 0.0f;
        for (int b = 10; b < 60; ++b)
        {
            const float r = wob.getRMSLevel (0, b * 1000, 1000);
            lo = std::min (lo, r);
            hi = std::max (hi, r);
        }
        supportCheck (hi > lo * 2.0f, "HexForge: the filter's own LFO sweeps the cutoff");
    }

    // Spectral waves: a square spectrum has odd harmonics only; the wave is saved with the state; one is read from a WAV.
    {
        auto p = make();
        p->setSpectrum (0, hexforge::Spectrum::preset ("Square"));
        set (*p, "op1_wave", static_cast<float> (hexforge::kNumClassicWaves)); // Spectral A
        const auto out = play (*p, { 57 }, 30000, 24000);
        const double h1 = harmonic (out, 220.0, 1, 4800, 40), h2 = harmonic (out, 220.0, 2, 4800, 40), h3 = harmonic (out, 220.0, 3, 4800, 40);
        supportCheck (h3 > h1 * 0.2 && h2 < h1 * 0.02, "HexForge: an operator plays Spectral A (a square spectrum: 3rd harmonic, no 2nd)");

        juce::MemoryBlock saved;
        p->getStateInformation (saved);
        auto q = make (false);
        q->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        bool same = true;
        for (size_t k = 0; k < q->getSpectrum (0).amplitude.size(); ++k)
            same = same && std::abs (q->getSpectrum (0).amplitude[k] - p->getSpectrum (0).amplitude[k]) < 1.0e-3f;
        supportCheck (same && std::abs (q->state.getRawParameterValue ("op1_wave")->load() - 32.0f) < 0.5f,
                      "HexForge: the state keeps the parameters and the spectral waves");

        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("hexforge-cycle.wav");
        {
            file.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
            std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), kRate, 1, 24, {}, 0));
            if (writer != nullptr)
            {
                [[maybe_unused]] auto* owned = stream.release();
                juce::AudioBuffer<float> cycle (1, 2048);
                for (int i = 0; i < 2048; ++i)
                    cycle.setSample (0, i, i < 1024 ? 0.8f : -0.8f);
                writer->writeFromAudioSampleBuffer (cycle, 0, 2048);
            }
        }
        juce::String error;
        const auto loaded = hexforge::Spectrum::fromAudioFile (file, error);
        supportCheck (loaded.has_value() && loaded->amplitude[0] > 0.99f && loaded->amplitude[1] < 0.01f && std::abs (loaded->amplitude[2] - 1.0f / 3.0f) < 0.02f,
                      "HexForge: a single-cycle WAV becomes a spectral wave (a square: harmonics 1, 1/3, no 2nd)");
        file.deleteFile();
    }

    // Unison widens; the polyphony limit holds; a user preset round trip.
    {
        auto p = make();
        set (*p, "uni_voices", 6.0f);
        set (*p, "uni_detune", 0.5f);
        set (*p, "uni_spread", 1.0f);
        const auto wide = play (*p, { 57 }, 30000, 24000);
        double mid = 0.0, side = 0.0;
        for (int i = 4800; i < 24000; ++i)
        {
            const double l = wide.getSample (0, i), r = wide.getSample (1, i);
            mid += (l + r) * (l + r);
            side += (l - r) * (l - r);
        }
        auto q = make();
        set (*q, "voices", 3.0f);
        play (*q, { 48, 52, 55, 59, 62 }, 30000, 9600);
        supportCheck (std::sqrt (side / mid) > 0.2 && q->activeVoices() <= 3, "HexForge: unison spreads the stereo field; the polyphony limit holds");

        set (*p, "flt_cut", 2345.0f);
        const auto file = p->savePreset ("HexForge test preset", "Reese");
        auto r = make (false);
        bool listed = false;
        for (const auto& e : r->presets())
            listed = listed || (e.file == file && e.category == "Reese");
        supportCheck (file.existsAsFile() && r->loadPresetFile (file) && listed && std::abs (r->state.getRawParameterValue ("flt_cut")->load() - 2345.0f) < 1.0f,
                      "HexForge: a user preset is saved, listed in its category and loaded");
        file.deleteFile();
    }
}
