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
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "HexForge", createLayout())
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

bool HexProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
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
    monoCount = 0;
    sustain = false;
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
            handleMidi ((*it).getMessage());

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

void HexProcessor::handleMidi (const juce::MidiMessage& m) noexcept
{
    if (m.isNoteOn())
        noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())
        noteOff (m.getNoteNumber());
    else if (m.isPitchWheel())
        controls.bend = static_cast<float> (m.getPitchWheelValue() - 8192) / 8192.0f;
    else if (m.isChannelPressure())
        controls.aftertouch = static_cast<float> (m.getChannelPressureValue()) / 127.0f;
    else if (m.isController())
    {
        const int cc = m.getControllerNumber(), value = m.getControllerValue();
        if (cc == 1)
            controls.modWheel = static_cast<float> (value) / 127.0f;
        else if (cc == 64)
        {
            sustain = value >= 64;
            if (! sustain)
                for (auto& v : voices)
                    if (v.isActive() && ! v.keyDown && ! v.isReleasing())
                        v.release();
        }
        else if (cc == 120 || cc == 123)
            allNotesOff (cc == 120);
    }
}

Voice* HexProcessor::findVoiceFor (int note) noexcept
{
    const int polyphony = std::clamp (geti (refs.voices), 1, kMaxVoices);
    Voice* best = nullptr;
    for (int i = 0; i < polyphony; ++i)
        if (auto& v = voices[static_cast<size_t> (i)]; v.isActive() && v.note == note)
            return &v;
    for (int i = 0; i < polyphony; ++i)
        if (! voices[static_cast<size_t> (i)].isActive())
            return &voices[static_cast<size_t> (i)];
    for (int i = 0; i < polyphony; ++i)
        if (auto& v = voices[static_cast<size_t> (i)]; v.isReleasing() && (best == nullptr || v.level() < best->level()))
            best = &v;
    if (best != nullptr)
        return best;
    for (int i = 0; i < polyphony; ++i)
        if (auto& v = voices[static_cast<size_t> (i)]; best == nullptr || v.age < best->age)
            best = &v;
    return best;
}

void HexProcessor::noteOn (int note, float velocity) noexcept
{
    const int mode = geti (refs.mode);
    const std::uint32_t seed = seedCounter++ * 747796405u + static_cast<std::uint32_t> (note);
    if (mode == 0)
    {
        if (auto* v = findVoiceFor (note))
        {
            v->start (note, velocity, lastNote, false, seed, patch);
            v->age = ++noteCounter;
        }
    }
    else
    {
        for (int i = 0; i < monoCount; ++i)
            if (monoStack[static_cast<size_t> (i)] == note)
            {
                for (int j = i; j < monoCount - 1; ++j)
                    monoStack[static_cast<size_t> (j)] = monoStack[static_cast<size_t> (j) + 1];
                --monoCount;
                break;
            }
        if (monoCount < static_cast<int> (monoStack.size()))
            monoStack[static_cast<size_t> (monoCount++)] = note;
        auto& v = voices[0];
        const bool legato = mode == 2 && v.isActive() && monoCount > 1;
        v.start (note, velocity, v.isActive() ? static_cast<double> (v.note >= 0 ? v.note : note) : lastNote, legato, seed, patch);
        v.age = ++noteCounter;
    }
    lastNote = note;
}

void HexProcessor::noteOff (int note) noexcept
{
    const int mode = geti (refs.mode);
    if (mode == 0)
    {
        for (auto& v : voices)
            if (v.isActive() && v.note == note && v.keyDown)
            {
                v.keyDown = false;
                if (! sustain)
                    v.release();
            }
        return;
    }
    for (int i = 0; i < monoCount; ++i)
        if (monoStack[static_cast<size_t> (i)] == note)
        {
            for (int j = i; j < monoCount - 1; ++j)
                monoStack[static_cast<size_t> (j)] = monoStack[static_cast<size_t> (j) + 1];
            --monoCount;
            break;
        }
    auto& v = voices[0];
    if (v.note != note)
        return;
    if (monoCount > 0)
    {
        const int previous = monoStack[static_cast<size_t> (monoCount - 1)];
        v.start (previous, v.velocity, note, mode == 2, seedCounter++, patch);
        lastNote = previous;
    }
    else
    {
        v.keyDown = false;
        if (! sustain)
            v.release();
    }
}

void HexProcessor::allNotesOff (bool hard) noexcept
{
    for (auto& v : voices)
    {
        if (hard)
            v.kill();
        else if (v.isActive())
            v.release();
    }
    monoCount = 0;
}

juce::AudioProcessorEditor* HexProcessor::createEditor()
{
    if (editorFactory())
        return editorFactory() (*this);
    return new juce::GenericAudioProcessorEditor (*this);
}

// --- Spectra, presets and state ----------------------------------------------------------------------------------------------

void HexProcessor::setSpectrum (int slot, const Spectrum& s)
{
    spectral.set (slot, s);
    state.state.setProperty (slot == 0 ? "spectralA" : "spectralB", s.toString(), nullptr);
}

void HexProcessor::restoreSpectra()
{
    for (int slot = 0; slot < 2; ++slot)
    {
        const auto text = state.state.getProperty (slot == 0 ? "spectralA" : "spectralB").toString();
        auto s = Spectrum::fromString (text);
        spectral.set (slot, s ? *s : Spectrum::preset (slot == 0 ? "Bright" : "Metal"));
    }
}

juce::File HexProcessor::userPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("HexForge").getChildFile ("Presets");
}

std::vector<PresetEntry> HexProcessor::presets() const
{
    std::vector<PresetEntry> out;
    const auto& factory = factoryPresets();
    for (size_t i = 0; i < factory.size(); ++i)
        out.push_back ({ factory[i].name, factory[i].category, {}, static_cast<int> (i) });
    auto files = userPresetFolder().findChildFiles (juce::File::findFiles, true, juce::String ("*") + kPresetExtension);
    files.sort();
    for (const auto& f : files)
    {
        juce::String category = "User";
        if (auto xml = juce::parseXML (f))
            category = xml->getStringAttribute ("category", "User");
        out.push_back ({ f.getFileNameWithoutExtension(), category, f, -1 });
    }
    return out;
}

void HexProcessor::loadPreset (const PresetEntry& preset)
{
    if (preset.factoryIndex >= 0)
        loadFactoryPreset (preset.factoryIndex);
    else
        loadPresetFile (preset.file);
}

void HexProcessor::applyValues (const std::vector<std::pair<juce::String, float>>& values)
{
    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());
    for (const auto& [id, value] : values)
    {
        if (id == "spectralA" || id == "spectralB")
        {
            setSpectrum (id == "spectralA" ? 0 : 1, Spectrum::preset (spectralPresetName (static_cast<int> (value))));
            continue;
        }
        if (auto* p = state.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
        else
            jassertfalse;
    }
}

void HexProcessor::loadFactoryPreset (int index)
{
    const auto& factory = factoryPresets();
    if (index < 0 || index >= static_cast<int> (factory.size()))
        return;
    setSpectrum (0, Spectrum::preset ("Bright"));
    setSpectrum (1, Spectrum::preset ("Metal"));
    const auto& p = factory[static_cast<size_t> (index)];
    applyValues (p.values);
    presetName = p.name;
    presetCategory = p.category;
    currentProgram = index;
    ++generation;
}

void HexProcessor::initPatch()
{
    setSpectrum (0, Spectrum::preset ("Bright"));
    setSpectrum (1, Spectrum::preset ("Metal"));
    applyValues ({});
    presetName = "Init";
    presetCategory = "Init";
    ++generation;
}

bool HexProcessor::loadPresetFile (const juce::File& file)
{
    auto xml = juce::parseXML (file);
    if (xml == nullptr || ! xml->hasTagName ("HexForgePreset"))
        return false;
    auto* params = xml->getChildByName (state.state.getType());
    if (params == nullptr)
        return false;
    state.replaceState (juce::ValueTree::fromXml (*params));
    restoreSpectra();
    presetName = file.getFileNameWithoutExtension();
    presetCategory = xml->getStringAttribute ("category", "User");
    ++generation;
    return true;
}

juce::File HexProcessor::savePreset (const juce::String& name, const juce::String& category)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty())
        return {};
    auto folder = userPresetFolder();
    folder.createDirectory();
    auto file = folder.getChildFile (clean + kPresetExtension);
    juce::XmlElement root ("HexForgePreset");
    root.setAttribute ("category", category.isEmpty() ? juce::String ("User") : category);
    if (auto params = state.copyState().createXml())
        root.addChildElement (params.release());
    if (! root.writeTo (file))
        return {};
    presetName = clean;
    presetCategory = category;
    ++generation;
    return file;
}

int HexProcessor::getNumPrograms() { return static_cast<int> (factoryPresets().size()); }
void HexProcessor::setCurrentProgram (int index) { loadFactoryPreset (index); }
const juce::String HexProcessor::getProgramName (int index)
{
    const auto& factory = factoryPresets();
    return index >= 0 && index < static_cast<int> (factory.size()) ? factory[static_cast<size_t> (index)].name : juce::String();
}

void HexProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto copy = state.copyState();
    copy.setProperty ("presetName", presetName, nullptr);
    copy.setProperty ("presetCategory", presetCategory, nullptr);
    if (auto xml = copy.createXml())
        copyXmlToBinary (*xml, destData);
}

void HexProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            presetName = tree.getProperty ("presetName", "Init").toString();
            presetCategory = tree.getProperty ("presetCategory", "Init").toString();
            state.replaceState (tree);
            restoreSpectra();
            ++generation;
        }
}
} // namespace hexforge
