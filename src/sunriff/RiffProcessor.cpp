#include "sunriff/RiffProcessor.h"

#include "sunriff/RiffParams.h"
#include "sunriff/RiffPresets.h"
#include "synthkit/ParamKit.h"

namespace sunriff
{
namespace
{
constexpr int kChunk = 16; // samples between control updates; the amp's feedback into the strings is one chunk late

enum P : int
{
    PMode, PSpeed, PHold, PGliss, PBend2, PString, PCoil, PPickup, PTone, PSlap, PFret, PHarmonic, PVel, PDamping, PDampVel,
    PAmp, PCabinet, PDrive, PPresence, PFeedback, PLow, PMid, PHigh, PFx, PFx1, PFx2, PMaster, PCount
};
const char* const kIds[PCount] { id::mode, id::speed, id::hold, id::gliss, id::bend2, id::string, id::coil, id::pickup, id::tone,
                                 id::slap, id::fret, id::harmonic, id::vel, id::damping, id::dampVel, id::amp, id::cabinet,
                                 id::drive, id::presence, id::feedback, id::low, id::mid, id::high, id::fx, id::fx1, id::fx2,
                                 id::master };
} // namespace

std::function<juce::AudioProcessorEditor* (RiffProcessor&)>& RiffProcessor::editorFactory()
{
    static std::function<juce::AudioProcessorEditor* (RiffProcessor&)> factory;
    return factory;
}

RiffProcessor::RiffProcessor()
    : synthkit::SynthProcessor ({ kName, "SunRiffPreset", ".srpreset", 4.0 }, createLayout(), factoryPresets())
{
    static_assert (PCount == 27, "one pointer per parameter");
    for (int i = 0; i < PCount; ++i)
        refs.p[i] = state.getRawParameterValue (kIds[i]);
    loadFactoryPreset (0);
}

void RiffProcessor::prepareToPlay (double sampleRate, int)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    for (auto& v : voices)
        v.prepare (rate);
    player.prepare (rate);
    amp.prepare (rate);
    fx.prepare (rate);
    outputDc.setup (rate, 10.0);
    outputDc.reset();
    for (auto* b : { &pickupResonance, &formant1, &formant2, &formant3 })
        b->reset();
    lastTone = lastHarmonic = -1.0f;
    lastCoil = -1;
    clock = 0;
    feedbackIn.fill (0.0f);
    keyboardState.reset();
}

StringVoice* RiffProcessor::voiceFor (int note) noexcept
{
    // The same note again: its string. Then a silent one, then the quietest released one, then the oldest.
    for (auto& v : voices)
        if (v.isActive() && v.currentNote() == note)
            return &v;
    for (auto& v : voices)
        if (! v.isActive())
            return &v;
    StringVoice* best = nullptr;
    for (auto& v : voices)
        if (v.isReleased() && (best == nullptr || v.level() < best->level()))
            best = &v;
    if (best != nullptr)
        return best;
    for (auto& v : voices)
        if (best == nullptr || v.age < best->age)
            best = &v;
    return best;
}

void RiffProcessor::apply (const StringEvent& e) noexcept
{
    const auto mode = static_cast<PlayMode> (std::clamp (synthkit::geti (refs.p[PMode]), 0, 5));
    const bool solo = mode == PlayMode::SoloFixed || mode == PlayMode::SoloDynamic;
    switch (e.type)
    {
        case StringEvent::On:
        {
            auto* v = solo ? &voices[0] : voiceFor (e.note);
            if (solo)
                for (size_t i = 1; i < voices.size(); ++i) // a change of mode leaves no other string ringing in a solo
                    if (voices[i].isActive() && ! voices[i].isReleased())
                        voices[i].release();
            seed = seed * 1664525u + 1013904223u;
            v->start (e.note, e.velocity, stringSettings, seed);
            v->age = ++ageCounter;
            fx.noteOn (noteToHz (e.note));
            break;
        }
        case StringEvent::Off:
            for (auto& v : voices)
                if (v.isActive() && v.currentNote() == e.note && v.keyDown)
                {
                    if (pedal)
                        v.keyDown = false; // the pedal holds it: released when the pedal is
                    else
                        v.release();
                }
            break;
        case StringEvent::Glide:
            if (voices[0].isActive())
                voices[0].glideTo (e.note, e.glideSamples);
            else
            {
                voices[0].start (e.note, e.velocity, stringSettings, ++seed);
                voices[0].age = ++ageCounter;
            }
            fx.noteOn (noteToHz (e.note));
            break;
    }
}

int RiffProcessor::soundingNotes (int* out, int max) const noexcept
{
    int n = 0;
    for (const auto& v : voices)
        if (v.isActive() && ! v.isReleased() && n < max)
            out[n++] = v.currentNote();
    return n;
}

void RiffProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    using synthkit::get;
    using synthkit::geti;
    using synthkit::getb;
    const juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n <= 0 || buffer.getNumChannels() < 2)
        return;
    if (auto* head = getPlayHead())
        if (const auto pos = head->getPosition())
            if (const auto b = pos->getBpm())
                bpm = std::clamp (*b, 20.0, 400.0);
    keyboardState.processNextMidiBuffer (midi, 0, n, true);

    // The patch, once per block.
    const bool hold = getb (refs.p[PHold]);
    stringSettings.type = static_cast<StringType> (std::clamp (geti (refs.p[PString]), 0, 6));
    stringSettings.coil = static_cast<Coil> (std::clamp (geti (refs.p[PCoil]), 0, 2));
    stringSettings.pickup = get (refs.p[PPickup]);
    stringSettings.slap = get (refs.p[PSlap]);
    stringSettings.fret = get (refs.p[PFret]);
    stringSettings.velSens = get (refs.p[PVel]);
    stringSettings.damping = get (refs.p[PDamping]);
    stringSettings.dampVel = get (refs.p[PDampVel]);
    stringSettings.hold = hold || pedal;
    for (auto& v : voices)
        v.setHold (hold || pedal);
    player.configure (static_cast<PlayMode> (std::clamp (geti (refs.p[PMode]), 0, 5)), get (refs.p[PSpeed]), bpm);

    AmpSettings a;
    a.type = std::clamp (geti (refs.p[PAmp]), 0, 3);
    a.cabinet = std::clamp (geti (refs.p[PCabinet]), 0, 3);
    a.drive = get (refs.p[PDrive]);
    a.presence = a.type == 0 ? 0.0f : get (refs.p[PPresence]);
    a.low = get (refs.p[PLow]);
    a.mid = get (refs.p[PMid]);
    a.high = get (refs.p[PHigh]);
    amp.set (a);
    // Feedback: the amp's output back into the strings (none with the Dry amp, as on FL Slayer).
    const float fbParam = a.type == 0 ? 0.0f : get (refs.p[PFeedback]);
    const float feedbackGain = fbParam * fbParam * 0.06f;

    FxParams fp;
    fp.type = std::clamp (geti (refs.p[PFx]), 0, FxCount - 1);
    fp.p1 = get (refs.p[PFx1]);
    fp.p2 = get (refs.p[PFx2]);
    fp.bpm = bpm;
    fx.configure (fp);

    // The body: Tone moves the formants; the pickup's resonance (Harmonic: dark .. bright) depends on its coils.
    const float tone = get (refs.p[PTone]), harmonic = get (refs.p[PHarmonic]);
    const int coil = static_cast<int> (stringSettings.coil);
    if (! same (tone, lastTone) || ! same (harmonic, lastHarmonic) || coil != lastCoil)
    {
        lastTone = tone;
        lastHarmonic = harmonic;
        lastCoil = coil;
        const double shift = std::pow (2.0, (tone - 0.5) * 2.0);
        formant1.set (rate, Biquad::Peak, 160.0 * shift, 1.2, 3.5);
        formant2.set (rate, Biquad::Peak, 900.0 * shift, 1.4, 3.0);
        formant3.set (rate, Biquad::Peak, 2600.0 * shift, 1.8, 2.5);
        const double cutoff = 1800.0 * std::pow (5.0, harmonic) * (coil == 2 ? 0.7 : 1.0);
        if (coil == 0)
            pickupResonance.set (rate, Biquad::LowPass, std::min (rate * 0.45, cutoff * 2.0), 0.6);
        else
            pickupResonance.set (rate, Biquad::LowPass, cutoff, coil == 1 ? 1.6 : 1.0);
    }

    // Pitch bend: the wheel and Pitch Bend 2, one coarse (+-12 semitones) and the other fine (+-2), as Glissando says.
    const float bend2 = get (refs.p[PBend2]);
    const bool pb2Coarse = geti (refs.p[PGliss]) == 1;

    const float masterTarget = get (refs.p[PMaster]);
    float* left = buffer.getWritePointer (0);
    float* right = buffer.getWritePointer (1);
    auto it = midi.begin();
    for (int done = 0; done < n; done += kChunk)
    {
        const int len = std::min (kChunk, n - done);
        for (; it != midi.end() && (*it).samplePosition < done + len; ++it)
        {
            const auto m = (*it).getMessage();
            const std::int64_t when = clock + std::max (0, (*it).samplePosition);
            if (m.isNoteOn())
                player.keyOn (m.getNoteNumber(), m.getFloatVelocity(), when);
            else if (m.isNoteOff())
                player.keyOff (m.getNoteNumber(), when);
            else if (m.isPitchWheel())
                wheel = static_cast<float> (m.getPitchWheelValue() - 8192) / 8192.0f;
            else if (m.isController() && m.getControllerNumber() == 64)
            {
                pedal = m.getControllerValue() >= 64;
                if (! pedal && ! hold)
                    for (auto& v : voices)
                        if (v.isActive() && ! v.keyDown && ! v.isReleased())
                        {
                            v.setHold (false);
                            v.release();
                        }
            }
            else if (m.isAllNotesOff() || m.isAllSoundOff())
            {
                player.allOff (when);
                if (m.isAllSoundOff())
                    for (auto& v : voices)
                        v.kill();
            }
        }
        StringEvent e;
        while (player.next (clock + done + len, e))
            apply (e);

        bendTarget = pb2Coarse ? bend2 * 12.0f + wheel * 2.0f : wheel * 12.0f + bend2 * 2.0f;
        bend += (bendTarget - bend) * 0.35f;

        std::fill (chunkOut.begin(), chunkOut.begin() + len, 0.0f);
        for (auto& v : voices)
            if (v.isActive())
                v.render (chunkOut.data(), feedbackGain > 0.0f ? feedbackIn.data() : nullptr, feedbackGain, len, bend);
        for (int i = 0; i < len; ++i)
        {
            float x = pickupResonance.process (chunkOut[static_cast<size_t> (i)]);
            x = formant3.process (formant2.process (formant1.process (x)));
            if (fxIsPre (fp.type))
                x = fx.pre (x);
            x = outputDc.process (amp.process (x));
            feedbackIn[static_cast<size_t> (i)] = std::clamp (x, -1.5f, 1.5f);
            float l = x, r = x;
            if (fxIsPost (fp.type))
                fx.post (x, l, r);
            // A soft ceiling: a hot effect into a loud amp never goes far above full scale.
            const auto limit = [] (float v) noexcept
            {
                const float mag = std::abs (v);
                return mag < 0.8f ? v : std::copysign (0.8f + 0.2f * std::tanh ((mag - 0.8f) / 0.2f), v);
            };
            l = limit (l);
            r = limit (r);
            smoothedMaster += (masterTarget - smoothedMaster) * 0.002f;
            left[done + i] = l * smoothedMaster;
            right[done + i] = r * smoothedMaster;
        }
    }
    clock += n;

    int sounding = 0;
    for (const auto& v : voices)
        sounding += v.isActive() ? 1 : 0;
    voicesSounding.store (sounding, std::memory_order_relaxed);
    peak.store (std::max (peak.load (std::memory_order_relaxed), buffer.getMagnitude (0, n)), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* RiffProcessor::createEditor()
{
    if (auto& make = editorFactory())
        return make (*this);
    return new juce::GenericAudioProcessorEditor (*this);
}
} // namespace sunriff
