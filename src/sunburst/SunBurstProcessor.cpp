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
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "SunBurst", createLayout())
{
    refs.attach (state);
    Wavetables::get(); // build the tables now, not on the audio thread
    loadFactoryPreset (0);
}

bool SunBurstProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo();
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
    monoCount = 0;
    sustain = false;
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
        allNotesOff (false);
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
            handleMidi ((*it).getMessage());

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

void SunBurstProcessor::handleMidi (const juce::MidiMessage& m) noexcept
{
    if (m.isNoteOn())
        noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())
        noteOff (m.getNoteNumber());
    else if (m.isPitchWheel())
        controls.bend = static_cast<float> (m.getPitchWheelValue() - 8192) / 8192.0f;
    else if (m.isChannelPressure())
        controls.aftertouch = static_cast<float> (m.getChannelPressureValue()) / 127.0f;
    else if (m.isAftertouch())
        controls.aftertouch = static_cast<float> (m.getAfterTouchValue()) / 127.0f;
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

Voice* SunBurstProcessor::findVoiceFor (int note) noexcept
{
    const int polyphony = std::clamp (geti (refs.voices), 1, kMaxVoices);
    Voice* best = nullptr;
    // The same note again: reuse its voice. Otherwise a silent voice, then the quietest released one, then the oldest.
    for (int i = 0; i < polyphony; ++i)
        if (auto& v = voices[static_cast<size_t> (i)]; v.isActive() && v.note == note)
            return &v;
    for (int i = 0; i < polyphony; ++i)
        if (! voices[static_cast<size_t> (i)].isActive())
            return &voices[static_cast<size_t> (i)];
    for (int i = 0; i < polyphony; ++i)
    {
        auto& v = voices[static_cast<size_t> (i)];
        if (v.isReleasing() && (best == nullptr || v.level() < best->level()))
            best = &v;
    }
    if (best != nullptr)
        return best;
    for (int i = 0; i < polyphony; ++i)
    {
        auto& v = voices[static_cast<size_t> (i)];
        if (best == nullptr || v.age < best->age)
            best = &v;
    }
    return best;
}

void SunBurstProcessor::noteOn (int note, float velocity) noexcept
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
        // Mono and legato: one voice and a stack of held keys (the last one sounds).
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

void SunBurstProcessor::noteOff (int note) noexcept
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
        // Back to the key still held: in legato without a new attack, in mono with one.
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

void SunBurstProcessor::allNotesOff (bool hard) noexcept
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

juce::AudioProcessorEditor* SunBurstProcessor::createEditor()
{
    if (editorFactory())
        return editorFactory() (*this);
    return new juce::GenericAudioProcessorEditor (*this);
}

// --- Presets and state ---------------------------------------------------------------------------------------------------

juce::File SunBurstProcessor::userPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("SunBurst").getChildFile ("Presets");
}

std::vector<PresetEntry> SunBurstProcessor::presets() const
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

void SunBurstProcessor::loadPreset (const PresetEntry& preset)
{
    if (preset.factoryIndex >= 0)
        loadFactoryPreset (preset.factoryIndex);
    else
        loadPresetFile (preset.file);
}

void SunBurstProcessor::applyValues (const std::vector<std::pair<juce::String, float>>& values)
{
    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());
    for (const auto& [id, value] : values)
        if (auto* p = state.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
        else
            jassertfalse; // a preset names a parameter that does not exist
}

void SunBurstProcessor::loadFactoryPreset (int index)
{
    const auto& factory = factoryPresets();
    if (index < 0 || index >= static_cast<int> (factory.size()))
        return;
    const auto& p = factory[static_cast<size_t> (index)];
    applyValues (p.values);
    presetName = p.name;
    presetCategory = p.category;
    currentProgram = index;
    ++generation;
}

void SunBurstProcessor::initPatch()
{
    applyValues ({});
    presetName = "Init";
    presetCategory = "Init";
    ++generation;
}

bool SunBurstProcessor::loadPresetFile (const juce::File& file)
{
    auto xml = juce::parseXML (file);
    if (xml == nullptr || ! xml->hasTagName ("SunBurstPreset"))
        return false;
    auto* params = xml->getChildByName (state.state.getType());
    if (params == nullptr)
        return false;
    state.replaceState (juce::ValueTree::fromXml (*params));
    presetName = file.getFileNameWithoutExtension();
    presetCategory = xml->getStringAttribute ("category", "User");
    ++generation;
    return true;
}

juce::File SunBurstProcessor::savePreset (const juce::String& name, const juce::String& category)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty())
        return {};
    auto folder = userPresetFolder();
    folder.createDirectory();
    auto file = folder.getChildFile (clean + kPresetExtension);
    juce::XmlElement root ("SunBurstPreset");
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

int SunBurstProcessor::getNumPrograms()
{
    return static_cast<int> (factoryPresets().size());
}

void SunBurstProcessor::setCurrentProgram (int index)
{
    loadFactoryPreset (index);
}

const juce::String SunBurstProcessor::getProgramName (int index)
{
    const auto& factory = factoryPresets();
    return index >= 0 && index < static_cast<int> (factory.size()) ? juce::String (factory[static_cast<size_t> (index)].name) : juce::String();
}

void SunBurstProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto copy = state.copyState();
    copy.setProperty ("presetName", presetName, nullptr);
    copy.setProperty ("presetCategory", presetCategory, nullptr);
    if (auto xml = copy.createXml())
        copyXmlToBinary (*xml, destData);
}

void SunBurstProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            presetName = tree.getProperty ("presetName", "Init").toString();
            presetCategory = tree.getProperty ("presetCategory", "Init").toString();
            state.replaceState (tree);
            ++generation;
        }
}
} // namespace sunburst
