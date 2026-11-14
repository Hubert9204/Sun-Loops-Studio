// SunSymphony's engine (0.66.0, Milestone 0): zones, pitch, voices, parts, loops, and no allocation on the audio thread.
#include <cmath>
#include <iostream>
#include <vector>

#include "orchestra/Engine.h"

#pragma GCC diagnostic ignored "-Wfloat-equal" // exact silence is what is tested

void supportCheck (bool ok, const char* name); // tests/test_core.cpp
void supportCountAllocations (bool on);
long supportAllocations();

namespace
{
using namespace orchestra;
constexpr double kRate = 48000.0;

struct Out
{
    std::vector<float> left, right;
    float peak() const
    {
        float p = 0.0f;
        for (size_t i = 0; i < left.size(); ++i)
            p = std::max (p, std::max (std::abs (left[i]), std::abs (right[i])));
        return p;
    }
    bool finite() const
    {
        for (size_t i = 0; i < left.size(); ++i)
            if (! std::isfinite (left[i]) || ! std::isfinite (right[i]))
                return false;
        return true;
    }
};

Out render (Engine& e, int samples, int block = 256)
{
    Out o;
    o.left.assign (static_cast<size_t> (samples), 0.0f);
    o.right.assign (static_cast<size_t> (samples), 0.0f);
    for (int s = 0; s < samples; s += block)
        e.render (o.left.data() + s, o.right.data() + s, std::min (block, samples - s));
    return o;
}

// The frequency of a signal whose only zero crossings are its fundamental's (the test tone's are), from rising crossings.
double frequency (const std::vector<float>& x, int from)
{
    double first = -1.0, last = -1.0;
    int count = 0;
    for (size_t i = static_cast<size_t> (from) + 1; i < x.size(); ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const double t = static_cast<double> (i - 1) + x[i - 1] / static_cast<double> (x[i - 1] - x[i]);
            if (first < 0.0)
                first = t;
            else
                ++count;
            last = t;
        }
    return count > 0 ? kRate * count / (last - first) : 0.0;
}

double cents (double measured, double expected) { return 1200.0 * std::log2 (measured / expected); }

void testPitch()
{
    Engine e;
    e.prepare (kRate);
    e.setInstrument (0, Instrument::makeTestTone (kRate));
    bool ok = true;
    for (const int key : { 33, 57, 60, 66, 69, 81, 100 }) // on a root, between two roots (66: transposed down), high and low
    {
        e.noteOn (0, key, 100);
        const auto o = render (e, static_cast<int> (kRate)); // 1 s
        e.controller (0, 120, 0);
        render (e, 2048);
        const double f = frequency (o.left, 4800);
        const double error = cents (f, midiToHz (key));
        if (std::abs (error) > 0.5)
        {
            std::cout << "  key " << key << ": " << f << " Hz (" << error << " cents)\n";
            ok = false;
        }
    }
    supportCheck (ok, "SunSymphony: the test tone plays every key within half a cent (Hermite, transposed up and down)");

    // A different output rate: the sample keeps its pitch.
    Engine e2;
    e2.prepare (kRate);
    e2.setInstrument (0, Instrument::makeTestTone (44100.0)); // a 44.1 kHz sample played at 48 kHz
    e2.noteOn (0, 69, 100);
    const auto o = render (e2, static_cast<int> (kRate));
    supportCheck (std::abs (cents (frequency (o.left, 4800), 440.0)) < 0.5, "SunSymphony: a 44.1 kHz sample keeps its pitch at 48 kHz");
}

std::shared_ptr<SampleBuffer> sine (double hz, int frames, float amp = 0.5f)
{
    auto b = std::make_shared<SampleBuffer>();
    b->sampleRate = kRate;
    b->left.resize (static_cast<size_t> (frames));
    for (int i = 0; i < frames; ++i)
        b->left[static_cast<size_t> (i)] = amp * static_cast<float> (std::sin (2.0 * 3.14159265358979 * hz * i / kRate));
    return b;
}

void testZonesFromSfz()
{
    // Two velocity layers on C4 (a quiet and a loud sample), a looped zone on A4, a zone whose sample is missing.
    const std::string sfz = "<group> lokey=c4 hikey=b4 pitch_keycenter=c4\n"
                            "<region> sample=soft.wav hivel=63 volume=-6\n"
                            "<region> sample=loud.wav lovel=64 pan=50\n"
                            "<region> sample=loop.wav key=a5 loop_mode=loop_continuous loop_start=0 loop_end=4799\n"
                            "<region> sample=missing.wav key=c2\n";
    std::vector<std::string> asked;
    std::string error;
    auto soft = sine (261.6255653, 48000, 0.2f), loud = sine (261.6255653, 48000, 0.8f), loop = sine (100.0, 4800);
    const auto inst = Instrument::fromSfz (sfz, "test", "Test", [&] (const std::string& path) -> std::shared_ptr<const SampleBuffer>
    {
        asked.push_back (path);
        if (path == "soft.wav") return soft;
        if (path == "loud.wav") return loud;
        if (path == "loop.wav") return loop;
        return nullptr;
    }, error);
    supportCheck (inst != nullptr && inst->regions.size() == 3 && asked.size() == 4 && error.find ("1 sample") != std::string::npos,
                  "SunSymphony: an SFZ instrument keeps the zones whose samples load, and says how many did not");
    if (inst == nullptr)
        return;
    const Region* picked[4];
    const int nSoft = inst->pick (60, 40, picked, 4);
    const bool softOk = nSoft == 1 && picked[0]->sample == soft && std::abs (picked[0]->gainDb + 6.0f) < 1.0e-4f;
    const int nLoud = inst->pick (60, 100, picked, 4);
    const bool loudOk = nLoud == 1 && picked[0]->sample == loud && std::abs (picked[0]->pan - 0.5f) < 1.0e-4f;
    supportCheck (softOk && loudOk && inst->pick (48, 100, picked, 4) == 0,
                  "SunSymphony: velocity picks the layer; a key outside every zone is silent");
    const int nLoop = inst->pick (81, 100, picked, 4);
    supportCheck (nLoop == 1 && picked[0]->loops() && picked[0]->loopStart == 0 && picked[0]->loopEnd == 4800,
                  "SunSymphony: SFZ loop points become a forward loop (loop_end is inclusive)");

    // The loop plays on long after the 0.1 s sample would have ended; the one-shot zone stops at its end.
    Engine e;
    e.prepare (kRate);
    e.setInstrument (0, inst);
    e.noteOn (0, 81, 100);
    auto o = render (e, 48000);
    float tail = 0.0f;
    for (size_t i = 40000; i < 48000; ++i)
        tail = std::max (tail, std::abs (o.left[i]));
    supportCheck (tail > 0.2f && e.activeVoices() == 1, "SunSymphony: a looped zone sounds while the key is held");
    e.noteOff (0, 81);
    e.noteOn (0, 60, 100);
    o = render (e, 72000);
    float after = 0.0f;
    for (size_t i = 60000; i < 72000; ++i)
        after = std::max (after, std::abs (o.left[i]));
    supportCheck (after == 0.0f && e.activeVoices() == 0, "SunSymphony: a one-shot zone stops at the end of its sample; a release fades out");
}

void testVoicesAndParts()
{
    Engine e;
    e.prepare (kRate);
    auto tone = Instrument::makeTestTone (kRate);
    for (int p = 0; p < 8; ++p)
        e.setInstrument (p, tone);
    render (e, 256);
    // 8 parts x 25 keys = 200 notes: only 128 sound, the oldest are stolen, nothing breaks.
    for (int p = 0; p < 8; ++p)
        for (int k = 0; k < 25; ++k)
            e.noteOn (p, 48 + k, 90);
    const auto o = render (e, 4800);
    supportCheck (e.activeVoices() <= kMaxVoices && e.activeVoices() >= kMaxVoices - 1 && e.voicesStolen() >= 200 - kMaxVoices && o.finite(),
                  "SunSymphony: 200 notes at once sound as 128 voices, the oldest stolen");
    e.allNotesOff();
    render (e, 4800);
    supportCheck (e.activeVoices() == 0, "SunSymphony: all notes off silences every voice");

    // A part with no instrument is silent; channel 2 plays part 2 only.
    Engine m;
    m.prepare (kRate);
    m.setInstrument (1, tone);
    m.noteOn (2, 60, 100); // part 3: empty
    auto quiet = render (m, 4800);
    m.noteOn (1, 60, 100);
    auto loud = render (m, 4800);
    supportCheck (quiet.peak() == 0.0f && loud.peak() > 0.1f, "SunSymphony: 16 parts on the 16 MIDI channels; an empty part is silent");

    // Part volume (CC7) and pan (CC10).
    m.controller (1, 10, 0); // hard left
    const auto left = render (m, 4800);
    float r = 0.0f, l = 0.0f;
    for (size_t i = 2400; i < 4800; ++i)
    {
        l = std::max (l, std::abs (left.left[i]));
        r = std::max (r, std::abs (left.right[i]));
    }
    m.controller (1, 7, 0);
    const auto muted = render (m, 4800);
    float rest = 0.0f;
    for (size_t i = 2400; i < 4800; ++i)
        rest = std::max (rest, std::abs (muted.left[i]));
    supportCheck (l > 0.1f && r < 1.0e-3f && rest == 0.0f, "SunSymphony: CC10 pans a part, CC7 sets its volume");

    // The sustain pedal holds released notes until it is lifted.
    Engine s;
    s.prepare (kRate);
    s.setInstrument (0, tone);
    s.controller (0, 64, 127);
    s.noteOn (0, 60, 100);
    render (s, 2400);
    s.noteOff (0, 60);
    render (s, 48000);
    const int held = s.activeVoices();
    s.controller (0, 64, 0);
    render (s, 48000);
    supportCheck (held == 1 && s.activeVoices() == 0, "SunSymphony: the sustain pedal holds a note, lifting it releases the note");
}

void testRealTime()
{
    Engine e;
    e.prepare (kRate);
    auto tone = Instrument::makeTestTone (kRate);
    e.setInstrument (0, tone);
    std::vector<float> l (512), r (512);
    e.render (l.data(), r.data(), 512);
    // Swapping instruments, playing and stealing on the audio thread: no allocation at all.
    e.setInstrument (0, Instrument::makeTestTone (kRate));
    supportCountAllocations (true);
    for (int block = 0; block < 200; ++block)
    {
        if (block % 2 == 0)
            for (int k = 0; k < 4; ++k)
                e.noteOn (0, 30 + (block * 3 + k * 7) % 70, 100);
        if (block % 3 == 0)
            e.noteOff (0, 30 + block % 70);
        e.controller (0, 7, block % 128);
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        e.render (l.data(), r.data(), 512);
    }
    e.allNotesOff();
    e.render (l.data(), r.data(), 512);
    const long allocations = supportAllocations();
    supportCountAllocations (false);
    supportCheck (allocations == 0, "SunSymphony: the audio thread never allocates (notes, stealing, instrument swaps)");
    if (allocations != 0)
        std::cout << "  " << allocations << " allocations\n";

    // The replaced instrument is freed on the message thread, once its voices are gone.
    tone.reset();
    e.collectRetired(); // (the first test tone, swapped out above)
    auto current = e.instrument (0);
    std::weak_ptr<const Instrument> old = current;
    current.reset();
    e.noteOn (0, 60, 100);
    e.render (l.data(), r.data(), 512);
    e.setInstrument (0, Instrument::makeTestTone (kRate));
    e.render (l.data(), r.data(), 512); // swapped in: the old one is retired
    e.collectRetired();
    const bool keptWhileFading = ! old.expired();
    for (int i = 0; i < 20; ++i)
        e.render (l.data(), r.data(), 512);
    e.collectRetired();
    supportCheck (keptWhileFading && old.expired(), "SunSymphony: a replaced instrument is freed later, off the audio thread");
}
} // namespace

void testOrchestra()
{
    std::cout << "SunSymphony engine\n";
    testPitch();
    testZonesFromSfz();
    testVoicesAndParts();
    testRealTime();
}
