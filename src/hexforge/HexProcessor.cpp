#include "hexforge/HexProcessor.h"

#include "hexforge/HexPresets.h"

namespace hexforge
{
namespace
{
constexpr int kChunk = 16;   // output samples between modulation updates (32 at the voices' doubled rate)
constexpr int kTaps = 31;    // the half-band filter that brings the doubled rate back down
} // namespace

std::function<juce::AudioProcessorEditor* (HexProcessor&)>& HexProcessor::editorFactory()
{
    static std::function<juce::AudioProcessorEditor* (HexProcessor&)> factory;
    return factory;
}

HexProcessor::HexProcessor()
    : synthkit::SynthProcessor ({ kName, "HexForgePreset", ".hfpreset", 3.0 }, createLayout(), factoryPresets())
{
    refs.attach (state);
    HexWaves::get(); // build the tables now, not on the audio thread
    // A windowed-sinc low-pass at 0.23 of the doubled rate: what is above the output's Nyquist frequency is removed before every
    // second sample is dropped.
    halfband.resize (kTaps);
    double sum = 0.0;
    for (int t = 0; t < kTaps; ++t)
    {
        const double x = t - (kTaps - 1) / 2.0;
        const double sinc = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (2.0 * sunburst::kPi * 0.23 * x) / (sunburst::kPi * x) / (2.0 * 0.23);
        const double window = 0.42 - 0.5 * std::cos (2.0 * sunburst::kPi * t / (kTaps - 1)) + 0.08 * std::cos (4.0 * sunburst::kPi * t / (kTaps - 1));
        halfband[static_cast<size_t> (t)] = static_cast<float> (sinc * window);
        sum += sinc * window;
    }
    for (auto& h : halfband)
        h = static_cast<float> (h / sum);
    loadFactoryPreset (0);
}

void HexProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    for (auto& v : voices)
        v.prepare (rate * 2.0);
    fx.prepare (rate);
    oversampled.setSize (2, std::max (1, maximumExpectedSamplesPerBlock) * 2 + 64);
    decimL = {};
    decimR = {};
    filterLfo.reset (0.0f, 11u);
    lfo2.reset (0.0f, 23u);
    router.reset();
    keyboardState.reset();
}

void HexProcessor::readPatch() noexcept
{
    for (size_t o = 0; o < patch.op.size(); ++o)
    {
        const auto& r = refs.op[o];
        auto& p = patch.op[o];
        p.wave = std::clamp (geti (r.wave), 0, kNumOpWaves - 1);
        p.ratio = ratioOf (geti (r.ratio));
        p.fine = get (r.fine);
        p.level = get (r.level);
        p.out = get (r.out);
        p.pan = get (r.pan);
        p.velocity = get (r.velocity);
        p.env = { get (r.env.attack), get (r.env.decay), get (r.env.sustain), get (r.env.release) };
    }
    for (size_t t = 0; t < static_cast<size_t> (kNumOps); ++t)
        for (size_t s = 0; s < static_cast<size_t> (kNumOps); ++s)
            patch.fm[t][s] = get (refs.fm[t][s]);
    patch.fmDepth = get (refs.fmDepth);
    patch.unison = geti (refs.unisonVoices);
    patch.detune = get (refs.unisonDetune);
    patch.spread = get (refs.unisonSpread);
    patch.octave = geti (refs.octave);
    patch.dropAmount = get (refs.dropAmount);
    patch.dropTime = get (refs.dropTime);
    patch.filterOn = getb (refs.filterOn);
    patch.mode = static_cast<LadderMode> (std::clamp (geti (refs.filterMode), 0, 4));
    patch.cutoff = get (refs.cutoff);
    patch.resonance = get (refs.resonance);
    patch.drive = get (refs.drive);
    patch.filterEnvAmount = get (refs.filterEnvAmount);
    patch.filterKey = get (refs.filterKey);
    patch.filterVelocity = get (refs.filterVelocity);
    patch.filterLfoAmount = get (refs.filterLfoAmount);
    patch.filterEnv = { get (refs.filterEnv.attack), get (refs.filterEnv.decay), get (refs.filterEnv.sustain), get (refs.filterEnv.release) };
    patch.ampEnv = { get (refs.ampEnv.attack), get (refs.ampEnv.decay), get (refs.ampEnv.sustain), get (refs.ampEnv.release) };
    patch.ampVelocity = get (refs.ampVelocity);
    patch.glide = get (refs.glide);
    patch.bendRange = get (refs.bendRange);
    patch.matrixCount = 0;
    for (const auto& slot : refs.matrix)
    {
        const int s = geti (slot.source), d = geti (slot.dest);
        const float a = get (slot.amount);
        if (s <= 0 || d <= 0 || s >= kNumSources || d >= kNumDests || std::abs (a) < 1.0e-4f)
            continue;
        patch.matrix[static_cast<size_t> (patch.matrixCount++)] = { static_cast<Source> (s), static_cast<Dest> (d), a };
    }
    patch.spectral = { spectral.table (0), spectral.table (1) };
    for (size_t m = 0; m < controls.macro.size(); ++m)
        controls.macro[m] = get (refs.macro[m]);
}

sunburst::FxSettings HexProcessor::fxSettings (float distDriveMod) noexcept
{
    sunburst::FxSettings s;
    s.distOn = getb (refs.distOn);
    s.distType = static_cast<sunburst::DistType> (std::clamp (geti (refs.distType), 0, 4));
    s.distDrive = std::clamp (get (refs.distDrive) + distDriveMod, 0.0f, 1.0f);
    s.distMix = get (refs.distMix);
    s.eqOn = getb (refs.eqOn);
    s.eqLow = get (refs.eqLow);
    s.eqMid = get (refs.eqMid);
    s.eqMidFreq = get (refs.eqMidFreq);
    s.eqHigh = get (refs.eqHigh);
    s.chorusOn = getb (refs.chorusOn);
    s.chorusRate = get (refs.chorusRate);
    s.chorusDepth = get (refs.chorusDepth);
    s.chorusMix = get (refs.chorusMix);
    s.delayOn = getb (refs.delayOn);
    s.delaySeconds = syncBeats (geti (refs.delayDivision)) * 60.0 / bpm;
    s.delayFeedback = get (refs.delayFeedback);
    s.delayPingPong = getb (refs.delayPingPong);
    s.delayTone = get (refs.delayTone);
    s.delayMix = get (refs.delayMix);
    s.reverbOn = getb (refs.reverbOn);
    s.reverbSize = get (refs.reverbSize);
    s.reverbDamping = get (refs.reverbDamping);
    s.reverbWidth = 1.0f;
    s.reverbPredelayMs = 10.0f;
    s.reverbMix = get (refs.reverbMix);
    s.masterGain = juce::Decibels::decibelsToGain (get (refs.master), -47.9f);
    return s;
}

void HexProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n <= 0 || buffer.getNumChannels() < 2 || n * 2 > oversampled.getNumSamples())
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
    controls.rate = rate * 2.0;

    const int polyphony = geti (refs.mode) == 0 ? std::clamp (geti (refs.voices), 1, kMaxVoices) : 1;
    for (int v = polyphony; v < kMaxVoices; ++v)
        if (voices[static_cast<size_t> (v)].isActive() && ! voices[static_cast<size_t> (v)].isReleasing())
            voices[static_cast<size_t> (v)].release();

    oversampled.clear (0, 2 * n);
    float* ol = oversampled.getWritePointer (0);
    float* orr = oversampled.getWritePointer (1);
    float distMod = 0.0f;
    auto it = midi.begin();
    for (int done = 0; done < n; done += kChunk)
    {
        const int len = std::min (kChunk, n - done);
        for (; it != midi.end() && (*it).samplePosition < done + len; ++it)
            router.handleMidi ((*it).getMessage(), controls, geti (refs.mode), geti (refs.voices), patch);

        // The two LFOs run freely, or lock to the song position when they follow the tempo and the song plays.
        const double seconds = len / rate;
        const auto runLfo = [&] (sunburst::Lfo& lfo, std::atomic<float>* shapeP, std::atomic<float>* rateP, std::atomic<float>* syncP,
                                 std::atomic<float>* divP, float rateMod)
        {
            const auto shape = static_cast<sunburst::LfoShape> (std::clamp (geti (shapeP), 0, 6));
            if (getb (syncP))
            {
                const double beats = syncBeats (geti (divP));
                if (hostPlaying)
                {
                    lfo.setPhase (std::fmod (ppq + done / rate * bpm / 60.0, beats) / beats);
                    return lfo.value (shape);
                }
                return lfo.advance (bpm / 60.0 / beats, seconds, shape);
            }
            return lfo.advance (get (rateP) * std::exp2 (3.0 * rateMod), seconds, shape);
        };
        // LFO 2's rate and the distortion follow the global sources of the matrix (wheel, aftertouch, the macros, the filter LFO).
        float lfo2RateMod = 0.0f;
        distMod = 0.0f;
        for (int s = 0; s < patch.matrixCount; ++s)
        {
            const auto& slot = patch.matrix[static_cast<size_t> (s)];
            if (slot.dest != Dest::Lfo2Rate && slot.dest != Dest::DistDrive)
                continue;
            float v = 0.0f;
            switch (slot.source)
            {
                case Source::FilterLfo: v = controls.filterLfo; break;
                case Source::ModWheel: v = controls.modWheel; break;
                case Source::Aftertouch: v = controls.aftertouch; break;
                case Source::Macro1:
                case Source::Macro2:
                case Source::Macro3:
                case Source::Macro4: v = controls.macro[static_cast<size_t> (static_cast<int> (slot.source) - static_cast<int> (Source::Macro1))]; break;
                case Source::Lfo2: v = controls.lfo2; break;
                case Source::None:
                case Source::FilterEnv:
                case Source::Velocity:
                case Source::KeyTrack:
                case Source::Random:
                case Source::Count: break;
            }
            (slot.dest == Dest::Lfo2Rate ? lfo2RateMod : distMod) += v * slot.amount;
        }
        controls.filterLfo = runLfo (filterLfo, refs.filterLfoShape, refs.filterLfoRate, refs.filterLfoSync, refs.filterLfoDivision, 0.0f);
        controls.lfo2 = runLfo (lfo2, refs.lfo2Shape, refs.lfo2Rate, refs.lfo2Sync, refs.lfo2Division, lfo2RateMod);

        for (auto& v : voices)
            if (v.isActive())
                v.render (patch, controls, ol + static_cast<ptrdiff_t> (2) * done, orr + static_cast<ptrdiff_t> (2) * done, 2 * len);
    }

    // Back to the output rate.
    float* left = buffer.getWritePointer (0);
    float* right = buffer.getWritePointer (1);
    const auto decimate = [this] (Decimator& d, const float* in, float* out, int count) noexcept
    {
        const auto size = static_cast<int> (d.history.size());
        for (int i = 0; i < count; ++i)
        {
            for (int k = 0; k < 2; ++k)
            {
                d.history[static_cast<size_t> (d.pos)] = in[2 * i + k];
                d.pos = (d.pos + 1) % size;
            }
            float y = 0.0f;
            for (int t = 0; t < kTaps; ++t)
                y += halfband[static_cast<size_t> (t)] * d.history[static_cast<size_t> ((d.pos - 1 - t + 2 * size) % size)];
            out[i] = y;
        }
    };
    decimate (decimL, ol, left, n);
    decimate (decimR, orr, right, n);

    int sounding = 0;
    for (const auto& v : voices)
        sounding += v.isActive() ? 1 : 0;
    voicesSounding.store (sounding, std::memory_order_relaxed);

    fx.process (left, right, n, fxSettings (distMod));
    peak.store (std::max (peak.load (std::memory_order_relaxed), buffer.getMagnitude (0, n)), std::memory_order_relaxed);
    ppq += n / rate * bpm / 60.0;
}

juce::AudioProcessorEditor* HexProcessor::createEditor()
{
    if (editorFactory())
        return editorFactory() (*this);
    return new juce::GenericAudioProcessorEditor (*this);
}

// --- Spectra and presets ----------------------------------------------------------------------------------------------

void HexProcessor::setSpectrum (int slot, const Spectrum& s)
{
    spectral.set (slot, s);
    state.state.setProperty (slot == 0 ? "spectralA" : "spectralB", s.toString(), nullptr);
}

void HexProcessor::stateRestored()
{
    for (int slot = 0; slot < 2; ++slot)
    {
        const auto text = state.state.getProperty (slot == 0 ? "spectralA" : "spectralB").toString();
        auto s = Spectrum::fromString (text);
        spectral.set (slot, s ? *s : Spectrum::preset (slot == 0 ? "Bright" : "Metal"));
    }
}

void HexProcessor::resetExtras()
{
    setSpectrum (0, Spectrum::preset ("Bright"));
    setSpectrum (1, Spectrum::preset ("Metal"));
}

bool HexProcessor::applyExtraValue (const juce::String& id, float value)
{
    if (id != "spectralA" && id != "spectralB")
        return false;
    setSpectrum (id == "spectralA" ? 0 : 1, Spectrum::preset (spectralPresetName (static_cast<int> (value))));
    return true;
}
} // namespace hexforge
