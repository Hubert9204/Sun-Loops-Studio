#include "sunburst/SunBurstProcessor.h"

#include "sunburst/Presets.h"

namespace sunburst
{
namespace
{
constexpr int kChunk = 32;
inline size_t idx (Dest d) noexcept { return static_cast<size_t> (d); }
} // namespace

std::function<juce::AudioProcessorEditor* (SunBurstProcessor&)>& SunBurstProcessor::editorFactory()
{
    static std::function<juce::AudioProcessorEditor* (SunBurstProcessor&)> factory;
    return factory;
}

SunBurstProcessor::SunBurstProcessor()
    : synthkit::SynthProcessor ({ kName, "SunBurstPreset", ".sbpreset", 4.0 }, createLayout(), factoryPresets())
{
    refs.attach (state);
    Wavetables::get(); // build the tables now, not on the audio thread
    loadFactoryPreset (0);
}

void SunBurstProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    for (auto& v : voices)
        v.prepare (rate);
    fx.prepare (rate);
    arp.reset();
    arpOut.ensureSize (static_cast<size_t> (std::max (4096, maximumExpectedSamplesPerBlock * 6)));
    for (size_t l = 0; l < globalLfos.size(); ++l)
        globalLfos[l].reset (0.0f, 1234u + static_cast<std::uint32_t> (l));
    router.reset();
    keyboardState.reset();
}

void SunBurstProcessor::readPatch() noexcept
{
    for (size_t o = 0; o < patch.osc.size(); ++o)
    {
        const auto& r = refs.osc[o];
        auto& p = patch.osc[o];
        p.on = getb (r.on);
        p.wave = static_cast<Wave> (std::clamp (geti (r.wave), 0, 5));
        p.table = geti (r.table);
        p.position = get (r.position);
        p.width = get (r.width);
        p.level = get (r.level);
        p.pan = get (r.pan);
        p.voices = geti (r.voices);
        p.detune = get (r.detune);
        p.blend = get (r.blend);
        p.spread = get (r.spread);
        p.tune = static_cast<float> (12 * static_cast<int> (std::lround (get (r.octave))) + static_cast<int> (std::lround (get (r.semi)))) + get (r.fine) / 100.0f;
        p.randomPhase = getb (r.randomPhase);
    }
    patch.subLevel = get (refs.subLevel);
    patch.subSquare = geti (refs.subWave) == 1;
    patch.subOctaves = geti (refs.subOctave) + 1;
    patch.noiseLevel = get (refs.noiseLevel);
    patch.noiseColour = get (refs.noiseColour);
    patch.filterOn = getb (refs.filterOn);
    patch.filterType = static_cast<FilterType> (std::clamp (geti (refs.filterType), 0, 7));
    patch.cutoff = get (refs.cutoff);
    patch.resonance = get (refs.resonance);
    patch.filterDrive = get (refs.filterDrive);
    patch.filterEnv = get (refs.filterEnv);
    patch.filterKey = get (refs.filterKey);
    patch.filterVelocity = get (refs.filterVelocity);
    for (size_t e = 0; e < patch.env.size(); ++e)
        patch.env[e] = { get (refs.env[e].attack), get (refs.env[e].decay), get (refs.env[e].sustain), get (refs.env[e].release) };
    patch.ampVelocity = get (refs.ampVelocity);
    patch.pitchEnv = get (refs.pitchEnv);
    for (size_t l = 0; l < patch.lfo.size(); ++l)
    {
        const auto& r = refs.lfo[l];
        auto& p = patch.lfo[l];
        p.shape = static_cast<LfoShape> (std::clamp (geti (r.shape), 0, 6));
        p.hz = getb (r.sync) ? bpm / 60.0 / syncBeats (geti (r.division)) : static_cast<double> (get (r.rate));
        p.retrigger = getb (r.retrigger);
        p.fade = get (r.fade);
        p.unipolar = getb (r.unipolar);
    }
    patch.matrixCount = 0;
    for (const auto& slot : refs.matrix)
    {
        const int s = geti (slot.source), d = geti (slot.dest);
        const float a = get (slot.amount);
        if (s <= 0 || d <= 0 || s >= kNumSources || d >= kNumDests || std::abs (a) < 1.0e-4f)
            continue;
        patch.matrix[static_cast<size_t> (patch.matrixCount++)] = { static_cast<Source> (s), static_cast<Dest> (d), a };
    }
    patch.glide = get (refs.glide);
    patch.bendRange = get (refs.bendRange);
    for (size_t m = 0; m < controls.macro.size(); ++m)
        controls.macro[m] = get (refs.macro[m]);
}

FxSettings SunBurstProcessor::fxSettings() noexcept
{
    // Effects follow the global sources: the free-running LFOs, the wheel, aftertouch, bend and the macros.
    std::array<float, kNumDests> mod {};
    for (int s = 0; s < patch.matrixCount; ++s)
    {
        const auto& slot = patch.matrix[static_cast<size_t> (s)];
        if (static_cast<int> (slot.dest) < kFirstFxDest)
            continue;
        float v = 0.0f;
        switch (slot.source)
        {
            case Source::Lfo1:
            case Source::Lfo2:
            case Source::Lfo3:
            {
                const auto l = static_cast<size_t> (static_cast<int> (slot.source) - static_cast<int> (Source::Lfo1));
                v = controls.globalLfo[l];
                if (patch.lfo[l].unipolar)
                    v = 0.5f * (v + 1.0f);
                break;
            }
            case Source::ModWheel: v = controls.modWheel; break;
            case Source::Aftertouch: v = controls.aftertouch; break;
            case Source::PitchBend: v = controls.bend; break;
            case Source::Macro1:
            case Source::Macro2:
            case Source::Macro3:
            case Source::Macro4: v = controls.macro[static_cast<size_t> (static_cast<int> (slot.source) - static_cast<int> (Source::Macro1))]; break;
            case Source::None:
            case Source::Env2:
            case Source::Env3:
            case Source::Velocity:
            case Source::KeyTrack:
            case Source::Random:
            case Source::Count: break;
        }
        mod[idx (slot.dest)] += v * slot.amount;
    }

    FxSettings s;
    s.distOn = getb (refs.distOn);
    s.distType = static_cast<DistType> (std::clamp (geti (refs.distType), 0, 4));
    s.distDrive = std::clamp (get (refs.distDrive) + mod[idx (Dest::DistDrive)], 0.0f, 1.0f);
    s.distMix = get (refs.distMix);
    s.eqOn = getb (refs.eqOn);
    s.eqLow = get (refs.eqLow);
    s.eqMid = get (refs.eqMid);
    s.eqMidFreq = get (refs.eqMidFreq);
    s.eqHigh = get (refs.eqHigh);
    s.filterOn = getb (refs.fxFilterOn);
    s.filterType = geti (refs.fxFilterType);
    s.filterCutoff = std::clamp (get (refs.fxCutoff) * std::exp2 (mod[idx (Dest::FxCutoff)] * 6.0f), 20.0f, 20000.0f);
    s.filterResonance = get (refs.fxResonance);
    s.chorusOn = getb (refs.chorusOn);
    s.chorusRate = get (refs.chorusRate);
    s.chorusDepth = get (refs.chorusDepth);
    s.chorusMix = std::clamp (get (refs.chorusMix) + mod[idx (Dest::ChorusMix)], 0.0f, 1.0f);
    s.delayOn = getb (refs.delayOn);
    s.delaySeconds = getb (refs.delaySync) ? syncBeats (geti (refs.delayDivision)) * 60.0 / bpm : get (refs.delayTime) / 1000.0;
    s.delayFeedback = get (refs.delayFeedback);
    s.delayPingPong = getb (refs.delayPingPong);
    s.delayTone = get (refs.delayTone);
    s.delayMix = std::clamp (get (refs.delayMix) + mod[idx (Dest::DelayMix)], 0.0f, 1.0f);
    s.reverbOn = getb (refs.reverbOn);
    s.reverbSize = get (refs.reverbSize);
    s.reverbDamping = get (refs.reverbDamping);
    s.reverbWidth = get (refs.reverbWidth);
    s.reverbPredelayMs = get (refs.reverbPredelay);
    s.reverbMix = std::clamp (get (refs.reverbMix) + mod[idx (Dest::ReverbMix)], 0.0f, 1.0f);
    s.masterGain = juce::Decibels::decibelsToGain (get (refs.master), -47.9f);
    return s;
}

void SunBurstProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n <= 0 || buffer.getNumChannels() < 2)
        return;

    if (auto* head = getPlayHead())
        if (const auto pos = head->getPosition())
        {
            if (const auto b = pos->getBpm())
                bpm = std::clamp (*b, 20.0, 400.0);
            hostPlaying = pos->getIsPlaying();
            if (const auto q = pos->getPpqPosition())
                ppq = *q;
        }

    keyboardState.processNextMidiBuffer (midi, 0, n, true);
    readPatch();
    controls.sampleRate = rate;

    // The arpeggiator turns the held keys into its notes.
    const bool arpOn = getb (refs.arpOn);
    if (arpOn != arpWasOn)
    {
        router.allNotesOff (false);
        arp.reset();
        arpWasOn = arpOn;
    }
    const juce::MidiBuffer* events = &midi;
    if (arpOn)
    {
        Arpeggiator::Settings a;
        a.mode = static_cast<ArpMode> (std::clamp (geti (refs.arpMode), 0, 5));
        a.stepBeats = arpRateBeats (geti (refs.arpRate));
        a.octaves = geti (refs.arpOctaves);
        a.gate = get (refs.arpGate);
        arp.process (midi, arpOut, n, rate, bpm, a);
        events = &arpOut;
    }

    // Too many voices for the polyphony now set: release the extra ones.
    const int polyphony = geti (refs.mode) == 0 ? std::clamp (geti (refs.voices), 1, kMaxVoices) : 1;
    for (int v = polyphony; v < kMaxVoices; ++v)
        if (voices[static_cast<size_t> (v)].isActive() && ! voices[static_cast<size_t> (v)].isReleasing())
            voices[static_cast<size_t> (v)].release();

    float* left = buffer.getWritePointer (0);
    float* right = buffer.getWritePointer (1);

    auto it = events->begin();
    for (int done = 0; done < n; done += kChunk)
    {
        const int len = std::min (kChunk, n - done);
        for (; it != events->end() && (*it).samplePosition < done + len; ++it)
            router.handleMidi ((*it).getMessage(), controls, geti (refs.mode), geti (refs.voices), patch);

        // The free-running LFOs (also what the effects follow); tempo-synced ones lock to the song position when it plays.
        const double seconds = len / rate;
        for (size_t l = 0; l < globalLfos.size(); ++l)
        {
            const auto& lp = patch.lfo[l];
            if (getb (refs.lfo[l].sync) && hostPlaying)
            {
                const double beats = syncBeats (geti (refs.lfo[l].division));
                globalLfos[l].setPhase (std::fmod (ppq + done / rate * bpm / 60.0, beats) / beats);
                controls.globalLfo[l] = globalLfos[l].value (lp.shape);
            }
            else
                controls.globalLfo[l] = globalLfos[l].advance (lp.hz, seconds, lp.shape);
        }

        float* l = left + done;
        float* r = right + done;
        for (auto& v : voices)
            if (v.isActive())
                v.render (patch, controls, l, r, len);
    }
    int sounding = 0;
    for (const auto& v : voices)
        sounding += v.isActive() ? 1 : 0;
    voicesSounding.store (sounding, std::memory_order_relaxed);

    fx.process (left, right, n, fxSettings());
    peak.store (std::max (peak.load (std::memory_order_relaxed), buffer.getMagnitude (0, n)), std::memory_order_relaxed);
    ppq += n / rate * bpm / 60.0;
}

juce::AudioProcessorEditor* SunBurstProcessor::createEditor()
{
    if (editorFactory())
        return editorFactory() (*this);
    return new juce::GenericAudioProcessorEditor (*this);
}

} // namespace sunburst
