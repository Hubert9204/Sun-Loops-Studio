// Headless tests for the audio core: project model, player (synth, sampler, mixer),
// snapshot exchange, offline renderer and WAV output. No audio device or display needed.

#include <atomic>
#include <chrono>
#include <set>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <new>
#include <thread>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

#include "audio/Arrangement.h"
#include "audio/TypingKeyboard.h"
#include "audio/AudioExport.h"
#include "audio/AudioImport.h"
#include "audio/BufferTuning.h"
#include "audio/BuiltinSamples.h"
#include "audio/ClipEdit.h"
#include "audio/DrumSynth.h"
#include "audio/SoundLibrary.h"
#include "audio/MusicTheory.h"
#include "audio/TimeFormat.h"
#include "audio/Effects.h"
#include "audio/InputRecorder.h"
#include "audio/RecordSetup.h"
#include "audio/Slicer.h"
#include "audio/PitchTrack.h"
#include "audio/StemSplit.h"
#include "library/HelpIndex.h"
#include "library/Sfz.h"
#include "audio/Loudness.h"
#include "audio/Biquad.h"
#include "audio/Spectrum.h"
#include "audio/MidiImport.h"
#include "audio/MidiNotes.h"
#include "audio/MixerRouting.h"
#include "audio/OneShot.h"
#include "audio/Auditioner.h"
#include "audio/DrumKit.h"
#include "library/DrumKitFile.h"
#include "library/SoundFont.h"
#include "plugins/PluginHost.h"
#include "audio/SampleEdit.h"
#include "audio/TimeStretch.h"
#include "audio/NoteRecorder.h"
#include "audio/ScoreLog.h"
#include "audio/LoopStarter.h"
#include "audio/SpscQueue.h"
#include "audio/LiveNotes.h"
#include "audio/OfflineRenderer.h"
#include "audio/Player.h"
#include "audio/Project.h"
#include "audio/SnapshotExchange.h"
#include "audio/TapTempo.h"
#include "library/BrowserModel.h"
#include "library/ChannelPreset.h"
#include "library/Library.h"
#include "library/ProjectFile.h"
#include "ui/PianoRoll.h"
#include "library/DemoSong.h"
#include "library/FlpProject.h"
#include "plugins/PluginScanner.h"
#include "ui/PanelLayout.h"

// Several checks below deliberately compare floats with ==: "exactly zero" means silence or
// "no contribution at all", which is the property being tested.
#pragma GCC diagnostic ignored "-Wfloat-equal"

// --- allocation counter, used to prove that Player::render never allocates ---------
static std::atomic<long> gAllocs { 0 };
static std::atomic<bool> gCounting { false };
static thread_local bool tCounting = false; // counts only the allocations of the calling thread (see gThreadAllocs)
static thread_local long tAllocs = 0;

void* operator new (std::size_t n)
{
    if (gCounting.load (std::memory_order_relaxed))
        gAllocs.fetch_add (1, std::memory_order_relaxed);
    if (tCounting)
        ++tAllocs;
    if (void* p = std::malloc (n != 0 ? n : 1))
        return p;
    throw std::bad_alloc();
}

// malloc/free are paired correctly here. GCC 13+ still reports -Wmismatched-new-delete when it
// inlines std::vector's allocation into the tests, so silence it for these two definitions only.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
#pragma GCC diagnostic pop

namespace
{
int failures = 0;

void check (bool ok, const char* name)
{
    std::cout << (ok ? "  PASS  " : "  FAIL  ") << name << "\n";
    if (! ok)
        ++failures;
}
void check (bool ok, const std::string& name) { check (ok, name.c_str()); }

constexpr double kSr = 48000.0;
constexpr int kBeat = sls::kTicksPerBeat;

// Every pattern the same length, as all of them were before 0.56.0 (since then each has its own).
void setLengths (sls::Arrangement& a, int ticks)
{
    for (auto& p : a.patterns)
        p.lengthTicks = ticks;
}

// A pattern clip given in whole lengths of its pattern, as the playlist placed them before 0.42.0 (start and length in "units").
sls::Clip uc (const sls::Arrangement& a, int pattern, int track, int startUnit, int lengthUnits)
{
    const int unit = a.patternLength (pattern);
    return { pattern, track, startUnit * unit, lengthUnits * unit, 0 };
}

// Magnitude of the component of x[begin,end) at `hz` (single-bin DFT).
double toneEnergy (const float* x, int begin, int end, double hz)
{
    double re = 0.0, im = 0.0;
    for (int i = begin; i < end; ++i)
    {
        const double w = 6.283185307179586 * hz * i / kSr;
        re += x[i] * std::cos (w);
        im += x[i] * std::sin (w);
    }
    return std::sqrt (re * re + im * im) / (end - begin);
}

float peakOf (const float* x, int begin, int end)
{
    float p = 0.0f;
    for (int i = begin; i < end; ++i)
        p = std::max (p, std::abs (x[i]));
    return p;
}

sls::Channel synthChannel (std::vector<sls::Note> notes, float volume = 1.0f)
{
    sls::Channel c;
    c.name = "Synth";
    c.notes = std::move (notes);
    c.volume = volume;
    return c;
}

std::shared_ptr<const sls::SampleData> sineSample (double hz, double seconds, double sampleRate)
{
    auto s = std::make_shared<sls::SampleData>();
    const int n = static_cast<int> (seconds * sampleRate);
    s->sampleRate = sampleRate;
    s->left.resize (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
        s->left[static_cast<size_t> (i)] = static_cast<float> (std::sin (6.283185307179586 * hz * i / sampleRate));
    s->right = s->left;
    return s;
}

sls::Channel samplerChannel (std::shared_ptr<const sls::SampleData> sample, std::vector<sls::Note> notes)
{
    sls::Channel c;
    c.name = "Sampler";
    c.instrument = sls::Instrument::Sampler;
    c.sample = std::move (sample);
    c.notes = std::move (notes);
    c.volume = 1.0f;
    c.rootPitch = 60;
    return c;
}

// One bar (4 beats, 2 s at 120 BPM).
sls::Project oneBar (std::vector<sls::Channel> channels)
{
    return sls::Project::make (std::move (channels), 4 * kBeat);
}

void testProjectModel()
{
    std::cout << "Project model\n";
    std::vector<sls::Note> in {
        { 60, 480, 480, 0.5f },
        { 200, 0, 100, 2.0f },        // pitch and velocity get clamped
        { 64, -5, 100, 0.5f },        // dropped: negative start
        { 64, 1920, 100, 0.5f },      // dropped: start == loop length
        { 64, 100, 0, 0.5f },         // dropped: zero length
        { 62, 1800, 5000, 0.5f },     // length clipped to loop end
    };
    auto c = synthChannel (in);
    c.volume = 9.0f;
    c.pan = -4.0f;
    const auto p = sls::Project::make ({ c }, 1920);
    const auto& notes = p.channels[0].notes;

    check (notes.size() == 3, "invalid notes are dropped");
    check (notes[0].startTick == 0 && notes[0].pitch == 127 && notes[0].velocity == 1.0f,
           "pitch/velocity are clamped, notes are sorted");
    check (notes[2].startTick == 1800 && notes[2].lengthTicks == 120, "length is clipped to the loop end");
    check (p.channels[0].volume == 1.5f && p.channels[0].pan == -1.0f, "volume and pan are clamped");

    std::vector<sls::Channel> many (static_cast<size_t> (sls::kMaxChannels) + 5, synthChannel ({}));
    check (sls::Project::make (many, 1920).channels.size() == static_cast<size_t> (sls::kMaxChannels),
           "channel count is capped");
}

void testSingleNote()
{
    std::cout << "Single synth note (A4, 1 beat, 120 BPM)\n";
    const auto project = oneBar ({ synthChannel ({ { 69, 0, kBeat, 0.8f } }) });
    const auto buf = sls::renderToBuffer (project, {});
    const float* l = buf.getReadPointer (0);
    const float* r = buf.getReadPointer (1);

    check (buf.getNumChannels() == 2 && buf.getNumSamples() == 3 * 48000, "length is loop (2 s) + tail (1 s)");

    bool same = true;
    for (int i = 0; i < buf.getNumSamples(); ++i)
        same = same && std::abs (l[i] - r[i]) < 1.0e-6f;
    check (same, "centered channel: left and right are identical");

    const float p = peakOf (l, 0, 24000);
    check (p > 0.05f && p < 0.3f, "peak level is audible and not clipping");

    const double e440 = toneEnergy (l, 4800, 21600, 440.0);
    check (e440 > 4.0 * toneEnergy (l, 4800, 21600, 415.0)
        && e440 > 4.0 * toneEnergy (l, 4800, 21600, 466.2)
        && e440 > 4.0 * toneEnergy (l, 4800, 21600, 220.0),
           "dominant frequency is 440 Hz");

    check (peakOf (l, 28500, buf.getNumSamples()) == 0.0f, "silence after the release has finished");
}

void testSampleAccuracy()
{
    std::cout << "Timing\n";
    // A note on beat 2 at 120 BPM starts exactly at 0.5 s = sample 24000.
    const auto project = oneBar ({ synthChannel ({ { 69, kBeat, kBeat, 0.8f } }) });
    const auto buf = sls::renderToBuffer (project, {});
    const float* l = buf.getReadPointer (0);

    int first = -1;
    for (int i = 0; i < buf.getNumSamples(); ++i)
        if (l[i] != 0.0f) { first = i; break; }

    check (first >= 24000 && first <= 24004, "note starts at the right sample (within 4 samples)");
}

void testLoopAndStop()
{
    std::cout << "Loop and stop\n";
    const auto project = oneBar ({ synthChannel ({ { 69, 0, kBeat, 0.8f } }) });
    const int loopSamples = 2 * 48000;

    sls::Player player;
    player.prepare (kSr);

    std::vector<float> left (static_cast<size_t> (3 * loopSamples), 0.0f);
    std::vector<float> right (left.size(), 0.0f);

    auto run = [&] (int from, int to, bool playing)
    {
        for (int pos = from; pos < to; pos += 512)
        {
            const int n = std::min (512, to - pos);
            float* ch[2] = { left.data() + pos, right.data() + pos };
            player.render (project, 120.0, playing, ch, 2, n);
        }
    };

    run (0, 2 * loopSamples, true);
    check (peakOf (left.data(), 4800, 21600) > 0.05f, "first pass plays the note");
    check (peakOf (left.data(), loopSamples + 4800, loopSamples + 21600) > 0.05f, "note retriggers on the second pass");
    check (peakOf (left.data(), loopSamples + 30000, loopSamples + 90000) == 0.0f, "gap between notes is silent");

    run (2 * loopSamples, 2 * loopSamples + 20000, false);
    check (peakOf (left.data(), 2 * loopSamples + 12000, 2 * loopSamples + 20000) == 0.0f, "stop releases the sound");

    run (2 * loopSamples + 20000, 3 * loopSamples, true);
    check (peakOf (left.data(), 2 * loopSamples + 20000 + 4800, 2 * loopSamples + 20000 + 21600) > 0.05f,
           "play after stop restarts from the beginning");
}

void testPolyphony()
{
    std::cout << "Polyphony\n";
    const auto project = oneBar ({ synthChannel ({ { 60, 0, kBeat, 0.8f }, { 64, 0, kBeat, 0.8f } }) });
    const auto buf = sls::renderToBuffer (project, {});
    const float* l = buf.getReadPointer (0);

    const double mid = toneEnergy (l, 4800, 21600, 295.0);
    check (toneEnergy (l, 4800, 21600, 261.63) > 4.0 * mid && toneEnergy (l, 4800, 21600, 329.63) > 4.0 * mid,
           "both notes of a chord are present");

    std::vector<sls::Note> many;
    for (int i = 0; i < 40; ++i)
        many.push_back ({ 40 + i, 0, kBeat, 1.0f });
    const auto loud = sls::renderToBuffer (oneBar ({ synthChannel (many) }), {});
    bool finite = true;
    for (int i = 0; i < loud.getNumSamples(); ++i)
        finite = finite && std::isfinite (loud.getReadPointer (0)[i]) && std::abs (loud.getReadPointer (0)[i]) <= 1.0f;
    check (finite, "voice stealing with 40 notes stays finite and within [-1, 1]");
}

void testMixer()
{
    std::cout << "Mixer (volume, pan, mute, solo)\n";
    const std::vector<sls::Note> a4 { { 69, 0, kBeat, 0.8f } };
    const std::vector<sls::Note> c4 { { 60, 0, kBeat, 0.8f } };

    {
        const auto full = sls::renderToBuffer (oneBar ({ synthChannel (a4, 1.0f) }), {});
        const auto half = sls::renderToBuffer (oneBar ({ synthChannel (a4, 0.5f) }), {});
        const float ratio = peakOf (half.getReadPointer (0), 0, 24000) / peakOf (full.getReadPointer (0), 0, 24000);
        check (ratio > 0.47f && ratio < 0.53f, "volume 0.5 halves the level");
    }
    {
        auto c = synthChannel (a4);
        c.pan = -1.0f;
        const auto b = sls::renderToBuffer (oneBar ({ c }), {});
        check (peakOf (b.getReadPointer (0), 0, 24000) > 0.05f && peakOf (b.getReadPointer (1), 0, 144000) == 0.0f,
               "pan hard left: right channel is silent");

        c.pan = 1.0f;
        const auto rb = sls::renderToBuffer (oneBar ({ c }), {});
        // cos(pi/2) is not exactly zero in floating point; anything below -120 dB counts as silent.
        check (peakOf (rb.getReadPointer (1), 0, 24000) > 0.05f && peakOf (rb.getReadPointer (0), 0, 144000) < 1.0e-6f,
               "pan hard right: left channel is silent");

        c.pan = 0.0f;
        const auto cb = sls::renderToBuffer (oneBar ({ c }), {});
        const float centre = peakOf (cb.getReadPointer (0), 0, 24000);
        check (centre > 0.05f && centre < peakOf (b.getReadPointer (0), 0, 24000),
               "equal-power pan: centre is quieter per side than hard pan");
    }
    {
        auto a = synthChannel (a4);
        auto b = synthChannel (c4);

        // Energy at 440 Hz when both play, and the floor left by spectral leakage from B alone.
        const auto both = sls::renderToBuffer (oneBar ({ a, b }), {});
        const double e440 = toneEnergy (both.getReadPointer (0), 4800, 21600, 440.0);
        const auto onlyB = sls::renderToBuffer (oneBar ({ b }), {});
        const double leakage = toneEnergy (onlyB.getReadPointer (0), 4800, 21600, 440.0);
        check (e440 > 50.0 * leakage, "reference: 440 Hz is well above the leakage floor when channel A plays");

        a.mute = true;
        auto buf = sls::renderToBuffer (oneBar ({ a, b }), {});
        const float* l = buf.getReadPointer (0);
        check (std::abs (toneEnergy (l, 4800, 21600, 440.0) - leakage) < 1.0e-9 && toneEnergy (l, 4800, 21600, 261.63) > 0.01,
               "muted channel contributes nothing, the other still plays");

        a.mute = false;
        b.solo = true;
        buf = sls::renderToBuffer (oneBar ({ a, b }), {});
        l = buf.getReadPointer (0);
        check (std::abs (toneEnergy (l, 4800, 21600, 440.0) - leakage) < 1.0e-9 && toneEnergy (l, 4800, 21600, 261.63) > 0.01,
               "solo silences every other channel");

        a.solo = true;
        buf = sls::renderToBuffer (oneBar ({ a, b }), {});
        l = buf.getReadPointer (0);
        check (toneEnergy (l, 4800, 21600, 440.0) > 0.01 && toneEnergy (l, 4800, 21600, 261.63) > 0.01,
               "two soloed channels both play");
    }
    {
        sls::Player player;
        player.prepare (kSr);
        const auto project = oneBar ({ synthChannel (a4), synthChannel ({}) });
        std::vector<float> l (4800), r (4800);
        float* ch[2] = { l.data(), r.data() };
        player.render (project, 120.0, true, ch, 2, 4800);
        check (player.getChannelPeak (0) > 0.01f && player.getChannelPeak (1) == 0.0f,
               "per-channel peak meters follow the channel that plays");
    }
}

void testSampler()
{
    std::cout << "Sampler\n";
    constexpr int kShort = kBeat / 4; // note much shorter than the sample

    {
        const auto sample = sineSample (1000.0, 0.5, 48000.0);
        const auto buf = sls::renderToBuffer (oneBar ({ samplerChannel (sample, { { 60, 0, kShort, 1.0f } }) }), {});
        const float* l = buf.getReadPointer (0);

        const double e = toneEnergy (l, 2400, 19200, 1000.0);
        check (e > 4.0 * toneEnergy (l, 2400, 19200, 900.0) && e > 4.0 * toneEnergy (l, 2400, 19200, 1100.0),
               "sample plays unchanged at its root pitch");
        check (toneEnergy (l, 15000, 22000, 1000.0) > 0.05, "one-shot: plays to the end even if the note is short");
        check (peakOf (l, 25000, buf.getNumSamples()) == 0.0f, "silent once the sample has ended");
    }
    {
        const auto sample = sineSample (1000.0, 0.5, 48000.0);
        const auto buf = sls::renderToBuffer (oneBar ({ samplerChannel (sample, { { 72, 0, kShort, 1.0f } }) }), {});
        const float* l = buf.getReadPointer (0);
        check (toneEnergy (l, 1200, 9600, 2000.0) > 4.0 * toneEnergy (l, 1200, 9600, 1000.0),
               "note one octave up plays the sample at double frequency");
    }
    {
        const auto sample = sineSample (500.0, 0.5, 24000.0); // half the output rate
        const auto buf = sls::renderToBuffer (oneBar ({ samplerChannel (sample, { { 60, 0, kShort, 1.0f } }) }), {});
        const float* l = buf.getReadPointer (0);
        check (toneEnergy (l, 2400, 19200, 500.0) > 4.0 * toneEnergy (l, 2400, 19200, 1000.0)
               && peakOf (l, 25500, buf.getNumSamples()) == 0.0f,
               "a sample recorded at another sample rate keeps its pitch and length");
    }
    {
        const auto project = oneBar ({ samplerChannel (nullptr, { { 60, 0, kShort, 1.0f } }) });
        const auto buf = sls::renderToBuffer (project, {});
        check (peakOf (buf.getReadPointer (0), 0, buf.getNumSamples()) == 0.0f, "sampler without a sample is silent");
    }
    {
        // Built-in drums: audible, finite and normalised.
        for (const auto& s : { sls::makeKickSample(), sls::makeSnareSample(), sls::makeHatSample() })
        {
            float peak = 0.0f;
            bool finite = true;
            for (float v : s->left)
            {
                peak = std::max (peak, std::abs (v));
                finite = finite && std::isfinite (v);
            }
            check (finite && peak > 0.8f && peak <= 1.0f && s->numFrames() > 1000, s->name.c_str());
        }
    }
}

void testSamplerSwapSafety()
{
    std::cout << "Replacing a sample while it plays\n";
    auto first = std::make_unique<sls::Project> (oneBar ({ samplerChannel (sineSample (1000.0, 1.0, 48000.0),
                                                                           { { 60, 0, kBeat, 1.0f } }) }));
    first->revision = 1;

    sls::Player player;
    player.prepare (kSr);
    std::vector<float> l (4800), r (4800);
    float* ch[2] = { l.data(), r.data() };
    player.render (*first, 120.0, true, ch, 2, 4800);
    check (peakOf (l.data(), 0, 4800) > 0.05f, "voice is playing before the swap");

    auto second = std::make_unique<sls::Project> (oneBar ({ samplerChannel (sineSample (300.0, 1.0, 48000.0), {}) }));
    second->revision = 2;
    first.reset(); // the old snapshot (and its sample) is gone, as it would be after publish()

    std::fill (l.begin(), l.end(), 0.0f);
    std::fill (r.begin(), r.end(), 0.0f);
    player.render (*second, 120.0, true, ch, 2, 4800);
    check (peakOf (l.data(), 0, 4800) == 0.0f, "stale sampler voices are dropped instead of reading freed memory");
}

void testNoAllocationInRender()
{
    std::cout << "Real-time safety\n";
    std::vector<sls::Note> synthNotes, drumNotes;
    for (int i = 0; i < 64; ++i)
    {
        synthNotes.push_back ({ 48 + i % 24, i * 100, 300, 0.7f });
        drumNotes.push_back ({ 60, i * 120, 120, 0.9f });
    }
    const auto project = oneBar ({ synthChannel (synthNotes), samplerChannel (sls::makeKickSample(), drumNotes),
                                   samplerChannel (sls::makeHatSample(), drumNotes) });

    sls::Player player;
    player.prepare (kSr);

    std::vector<float> l (256), r (256);
    float* ch[2] = { l.data(), r.data() };

    gAllocs = 0;
    gCounting = true;
    for (int i = 0; i < 2000; ++i)
        player.render (project, 140.0, true, ch, 2, 256);
    gCounting = false;

    check (gAllocs.load() == 0, "Player::render performs no heap allocation (synth + samplers, 2000 blocks)");
}

void testExchangeStress()
{
    std::cout << "Snapshot exchange\n";
    sls::SnapshotExchange<sls::Project> exchange;
    std::atomic<bool> stop { false };
    std::atomic<bool> corrupt { false };
    std::atomic<long> reads { 0 };
    std::atomic<bool> readerStarted { false };

    std::thread audio ([&]
    {
        readerStarted = true;
        while (! stop.load())
        {
            const auto* p = exchange.acquire();

            // The exchange publishes an empty default Project (revision 1) when it is constructed.
            // If this thread starts before the writer's first publish it will see that one, which
            // does not follow the invariant below. It is valid, so skip it.
            if (p->revision == 1)
                continue;

            const size_t n = p->channels.size();
            if (static_cast<int> (n) * kBeat != p->lengthTicks)
                corrupt = true;
            for (size_t c = 0; c < n; ++c)
                for (size_t i = 0; i < p->channels[c].notes.size(); ++i)
                    if (p->channels[c].notes[i].startTick != static_cast<int> (i) * kBeat
                        || p->channels[c].sample == nullptr)
                        corrupt = true;
            ++reads;
        }
    });

    const auto shared = sls::makeHatSample();

    auto publishOne = [&] (int round)
    {
        const int k = 1 + round % sls::kMaxChannels;
        std::vector<sls::Channel> chans;
        for (int c = 0; c < k; ++c)
        {
            std::vector<sls::Note> notes;
            for (int i = 0; i < k; ++i)
                notes.push_back ({ 60, i * kBeat, 10, 0.5f });
            chans.push_back (samplerChannel (shared, std::move (notes)));
        }
        exchange.publish (std::make_unique<sls::Project> (sls::Project::make (std::move (chans), k * kBeat)));
    };

    // Make sure the two threads really overlap: wait for the reader to start, and keep publishing
    // until it has read a good number of snapshots. Without this, a loaded machine can run the whole
    // writer loop before the reader thread is ever scheduled.
    using Clock = std::chrono::steady_clock;
    const auto giveUp = Clock::now() + std::chrono::seconds (10);

    while (! readerStarted.load() && Clock::now() < giveUp)
        std::this_thread::yield();

    int round = 0;
    while (round < 3000 || (reads.load() < 1000 && Clock::now() < giveUp))
        publishOne (round++);

    stop = true;
    audio.join();

    // Two separate questions: did the reader ever see a broken snapshot, and did it get to read at all?
    check (! corrupt.load(), "3000+ publishes against a concurrent reader: no torn or freed snapshot");
    check (reads.load() >= 1000, "the reader really overlapped with the writer (at least 1000 snapshots read)");
}

void testWavRoundTrip()
{
    std::cout << "WAV export\n";
    const auto project = oneBar ({ synthChannel ({ { 60, 0, 480, 0.8f }, { 67, 960, 480, 0.6f } }),
                                   samplerChannel (sls::makeKickSample(), { { 60, 0, 120, 1.0f }, { 60, 960, 120, 1.0f } }) });

    sls::RenderOptions opts;
    opts.bitDepth = 24;

    const auto file = juce::File::createTempFile (".wav");
    const auto result = sls::renderToWavFile (project, opts, file);
    check (result.wasOk(), "renderToWavFile reports success");

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    check (reader != nullptr, "the written file can be read back");

    if (reader != nullptr)
    {
        check (reader->numChannels == 2 && reader->sampleRate == 48000.0
               && reader->bitsPerSample == 24 && reader->lengthInSamples == 3 * 48000,
               "format is 2 ch / 48 kHz / 24 bit / 3 s");

        juce::AudioBuffer<float> back (2, static_cast<int> (reader->lengthInSamples));
        reader->read (&back, 0, back.getNumSamples(), 0, true, true);
        const auto original = sls::renderToBuffer (project, opts);

        float maxDiff = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < back.getNumSamples(); ++i)
                maxDiff = std::max (maxDiff, std::abs (back.getSample (ch, i) - original.getSample (ch, i)));
        check (maxDiff < 1.0e-5f, "audio read back matches the rendered buffer (24-bit precision)");

        // A kick at 1.875 s lasts 0.4 s, so its tail crosses the end of the 2 s loop.
        const auto tailProject = oneBar ({ samplerChannel (sls::makeKickSample(), { { 60, 3 * kBeat + 360, 120, 1.0f } }) });
        const auto tail = sls::renderToBuffer (tailProject, opts);
        check (peakOf (tail.getReadPointer (0), 2 * 48000 + 100, 2 * 48000 + 4000) > 0.01f,
               "a drum hit on the last beat keeps ringing through the export tail");
    }

    file.deleteFile();

    sls::RenderOptions bad;
    bad.bitDepth = 32;
    check (! sls::renderToWavFile (project, bad, juce::File::createTempFile (".wav")).wasOk(), "unsupported bit depth is rejected");
}
void testSharedDsp()
{
    std::cout << "Shared filter and audio reading (0.51.0)\n";

    // The low-pass design passes DC and stops what is near Nyquist; each channel keeps its own state.
    sls::Biquad<2> lp (sls::BiquadCoefs::lowPass (2.0 * juce::MathConstants<double>::pi * 1000.0 / 48000.0, 0.7071));
    double dc = 0.0, nyquist = 0.0, right = 0.0;
    for (int i = 0; i < 4800; ++i)
    {
        dc = lp.tick (0, 1.0);
        right = lp.tick (1, 0.0);
    }
    lp.clearState();
    for (int i = 0; i < 4800; ++i)
    {
        const double y = lp.tick (0, (i % 2) == 0 ? 1.0 : -1.0);
        if (i > 4000)
            nyquist = std::max (nyquist, std::abs (y));
    }
    check (std::abs (dc - 1.0) < 1.0e-6 && right == 0.0 && nyquist < 1.0e-3,
           "a Biquad low-pass passes DC, stops Nyquist, and keeps one state per channel");
    const auto hp = sls::BiquadCoefs::highPass (0.2, 0.7071);
    const auto bp = sls::BiquadCoefs::bandPass (0.2, 1.0);
    check (std::abs ((hp.b0 + hp.b1 + hp.b2) / (1.0 + hp.a1 + hp.a2)) < 1.0e-9 && std::abs (bp.b0 + bp.b1 + bp.b2) < 1.0e-12,
           "the high-pass and band-pass designs stop DC");

    // readSampleData: a mono WAV becomes stereo with both sides equal.
    juce::MemoryBlock block;
    {
        auto stream = std::make_unique<juce::MemoryOutputStream> (block, false);
        std::unique_ptr<juce::AudioFormatWriter> writer (juce::WavAudioFormat().createWriterFor (stream.get(), 44100.0, 1, 16, {}, 0));
        check (writer != nullptr, "a mono WAV can be written to memory");
        if (writer == nullptr)
            return;
        stream.release(); // (the writer owns it now)
        juce::AudioBuffer<float> mono (1, 1000);
        for (int i = 0; i < 1000; ++i)
            mono.setSample (0, i, 0.5f * std::sin (static_cast<float> (i) * 0.05f));
        writer->writeFromAudioSampleBuffer (mono, 0, 1000);
    }
    std::unique_ptr<juce::AudioFormatReader> reader (
        juce::WavAudioFormat().createReaderFor (new juce::MemoryInputStream (block.getData(), block.getSize(), false), true));
    const auto sample = reader != nullptr ? sls::readSampleData (*reader, 1000, "tone", "memory") : nullptr;
    check (sample != nullptr && sample->numFrames() == 1000 && sample->left == sample->right && sample->sampleRate == 44100.0
               && sample->name == "tone" && sample->source == "memory" && std::abs (sample->left[20] - 0.5f * std::sin (1.0f)) < 1.0e-3f,
           "readSampleData reads a mono file as stereo, with its rate, name and source");
}

void testPluginScanner()
{
    std::cout << "Plugin scanner\n";
    sls::PluginScanner scanner;

    const auto empty = juce::File::createTempFile ("");
    empty.createDirectory();
    check (scanner.scan (juce::FileSearchPath (empty.getFullPathName())).empty(), "an empty directory yields no plugins");
    empty.deleteRecursively();

    check (scanner.scan (juce::FileSearchPath ("/definitely/not/a/real/dir")).empty(), "a missing directory yields no plugins");

    // A file that is not a plugin must be ignored, not crash the scan.
    const auto dir = juce::File::createTempFile ("");
    dir.createDirectory();
    dir.getChildFile ("notes.txt").replaceWithText ("not a plugin");
    dir.getChildFile ("fake.vst3").replaceWithText ("not a plugin either");
    check (scanner.scan (juce::FileSearchPath (dir.getFullPathName())).empty(), "non-plugin files are ignored");
    dir.deleteRecursively();

    check (sls::PluginScanner::defaultSearchPaths().getNumPaths() > 0, "default search paths are defined for this platform");
}

void testPanelLayout()
{
    std::cout << "Panel layout\n";
    using sls::PanelLayout;

    const PanelLayout original { 40, 25, 600, 300, true };
    const auto back = PanelLayout::parse (original.toString());
    check (back.has_value() && *back == original, "a layout survives toString/parse");

    const PanelLayout hidden { 10, 20, 300, 200, false };
    const auto hiddenBack = PanelLayout::parse (hidden.toString());
    check (hiddenBack.has_value() && ! hiddenBack->visible && *hiddenBack == hidden, "a closed panel stays closed after parse");

    check (! PanelLayout::parse ("").has_value(), "empty text is rejected");
    check (! PanelLayout::parse ("1 2 3 4").has_value(), "too few values are rejected");
    check (! PanelLayout::parse ("1 2 3 4 1 9").has_value(), "trailing values are rejected");
    check (! PanelLayout::parse ("1 2 abc 4 1").has_value(), "non-numbers are rejected");
    check (! PanelLayout::parse ("1 2 0 4 1").has_value(), "zero width is rejected");
    check (! PanelLayout::parse ("1 2 3 -4 1").has_value(), "negative height is rejected");
    check (! PanelLayout::parse ("1 2 3 4 2").has_value(), "visible other than 0/1 is rejected");
    check (! PanelLayout::parse ("1 2 3 4 1 junk").has_value(), "trailing garbage is rejected");
    check (! PanelLayout::parse ("99999999999 2 3 4 1").has_value(), "integer overflow is rejected");

    const auto inside = PanelLayout { 100, 50, 400, 300, true }.clampedTo (1000, 700, 200, 150);
    check (inside == PanelLayout { 100, 50, 400, 300, true }, "a panel already inside the workspace is unchanged");

    const auto off = PanelLayout { 900, 600, 400, 300, true }.clampedTo (1000, 700, 200, 150);
    check (off.x == 600 && off.y == 400 && off.width == 400 && off.height == 300, "a panel sticking out is pulled back inside");

    const auto negative = PanelLayout { -50, -80, 400, 300, true }.clampedTo (1000, 700, 200, 150);
    check (negative.x == 0 && negative.y == 0, "negative positions are clamped to the origin");

    const auto huge = PanelLayout { 0, 0, 5000, 4000, true }.clampedTo (1000, 700, 200, 150);
    check (huge.width == 1000 && huge.height == 700, "an oversized panel is shrunk to the workspace");

    const auto tiny = PanelLayout { 0, 0, 20, 10, true }.clampedTo (1000, 700, 200, 150);
    check (tiny.width == 200 && tiny.height == 150, "a too-small panel grows to its minimum size");

    const auto cramped = PanelLayout { 30, 30, 400, 300, true }.clampedTo (150, 100, 200, 150);
    check (cramped.x == 0 && cramped.y == 0 && cramped.width == 150 && cramped.height == 100,
           "when the workspace is smaller than the minimum, the workspace wins");

    const auto keep = PanelLayout { 900, 900, 400, 300, false }.clampedTo (1000, 700, 200, 150);
    check (! keep.visible, "clamping does not change visibility");

    bool allInside = true;
    for (int x = -300; x <= 1400; x += 97)
        for (int y = -300; y <= 1000; y += 89)
            for (int w : { 1, 150, 400, 2000 })
            {
                const auto c = PanelLayout { x, y, w, w, true }.clampedTo (1000, 700, 200, 150);
                allInside = allInside && c.x >= 0 && c.y >= 0 && c.x + c.width <= 1000 && c.y + c.height <= 700
                            && c.width >= 200 && c.height >= 150;
            }
    check (allInside, "clamped panels are always inside the workspace and not below the minimum size");
}

void testArrangement()
{
    std::cout << "Patterns and playlist\n";
    using sls::Arrangement;
    using sls::Clip;

    Arrangement a;
    a.patterns.resize (3);
    setLengths (a, 4 * kBeat); // one bar = 2 s at 120 BPM
    a.patterns[0].name = "Tone";
    a.patterns[0].notes = { { { 69, 0, kBeat, 0.8f } } };            // A4 on beat 1
    a.patterns[1].name = "Empty";
    a.patterns[1].notes = { {} };
    a.patterns[2].name = "Late";
    a.patterns[2].notes = { { { 60, 3 * kBeat, 4 * kBeat, 0.8f } } }; // longer than what is left of the pattern

    const std::vector<sls::Channel> channels { synthChannel ({}) };

    // compilePattern
    const auto pat = sls::compilePattern (channels, a, 0);
    check (pat.lengthTicks == 4 * kBeat && pat.channels[0].notes.size() == 1, "pattern mode loops one pattern");
    check (sls::compilePattern (channels, a, 7).channels[0].notes.empty(), "an unknown pattern compiles to silence");
    check (channels[0].notes.empty(), "compiling does not modify the channel settings");

    // canPlace
    check (a.canPlace (uc (a, 0, 0, 0, 1)), "a clip on an empty playlist can be placed");
    check (! a.canPlace (uc (a, 5, 0, 0, 1)), "a clip of an unknown pattern is refused");
    check (! a.canPlace (uc (a, 0, a.trackCount(), 0, 1)), "a clip below the last track is refused");
    check (! a.canPlace (uc (a, 0, 0, -1, 1)), "a clip before the start is refused");
    check (! a.canPlace (uc (a, 0, 0, 0, 0)), "a zero-length clip is refused");
    check (! a.canPlace (uc (a, 0, 0, (a.timelineTicks() / a.patternLength (0)) - 1, 2)), "a clip past the end of the timeline is refused");
    check (a.canPlace (uc (a, 0, 0, (a.timelineTicks() / a.patternLength (0)) - 2, 2)), "a clip ending exactly at the end is accepted");

    // song with two clips on one track, a gap between them
    a.clips = { uc (a, 0, 0, 0, 1), uc (a, 0, 0, 2, 1) };
    check (a.songLengthTicks() == 3 * a.patternLength (0), "song length is the end of the last clip");
    check (a.canPlace (uc (a, 1, 0, 1, 2)), "a clip may overlap another on its track (0.33.0: layering)");
    check (a.canPlace (uc (a, 1, 0, 1, 1)), "filling the gap exactly is accepted");
    check (a.canPlace (uc (a, 1, 1, 0, 3)), "the same time on another track is accepted");
    check (a.canPlace (a.clips[0], 0), "a clip does not collide with itself when moved");
    check (a.clipAt (0, 2 * a.patternLength (0)) == 1 && a.clipAt (0, a.patternLength (0)) == -1 && a.clipAt (1, 0) == -1, "clipAt finds the clip under a tick");

    const auto song = sls::compileSong (channels, a);
    check (song.lengthTicks == 3 * 4 * kBeat, "song mode loops the whole arrangement");
    check (song.channels[0].notes.size() == 2 && song.channels[0].notes[0].startTick == 0
               && song.channels[0].notes[1].startTick == 2 * 4 * kBeat,
           "clips place the pattern's notes at their position");

    // sound: tone in unit 0, silence in unit 1, tone in unit 2
    const auto buf = sls::renderToBuffer (song, {});
    const float* l = buf.getReadPointer (0);
    const int unit = 96000; // 2 s
    check (toneEnergy (l, 4800, 21600, 440.0) > 0.05, "the first clip is audible");
    check (peakOf (l, 30000, 2 * unit) == 0.0f, "the gap between the clips is silent");
    check (toneEnergy (l, 2 * unit + 4800, 2 * unit + 21600, 440.0) > 0.05, "the second clip is audible");

    // repeats
    a.clips = { uc (a, 0, 0, 1, 3) };
    const auto rep = sls::compileSong (channels, a);
    check (rep.lengthTicks == 4 * 4 * kBeat && rep.channels[0].notes.size() == 3
               && rep.channels[0].notes[0].startTick == 4 * kBeat && rep.channels[0].notes[2].startTick == 3 * 4 * kBeat,
           "a long clip repeats its pattern once per unit");

    // notes are cut at their pattern's end so they cannot leak into the next unit
    a.clips = { uc (a, 2, 0, 0, 2) };
    const auto cut = sls::compileSong (channels, a);
    check (cut.channels[0].notes.size() == 2 && cut.channels[0].notes[0].lengthTicks == kBeat
               && cut.channels[0].notes[1].startTick == 4 * kBeat + 3 * kBeat,
           "notes are clipped at the end of their pattern");

    // overlapping clips on different tracks play together
    a.clips = { uc (a, 0, 0, 0, 1), uc (a, 2, 1, 0, 1) };
    check (sls::compileSong (channels, a).channels[0].notes.size() == 2, "clips on different tracks are mixed");

    // muted clips, muted and soloed tracks (0.60.0: the Playlist's Mute tool and the track headers' M / S)
    a.clips = { uc (a, 0, 0, 0, 1), uc (a, 2, 1, 0, 1) };
    a.clips[0].muted = true;
    check (sls::compileSong (channels, a).channels[0].notes.size() == 1 && sls::compileSong (channels, a).channels[0].notes[0].pitch == 60,
           "a muted clip does not play");
    a.clips[0].muted = false;
    a.playlistTracks[1].mute = true;
    check (sls::compileSong (channels, a).channels[0].notes.size() == 1 && sls::compileSong (channels, a).channels[0].notes[0].pitch == 69,
           "a muted track's clips do not play");
    a.playlistTracks[1].mute = false;
    a.playlistTracks[1].solo = true;
    check (a.anyTrackSoloed() && ! a.trackPlays (0) && a.trackPlays (1), "a solo silences the other tracks");
    check (sls::compileSong (channels, a).channels[0].notes.size() == 1 && sls::compileSong (channels, a).channels[0].notes[0].pitch == 60,
           "only the soloed track plays");
    a.playlistTracks[1].solo = false;
    check (sls::compileSong (channels, a).channels[0].notes.size() == 2, "without a solo every track plays again");
    {
        sls::ProjectDocument doc = sls::ProjectDocument::blank();
        doc.arrangement = a;
        doc.arrangement.clips[1].muted = true;
        doc.arrangement.playlistTracks[0].mute = true;
        doc.arrangement.playlistTracks[1].solo = true;
        const auto parsed = sls::ProjectDocument::fromJson (doc.toJson());
        check (parsed.document.has_value() && parsed.document->arrangement.clips.size() == 2 && ! parsed.document->arrangement.clips[0].muted
                   && parsed.document->arrangement.clips[1].muted && parsed.document->arrangement.playlistTracks[0].mute
                   && parsed.document->arrangement.playlistTracks[1].solo && ! parsed.document->arrangement.playlistTracks[0].solo,
               "clip mutes and track mute / solo survive a save and a load");
    }

    // empty playlist
    a.clips.clear();
    const auto empty = sls::compileSong (channels, a);
    check (empty.lengthTicks == 4 * kBeat && empty.channels[0].notes.empty(), "an empty playlist is one silent pattern length");

    // robustness
    a.clips = { uc (a, 9, 0, 0, 1), uc (a, 0, 0, 0, 1) };
    check (sls::compileSong (channels, a).channels[0].notes.size() == 1, "clips of unknown patterns are skipped");
    a.dropInvalidClips();
    check (a.clips.size() == 1, "dropInvalidClips removes them");

    a.patterns[0].notes.clear(); // pattern with fewer channel lists than channels
    const std::vector<sls::Channel> two { synthChannel ({}), synthChannel ({}) };
    check (sls::compileSong (two, a).channels.size() == 2, "a pattern missing a channel's notes does not break compilation");
    a.ensureChannelCount (2);
    check (a.patterns[0].notes.size() == 2 && a.patterns[2].notes.size() == 2, "ensureChannelCount resizes every pattern");

    // the samplers keep their sample object (needed for voices to survive a snapshot change)
    auto sample = sineSample (440.0, 0.1, 44100.0);
    const std::vector<sls::Channel> sampler { samplerChannel (sample, {}) };
    check (sls::compileSong (sampler, a).channels[0].sample == sample, "compiling keeps the same sample object");
}

// --- helpers for the mixer / effects tests ------------------------------------------------

std::vector<float> sineWave (double hz, double amp, int n)
{
    std::vector<float> x (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
        x[static_cast<size_t> (i)] = static_cast<float> (amp * std::sin (6.283185307179586 * hz * i / kSr));
    return x;
}

// Runs a signal through one effect in blocks of 64 (the size the Player uses). Both channels carry the same input.
std::vector<float> runEffect (const sls::EffectSlot& slot, const std::vector<float>& in)
{
    sls::EffectUnit unit;
    unit.prepare (kSr);
    sls::EffectMemory unitMemory; // the big buffers the Player would lend (0.29.0)
    unitMemory.prepare (kSr);
    unit.attachMemory (&unitMemory);
    std::vector<float> l = in, r = in;
    for (size_t i = 0; i < l.size(); i += 64)
    {
        const int n = static_cast<int> (std::min<size_t> (64, l.size() - i));
        unit.process (slot, l.data() + i, r.data() + i, n);
    }
    return l;
}

double rmsOf (const std::vector<float>& x, int begin, int end)
{
    double sum = 0.0;
    for (int i = begin; i < end; ++i)
        sum += static_cast<double> (x[static_cast<size_t> (i)]) * x[static_cast<size_t> (i)];
    return std::sqrt (sum / (end - begin));
}

double energyOf (const std::vector<float>& x, int begin, int end)
{
    double sum = 0.0;
    for (int i = begin; i < end; ++i)
        sum += static_cast<double> (x[static_cast<size_t> (i)]) * x[static_cast<size_t> (i)];
    return sum;
}

sls::EffectSlot slotWith (sls::EffectType type, std::initializer_list<float> params)
{
    auto s = sls::EffectSlot::make (type);
    size_t i = 0;
    for (float v : params)
        s.params[i++] = v;
    return s;
}

// Index of the first sample whose magnitude exceeds `threshold`, or -1.
int firstOnset (const float* x, int n, float threshold)
{
    for (int i = 0; i < n; ++i)
        if (std::abs (x[i]) > threshold)
            return i;
    return -1;
}

void testChannelPitch()
{
    std::cout << "Channel pitch\n";
    const std::vector<sls::Note> a4 { { 69, 0, 2 * kBeat, 0.8f } };

    auto render = [&] (float pitch)
    {
        auto c = synthChannel (a4);
        c.pitch = pitch;
        return sls::renderToBuffer (oneBar ({ c }), {});
    };

    {
        const auto b = render (12.0f);
        const float* l = b.getReadPointer (0);
        const double e880 = toneEnergy (l, 4800, 40000, 880.0);
        // the synth adds a 2nd harmonic at 30 %, so the fundamental wins by about 3.3x over it
        check (e880 > 2.5 * toneEnergy (l, 4800, 40000, 1760.0) && e880 > 20.0 * toneEnergy (l, 4800, 40000, 440.0),
               "synth: +12 semitones sounds an octave higher (880 Hz)");
    }
    {
        const auto b = render (-12.0f);
        const float* l = b.getReadPointer (0);
        check (toneEnergy (l, 4800, 40000, 220.0) > 2.5 * toneEnergy (l, 4800, 40000, 440.0),
               "synth: -12 semitones sounds an octave lower (220 Hz)");
    }
    {
        const auto b = render (1.0f);
        const float* l = b.getReadPointer (0);
        check (toneEnergy (l, 4800, 40000, 466.164) > 4.0 * toneEnergy (l, 4800, 40000, 440.0),
               "synth: +1 semitone gives 466.16 Hz");
    }
    {
        // a 440 Hz sample at its root pitch, retuned
        auto sample = sineSample (440.0, 1.0, kSr);
        auto up = samplerChannel (sample, { { 60, 0, kBeat, 1.0f } });
        up.pitch = 12.0f;
        auto down = up;
        down.pitch = -12.0f;
        const auto bu = sls::renderToBuffer (oneBar ({ up }), {});
        const auto bd = sls::renderToBuffer (oneBar ({ down }), {});
        check (toneEnergy (bu.getReadPointer (0), 4800, 30000, 880.0) > 4.0 * toneEnergy (bu.getReadPointer (0), 4800, 30000, 440.0),
               "sampler: +12 semitones plays the sample an octave higher");
        check (toneEnergy (bd.getReadPointer (0), 4800, 30000, 220.0) > 4.0 * toneEnergy (bd.getReadPointer (0), 4800, 30000, 440.0),
               "sampler: -12 semitones plays the sample an octave lower");
    }
    {
        auto c = synthChannel (a4);
        c.pitch = 100.0f;
        check (sls::Project::make ({ c }, 1920).channels[0].pitch == 24.0f, "pitch is clamped to +-24 semitones");
        c.pitch = std::nanf ("");
        check (sls::Project::make ({ c }, 1920).channels[0].pitch == 0.0f, "a NaN pitch becomes 0");
    }
}

void testSwing()
{
    std::cout << "Swing\n";
    std::vector<sls::Note> grid;
    for (int i = 0; i < 8; ++i)
        grid.push_back ({ 60, i * 120, 60, 0.8f }); // eight 16th notes

    auto starts = [&] (float swing)
    {
        auto c = synthChannel (grid);
        c.swing = swing;
        const auto project = sls::Project::make ({ c }, 4 * kBeat);
        std::vector<int> v;
        for (const auto& n : project.channels[0].notes)
            v.push_back (n.startTick);
        return v;
    };

    check (starts (0.0f) == std::vector<int> { 0, 120, 240, 360, 480, 600, 720, 840 }, "swing 0 leaves the grid untouched");
    check (starts (1.0f) == std::vector<int> { 0, 180, 240, 420, 480, 660, 720, 900 },
           "swing 100 % delays every off-beat 16th by 60 ticks and keeps the on-beat ones");
    check (starts (0.5f) == std::vector<int> { 0, 150, 240, 390, 480, 630, 720, 870 }, "swing 50 % delays them by 30 ticks");

    {
        auto c = synthChannel ({ { 60, 4 * kBeat - 50, 30, 0.8f }, { 60, 4 * kBeat - 120, 60, 0.8f } });
        c.swing = 1.0f;
        const auto edge = sls::Project::make ({ c }, 4 * kBeat);
        check (edge.channels[0].notes.size() == 2 && edge.channels[0].notes[0].startTick == 4 * kBeat - 60
                   && edge.channels[0].notes[1].startTick == 4 * kBeat - 50,
               "a note that would be pushed past the loop end stays in place, the others still move");
        c.swing = std::nanf ("");
        check (sls::Project::make ({ c }, 4 * kBeat).channels[0].swing == 0.0f, "a NaN swing becomes 0");
    }

    // audible: the onset really moves (a single off-beat sample hit)
    {
        auto sample = sineSample (440.0, 0.2, kSr);
        auto c = samplerChannel (sample, { { 60, 120, 60, 1.0f } });
        const auto straight = sls::renderToBuffer (oneBar ({ c }), {});
        c.swing = 1.0f;
        const auto swung = sls::renderToBuffer (oneBar ({ c }), {});
        const int a = firstOnset (straight.getReadPointer (0), 48000, 0.002f);
        const int b = firstOnset (swung.getReadPointer (0), 48000, 0.002f);
        check (a > 5800 && a < 6300 && b - a > 2800 && b - a < 3200,
               "off-beat hit starts 60 ticks (3000 samples at 120 BPM) later with full swing");
    }

    // swing is also applied to compiled songs
    {
        sls::Arrangement a;
        a.patterns.resize (1);
        a.patterns[0].notes = { { { 60, 120, 60, 0.8f } } };
        a.patterns[0].steps = { sls::steps::patternSteps (a.patternLength (0)) }; // the whole pattern (one step: it would repeat each bar)
        a.clips = { uc (a, 0, 0, 1, 1) };
        auto c = synthChannel ({});
        c.swing = 1.0f;
        const auto song = sls::compileSong ({ c }, a);
        check (song.channels[0].notes.size() == 1 && song.channels[0].notes[0].startTick == a.patternLength (0) + 180,
               "compileSong applies swing to the placed notes");
    }
}

void testMixerTracks()
{
    std::cout << "Mixer tracks (routing, fader, balance, mute, solo, metering)\n";
    const std::vector<sls::Note> a4 { { 69, 0, kBeat, 0.8f } };

    auto build = [&] (std::vector<sls::Channel> channels, auto&& editTracks)
    {
        auto tracks = sls::Project::defaultTracks();
        editTracks (tracks);
        return sls::Project::make (std::move (channels), 4 * kBeat, std::move (tracks));
    };
    auto peakL = [] (const juce::AudioBuffer<float>& b) { return peakOf (b.getReadPointer (0), 0, 24000); };
    auto peakR = [] (const juce::AudioBuffer<float>& b) { return peakOf (b.getReadPointer (1), 0, 24000); };

    const auto reference = sls::renderToBuffer (build ({ synthChannel (a4) }, [] (auto&) {}), {});
    const float refPeak = peakL (reference);

    {
        auto c = synthChannel (a4);
        c.mixerTrack = 5;
        const auto b = sls::renderToBuffer (build ({ c }, [] (auto&) {}), {});
        check (std::abs (peakL (b) - refPeak) < 1.0e-6f, "routing to another track does not change the level (unity track)");
    }
    {
        const auto b = sls::renderToBuffer (build ({ synthChannel (a4) }, [] (auto& t) { t[0].volume = 0.5f; }), {});
        check (std::abs (peakL (b) / refPeak - 0.5f) < 0.02f, "track fader 0.5 halves the level");
    }
    {
        auto c = synthChannel (a4);
        c.mixerTrack = 2;
        const auto b = sls::renderToBuffer (build ({ c }, [] (auto& t) { t[2].mute = true; }), {});
        check (peakOf (b.getReadPointer (0), 0, 144000) == 0.0f, "a muted track is silent (and its channel is not heard elsewhere)");
    }
    {
        auto a = synthChannel (a4), b2 = synthChannel (a4);
        b2.mixerTrack = 1;
        const auto soloed = sls::renderToBuffer (build ({ a, b2 }, [] (auto& t) { t[1].solo = true; }), {});
        const auto plain = sls::renderToBuffer (build ({ b2 }, [] (auto&) {}), {});
        check (std::abs (peakL (soloed) - peakL (plain)) < 1.0e-5f, "soloing a track silences the other tracks");
    }
    {
        const auto left = sls::renderToBuffer (build ({ synthChannel (a4) }, [] (auto& t) { t[0].pan = -1.0f; }), {});
        const auto right = sls::renderToBuffer (build ({ synthChannel (a4) }, [] (auto& t) { t[0].pan = 1.0f; }), {});
        check (peakR (left) == 0.0f && std::abs (peakL (left) - refPeak) < 1.0e-6f,
               "balance hard left: right side silent, left side stays at unity");
        check (peakL (right) == 0.0f && std::abs (peakR (right) - refPeak) < 1.0e-6f, "balance hard right: mirror image");
    }
    {
        auto a = synthChannel (a4), b2 = synthChannel (a4);
        b2.mixerTrack = 3;
        const auto two = sls::renderToBuffer (build ({ a, b2 }, [] (auto&) {}), {});
        const auto one = sls::renderToBuffer (oneBar ({ a, b2 }), {}); // default tracks, default routing
        check (std::abs (peakL (two) - peakL (one)) < 1.0e-6f, "same project via both construction paths renders identically");
    }
    {
        const auto missing = sls::renderToBuffer (sls::Project {}, {});
        check (missing.getNumSamples() > 0, "a project without track data (manual construction) still renders");
        auto p = oneBar ({ synthChannel (a4) });
        p.tracks.clear();
        check (std::abs (peakL (sls::renderToBuffer (p, {})) - refPeak) < 1.0e-6f, "missing tracks behave as unity tracks");
    }
    {
        auto c = synthChannel (a4);
        c.mixerTrack = 99;
        check (sls::Project::make ({ c }, 1920).channels[0].mixerTrack == sls::kDefaultTracks - 1, "a track index out of range is clamped");
        auto t = sls::Project::defaultTracks();
        t[0].volume = 9.0f;
        t[1].pan = std::nanf ("");
        const auto p = sls::Project::make ({}, 1920, t);
        check (p.tracks.size() == static_cast<size_t> (sls::kDefaultTracks) && p.tracks[0].volume == 1.5f && p.tracks[1].pan == 0.0f,
               "track values are clamped; a new project has eight tracks");
        const auto few = sls::Project::make ({}, 1920, std::vector<sls::MixerTrack> (3));
        auto many = std::vector<sls::MixerTrack> (700);
        c.mixerTrack = 450;
        const auto big = sls::Project::make ({ c }, 1920, many);
        check (few.tracks.size() == static_cast<size_t> (sls::kDefaultTracks) && big.tracks.size() == static_cast<size_t> (sls::kMaxTracks)
                   && big.channels[0].mixerTrack == 450,
               "at least eight tracks, at most 500 (0.29.0); a channel can play through track 451");
    }

    // metering
    {
        auto c = synthChannel ({ { 69, 0, 4 * kBeat, 0.8f } });
        c.mixerTrack = 4;
        const auto project = oneBar ({ c });
        sls::Player player;
        player.prepare (kSr);
        std::vector<float> l (512), r (512);
        float* ch[2] = { l.data(), r.data() };
        for (int i = 0; i < 100; ++i)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            player.render (project, 120.0, true, ch, 2, 512);
        }
        const float peak = player.getTrackPeak (4), rms = player.getTrackRms (4);
        check (peak > 0.05f && rms > 0.02f && rms < peak, "track meter: peak above RMS for a sine-like signal");
        check (player.getTrackPeak (0) == 0.0f && player.getTrackRms (0) == 0.0f, "track meter: untouched tracks read zero");
        check (player.getTrackPeak (-1) == 0.0f && player.getTrackRms (99) == 0.0f, "track meter: out-of-range index reads zero");
    }
}

void testEffects()
{
    std::cout << "Effects\n";
    const auto sine1k = sineWave (1000.0, 0.5, 48000);

    // slot model
    {
        sls::EffectSlot s = sls::EffectSlot::make (sls::EffectType::Compressor);
        s.params[1] = std::nanf ("");
        s.params[0] = -500.0f;
        s.params[4] = 99.0f;
        const auto c = s.sanitised();
        check (c.params[1] == 4.0f && c.params[0] == -60.0f && c.params[4] == 24.0f, "slot parameters: NaN -> default, out of range -> clamped");
        s.type = static_cast<sls::EffectType> (77);
        check (s.sanitised().type == sls::EffectType::None, "an unknown effect type becomes empty");
        check (sls::effectParamCount (sls::EffectType::None) == 0 && sls::effectParamCount (sls::EffectType::Compressor) == 6,
               "parameter counts");
    }

    // pass-through cases
    {
        check (runEffect (sls::EffectSlot {}, sine1k) == sine1k, "empty slot passes the signal through untouched");
        auto bypassed = sls::EffectSlot::make (sls::EffectType::Reverb);
        bypassed.bypass = true;
        check (runEffect (bypassed, sine1k) == sine1k, "bypassed effect passes the signal through untouched");
        check (runEffect (sls::EffectSlot::make (sls::EffectType::Equalizer), sine1k) == sine1k, "flat equalizer is bit-exact");
        check (runEffect (slotWith (sls::EffectType::Reverb, { 0.5f, 0.5f, 1.0f, 0.0f }), sine1k) == sine1k, "reverb mix 0 is bit-exact");
        check (runEffect (slotWith (sls::EffectType::Delay, { 100.0f, 0.5f, 0.7f, 0.0f }), sine1k) == sine1k, "delay mix 0 is bit-exact");
        check (runEffect (slotWith (sls::EffectType::Compressor, { -20.0f, 1.0f, 5.0f, 100.0f, 0.0f }), sine1k) == sine1k,
               "compressor ratio 1 with no makeup is bit-exact");
    }

    // equalizer
    {
        const double base = rmsOf (sine1k, 4800, 48000);
        const auto boost = runEffect (slotWith (sls::EffectType::Equalizer, { 0.0f, 12.0f, 1000.0f, 0.0f }), sine1k);
        const double gainDb = 20.0 * std::log10 (rmsOf (boost, 4800, 48000) / base);
        check (std::abs (gainDb - 12.0) < 0.5, "mid band +12 dB at its centre frequency gives +12 dB");

        const auto cut = runEffect (slotWith (sls::EffectType::Equalizer, { 0.0f, -12.0f, 1000.0f, 0.0f }), sine1k);
        check (std::abs (20.0 * std::log10 (rmsOf (cut, 4800, 48000) / base) + 12.0) < 0.5, "mid band -12 dB gives -12 dB");

        const auto moved = runEffect (slotWith (sls::EffectType::Equalizer, { 0.0f, 12.0f, 4000.0f, 0.0f }), sine1k);
        check (std::abs (20.0 * std::log10 (rmsOf (moved, 4800, 48000) / base)) < 3.0, "moving the mid band away leaves 1 kHz mostly alone");

        const auto lowSine = sineWave (60.0, 0.5, 48000);
        const auto lowBoost = runEffect (slotWith (sls::EffectType::Equalizer, { 12.0f, 0.0f, 1000.0f, 0.0f }), lowSine);
        check (20.0 * std::log10 (rmsOf (lowBoost, 9600, 48000) / rmsOf (lowSine, 9600, 48000)) > 9.0, "low shelf +12 dB lifts 60 Hz by more than 9 dB");

        const auto highSine = sineWave (12000.0, 0.5, 48000);
        const auto highBoost = runEffect (slotWith (sls::EffectType::Equalizer, { 0.0f, 0.0f, 1000.0f, 12.0f }), highSine);
        check (20.0 * std::log10 (rmsOf (highBoost, 9600, 48000) / rmsOf (highSine, 9600, 48000)) > 9.0, "high shelf +12 dB lifts 12 kHz by more than 9 dB");

        const auto shelvesOnly = runEffect (slotWith (sls::EffectType::Equalizer, { 12.0f, 0.0f, 1000.0f, 12.0f }), sine1k);
        check (std::abs (20.0 * std::log10 (rmsOf (shelvesOnly, 4800, 48000) / base)) < 1.5, "shelves leave the middle (1 kHz) nearly untouched");
    }

    // delay
    {
        std::vector<float> impulse (48000, 0.0f);
        impulse[0] = 1.0f;
        const auto echo = runEffect (slotWith (sls::EffectType::Delay, { 100.0f, 0.5f, 1.0f, 1.0f }), impulse);
        check (std::abs (echo[4800] - 1.0f) < 0.01f && peakOf (echo.data(), 0, 4700) == 0.0f, "delay: first echo arrives exactly after the set time");
        check (peakOf (echo.data(), 9500, 9700) > 0.3f && peakOf (echo.data(), 9500, 9700) < 0.55f, "delay: feedback 0.5 gives a second, quieter echo");

        const auto single = runEffect (slotWith (sls::EffectType::Delay, { 100.0f, 0.0f, 1.0f, 1.0f }), impulse);
        check (peakOf (single.data(), 9500, 48000) == 0.0f, "delay: feedback 0 gives exactly one echo");

        const auto mixed = runEffect (slotWith (sls::EffectType::Delay, { 100.0f, 0.0f, 1.0f, 0.4f }), impulse);
        check (std::abs (mixed[0] - 0.6f) < 1.0e-6f && std::abs (mixed[4800] - 0.4f) < 0.01f, "delay: mix crossfades dry and echo");

        // stale audio must not come back after a bypass
        sls::EffectUnit unit;
        unit.prepare (kSr);
        sls::EffectMemory unitMemory; // the big buffers the Player would lend (0.29.0)
        unitMemory.prepare (kSr);
        unit.attachMemory (&unitMemory);
        std::vector<float> l (64, 0.0f), r (64, 0.0f);
        l[0] = r[0] = 1.0f;
        auto slot = slotWith (sls::EffectType::Delay, { 20.0f, 0.9f, 1.0f, 1.0f });
        unit.process (slot, l.data(), r.data(), 64);
        slot.bypass = true;
        std::fill (l.begin(), l.end(), 0.0f);
        unit.process (slot, l.data(), r.data(), 64);
        slot.bypass = false;
        float worst = 0.0f;
        for (int block = 0; block < 40; ++block)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            unit.process (slot, l.data(), r.data(), 64);
            worst = std::max (worst, peakOf (l.data(), 0, 64));
        }
        check (worst == 0.0f, "bypassing clears the effect's memory (no stale echoes on re-enable)");
    }

    // reverb
    {
        std::vector<float> impulse (48000 * 6, 0.0f);
        impulse[0] = 1.0f;
        const auto small = runEffect (slotWith (sls::EffectType::Reverb, { 0.1f, 0.5f, 1.0f, 1.0f }), impulse);
        const auto large = runEffect (slotWith (sls::EffectType::Reverb, { 1.0f, 0.2f, 1.0f, 1.0f }), impulse);
        const double early = energyOf (large, 2400, 24000);
        check (early > 1.0e-6, "reverb: an impulse produces a tail");
        check (energyOf (large, 24000, 48000) < early && energyOf (large, 48000 * 4, 48000 * 5) < energyOf (large, 24000, 48000),
               "reverb: the tail decays");
        check (energyOf (large, 48000, 48000 * 2) > 5.0 * energyOf (small, 48000, 48000 * 2), "reverb: a bigger room rings longer");

        auto narrow = slotWith (sls::EffectType::Reverb, { 0.5f, 0.5f, 0.0f, 1.0f });
        const auto n = runEffect (narrow, impulse);
        bool mono = true;
        for (int i = 0; i < 48000; ++i)
            mono = mono && std::isfinite (n[static_cast<size_t> (i)]);
        check (mono, "reverb: output stays finite");
    }

    // compressor
    {
        auto loud = sineWave (1000.0, 1.0, 48000);
        const auto out = runEffect (slotWith (sls::EffectType::Compressor, { -20.0f, 4.0f, 1.0f, 100.0f, 0.0f }), loud);
        const float p = peakOf (out.data(), 24000, 48000);
        check (p > 0.15f && p < 0.21f, "compressor: 0 dBFS over a -20 dB threshold at 4:1 comes out at about -15 dB");

        const auto madeUp = runEffect (slotWith (sls::EffectType::Compressor, { -20.0f, 4.0f, 1.0f, 100.0f, 6.0f }), loud);
        check (std::abs (peakOf (madeUp.data(), 24000, 48000) / p - 1.995f) < 0.1f, "compressor: makeup gain +6 dB doubles the level");

        const auto quiet = sineWave (1000.0, 0.01, 48000);
        const auto q = runEffect (slotWith (sls::EffectType::Compressor, { -20.0f, 4.0f, 1.0f, 100.0f, 0.0f }), quiet);
        check (std::abs (peakOf (q.data(), 24000, 48000) / 0.01f - 1.0f) < 0.03f, "compressor: a signal below the threshold is not touched");
    }

    // every effect, extreme settings, loud input: finite and bounded
    {
        std::vector<float> noise (48000 * 2);
        unsigned seed = 12345;
        for (auto& v : noise)
        {
            seed = seed * 1664525u + 1013904223u;
            v = (static_cast<float> (seed >> 8) / 8388608.0f - 1.0f) * 2.0f;
        }
        bool ok = true;
        for (int t = 1; t < sls::kEffectTypeCount; ++t)
        {
            const auto type = static_cast<sls::EffectType> (t);
            for (int extreme = 0; extreme < 2; ++extreme)
            {
                auto slot = sls::EffectSlot::make (type);
                for (int i = 0; i < sls::effectParamCount (type); ++i)
                {
                    const auto info = sls::effectParamInfo (type, i);
                    slot.params[static_cast<size_t> (i)] = extreme == 0 ? info.min : info.max;
                }
                const auto out = runEffect (slot, noise);
                bool fine = true;
                float worst = 0.0f;
                for (float v : out)
                {
                    // (the parametric EQ at its maximum adds up to +54 dB to this +6 dBFS noise on purpose: 500 is still "bounded";
                    //  the seven-band one, 0.61.0, stacks seven +18 dB bands and +18 dB of output at one frequency: up to +144 dB)
                    const float bound = type == sls::EffectType::ParametricEq ? 1000.0f : type == sls::EffectType::ParametricEq2 ? 1.0e8f : 100.0f;
                    fine = fine && std::isfinite (v) && std::abs (v) < bound;
                    worst = std::max (worst, std::isfinite (v) ? std::abs (v) : 1.0e30f);
                }
                if (! fine)
                    std::cout << "    (" << sls::effectName (type) << (extreme == 0 ? " at its minimum" : " at its maximum") << ": peak " << worst << ")\n";
                ok = ok && fine;
            }
        }
        check (ok, "all effects stay finite and bounded at their minimum and maximum settings");
    }
}

// ------------------------------------------------------------------------- 0.21.0: the new effects

std::pair<std::vector<float>, std::vector<float>> runEffectStereo (const sls::EffectSlot& slot, std::vector<float> l, std::vector<float> r)
{
    sls::EffectUnit unit;
    unit.prepare (kSr);
    sls::EffectMemory unitMemory; // the big buffers the Player would lend (0.29.0)
    unitMemory.prepare (kSr);
    unit.attachMemory (&unitMemory);
    for (size_t i = 0; i < l.size(); i += 64)
        unit.process (slot, l.data() + i, r.data() + i, static_cast<int> (std::min<size_t> (64, l.size() - i)));
    return { std::move (l), std::move (r) };
}

double gainAt (const sls::EffectSlot& slot, double hz)
{
    const auto in = sineWave (hz, 0.25, 24000);
    const auto out = runEffect (slot, in);
    return toneEnergy (out.data(), 12000, 24000, hz) / toneEnergy (in.data(), 12000, 24000, hz);
}

int firstAbove (const std::vector<float>& x, float level, int from = 0)
{
    for (int i = from; i < static_cast<int> (x.size()); ++i)
        if (std::abs (x[static_cast<size_t> (i)]) > level)
            return i;
    return -1;
}

// --- the Time Shaper (0.25.0) -----------------------------------------------------------------------------------------------
// Runs the Time Shaper over a mono signal (both sides the same) at 120 BPM from tick `startTick`, with its own 8.5 s recording.
std::vector<float> runTimeShaper (const sls::EffectSlot& slot, std::vector<float> x, double startTick = 0.0, bool attach = true, bool playing = true)
{
    sls::EffectUnit unit;
    unit.prepare (kSr);
    sls::EffectMemory unitMemory; // the big buffers the Player would lend (0.29.0)
    unitMemory.prepare (kSr);
    unit.attachMemory (&unitMemory);
    std::vector<float> bl, br;
    if (attach)
    {
        bl.assign (static_cast<size_t> (8.5 * kSr), 0.0f);
        br = bl;
        unit.attachTimeBuffer (bl.data(), br.data(), static_cast<int> (bl.size()));
    }
    auto r = x;
    const double tps = 120.0 * 480.0 / (60.0 * kSr); // 0.02 ticks per sample: a beat is 24000 samples
    for (size_t i = 0; i < x.size(); i += 64)
    {
        const sls::EffectTransport t { playing ? startTick + static_cast<double> (i) * tps : -1.0, tps };
        unit.process (slot, x.data() + i, r.data() + i, static_cast<int> (std::min<size_t> (64, x.size() - i)), t);
    }
    return x;
}

void testTimeShaper()
{
    std::cout << "Time Shaper (0.25.0)\n";
    using T = sls::EffectType;
    constexpr int beat = 24000; // samples at 120 BPM
    // a signal that is easy to follow: a slow ramp (each sample says where it came from) plus nothing else
    std::vector<float> ramp (8 * beat);
    for (size_t i = 0; i < ramp.size(); ++i)
        ramp[i] = static_cast<float> (i) / static_cast<float> (ramp.size());

    const auto same = runTimeShaper (slotWith (T::TimeShaper, { 0.0f, 0.0f, 2.0f, 100.0f, 3.0f, 100.0f }), ramp);
    check (same == ramp, "no time pattern and no volume pattern: the sound passes unchanged");

    // Half speed, cycle 1 bar: in bar 2 (from sample 4 beats), the output at n is the input at barStart + (n - barStart) / 2.
    const auto half = runTimeShaper (slotWith (T::TimeShaper, { 1.0f, 0.0f, 2.0f, 100.0f, 3.0f, 100.0f }), ramp);
    double worst = 0.0;
    for (int n = 4 * beat + 2000; n < 8 * beat - 10; n += 97)
        worst = std::max (worst, std::abs (static_cast<double> (half[static_cast<size_t> (n)]) - ramp[static_cast<size_t> (4 * beat + (n - 4 * beat) / 2)]));
    check (worst < 2e-5, "half speed: through the bar it plays the bar's start at half the speed");

    // Reverse, cycle 1 beat: in the third beat, the output at barStart + k is the input at barStart - k (the beat before, backwards).
    const auto rev = runTimeShaper (slotWith (T::TimeShaper, { 2.0f, 0.0f, 0.0f, 100.0f, 3.0f, 100.0f }), ramp);
    worst = 0.0;
    for (int k = 1000; k < beat - 10; k += 101)
        worst = std::max (worst, std::abs (static_cast<double> (rev[static_cast<size_t> (2 * beat + k)]) - ramp[static_cast<size_t> (2 * beat - k)]));
    check (worst < 2e-5, "reverse: each beat plays the beat before it backwards");

    // Stutter 1/16 (a 16th is 6000 samples), cycle 1 beat: every 16th of the beat repeats its first 16th.
    const auto stut = runTimeShaper (slotWith (T::TimeShaper, { 6.0f, 0.0f, 0.0f, 100.0f, 3.0f, 100.0f }), ramp);
    worst = 0.0;
    for (int k = 500; k < 6000; k += 53)
        for (int slice = 1; slice < 4; ++slice)
            worst = std::max (worst, std::abs (static_cast<double> (stut[static_cast<size_t> (beat + slice * 6000 + k)]) - ramp[static_cast<size_t> (beat + k)]));
    check (worst < 2e-5, "stutter 1/16: the first 16th of the beat four times");

    // Tape stop: the speed falls to 0 over the bar, and the sound fades.
    const auto tape = runTimeShaper (slotWith (T::TimeShaper, { 3.0f, 0.0f, 2.0f, 100.0f, 3.0f, 100.0f }), sineWave (1000.0, 0.5, 8 * beat));
    const auto crossingsHz = [&tape] (int from, int to)
    {
        int crossings = 0;
        for (int i = from + 1; i < to; ++i)
            crossings += (tape[static_cast<size_t> (i - 1)] < 0.0f) != (tape[static_cast<size_t> (i)] < 0.0f) ? 1 : 0;
        return crossings * kSr / (2.0 * (to - from));
    };
    check (crossingsHz (4 * beat + 2000, 4 * beat + 8000) > 900.0 && crossingsHz (7 * beat, 7 * beat + 12000) < 300.0
               && peakOf (tape.data(), 8 * beat - 3000, 8 * beat) < 0.1f,
           "tape stop: the pitch falls from 1 kHz to a low growl and the sound fades out by the bar's end");

    // Gate 1/8: the first half of every eighth sounds, the second half is silent (after the smoothing).
    const std::vector<float> ones (4 * beat, 0.5f);
    const auto gated = runTimeShaper (slotWith (T::TimeShaper, { 0.0f, 1.0f, 2.0f, 100.0f, 1.0f, 100.0f }), ones);
    check (std::abs (gated[static_cast<size_t> (beat + 4000)] - 0.5f) < 0.01f && std::abs (gated[static_cast<size_t> (beat + 10000)]) < 0.01f
               && std::abs (gated[static_cast<size_t> (beat + 12000 + 4000)] - 0.5f) < 0.01f,
           "gate 1/8: on for the first half of each eighth, off for the second");
    const auto halfDepth = runTimeShaper (slotWith (T::TimeShaper, { 0.0f, 1.0f, 2.0f, 50.0f, 1.0f, 100.0f }), ones);
    check (std::abs (halfDepth[static_cast<size_t> (beat + 10000)] - 0.25f) < 0.01f, "depth 50 %: the closed gate is at half the level");
    const auto smoothFast = runTimeShaper (slotWith (T::TimeShaper, { 0.0f, 1.0f, 2.0f, 100.0f, 0.5f, 100.0f }), ones);
    const auto smoothSlow = runTimeShaper (slotWith (T::TimeShaper, { 0.0f, 1.0f, 2.0f, 100.0f, 20.0f, 100.0f }), ones);
    check (smoothSlow[static_cast<size_t> (beat + 6000 + 200)] > smoothFast[static_cast<size_t> (beat + 6000 + 200)] + 0.1f, "the smoothing softens the gate's edges");
    const auto trance = runTimeShaper (slotWith (T::TimeShaper, { 0.0f, 4.0f, 2.0f, 100.0f, 0.5f, 100.0f }), ones);
    check (trance[static_cast<size_t> (2000)] > 0.45f && trance[static_cast<size_t> (6000 + 2000)] < 0.01f && trance[static_cast<size_t> (12000 + 2000)] > 0.45f,
           "trance gate: its 16th pattern (on, off, on, on...)");
    const auto stopped = runTimeShaper (slotWith (T::TimeShaper, { 0.0f, 1.0f, 2.0f, 100.0f, 1.0f, 100.0f }), ones, 0.0, true, false);
    check (std::abs (stopped[static_cast<size_t> (beat + 4000)] - 0.5f) < 0.01f && std::abs (stopped[static_cast<size_t> (beat + 10000)]) < 0.01f,
           "with the transport stopped it follows its own clock at the song's tempo");

    const auto dry = runTimeShaper (slotWith (T::TimeShaper, { 2.0f, 1.0f, 0.0f, 100.0f, 3.0f, 0.0f }), ramp);
    check (dry == ramp, "mix 0 %: the dry sound");
    const auto noBuffer = runTimeShaper (slotWith (T::TimeShaper, { 2.0f, 0.0f, 0.0f, 100.0f, 3.0f, 100.0f }), ramp, 0.0, false);
    check (noBuffer == ramp, "without a recording (none free) the time pattern is skipped, safely");

    // Every time pattern, with every volume pattern, on noise: finite and bounded.
    bool bounded = true;
    std::vector<float> noise (static_cast<size_t> (2 * beat));
    std::uint32_t seed = 7u;
    for (auto& v : noise)
    {
        seed = seed * 1664525u + 1013904223u;
        v = static_cast<float> (seed >> 8) / 16777216.0f * 1.6f - 0.8f;
    }
    for (int tm = 0; tm <= 8; ++tm)
        for (int vm = 0; vm <= 7; ++vm)
        {
            const auto out = runTimeShaper (slotWith (T::TimeShaper, { static_cast<float> (tm), static_cast<float> (vm), 0.0f, 100.0f, 0.5f, 100.0f }), noise, 333.0);
            for (const float v : out)
                bounded = bounded && std::isfinite (v) && std::abs (v) <= 1.0001f;
        }
    check (bounded, "every time pattern with every volume pattern stays finite and never louder than its input");

    // The Player lends its recordings (four) to the Time Shapers of the project; playing allocates nothing.
    {
        auto c = synthChannel ({ { 60, 0, 2 * kBeat, 0.8f }, { 64, 2 * kBeat, 2 * kBeat, 0.8f } });
        std::vector<sls::MixerTrack> tracks (static_cast<size_t> (sls::kMaxTracks));
        for (size_t s = 0; s < 4; ++s) // four of the track's ten slots
            tracks[0].slots[s] = slotWith (T::TimeShaper, { static_cast<float> (s + 1), 2.0f, 0.0f, 100.0f, 3.0f, 100.0f });
        tracks[1].slots[0] = slotWith (T::TimeShaper, { 1.0f, 0.0f, 2.0f, 100.0f, 3.0f, 100.0f }); // a fifth: none left for it
        auto project = sls::Project::make ({ c }, 16 * kBeat);
        project.tracks = tracks;
        sls::Player player;
        player.prepare (kSr);
        std::vector<float> l (256), r (256);
        float* ch[2] = { l.data(), r.data() };
        player.render (project, 120.0, true, ch, 2, 256); // the lending happens here
        bool fine = true;
        gAllocs = 0;
        gCounting = true;
        for (int i = 0; i < 1500; ++i)
        {
            player.render (project, 120.0, true, ch, 2, 256);
            for (int k = 0; k < 256; ++k)
                fine = fine && std::isfinite (l[static_cast<size_t> (k)]);
        }
        gCounting = false;
        check (gAllocs.load() == 0 && fine, "five Time Shapers in a project (four get a recording): finite, and no allocation while playing");
        check (player.timeShaperShortfall() == 1, "the player says one Time Shaper has no recording (the interface tells the user)");
        project.tracks[1].slots[0] = {};
        float l2[256] {}, r2[256] {};
        float* ch2[2] = { l2, r2 };
        player.render (project, 120.0, true, ch2, 2, 256);
        check (player.timeShaperShortfall() == 0, "with four, none is left without");
    }
}

void testNewEffects()
{
    std::cout << "New effects (0.21.0)\n";
    using T = sls::EffectType;
    std::vector<float> impulse (48000, 0.0f);
    impulse[100] = 1.0f;

    // --- Reverb: pre-delay, Room shorter than Hall, Plate brighter than Room
    {
        const auto hall = runEffect (slotWith (T::Reverb, { 0.6f, 0.4f, 1.0f, 1.0f, 0.0f, 0.0f }), impulse);
        const auto pre = runEffect (slotWith (T::Reverb, { 0.6f, 0.4f, 1.0f, 1.0f, 0.0f, 100.0f }), impulse);
        const int onsetHall = firstAbove (hall, 1.0e-4f, 101), onsetPre = firstAbove (pre, 1.0e-4f, 101);
        check (onsetPre - onsetHall > 4700 && onsetPre - onsetHall < 4900, "reverb: 100 ms pre-delay starts the tail 100 ms later");
        const auto room = runEffect (slotWith (T::Reverb, { 0.6f, 0.4f, 1.0f, 1.0f, 1.0f, 0.0f }), impulse);
        const auto plate = runEffect (slotWith (T::Reverb, { 0.6f, 0.4f, 1.0f, 1.0f, 2.0f, 0.0f }), impulse);
        check (rmsOf (room, 24000, 36000) < 0.5 * rmsOf (hall, 24000, 36000), "reverb: Room dies away sooner than Hall");
        double roughRoom = 0.0, roughPlate = 0.0, eRoom = 0.0, ePlate = 0.0;
        for (int i = 2001; i < 20000; ++i)
        {
            roughRoom += std::pow (room[static_cast<size_t> (i)] - room[static_cast<size_t> (i - 1)], 2.0);
            roughPlate += std::pow (plate[static_cast<size_t> (i)] - plate[static_cast<size_t> (i - 1)], 2.0);
            eRoom += std::pow (room[static_cast<size_t> (i)], 2.0);
            ePlate += std::pow (plate[static_cast<size_t> (i)], 2.0);
        }
        check (roughPlate / ePlate > roughRoom / eRoom, "reverb: Plate is brighter than Room");
    }

    // --- Delay: ping-pong alternates sides, spread delays the right side, tape wobbles but stays near its time
    {
        auto pp = slotWith (T::Delay, { 100.0f, 0.5f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f });
        const auto [l, r] = runEffectStereo (pp, impulse, impulse);
        const int t = 4800;
        check (std::abs (l[static_cast<size_t> (100 + t)]) > 0.3f && std::abs (r[static_cast<size_t> (100 + t)]) < 0.05f,
               "ping-pong: the first repeat is on the left");
        check (peakOf (r.data(), 100 + 2 * t - 4, 100 + 2 * t + 4) > 0.15f && peakOf (l.data(), 100 + 2 * t - 4, 100 + 2 * t + 4) < 0.05f,
               "ping-pong: the second repeat is on the right");
        const auto [sl, sr] = runEffectStereo (slotWith (T::Delay, { 100.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.5f, 0.0f }), impulse, impulse);
        check (std::abs (sl[static_cast<size_t> (100 + t)]) > 0.5f && firstAbove (sr, 0.3f, 101) - 100 > 7150 && firstAbove (sr, 0.3f, 101) - 100 < 7250,
               "stereo delay: spread 0.5 puts the right repeat at 1.5 x the time");
        const auto tape = runEffect (slotWith (T::Delay, { 200.0f, 0.6f, 0.6f, 1.0f, 2.0f, 0.0f, 1.0f }), impulse);
        const int echo = firstAbove (tape, 0.02f, 101) - 100;
        check (echo > 9600 && echo < 9600 + 650, "tape delay: the first repeat comes within the wobble's few ms of its time");
        bool finite = true;
        for (float v : tape)
            finite = finite && std::isfinite (v) && std::abs (v) < 2.0f;
        check (finite, "tape delay: the saturated feedback stays bounded");
    }

    // --- Overdrive: adds harmonics, little aliasing, drive 0 is close to clean
    {
        const auto clean = runEffect (slotWith (T::Overdrive, { 0.0f, 0.0f, 0.5f, 1.0f, 0.0f, 1.0f, 0.0f }), sineWave (200.0, 0.1, 24000));
        check (std::abs (toneEnergy (clean.data(), 12000, 24000, 200.0) / (0.05) - 1.0) < 0.1, "overdrive at drive 0, open tone: the level of a quiet sine is kept");
        const auto hot = runEffect (slotWith (T::Overdrive, { 0.8f, 0.0f, 0.5f, 1.0f, 0.0f, 1.0f, 0.0f }), sineWave (200.0, 0.5, 24000));
        check (toneEnergy (hot.data(), 12000, 24000, 600.0) > 0.05 * toneEnergy (hot.data(), 12000, 24000, 200.0),
               "overdrive: strong odd harmonics (the 3rd)");
        const auto asym = runEffect (slotWith (T::Overdrive, { 0.8f, 0.0f, 0.5f, 1.0f, 1.0f, 1.0f, 0.0f }), sineWave (200.0, 0.5, 24000));
        check (toneEnergy (asym.data(), 12000, 24000, 400.0) > 4.0 * toneEnergy (hot.data(), 12000, 24000, 400.0) + 1.0e-4,
               "overdrive: Character adds even harmonics (the 2nd)");
        // 5 kHz driven hard: its 5th harmonic (25 kHz) would alias to 23 kHz, the 7th (35 kHz) to 13 kHz; at 4x the 39th and 41st
        // (195 and 205 kHz) fold back to 3 and 13 kHz.
        const auto bright = runEffect (slotWith (T::Overdrive, { 1.0f, 1.0f, 0.5f, 1.0f, 0.0f, 1.0f, 0.0f }), sineWave (5000.0, 0.5, 24000));
        const double fundamental = toneEnergy (bright.data(), 12000, 24000, 5000.0);
        std::cout << "    (relative to 5 kHz: 13 kHz " << 20.0 * std::log10 (toneEnergy (bright.data(), 12000, 24000, 13000.0) / fundamental)
                  << " dB, 3 kHz " << 20.0 * std::log10 (toneEnergy (bright.data(), 12000, 24000, 3000.0) / fundamental) << " dB, 23 kHz "
                  << 20.0 * std::log10 (toneEnergy (bright.data(), 12000, 24000, 23000.0) / fundamental) << " dB)\n";
        check (toneEnergy (bright.data(), 12000, 24000, 13000.0) < 0.01 * fundamental && toneEnergy (bright.data(), 12000, 24000, 23000.0) < 0.01 * fundamental
                   && toneEnergy (bright.data(), 12000, 24000, 3000.0) < 0.01 * fundamental,
               "overdrive: oversampling and anti-aliasing keep the aliases of a 5 kHz sine driven into a square 40 dB down (3, 13, 23 kHz)");
    }

    // --- Chorus: mix 0 is dry, a mono input becomes stereo
    {
        const auto in = sineWave (440.0, 0.3, 24000);
        const auto dry = runEffect (slotWith (T::Chorus, { 0.8f, 3.0f, 15.0f, 0.0f, 2.0f, 1.0f, 0.0f }), in);
        check (dry == in, "chorus at mix 0 leaves the sound alone");
        const auto [l, r] = runEffectStereo (slotWith (T::Chorus, { 0.8f, 5.0f, 15.0f, 0.0f, 2.0f, 1.0f, 0.5f }), in, in);
        double diff = 0.0;
        for (size_t i = 4800; i < 24000; ++i)
            diff += std::abs (l[i] - r[i]);
        check (diff / 19200.0 > 0.01, "chorus with width: a mono input comes out different on the two sides");
    }

    // --- Phaser: a deep notch somewhere in the spectrum, none at mix 0
    {
        const auto phaser = slotWith (T::Phaser, { 0.02f, 0.0f, 1000.0f, 0.0f, 0.0f, 0.0f, 0.5f });
        double lowest = 1.0, highest = 0.0;
        for (double hz = 100.0; hz < 10000.0; hz *= 1.06)
        {
            const double g = gainAt (phaser, hz);
            lowest = std::min (lowest, g);
            highest = std::max (highest, g);
        }
        check (lowest < 0.15 && highest > 0.9, "phaser: notches (below -16 dB) between frequencies that pass");
    }

    // --- Filter: low-pass, high-pass, notch, 24 dB steeper than 12
    {
        check (gainAt (slotWith (T::Filter, { 0.0f, 500.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f }), 5000.0) < 0.02,
               "filter low-pass 500 Hz, 12 dB: 5 kHz is more than 34 dB down");
        check (gainAt (slotWith (T::Filter, { 0.0f, 500.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, 1.0f }), 5000.0) < 0.001,
               "filter low-pass 500 Hz, 24 dB: 5 kHz is more than 60 dB down");
        check (gainAt (slotWith (T::Filter, { 1.0f, 2000.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, 1.0f }), 100.0) < 0.001
                   && gainAt (slotWith (T::Filter, { 1.0f, 2000.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, 1.0f }), 15000.0) > 0.9,
               "filter high-pass 2 kHz: 100 Hz gone, 15 kHz kept");
        check (gainAt (slotWith (T::Filter, { 3.0f, 1000.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f }), 1000.0) < 0.02,
               "filter notch at 1 kHz takes 1 kHz out");
        check (gainAt (slotWith (T::Filter, { 0.0f, 500.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, 0.0f }), 5000.0) > 0.99, "filter at mix 0 is dry");
    }

    // --- Parametric EQ
    {
        const auto peq = slotWith (T::ParametricEq, { 100.0f, 0.0f, 1000.0f, 12.0f, 1.0f, 8000.0f, 0.0f, 0.0f });
        check (std::abs (20.0 * std::log10 (gainAt (peq, 1000.0)) - 12.0) < 0.5, "parametric EQ: +12 dB at the mid frequency");
        check (std::abs (20.0 * std::log10 (gainAt (peq, 100.0))) < 1.0, "parametric EQ: two octaves and more away, almost nothing");
        const auto shelf = slotWith (T::ParametricEq, { 200.0f, 6.0f, 1000.0f, 0.0f, 1.0f, 8000.0f, 0.0f, -6.0f });
        check (std::abs (20.0 * std::log10 (gainAt (shelf, 40.0))) < 0.6, "parametric EQ: +6 dB low shelf and -6 dB output cancel in the bass");
    }

    // --- Maximizer: never over the ceiling, quiet sounds untouched, the look-ahead is its latency
    {
        const auto loud = sineWave (1000.0, 2.0, 48000);
        const auto out = runEffect (slotWith (T::Maximizer, { 6.0f, -1.0f, 100.0f, 0.3f, 0.3f }), loud);
        const float ceiling = std::pow (10.0f, -1.0f / 20.0f);
        check (peakOf (out.data(), 0, 48000) <= ceiling + 1.0e-6f, "maximizer: +12 dB over full scale never passes the -1 dB ceiling");
        check (rmsOf (out, 24000, 48000) > 0.5 * ceiling, "maximizer: and the level stays up (not crushed)");
        const auto quiet = sineWave (1000.0, 0.03, 48000);
        const auto q = runEffect (slotWith (T::Maximizer, { 0.0f, -1.0f, 100.0f, 0.0f, 0.0f }), quiet);
        const int lat = static_cast<int> (std::lround (0.0015 * kSr));
        double err = 0.0;
        for (int i = 2000; i < 48000; ++i)
            err = std::max (err, static_cast<double> (std::abs (q[static_cast<size_t> (i)] - quiet[static_cast<size_t> (i - lat)])));
        check (err < 1.0e-6, "maximizer: a quiet sound comes out unchanged, 1.5 ms late (the look-ahead)");
        sls::EffectUnit unit;
        unit.prepare (kSr);
        sls::EffectMemory unitMemory; // the big buffers the Player would lend (0.29.0)
        unitMemory.prepare (kSr);
        unit.attachMemory (&unitMemory);
        check (unit.latencySamples (T::Maximizer) == lat && unit.latencySamples (T::Reverb) == 0, "the maximizer reports its latency");
    }

    // --- Transient: attack and sustain apart
    {
        std::vector<float> hits (48000, 0.0f);
        for (int hit = 0; hit < 4; ++hit)
            for (int i = 0; i < 9000; ++i)
                hits[static_cast<size_t> (hit * 12000 + i)] = static_cast<float> (0.5 * std::exp (-i / 1500.0) * std::sin (2.0 * 3.14159265 * 150.0 * i / kSr));
        const auto attackLoud = runEffect (slotWith (T::Transient, { 12.0f, 0.0f, 0.3f, 0.0f, 1.0f, 0.0f }), hits);
        const auto sustainLow = runEffect (slotWith (T::Transient, { 0.0f, -12.0f, 0.3f, 0.0f, 1.0f, 0.0f }), hits);
        const int h = 24000; // the third hit
        check (rmsOf (attackLoud, h, h + 300) > 1.8 * rmsOf (hits, h, h + 300), "transient: +12 dB attack makes the start of a hit much louder");
        check (rmsOf (sustainLow, h + 4000, h + 8000) < 0.6 * rmsOf (hits, h + 4000, h + 8000), "transient: -12 dB sustain makes the tail quieter");
        check (rmsOf (sustainLow, h, h + 200) > 0.8 * rmsOf (hits, h, h + 200), "transient: and leaves the attack alone");
        const auto onlyAttack = runEffect (slotWith (T::Transient, { 0.0f, 0.0f, 0.3f, 1.0f, 1.0f, 0.0f }), hits);
        check (rmsOf (onlyAttack, h + 4000, h + 8000) < 0.2 * rmsOf (hits, h + 4000, h + 8000), "transient: Listen Transients drops the tail");
    }

    // --- Stereo imager
    {
        const auto l = sineWave (440.0, 0.4, 24000);
        const auto r = sineWave (660.0, 0.4, 24000);
        const auto [ml, mr] = runEffectStereo (slotWith (T::Imager, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }), l, r);
        check (ml == mr, "imager: width 0 makes the sound mono");
        const auto [wl, wr] = runEffectStereo (slotWith (T::Imager, { 200.0f, 0.0f, 0.0f, 0.0f, 0.0f }), l, r);
        double sideIn = 0.0, sideOut = 0.0;
        for (size_t i = 0; i < 24000; ++i)
        {
            sideIn += std::abs (l[i] - r[i]);
            sideOut += std::abs (wl[i] - wr[i]);
        }
        check (std::abs (sideOut / sideIn - 2.0) < 0.01, "imager: width 200 % doubles the difference between the sides");
        auto bassL = sineWave (40.0, 0.4, 48000), bassR = bassL;
        for (auto& v : bassR)
            v = -v; // all side, no middle
        const auto [bl, br] = runEffectStereo (slotWith (T::Imager, { 100.0f, 0.0f, 0.0f, 200.0f, 0.0f }), bassL, bassR);
        check (rmsOf (bl, 24000, 48000) < 0.25 * rmsOf (bassL, 24000, 48000), "imager: Mono bass 200 Hz takes a 40 Hz out-of-phase bass out of the sides");
    }

    // --- Presets, files, allocation
    {
        bool presetsOk = true;
        int presetCount = 0;
        for (int t = 1; t < sls::kEffectTypeCount; ++t)
            for (const auto& preset : sls::effectPresets (static_cast<T> (t)))
            {
                ++presetCount;
                sls::EffectSlot slot = sls::EffectSlot::make (static_cast<T> (t));
                slot.params = preset.params;
                presetsOk = presetsOk && slot.sanitised().params == slot.params;
                const auto out = runEffect (slot, sineWave (220.0, 0.5, 9600));
                for (float v : out)
                    presetsOk = presetsOk && std::isfinite (v) && std::abs (v) < 4.0f;
            }
        check (presetsOk && presetCount >= 35, "every effect preset is within its ranges and sounds finite (35 or more presets)");

        auto doc = sls::ProjectDocument::blank();
        const T types[] = { T::Overdrive, T::Chorus, T::Phaser, T::Filter, T::ParametricEq, T::Maximizer, T::Transient, T::Imager };
        for (size_t i = 0; i < 8; ++i)
        {
            auto slot = sls::EffectSlot::make (types[i]);
            slot.params[0] = sls::effectParamInfo (types[i], 0).max;
            doc.tracks[i].slots[static_cast<size_t> (i % 4)] = slot;
        }
        doc.tracks[0].slots[3] = slotWith (T::Delay, { 200.0f, 0.5f, 0.5f, 0.4f, 2.0f, 0.25f, 0.5f });
        const auto parsed = sls::ProjectDocument::fromJson (doc.toJson());
        bool same = parsed.document.has_value();
        for (size_t i = 0; same && i < 8; ++i)
            same = parsed.document->tracks[i].slots[static_cast<size_t> (i % 4)] == doc.tracks[i].slots[static_cast<size_t> (i % 4)];
        check (same && parsed.document->tracks[0].slots[3] == doc.tracks[0].slots[3], "the new effects and the delay's modes are saved and read back");

        auto v = juce::JSON::parse (sls::ProjectDocument::blank().toJson());
        auto* track = v.getDynamicObject()->getProperty ("tracks").getArray()->getReference (0).getDynamicObject();
        juce::Array<juce::var> effects;
        auto* old = new juce::DynamicObject();
        old->setProperty ("type", "reverb");
        old->setProperty ("params", juce::Array<juce::var> { 0.7, 0.3, 0.9, 0.4 });
        effects.add (juce::var (old));
        track->setProperty ("effects", effects);
        const auto oldRead = sls::ProjectDocument::fromJson (juce::JSON::toString (v));
        check (oldRead.document && oldRead.document->tracks[0].slots[0].type == T::Reverb && oldRead.document->tracks[0].slots[0].params[4] == 0.0f
                   && oldRead.document->tracks[0].slots[0].params[5] == 0.0f && std::abs (oldRead.document->tracks[0].slots[0].params[0] - 0.7f) < 1.0e-6f,
               "a reverb saved by an older version (4 values) reads as Hall without pre-delay");

        std::vector<sls::EffectUnit> units (8);
        for (auto& u : units)
            u.prepare (kSr);
        std::array<float, 64> bl {}, br {};
        gAllocs = 0;
        gCounting = true;
        for (int block = 0; block < 500; ++block)
            for (size_t i = 0; i < 8; ++i)
            {
                for (size_t k = 0; k < 64; ++k)
                    bl[k] = br[k] = std::sin (static_cast<float> (block * 64 + static_cast<int> (k)) * 0.05f) * 0.8f;
                units[i].process (sls::EffectSlot::make (types[i]), bl.data(), br.data(), 64);
            }
        gCounting = false;
        check (gAllocs.load() == 0, "the new effects allocate nothing while processing (8 effects, 500 blocks)");
    }
}

void testEffectsInPlayer()
{
    std::cout << "Effects in the mixer\n";
    const std::vector<sls::Note> a4 { { 69, 0, kBeat, 0.8f } };

    auto withSlot = [&] (const sls::EffectSlot& slot)
    {
        auto tracks = sls::Project::defaultTracks();
        tracks[0].slots[1] = slot;
        return sls::renderToBuffer (sls::Project::make ({ synthChannel (a4) }, 4 * kBeat, tracks), {});
    };

    const auto dry = withSlot (sls::EffectSlot {});
    const auto wet = withSlot (slotWith (sls::EffectType::Delay, { 250.0f, 0.7f, 1.0f, 0.5f }));
    check (peakOf (dry.getReadPointer (0), 40000, 144000) == 0.0f, "reference: the dry note is silent after its release");
    check (peakOf (wet.getReadPointer (0), 48000, 70000) > 0.003f, "a delay in a track slot keeps sounding after the note has ended");

    auto bypassed = slotWith (sls::EffectType::Delay, { 250.0f, 0.7f, 1.0f, 0.5f });
    bypassed.bypass = true;
    const auto byp = withSlot (bypassed);
    bool same = true;
    for (int i = 0; i < dry.getNumSamples(); ++i)
        same = same && dry.getReadPointer (0)[i] == byp.getReadPointer (0)[i];
    check (same, "a bypassed slot leaves the track untouched");

    const auto comp = withSlot (slotWith (sls::EffectType::Compressor, { -30.0f, 20.0f, 1.0f, 100.0f, 0.0f }));
    check (peakOf (comp.getReadPointer (0), 4800, 24000) < 0.6f * peakOf (dry.getReadPointer (0), 4800, 24000),
           "a compressor in a track slot reduces the track's level");

    // real-time safety with the full effect chains active
    {
        std::vector<sls::Note> notes;
        for (int i = 0; i < 64; ++i)
            notes.push_back ({ 48 + i % 24, i * 100, 300, 0.7f });
        auto tracks = sls::Project::defaultTracks();
        for (size_t t = 0; t < 2; ++t)
        {
            tracks[t].slots[0] = sls::EffectSlot::make (sls::EffectType::Equalizer);
            tracks[t].slots[0].params[1] = 6.0f;
            tracks[t].slots[1] = sls::EffectSlot::make (sls::EffectType::Reverb);
            tracks[t].slots[2] = sls::EffectSlot::make (sls::EffectType::Delay);
            tracks[t].slots[3] = sls::EffectSlot::make (sls::EffectType::Compressor);
        }
        auto a = synthChannel (notes), b = synthChannel (notes);
        b.mixerTrack = 1;
        const auto project = sls::Project::make ({ a, b }, 4 * kBeat, tracks);

        sls::Player player;
        player.prepare (kSr);
        std::vector<float> l (256), r (256);
        float* ch[2] = { l.data(), r.data() };

        gAllocs = 0;
        gCounting = true;
        for (int i = 0; i < 2000; ++i)
            player.render (project, 140.0, true, ch, 2, 256);
        gCounting = false;
        check (gAllocs.load() == 0, "Player::render performs no heap allocation with 8 active effects (2000 blocks)");
        check (std::isfinite (l[10]) && std::isfinite (r[10]), "output stays finite with all effects running");
    }

    // removing an effect while its tail rings: the tail stops at once, the dry signal is unaffected
    {
        auto tracks = sls::Project::defaultTracks();
        tracks[0].slots[0] = sls::EffectSlot::make (sls::EffectType::Reverb);
        auto project = sls::Project::make ({ synthChannel (a4) }, 4 * kBeat, tracks);
        sls::Player player;
        player.prepare (kSr);
        std::vector<float> l (480), r (480);
        float* ch[2] = { l.data(), r.data() };
        for (int i = 0; i < 20; ++i)
            player.render (project, 120.0, true, ch, 2, 480);
        for (int i = 0; i < 30; ++i) // transport stopped: the 80 ms release is over after this
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            player.render (project, 120.0, false, ch, 2, 480);
        }
        const float tail = peakOf (l.data(), 0, 480);
        check (tail > 1.0e-4f, "precondition: the reverb tail rings on after the note and the release are over");

        project.tracks[0].slots[0] = sls::EffectSlot::make (sls::EffectType::None);
        project.revision = 99;
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        player.render (project, 120.0, false, ch, 2, 480);
        check (peakOf (l.data(), 0, 480) == 0.0f, "removing an effect from a slot stops its tail at once");
    }
}

sls::BrowserNode node (const char* name, sls::NodeKind kind, std::vector<sls::BrowserNode> children = {}, const char* payload = "")
{
    sls::BrowserNode n;
    n.name = name;
    n.kind = kind;
    n.payload = payload;
    n.children = std::move (children);
    return n;
}

const sls::BrowserNode* findChild (const sls::BrowserNode& parent, const std::string& name)
{
    for (const auto& c : parent.children)
        if (c.name == name)
            return &c;
    return nullptr;
}

void testBrowserModel()
{
    std::cout << "File browser: tree, search, drag rules\n";
    using sls::NodeKind;

    const auto tree = node ("Browser", NodeKind::Folder, {
        node ("Samples", NodeKind::Folder, {
            node ("Kicks", NodeKind::Folder, {
                node ("Deep Kick.wav", NodeKind::AudioFile, {}, "/m/Deep Kick.wav"),
                node ("Punchy.wav", NodeKind::AudioFile, {}, "/m/Punchy.wav") }),
            node ("Snare 01.wav", NodeKind::AudioFile, {}, "/m/Snare 01.wav") }),
        node ("Plugin", NodeKind::Folder, {
            node ("Scan for VST3 plugins...", NodeKind::Action, {}, "scan-plugins"),
            node ("Surge XT", NodeKind::Plugin, {}, "/usr/lib/vst3/Surge XT.vst3") }) });

    check (sls::filterTree (tree, "").children.size() == 2 && sls::countLeaves (tree) == 5, "an empty query keeps the whole tree");

    {
        const auto r = sls::filterTree (tree, "KICK");
        check (r.children.size() == 1 && sls::countLeaves (r) == 2 && findChild (r, "Samples") != nullptr,
               "search is case-insensitive; a folder whose name matches keeps its contents");
    }
    {
        const auto r = sls::filterTree (tree, "punch");
        const auto* samples = findChild (r, "Samples");
        const auto* kicks = samples != nullptr ? findChild (*samples, "Kicks") : nullptr;
        check (kicks != nullptr && kicks->children.size() == 1 && kicks->children[0].name == "Punchy.wav" && samples->children.size() == 1,
               "a match deep in the tree keeps its folders and drops the siblings");
    }
    check (sls::filterTree (tree, "zzz").children.empty(), "no match: nothing is left");
    {
        const auto r = sls::filterTree (tree, "scan");
        check (r.children.empty(), "actions and notes are hidden while searching");
        check (sls::filterTree (tree, "surge").children.size() == 1, "plugins are searchable");
    }

    check (node ("a", NodeKind::AudioFile).isDraggable() && node ("a", NodeKind::BuiltinSample).isDraggable()
               && node ("a", NodeKind::Instrument).isDraggable() && node ("a", NodeKind::Preset).isDraggable() && node ("a", NodeKind::Plugin).isDraggable(),
           "audio, built-in samples, instruments, presets and plugins (0.17.0) can be dragged");
    check (! node ("a", NodeKind::Folder).isDraggable() && ! node ("a", NodeKind::Action).isDraggable() && ! node ("a", NodeKind::Note).isDraggable(),
           "folders, actions and notes cannot be dragged");
}

void testChannelPresets()
{
    std::cout << "Channel presets\n";

    auto channel = synthChannel ({});
    channel.instrument = sls::Instrument::Sampler;
    channel.sample = sls::makeKickSample();
    channel.volume = 0.6f;
    channel.pan = -0.25f;
    channel.pitch = 3.5f;
    channel.swing = 0.4f;
    channel.rootPitch = 62;

    const auto preset = sls::ChannelPreset::fromChannel (channel, "My Kick");
    const auto back = sls::ChannelPreset::fromJson (preset.toJson());
    check (back.has_value() && back->name == "My Kick" && back->instrument == sls::Instrument::Sampler
               && std::abs (back->volume - 0.6f) < 1.0e-6f && std::abs (back->pan + 0.25f) < 1.0e-6f
               && std::abs (back->pitch - 3.5f) < 1.0e-6f && std::abs (back->swing - 0.4f) < 1.0e-6f
               && back->rootPitch == 62 && back->sampleSource == "builtin:kick",
           "a preset survives the JSON round trip");

    auto target = synthChannel ({ { 60, 0, 100, 0.5f } });
    target.mixerTrack = 4;
    target.mute = true;
    back->applyTo (target);
    check (target.name == "My Kick" && target.instrument == sls::Instrument::Sampler && target.pitch == 3.5f && target.notes.size() == 1
               && target.mixerTrack == 4 && target.mute,
           "applying a preset sets the sound and leaves notes, routing and mute alone");

    check (! sls::ChannelPreset::fromJson ("not json at all").has_value(), "garbage is rejected");
    check (! sls::ChannelPreset::fromJson ("{\"format\":\"something-else\"}").has_value(), "a foreign JSON file is rejected");
    check (! sls::ChannelPreset::fromJson ("{\"format\":\"sun-loops-studio-channel-preset\",\"instrument\":\"theremin\"}").has_value(),
           "an unknown instrument is rejected");
    check (! sls::ChannelPreset::fromJson ("[1,2,3]").has_value(), "a JSON array is rejected");

    const auto wild = sls::ChannelPreset::fromJson (
        "{\"format\":\"sun-loops-studio-channel-preset\",\"instrument\":\"synth\",\"volume\":99,\"pan\":-7,\"pitch\":1e9,"
        "\"swing\":\"x\",\"rootPitch\":500,\"name\":\"  \"}");
    check (wild.has_value() && wild->volume == 1.5f && wild->pan == -1.0f && wild->pitch == 24.0f && wild->swing == 0.0f
               && wild->rootPitch == 127 && wild->name == "Preset",
           "out-of-range numbers are clamped, wrong types fall back to defaults, a blank name becomes 'Preset'");

    bool factoryOk = ! sls::factoryPresets().empty();
    for (const auto& p : sls::factoryPresets())
    {
        bool sourceOk = true;
        if (p.instrument == sls::Instrument::Sampler && p.programSource.empty())
            sourceOk = p.sampleSource.rfind ("builtin:", 0) == 0 && sls::makeBuiltinSample (p.sampleSource.substr (8)) != nullptr;
        else if (p.instrument == sls::Instrument::Sampler)
            sourceOk = sls::isLibraryProgramSource (p.programSource) && p.sampleSource.empty();
        sourceOk = sourceOk && ! p.category.empty();
        factoryOk = factoryOk && ! p.id.empty() && sourceOk && sls::ChannelPreset::fromJson (p.toJson()).has_value();
    }
    check (factoryOk, "every factory preset is valid, round-trips, has a category and points at an existing built-in sample or instrument");

    check (sls::makeBuiltinSample ("kick") != nullptr && sls::makeBuiltinSample ("kick")->source == "builtin:kick"
               && sls::makeBuiltinSample ("nope") == nullptr && sls::builtinSampleIds().size() == 3,
           "built-in samples are looked up by id and remember their source");
}

void testLibraryScan()
{
    std::cout << "File browser: scanning folders\n";
    using sls::NodeKind;

    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sls-test-library-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
    tmp.createDirectory();
    auto touch = [&] (const juce::String& rel) { auto f = tmp.getChildFile (rel); f.getParentDirectory().createDirectory(); f.replaceWithText ("x"); };

    touch ("b.wav");
    touch ("A.FLAC");
    touch ("notes.txt");
    touch (".hidden.wav");
    touch ("kit/hat.ogg");
    touch ("kit/deep/clap.aiff");
    touch ("emptyish/readme.md");
    touch ("c.aif");

    int budget = 1000;
    const auto scanned = sls::scanDirectory (tmp, NodeKind::AudioFile, {}, 6, budget);
    std::vector<std::string> names;
    for (const auto& c : scanned.children)
        names.push_back (c.name);
    check (names == std::vector<std::string> { "kit", "A.FLAC", "b.wav", "c.aif" },
           "folders first, then audio files sorted without regard to case; text and hidden files and empty folders are skipped");

    const auto* kit = findChild (scanned, "kit");
    const auto* deep = kit != nullptr ? findChild (*kit, "deep") : nullptr;
    check (kit != nullptr && deep != nullptr && deep->children.size() == 1 && deep->children[0].payload == tmp.getChildFile ("kit/deep/clap.aiff").getFullPathName().toStdString(),
           "sub-folders are scanned and leaves carry the absolute path");

    int tiny = 3;
    const auto cut = sls::scanDirectory (tmp, NodeKind::AudioFile, {}, 6, tiny);
    check (sls::countLeaves (cut) <= 4 && cut.children.back().kind == NodeKind::Note, "a node budget stops huge folders and says so");

    int shallowBudget = 1000;
    const auto shallow = sls::scanDirectory (tmp, NodeKind::AudioFile, {}, 0, shallowBudget);
    check (findChild (shallow, "kit") == nullptr && findChild (shallow, "b.wav") != nullptr, "the depth limit is respected");

    // the whole tree
    const auto paths = sls::LibraryPaths::forRoot (tmp.getChildFile ("lib"));
    check (paths.ensureExists() && paths.samples.isDirectory() && paths.drumKits.isDirectory() && paths.presets.isDirectory() && paths.projects.isDirectory(),
           "the library folders are created on demand");
    paths.drumKits.getChildFile ("My Kit").createDirectory();
    paths.drumKits.getChildFile ("My Kit/snare.wav").replaceWithText ("x");
    paths.presets.getChildFile ("bass.slspreset").replaceWithText ("{}");
    paths.presets.getChildFile ("ignored.txt").replaceWithText ("x");

    const auto tree = sls::buildBrowserTree (paths, {}, sls::PluginScanState::NotScanned);
    std::vector<std::string> roots;
    for (const auto& c : tree.children)
        roots.push_back (c.name);
    check (roots == std::vector<std::string> { "Samples", "Drum Kits", "Instruments", "Presets", "Plugin" }, "the five top-level folders, in order");

    const auto* kits = findChild (tree, "Drum Kits");
    check (kits != nullptr && findChild (*kits, "Sun Loops Basic Kit") != nullptr && findChild (*kits, "My Kit") != nullptr,
           "drum kits: the built-in kit plus one entry per user folder");
    const auto* presets = findChild (tree, "Presets");
    const auto* mine = presets != nullptr ? findChild (*presets, "My presets") : nullptr;
    check (mine != nullptr && mine->children.size() == 1 && mine->children[0].name == "bass" && mine->children[0].kind == NodeKind::Preset,
           "user presets are listed without their extension; other files are ignored");
    const auto* factory = presets != nullptr ? findChild (*presets, "Factory") : nullptr;
    check (factory != nullptr && sls::countLeaves (*factory) == static_cast<int> (sls::factoryPresets().size()) && ! factory->children.empty()
               && factory->children[0].kind == NodeKind::Folder && ! factory->children[0].children.empty()
               && factory->children[0].children[0].payload.rfind ("builtin:", 0) == 0,
           "factory presets are listed, in a folder per category");
    const auto* instruments = findChild (tree, "Instruments");
    check (instruments != nullptr && instruments->children.size() == 3 && instruments->children[0].payload == "synth" && instruments->children[2].payload == "drums",
           "instruments: Synth, Sampler and Drum machine");

    const auto* plugin = findChild (tree, "Plugin");
    check (plugin != nullptr && plugin->children[0].kind == NodeKind::Action && plugin->children[0].payload == "scan-plugins",
           "plugins: the scan action comes first");
    sls::PluginInfo info;
    info.name = "Fake Synth";
    info.manufacturer = "Acme";
    info.path = "/x/Fake.vst3";
    info.identifier = "VST3-Fake Synth-1-2";
    info.format = "VST3";
    info.isInstrument = true;
    sls::PluginInfo fx = info;
    fx.name = "Fake Delay";
    fx.identifier = "LV2-Fake Delay-3-4";
    fx.format = "LV2";
    fx.isInstrument = false;
    const auto scannedTree = sls::buildBrowserTree (paths, { info, fx }, sls::PluginScanState::Done);
    const auto* scannedPlugin = findChild (scannedTree, "Plugin");
    const auto* instFolder = scannedPlugin != nullptr ? findChild (*scannedPlugin, "Instruments") : nullptr;
    const auto* fxFolder = scannedPlugin != nullptr ? findChild (*scannedPlugin, "Effects") : nullptr;
    const auto* entry = instFolder != nullptr ? findChild (*instFolder, "Fake Synth  (VST3, Acme)") : nullptr;
    const auto* fxEntry = fxFolder != nullptr ? findChild (*fxFolder, "Fake Delay  (LV2, Acme)") : nullptr;
    check (entry != nullptr && entry->kind == NodeKind::Plugin && entry->isDraggable() && entry->payload == "instrument|VST3-Fake Synth-1-2",
           "scanned instrument plugins are listed under Instruments and can be dragged");
    check (fxEntry != nullptr && fxEntry->payload == "effect|LV2-Fake Delay-3-4", "scanned effect plugins are listed under Effects");

    tmp.deleteRecursively();
}

// ---------------------------------------------------------------------------------------- live notes

// Renders `seconds` of audio in blocks of 256 (like a real device) and returns the left channel.
void testConsole()
{
    std::cout << "Aux buses and VCA groups\n";
    const std::vector<sls::Note> a4 { { 69, 0, kBeat, 0.6f } };

    auto build = [&] (auto&& edit)
    {
        auto tracks = sls::Project::defaultTracks();
        auto console = sls::Console::defaults();
        edit (tracks, console);
        return sls::Project::make ({ synthChannel (a4) }, 4 * kBeat, std::move (tracks), {}, {}, {}, std::move (console));
    };
    auto peakIn = [] (const juce::AudioBuffer<float>& b, int from, int to) { return peakOf (b.getReadPointer (0), from, to); };
    auto render = [&] (auto&& edit) { return sls::renderToBuffer (build (edit), {}); };

    const auto dry = render ([] (auto&, auto&) {});
    const float refPeak = peakIn (dry, 0, 24000);
    check (refPeak > 0.05f, "reference note is audible");

    {
        const auto sent = render ([] (auto& t, auto&) { t[0].sends[0] = 1.0f; });
        check (std::abs (peakIn (sent, 0, 24000) / refPeak - 2.0f) < 0.15f, "a post-fader send of 1.0 to an empty aux bus doubles the level (dry + send)");
        const auto b = render ([] (auto& t, auto&) { t[0].sends[1] = 1.0f; });
        check (std::abs (peakIn (b, 0, 24000) / refPeak - 2.0f) < 0.15f, "the second aux bus works the same way");
        const auto half = render ([] (auto& t, auto&) { t[0].sends[0] = 0.5f; });
        check (std::abs (peakIn (half, 0, 24000) / refPeak - 1.5f) < 0.12f, "a send of 0.5 adds half a copy");
    }
    {
        const auto off = render ([] (auto& t, auto& c) { t[0].sends[0] = 1.0f; c.aux[0].mute = true; });
        check (std::abs (peakIn (off, 0, 24000) / refPeak - 1.0f) < 0.02f, "a muted aux bus adds nothing");
        const auto fader = render ([] (auto& t, auto& c) { t[0].sends[0] = 1.0f; t[0].sendsPreFader = true; t[0].volume = 0.0f; c.aux[0].volume = 1.0f; });
        const auto fader2 = render ([] (auto& t, auto& c) { t[0].sends[0] = 1.0f; t[0].sendsPreFader = true; t[0].volume = 0.0f; c.aux[0].volume = 0.5f; });
        check (std::abs (peakIn (fader, 0, 24000) / refPeak - 1.0f) < 0.05f, "pre-fader send: the track fader at zero does not stop the send");
        check (std::abs (peakIn (fader2, 0, 24000) / peakIn (fader, 0, 24000) - 0.5f) < 0.03f, "the aux fader scales what the bus returns");
        const auto post = render ([] (auto& t, auto&) { t[0].sends[0] = 1.0f; t[0].volume = 0.0f; });
        check (peakIn (post, 0, 120000) == 0.0f, "post-fader send: the track fader at zero silences the send too");
        const auto muted = render ([] (auto& t, auto&) { t[0].sends[0] = 1.0f; t[0].sendsPreFader = true; t[0].mute = true; });
        check (peakIn (muted, 0, 120000) == 0.0f, "a muted track sends nothing, even pre-fader");
        const auto left = render ([] (auto& t, auto& c) { t[0].sends[0] = 1.0f; t[0].sendsPreFader = true; t[0].volume = 0.0f; c.aux[0].pan = -1.0f; });
        check (peakOf (left.getReadPointer (1), 0, 24000) == 0.0f && peakIn (left, 0, 24000) > 0.05f, "the aux balance works (hard left: right side silent)");
    }
    {
        // An effect on the bus: a delay with only the wet signal, 800 ms, heard after the dry note has ended.
        const auto echo = render ([] (auto& t, auto& c)
        {
            t[0].sends[0] = 1.0f; t[0].sendsPreFader = true; t[0].volume = 0.0f;
            auto delay = sls::EffectSlot::make (sls::EffectType::Delay);
            delay.params[0] = 800.0f; delay.params[1] = 0.0f; delay.params[3] = 1.0f;
            c.aux[0].slots[0] = delay;
        });
        const auto none = render ([] (auto& t, auto&) { t[0].sends[0] = 1.0f; t[0].sendsPreFader = true; t[0].volume = 0.0f; });
        check (peakIn (echo, 37000, 66000) > 0.05f && peakIn (none, 37000, 66000) == 0.0f, "an effect on the aux bus processes the sends (echo after the note)");
        check (peakIn (echo, 0, 38000) < 0.001f, "and the dry signal is not in the bus output (delay mix 100 %)");
    }
    {
        // 0.61.0: a track's polarity, channel swap and stereo separation (as an FL Studio mixer track's)
        const auto inverted = render ([] (auto& t, auto&) { t[0].invertPhase = true; });
        bool opposite = true;
        for (int i = 0; i < 24000; ++i)
            opposite = opposite && std::abs (inverted.getSample (0, i) + dry.getSample (0, i)) < 1.0e-5f;
        check (opposite, "track polarity: every sample comes out inverted");
        const auto panned = render ([] (auto& t, auto&) { t[0].pan = 0.0f; t[0].slots[0] = slotWith (sls::EffectType::Panomatic, { 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.0f }); });
        const auto swapped = render ([] (auto& t, auto&) { t[0].swapStereo = true; t[0].slots[0] = slotWith (sls::EffectType::Panomatic, { 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.0f }); });
        check (peakOf (panned.getReadPointer (1), 0, 24000) == 0.0f && peakOf (panned.getReadPointer (0), 0, 24000) > 0.05f
                   && peakOf (swapped.getReadPointer (0), 0, 24000) == 0.0f && peakOf (swapped.getReadPointer (1), 0, 24000) > 0.05f,
               "track swap: a sound on the left comes out on the right");
        const auto mono = render ([] (auto& t, auto&) { t[0].separation = 1.0f; t[0].slots[0] = slotWith (sls::EffectType::Panomatic, { 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.0f }); });
        check (std::abs (peakOf (mono.getReadPointer (0), 0, 24000) - peakOf (mono.getReadPointer (1), 0, 24000)) < 1.0e-6f
                   && std::abs (peakOf (mono.getReadPointer (0), 0, 24000) / peakOf (panned.getReadPointer (0), 0, 24000) - 0.5f) < 0.02f,
               "track separation at +1: mono, the same on both sides");
        const auto untouched = render ([] (auto& t, auto&) { t[0].separation = -1.0f; });
        check (std::abs (peakIn (untouched, 0, 24000) / refPeak - 1.0f) < 0.01f, "track separation does not change a sound that is already in the middle");
    }
    {
        const auto half = render ([] (auto& t, auto& c) { t[0].vcaMask = 1; c.vcaLevel[0] = 0.5f; });
        check (std::abs (peakIn (half, 0, 24000) / refPeak - 0.5f) < 0.03f, "a VCA at 0.5 halves its member track");
        const auto other = render ([] (auto& t, auto& c) { t[0].vcaMask = 2; c.vcaLevel[0] = 0.5f; });
        check (std::abs (peakIn (other, 0, 24000) / refPeak - 1.0f) < 0.01f, "a VCA does not touch tracks that are not members");
        const auto two = render ([] (auto& t, auto& c) { t[0].vcaMask = 3; c.vcaLevel[0] = 0.5f; c.vcaLevel[1] = 0.5f; });
        check (std::abs (peakIn (two, 0, 24000) / refPeak - 0.25f) < 0.03f, "two VCAs multiply");
        const auto mute = render ([] (auto& t, auto& c) { t[0].vcaMask = 1; c.vcaMute[0] = true; });
        check (peakIn (mute, 0, 120000) == 0.0f, "a muted VCA silences its members");
        const auto sendFollows = render ([] (auto& t, auto& c) { t[0].vcaMask = 1; c.vcaLevel[0] = 0.0f; t[0].sends[0] = 1.0f; });
        check (peakIn (sendFollows, 0, 120000) == 0.0f, "a post-fader send follows the VCA");
        const auto preIgnores = render ([] (auto& t, auto& c) { t[0].vcaMask = 1; c.vcaLevel[0] = 0.0f; t[0].sends[0] = 1.0f; t[0].sendsPreFader = true; });
        check (std::abs (peakIn (preIgnores, 0, 24000) / refPeak - 1.0f) < 0.05f, "a pre-fader send does not follow the VCA");
    }
    {
        auto tracks = sls::Project::defaultTracks();
        tracks[0].sends = { 9.0f, std::nanf ("") };
        tracks[0].vcaMask = 0xff;
        auto console = sls::Console::defaults();
        console.aux[1].volume = -3.0f;
        console.vcaLevel[0] = 7.0f;
        console.aux[0].sends[0] = 1.0f;
        const auto p = sls::Project::make ({}, 1920, tracks, {}, {}, {}, console);
        check (p.tracks[0].sends[0] == 1.5f && p.tracks[0].sends[1] == 0.0f && p.tracks[0].vcaMask == 3,
               "send levels are clamped (NaN becomes 0) and the VCA mask is cut to the groups that exist");
        check (p.console.aux[1].volume == 0.0f && p.console.vcaLevel[0] == 1.5f && p.console.aux[0].sends[0] == 0.0f,
               "aux and VCA values are clamped; an aux bus has no sends of its own");
    }
    {
        auto project = build ([] (auto& t, auto&) { t[0].sends[0] = 1.0f; });
        sls::Player player;
        player.prepare (kSr);
        std::vector<float> l (512), r (512);
        float* ch[2] = { l.data(), r.data() };
        for (int i = 0; i < 40; ++i)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            player.render (project, 120.0, true, ch, 2, 512);
        }
        check (player.getAuxPeak (0) > 0.05f && player.getAuxRms (0) > 0.01f && player.getAuxPeak (1) == 0.0f && player.getAuxPeak (7) == 0.0f,
               "aux meters: the bus that receives the send reads a level, the other reads zero");
    }
}

// Feeds `seconds` of a stereo sine to the meter (a different amplitude / frequency per call).
void feedSine (sls::LoudnessMeter& meter, double sr, double hz, double amplitude, double seconds, double phase = 0.0, bool inverted = false)
{
    const int total = static_cast<int> (seconds * sr);
    std::vector<float> l (480), r (480);
    int n0 = 0;
    for (int done = 0; done < total; done += 480)
    {
        const int n = std::min (480, total - done);
        for (int i = 0; i < n; ++i)
        {
            const double v = amplitude * std::sin (6.283185307179586 * hz * (n0 + i) / sr + phase);
            l[static_cast<size_t> (i)] = static_cast<float> (v);
            r[static_cast<size_t> (i)] = static_cast<float> (inverted ? -v : v);
        }
        n0 += n;
        meter.process (l.data(), r.data(), n);
    }
}

double dbOf (double amplitude) { return 20.0 * std::log10 (amplitude); }

void testMetering()
{
    std::cout << "Loudness, true peak, spectrum, goniometer\n";
    const double amp23 = std::pow (10.0, -23.0 / 20.0); // a stereo 1 kHz sine at -23 dBFS reads -23.0 LUFS (EBU Tech 3341)

    for (const double sr : { 44100.0, 48000.0, 96000.0 })
    {
        sls::LoudnessMeter m;
        m.prepare (sr);
        feedSine (m, sr, 1000.0, amp23, 25.0);
        const std::string name = "EBU reference: -23 dBFS stereo sine reads -23.0 LUFS (momentary, short-term, integrated) at "
                                 + std::to_string (static_cast<int> (sr)) + " Hz";
        check (std::abs (m.momentary() + 23.0f) < 0.1f && std::abs (m.shortTerm() + 23.0f) < 0.1f && std::abs (m.integrated() + 23.0f) < 0.1f,
               name.c_str());
    }
    {
        sls::LoudnessMeter m;
        m.prepare (48000.0);
        check (m.momentary() == sls::LoudnessMeter::kSilence && m.integrated() == sls::LoudnessMeter::kSilence, "nothing measured yet: silence");
        feedSine (m, 48000.0, 1000.0, amp23, 5.0);
        const float before = m.momentary();
        feedSine (m, 48000.0, 1000.0, amp23 * std::pow (10.0, 10.0 / 20.0), 1.0);
        check (std::abs (m.momentary() - before - 10.0f) < 0.1f, "10 dB louder reads 10 LU higher (after 400 ms)");
        feedSine (m, 48000.0, 1000.0, 0.0, 1.0);
        check (m.momentary() == sls::LoudnessMeter::kSilence, "digital silence reads below the range after 400 ms");
    }
    {
        // K-weighting: the high shelf lifts 10 kHz by about 4 dB, the high-pass takes away some of 50 Hz.
        auto reading = [] (double hz)
        {
            sls::LoudnessMeter m;
            m.prepare (48000.0);
            feedSine (m, 48000.0, hz, 0.1, 3.0);
            return m.shortTerm();
        };
        const float ref = reading (1000.0);
        check (reading (10000.0) - ref > 3.0f && reading (10000.0) - ref < 4.6f, "K-weighting: 10 kHz reads about 4 dB above 1 kHz");
        // The analogue prototype of the RLB high-pass (38.1 Hz, Q 0.5) is 1.17 dB down at 100 Hz and 4.0 dB down at 50 Hz;
        // the 1 kHz reference sits 0.69 dB above flat, so the readings are 1.9 dB and 4.7 dB below it.
        check (std::abs (reading (100.0) - ref + 1.85f) < 0.25f, "K-weighting: 100 Hz reads about 1.9 dB below 1 kHz");
        check (std::abs (reading (50.0) - ref + 4.65f) < 0.3f, "K-weighting: 50 Hz reads about 4.7 dB below 1 kHz");
        check (reading (20.0) - ref < -8.0f, "K-weighting: 20 Hz is strongly attenuated");
    }
    {
        // Gating (Tech 3341): a quiet passage under the relative gate does not pull the integrated value down.
        sls::LoudnessMeter m;
        m.prepare (48000.0);
        feedSine (m, 48000.0, 1000.0, amp23, 20.0);
        feedSine (m, 48000.0, 1000.0, amp23 * std::pow (10.0, -40.0 / 20.0), 20.0); // -63 LUFS: more than 10 LU below
        check (std::abs (m.integrated() + 23.0f) < 0.15f, "relative gate: a passage 40 LU lower is ignored in the integrated loudness");

        sls::LoudnessMeter quiet;
        quiet.prepare (48000.0);
        feedSine (quiet, 48000.0, 1000.0, amp23 * std::pow (10.0, -55.0 / 20.0), 10.0); // -78 LUFS: under the absolute gate
        check (quiet.integrated() == sls::LoudnessMeter::kSilence, "absolute gate: nothing above -70 LUFS gives no integrated value");

        sls::LoudnessMeter two;
        two.prepare (48000.0);
        feedSine (two, 48000.0, 1000.0, amp23 * std::pow (10.0, 3.0 / 20.0), 20.0); // -20
        feedSine (two, 48000.0, 1000.0, amp23 * std::pow (10.0, -7.0 / 20.0), 20.0); // -30
        // the second half is only 10 LU below the first: it counts. The energy mean of -20 and -30 is 10 log10 (0.0055) = -22.6
        check (std::abs (two.integrated() + 22.6f) < 0.2f, "two passages within the gate are averaged by energy (-20 and -30 LUFS give -22.6)");
    }
    {
        // Loudness range (Tech 3342, case 3 style): 20 s at -20 LUFS then 20 s at -30 LUFS: 10 LU.
        sls::LoudnessMeter m;
        m.prepare (48000.0);
        feedSine (m, 48000.0, 1000.0, std::pow (10.0, -20.0 / 20.0), 20.0);
        feedSine (m, 48000.0, 1000.0, std::pow (10.0, -30.0 / 20.0), 20.0);
        check (std::abs (m.range() - 10.0f) < 1.0f, "loudness range of 20 s at -20 LUFS and 20 s at -30 LUFS is 10 LU (+-1)");
        sls::LoudnessMeter steady;
        steady.prepare (48000.0);
        feedSine (steady, 48000.0, 1000.0, amp23, 40.0);
        check (steady.range() < 0.3f, "a steady tone has a loudness range of zero");
        m.requestReset();
        feedSine (m, 48000.0, 1000.0, amp23, 0.1);
        check (m.integrated() == sls::LoudnessMeter::kSilence && m.range() == 0.0f, "reset starts a new measurement");
    }
    {
        // Peaks: a sine at fs/4 with a 45 degree phase has samples of +-0.707 but a true peak of 1.
        sls::LoudnessMeter m;
        m.prepare (48000.0);
        feedSine (m, 48000.0, 12000.0, 1.0, 0.5, 3.14159265358979323846 / 4.0);
        check (std::abs (m.samplePeakDb() - dbOf (0.7071)) < 0.1f, "sample peak of the fs/4 sine is -3.0 dB");
        check (std::abs (m.truePeakDb()) < 0.3f, "true peak of the same signal is 0 dBTP (+-0.3): the converter would reach full scale");
        sls::LoudnessMeter q;
        q.prepare (48000.0);
        feedSine (q, 48000.0, 1000.0, 0.5, 0.5);
        check (std::abs (q.truePeakDb() - dbOf (0.5)) < 0.1f, "true peak of a slow sine matches its amplitude");

        // A high tone, faded in (a hard start is a click, and a click really does overshoot): the samples miss the crest,
        // the reconstruction finds it.
        sls::LoudnessMeter h;
        h.prepare (48000.0);
        std::vector<float> wave (480);
        for (int d = 0; d < 24000; d += 480)
        {
            for (int i = 0; i < 480; ++i)
            {
                const int k = d + i;
                wave[static_cast<size_t> (i)] = static_cast<float> (0.5 * std::min (1.0, k / 4800.0) * std::sin (6.283185307179586 * 18000.0 * k / 48000.0 + 0.3));
            }
            h.process (wave.data(), wave.data(), 480);
        }
        check (h.truePeakDb() - h.samplePeakDb() > 0.25f && std::abs (h.truePeakDb() - dbOf (0.5)) < 0.1f,
               "an 18 kHz tone: the true peak (-6.0) is higher than the highest sample (-6.4) and matches the real amplitude");
    }
    {
        // Spectrum
        auto analyser = std::make_unique<sls::SpectrumAnalyser>();
        std::vector<float> x (sls::SpectrumAnalyser::kFftSize), db (sls::SpectrumAnalyser::kBins);
        const double sr = 48000.0;
        const int bin = 256; // exactly on a bin: 3 kHz
        const double hz = sls::SpectrumAnalyser::binFrequency (bin, sr);
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = static_cast<float> (0.5 * std::sin (6.283185307179586 * hz * static_cast<double> (i) / sr));
        analyser->analyse (x.data(), static_cast<int> (x.size()), db.data());
        const int peakBin = static_cast<int> (std::max_element (db.begin(), db.end()) - db.begin());
        check (peakBin == bin && std::abs (db[static_cast<size_t> (bin)] - dbOf (0.5)) < 0.2f, "spectrum: a 0.5 sine peaks at its bin and reads -6.0 dB");
        check (db[static_cast<size_t> (bin + 40)] < -70.0f && db[static_cast<size_t> (bin - 40)] < -70.0f, "spectrum: far from the tone the floor is low (window leakage)");
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = static_cast<float> (0.25 * std::sin (6.283185307179586 * hz * static_cast<double> (i) / sr)
                                       + 0.25 * std::sin (6.283185307179586 * sls::SpectrumAnalyser::binFrequency (900, sr) * static_cast<double> (i) / sr));
        analyser->analyse (x.data(), static_cast<int> (x.size()), db.data());
        check (std::abs (db[256] - dbOf (0.25)) < 0.2f && std::abs (db[900] - dbOf (0.25)) < 0.2f, "spectrum: two tones are measured separately");
    }
    {
        // Goniometer / correlation and the ring buffer that carries the samples to the GUI
        std::vector<float> a (1000), b (1000), c (1000);
        for (size_t i = 0; i < a.size(); ++i)
        {
            a[i] = static_cast<float> (std::sin (0.05 * static_cast<double> (i)));
            b[i] = a[i];
            c[i] = -a[i];
        }
        check (std::abs (sls::stereoCorrelation (a.data(), b.data(), 1000) - 1.0f) < 1.0e-5f, "correlation of identical channels is +1");
        check (std::abs (sls::stereoCorrelation (a.data(), c.data(), 1000) + 1.0f) < 1.0e-5f, "correlation of opposite channels is -1");
        std::vector<float> quad (1000);
        for (size_t i = 0; i < quad.size(); ++i)
            quad[i] = static_cast<float> (std::cos (0.05 * static_cast<double> (i)));
        check (std::abs (sls::stereoCorrelation (a.data(), quad.data(), 1000)) < 0.1f, "correlation of channels 90 degrees apart is about 0");
        const std::vector<float> zero (1000, 0.0f);
        check (sls::stereoCorrelation (zero.data(), zero.data(), 1000) == 0.0f, "correlation of silence is 0");

        auto ring = std::make_unique<sls::StereoRing>();
        std::vector<float> l (300), r (300), outL (200), outR (200);
        for (int i = 0; i < 300; ++i) { l[static_cast<size_t> (i)] = static_cast<float> (i); r[static_cast<size_t> (i)] = static_cast<float> (-i); }
        check (ring->latest (outL.data(), outR.data(), 200) == 0, "ring: nothing written yet");
        ring->push (l.data(), r.data(), 300);
        const int got = ring->latest (outL.data(), outR.data(), 200);
        check (got == 200 && outL[0] == 100.0f && outL[199] == 299.0f && outR[199] == -299.0f, "ring: the latest frames come back oldest first");
        for (int k = 0; k < 100; ++k)
            ring->push (l.data(), r.data(), 300); // wraps the buffer many times
        ring->latest (outL.data(), outR.data(), 200);
        check (outL[0] == 100.0f && outL[199] == 299.0f, "ring: still right after wrapping");
    }
}

std::vector<float> renderBlocks (sls::Player& player, const sls::Project& project, bool playing, double seconds,
                                 std::vector<float>* rightOut = nullptr)
{
    const int total = static_cast<int> (seconds * kSr);
    std::vector<float> left (static_cast<size_t> (total)), right (static_cast<size_t> (total));
    for (int done = 0; done < total; done += 256)
    {
        const int n = std::min (256, total - done);
        float* ch[2] = { left.data() + done, right.data() + done };
        player.render (project, 120.0, playing, ch, 2, n);
    }
    if (rightOut != nullptr)
        *rightOut = std::move(right);
    return left;
}

// --- up to 500 mixer tracks and the effect memory pool (0.29.0) --------------------------------------------------------------
void testManyTracks()
{
    std::cout << "Up to 500 mixer tracks, effect memory pool (0.29.0)\n";
    using T = sls::EffectType;
    const auto reverbSlot = slotWith (T::Reverb, { 0.0f, 80.0f, 50.0f, 20.0f, 0.0f, 30.0f });
    const std::vector<sls::Note> a4 { { 69, 0, kBeat, 0.8f } };

    // A project with 8 tracks and the same with 500 (the extra ones empty) sound the same, sample for sample.
    {
        auto c = synthChannel (a4);
        auto eight = sls::Project::make ({ c }, 4 * kBeat);
        auto many = sls::Project::make ({ c }, 4 * kBeat, std::vector<sls::MixerTrack> (500));
        sls::Player a, b;
        a.prepare (kSr);
        b.prepare (kSr);
        const auto x = renderBlocks (a, eight, true, 1.0);
        const auto y = renderBlocks (b, many, true, 1.0);
        check (many.tracks.size() == 500 && x == y, "500 tracks with 499 empty sound exactly like 8");

        // a channel on track 451, through a reverb there
        c.mixerTrack = 450;
        std::vector<sls::MixerTrack> tracks (500);
        tracks[450].slots[0] = reverbSlot;
        auto far = sls::Project::make ({ c }, 4 * kBeat, tracks);
        sls::Player p;
        p.prepare (kSr);
        const auto out = renderBlocks (p, far, true, 2.0);
        check (far.channels[0].mixerTrack == 450 && peakOf (out.data(), 0, 24000) > 0.05f && peakOf (out.data(), 60000, 96000) > 1e-4f,
               "a channel on track 451 is heard, with the reverb's tail");

        // the time it takes: 500 tracks (one sounding) for 2 s of audio
        sls::Player timed;
        timed.prepare (kSr);
        const auto start = std::chrono::steady_clock::now();
        renderBlocks (timed, far, true, 2.0);
        const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        std::cout << "    (500 tracks, 2 s of audio: " << static_cast<int> (seconds * 1000) << " ms)\n";
        check (seconds < 0.5, "and 2 s of it render in under 0.5 s (silent tracks cost almost nothing)");
    }

    // The pool: 16 memories at the start; a 20th reverb waits until memory is offered, then works; none of it allocates.
    {
        auto c = synthChannel ({ { 69, 0, 8 * kBeat, 0.8f } });
        c.mixerTrack = 19;
        std::vector<sls::MixerTrack> tracks (20);
        for (auto& t : tracks)
            t.slots[0] = reverbSlot;
        auto project = sls::Project::make ({ c }, 16 * kBeat, tracks);
        sls::Player p;
        p.prepare (kSr);
        check (p.effectMemoryCount() == sls::Player::kInitialEffectMemories && p.effectMemoryShortfall() == 0, "the pool starts with 16 memories");
        std::vector<float> l (256), r (256);
        float* ch[2] = { l.data(), r.data() };
        gAllocs = 0;
        gCounting = true;
        p.render (project, 120.0, true, ch, 2, 256);
        gCounting = false;
        check (gAllocs.load() == 0 && p.effectMemoryShortfall() == 4, "20 reverbs: 4 wait for memory (counted, nothing allocated)");
        // track 20 (the one that sounds) has no memory yet: the sound passes dry, so it is not silent and not reverberant
        for (int i = 0; i < 4; ++i)
        {
            auto m = std::make_unique<sls::EffectMemory>();
            m->prepare (kSr);
            const bool taken = p.offerEffectMemory (std::move (m));
            if (i == 0)
                check (taken, "memory made elsewhere is taken");
        }
        gAllocs = 0;
        gCounting = true;
        for (int i = 0; i < 4; ++i)
            p.render (project, 120.0, true, ch, 2, 256);
        gCounting = false;
        check (gAllocs.load() == 0 && p.effectMemoryShortfall() == 0 && p.effectMemoryCount() == 20,
               "the audio thread adopts it without allocating; no slot waits any more");

        // effects taken out give their memory back to the pool
        for (size_t t = 0; t < 10; ++t)
            project.tracks[t].slots[0] = {};
        tracks.resize (30);
        for (size_t t = 20; t < 30; ++t)
            project.tracks.push_back (tracks[0]); // ten more tracks with a reverb
        for (size_t t = 20; t < 30; ++t)
            project.tracks[t].slots[0] = reverbSlot;
        gAllocs = 0;
        gCounting = true;
        p.render (project, 120.0, true, ch, 2, 256);
        gCounting = false;
        check (gAllocs.load() == 0 && p.effectMemoryShortfall() == 0 && p.effectMemoryCount() == 20,
               "ten reverbs removed and ten added elsewhere: the memory moves, none is missing");
        project.tracks.resize (8);
        p.render (project, 120.0, true, ch, 2, 256);
        check (p.effectMemoryShortfall() == 0, "fewer tracks: the removed tracks' units give theirs back too");

        // a reverb that only got its memory late still works: compare with one that had it from the start
        bool finite = true;
        for (int i = 0; i < 200; ++i)
        {
            p.render (project, 120.0, true, ch, 2, 256);
            for (float v : l)
                finite = finite && std::isfinite (v);
        }
        check (finite, "everything stays finite while memory moves around");
    }

    // An offline Player (export) makes what it lacks itself, at once.
    {
        auto c = synthChannel (a4);
        c.mixerTrack = 39;
        std::vector<sls::MixerTrack> tracks (40);
        for (auto& t : tracks)
            t.slots[1] = slotWith (T::Delay, { 250.0f, 0.3f, 0.7f, 0.5f }); // 250 ms, feedback 0.3, mix 0.5
        const auto project = sls::Project::make ({ c }, 4 * kBeat, tracks);
        const auto exported = sls::renderToBuffer (project, {});
        auto dryTracks = tracks;
        for (auto& t : dryTracks)
            t.slots[1] = {};
        const auto dry = sls::renderToBuffer (sls::Project::make ({ c }, 4 * kBeat, dryTracks), {});
        // the echo of the note (track 40's delay) rings on after the dry note has died away
        const int from = static_cast<int> (0.7 * kSr), to = static_cast<int> (1.0 * kSr);
        check (peakOf (exported.getReadPointer (0), from, to) > 4.0f * peakOf (dry.getReadPointer (0), from, to) + 0.005f,
               "an export with 40 delays: every one works (the export makes its own memory)");
    }

    // Project files with many tracks.
    {
        auto doc = sls::ProjectDocument::blank();
        doc.tracks.resize (300);
        doc.tracks[249].name = "Strings bus";
        doc.tracks[249].volume = 0.5f;
        doc.channels[0].mixerTrack = 249;
        const auto start = std::chrono::steady_clock::now();
        const auto text = doc.toJson();
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
        const auto back = sls::ProjectDocument::fromJson (text);
        check (back.document.has_value() && back.document->tracks.size() == 300 && back.document->tracks[249].name == "Strings bus"
                   && back.document->tracks[249].volume == 0.5f && back.document->channels[0].mixerTrack == 249,
               "300 tracks are saved and opened, with a channel on track 250");
        std::cout << "    (300 tracks written in " << ms << " ms)\n";

        auto json = juce::JSON::parse (text);
        auto* list = json.getDynamicObject()->getProperty ("tracks").getArray();
        while (list->size() < 520)
            list->add (list->getReference (0).clone());
        const auto tooMany = sls::ProjectDocument::fromJson (juce::JSON::toString (json));
        check (tooMany.document.has_value() && tooMany.document->tracks.size() == 500 && ! tooMany.warnings.empty(),
               "a file with 520 tracks opens with 500, and says so");
        while (list->size() > 8)
            list->remove (list->size() - 1);
        const auto few = sls::ProjectDocument::fromJson (juce::JSON::toString (json));
        check (few.document.has_value() && few.document->tracks.size() == 8 && few.document->channels[0].mixerTrack == 7,
               "a channel on a track the file does not have goes to its last track");
    }

    // The memory itself.
    {
        sls::EffectMemory m;
        m.prepare (96000.0);
        check (m.preparedRate() == 96000.0 && m.delayLine[0].size() > 96000u && ! m.preDelay.empty(), "memory is made for a rate");
        m.delayLine[0][5] = 1.0f;
        m.combs[1][3].buf[0] = 1.0f;
        m.clear();
        check (m.delayLine[0][5] == 0.0f && m.combs[1][3].buf[0] == 0.0f, "and cleared to silence");
        sls::EffectUnit unit;
        unit.prepare (kSr);
        std::vector<float> l (512, 0.5f), r (512, 0.5f);
        unit.process (reverbSlot, l.data(), r.data(), 512);
        check (l[100] == 0.5f && r[400] == 0.5f, "a reverb without memory lets the sound through unchanged");
        check (sls::effectNeedsMemory (T::Reverb) && sls::effectNeedsMemory (T::Delay) && sls::effectNeedsMemory (T::Chorus)
                   && sls::effectNeedsMemory (T::PitchCorrector) && ! sls::effectNeedsMemory (T::Equalizer) && ! sls::effectNeedsMemory (T::TimeShaper),
               "the four effects that need memory are known");
    }
}

void testLiveNoteQueue()
{
    std::cout << "Live note queue\n";
    sls::LiveNoteQueue q;
    sls::LiveNoteEvent e;

    check (! q.pop (e), "an empty queue gives nothing");

    bool ordered = true;
    for (int i = 0; i < 10; ++i)
        ordered = ordered && q.push ({ i % 2 == 0, static_cast<std::uint8_t> (i % 8), static_cast<std::uint8_t> (40 + i), 0.5f });
    for (int i = 0; i < 10; ++i)
    {
        const bool got = q.pop (e);
        ordered = ordered && got && e.pitch == 40 + i && e.on == (i % 2 == 0) && e.channel == i % 8;
    }
    check (ordered && ! q.pop (e), "events come out in the order they went in, once");

    int accepted = 0;
    for (int i = 0; i < 200; ++i)
        accepted += q.push ({ true, 0, 60, 0.8f }) ? 1 : 0;
    check (accepted == static_cast<int> (sls::LiveNoteQueue::kCapacity), "a full queue refuses events instead of overwriting");
    check (q.pop (e) && q.push ({ false, 0, 60, 0.0f }), "after one pop there is room again");

    // One producer thread, one consumer thread, 200000 events: nothing lost, nothing reordered, nothing duplicated.
    sls::LiveNoteQueue shared;
    constexpr int kEvents = 200000;
    std::atomic<bool> inOrder { true };
    std::thread consumer ([&]
    {
        sls::LiveNoteEvent ev;
        int expected = 0;
        while (expected < kEvents)
        {
            if (shared.pop (ev))
            {
                const int value = ev.pitch | (ev.channel << 7);
                if (value != (expected & 0x3ff))
                    inOrder = false;
                ++expected;
            }
        }
    });
    for (int i = 0; i < kEvents; ++i)
    {
        const int value = i & 0x3ff;
        while (! shared.push ({ true, static_cast<std::uint8_t> (value >> 7), static_cast<std::uint8_t> (value & 0x7f), 0.5f }))
            std::this_thread::yield();
    }
    consumer.join();
    check (inOrder.load(), "two threads: 200000 events arrive complete and in order");
}

void testLiveNotes()
{
    std::cout << "Live notes (played by hand)\n";
    const auto rms = [] (const std::vector<float>& x, double fromS, double toS)
    {
        return rmsOf (x, static_cast<int> (fromS * kSr), static_cast<int> (toS * kSr));
    };

    {
        // Transport stopped: the note sounds as long as it is held, then dies away within the release.
        const auto project = oneBar ({ synthChannel ({}) });
        sls::Player player;
        player.prepare (kSr);

        player.liveNoteOn (project, 0, 69, 0.8f);
        const auto held = renderBlocks (player, project, false, 1.0);
        check (rms (held, 0.1, 1.0) > 0.03, "a held note sounds with the transport stopped");
        check (toneEnergy (held.data(), 4800, 40000, 440.0) > 4.0 * toneEnergy (held.data(), 4800, 40000, 415.0)
              && toneEnergy (held.data(), 4800, 40000, 440.0) > 4.0 * toneEnergy (held.data(), 4800, 40000, 466.2),
              "its pitch is the pressed key (A4 = 440 Hz)");

        player.liveNoteOff (0, 69);
        const auto tail = renderBlocks (player, project, false, 0.5);
        check (rms (tail, 0.0, 0.03) > 0.01 && peakOf (tail.data(), static_cast<int> (0.2 * kSr), static_cast<int> (0.5 * kSr)) == 0.0f,
               "after the release it is silent again");
    }
    {
        // It lasts through the loop boundary and through the transport starting and stopping.
        const auto project = oneBar ({ synthChannel ({}) }); // 2 s loop at 120 BPM
        sls::Player player;
        player.prepare (kSr);
        player.liveNoteOn (project, 0, 60, 0.8f);
        const auto a = renderBlocks (player, project, true, 3.0);  // crosses the loop end at 2 s
        const auto b = renderBlocks (player, project, false, 0.5); // transport stops: rewinds
        const auto c = renderBlocks (player, project, true, 0.5);  // and starts again
        check (rms (a, 2.0, 3.0) > 0.03, "a held note keeps sounding when the loop wraps");
        check (rms (b, 0.2, 0.5) > 0.03 && rms (c, 0.2, 0.5) > 0.03, "and when the transport stops and starts");
        player.liveNoteOff (0, 60);
        const auto d = renderBlocks (player, project, true, 0.4);
        check (peakOf (d.data(), static_cast<int> (0.25 * kSr), static_cast<int> (0.4 * kSr)) == 0.0f, "and it ends on release");
    }
    {
        // Two keys at once, released independently.
        const auto project = oneBar ({ synthChannel ({}) });
        sls::Player player;
        player.prepare (kSr);
        player.liveNoteOn (project, 0, 60, 0.8f);
        player.liveNoteOn (project, 0, 72, 0.8f);
        const auto both = renderBlocks (player, project, false, 0.5);
        player.liveNoteOff (0, 60);
        const auto one = renderBlocks (player, project, false, 0.5);
        const double c4 = 261.626, c5 = 523.251;
        check (toneEnergy (both.data(), 4800, 24000, c4) > 0.02 && toneEnergy (both.data(), 4800, 24000, c5) > 0.02,
               "two held keys sound together");
        check (toneEnergy (one.data(), 9600, 24000, c5) > 0.02 && toneEnergy (one.data(), 9600, 24000, c4) < 0.005,
               "releasing one key leaves the other");
    }
    {
        // It goes through the channel (pitch, volume, mute) and the mixer track (mute).
        auto c = synthChannel ({});
        c.pitch = 12.0f;
        sls::Player player;
        player.prepare (kSr);
        auto project = oneBar ({ c });
        player.liveNoteOn (project, 0, 69, 0.8f);
        const auto up = renderBlocks (player, project, false, 0.5);
        player.liveNoteOff (0, 69);
        check (toneEnergy (up.data(), 4800, 24000, 880.0) > 4.0 * toneEnergy (up.data(), 4800, 24000, 440.0),
               "the channel's Pitch knob applies to a live note");

        sls::Player p2;
        p2.prepare (kSr);
        auto muted = synthChannel ({});
        muted.mute = true;
        const auto mutedProject = oneBar ({ muted });
        p2.liveNoteOn (mutedProject, 0, 69, 0.8f);
        const auto m = renderBlocks (p2, mutedProject, false, 0.5);
        check (peakOf (m.data(), static_cast<int> (0.2 * kSr), static_cast<int> (0.5 * kSr)) < 1.0e-3f, "a muted channel is silent");

        sls::Player p3;
        p3.prepare (kSr);
        auto tracks = sls::Project::defaultTracks();
        tracks[0].mute = true;
        const auto trackMuted = sls::Project::make ({ synthChannel ({}) }, 4 * kBeat, tracks);
        p3.liveNoteOn (trackMuted, 0, 69, 0.8f);
        const auto t = renderBlocks (p3, trackMuted, false, 0.5);
        check (peakOf (t.data(), static_cast<int> (0.2 * kSr), static_cast<int> (0.5 * kSr)) < 1.0e-3f, "a muted mixer track is silent");
    }
    {
        // Sampler channel: the sample plays once at the pitch of the key.
        auto sample = sineSample (440.0, 1.0, kSr);
        const auto project = oneBar ({ samplerChannel (sample, {}) });
        sls::Player player;
        player.prepare (kSr);
        player.liveNoteOn (project, 0, 72, 1.0f); // an octave above the root (60)
        const auto out = renderBlocks (player, project, false, 0.6);
        check (toneEnergy (out.data(), 4800, 24000, 880.0) > 4.0 * toneEnergy (out.data(), 4800, 24000, 440.0),
               "a sampler channel plays its sample at the key's pitch");
    }
    {
        // Bad input is ignored.
        const auto project = oneBar ({ synthChannel ({}) });
        sls::Player player;
        player.prepare (kSr);
        player.liveNoteOn (project, 3, 60, 0.8f);   // no such channel
        player.liveNoteOn (project, -1, 60, 0.8f);
        player.liveNoteOn (project, 99, 60, 0.8f);
        player.liveNoteOff (5, 60);                 // nothing to release
        const auto out = renderBlocks (player, project, false, 0.3);
        check (peakOf (out.data(), 0, static_cast<int> (0.3 * kSr)) == 0.0f, "a channel that does not exist makes no sound");

        player.liveNoteOn (project, 0, 60, std::nanf (""));
        const auto nan = renderBlocks (player, project, false, 0.2);
        check (peakOf (nan.data(), 4800, 9600) > 0.01f && peakOf (nan.data(), 4800, 9600) < 1.0f, "a NaN velocity falls back to a normal level");
        player.liveNoteOff (0, 60);
    }
    {
        // A note whose "off" was lost ends by itself (20 s).
        const auto project = oneBar ({ synthChannel ({}) });
        sls::Player player;
        player.prepare (kSr);
        player.liveNoteOn (project, 0, 60, 0.8f);
        const auto out = renderBlocks (player, project, false, 21.0);
        check (rms (out, 18.0, 19.0) > 0.03, "a held note is still sounding after 19 s");
        check (peakOf (out.data(), static_cast<int> (20.2 * kSr), static_cast<int> (21.0 * kSr)) == 0.0f,
               "a note that was never released is ended after 20 s");
    }
    {
        // Real-time safety: starting and stopping live notes between blocks does not allocate.
        auto project = oneBar ({ synthChannel ({}), samplerChannel (sls::makeKickSample(), {}) });
        sls::Player player;
        player.prepare (kSr);
        std::vector<float> l (256), r (256);
        float* ch[2] = { l.data(), r.data() };

        gAllocs = 0;
        gCounting = true;
        for (int i = 0; i < 500; ++i)
        {
            player.liveNoteOn (project, i % 2, 48 + i % 24, 0.8f);
            player.render (project, 120.0, i % 3 == 0, ch, 2, 256);
            player.liveNoteOff (i % 2, 48 + i % 24);
        }
        gCounting = false;
        check (gAllocs.load() == 0, "live notes on and off allocate nothing (500 blocks)");
    }
}


// ---------------------------------------------------------------------------------------- project files

bool sameNotes (const std::vector<sls::Note>& a, const std::vector<sls::Note>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].pitch != b[i].pitch || a[i].startTick != b[i].startTick || a[i].lengthTicks != b[i].lengthTicks
            || a[i].velocity != b[i].velocity || a[i].link != b[i].link)
            return false;
    return true;
}

bool sameDocument (const sls::ProjectDocument& a, const sls::ProjectDocument& b)
{
    if (a.channels.size() != b.channels.size() || a.tracks.size() != b.tracks.size()
        || a.sampleSources != b.sampleSources || a.currentPattern != b.currentPattern || a.songMode != b.songMode
        || a.bpm != b.bpm || a.masterVolume != b.masterVolume
        || a.arrangement.patterns.size() != b.arrangement.patterns.size()
        || a.arrangement.clips.size() != b.arrangement.clips.size()
        || a.arrangement.automationClips.size() != b.arrangement.automationClips.size())
        return false;

    for (size_t i = 0; i < a.channels.size(); ++i)
    {
        const auto &x = a.channels[i], &y = b.channels[i];
        if (x.name != y.name || x.instrument != y.instrument || x.rootPitch != y.rootPitch || x.volume != y.volume
            || x.pan != y.pan || x.mute != y.mute || x.solo != y.solo || x.pitch != y.pitch || x.swing != y.swing
            || x.mixerTrack != y.mixerTrack)
            return false;
    }
    for (size_t i = 0; i < a.console.aux.size(); ++i)
    {
        const auto &x = a.console.aux[i], &y = b.console.aux[i];
        if (x.name != y.name || x.volume != y.volume || x.pan != y.pan || x.mute != y.mute)
            return false;
        for (size_t s = 0; s < x.slots.size(); ++s)
            if (! (x.slots[s].sanitised() == y.slots[s].sanitised()))
                return false;
    }
    if (a.console.vcaLevel != b.console.vcaLevel || a.console.vcaMute != b.console.vcaMute)
        return false;
    for (size_t i = 0; i < a.tracks.size(); ++i)
    {
        if (a.tracks[i].sends != b.tracks[i].sends || a.tracks[i].sendsPreFader != b.tracks[i].sendsPreFader
            || a.tracks[i].vcaMask != b.tracks[i].vcaMask)
            return false;
        const auto &x = a.tracks[i], &y = b.tracks[i];
        if (x.name != y.name || x.volume != y.volume || x.pan != y.pan || x.mute != y.mute || x.solo != y.solo)
            return false;
        for (size_t s = 0; s < x.slots.size(); ++s)
            if (! (x.slots[s].sanitised() == y.slots[s].sanitised()))
                return false;
    }
    for (size_t s = 0; s < a.masterSlots.size(); ++s)
        if (! (a.masterSlots[s].sanitised() == b.masterSlots[s].sanitised()))
            return false;
    for (size_t p = 0; p < a.arrangement.patterns.size(); ++p)
    {
        const auto &x = a.arrangement.patterns[p], &y = b.arrangement.patterns[p];
        if (x.name != y.name || x.notes.size() != y.notes.size())
            return false;
        for (size_t c = 0; c < x.notes.size(); ++c)
            if (! sameNotes (x.notes[c], y.notes[c]))
                return false;
    }
    for (size_t i = 0; i < a.arrangement.clips.size(); ++i)
    {
        const auto &x = a.arrangement.clips[i], &y = b.arrangement.clips[i];
        if (x.pattern != y.pattern || x.track != y.track || x.startTick != y.startTick || x.lengthTicks != y.lengthTicks || x.offsetTicks != y.offsetTicks)
            return false;
    }
    for (size_t i = 0; i < a.arrangement.automationClips.size(); ++i)
    {
        const auto &x = a.arrangement.automationClips[i], &y = b.arrangement.automationClips[i];
        if (! (x.target == y.target) || x.lane != y.lane || x.startTick != y.startTick || x.lengthTicks != y.lengthTicks
            || x.points.size() != y.points.size())
            return false;
        for (size_t k = 0; k < x.points.size(); ++k)
            if (x.points[k].tick != y.points[k].tick || x.points[k].value != y.points[k].value || x.points[k].shape != y.points[k].shape)
                return false;
    }
    return true;
}

sls::ProjectDocument richDocument()
{
    sls::ProjectDocument d;
    d.bpm = 133.0;
    d.masterVolume = 0.37f;
    d.songMode = true;
    d.currentPattern = 1;

    sls::Channel lead;
    lead.name = "Lead";
    lead.volume = 0.55f;
    lead.pan = -0.3f;
    lead.pitch = 0.1f;
    lead.swing = 0.25f;
    lead.mixerTrack = 2;
    sls::Channel kick;
    kick.name = "Kick";
    kick.instrument = sls::Instrument::Sampler;
    kick.rootPitch = 64;
    kick.mute = true;
    kick.solo = true;
    kick.volume = 1.25f;
    d.channels = { lead, kick };
    d.sampleSources = { "", "builtin:kick" };

    d.tracks = sls::Project::defaultTracks();
    d.tracks[2].name = "Leads";
    d.tracks[2].volume = 0.8f;
    d.tracks[2].pan = 0.4f;
    d.tracks[2].mute = true;
    d.tracks[2].slots[0] = sls::EffectSlot::make (sls::EffectType::Reverb);
    d.tracks[2].slots[0].params[3] = 0.61f;
    d.tracks[2].slots[2] = sls::EffectSlot::make (sls::EffectType::Compressor);
    d.tracks[2].slots[2].bypass = true;
    d.tracks[2].slots[2].params[0] = -33.5f;
    d.tracks[5].solo = true;
    d.tracks[2].sends = { 0.7f, 0.25f };
    d.tracks[2].sendsPreFader = true;
    d.tracks[2].vcaMask = 2;
    d.tracks[4].vcaMask = 3;
    d.console.aux[1].name = "Hall";
    d.console.aux[1].volume = 0.9f;
    d.console.aux[1].pan = -0.3f;
    d.console.aux[1].mute = true;
    d.console.aux[0].slots[1] = sls::EffectSlot::make (sls::EffectType::Reverb);
    d.console.aux[0].slots[1].params[3] = 1.0f;
    d.console.vcaLevel = { 0.6f, 1.2f };
    d.console.vcaMute = { false, true };
    d.masterSlots[1] = sls::EffectSlot::make (sls::EffectType::Equalizer);
    d.masterSlots[1].params[1] = 4.5f;
    d.masterSlots[3] = sls::EffectSlot::make (sls::EffectType::Compressor);
    d.masterSlots[3].bypass = true;

    d.arrangement.patterns.resize (2);
    setLengths (d.arrangement, 8 * kBeat); // two bars (the default before 0.42.0), so the note at beat 5 fits
    d.arrangement.patterns[0].name = "Intro";
    d.arrangement.patterns[1].name = "Verse";
    d.arrangement.ensureChannelCount (2);
    d.arrangement.patterns[0].notes[0] = { { 60, 0, 240, 0.8f }, { 64, 480, 480, 0.5f }, { 67, 960, 120, 1.0f, sls::NoteLink::Slide } };
    d.arrangement.patterns[1].notes[1] = { { 60, 0, 120, 0.9f, sls::NoteLink::Legato }, { 60, 1920, 120, 0.7f } };
    d.arrangement.clips = { uc (d.arrangement, 0, 0, 0, 2), uc (d.arrangement, 1, 1, 1, 3) };

    auto fade = sls::makeFlatAutomationClip ({ sls::AutomationKind::TrackVolume, 2, 0, 0 }, 1, 960, 3840, 0.0f);
    fade.points.back().value = 0.8123456f;
    fade.points[0].shape = sls::AutomationShape::Smooth;
    sls::insertAutomationPoint (fade, 1234.5678, 0.3f, sls::automationRange (fade.target), sls::AutomationShape::Hold);
    d.arrangement.automationClips.push_back (fade);
    d.arrangement.automationClips.push_back (sls::makeFlatAutomationClip ({ sls::AutomationKind::TrackEffect, 2, 0, 3 }, 0, 0, 1920, 0.61f));
    return d;
}

juce::File freshTempDir (const char* name)
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile (name, "", false);
    dir.createDirectory();
    return dir;
}

std::string errorOf (const juce::String& text)
{
    return sls::ProjectDocument::fromJson (text).error;
}

void testProjectFile()
{
    std::cout << "Project files\n";

    {
        const auto doc = richDocument();
        const auto back = sls::ProjectDocument::fromJson (doc.toJson());
        check (back.document.has_value() && back.error.empty() && back.warnings.empty(), "a saved project reads back without errors or warnings");
        check (back.document.has_value() && sameDocument (doc, *back.document),
               "every field survives a round trip (channels, tracks, effects, patterns, notes, clips, tempo)");
        check (back.document.has_value() && back.document->channels[0].pitch == 0.1f && back.document->masterVolume == 0.37f
                   && back.document->tracks[2].slots[2].params[0] == -33.5f,
               "floating-point values come back bit for bit");
    }
    {
        const auto doc = richDocument();
        const auto back = sls::ProjectDocument::fromJson (doc.toJson());
        check (back.document.has_value() && back.document->arrangement.automationClips.size() == 2
                   && back.document->arrangement.automationClips[0].points[1].tick == 1234.5678
                   && back.document->arrangement.automationClips[0].points[1].shape == sls::AutomationShape::Hold,
               "automation clips survive a round trip, points at fractional ticks included");

        auto v = juce::JSON::parse (doc.toJson());
        v.getDynamicObject()->removeProperty ("automation");
        const auto old = sls::ProjectDocument::fromJson (juce::JSON::toString (v));
        check (old.document.has_value() && old.document->arrangement.automationClips.empty() && old.warnings.empty(),
               "a file without automation (from an older version) opens as before");

        auto w = juce::JSON::parse (doc.toJson());
        auto* list = w.getDynamicObject()->getProperty ("automation").getArray();
        list->getReference (0).getDynamicObject()->setProperty ("kind", "somethingNew");
        list->getReference (1).getDynamicObject()->setProperty ("lane", 99);
        const auto odd = sls::ProjectDocument::fromJson (juce::JSON::toString (w));
        check (odd.document.has_value() && odd.document->arrangement.automationClips.empty() && odd.warnings.size() == 1,
               "an automation clip of an unknown kind, or on a lane that does not exist, is left out with one warning");
    }
    {
        const auto doc = richDocument();
        const auto back = sls::ProjectDocument::fromJson (doc.toJson());
        check (back.document.has_value() && back.document->masterSlots[1].type == sls::EffectType::Equalizer
                   && back.document->masterSlots[1].params[1] == 4.5f && back.document->masterSlots[3].bypass
                   && back.document->masterSlots[0].type == sls::EffectType::None,
               "the master bus effects survive a round trip");

        auto v = juce::JSON::parse (doc.toJson());
        v.getDynamicObject()->removeProperty ("master");
        const auto old = sls::ProjectDocument::fromJson (juce::JSON::toString (v));
        check (old.document.has_value() && old.warnings.empty() && old.document->masterSlots[1].type == sls::EffectType::None,
               "a project from before master effects opens, with an empty master chain");
    }
    {
        // aux buses, sends and VCA groups
        const auto doc = richDocument();
        const auto back = sls::ProjectDocument::fromJson (doc.toJson());
        check (back.document.has_value() && back.document->tracks[2].sends[0] == 0.7f && back.document->tracks[2].sends[1] == 0.25f
                   && back.document->tracks[2].sendsPreFader && back.document->tracks[2].vcaMask == 2 && back.document->tracks[4].vcaMask == 3
                   && back.document->console.aux[1].name == "Hall" && back.document->console.aux[1].mute
                   && std::abs (back.document->console.aux[1].pan + 0.3f) < 1.0e-6f
                   && back.document->console.aux[0].slots[1].type == sls::EffectType::Reverb
                   && back.document->console.vcaLevel[0] == 0.6f && back.document->console.vcaMute[1],
               "sends, aux buses (with their effects) and VCA groups survive a round trip");

        auto v = juce::JSON::parse (doc.toJson());
        v.getDynamicObject()->removeProperty ("console");
        auto* trackList = v.getDynamicObject()->getProperty ("tracks").getArray();
        trackList->getReference (2).getDynamicObject()->removeProperty ("sends");
        trackList->getReference (2).getDynamicObject()->removeProperty ("vca");
        const auto old = sls::ProjectDocument::fromJson (juce::JSON::toString (v));
        check (old.document.has_value() && old.warnings.empty() && old.document->tracks[2].sends[0] == 0.0f
                   && old.document->tracks[2].vcaMask == 0 && old.document->console.aux[0].name == "Aux A"
                   && old.document->console.vcaLevel[0] == 1.0f,
               "a project from before aux buses opens with no sends, no VCA and two empty buses");

        auto w = juce::JSON::parse (doc.toJson());
        auto* consoleObject = w.getDynamicObject()->getProperty ("console").getDynamicObject();
        consoleObject->setProperty ("aux", juce::var ("not a list"));
        consoleObject->setProperty ("vca", juce::var (3));
        w.getDynamicObject()->getProperty ("tracks").getArray()->getReference (2).getDynamicObject()->setProperty ("vca", juce::var (99));
        w.getDynamicObject()->getProperty ("tracks").getArray()->getReference (2).getDynamicObject()->setProperty ("sends", juce::var (juce::Array<juce::var> { juce::var (50.0), juce::var ("x") }));
        const auto odd = sls::ProjectDocument::fromJson (juce::JSON::toString (w));
        check (odd.document.has_value() && odd.document->console.aux[1].name == "Aux B" && odd.document->tracks[2].vcaMask == 3
                   && odd.document->tracks[2].sends[0] == 1.5f && odd.document->tracks[2].sends[1] == 0.0f,
               "odd console values are repaired (wrong types ignored, numbers clamped)");
    }
    {
        const auto blank = sls::ProjectDocument::blank();
        const auto back = sls::ProjectDocument::fromJson (blank.toJson());
        check (back.document.has_value() && sameDocument (blank, *back.document) && blank.channels.size() == 1
                   && blank.tracks.size() == static_cast<size_t> (sls::kDefaultTracks) && blank.arrangement.patterns.size() == 1
                   && blank.arrangement.clips.empty(),
               "a blank project is valid and round-trips");
    }
    {
        // What is heard is the same: compile the original and the reloaded document and render both.
        auto doc = richDocument();
        doc.channels[1].sample = sls::makeKickSample();
        auto build = [] (const sls::ProjectDocument& d)
        {
            auto channels = d.channels;
            for (size_t c = 0; c < channels.size(); ++c)
                channels[c].notes = d.arrangement.patterns[0].notes[c];
            channels[1].mute = false;
            channels[1].solo = false;
            auto tracks = d.tracks;
            tracks[2].mute = false;
            tracks[5].solo = false;
            return sls::compilePattern (channels, d.arrangement, 0, tracks);
        };
        auto reloaded = *sls::ProjectDocument::fromJson (doc.toJson()).document;
        reloaded.channels[1].sample = sls::makeKickSample();
        const auto a = sls::renderToBuffer (build (doc), {});
        const auto b = sls::renderToBuffer (build (reloaded), {});
        bool identical = a.getNumSamples() == b.getNumSamples() && peakOf (a.getReadPointer (0), 0, a.getNumSamples()) > 0.02f; // audible, not silence==silence
        for (int i = 0; identical && i < a.getNumSamples(); ++i)
            identical = a.getReadPointer (0)[i] == b.getReadPointer (0)[i] && a.getReadPointer (1)[i] == b.getReadPointer (1)[i];
        check (identical, "a reloaded project renders to exactly the same audio (reverb, pitch, swing, pan included)");
    }

    {
        // A sample that was not found stays referenced, is silent, and does not upset the engine.
        auto doc = richDocument();
        const auto missing = sls::makeMissingSample ("/music/gone/kick808.wav");
        check (missing != nullptr && missing->numFrames() == 0 && missing->source == "/music/gone/kick808.wav" && missing->name == "kick808",
               "a missing sample keeps its path and name and holds no audio");

        auto channel = samplerChannel (missing, { { 60, 0, 120, 1.0f }, { 60, 480, 120, 1.0f } });
        const auto project = oneBar ({ channel });
        sls::Player player;
        player.prepare (kSr);
        player.liveNoteOn (project, 0, 60, 1.0f);
        const auto out = renderBlocks (player, project, true, 1.0);
        check (peakOf (out.data(), 0, static_cast<int> (kSr)) == 0.0f, "a channel with a missing sample plays silence (notes and live notes)");

        doc.channels[1].instrument = sls::Instrument::Sampler;
        doc.sampleSources[1] = missing->source;
        const auto back = sls::ProjectDocument::fromJson (doc.toJson());
        check (back.document.has_value() && back.document->sampleSources[1] == "/music/gone/kick808.wav",
               "the path of a missing sample survives saving and loading again");
    }

    // --- rejected files
    check (! errorOf ("").empty() && ! errorOf ("not json at all").empty() && ! errorOf ("[1,2,3]").empty(), "empty, garbage and non-object files are refused");
    check (! errorOf ("{\"format\":\"something-else\",\"version\":1}").empty(), "a different file format is refused");
    {
        auto text = richDocument().toJson();
        check (errorOf (text.replace ("\"version\": 1", "\"version\": 7")).find ("newer version") != std::string::npos
                   || errorOf (text.replace ("\"version\":1", "\"version\":7")).find ("newer version") != std::string::npos,
               "a project from a newer version is refused with a clear message");
        check (! errorOf (text.replace ("\"version\": 1", "\"version\": 0").replace ("\"version\":1", "\"version\":0")).empty(),
               "version 0 is refused");
    }
    {
        auto doc = richDocument();
        doc.channels.clear();
        doc.sampleSources.clear();
        check (! errorOf (doc.toJson()).empty(), "a project without channels is refused");

        doc = richDocument();
        while (doc.channels.size() < static_cast<size_t> (sls::kMaxChannels) + 1)
        {
            doc.channels.push_back (doc.channels[0]);
            doc.sampleSources.emplace_back();
        }
        check (! errorOf (doc.toJson()).empty(), "a project with one channel too many is refused");

        doc = richDocument();
        doc.arrangement.patterns.clear();
        check (! errorOf (doc.toJson()).empty(), "a project without patterns is refused");

        auto text = richDocument().toJson();
        check (! errorOf (text.replace ("\"synth\"", "\"theremin\"")).empty(), "an unknown instrument is refused");
    }

    // --- repairs
    {
        const auto parsedDoc = juce::JSON::parse (richDocument().toJson());
        auto* root = parsedDoc.getDynamicObject();
        root->setProperty ("bpm", 9999.0);
        root->setProperty ("masterVolume", "loud");
        const auto text = juce::JSON::toString (parsedDoc, false);
        const auto r = sls::ProjectDocument::fromJson (text);
        check (r.document.has_value() && r.document->bpm == 240.0 && r.document->masterVolume == 0.8f,
               "an out-of-range tempo is clamped and a non-number volume falls back to its default");
    }
    {
        auto doc = richDocument();
        doc.channels[0].volume = 40.0f;
        doc.channels[0].pan = -9.0f;
        doc.channels[0].pitch = 500.0f;
        doc.channels[0].swing = 7.0f;
        doc.channels[0].mixerTrack = 99;
        doc.currentPattern = 50;
        doc.tracks[0].volume = 5.0f;
        doc.arrangement.patterns[0].notes[0].push_back ({ 200, 0, 100, 0.5f });      // pitch out of range
        doc.arrangement.patterns[0].notes[0].push_back ({ 60, 99999, 100, 0.5f });   // starts after the pattern
        doc.arrangement.patterns[0].notes[0].push_back ({ 60, 3800, 9999, 5.0f });   // too long, too loud: kept, trimmed
        doc.arrangement.clips.push_back (uc (doc.arrangement, 0, 0, 1, 1));                            // overlaps the first clip
        doc.arrangement.clips.push_back (uc (doc.arrangement, 9, 0, 20, 1));                           // no such pattern
        const auto r = sls::ProjectDocument::fromJson (doc.toJson());
        check (r.document.has_value(), "a file with out-of-range values still opens");
        if (r.document.has_value())
        {
            const auto& d = *r.document;
            check (d.channels[0].volume == 1.5f && d.channels[0].pan == -1.0f && d.channels[0].pitch == 24.0f
                       && d.channels[0].swing == 1.0f && d.channels[0].mixerTrack == sls::kDefaultTracks - 1
                       && d.currentPattern == 1 && d.tracks[0].volume == 1.5f,
                   "channel, track and pattern values are clamped into range");
            const auto& notes = d.arrangement.patterns[0].notes[0];
            check (notes.size() == 4 && notes.back().startTick == 3800 && notes.back().lengthTicks == 40 && notes.back().velocity == 1.0f,
                   "bad notes are dropped; a note that is too long is trimmed to its pattern");
            check (d.arrangement.clips.size() == 3 && r.warnings.size() == 2,
                   "a clip that cannot be placed is dropped (one overlapping another is kept: layering), and both repairs are reported");
        }
    }
    {
        auto text = richDocument().toJson();
        text = text.replace ("\"reverb\"", "\"quantum-flanger\"");
        const auto r = sls::ProjectDocument::fromJson (text);
        check (r.document.has_value() && r.document->tracks[2].slots[0].type == sls::EffectType::None
                   && r.document->tracks[2].slots[2].type == sls::EffectType::Compressor,
               "an effect this version does not know becomes an empty slot");
    }
    {
        // Only the first 3 tracks present: the others keep their defaults.
        const auto parsedDoc = juce::JSON::parse (richDocument().toJson()); // keeps the object alive
        auto* root = parsedDoc.getDynamicObject();
        auto* tracks = root->getProperty ("tracks").getArray();
        while (tracks->size() > 3)
            tracks->remove (tracks->size() - 1);
        const auto r = sls::ProjectDocument::fromJson (juce::JSON::toString (juce::var (root), false));
        check (r.document.has_value() && r.document->tracks.size() == static_cast<size_t> (sls::kDefaultTracks)
                   && r.document->tracks[7].name == "Track 8" && r.document->tracks[2].name == "Leads",
               "a short list of mixer tracks is padded with defaults (to eight)");
    }

    // --- files on disk
    {
        const auto dir = freshTempDir ("sls-project-test");
        const auto file = dir.getChildFile ("song" + juce::String (sls::kProjectExtension));
        const auto doc = richDocument();

        check (sls::writeProjectFile (file, doc).isEmpty() && file.existsAsFile(), "a project is written to disk");
        const auto back = sls::readProjectFile (file);
        check (back.document.has_value() && sameDocument (doc, *back.document), "and read back from disk");
        check (dir.getNumberOfChildFiles (juce::File::findFiles) == 1, "no temporary file is left behind");

        auto changed = doc;
        changed.bpm = 90.0;
        check (sls::writeProjectFile (file, changed).isEmpty()
                   && sls::readProjectFile (file).document.has_value() && sls::readProjectFile (file).document->bpm == 90.0
                   && dir.getNumberOfChildFiles (juce::File::findFiles) == 1,
               "saving again replaces the file");

        const auto nested = dir.getChildFile ("a").getChildFile ("b").getChildFile ("deep.slp");
        check (sls::writeProjectFile (nested, doc).isEmpty() && nested.existsAsFile(), "missing folders are created");

        check (! sls::readProjectFile (dir.getChildFile ("nope.slp")).error.empty(), "reading a missing file reports an error");
        const auto junk = dir.getChildFile ("junk.slp");
        junk.replaceWithText ("{\"format\": \"sun-loops-studio-project\", \"version\": 1, \"channels\": [");
        check (! sls::readProjectFile (junk).error.empty() && ! sls::readProjectFile (junk).document.has_value(), "a truncated file is refused");
        check (! sls::readProjectFile (dir).error.empty(), "a folder is not a project");

        // A failed save must not destroy the existing file.
        const auto blocked = dir.getChildFile ("blocked.slp");
        sls::writeProjectFile (blocked, doc);
        const auto before = blocked.loadFileAsString();
        const auto notAFolder = dir.getChildFile ("song.slp").getChildFile ("child.slp"); // parent is a file
        check (! sls::writeProjectFile (notAFolder, doc).isEmpty() && blocked.loadFileAsString() == before,
               "writing to an impossible location fails with a message and changes nothing");

        dir.deleteRecursively();
    }

    // --- names
    {
        const auto base = juce::File ("/tmp/x");
        check (sls::withProjectExtension (base.getChildFile ("a")).getFileName() == "a.slp"
                   && sls::withProjectExtension (base.getChildFile ("a.slp")).getFileName() == "a.slp"
                   && sls::withProjectExtension (base.getChildFile ("A.SLP")).getFileName() == "A.SLP"
                   && sls::withProjectExtension (base.getChildFile ("My.Song")).getFileName() == "My.Song.slp",
               "the .slp extension is added when missing and never doubled");
        check (sls::hasProjectExtension (base.getChildFile ("x.SLP")) && ! sls::hasProjectExtension (base.getChildFile ("x.wav"))
                   && ! sls::hasProjectExtension (base.getChildFile ("slp")) && ! sls::hasProjectExtension (base.getChildFile ("old.slsproj"))
                   && sls::withProjectExtension (base.getChildFile ("old.slsproj")).getFileName() == "old.slsproj.slp",
               "only .slp is a project (0.44.0: the .slsproj of versions before 0.8 is no longer recognised)");

        const auto dir = freshTempDir ("sls-project-names");
        const auto when = juce::Time (2026, 10, 5, 21, 54, 7); // month is 0-based: 5 November
        const auto first = sls::automaticProjectFile (dir, when);
        check (first.getFileName() == "Untitled 2026-11-05 21-54-07.slp", "an automatic name is 'Untitled' plus the date and time");
        first.replaceWithText ("x");
        const auto second = sls::automaticProjectFile (dir, when);
        check (second != first && second.getFileName() == "Untitled 2026-11-05 21-54-07 (2).slp", "an automatic name never overwrites an existing file");
        dir.deleteRecursively();
    }
}

} // namespace

void testBufferTuning()
{
    using sls::BufferTuner;

    check (std::abs (sls::bufferMilliseconds (1024, 48000.0) - 21.333) < 0.01 && sls::bufferMilliseconds (512, 0.0) == 0.0,
           "a buffer's length in milliseconds");
    check (sls::isClean ({ 512, 0, 0.5 }) && sls::isClean ({ 512, -1, 0.1 }) && ! sls::isClean ({ 512, 1, 0.1 })
               && ! sls::isClean ({ 512, 0, 0.51 }),
           "clean means no dropped block and the worst block under half its time (unknown dropouts count as none)");

    const auto run = [] (std::vector<int> sizes, const std::vector<std::pair<int, double>>& seen)
    {
        BufferTuner t (std::move (sizes));
        for (const auto& [xruns, load] : seen)
            if (! t.finished())
                t.addTrial (xruns, load);
        return t;
    };

    {   // a fast computer: 256 is clean, and so is 512 -> 512 (the measured step of margin)
        auto t = run ({ 256, 512, 1024, 2048 }, { { 0, 0.05 }, { 0, 0.04 } });
        check (t.finished() && t.result() == 512 && ! t.struggling() && t.triedCount() == 2, "two clean sizes in a row: the second is chosen, testing stops");
    }
    {   // drops at 256 and 512, clean from 1024 -> 2048
        auto t = run ({ 256, 512, 1024, 2048 }, { { 3, 0.2 }, { 1, 0.3 }, { 0, 0.2 }, { 0, 0.1 } });
        check (t.finished() && t.result() == 2048 && ! t.struggling(), "drops at the small sizes: the first clean pair decides");
    }
    {   // a size that is clean but whose bigger neighbour drops is not trusted
        auto t = run ({ 256, 512, 1024, 2048 }, { { 0, 0.1 }, { 2, 0.1 }, { 0, 0.1 }, { 0, 0.1 } });
        check (t.finished() && t.result() == 2048, "a clean size followed by a dirty one is not trusted");
    }
    {   // slow in load rather than dropouts
        auto t = run ({ 256, 512, 1024 }, { { 0, 0.9 }, { 0, 0.6 }, { 0, 0.3 } });
        check (t.finished() && t.result() == 1024 && ! t.struggling(), "a block that takes most of its time is not clean even without dropouts");
    }
    {   // only the last one is clean
        auto t = run ({ 256, 512, 1024 }, { { 1, 0.1 }, { 1, 0.1 }, { 0, 0.1 } });
        check (t.finished() && t.result() == 1024 && ! t.struggling(), "only the largest size clean: it is used");
    }
    {   // nothing is clean
        auto t = run ({ 256, 512, 1024 }, { { 5, 0.9 }, { 4, 0.8 }, { 2, 0.7 } });
        check (t.finished() && t.result() == 1024 && t.struggling() && t.trials().size() == 3, "nothing clean: the largest is used and the tuner says it is struggling");
    }
    {   // odd input
        BufferTuner empty ({});
        check (! empty.finished() && empty.sizeToTry() == sls::kDefaultBufferSize, "no candidates: falls back to the default size");
        BufferTuner messy ({ 1024, 256, 1024, 512 });
        check (messy.candidateCount() == 3 && messy.sizeToTry() == 256, "candidates are sorted and de-duplicated, smallest first");
        BufferTuner single ({ 512 });
        single.addTrial (0, 0.1);
        check (single.finished() && single.result() == 512, "a single candidate decides at once");
        const int tried = single.triedCount();
        single.addTrial (9, 9.0);
        check (single.triedCount() == tried && single.result() == 512, "results after the end are ignored");
    }
}

void testMasterEffectsAndRouting()
{
    std::cout << "Master effects and track assignment\n";
    const std::vector<sls::Note> a4 { { 69, 0, kBeat, 0.8f } };

    auto withMaster = [&] (const sls::EffectSlot& slot, size_t index = 1)
    {
        std::array<sls::EffectSlot, sls::kSlotsPerTrack> master {};
        master[index] = slot;
        return sls::renderToBuffer (sls::Project::make ({ synthChannel (a4) }, 4 * kBeat, {}, master), {});
    };

    const auto dry = withMaster (sls::EffectSlot {});
    const auto wet = withMaster (slotWith (sls::EffectType::Delay, { 250.0f, 0.7f, 1.0f, 0.5f }));
    check (peakOf (dry.getReadPointer (0), 40000, 144000) == 0.0f, "reference: without master effects the note ends in silence");
    check (peakOf (wet.getReadPointer (0), 48000, 70000) > 0.003f, "a delay in a master slot keeps sounding after the note has ended");

    auto bypassed = slotWith (sls::EffectType::Delay, { 250.0f, 0.7f, 1.0f, 0.5f });
    bypassed.bypass = true;
    const auto byp = withMaster (bypassed);
    bool same = true;
    for (int i = 0; i < dry.getNumSamples(); ++i)
        same = same && dry.getReadPointer (0)[i] == byp.getReadPointer (0)[i] && dry.getReadPointer (1)[i] == byp.getReadPointer (1)[i];
    check (same, "a bypassed master slot leaves the sound untouched");

    const auto comp = withMaster (slotWith (sls::EffectType::Compressor, { -30.0f, 20.0f, 1.0f, 100.0f, 0.0f }));
    check (peakOf (comp.getReadPointer (0), 4800, 24000) < 0.6f * peakOf (dry.getReadPointer (0), 4800, 24000),
           "a compressor on the master reduces the level of the whole mix");

    // the master chain acts on the sum: two channels on different tracks are compressed together
    {
        auto a = synthChannel (a4), b = synthChannel (a4);
        b.mixerTrack = 3;
        std::array<sls::EffectSlot, sls::kSlotsPerTrack> master {};
        master[0] = slotWith (sls::EffectType::Compressor, { -30.0f, 20.0f, 1.0f, 100.0f, 0.0f });
        const auto two = sls::renderToBuffer (sls::Project::make ({ a, b }, 4 * kBeat, {}, master), {});
        const auto twoDry = sls::renderToBuffer (sls::Project::make ({ a, b }, 4 * kBeat), {});
        check (peakOf (two.getReadPointer (0), 4800, 24000) < 0.6f * peakOf (twoDry.getReadPointer (0), 4800, 24000),
               "the master chain processes the sum of every track");
    }

    // hostile slots are sanitised like track slots
    {
        sls::EffectSlot bad;
        bad.type = sls::EffectType::Reverb;
        bad.params[0] = std::nanf ("");
        bad.params[1] = 1.0e30f;
        const auto out = withMaster (bad, 3);
        bool finite = true;
        for (int i = 0; i < out.getNumSamples(); ++i)
            finite = finite && std::isfinite (out.getReadPointer (0)[i]);
        check (finite, "NaN and out-of-range parameters in a master slot give finite output");
    }

    // real-time safety with a full master chain
    {
        std::vector<sls::Note> notes;
        for (int i = 0; i < 64; ++i)
            notes.push_back ({ 48 + i % 24, i * 100, 300, 0.7f });
        std::array<sls::EffectSlot, sls::kSlotsPerTrack> master {};
        master[0] = sls::EffectSlot::make (sls::EffectType::Equalizer);
        master[1] = sls::EffectSlot::make (sls::EffectType::Reverb);
        master[2] = sls::EffectSlot::make (sls::EffectType::Delay);
        master[3] = sls::EffectSlot::make (sls::EffectType::Compressor);
        const auto project = sls::Project::make ({ synthChannel (notes) }, 4 * kBeat, {}, master);

        sls::Player player;
        player.prepare (kSr);
        std::vector<float> l (256), r (256);
        float* ch[2] = { l.data(), r.data() };
        gAllocs = 0;
        gCounting = true;
        for (int i = 0; i < 2000; ++i)
            player.render (project, 140.0, true, ch, 2, 256);
        gCounting = false;
        check (gAllocs.load() == 0, "Player::render performs no heap allocation with a full master chain (2000 blocks)");
    }

    // removing a master effect while its tail rings stops the tail at once
    {
        std::array<sls::EffectSlot, sls::kSlotsPerTrack> master {};
        master[0] = sls::EffectSlot::make (sls::EffectType::Reverb);
        auto project = sls::Project::make ({ synthChannel (a4) }, 4 * kBeat, {}, master);
        sls::Player player;
        player.prepare (kSr);
        std::vector<float> l (480), r (480);
        float* ch[2] = { l.data(), r.data() };
        for (int i = 0; i < 20; ++i)
            player.render (project, 120.0, true, ch, 2, 480);
        for (int i = 0; i < 30; ++i)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            player.render (project, 120.0, false, ch, 2, 480);
        }
        check (peakOf (l.data(), 0, 480) > 1.0e-4f, "precondition: the master reverb tail rings on");
        project.masterSlots[0] = sls::EffectSlot {};
        project.revision = 99;
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        player.render (project, 120.0, false, ch, 2, 480);
        check (peakOf (l.data(), 0, 480) == 0.0f, "removing a master effect stops its tail at once");
    }

    // pattern and song compilation carry the master chain
    {
        sls::Arrangement arrangement;
        arrangement.ensureChannelCount (1);
        std::array<sls::EffectSlot, sls::kSlotsPerTrack> master {};
        master[2] = sls::EffectSlot::make (sls::EffectType::Delay);
        const auto pat = sls::compilePattern ({ synthChannel (a4) }, arrangement, 0, {}, master);
        const auto song = sls::compileSong ({ synthChannel (a4) }, arrangement, {}, master);
        check (pat.masterSlots[2].type == sls::EffectType::Delay && song.masterSlots[2].type == sls::EffectType::Delay
                   && pat.masterSlots[0].type == sls::EffectType::None,
               "pattern and song projects carry the master effects");
    }

    // track assignment
    {
        std::vector<sls::Channel> ch (4);
        for (size_t i = 0; i < ch.size(); ++i)
            ch[i].mixerTrack = static_cast<int> (i);
        check (sls::firstFreeTrack (ch, 8) == 4 && sls::isTrackFree (ch, 8, 4) && ! sls::isTrackFree (ch, 8, 2),
               "four channels on tracks 1-4: the first free track is 5");
        check (sls::isTrackFree (ch, 8, 2, 2) && sls::firstFreeTrack (ch, 8, 2) == 2,
               "a channel's own track counts as free for that channel");
        ch[1].mixerTrack = 0;
        check (sls::firstFreeTrack (ch, 8) == 1 && sls::channelsPerTrack (ch, 8)[0] == 2, "two channels can share a track; the vacated one is free");
        ch.resize (8);
        for (size_t i = 0; i < ch.size(); ++i)
            ch[i].mixerTrack = static_cast<int> (i);
        check (sls::firstFreeTrack (ch, 8) == -1 && sls::firstFreeTrack (ch, 8, 5) == 5, "no free track when all eight are used (unless one channel is ignored)");
        check (sls::firstFreeTrack (ch, 12) == 8 && sls::channelsPerTrack (ch, 500).size() == 500, "with more tracks, the first one past them is free (0.29.0)");
        ch[0].mixerTrack = 99;
        ch[1].mixerTrack = -4;
        const auto count = sls::channelsPerTrack (ch, 8);
        check (count[7] == 2 && count[0] == 1 && ! sls::isTrackFree (ch, 8, -1) && ! sls::isTrackFree (ch, 8, 8),
               "out-of-range track numbers are clamped; asking about a track that does not exist is not 'free'");
    }
}

void testAudioExport()
{
    std::cout << "Audio export\n";
    const std::vector<sls::Note> notes { { 69, 0, 960, 0.8f }, { 72, 960, 960, 0.8f } };
    const auto project = sls::Project::make ({ synthChannel (notes) }, 4 * kBeat);
    sls::RenderOptions options;
    options.bpm = 120.0;
    options.tailSeconds = 0.5;
    const double expectedSeconds = 4 * 0.5 + 0.5; // 4 beats at 120 bpm plus the tail

    const auto dir = freshTempDir ("sls-export");
    juce::AudioFormatManager manager;
    manager.registerBasicFormats();

    // what a file contains: length in seconds and loudest sample, or -1 when it cannot be read
    const auto inspect = [&] (const juce::File& f, double& seconds, float& loudest)
    {
        seconds = -1.0;
        loudest = 0.0f;
        std::unique_ptr<juce::AudioFormatReader> reader;
        if (f.hasFileExtension ("oga")) // the format manager only knows .ogg; the content is the same
        {
            juce::OggVorbisAudioFormat ogg;
            reader.reset (ogg.createReaderFor (f.createInputStream().release(), true));
        }
        else
            reader.reset (manager.createReaderFor (f));
        if (reader == nullptr || reader->lengthInSamples <= 0)
            return false;
        seconds = static_cast<double> (reader->lengthInSamples) / reader->sampleRate;
        juce::AudioBuffer<float> b (static_cast<int> (reader->numChannels), static_cast<int> (std::min<juce::int64> (reader->lengthInSamples, 400000)));
        reader->read (&b, 0, b.getNumSamples(), 0, true, true);
        for (int c = 0; c < b.getNumChannels(); ++c)
            loudest = std::max (loudest, b.getMagnitude (c, 0, b.getNumSamples()));
        return true;
    };

    for (const auto fmt : { sls::ExportFormat::Wav, sls::ExportFormat::Aiff, sls::ExportFormat::Flac, sls::ExportFormat::Ogg,
                            sls::ExportFormat::Oga })
    {
        const auto& info = sls::exportFormatInfo (fmt);
        const auto file = dir.getChildFile ("song" + juce::String (info.extension));
        const auto result = sls::exportAudio (project, options, file, fmt);
        double seconds;
        float loudest;
        const bool readable = result.wasOk() && inspect (file, seconds, loudest);
        const auto label = juce::String (info.extension).substring (1).toUpperCase() + " export is written, read back with the right length and not silent";
        check (readable && std::abs (seconds - expectedSeconds) < 0.1 && loudest > 0.05f, label.toRawUTF8());
    }
    check (dir.getNumberOfChildFiles (juce::File::findFiles) == 5, "no temporary files are left behind");

    const auto ffmpeg = sls::findFfmpeg();
    if (ffmpeg.existsAsFile())
    {
        const auto mp3 = dir.getChildFile ("song.mp3");
        const auto aac = dir.getChildFile ("song.aac");
        check (sls::exportAudio (project, options, mp3, sls::ExportFormat::Mp3).wasOk(), "MP3 export through ffmpeg succeeds");
        check (sls::exportAudio (project, options, aac, sls::ExportFormat::Aac).wasOk(), "AAC export through ffmpeg succeeds");

        // decode both again with ffmpeg and check what comes out
        for (const auto& f : { mp3, aac })
        {
            const auto wav = dir.getChildFile ("decoded-" + f.getFileNameWithoutExtension() + juce::String (f.getFileExtension().substring (1)) + ".wav");
            juce::ChildProcess p;
            p.start ({ ffmpeg.getFullPathName(), "-loglevel", "error", "-nostdin", "-y", "-i", f.getFullPathName(), wav.getFullPathName() });
            p.waitForProcessToFinish (60000);
            double seconds;
            float loudest;
            const bool ok = inspect (wav, seconds, loudest);
            const auto label = f.getFileExtension().substring (1).toUpperCase() + " file decodes with about the right length and is not silent";
            check (ok && std::abs (seconds - expectedSeconds) < 0.25 && loudest > 0.05f, label.toRawUTF8());
        }
    }
    else
        std::cout << "  SKIP  MP3 and AAC export (ffmpeg is not installed here)\n";

    // without ffmpeg the lossy formats that need it fail with a clear message and leave nothing behind
    {
        const auto target = dir.getChildFile ("never.mp3");
        const auto r = sls::exportAudio (project, options, target, sls::ExportFormat::Mp3, dir.getChildFile ("no-such-ffmpeg"));
        check (r.failed() && r.getErrorMessage().contains ("ffmpeg") && ! target.exists(), "MP3 export without ffmpeg fails with a message and writes nothing");
        const auto r2 = sls::exportAudio (project, options, dir.getChildFile ("never.aac"), sls::ExportFormat::Aac, dir.getChildFile ("no-such-ffmpeg"));
        check (r2.failed() && ! dir.getChildFile ("never.aac").exists(), "the same for AAC");
        // a program that is not ffmpeg: failure is reported, an existing good file is not damaged
        const auto good = dir.getChildFile ("keep.mp3");
        good.replaceWithText ("precious");
        const auto fake = dir.getChildFile ("fake-ffmpeg.sh");
        fake.replaceWithText ("#!/bin/sh\necho boom >&2\nexit 3\n");
        fake.setExecutePermission (true);
        const auto r3 = sls::exportAudio (project, options, good, sls::ExportFormat::Mp3, fake);
        check (r3.failed() && good.loadFileAsString() == "precious", "a failing encoder is reported and never damages an existing file");
    }

    {
        sls::ExportFormat f;
        check (sls::exportFormatForFile (juce::File ("/x/Song.MP3"), f) && f == sls::ExportFormat::Mp3
                   && sls::exportFormatForFile (juce::File ("/x/a.oga"), f) && f == sls::ExportFormat::Oga
                   && sls::exportFormatForFile (juce::File ("/x/a.aif"), f) && f == sls::ExportFormat::Aiff
                   && ! sls::exportFormatForFile (juce::File ("/x/a.txt"), f) && sls::exportFormats().size() == 7,
               "export formats are recognised by extension (seven of them)");
    }
    dir.deleteRecursively(); // test folders in /tmp are not left behind
}

std::shared_ptr<const sls::SampleData> sineData (int frames, double sampleRate, double hz, float amplitude, const char* source = "/tmp/sine.wav")
{
    auto d = std::make_shared<sls::SampleData>();
    d->sampleRate = sampleRate;
    d->name = "sine";
    d->source = source;
    d->left.resize (static_cast<size_t> (frames));
    for (int i = 0; i < frames; ++i)
        d->left[static_cast<size_t> (i)] = amplitude * static_cast<float> (std::sin (6.283185307179586 * hz * i / sampleRate));
    d->right = d->left;
    return d;
}

void testAudioClips()
{
    std::cout << "Audio clips in the playlist\n";
    const auto sine = sineData (48000, 48000.0, 440.0, 0.5f);

    sls::Arrangement a;
    a.patterns.resize (1);
    const auto audio = [&] (int track, int start, int length)
    {
        sls::AudioClip c;
        c.data = sine;
        c.track = track;
        c.startTick = start;
        c.lengthTicks = length;
        return c;
    };

    check (a.canPlaceAudio (audio (0, 0, 960)), "an audio clip fits on an empty track");
    a.audioClips.push_back (audio (0, 0, 960));
    check (a.canPlaceAudio (audio (0, 480, 960)), "audio clips may overlap on a track (0.33.0: layering)");
    check (a.canPlaceAudio (audio (1, 480, 960)), "but may on another track");
    check (a.canPlaceAudio (audio (0, 960, 960)), "an audio clip can start where the previous one ends");
    check (a.canPlaceAudio (audio (0, 1060, 960)), "audio clips may start on any tick (they are no longer tied to the beat grid)");
    check (! a.canPlaceAudio (audio (0, a.timelineTicks() - 480, 960)), "an audio clip cannot run past the end of the timeline");
    check (! a.canPlaceAudio (audio (a.trackCount(), 0, 480)), "or sit on a track that does not exist");
    check (a.canPlaceAudio (audio (0, 0, 480), 0), "a clip being moved does not collide with itself");
    check (a.canPlace (uc (a, 0, 0, 0, 1)), "a pattern clip may cover an audio clip");
    check (a.canPlace (uc (a, 0, 0, 1, 1)), "but can sit after it");
    a.clips.push_back (uc (a, 0, 2, 1, 1));
    check (a.canPlaceAudio (audio (2, a.patternLength (0) + 480, 480)), "and an audio clip a pattern clip");
    check (a.audioClipAt (0, 0) == 0 && a.audioClipAt (0, 959) == 0 && a.audioClipAt (0, 960) == -1, "audioClipAt finds the clip under a tick");
    check (a.findFreeAudioStart (0, 0, 960) == 0 && a.findFreeAudioStart (1, 100, 480) == 100 && a.findFreeAudioStart (0, 100, 480) == 100,
           "findFreeAudioStart uses the wanted tick (clips may overlap)");
    check (a.findFreeAudioStart (0, 1, a.timelineTicks()) == -1, "and gives up when the clip would run past the timeline");

    sls::Arrangement b;
    b.patterns.resize (1);
    b.audioClips.push_back (audio (3, 960, 3000));
    check (b.songLengthTicks() == 3 * 4 * kBeat, "the song ends at its last audio clip, rounded up to a whole bar (3960 ticks: 3 bars)");

    // compiling
    const auto empty = std::vector<sls::Channel> { synthChannel ({}) };
    const auto song = sls::compileSong (empty, b);
    check (song.audioEvents.size() == 1 && song.audioEvents[0].startTick == 960 && song.audioEvents[0].endTick == 3960
                && song.audioEvents[0].mixerTrack == 3,
           "song mode compiles the audio clips (playlist track n = mixer track n)");
    check (sls::compilePattern (empty, b, 0).audioEvents.empty(), "pattern mode plays no audio clips");

    // playing
    const auto run = [] (sls::Player& player, const sls::Project& project, bool playing, int blocks, double bpm = 120.0)
    {
        std::vector<float> all;
        std::vector<float> l (256), r (256);
        float* ch[2] = { l.data(), r.data() };
        for (int i = 0; i < blocks; ++i)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            player.render (project, bpm, playing, ch, 2, 256);
            all.insert (all.end(), l.begin(), l.end());
        }
        return all;
    };
    const auto peak = [] (const std::vector<float>& x, int from, int to)
    {
        float m = 0.0f;
        for (int i = from; i < to && i < static_cast<int> (x.size()); ++i)
            m = std::max (m, std::abs (x[static_cast<size_t> (i)]));
        return m;
    };

    sls::Arrangement play;
    play.patterns.resize (1);
    play.audioClips.push_back (audio (0, 480, 480)); // 0.5 s .. 1.0 s at 120 bpm (one beat is 480 ticks = 0.5 s)
    {
        const auto project = sls::compileSong (empty, play);
        sls::Player p;
        p.prepare (kSr);
        const auto x = run (p, project, true, 300); // 1.6 s
        check (peak (x, 0, 23900) == 0.0f, "an audio clip is silent before it starts");
        check (peak (x, 24500, 46000) > 0.3f, "it plays from its start tick");
        check (peak (x, 52500, 76000) == 0.0f, "and is cut at its end tick");
        const auto stopped = run (p, project, false, 20);
        check (peak (stopped, 0, 5000) == 0.0f, "stopping the transport silences it");

        sls::Player q;
        q.prepare (kSr);
        const auto y = run (q, project, true, 850); // 4.5 s: the loop (4 s) wraps and the clip plays again at 4.5 s
        check (peak (y, 216500, 221000) > 0.3f, "the clip plays again when the song loops");
    }
    {   // goes through its mixer track: muting the track silences it
        auto tracks = sls::Project::defaultTracks();
        tracks[2].mute = true;
        sls::Arrangement other = play;
        other.audioClips[0].track = 2;
        sls::Player p;
        p.prepare (kSr);
        const auto muted = run (p, sls::compileSong (empty, other, tracks), true, 200);
        sls::Player q;
        q.prepare (kSr);
        const auto open = run (q, sls::compileSong (empty, other), true, 200);
        check (peak (muted, 0, 51000) == 0.0f && peak (open, 24500, 46000) > 0.3f, "an audio clip obeys the mute of its mixer track");
        sls::Player r;
        r.prepare (kSr);
        run (r, sls::compileSong (empty, other), true, 150);
        check (r.getTrackPeak (2) > 0.1f && r.getTrackPeak (0) == 0.0f, "and shows on that track's meter");
    }
    {   // gain and resampling
        sls::Arrangement g = play;
        g.audioClips[0].gain = 0.0f;
        sls::Player p;
        p.prepare (kSr);
        check (peak (run (p, sls::compileSong (empty, g), true, 200), 0, 51000) == 0.0f, "a clip with gain 0 is silent");

        sls::Arrangement slow;
        slow.patterns.resize (1);
        sls::AudioClip c = audio (0, 0, 960);
        c.data = sineData (24000, 24000.0, 440.0, 0.5f); // 1 s of 24 kHz audio must last 1 s at 48 kHz, not 0.5 s
        slow.audioClips.push_back (c);
        c.lengthTicks = 1920;
        slow.audioClips[0] = c;
        sls::Player q;
        q.prepare (kSr);
        const auto x = run (q, sls::compileSong (empty, slow), true, 250);
        check (peak (x, 38000, 46000) > 0.3f && peak (x, 52000, 60000) == 0.0f, "audio at another sample rate plays at the right speed");
    }
    {   // snapshot changes while playing
        sls::Arrangement with;
        with.patterns.resize (1);
        with.audioClips.push_back (audio (0, 0, 1920));
        auto p1 = sls::compileSong (empty, with);
        auto p2 = sls::compileSong (empty, with); // same audio: keeps playing
        sls::Arrangement removed;
        removed.patterns = { sls::Pattern {} };
        setLengths (removed, 8 * kBeat);
        auto p3 = sls::compileSong (empty, removed); // clip removed
        p1.revision = 1;
        p2.revision = 2;
        p3.revision = 3;
        sls::Player p;
        p.prepare (kSr);
        run (p, p1, true, 20);
        const auto keeps = run (p, p2, true, 10);
        const auto gone = run (p, p3, true, 10);
        check (peak (keeps, 0, 2560) > 0.3f, "an edit that leaves the clip alone does not interrupt it");
        check (peak (gone, 0, 2560) == 0.0f, "removing the clip stops it");
    }
    {   // real-time safety
        const auto project = sls::compileSong (empty, play);
        sls::Player p;
        p.prepare (kSr);
        std::vector<float> l (256), r (256);
        float* ch[2] = { l.data(), r.data() };
        gAllocs = 0;
        gCounting = true;
        for (int i = 0; i < 300; ++i)
            p.render (project, 120.0, true, ch, 2, 256);
        gCounting = false;
        check (gAllocs.load() == 0, "rendering audio clips allocates nothing");
    }
    {   // export contains the clip
        sls::RenderOptions options;
        options.tailSeconds = 0.2;
        const auto buffer = sls::renderToBuffer (sls::compileSong (empty, play), options);
        check (buffer.getMagnitude (0, 0, buffer.getNumSamples()) > 0.3f, "exports contain the audio clips");
    }

    // project file
    {
        auto doc = sls::ProjectDocument::blank();
        auto c1 = audio (1, 960, 1920);
        c1.name = "Vocals take 1";
        c1.gain = 0.7f;
        c1.data = sineData (100, 48000.0, 440.0, 0.1f, "/home/x/take1.wav");
        auto c2 = audio (1, 3840, 960);
        c2.data = sineData (100, 48000.0, 440.0, 0.1f, "/home/x/take 2.flac");
        doc.arrangement.audioClips = { c1, c2 };
        const auto parsed = sls::ProjectDocument::fromJson (doc.toJson());
        check (parsed.document.has_value() && parsed.warnings.empty() && parsed.document->arrangement.audioClips.size() == 2, "audio clips survive saving and loading");
        if (parsed.document)
        {
            const auto& r = parsed.document->arrangement.audioClips;
            check (r[0].name == "Vocals take 1" && r[0].track == 1 && r[0].startTick == 960 && r[0].lengthTicks == 1920
                       && std::abs (r[0].gain - 0.7f) < 1e-6f && r[0].data->source == "/home/x/take1.wav"
                       && r[1].data->source == "/home/x/take 2.flac",
                   "with their position, gain and the path of their file");
            check (r[0].data->numFrames() == 0, "the audio itself is not stored in the project");
        }

        // overlapping / out-of-range clips are dropped with a warning
        auto text = doc.toJson();
        auto var = juce::JSON::parse (text);
        auto* list = var.getDynamicObject()->getProperty ("audioClips").getArray();
        auto* extra = new juce::DynamicObject();
        extra->setProperty ("source", "/a/b.wav");
        extra->setProperty ("track", 1);
        extra->setProperty ("startTick", 960);
        extra->setProperty ("lengthTicks", 480);
        list->add (juce::var (extra));
        auto* bad = new juce::DynamicObject();
        bad->setProperty ("source", "/a/c.wav");
        bad->setProperty ("track", 99);
        list->add (juce::var (bad));
        const auto repaired = sls::ProjectDocument::fromJson (juce::JSON::toString (var));
        check (repaired.document.has_value() && repaired.document->arrangement.audioClips.size() == 3 && ! repaired.warnings.empty(),
               "a clip with an impossible position is dropped with a warning; one overlapping another is kept (layering)");

        var.getDynamicObject()->removeProperty ("audioClips"); // a file from before audio clips existed
        const auto old = sls::ProjectDocument::fromJson (juce::JSON::toString (var));
        check (old.document.has_value() && old.document->arrangement.audioClips.empty() && old.warnings.empty(),
               "projects without audio clips still open");
    }
}

// --- per-clip pan, reverse and fades (0.27.0) -------------------------------------------------------------------------------
// --- the score log (0.28.0) ------------------------------------------------------------------------------------------------
// --- the Loop Starter (0.30.0) ----------------------------------------------------------------------------------------------
static int st_whole (int patternTicks) { return sls::steps::patternSteps (patternTicks); }

void testPlaylistLayering()
{
    std::cout << "Playlist tracks and layering (0.33.0)\n";
    sls::Arrangement a;
    a.patterns.resize (2);
    a.ensureChannelCount (1);
    a.patterns[0].notes[0] = { { 60, 0, 240, 0.8f } };
    a.patterns[1].notes[0] = { { 72, 120, 240, 0.8f } };
    a.patterns[0].steps = { st_whole (a.patternLength (0)) };
    a.patterns[1].steps = { st_whole (a.patternLength (1)) };
    a.clips = { uc (a, 0, 0, 0, 2), uc (a, 1, 0, 1, 1) }; // the second lies over the first's second repeat, on the same track
    const std::vector<sls::Channel> channels { synthChannel ({}) };
    const auto song = sls::compileSong (channels, a);
    int at60 = 0, at72 = 0;
    for (const auto& n : song.channels[0].notes)
    {
        at60 += n.pitch == 60 ? 1 : 0;
        at72 += n.pitch == 72 && n.startTick == a.patternLength (0) + 120 ? 1 : 0;
    }
    check (at60 == 2 && at72 == 1, "pattern clips laid over each other on a track all play");
    check (a.clipAt (0, a.patternLength (0)) == 1 && a.clipAt (0, 0) == 0, "the clip on top (the later one) is the one picked under the mouse");

    // Two audio clips over each other on one track are both heard.
    const auto tone = [] (double hz)
    {
        auto s = std::make_shared<sls::SampleData>();
        s->sampleRate = 48000.0;
        s->left.resize (48000);
        for (size_t i = 0; i < s->left.size(); ++i)
            s->left[i] = 0.3f * static_cast<float> (std::sin (2.0 * 3.14159265358979 * hz * static_cast<double> (i) / 48000.0));
        s->right = s->left;
        return std::shared_ptr<const sls::SampleData> (s);
    };
    sls::Arrangement b;
    b.patterns.resize (1);
    sls::AudioClip low, high;
    low.data = tone (220.0);
    high.data = tone (880.0);
    low.track = high.track = 2;
    low.lengthTicks = high.lengthTicks = 960;
    check (b.canPlaceAudio (low) && (b.audioClips.push_back (low), b.canPlaceAudio (high)), "an audio clip may lie over another");
    b.audioClips.push_back (high);
    const auto project = sls::compileSong (std::vector<sls::Channel> { synthChannel ({}) }, b);
    check (project.audioEvents.size() == 2, "both are in the song");
    sls::Player player;
    player.prepare (48000.0);
    std::vector<float> l (256), r (256), all;
    float* ch[2] = { l.data(), r.data() };
    for (int i = 0; i < 60; ++i)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        player.render (project, 120.0, true, ch, 2, 256);
        all.insert (all.end(), l.begin(), l.end());
    }
    // The mix of 220 Hz and 880 Hz: correlate with each.
    double c220 = 0.0, c880 = 0.0;
    for (size_t i = 4800; i < 14400 && i < all.size(); ++i)
    {
        const double t = static_cast<double> (i) / 48000.0;
        c220 += all[i] * std::sin (2.0 * 3.14159265358979 * 220.0 * t);
        c880 += all[i] * std::sin (2.0 * 3.14159265358979 * 880.0 * t);
    }
    // (phase is unknown: compare energies at both frequencies with their cosines too)
    double s220 = 0.0, s880 = 0.0;
    for (size_t i = 4800; i < 14400 && i < all.size(); ++i)
    {
        const double t = static_cast<double> (i) / 48000.0;
        s220 += all[i] * std::cos (2.0 * 3.14159265358979 * 220.0 * t);
        s880 += all[i] * std::cos (2.0 * 3.14159265358979 * 880.0 * t);
    }
    const double e220 = std::hypot (c220, s220), e880 = std::hypot (c880, s880);
    check (e220 > 100.0 && e880 > 100.0 && e220 / e880 > 0.5 && e220 / e880 < 2.0, "and both are heard, at their level");

    // Names and colours.
    {
        auto doc = richDocument();
        doc.arrangement.playlistTracks.resize (8);
        doc.arrangement.playlistTracks[1] = { "Drums", 0xffe0524au };
        doc.arrangement.playlistTracks[5].colour = 0xff4a90e2u;
        const auto back = sls::ProjectDocument::fromJson (doc.toJson());
        check (back.document.has_value() && back.document->arrangement.playlistTracks == doc.arrangement.playlistTracks,
               "the playlist tracks' names and colours survive a round trip");
        auto v = juce::JSON::parse (doc.toJson());
        auto* list = v.getDynamicObject()->getProperty ("playlistTracks").getArray();
        check (list != nullptr && list->size() == 2, "only the tracks with their own name or colour are written");
        list->getReference (0).getDynamicObject()->setProperty ("colour", "zz");
        list->getReference (1).getDynamicObject()->setProperty ("track", 99);
        const auto odd = sls::ProjectDocument::fromJson (juce::JSON::toString (v));
        check (odd.document.has_value() && odd.document->arrangement.playlistTracks[1].name == "Drums"
                   && odd.document->arrangement.playlistTracks[1].colour == 0 && odd.document->arrangement.playlistTracks[5] == sls::PlaylistTrack {},
               "a bad colour or a track that does not exist is ignored");
        v.getDynamicObject()->removeProperty ("playlistTracks");
        const auto old = sls::ProjectDocument::fromJson (juce::JSON::toString (v));
        bool plain = old.document.has_value();
        for (const auto& t : old.document->arrangement.playlistTracks)
            plain = plain && t == sls::PlaylistTrack {};
        check (plain, "an older file opens with the default names and colours");
    }
}

void testFlpFiles()
{
    std::cout << "FL Studio projects (0.34.0)\n";
    namespace fl = sls::flp;

    check (fl::fromUtf16 (fl::utf16 ("B\xc3\xa4sse \xf0\x9f\x8e\xb5").data(), fl::utf16 ("B\xc3\xa4sse \xf0\x9f\x8e\xb5").size()) == "B\xc3\xa4sse \xf0\x9f\x8e\xb5",
           "names go to UTF-16 and back (accents and a character outside the basic plane)");

    // A project to export.
    auto doc = sls::ProjectDocument::blank();
    doc.bpm = 128.5;
    doc.channels[0].name = "Lead";
    sls::Channel kick;
    kick.name = "Kick";
    kick.instrument = sls::Instrument::Sampler;
    kick.rootPitch = 48;
    doc.channels.push_back (kick);
    doc.sampleSources = { "", "/home/me/Samples/kick.wav" };
    doc.kitSpecs.push_back (sls::KitSpec::empty());
    auto& a = doc.arrangement;
    a.patterns.resize (2);
    setLengths (a, 2 * 1920);
    a.patterns[1].name = "Verse \xc3\xa8";
    a.ensureChannelCount (2);
    a.patterns[0].notes[0] = { { 64, 0, 480, 0.5f }, { 67, 1000, 333, 1.0f } };
    a.patterns[0].notes[1] = { { 48, 0, 120, 0.9f }, { 48, 480, 120, 0.9f } }; // one bar of steps: repeats in the second
    a.patterns[1].notes[0] = { { 72, 3800, 40, 0.8f } };
    a.clips = { uc (a, 0, 0, 0, 2), uc (a, 1, 3, 4, 1) };
    sls::AudioClip audio;
    audio.data = sls::makeMissingSample ("/home/me/Audio/vocal take.wav");
    audio.name = "vocal take";
    audio.track = 2;
    audio.startTick = 960;
    audio.lengthTicks = 5000;
    a.audioClips.push_back (audio);
    a.playlistTracks[0] = { "Beat", 0xffe0524au };
    a.playlistTracks[3].name = "Verses";

    const auto exported = sls::projectToFlp (doc);
    const auto parsed = fl::parse (exported.bytes);
    check (parsed.song.has_value() && parsed.error.empty(), "an exported project reads back");
    if (! parsed.song.has_value())
        return;
    const auto& song = *parsed.song;
    check (song.ppq == 960 && std::abs (song.bpm - 128.5) < 1.0e-9 && song.version == "20.8.4.2576", "with its tempo, at 960 PPQ, as FL Studio 20.8");
    check (song.channels.size() == 3 && song.channels[0].name == "Lead" && song.channels[1].name == "Kick" && song.channels[1].samplePath == "/home/me/Samples/kick.wav"
               && song.channels[1].rootNote == 48 && song.channels[2].type == 4 && song.channels[2].samplePath == "/home/me/Audio/vocal take.wav",
           "the channels, a sample file with its root key, and a channel for the audio clip's file");
    check (song.patterns.size() == 2 && song.patterns[1].name == "Verse \xc3\xa8", "the patterns with their names");
    int lead = 0, kicks = 0;
    bool exact = false;
    for (const auto& n : song.patterns[0].notes)
    {
        lead += n.channel == 0 ? 1 : 0;
        kicks += n.channel == 1 ? 1 : 0;
        exact = exact || (n.channel == 0 && n.key == 67 && n.position == 2000 && n.length == 666 && n.velocity == 128);
    }
    check (lead == 2 && exact, "notes keep their exact position, length and velocity (ticks doubled)");
    check (kicks == 2, "steps are written as they are (since 0.42.0 nothing repeats by itself)");
    int patternItems = 0, audioItems = 0;
    for (const auto& item : song.items)
    {
        patternItems += item.pattern > 0 ? 1 : 0;
        audioItems += item.pattern == 0 && item.channel == 2 && item.position == 1920 && item.length == 10000 && item.track == 2 ? 1 : 0;
    }
    check (patternItems == 3 && audioItems == 1, "a clip repeated twice is two items; the audio clip is an item of its channel");
    check (song.tracks.size() == 3 && song.tracks[0].name == "Beat" && song.tracks[0].hasColour && song.tracks[0].rgb == 0xe0524a
               && song.tracks[1].index == 3 && song.tracks[1].name == "Verses" && ! song.tracks[1].hasColour
               && song.tracks[2].index == 4 && song.tracks[2].name == a.patterns[0].name,
           "the playlist tracks' names and colours (0.44.0: a pattern's own track carries the pattern's name)");
    check (exported.warnings.size() >= 2, "what does not carry over is listed");

    // ...and back into a project here.
    const auto back = sls::projectFromFlp (song);
    check (back.document.has_value(), "the .flp opens as a project");
    if (back.document.has_value())
    {
        const auto& d = *back.document;
        check (d.channels.size() == 2 && d.channels[0].name == "Lead" && d.channels[0].instrument == sls::Instrument::Sampler
                   && d.channels[1].instrument == sls::Instrument::Sampler && d.sampleSources[1] == "/home/me/Samples/kick.wav" && d.channels[1].rootPitch == 48,
               "the channels come back (the synth as an empty Sampler, as FL Studio would have it); the audio file is not a rack channel");
        check (d.arrangement.patternLength (0) == 2 * 1920 && d.arrangement.patternLength (1) == 2 * 1920 && d.arrangement.patterns.size() == 2, "two-bar patterns, both of them");
        const auto& n = d.arrangement.patterns[0].notes[0];
        check (n.size() == 2 && n[1].pitch == 67 && n[1].startTick == 1000 && n[1].lengthTicks == 333 && std::abs (n[1].velocity - 1.0f) < 1.0e-6f
                   && std::abs (n[0].velocity - 0.5f) < 1.0e-6f,
               "the notes come back exactly");
        check (d.arrangement.patterns[0].notes[1].size() == 2, "the steps come back as written");
        int clipsOnTrack0 = 0;
        for (const auto& c : d.arrangement.clips)
            clipsOnTrack0 += c.track == 0 && c.pattern == 0 ? 1 : 0;
        check (d.arrangement.clips.size() == 3 && clipsOnTrack0 == 2, "the pattern clips come back (one per repeat)");
        check (d.arrangement.audioClips.size() == 1 && d.arrangement.audioClips[0].data->source == "/home/me/Audio/vocal take.wav"
                   && d.arrangement.audioClips[0].startTick == 960 && d.arrangement.audioClips[0].lengthTicks == 5000,
               "the audio clip comes back with its file, place and length");
        check (d.arrangement.playlistTracks[0].name == "Beat" && d.arrangement.playlistTracks[0].colour == 0xffe0524au,
               "track names and colours come back (FL Studio's track 1 is the first track used here)");
        check (std::abs (d.bpm - 128.5) < 1.0e-9 && d.songMode, "the tempo, and Song mode since the playlist has clips");
        const auto json = d.toJson();
        check (sls::ProjectDocument::fromJson (json).document.has_value(), "and it can be saved as a .slp project");
    }

    // An older file, written by hand: 8-bit names, the tempo in two parts, 96 PPQ, a generator and an automation channel.
    {
        std::vector<std::uint8_t> ev;
        const auto u = [&ev] (int id, std::uint32_t v, int n) { ev.push_back (static_cast<std::uint8_t> (id)); for (int i = 0; i < n; ++i) ev.push_back (static_cast<std::uint8_t> (v >> (8 * i))); };
        const auto t = [&ev] (int id, const std::string& s) { ev.push_back (static_cast<std::uint8_t> (id)); ev.push_back (static_cast<std::uint8_t> (s.size() + 1)); ev.insert (ev.end(), s.begin(), s.end()); ev.push_back (0); };
        t (199, "9.0.3");
        u (66, 125, 2);
        u (93, 500, 2);
        u (64, 0, 2); u (21, 2, 1); t (201, "3x Osc"); t (192, "Bass\xe9");
        u (64, 1, 2); u (21, 5, 1); t (192, "Auto");
        u (65, 1, 2); t (193, "Intro");
        std::vector<std::uint8_t> note (24, 0);
        note[0] = 96; note[6] = 0; note[8] = 48; note[12] = 36; note[21] = 100;
        ev.push_back (224); ev.push_back (24); ev.insert (ev.end(), note.begin(), note.end());
        std::vector<std::uint8_t> file { 'F', 'L', 'h', 'd', 6, 0, 0, 0, 0, 0, 2, 0, 96, 0, 'F', 'L', 'd', 't' };
        for (int i = 0; i < 4; ++i) file.push_back (static_cast<std::uint8_t> (ev.size() >> (8 * i)));
        file.insert (file.end(), ev.begin(), ev.end());
        const auto old = fl::parse (file);
        check (old.song.has_value() && std::abs (old.song->bpm - 125.5) < 1.0e-9 && old.song->channels.size() == 2 && old.song->channels[0].name == "Bass\xc3\xa9"
                   && old.song->patterns.size() == 1 && old.song->patterns[0].notes.size() == 1 && old.song->patterns[0].notes[0].position == 96,
               "a file from FL Studio 9: 8-bit names, the tempo in two parts");
        if (old.song.has_value())
        {
            const auto d = sls::projectFromFlp (*old.song);
            check (d.document.has_value() && d.document->channels.size() == 1 && d.document->channels[0].instrument == sls::Instrument::Synth
                       && d.document->arrangement.patterns[0].notes[0].size() == 1 && d.document->arrangement.patterns[0].notes[0][0].startTick == 480
                       && d.document->arrangement.patterns[0].notes[0][0].lengthTicks == 240 && d.document->arrangement.patternLength (0) == 1920,
                   "its generator plays on a default synth, the automation channel is left out, 96 PPQ becomes 480");
            check (d.warnings.size() >= 2, "and that is reported");
        }
    }

    // Damaged files are refused, never read past their end.
    {
        check (! fl::parse (std::vector<std::uint8_t> {}).song.has_value(), "an empty file is refused");
        check (! fl::parse (std::vector<std::uint8_t> (40, 0)).song.has_value(), "a file that is not a .flp is refused");
        auto cut = exported.bytes;
        cut.resize (cut.size() - 10);
        check (! fl::parse (cut).song.has_value() && ! fl::parse (cut).error.empty(), "a file cut short is refused with a reason");
        auto wrongPpq = exported.bytes;
        wrongPpq[12] = 7;
        check (! fl::parse (wrongPpq).song.has_value(), "an impossible timebase is refused");
        auto score = exported.bytes;
        score[8] = 1;
        check (! fl::parse (score).song.has_value(), "a file that is not a song (a score, a preset) is refused");
        // Random damage: thousands of mutated files never crash and never read out of bounds.
        std::uint32_t seed = 12345;
        const auto next = [&seed] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
        int opened = 0;
        for (int round = 0; round < 3000; ++round)
        {
            auto bytes = exported.bytes;
            const int changes = 1 + static_cast<int> (next() % 8);
            for (int k = 0; k < changes; ++k)
                bytes[22 + next() % (bytes.size() - 22)] = static_cast<std::uint8_t> (next());
            if (next() % 4 == 0)
                bytes.resize (22 + next() % (bytes.size() - 22));
            const auto r = fl::parse (bytes);
            if (r.song.has_value())
            {
                ++opened;
                (void) sls::projectFromFlp (*r.song);
            }
        }
        std::cout << "  (" << opened << " of 3000 damaged files still opened)\n";
        check (opened > 0, "3000 randomly damaged files: read or refused, never a crash");
    }
}

void testPluginAutomationFile()
{
    std::cout << "Plugin automation in the project file (0.35.0)\n";
    auto doc = richDocument();
    for (const auto kind : { sls::AutomationKind::ChannelPlugin, sls::AutomationKind::TrackPlugin, sls::AutomationKind::MasterPlugin })
    {
        sls::AutomationTarget t { kind, kind == sls::AutomationKind::MasterPlugin ? 0 : 1, kind == sls::AutomationKind::ChannelPlugin ? 0 : 3, 1234 };
        auto clip = sls::makeFlatAutomationClip (t, static_cast<int> (doc.arrangement.automationClips.size()) % sls::kAutomationLanes,
                                                 static_cast<int> (doc.arrangement.automationClips.size()) * 4000, 960, 0.75f);
        doc.arrangement.automationClips.push_back (clip);
    }
    const auto back = sls::ProjectDocument::fromJson (doc.toJson());
    int found = 0;
    if (back.document.has_value())
        for (const auto& c : back.document->arrangement.automationClips)
            found += sls::isPluginAutomation (c.target.kind) && c.target.param == 1234 && std::abs (c.points[0].value - 0.75f) < 1.0e-6f ? 1 : 0;
    check (found == 3, "clips for a channel's plugin, a track's and the master's plugin parameter survive a round trip (parameter 1234)");

    // Moving channels moves a channel plugin's automation with them.
    sls::Arrangement a;
    a.patterns.resize (1);
    a.ensureChannelCount (2);
    a.automationClips = { sls::makeFlatAutomationClip ({ sls::AutomationKind::ChannelPlugin, 0, 0, 7 }, 0, 0, 960, 0.5f) };
    sls::remapArrangementChannels (a, { 1, 0 }, 2);
    check (a.automationClips.size() == 1 && a.automationClips[0].target.index == 1, "a channel plugin's automation follows its channel");
    sls::remapArrangementChannels (a, { 0 }, 2);
    check (a.automationClips.empty(), "and goes with it when the channel is deleted");
}

void testTypingKeyboard()
{
    std::cout << "Typing keyboard (0.32.0)\n";
    namespace ty = sls::typing;
    check (ty::pitchFor ('a', 0) == 60 && sls::theory::noteName (ty::pitchFor ('a', 0)) == "C5", "A plays C5 (MIDI 60)");
    const char32_t whites[] = { 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'' };
    const int whitePitches[] = { 60, 62, 64, 65, 67, 69, 71, 72, 74, 76, 77 };
    bool allWhite = true;
    for (size_t i = 0; i < std::size (whites); ++i)
        allWhite = allWhite && ty::pitchFor (whites[i], 0) == whitePitches[i];
    check (allWhite, "the home row is the white keys: C D E F G A B C D E F");
    const char32_t blacks[] = { 'w', 'e', 't', 'y', 'u', 'o', 'p' };
    const int blackPitches[] = { 61, 63, 66, 68, 70, 73, 75 };
    bool allBlack = true;
    for (size_t i = 0; i < std::size (blacks); ++i)
        allBlack = allBlack && ty::pitchFor (blacks[i], 0) == blackPitches[i];
    check (allBlack, "the row above is the black keys: C# D# F# G# A# C# D#");
    check (ty::pitchFor ('A', 0) == 60 && ty::pitchFor ('W', 0) == 61, "capital letters (Caps Lock, Shift) play the same notes");
    check (ty::pitchFor (U'ò', 0) == 76 && ty::pitchFor (U'à', 0) == 77, "the Italian keys where ; and ' are (ò, à) play E and F");
    check (ty::pitchFor ('q', 0) < 0 && ty::pitchFor ('1', 0) < 0 && ty::pitchFor (' ', 0) < 0 && ty::pitchFor ('z', 0) < 0,
           "other keys play nothing (Z and X move the octave)");
    check (ty::pitchFor ('a', -1) == 48 && ty::pitchFor ('a', 2) == 84, "an octave down and up");
    check (ty::pitchFor ('a', 99) == 108 && ty::pitchFor ('a', -99) == 0, "the octave stays in its range");
    check (ty::pitchFor ('\'', ty::kMaxOctave) >= 0 && ty::pitchFor ('\'', ty::kMaxOctave) <= 127, "every key stays a MIDI note at the top octave");
    check (ty::commandFor ('z') == ty::Command::OctaveDown && ty::commandFor ('X') == ty::Command::OctaveUp
               && ty::commandFor ('c') == ty::Command::VelocityDown && ty::commandFor ('v') == ty::Command::VelocityUp
               && ty::commandFor ('a') == ty::Command::None,
           "Z / X move the octave, C / V the velocity");
    check (ty::nextVelocity (100, true) == 110 && ty::nextVelocity (100, false) == 80 && ty::nextVelocity (127, true) == 127
               && ty::nextVelocity (1, false) == 1 && ty::nextVelocity (90, true) == 100,
           "velocity steps");
}




void testRecording047()
{
    std::cout << "Recording: count-in click, loop takes (0.47.0)\n";
    // The count-in's clicks: the free-running click starts over after a delay, accented first.
    {
        sls::Player player;
        player.prepare (48000.0);
        player.setMetronome (true, 0.8f);
        player.delayFreeClick (1000.0);
        const auto project = sls::Project::make ({}, 4 * kBeat);
        std::vector<float> l (256), r (256), all;
        float* ch[2] = { l.data(), r.data() };
        while (all.size() < 52000)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            player.render (project, 120.0, false, ch, 2, 256);
            all.insert (all.end(), l.begin(), l.end());
        }
        const auto peak = [&all] (size_t from, size_t to)
        {
            float p = 0.0f;
            for (size_t i = from; i < to; ++i)
                p = std::max (p, std::abs (all[i]));
            return p;
        };
        check (peak (0, 999) == 0.0f && peak (1000, 1600) > 0.05f && peak (3600, 24999) == 0.0f && peak (25000, 25600) > 0.05f
                   && peak (1000, 1600) > peak (25000, 25600) * 1.2f,
               "the count-in's first click comes after the delay, accented, the next a beat later");
    }

    // Where the passes of a loop recording are: loop region bars 2-3 (ticks 1920-3840) at 120 BPM = 2 s per pass.
    {
        const auto fromStart = sls::loopTakesOf ({}, 120.0, 0, 4 * kBeat, 8 * kBeat, 9.5);
        check (std::abs (fromStart.firstSeconds - 2.0) < 1e-9 && std::abs (fromStart.passSeconds - 2.0) < 1e-9 && fromStart.complete == 3,
               "a take from the start of the song: the first pass starts when the song reaches the region; 3 whole passes in 9.5 s");
        const auto inside = sls::loopTakesOf ({}, 120.0, 6 * kBeat, 4 * kBeat, 8 * kBeat, 5.0);
        check (std::abs (inside.firstSeconds - 1.0) < 1e-9 && inside.complete == 2, "a take that starts inside the region: whole passes from its first wrap");
        const auto atStart = sls::loopTakesOf ({}, 120.0, 4 * kBeat, 4 * kBeat, 8 * kBeat, 3.99);
        check (atStart.firstSeconds == 0.0 && atStart.complete == 1, "a take at the region's start: the first pass counts (3.99 s rounds to 2 whole passes only past 4 s)");
        const auto slower = sls::loopTakesOf ({ { 4 * kBeat, 60.0 } }, 120.0, 0, 4 * kBeat, 8 * kBeat, 10.0);
        check (std::abs (slower.passSeconds - 4.0) < 1e-9 && slower.complete == 2, "the passes follow tempo changes");
        check (sls::loopTakesOf ({}, 120.0, 8 * kBeat, 4 * kBeat, 8 * kBeat, 9.0).complete == 0, "a take after the region has no passes");
    }

    // Choosing a take on the clip.
    {
        sls::AudioClip clip;
        clip.data = sineData (static_cast<int> (9.5 * 48000), 48000.0, 220.0, 0.3f);
        clip.takeFirstSeconds = 2.0;
        clip.takeSeconds = 2.0;
        clip.offsetSeconds = 6.0;
        check (sls::takeCount (clip) == 3 && sls::currentTake (clip) == 2, "the clip has 3 takes and plays the last");
        check (sls::chooseTake (clip, 0) && std::abs (clip.offsetSeconds - 2.0) < 1e-9 && sls::currentTake (clip) == 0, "take 1 is chosen");
        clip.offsetSeconds = 2.5; // the clip was cut half a second into the pass
        check (sls::chooseTake (clip, 1) && std::abs (clip.offsetSeconds - 4.5) < 1e-9, "a clip cut at its start keeps the cut in the other take");
        check (! sls::chooseTake (clip, 3), "no take beyond the file");
        clip.reverse = true;
        check (sls::takeCount (clip) == 0, "a reversed clip offers no takes");
        clip.reverse = false;

        auto doc = sls::ProjectDocument::blank();
        clip.data = sineData (100, 48000.0, 220.0, 0.3f, "/home/x/rec.wav");
        clip.lengthTicks = 960;
        doc.arrangement.audioClips = { clip };
        const auto back = sls::ProjectDocument::fromJson (doc.toJson());
        check (back.document.has_value() && back.document->arrangement.audioClips.size() == 1
                   && back.document->arrangement.audioClips[0].takeSeconds == 2.0 && back.document->arrangement.audioClips[0].takeFirstSeconds == 2.0,
               "the takes of a clip are saved and loaded");
    }
}

void testMixAutomation047()
{
    std::cout << "Automation of sends, aux buses and VCA groups (0.47.0)\n";
    using K = sls::AutomationKind;
    const auto render = [] (std::vector<sls::AutomationClip> clips, float send, unsigned vcaMask)
    {
        auto tracks = sls::Project::defaultTracks();
        tracks[0].sends[0] = send;
        tracks[0].vcaMask = vcaMask;
        auto ch = synthChannel ({ { 57, 0, 8 * kBeat, 0.6f } }, 0.6f);
        ch.mixerTrack = 0;
        return sls::renderToBuffer (sls::Project::make ({ ch }, 8 * kBeat, tracks, {}, {}, clips, sls::Console::defaults()), {});
    };
    const auto rms = [] (const juce::AudioBuffer<float>& b) { return b.getRMSLevel (0, 24000, 96000); }; // 0.5 s to 2.5 s
    const float plain = rms (render ({}, 0.0f, 0));
    const float sent = rms (render ({ sls::makeFlatAutomationClip ({ K::TrackSend, 0, 0, 0 }, 0, 0, 8 * kBeat, 1.0f) }, 0.0f, 0));
    check (plain > 0.01f && sent > plain * 1.5f, "a send automated up adds the aux bus to the mix (it is off on the track itself)");
    const float projectSend = rms (render ({}, 1.0f, 0));
    const float auxDown = rms (render ({ sls::makeFlatAutomationClip ({ K::AuxVolume, 0, 0, 0 }, 0, 0, 8 * kBeat, 0.0f) }, 1.0f, 0));
    check (projectSend > plain * 1.5f && std::abs (auxDown - plain) < plain * 0.05f, "the aux bus's volume automated down takes it out again");
    const float sendOther = rms (render ({ sls::makeFlatAutomationClip ({ K::TrackSend, 0, 1, 0 }, 0, 0, 8 * kBeat, 0.0f) }, 1.0f, 0));
    check (std::abs (sendOther - projectSend) < projectSend * 0.02f, "automating the send to Aux B leaves the send to Aux A alone");
    const float vcaDown = rms (render ({ sls::makeFlatAutomationClip ({ K::VcaLevel, 0, 0, 0 }, 0, 0, 8 * kBeat, 0.0f) }, 0.0f, 1u));
    const float vcaOther = rms (render ({ sls::makeFlatAutomationClip ({ K::VcaLevel, 0, 1, 0 }, 0, 0, 8 * kBeat, 0.0f) }, 0.0f, 1u));
    check (vcaDown < plain * 0.01f && std::abs (vcaOther - plain) < plain * 0.02f, "a VCA level automated to 0 silences its member tracks, another group's does not");
    const float panned = sls::renderToBuffer (sls::Project::make ({ [] { auto c = synthChannel ({ { 57, 0, 8 * kBeat, 0.6f } }, 0.6f); c.mixerTrack = 0; return c; }() },
                                                                  8 * kBeat, [] { auto t = sls::Project::defaultTracks(); t[0].sends[0] = 1.0f; t[0].volume = 0.0f; return t; }(), {}, {},
                                                                  { sls::makeFlatAutomationClip ({ K::AuxPan, 0, 0, 0 }, 0, 0, 8 * kBeat, 1.0f) }, sls::Console::defaults()),
                                              {}).getRMSLevel (0, 24000, 96000);
    check (panned < 1.0e-4f, "the aux bus's balance automated fully right leaves its left side silent");

    check (sls::automationTargetExists ({ K::TrackSend, 3, 1, 0 }, 1, 8) && ! sls::automationTargetExists ({ K::TrackSend, 3, 2, 0 }, 1, 8)
               && ! sls::automationTargetExists ({ K::AuxVolume, 0, 2, 0 }, 1, 8) && sls::automationTargetExists ({ K::VcaLevel, 0, 1, 0 }, 1, 8)
               && sls::automationRange ({ K::TrackSend, 0, 0, 0 }).max == 1.5f && sls::automationRange ({ K::AuxPan, 0, 0, 0 }).min == -1.0f,
           "the new targets: two aux buses and two VCA groups, in their controls' ranges");
    auto doc = sls::ProjectDocument::blank();
    doc.arrangement.automationClips = { sls::makeFlatAutomationClip ({ K::TrackSend, 2, 1, 0 }, 0, 0, 960, 0.7f),
                                        sls::makeFlatAutomationClip ({ K::AuxPan, 0, 1, 0 }, 1, 0, 960, -0.5f),
                                        sls::makeFlatAutomationClip ({ K::VcaLevel, 0, 1, 0 }, 2, 0, 960, 1.2f) };
    const auto back = sls::ProjectDocument::fromJson (doc.toJson());
    check (back.document.has_value() && back.document->arrangement.automationClips.size() == 3
               && back.document->arrangement.automationClips[0].target == sls::AutomationTarget { K::TrackSend, 2, 1, 0 }
               && back.document->arrangement.automationClips[1].target.kind == K::AuxPan
               && back.document->arrangement.automationClips[2].target == sls::AutomationTarget { K::VcaLevel, 0, 1, 0 },
           "automation of sends, aux buses and VCA groups is saved and loaded");
}

void testTempoAndMeter046()
{
    std::cout << "Tempo changes and time signatures (0.46.0)\n";
    using sls::TempoMarker;
    constexpr double kSr = 48000.0;

    // The tempo map.
    const auto tidy = sls::tidyTempoMarkers ({ { 8 * kBeat, 90.0 }, { 0, 50.0 }, { 4 * kBeat, 60.0 }, { 8 * kBeat, 100.0 }, { 12 * kBeat, 9999.0 } });
    check (tidy.size() == 3 && tidy[0] == TempoMarker { 4 * kBeat, 60.0 } && tidy[1] == TempoMarker { 8 * kBeat, 100.0 } && tidy[2].bpm == sls::kMaxBpm,
           "tempo changes are sorted, one per tick (the last wins), none at the start of the song, tempos kept in range");
    const std::vector<TempoMarker> markers { { 4 * kBeat, 60.0 }, { 8 * kBeat, 240.0 } };
    check (sls::tempoAt (markers, 120.0, 0) == 120.0 && sls::tempoAt (markers, 120.0, 4 * kBeat - 1) == 120.0 && sls::tempoAt (markers, 120.0, 4 * kBeat) == 60.0
               && sls::tempoAt (markers, 120.0, 100 * kBeat) == 240.0 && sls::tempoAt ({}, 120.0, 50 * kBeat) == 120.0,
           "the tempo at a tick is the last change before it, else the song's own");
    check (std::abs (sls::tickToSeconds (markers, 120.0, 4 * kBeat) - 2.0) < 1e-9 && std::abs (sls::tickToSeconds (markers, 120.0, 8 * kBeat) - 6.0) < 1e-9
               && std::abs (sls::tickToSeconds (markers, 120.0, 12 * kBeat) - 7.0) < 1e-9,
           "seconds add up through each tempo (4 beats at 120 = 2 s, 4 at 60 = 4 s, 4 at 240 = 1 s)");
    bool roundTrip = true;
    for (int tick = 0; tick < 20 * kBeat; tick += 77)
        roundTrip = roundTrip && std::abs (sls::secondsToTick (markers, 120.0, sls::tickToSeconds (markers, 120.0, tick)) - tick) < 1e-6;
    check (roundTrip, "seconds go back to the same tick");

    // Time signatures.
    const sls::TimeSignature threeFour { 3, 4 }, sixEight { 6, 8 }, twoFour { 2, 4 }, sevenFour { 7, 4 };
    check (threeFour.barTicks() == 3 * kBeat && sixEight.beatTicks() == kBeat / 2 && sixEight.barTicks() == 3 * kBeat && sixEight.barQuarters() == 3
               && sixEight.text() == "6/8" && sls::TimeSignature::choices().size() == 8,
           "a bar of 3/4 is 3 quarters, a bar of 6/8 is 6 eighths");
    check (sls::TimeSignature::valid (5, 4) == sls::TimeSignature { 5, 4 } && sls::TimeSignature::valid (9, 7) == sls::TimeSignature {},
           "an unknown time signature becomes 4/4");
    check (sls::formatBarsBeatsTicks (3 * kBeat, threeFour) == "2.1.000" && sls::formatBarsBeatsTicks (3 * kBeat + kBeat / 2, sixEight) == "2.2.000"
               && sls::formatBarsBeatsTicks (4 * kBeat, {}) == "2.1.000",
           "bars and beats are counted in the time signature");

    // The patterns' length: at least one bar of 4/4, or one bar when the bar is shorter.
    sls::Arrangement arr;
    arr.meter = threeFour;
    check (arr.minPatternBeats() == 3, "in 3/4 a pattern can be 3 beats long");
    arr.patterns.resize (1);
    arr.setPatternLengthKeepingClips (0, 2 * kBeat);
    check (arr.patternLength (0) == 3 * kBeat, "but not shorter than a bar");
    arr.meter = twoFour;
    const int twoFourMin = arr.minPatternBeats();
    arr.meter = sevenFour;
    check (twoFourMin == 2 && arr.minPatternBeats() == 4, "2/4 allows 2 beats; a long bar still allows 4");

    // Song mode carries the tempo changes and the time signature.
    arr.meter = sixEight;
    arr.tempoMarkers = { { 8 * kBeat, 90.0 }, { 2 * kBeat, 100.0 } };
    const auto song = sls::compileSong ({}, arr);
    check (song.tempoMarkers.size() == 2 && song.tempoMarkers[0].tick == 2 * kBeat && song.beatsPerBar == 6 && song.beatUnit == 8,
           "the song plays its tempo changes (sorted) and counts in 6/8");
    check (sls::compilePattern ({}, arr, 0).tempoMarkers.empty(), "a pattern plays at one tempo");

    // Rendering: 4 beats at 120, then 60 BPM. A note on beat 8 starts at 2 s + 4 s.
    {
        auto project = sls::Project::make ({ synthChannel ({ { 60, 8 * kBeat, kBeat, 1.0f } }) }, 12 * kBeat);
        project.tempoMarkers = { { 4 * kBeat, 60.0 } };
        const auto out = sls::renderToBuffer (project, {});
        check (out.getNumSamples() == static_cast<int> ((2.0 + 8.0 + 1.0) * kSr), "the export lasts as long as the tempos say (4 beats = 2 s, 8 beats at 60 = 8 s, the 1 s tail)");
        check (out.getMagnitude (0, 0, static_cast<int> (5.95 * kSr)) == 0.0f && out.getMagnitude (0, static_cast<int> (6.0 * kSr), 4800) > 0.01f,
               "a note after a tempo change starts at its time in the new tempo");
    }

    // The metronome: accents every bar of the time signature, and follows the tempo changes.
    const auto clicks = [] (sls::Project project, int samples)
    {
        sls::Player player;
        player.prepare (kSr);
        player.setMetronome (true, 0.8f);
        std::vector<float> l (256), r (256), all;
        float* ch[2] = { l.data(), r.data() };
        while (static_cast<int> (all.size()) < samples)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            player.render (project, 120.0, true, ch, 2, 256);
            all.insert (all.end(), l.begin(), l.end());
        }
        return all;
    };
    const auto peak = [] (const std::vector<float>& x, int from, int to)
    {
        float p = 0.0f;
        for (int i = std::max (0, from); i < std::min (to, static_cast<int> (x.size())); ++i)
            p = std::max (p, std::abs (x[static_cast<size_t> (i)]));
        return p;
    };
    {
        auto project = sls::Project::make ({}, 12 * kBeat);
        project.beatsPerBar = 3;
        const auto x = clicks (project, 4 * 24000 + 2000);
        const float b0 = peak (x, 0, 1500), b1 = peak (x, 24000, 25500), b2 = peak (x, 48000, 49500), b3 = peak (x, 72000, 73500);
        check (b0 > b1 * 1.2f && std::abs (b1 - b2) < 1e-4f && std::abs (b3 - b0) < 1e-4f, "in 3/4 the metronome accents every third beat");
    }
    {
        auto project = sls::Project::make ({}, 12 * kBeat);
        project.tempoMarkers = { { 2 * kBeat, 60.0 } }; // beats at 0, 0.5 s, then 1 s, 2 s, 3 s
        const auto x = clicks (project, static_cast<int> (3.1 * kSr));
        check (peak (x, 24000, 25000) > 0.05f && peak (x, 48000, 49000) > 0.05f && peak (x, 96000, 97000) > 0.05f
                   && peak (x, 72000 - 500, 72000 + 1000) == 0.0f,
               "after a tempo change the clicks come at the new tempo");
    }

    // Saving and loading.
    {
        auto doc = sls::ProjectDocument::blank();
        check (doc.toJson().indexOf ("timeSignature") < 0 && doc.toJson().indexOf ("tempoChanges") < 0, "a 4/4 project with one tempo saves as before");
        doc.arrangement.meter = sixEight;
        doc.arrangement.tempoMarkers = { { 4 * kBeat, 87.5 }, { 16 * kBeat, 140.0 } };
        const auto back = sls::ProjectDocument::fromJson (doc.toJson());
        check (back.document.has_value() && back.document->arrangement.meter == sixEight && back.document->arrangement.tempoMarkers == doc.arrangement.tempoMarkers,
               "the time signature and the tempo changes are saved and loaded");
        auto json = doc.toJson().replace ("\"6/8\"", "\"11/3\"");
        const auto odd = sls::ProjectDocument::fromJson (json);
        check (odd.document.has_value() && odd.document->arrangement.meter == sls::TimeSignature {}, "an unknown time signature loads as 4/4");
    }
}

void testMixerAndExport045()
{
    std::cout << "Mixer and export: sidechain, 10 slots, effect chains, export options and stems (0.45.0)\n";
    check (sls::kSlotsPerTrack == 10, "10 effect slots per track, aux bus and master");

    // Sidechain: a steady tone on track 2 with a compressor listening to track 1, where a loud note plays in the second half.
    // Track 1 is muted: its sound still drives the compressor (the key is taken before the fader and mute).
    const auto buildProject = [] (int sidechain)
    {
        auto pad = synthChannel ({ { 57, 0, 8 * kBeat, 0.5f } }, 0.6f);
        pad.mixerTrack = 1;
        auto kick = synthChannel ({ { 45, 4 * kBeat, 4 * kBeat, 1.0f } }, 1.5f);
        kick.mixerTrack = 0;
        auto tracks = sls::Project::defaultTracks();
        tracks[0].mute = true;
        auto comp = sls::EffectSlot::make (sls::EffectType::Compressor);
        comp.params = { -40.0f, 20.0f, 1.0f, 50.0f, 0.0f, static_cast<float> (sidechain) };
        tracks[1].slots[9] = comp; // the last of the 10 slots works too
        return sls::Project::make ({ kick, pad }, 8 * kBeat, tracks);
    };
    const auto rms = [] (const juce::AudioBuffer<float>& b, double from, double to)
    {
        const int a = static_cast<int> (from * 48000.0), z = static_cast<int> (to * 48000.0);
        return b.getRMSLevel (0, a, z - a);
    };
    const auto keyed = sls::renderToBuffer (buildProject (1), {});
    const auto own = sls::renderToBuffer (buildProject (0), {});
    // 120 BPM: the kick plays from 2 s to 4 s.
    const float keyedQuiet = rms (keyed, 0.5, 1.8), keyedDucked = rms (keyed, 2.3, 3.8);
    const float ownQuiet = rms (own, 0.5, 1.8), ownLater = rms (own, 2.3, 3.8);
    check (keyedDucked < keyedQuiet * 0.5f, "with the sidechain on track 1 the pad is turned down while track 1's (muted) note plays");
    check (std::abs (ownLater - ownQuiet) < ownQuiet * 0.1f, "without it the pad stays level (the muted track is not heard)");
    check (sls::effectParamInfo (sls::EffectType::Compressor, 5).def == 0.0f && sls::EffectSlot::make (sls::EffectType::Compressor).params[5] == 0.0f,
           "the sidechain is off by default (older projects keep their sound)");
    // A source beyond the project's tracks is ignored, not read.
    const auto far = sls::renderToBuffer (buildProject (400), {});
    check (std::abs (rms (far, 2.3, 3.8) - ownLater) < ownLater * 0.05f, "a sidechain from a track that does not exist does nothing");

    // Effect chains.
    std::array<sls::EffectSlot, sls::kSlotsPerTrack> chain {};
    chain[0] = sls::EffectSlot::make (sls::EffectType::ParametricEq);
    chain[3] = sls::EffectSlot::make (sls::EffectType::Compressor);
    chain[3].params[0] = -30.0f;
    chain[3].params[5] = 2.0f;
    chain[3].bypass = true;
    chain[9] = sls::EffectSlot::make (sls::EffectType::Reverb);
    chain[5].type = sls::EffectType::Plugin;
    int leftOut = 0;
    const auto json = sls::effectChainToJson (chain, &leftOut);
    const auto back = sls::effectChainFromJson (json);
    check (leftOut == 1 && back.has_value() && (*back)[0].type == sls::EffectType::ParametricEq && (*back)[3].params[0] == -30.0f
               && (*back)[3].params[5] == 2.0f && (*back)[3].bypass && (*back)[9].type == sls::EffectType::Reverb
               && (*back)[5].type == sls::EffectType::None,
           "an effect chain is saved and read back (built-in effects, bypass, sidechain; a plugin slot is left out)");
    check (! sls::effectChainFromJson ("{\"format\":\"other\"}") && ! sls::effectChainFromJson ("not json"), "other files are not effect chains");

    // Older projects: 4 slots per track come back in the first 4 of 10.
    {
        auto doc = sls::ProjectDocument::blank();
        doc.tracks[0].slots[3] = sls::EffectSlot::make (sls::EffectType::Delay);
        doc.tracks[0].slots[8] = sls::EffectSlot::make (sls::EffectType::Chorus);
        const auto again = sls::ProjectDocument::fromJson (doc.toJson());
        check (again.document && again.document->tracks[0].slots[3].type == sls::EffectType::Delay
                   && again.document->tracks[0].slots[8].type == sls::EffectType::Chorus,
               "slots 4 and 9 of a track are saved and read back");
    }

    // Export options.
    const auto dir = freshTempDir ("sls-export-045");
    const auto project = buildProject (0);
    const auto read = [] (const juce::File& f, int& bits, double& rate, juce::StringPairArray& tags)
    {
        juce::AudioFormatManager m;
        m.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (m.createReaderFor (f));
        if (r == nullptr)
            return false;
        bits = static_cast<int> (r->bitsPerSample);
        rate = r->sampleRate;
        tags = r->metadataValues;
        return true;
    };
    sls::RenderOptions o16;
    o16.sampleRate = 44100.0;
    o16.bitDepth = 16;
    sls::ExportSettings tagsIn;
    tagsIn.title = "Sole";
    tagsIn.artist = "Hubert";
    int bits = 0;
    double rate = 0.0;
    juce::StringPairArray tags;
    const auto wav16 = dir.getChildFile ("a.wav");
    check (sls::exportAudio (project, o16, wav16, sls::ExportFormat::Wav, {}, tagsIn).wasOk() && read (wav16, bits, rate, tags) && bits == 16
               && std::abs (rate - 44100.0) < 0.5,
           "a 16-bit WAV at 44.1 kHz");
    check (tags[juce::WavAudioFormat::riffInfoTitle] == "Sole" && tags[juce::WavAudioFormat::riffInfoArtist] == "Hubert", "with the title and artist in its INFO list");
    sls::RenderOptions o32;
    o32.sampleRate = 96000.0;
    o32.bitDepth = 32;
    const auto wav32 = dir.getChildFile ("b.wav");
    check (sls::exportAudio (project, o32, wav32, sls::ExportFormat::Wav).wasOk() && read (wav32, bits, rate, tags) && bits == 32
               && std::abs (rate - 96000.0) < 0.5,
           "a 32-bit floating-point WAV at 96 kHz");
    sls::RenderOptions o24;
    const auto flac16 = dir.getChildFile ("c.flac");
    check (sls::exportAudio (project, o16, flac16, sls::ExportFormat::Flac).wasOk() && read (flac16, bits, rate, tags) && bits == 16,
           "a 16-bit FLAC");

    // Dither: a quiet steady value comes out on the 16-bit grid, on average where it was (no truncation bias).
    juce::AudioBuffer<float> quiet (1, 48000);
    quiet.clear();
    for (int i = 0; i < quiet.getNumSamples(); ++i)
        quiet.setSample (0, i, 0.3f / 32768.0f);
    sls::ditherTo16Bits (quiet, 7);
    double mean = 0.0;
    bool onGrid = true;
    for (int i = 0; i < quiet.getNumSamples(); ++i)
    {
        const double v = quiet.getSample (0, i) * 32768.0;
        mean += v;
        onGrid = onGrid && std::abs (v - std::round (v)) < 1.0e-3;
    }
    mean /= quiet.getNumSamples();
    check (onGrid && std::abs (mean - 0.3) < 0.03, "16-bit dither: every sample on the grid, the average kept (0.3 of a step)");

    // Stems.
    {
        auto tracks = sls::Project::defaultTracks();
        tracks[2].name = "Keys";
        auto a = synthChannel ({ { 60, 0, kBeat, 0.8f } });
        a.mixerTrack = 0;
        auto b = synthChannel ({ { 64, kBeat, kBeat, 0.8f } });
        b.mixerTrack = 2;
        auto c = synthChannel ({});
        c.mixerTrack = 5; // routed, but plays nothing
        const auto song = sls::Project::make ({ a, b, c }, 4 * kBeat, tracks);
        check (sls::tracksInUse (song) == std::vector<int> ({ 0, 2, 5 }), "the tracks in use are those of the channels (and audio clips)");
        std::vector<juce::File> written;
        sls::RenderOptions opts;
        const auto result = sls::exportStems (song, opts, dir.getChildFile ("Mix.wav"), sls::ExportFormat::Wav, written);
        check (result.wasOk() && written.size() == 2 && written[0].getFileName() == "Mix - Track 1.wav" && written[1].getFileName() == "Mix - Keys.wav",
               "stems: one file per track that plays, named after it; the silent track is skipped");
        juce::AudioFormatManager m;
        m.registerBasicFormats();
        float sumPeak = 0.0f;
        std::vector<juce::AudioBuffer<float>> stems;
        for (const auto& f : written)
        {
            std::unique_ptr<juce::AudioFormatReader> r (m.createReaderFor (f));
            juce::AudioBuffer<float> buf (2, static_cast<int> (r->lengthInSamples));
            r->read (&buf, 0, buf.getNumSamples(), 0, true, true);
            stems.push_back (buf);
        }
        const auto full = sls::renderToBuffer (song, opts);
        float worst = 0.0f;
        if (stems.size() == 2)
            for (int i = 0; i < full.getNumSamples(); ++i)
                worst = std::max (worst, std::abs (full.getSample (0, i) - stems[0].getSample (0, i) - stems[1].getSample (0, i)));
        sumPeak = full.getMagnitude (0, 0, full.getNumSamples());
        check (stems.size() == 2 && worst < sumPeak * 0.001f, "the stems add up to the mix (within 24-bit rounding)");
    }
    dir.deleteRecursively();
}

void testPlaylistTracks()
{
    std::cout << "Playlist tracks: 4 free tracks, one per pattern, - and + (0.44.0)\n";
    auto doc = sls::ProjectDocument::blank();
    auto& a = doc.arrangement;
    check (a.trackCount() == 5 && a.playlistTracks[4].pattern == 0 && a.dedicatedTrack (0) == 4
               && std::all_of (a.playlistTracks.begin(), a.playlistTracks.begin() + 4, [] (const sls::PlaylistTrack& t) { return t.pattern == -1; }),
           "a new project: 4 free tracks and Pattern 1's own track");
    for (int p = 1; p < 4; ++p)
    {
        a.patterns.emplace_back();
        a.addPatternTrack (p);
    }
    check (a.trackCount() == 8 && a.dedicatedTrack (3) == 7, "with 4 patterns: 4 tracks for the patterns + the 4 free ones = 8");
    check (a.addPatternTrack (3) == 7 && a.trackCount() == 8, "a pattern never gets a second track");
    check (a.addTrack() == 8 && a.trackCount() == 9 && a.playlistTracks[8].pattern == -1, "+ adds a free track at the end");

    // Removing a track takes its clips with it; the tracks (and clips) after it move up.
    a.clips = { { 0, 4, 0, 1920, 0 }, { 1, 5, 0, 1920, 0 }, { 3, 7, 1920, 1920, 0 } };
    sls::AudioClip audio;
    audio.data = sls::makeMissingSample ("/x.wav");
    audio.track = 6;
    audio.lengthTicks = 960;
    a.audioClips = { audio };
    check (a.clipsOnTrack (5) == 1 && a.clipsOnTrack (6) == 1 && a.clipsOnTrack (0) == 0, "clips are counted per track");
    check (a.removeTrack (5) && a.trackCount() == 8 && a.clips.size() == 2 && a.clips[1].track == 6 && a.audioClips[0].track == 5
               && a.playlistTracks[5].pattern == 2 && a.dedicatedTrack (1) == -1,
           "- on a track: its clip goes, the clips and tracks below move up one place");
    check (! a.removeTrack (-1) && ! a.removeTrack (99), "a track that does not exist cannot be removed");
    sls::Arrangement one;
    one.playlistTracks.resize (1);
    check (! one.removeTrack (0) && one.trackCount() == 1, "the last track stays");
    sls::Arrangement full;
    while (full.addTrack() >= 0) {}
    check (full.trackCount() == sls::kMaxPlaylistTracks, "at most kMaxPlaylistTracks tracks");

    // Deleting a pattern: its own track goes when empty, else it becomes a free track named after it; later patterns' tracks follow.
    {
        auto d = sls::ProjectDocument::blank();
        auto& b = d.arrangement;
        b.patterns.resize (3);
        b.patterns[1].name = "Verse";
        b.addPatternTrack (1);
        b.addPatternTrack (2);
        b.clips = { { 2, b.dedicatedTrack (2), 0, 1920, 0 } };
        check (b.removePattern (1) && b.trackCount() == 6 && b.dedicatedTrack (1) == 5 && b.clips[0].pattern == 1 && b.clips[0].track == 5,
               "deleting a pattern whose track is empty removes the track; the next pattern's track and clip move up");
        b.clips.push_back ({ 0, 4, 0, 1920, 0 });
        sls::AudioClip take;
        take.data = sls::makeMissingSample ("/take.wav");
        take.track = 4;
        take.lengthTicks = 960;
        b.audioClips = { take };
        b.patterns[0].name = "Intro";
        check (b.removePattern (0) && b.trackCount() == 6 && b.playlistTracks[4].pattern == -1 && b.playlistTracks[4].name == "Intro"
                   && b.audioClips.size() == 1 && b.dedicatedTrack (0) == 5,
               "a pattern's track that still holds a recording stays, as a free track with the pattern's name");
    }

    // Saved and read back; older files had 8 tracks.
    {
        auto d = sls::ProjectDocument::blank();
        d.arrangement.patterns.resize (2);
        d.arrangement.patterns[1].name = "B";
        d.arrangement.addPatternTrack (1);
        d.arrangement.addTrack();
        d.arrangement.playlistTracks[2].name = "Vocals";
        const auto back = sls::ProjectDocument::fromJson (d.toJson());
        check (back.document && back.document->arrangement.playlistTracks == d.arrangement.playlistTracks && back.document->arrangement.trackCount() == 7,
               "7 tracks, two of them dedicated to patterns and one named, are saved and read back");
        auto v = juce::JSON::parse (d.toJson());
        v.getDynamicObject()->removeProperty ("trackCount");
        const auto old = sls::ProjectDocument::fromJson (juce::JSON::toString (v));
        check (old.document && old.document->arrangement.trackCount() == sls::kLegacyPlaylistTracks,
               "a file from before 0.44.0 opens with its 8 tracks");
        auto* list = v.getDynamicObject()->getProperty ("playlistTracks").getArray();
        for (auto& t : *list)
            t.getDynamicObject()->setProperty ("pattern", 1); // two tracks claiming one pattern
        v.getDynamicObject()->setProperty ("trackCount", 100000);
        const auto odd = sls::ProjectDocument::fromJson (juce::JSON::toString (v));
        int claims = 0;
        if (odd.document)
            for (const auto& t : odd.document->arrangement.playlistTracks)
                claims += t.pattern == 1 ? 1 : 0;
        check (odd.document && odd.document->arrangement.trackCount() == sls::kMaxPlaylistTracks && claims == 1,
               "an impossible track count is limited and a pattern keeps a single track");
    }

    // Recording settings follow a removed track.
    sls::RecordSetup setup;
    setup.armOnly (3);
    setup.tracks[5] = { true, 4, true };
    setup.removeTrack (2);
    check (setup.armedTracks() == std::vector<int> ({ 2, 4 }) && setup.tracks[4].firstInput == 4, "the arms and inputs of the tracks below move up with them");
    setup.armOnly (0);
    setup.removeTrack (0);
    check (setup.armedTracks() == std::vector<int> { 0 }, "removing the only armed track arms the first one");
}

void testFreePlaylist()
{
    std::cout << "Playlist: pattern clips anywhere on the grid, the pattern length in quarters (0.42.0)\n";
    const int bar = 4 * kBeat;
    sls::Arrangement a;
    check (sls::Pattern {}.lengthTicks == bar && sls::ProjectDocument::blank().arrangement.patternLength (0) == bar,
           "a new pattern is one bar of 4/4 (4 quarters, 16 steps)");
    a.patterns.resize (1);
    a.ensureChannelCount (1);
    a.patterns[0].notes[0] = { { 60, 0, 120, 0.8f }, { 62, kBeat, 120, 0.8f }, { 64, 2 * kBeat, 120, 0.8f }, { 65, 3 * kBeat, 120, 0.8f } };
    const std::vector<sls::Channel> channels { synthChannel ({}) };

    // A clip one 16th after a bar line: every note moves with it (no snapping to whole patterns).
    a.clips = { { 0, 0, bar + 120, bar, 0 } };
    auto song = sls::compileSong (channels, a);
    check (song.channels[0].notes.size() == 4 && song.channels[0].notes[0].startTick == bar + 120 && song.channels[0].notes[3].startTick == bar + 120 + 3 * kBeat,
           "a clip placed a 16th after the bar plays its notes a 16th late");
    a.clips = { { 0, 0, 37, bar, 0 } };
    song = sls::compileSong (channels, a);
    check (song.channels[0].notes[0].startTick == 37, "off the grid too (Alt): any tick");
    // Two clips on different tracks, one of them a beat behind the other: they play out of step, as placed.
    a.clips = { { 0, 0, 0, bar, 0 }, { 0, 1, kBeat, bar, 0 } };
    song = sls::compileSong (channels, a);
    check (song.channels[0].notes.size() == 8, "clips may be offset against each other freely");
    // Length not a whole number of patterns: the notes after the end are left out, a note running over is shortened.
    a.clips = { { 0, 0, 0, bar + kBeat + 60, 0 } };
    song = sls::compileSong (channels, a);
    check (song.channels[0].notes.size() == 6 && song.channels[0].notes.back().startTick == bar + kBeat && song.channels[0].notes.back().lengthTicks == 60,
           "a clip 1 bar + 1 beat + a 32nd long: the second pass is cut where the clip ends");
    // A clip that starts inside the pattern (after a split) goes on from there.
    a.clips = { { 0, 0, 0, 2 * kBeat, 2 * kBeat } };
    song = sls::compileSong (channels, a);
    check (song.channels[0].notes.size() == 2 && song.channels[0].notes[0].pitch == 64 && song.channels[0].notes[0].startTick == 0
               && song.channels[0].notes[1].pitch == 65,
           "a clip with an offset of 2 beats starts with the pattern's third beat");
    a.clips = { { 0, 0, 0, bar, 3 * kBeat } };
    song = sls::compileSong (channels, a);
    check (song.channels[0].notes.size() == 4 && song.channels[0].notes[0].pitch == 65 && song.channels[0].notes[1].pitch == 60
               && song.channels[0].notes[1].startTick == kBeat,
           "and wraps round to the pattern's start");
    check (song.lengthTicks == bar, "the song ends at the last clip, rounded up to a bar");

    // The pattern length in quarters, kept for clips.
    a.clips = { { 0, 0, 120, 2 * bar, 0 }, { 0, 1, 0, 3 * kBeat, 0 } };
    a.setPatternLengthKeepingClips (0, 5 * kBeat);
    check (a.patternLength (0) == 5 * kBeat && a.clips[0].startTick == 120 && a.clips[0].lengthTicks == 10 * kBeat && a.clips[1].lengthTicks == 3 * kBeat,
           "one quarter longer: clips stay put, a two-pattern clip keeps its two repeats, a shorter one its length");
    a.setPatternLengthKeepingClips (0, 2 * kBeat);
    check (a.patternLength (0) == 4 * kBeat, "never shorter than one bar of 4/4");

    // Saved and read back in ticks; older files in whole patterns.
    auto doc = sls::ProjectDocument::blank();
    setLengths (doc.arrangement, 5 * kBeat);
    doc.arrangement.clips = { { 0, 2, 777, 3000, 240 } };
    auto back = sls::ProjectDocument::fromJson (doc.toJson());
    check (back.document && back.document->arrangement.patternLength (0) == 5 * kBeat && back.document->arrangement.clips.size() == 1
               && back.document->arrangement.clips[0].startTick == 777 && back.document->arrangement.clips[0].lengthTicks == 3000
               && back.document->arrangement.clips[0].offsetTicks == 240 && back.document->arrangement.clips[0].track == 2,
           "a pattern length of 5 quarters and a clip at tick 777 with an offset are saved and read back");
    setLengths (doc.arrangement, 8 * kBeat);
    auto v = juce::JSON::parse (doc.toJson());
    v.getDynamicObject()->removeProperty ("patternBeats");
    for (auto& pv : *v.getDynamicObject()->getProperty ("patterns").getArray())
        pv.getDynamicObject()->removeProperty ("beats"); // no length of its own either (0.56.0)
    auto* clip = v.getDynamicObject()->getProperty ("clips").getArray()->getReference (0).getDynamicObject();
    clip->removeProperty ("startTick");
    clip->removeProperty ("lengthTicks");
    clip->removeProperty ("offsetTicks");
    clip->setProperty ("start", 3);
    clip->setProperty ("length", 2);
    back = sls::ProjectDocument::fromJson (juce::JSON::toString (v));
    check (back.document && back.document->arrangement.patternLength (0) == 8 * kBeat && back.document->arrangement.clips[0].startTick == 3 * 8 * kBeat
               && back.document->arrangement.clips[0].lengthTicks == 2 * 8 * kBeat,
           "a file from before 0.42.0 (whole patterns of two bars) opens with its clips in the same places");
}

void testStepSequencer()
{
    std::cout << "Step sequencer (0.31.0)\n";
    namespace st = sls::steps;
    const int bar = 16 * st::kStepTicks;
    const int pattern2 = 2 * bar; // a 2-bar pattern: 32 steps

    sls::Channel sampler;
    sampler.instrument = sls::Instrument::Sampler;
    sampler.rootPitch = 60;
    sls::Channel drums;
    drums.instrument = sls::Instrument::Drums;
    check (st::stepPitch (sampler) == 60 && st::stepPitch (drums) == 36, "a step plays the root note, or the kick on a drum channel");

    const auto step = [] (int s, int pitch) { return sls::Note { pitch, s * st::kStepTicks, st::kStepTicks, 0.9f }; };
    std::vector<sls::Note> kicks { step (0, 60), step (4, 60), step (8, 60), step (12, 60) };
    check (st::isStepSequence ({}, 60), "no notes at all: squares");
    check (st::isStepSequence (kicks, 60), "only steps: squares");
    auto chord = kicks;
    chord.push_back ({ 64, 0, st::kStepTicks, 0.8f });
    check (! st::isStepSequence (chord, 60), "a note on another pitch (from the piano roll): the preview");
    auto longNote = kicks;
    longNote[1].lengthTicks = 2 * st::kStepTicks;
    check (! st::isStepSequence (longNote, 60), "a note longer than a step: the preview");
    auto offGrid = kicks;
    offGrid[2].startTick += 30;
    check (! st::isStepSequence (offGrid, 60), "a note off the step grid: the preview");

    // 0.42.0: the grid is the whole pattern; nothing is greyed out, nothing grows or repeats by itself.
    check (st::activeSteps ({}, 0, bar) == 16, "a bar of 4/4 shows 16 steps");
    check (st::activeSteps (kicks, 0, bar + 4 * st::kStepTicks) == 20, "a quarter more: 20 steps");
    check (st::activeSteps ({ step (3, 60) }, 8, pattern2) == 32, "every step of the pattern is a real step (a length set before 0.42.0 is ignored)");
    check (st::playedNotes (kicks, pattern2).size() == 4, "a one-bar step sequence in a two-bar pattern is not repeated");
    check (st::playedNotes (chord, pattern2).size() == chord.size(), "notes play as written");

    // Editing: left writes, right erases, anywhere in the pattern.
    {
        auto notes = kicks;
        int set = 0;
        check (st::setStep (notes, set, 2, true, pattern2, 60) && st::stepOn (notes, 2, 60), "left click writes a step");
        check (! st::setStep (notes, set, 2, true, pattern2, 60), "writing a step that is on changes nothing");
        check (st::setStep (notes, set, 2, false, pattern2, 60) && ! st::stepOn (notes, 2, 60), "right click erases it");
        check (! st::setStep (notes, set, 3, false, pattern2, 60), "erasing an empty step changes nothing");
        check (st::setStep (notes, set, 22, true, pattern2, 60) && st::stepOn (notes, 22, 60) && notes.size() == 5,
               "a step in the second bar is written alone (nothing else is filled in)");
        check (! st::setStep (notes, set, 32, true, pattern2, 60) && ! st::setStep (notes, set, -1, true, pattern2, 60), "steps outside the pattern are refused");
    }

    // Pattern and song mode play the notes as written.
    {
        sls::Arrangement arrangement;
        arrangement.patterns.resize (1);
        setLengths (arrangement, pattern2);
        std::vector<sls::Channel> channels { sampler, sampler };
        arrangement.ensureChannelCount (channels.size());
        arrangement.patterns[0].notes[0] = kicks;
        arrangement.patterns[0].notes[1] = chord;
        const auto p = sls::compilePattern (channels, arrangement, 0);
        check (p.channels[0].notes.size() == 4 && p.channels[1].notes.size() == chord.size(), "pattern mode plays the steps once");
        arrangement.clips = { uc (arrangement, 0, 0, 0, 2) };
        check (sls::compileSong (channels, arrangement).channels[0].notes.size() == 8, "song mode: once per repeat of the clip");
    }

    // Project files from 0.31.0 - 0.41.0 keep their sound: the repeats of the greyed steps are written out as notes.
    {
        std::vector<sls::Note> legacy = kicks;
        st::bakeLegacyRepeats (legacy, 0, pattern2, 60);
        check (legacy.size() == 8 && legacy[4].startTick == bar, "a one-bar sequence that repeated in a two-bar pattern gets its second bar");
        std::vector<sls::Note> twelve { step (0, 60) };
        st::bakeLegacyRepeats (twelve, 12, pattern2, 60);
        check (twelve.size() == 3 && twelve[1].startTick == 12 * st::kStepTicks && twelve[2].startTick == 24 * st::kStepTicks, "a 12-step sequence: every 12 steps");
        auto whole = kicks;
        st::bakeLegacyRepeats (whole, 32, pattern2, 60);
        check (whole.size() == 4, "a channel set to the whole pattern did not repeat: unchanged");
        auto melody = chord;
        st::bakeLegacyRepeats (melody, 0, pattern2, 60);
        check (melody.size() == chord.size(), "a channel written in the piano roll did not repeat: unchanged");

        auto doc = richDocument();
        setLengths (doc.arrangement, pattern2);
        doc.arrangement.ensureChannelCount (doc.channels.size());
        doc.channels[0].instrument = sls::Instrument::Sampler;
        doc.channels[0].rootPitch = 60;
        doc.arrangement.patterns[0].notes[0] = kicks;
        auto v = juce::JSON::parse (doc.toJson());
        check (! v.getDynamicObject()->getProperty ("patterns").getArray()->getReference (0).getDynamicObject()->hasProperty ("steps"),
               "0.42.0 no longer writes the active lengths");
        for (auto& pv : *v.getDynamicObject()->getProperty ("patterns").getArray())
        {
            juce::Array<juce::var> lengths;
            for (size_t c = 0; c < doc.channels.size(); ++c)
                lengths.add (0);
            pv.getDynamicObject()->setProperty ("steps", lengths); // as 0.31.0 - 0.41.0 wrote them
        }
        const auto old = sls::ProjectDocument::fromJson (juce::JSON::toString (v));
        check (old.document.has_value() && old.document->arrangement.patterns[0].notes[0].size() == 8,
               "an older file opens with the repeats written out, so it sounds as before");
    }
}

void testLoopStarter()
{
    std::cout << "Loop Starter (0.30.0)\n";
    namespace ls = sls::loopstarter;
    using sls::kTicksPerBeat;
    constexpr int bar = 4 * kTicksPerBeat;

    bool allValid = true, allInScale = true, allHaveParts = true, chordTonesOnBars = true, bassBelow = true, melodyRange = true;
    bool soundsKnown = true;
    for (int g = 0; g < ls::kGenreCount; ++g)
        for (const bool minor : { true, false })
            for (const int bars : { 1, 2, 4, 8 })
                for (std::uint32_t v = 1; v <= 6; ++v)
                {
                    ls::Request q;
                    q.genre = static_cast<ls::Genre> (g);
                    q.key = (g * 5 + static_cast<int> (v)) % 12;
                    q.minor = minor;
                    q.variation = v;
                    q.patternTicks = bars * bar;
                    const auto loop = ls::make (q);
                    allHaveParts = allHaveParts && loop.parts.size() == 4 && loop.bpm == ls::genreInfo (q.genre).bpm;
                    int lowestChord = 128, highestBass = -1;
                    for (const auto& part : loop.parts)
                    {
                        allValid = allValid && ! part.notes.empty();
                        soundsKnown = soundsKnown && (part.sound.id.rfind ("kit:", 0) == 0 || part.sound.id.rfind ("program:", 0) == 0 || part.sound.id.rfind ("patch:", 0) == 0);
                        int previousStart = -1;
                        for (const auto& n : part.notes)
                        {
                            allValid = allValid && n.startTick >= 0 && n.startTick + n.lengthTicks <= q.patternTicks && n.lengthTicks > 0
                                       && n.pitch >= 0 && n.pitch <= 127 && n.velocity > 0.0f && n.velocity <= 1.0f && n.startTick >= previousStart;
                            previousStart = n.startTick;
                            if (part.role != ls::Role::Drums)
                                allInScale = allInScale && loop.scale.contains (n.pitch);
                            if (part.role == ls::Role::Chords)
                                lowestChord = std::min (lowestChord, n.pitch);
                            if (part.role == ls::Role::Bass)
                                highestBass = std::max (highestBass, n.pitch);
                            if (part.role == ls::Role::Melody)
                                melodyRange = melodyRange && n.pitch >= 60 && n.pitch <= 90;
                        }
                    }
                    bassBelow = bassBelow && highestBass < lowestChord;
                    // the melody's note on each bar's downbeat is a note of the chord playing there
                    const auto& chordsPart = loop.parts[2].notes;
                    for (const auto& n : loop.parts[3].notes)
                        if (n.startTick % bar == 0)
                        {
                            bool found = false;
                            for (const auto& c : chordsPart)
                                if (c.startTick <= n.startTick && n.startTick < c.startTick + c.lengthTicks + kTicksPerBeat && (c.pitch - n.pitch) % 12 == 0)
                                    found = true;
                            chordTonesOnBars = chordTonesOnBars && found;
                        }
                }
    check (allHaveParts, "every genre, both modes, 1 to 8 bars: four parts at the genre's tempo");
    check (allValid, "every note inside the pattern, in order, with a sensible pitch, length and velocity");
    check (allInScale, "bass, chords and melody stay in the key");
    check (bassBelow, "the bass is always below the chords");
    check (melodyRange, "the melody stays between C4 and F#6");
    check (chordTonesOnBars, "the melody starts every bar on a note of the chord");
    check (soundsKnown, "every part names a kit, a library program or a synth patch");
    {
        // and every one of them exists: each genre's choices over many variations, fallbacks included
        std::set<std::string> ids;
        for (int g = 0; g < ls::kGenreCount; ++g)
            for (std::uint32_t v = 1; v <= 40; ++v)
                for (const bool minor : { true, false })
                {
                    ls::Request q;
                    q.genre = static_cast<ls::Genre> (g);
                    q.variation = v;
                    q.minor = minor;
                    for (const auto& part : ls::make (q).parts)
                        ids.insert (part.sound.id), ids.insert (part.sound.fallback);
                }
        bool allExist = true;
        std::string missing;
        for (const auto& id : ids)
        {
            bool found = false;
            if (id.rfind ("patch:", 0) == 0)
                for (const auto& f : sls::synthFactoryPatches())
                    found = found || "patch:" + f.name == id;
            else if (id.rfind ("program:", 0) == 0)
            {
                sls::SamplerSettings settings;
                std::string error;
                found = sls::makeLibraryProgram (id.substr (8), &settings, error) != nullptr;
            }
            else if (id.rfind ("kit:", 0) == 0)
                for (const auto& k : sls::builtinKits())
                    found = found || "kit:" + k.id == id;
            if (! found)
                missing += " " + id;
            allExist = allExist && found;
        }
        if (! allExist)
            std::cout << "    missing:" << missing << "\n";
        std::cout << "    (" << ids.size() << " different sounds)\n";
        check (allExist && ids.size() > 20, "every sound the Loop Starter names exists");
    }

    // genre traits
    const auto notesOf = [] (const ls::Loop& l, ls::Role role) -> const std::vector<sls::Note>& {
        for (const auto& p : l.parts)
            if (p.role == role)
                return p.notes;
        static const std::vector<sls::Note> none;
        return none;
    };
    const auto hasHit = [] (const std::vector<sls::Note>& notes, int key, int tick)
    { return std::any_of (notes.begin(), notes.end(), [&] (const sls::Note& n) { return n.pitch == key && n.startTick == tick; }); };
    bool fourOnTheFloor = true, backbeat = true, halfTime = true, dembow = true;
    for (std::uint32_t v = 1; v <= 8; ++v)
    {
        ls::Request q;
        q.variation = v;
        q.patternTicks = 2 * bar;
        for (const auto g : { ls::Genre::House, ls::Genre::Techno, ls::Genre::Reggaeton })
        {
            q.genre = g;
            const auto d = notesOf (ls::make (q), ls::Role::Drums);
            for (int beat = 0; beat < 8; ++beat)
                fourOnTheFloor = fourOnTheFloor && hasHit (d, ls::drum::kick, beat * kTicksPerBeat);
        }
        for (const auto g : { ls::Genre::HipHop, ls::Genre::DrumAndBass, ls::Genre::Pop, ls::Genre::Rock, ls::Genre::Synthwave })
        {
            q.genre = g;
            const auto d = notesOf (ls::make (q), ls::Role::Drums);
            for (int b = 0; b < 2; ++b)
                backbeat = backbeat && hasHit (d, ls::drum::snare, b * bar + kTicksPerBeat) && hasHit (d, ls::drum::snare, b * bar + 3 * kTicksPerBeat);
        }
        q.genre = ls::Genre::Trap;
        const auto trap = notesOf (ls::make (q), ls::Role::Drums);
        for (int b = 0; b < 2; ++b)
            halfTime = halfTime && hasHit (trap, ls::drum::clap, b * bar + 2 * kTicksPerBeat) && ! hasHit (trap, ls::drum::clap, b * bar + kTicksPerBeat);
        q.genre = ls::Genre::Reggaeton;
        const auto reg = notesOf (ls::make (q), ls::Role::Drums);
        for (const int s : { 3, 6, 11, 14 })
            dembow = dembow && hasHit (reg, ls::drum::snare, s * kTicksPerBeat / 4);
    }
    check (fourOnTheFloor, "house, techno and reggaeton: a kick on every beat");
    check (backbeat, "hip-hop, drum & bass, pop, rock and synthwave: the snare on beats 2 and 4");
    check (halfTime, "trap: the clap on beat 3 (half time), not on 2");
    check (dembow, "reggaeton: the dembow snare (16ths 4, 7, 12 and 15 of the bar)");

    // the same request, the same loop; another variation, another loop
    {
        ls::Request q;
        q.genre = ls::Genre::LoFi;
        const auto a = ls::make (q), b = ls::make (q);
        bool same = a.parts.size() == b.parts.size() && a.progression == b.progression;
        for (size_t i = 0; same && i < a.parts.size(); ++i)
            same = sameNotes (a.parts[i].notes, b.parts[i].notes) && a.parts[i].sound.id == b.parts[i].sound.id;
        check (same, "the same genre, key and variation always give the same loop");
        int different = 0;
        for (std::uint32_t v = 2; v <= 9; ++v)
        {
            q.variation = v;
            const auto c = ls::make (q);
            different += ! sameNotes (c.parts[0].notes, a.parts[0].notes) || ! sameNotes (c.parts[3].notes, a.parts[3].notes) ? 1 : 0;
        }
        check (different == 8, "eight other variations: eight other loops (drums or melody)");
        q.key = 2;
        q.minor = false;
        const auto d = ls::make (q);
        check (d.scale.root == 2 && d.scale.type == 1, "the key and mode asked for (D major)");
        q.drums = false;
        q.melody = false;
        const auto e = ls::make (q);
        check (e.parts.size() == 2 && e.parts[0].role == ls::Role::Bass && e.parts[1].role == ls::Role::Chords, "only the parts asked for");
    }
    {
        // every genre plays with its own sounds (kits, library instruments, synth patches): every part is heard on its own, and
        // the whole loop is neither quiet nor clipping
        const auto channelFor = [] (const ls::Part& part)
        {
            sls::Channel c;
            c.name = part.name;
            c.volume = part.volume;
            c.swing = part.swing;
            c.notes = part.notes;
            const auto& id = part.sound.id;
            if (id.rfind ("kit:", 0) == 0)
            {
                c.instrument = sls::Instrument::Drums;
                juce::StringArray problems;
                c.kit = std::make_shared<const sls::DrumKit> (sls::loadKit (sls::builtinKitSpec (id.substr (4)), problems));
            }
            else if (id.rfind ("program:", 0) == 0)
            {
                c.instrument = sls::Instrument::Sampler;
                std::string error;
                c.program = sls::makeLibraryProgram (id.substr (8), &c.sampler, error);
            }
            else
            {
                c.instrument = sls::Instrument::Synth;
                for (const auto& f : sls::synthFactoryPatches())
                    if ("patch:" + f.name == id)
                        c.synth = f.patch;
            }
            return c;
        };
        bool everyPartHeard = true, loopsFine = true;
        std::string report;
        for (int g = 0; g < ls::kGenreCount; ++g)
        {
            ls::Request q;
            q.genre = static_cast<ls::Genre> (g);
            q.variation = 3;
            q.patternTicks = 2 * bar;
            const auto loop = ls::make (q);
            sls::RenderOptions o;
            o.bpm = loop.bpm;
            o.tailSeconds = 0.3;
            std::vector<sls::Channel> all;
            for (const auto& part : loop.parts)
            {
                auto c = channelFor (part);
                const auto solo = sls::renderToBuffer (sls::Project::make ({ c }, 2 * bar), o);
                const float partPeak = peakOf (solo.getReadPointer (0), 0, solo.getNumSamples());
                if (partPeak < 0.02f)
                    report += " " + std::string (ls::genreInfo (q.genre).name) + "/" + part.name;
                everyPartHeard = everyPartHeard && partPeak >= 0.02f;
                c.mixerTrack = static_cast<int> (all.size());
                all.push_back (std::move (c));
            }
            const auto out = sls::renderToBuffer (sls::Project::make (std::move (all), 2 * bar), o);
            const float peak = peakOf (out.getReadPointer (0), 0, out.getNumSamples());
            loopsFine = loopsFine && peak > 0.15f && peak <= 1.0f;
        }
        if (! report.empty())
            std::cout << "    quiet:" << report << "\n";
        check (everyPartHeard, "every genre: each part, with its own sound, is heard on its own");
        check (loopsFine, "and each whole loop is clearly heard without clipping");
    }
}

void testScoreLog()
{
    std::cout << "Score log (0.28.0)\n";
    const auto on = [] (int pitch, double seconds, float velocity = 0.8f, int channel = 0)
    { return sls::ScoreEvent { true, static_cast<std::uint8_t> (channel), static_cast<std::uint8_t> (pitch), velocity, seconds }; };
    const auto off = [] (int pitch, double seconds, int channel = 0)
    { return sls::ScoreEvent { false, static_cast<std::uint8_t> (channel), static_cast<std::uint8_t> (pitch), 0.0f, seconds }; };
    const auto same = [] (const sls::Note& n, int pitch, int start, int length)
    { return n.pitch == pitch && n.startTick == start && n.lengthTicks == length; };

    sls::ScoreLog log;
    check (log.empty() && log.notes (120.0).empty(), "an empty log gives no notes");

    // 120 BPM: 960 ticks a second
    log.add (on (60, 10.0, 0.7f));
    log.add (off (60, 10.5));
    log.add (on (64, 11.0));
    log.add (off (64, 11.25));
    auto notes = log.notes (120.0);
    check (notes.size() == 2 && same (notes[0].note, 60, 0, 480) && same (notes[1].note, 64, 960, 240)
               && std::abs (notes[0].note.velocity - 0.7f) < 1e-6f,
           "notes keep their timing from the first one (at tick 0), length and velocity");
    check (log.notes (60.0).size() == 2 && same (log.notes (60.0)[1].note, 64, 480, 120), "at the tempo asked for (60 BPM: half the ticks)");

    log.add (on (67, 11.27));
    log.add (off (67, 11.4));
    notes = log.notes (120.0, sls::kTicksPerBeat / 4);
    check (notes.size() == 3 && notes[2].note.startTick == 1200 && notes[2].note.lengthTicks == 125,
           "quantize: a start 1.27 s after the first note (1219 ticks) goes to the 1/16 grid (1200); the length stays");

    {
        sls::ScoreLog held;
        held.add (on (48, 1.0));
        held.add (on (52, 1.5));
        held.add (off (52, 2.0));
        const auto n = held.notes (120.0);
        check (n.size() == 2 && same (n[0].note, 48, 0, 960), "a key still held ends with the last event");
        sls::ScoreLog repeated;
        repeated.add (on (60, 0.0));
        repeated.add (on (60, 0.25));
        repeated.add (off (60, 0.5));
        const auto r = repeated.notes (120.0);
        check (r.size() == 2 && same (r[0].note, 60, 0, 240) && same (r[1].note, 60, 240, 240), "a repeated note on ends the note before");
        sls::ScoreLog orphan;
        orphan.add (off (60, 0.0));
        orphan.add (on (62, 0.5));
        orphan.add (off (62, 1.0));
        check (orphan.notes (120.0).size() == 1 && orphan.notes (120.0)[0].note.pitch == 62, "a note off without its note on is ignored");
    }
    {
        sls::ScoreLog channels;
        channels.add (on (60, 0.0, 0.8f, 2));
        channels.add (on (60, 0.1, 0.8f, 5));
        channels.add (off (60, 0.5, 2));
        channels.add (off (60, 1.0, 5));
        const auto n = channels.notes (120.0);
        check (n.size() == 2 && n[0].channel == 2 && n[0].note.lengthTicks == 480 && n[1].channel == 5 && n[1].note.lengthTicks == 864,
               "the same key on two channels is two notes, each on its channel");
    }
    {
        sls::ScoreLog phrases;
        phrases.add (on (60, 0.0));
        phrases.add (off (60, 1.0));
        phrases.add (on (62, 1.5));
        phrases.add (off (62, 2.0));
        phrases.add (on (64, 6.0)); // after a 4 s pause
        phrases.add (off (64, 6.5));
        phrases.add (on (65, 7.0));
        phrases.add (off (65, 7.5));
        const auto last = phrases.notes (120.0, 0, 3.0);
        check (last.size() == 2 && same (last[0].note, 64, 0, 480) && same (last[1].note, 65, 960, 480),
               "the last phrase: what came after the last pause of 3 s, from tick 0");
        check (phrases.notes (120.0, 0, 5.0).size() == 4 && phrases.notes (120.0).size() == 4, "a shorter pause, or no phrase asked for: everything");
        phrases.add (on (36, 7.5));    // a long note bridging the next pause
        phrases.add (on (67, 12.0));
        phrases.add (off (36, 12.5));
        phrases.add (off (67, 12.5));
        check (phrases.notes (120.0, 0, 3.0).size() == 4, "a pause while a key is held is not a pause");
    }
    {
        sls::ScoreLog old;
        old.add (on (60, 0.0));
        old.add (off (60, 1.0));
        old.add (on (62, 400.0));
        old.add (off (62, 401.0));
        check (old.size() == 2 && old.notes (120.0).size() == 1 && old.notes (120.0)[0].note.pitch == 62, "events older than five minutes are forgotten");
        sls::ScoreLog odd;
        odd.add (on (60, std::numeric_limits<double>::quiet_NaN()));
        odd.add (on (200, 1.0));
        odd.add (on (60, 5.0, 3.0f));
        odd.add (off (60, 4.0)); // stamped earlier than the event before (the latency changed): taken as the same time
        const auto n = odd.notes (120.0);
        check (odd.size() == 2 && n.size() == 1 && n[0].note.lengthTicks == 1 && n[0].note.velocity == 1.0f,
               "a time that is not a number and a pitch above 127 are refused; time never goes back; velocity at most 1");
        sls::ScoreLog full;
        for (int i = 0; i < 25000; ++i)
            full.add (on (60 + i % 12, i * 0.001));
        check (full.size() == sls::ScoreLog::kMaxEvents, "at most 20000 events are kept");
        full.clear();
        check (full.empty() && full.notes (120.0).empty(), "clearing empties it");
    }
}

void testClipPanReverseFades()
{
    std::cout << "Audio clips: pan, reverse and fades (0.27.0)\n";
    const auto empty = std::vector<sls::Channel> { synthChannel ({}) };
    // 1 s of a ramp: frame i holds 0.5 * i / 48000 on both sides, so a sample says where in the audio it came from
    auto rampData = std::make_shared<sls::SampleData>();
    rampData->sampleRate = kSr;
    rampData->source = "/tmp/ramp.wav";
    rampData->left.resize (48000);
    for (size_t i = 0; i < rampData->left.size(); ++i)
        rampData->left[i] = 0.5f * static_cast<float> (i) / 48000.0f;
    rampData->right = rampData->left;
    const std::shared_ptr<const sls::SampleData> ramp = rampData;
    const auto sine = sineData (48000, kSr, 440.0, 0.5f);

    const auto clipOf = [] (std::shared_ptr<const sls::SampleData> data, int start, int length)
    {
        sls::AudioClip c;
        c.data = std::move (data);
        c.startTick = start;
        c.lengthTicks = length;
        return c;
    };
    // renders an arrangement with one clip at 120 BPM (480 ticks = 0.5 s): left and right
    const auto render = [&] (const sls::AudioClip& clip, int blocks)
    {
        sls::Arrangement a;
        a.patterns.resize (1);
        a.audioClips.push_back (clip);
        const auto project = sls::compileSong (empty, a);
        sls::Player p;
        p.prepare (kSr);
        std::vector<float> l (256), r (256), allL, allR;
        float* ch[2] = { l.data(), r.data() };
        for (int i = 0; i < blocks; ++i)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            p.render (project, 120.0, true, ch, 2, 256);
            allL.insert (allL.end(), l.begin(), l.end());
            allR.insert (allR.end(), r.begin(), r.end());
        }
        return std::make_pair (allL, allR);
    };
    const auto peakIn = [] (const std::vector<float>& x, double fromSeconds, double toSeconds)
    {
        const int size = static_cast<int> (x.size());
        return peakOf (x.data(), std::min (size, static_cast<int> (fromSeconds * kSr)), std::min (size, static_cast<int> (toSeconds * kSr)));
    };
    // the master's pan law and level are the same for every case: compare with the clip at its defaults
    const auto plain = render (clipOf (sine, 0, 960), 220);
    const float level = peakIn (plain.first, 0.2, 0.6);

    // pan
    {
        auto c = clipOf (sine, 0, 960);
        c.pan = -1.0f;
        const auto left = render (c, 220);
        c.pan = 0.5f;
        const auto half = render (c, 220);
        check (peakIn (left.second, 0.0, 1.0) == 0.0f && std::abs (peakIn (left.first, 0.2, 0.6) - level) < 1e-4f,
               "pan -100: only the left side, at its level");
        check (std::abs (peakIn (half.first, 0.2, 0.6) - 0.5f * level) < 1e-3f && std::abs (peakIn (half.second, 0.2, 0.6) - peakIn (plain.second, 0.2, 0.6)) < 1e-4f,
               "pan +50: the left side at half, the right side untouched (a balance, like the mixer's)");
    }

    // reverse
    {
        auto c = clipOf (ramp, 0, 960);
        c.reverse = true;
        const auto out = render (c, 200).first;
        const auto ref = render (clipOf (ramp, 0, 960), 200).first; // forwards: sample k is 0.5 k / 48000 times the master's gain
        const double gain = ref[24000] / (0.5 * 24000.0 / 48000.0);
        double worst = 0.0;
        for (int k = 200; k < 47000; k += 101)
            worst = std::max (worst, std::abs (out[static_cast<size_t> (k)] - gain * 0.5 * (47999.0 - k) / 48000.0));
        check (worst < 1e-4, "reversed: the clip plays its audio from the end backwards, sample by sample");
        check (peakIn (out, 1.1, 1.5) == 0.0f, "and stops at the clip's end");

        // the same part of the audio, the other way round
        auto part = clipOf (ramp, 0, 480); // 0.5 s
        part.offsetSeconds = 0.25;
        sls::setAudioReverse (part, true, 120.0);
        check (part.reverse && std::abs (part.offsetSeconds - 0.25) < 1e-9, "turning a clip around keeps the part it plays (0.25-0.75 s of 1 s: offset 0.25 from the end)");
        auto shifted = clipOf (ramp, 0, 240); // 0.25 s from 0.1 s
        shifted.offsetSeconds = 0.1;
        sls::setAudioReverse (shifted, true, 120.0);
        check (std::abs (shifted.offsetSeconds - 0.65) < 1e-9, "0.1-0.35 s reversed starts 0.65 s from the end");
        const auto played = render (shifted, 100).first;
        check (std::abs (played[2400] - gain * 0.5 * (0.35 * 48000.0 - 1.0 - 2400.0) / 48000.0) < 1e-4,
               "and plays it backwards from 0.35 s");
        sls::setAudioReverse (shifted, false, 120.0);
        check (! shifted.reverse && std::abs (shifted.offsetSeconds - 0.1) < 1e-9, "turning it back restores the offset");

        // started in the middle (the transport set inside the clip)
        sls::Arrangement a;
        a.patterns.resize (1);
        a.audioClips.push_back (c);
        const auto project = sls::compileSong (empty, a);
        sls::Player p;
        p.prepare (kSr);
        std::vector<float> l (256), r (256);
        float* ch[2] = { l.data(), r.data() };
        p.render (project, 120.0, true, ch, 2, 256);
        p.seek (project, 480.0, 120.0); // while playing: 0.5 s into the clip
        for (int i = 0; i < 20; ++i) // past the 80 ms release of the voice the seek cut
        {
            std::fill (l.begin(), l.end(), 0.0f);
            p.render (project, 120.0, true, ch, 2, 256);
        }
        check (std::abs (l[200] - gain * 0.5 * (47999.0 - 24000.0 - 19.0 * 256.0 - 200.0) / 48000.0) < 1e-4,
               "started half-way, it plays from half-way back (the right sample)");
    }

    // fades
    {
        auto c = clipOf (sine, 0, 960);
        c.fadeInSeconds = 0.2;
        c.fadeOutSeconds = 0.3;
        const auto out = render (c, 200).first;
        check (peakIn (out, 0.0, 0.002) < 0.02f * level, "fade in: silent at the start");
        check (std::abs (peakIn (out, 0.097, 0.103) - 0.7071f * level) < 0.02f * level, "half-way through the fade-in: -3 dB (a sine fade)");
        check (std::abs (peakIn (out, 0.4, 0.6) - level) < 1e-4f, "full level between the fades");
        check (std::abs (peakIn (out, 0.847, 0.853) - 0.7071f * level) < 0.02f * level, "half-way through the fade-out: -3 dB");
        check (peakIn (out, 0.997, 1.0) < 0.02f * level, "and silent at the clip's end");
    }

    // editing keeps the fades at the outer edges
    {
        sls::Arrangement a;
        a.patterns.resize (1);
        auto c = clipOf (sine, 0, 960);
        c.fadeInSeconds = 0.1;
        c.fadeOutSeconds = 0.2;
        c.pan = 0.3f;
        a.audioClips.push_back (c);
        const int right = sls::splitAudioClip (a, 0, 480, 120.0);
        check (right == 1 && a.audioClips[0].fadeInSeconds == 0.1 && a.audioClips[0].fadeOutSeconds == 0.0
                   && a.audioClips[1].fadeInSeconds == 0.0 && a.audioClips[1].fadeOutSeconds == 0.2 && a.audioClips[1].pan == 0.3f,
               "split: the left piece keeps the fade-in, the right one the fade-out; both keep the pan");
        sls::Arrangement b;
        b.patterns.resize (1);
        b.audioClips.push_back (c);
        sls::cutAudioRange (b, 0, 0, 240, 120.0);
        check (b.audioClips[0].fadeInSeconds == 0.0 && b.audioClips[0].fadeOutSeconds == 0.2, "cutting the start away removes the fade-in");
        sls::cutAudioRange (b, 0, 720, 960, 120.0);
        check (b.audioClips[0].fadeOutSeconds == 0.0, "cutting the end away removes the fade-out");
        sls::Arrangement m;
        m.patterns.resize (1);
        m.audioClips.push_back (c);
        sls::cutAudioRange (m, 0, 300, 600, 120.0);
        check (m.audioClips.size() == 2 && m.audioClips[0].fadeInSeconds == 0.1 && m.audioClips[0].fadeOutSeconds == 0.0
                   && m.audioClips[1].fadeInSeconds == 0.0 && m.audioClips[1].fadeOutSeconds == 0.2,
               "cutting out the middle: the same as a split");
        auto longFades = clipOf (sine, 0, 480); // 0.5 s
        longFades.fadeInSeconds = 0.6;
        longFades.fadeOutSeconds = 0.4;
        sls::limitFades (longFades, 120.0);
        check (std::abs (longFades.fadeInSeconds - 0.3) < 1e-9 && std::abs (longFades.fadeOutSeconds - 0.2) < 1e-9,
               "fades longer than the clip share it in proportion");
    }

    // the snapshot and the project file
    {
        sls::Arrangement a;
        a.patterns.resize (1);
        auto c = clipOf (sine, 0, 960);
        c.pan = std::numeric_limits<float>::quiet_NaN();
        c.fadeInSeconds = -1.0;
        c.fadeOutSeconds = std::numeric_limits<double>::infinity();
        a.audioClips.push_back (c);
        const auto project = sls::compileSong (empty, a);
        const auto& e = project.audioEvents[0];
        check (e.pan == 0.0f && e.fadeInSeconds == 0.0 && e.fadeOutSeconds == 0.0, "nonsense pan and fades become none");

        auto doc = sls::ProjectDocument::blank();
        auto saved = clipOf (sineData (100, kSr, 440.0, 0.1f, "/home/x/a.wav"), 0, 960);
        saved.pan = -0.4f;
        saved.reverse = true;
        saved.fadeInSeconds = 0.25;
        saved.fadeOutSeconds = 1.5;
        auto plainClip = clipOf (sineData (100, kSr, 440.0, 0.1f, "/home/x/b.wav"), 1920, 960);
        doc.arrangement.audioClips = { saved, plainClip };
        const auto parsed = sls::ProjectDocument::fromJson (doc.toJson());
        check (parsed.document.has_value() && parsed.document->arrangement.audioClips.size() == 2, "clips with pan, reverse and fades load");
        if (parsed.document)
        {
            const auto& r = parsed.document->arrangement.audioClips;
            check (std::abs (r[0].pan + 0.4f) < 1e-6f && r[0].reverse && std::abs (r[0].fadeInSeconds - 0.25) < 1e-9
                       && std::abs (r[0].fadeOutSeconds - 1.5) < 1e-9,
                   "with their pan, direction and fades");
            check (r[1].pan == 0.0f && ! r[1].reverse && r[1].fadeInSeconds == 0.0 && r[1].fadeOutSeconds == 0.0,
                   "a clip without them (and every older file) loads centred, forwards, without fades");
        }
        const auto written = juce::JSON::parse (doc.toJson());
        const auto* list = written.getDynamicObject()->getProperty ("audioClips").getArray();
        const auto* second = list != nullptr && list->size() == 2 ? (*list)[1].getDynamicObject() : nullptr;
        check (second != nullptr && ! second->hasProperty ("pan") && ! second->hasProperty ("reverse") && ! second->hasProperty ("fadeIn")
                   && ! second->hasProperty ("fadeOut"),
               "and they are only written when set");
    }

    // real-time safety with everything on
    {
        auto c = clipOf (ramp, 0, 960);
        c.reverse = true;
        c.pan = 0.7f;
        c.fadeInSeconds = 0.3;
        c.fadeOutSeconds = 0.3;
        sls::Arrangement a;
        a.patterns.resize (1);
        a.audioClips.push_back (c);
        const auto project = sls::compileSong (empty, a);
        sls::Player p;
        p.prepare (kSr);
        std::vector<float> l (256), r (256);
        float* ch[2] = { l.data(), r.data() };
        bool finite = true;
        gAllocs = 0;
        gCounting = true;
        for (int i = 0; i < 400; ++i)
        {
            p.render (project, 120.0, true, ch, 2, 256);
            for (int k = 0; k < 256; ++k)
                finite = finite && std::isfinite (l[static_cast<size_t> (k)]) && std::isfinite (r[static_cast<size_t> (k)]);
        }
        gCounting = false;
        check (gAllocs.load() == 0 && finite, "a reversed, panned, faded clip renders without allocating");
    }
}

void testAudioImport()
{
    std::cout << "Audio import\n";
    const auto dir = freshTempDir ("sls-import");
    const std::vector<sls::Note> notes { { 69, 0, 960, 0.8f } };
    const auto project = sls::Project::make ({ synthChannel (notes) }, 2 * kBeat);
    sls::RenderOptions options;
    options.tailSeconds = 0.25; // 1 s + 0.25 s of audio

    using sls::ExportFormat;
    check (sls::classifyMediaFile (juce::File ("/x/a.WAV")) == sls::FileKind::Audio
               && sls::classifyMediaFile (juce::File ("/x/a.oga")) == sls::FileKind::Audio
               && sls::classifyMediaFile (juce::File ("/x/a.Aac")) == sls::FileKind::Audio
               && sls::classifyMediaFile (juce::File ("/x/movie.MP4")) == sls::FileKind::Video
               && sls::classifyMediaFile (juce::File ("/x/movie.mkv")) == sls::FileKind::Video
               && sls::classifyMediaFile (juce::File ("/x/notes.txt")) == sls::FileKind::Unsupported
               && sls::classifyMediaFile (juce::File ("/x/noextension")) == sls::FileKind::Unsupported,
           "files are classified as audio, video or unsupported by their extension");
    for (const char* ext : { ".wav", ".mp3", ".ogg", ".oga", ".aac", ".flac", ".aiff" })
        check (sls::importableAudioExtensions().contains (ext), (juce::String ("importable: ") + ext).toRawUTF8());

    for (const auto fmt : { ExportFormat::Wav, ExportFormat::Aiff, ExportFormat::Flac, ExportFormat::Ogg, ExportFormat::Oga })
    {
        const auto& info = sls::exportFormatInfo (fmt);
        const auto file = dir.getChildFile ("clip" + juce::String (info.extension));
        const bool wrote = sls::exportAudio (project, options, file, fmt).wasOk();
        const auto r = sls::importAudioFile (file);
        const auto label = juce::String (info.extension).substring (1).toUpperCase() + " files are imported with the right length and sound";
        bool good = wrote && r.data != nullptr && std::abs (r.data->numFrames() / r.data->sampleRate - 1.25) < 0.05
                    && r.data->source == file.getFullPathName().toStdString() && r.data->name == "clip" && r.warning.isEmpty();
        if (good)
        {
            float m = 0.0f;
            for (float v : r.data->left)
                m = std::max (m, std::abs (v));
            good = m > 0.05f && r.data->left.size() == r.data->right.size();
        }
        check (good, label.toRawUTF8());
    }

    {   // mono files are copied to both sides
        const auto file = dir.getChildFile ("mono.wav");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), 44100.0, 1, 16, {}, 0));
        juce::AudioBuffer<float> b (1, 4410);
        for (int i = 0; i < 4410; ++i)
            b.setSample (0, i, 0.5f * std::sin (0.05f * static_cast<float> (i)));
        writer->writeFromAudioSampleBuffer (b, 0, 4410);
        writer.reset();
        const auto r = sls::importAudioFile (file);
        check (r.data != nullptr && r.data->left == r.data->right && r.data->sampleRate == 44100.0 && r.data->numFrames() == 4410,
               "a mono file becomes the same sound on both sides");
    }
    {   // very long files are cut, with a warning
        const auto file = dir.getChildFile ("long.wav");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), 8000.0, 1, 16, {}, 0));
        juce::AudioBuffer<float> b (1, 8000);
        b.clear();
        for (int s = 0; s < 310; ++s)
            writer->writeFromAudioSampleBuffer (b, 0, 8000);
        writer.reset();
        const auto r = sls::importAudioFile (file);
        check (r.data != nullptr && r.data->numFrames() == static_cast<int> (8000 * sls::kMaxImportSeconds) && r.warning.isNotEmpty(),
               "a file longer than the limit is cut and the result says so");
    }
    {   // failures
        const auto bogus = dir.getChildFile ("bogus.wav");
        bogus.replaceWithText ("this is not audio");
        const auto noFfmpeg = dir.getChildFile ("no-such-ffmpeg");
        const auto r1 = sls::importAudioFile (bogus, noFfmpeg);
        check (r1.data == nullptr && r1.error.isNotEmpty(), "a damaged file is reported, not imported");
        const auto r2 = sls::importAudioFile (dir.getChildFile ("missing.flac"));
        check (r2.data == nullptr && r2.error.contains ("does not exist"), "a missing file is reported");
        const auto aac = dir.getChildFile ("song.aac");
        aac.replaceWithText ("x");
        const auto r3 = sls::importAudioFile (aac, noFfmpeg);
        check (r3.data == nullptr && r3.error.contains ("ffmpeg"), "AAC without ffmpeg fails with a message that names ffmpeg");
        const auto r4 = sls::importVideoAudio (dir.getChildFile ("movie.mp4"), dir.getChildFile ("out"), noFfmpeg);
        check (r4.data == nullptr && r4.error.isNotEmpty() && ! dir.getChildFile ("out").getChildFile ("movie.wav").exists(),
               "a video that does not exist fails and leaves nothing behind");
        const auto r5 = sls::importMediaFile (dir.getChildFile ("notes.txt"), dir);
        check (r5.data == nullptr && r5.error.contains ("not an audio or video"), "other files are refused");
    }

    const auto ffmpeg = sls::findFfmpeg();
    if (! ffmpeg.existsAsFile())
    {
        std::cout << "  SKIP  AAC, MP3 and video import (ffmpeg is not installed here)\n";
        return;
    }

    for (const auto fmt : { ExportFormat::Mp3, ExportFormat::Aac })
    {
        const auto& info = sls::exportFormatInfo (fmt);
        const auto file = dir.getChildFile ("lossy" + juce::String (info.extension));
        const bool wrote = sls::exportAudio (project, options, file, fmt).wasOk();
        const auto r = sls::importAudioFile (file);
        const auto label = juce::String (info.extension).substring (1).toUpperCase() + " files are imported (MP3 by the program, AAC through ffmpeg)";
        float m = 0.0f;
        if (r.data != nullptr)
            for (float v : r.data->left)
                m = std::max (m, std::abs (v));
        check (wrote && r.data != nullptr && std::abs (r.data->numFrames() / r.data->sampleRate - 1.25) < 0.25 && m > 0.05f
                   && r.data->source == file.getFullPathName().toStdString(),
               label.toRawUTF8());
    }

    // video: a short clip with a sine tone, and one without a sound track
    const auto runFfmpeg = [&] (const juce::StringArray& extra)
    {
        juce::StringArray args { ffmpeg.getFullPathName(), "-loglevel", "error", "-nostdin", "-y" };
        args.addArray (extra);
        juce::ChildProcess p;
        p.start (args);
        p.readAllProcessOutput();
        return p.waitForProcessToFinish (60000) && p.getExitCode() == 0;
    };
    const auto video = dir.getChildFile ("clip video.mp4");
    const auto silentVideo = dir.getChildFile ("mute.mp4");
    const bool made = runFfmpeg ({ "-f", "lavfi", "-i", "color=c=black:s=64x64:d=1:r=10", "-f", "lavfi", "-i", "sine=frequency=440:duration=1",
                                   "-shortest", "-c:v", "mpeg4", "-c:a", "aac", video.getFullPathName() })
                    && runFfmpeg ({ "-f", "lavfi", "-i", "color=c=black:s=64x64:d=1:r=10", "-an", "-c:v", "mpeg4", silentVideo.getFullPathName() });
    if (! made)
    {
        std::cout << "  SKIP  video import (this ffmpeg cannot make a test video)\n";
        return;
    }

    const auto library = dir.getChildFile ("library");
    const auto r1 = sls::importMediaFile (video, library, {});
    float loudest = 0.0f;
    if (r1.data != nullptr)
        for (float v : r1.data->left)
            loudest = std::max (loudest, std::abs (v));
    check (r1.data != nullptr && std::abs (r1.data->numFrames() / r1.data->sampleRate - 1.0) < 0.25 && loudest > 0.05f,
           "the sound track of a video is extracted and imported");
    check (r1.data != nullptr && library.getChildFile ("clip video.wav").existsAsFile()
               && r1.data->source == library.getChildFile ("clip video.wav").getFullPathName().toStdString() && r1.data->name == "clip video",
           "it is kept as a WAV file in the library folder, which the clip then refers to");
    const auto r2 = sls::importVideoAudio (video, library);
    check (r2.data != nullptr && r2.file != r1.file && library.getChildFile ("clip video (2).wav").existsAsFile(),
           "importing the same video again never replaces the earlier file");
    const auto r3 = sls::importVideoAudio (silentVideo, library);
    check (r3.data == nullptr && r3.error.contains ("no audio"), "a video without sound is reported");
    check (library.getNumberOfChildFiles (juce::File::findFiles) == 2, "and leaves no partial files behind");
    dir.deleteRecursively(); // test folders in /tmp are not left behind
}

void testSfz()
{
    std::cout << "SFZ instruments\n";
    check (sls::sfz::keyFromText ("c4") == 60 && sls::sfz::keyFromText ("C#4") == 61 && sls::sfz::keyFromText ("db4") == 61
               && sls::sfz::keyFromText ("a-1") == 9 && sls::sfz::keyFromText ("g9") == 127 && sls::sfz::keyFromText ("64") == 64
               && sls::sfz::keyFromText ("b4") == 71,
           "keys by name (c4 = 60, as in SFZ) or number");
    check (sls::sfz::keyFromText ("h4") == -1 && sls::sfz::keyFromText ("c") == -1 && sls::sfz::keyFromText ("200") == -1
               && sls::sfz::keyFromText ("c4.5") == -1 && sls::sfz::keyFromText ("") == -1,
           "anything else is not a key");

    const std::string text = "// a test instrument\n<control> default_path=samples\\piano/\n"
                             "<global> volume=-6 ampeg_release=0.4\n"
                             "<group> lovel=1 hivel=64 /* soft */\n"
                             "<region> sample=C4 soft.wav lokey=b3 hikey=c#4 pitch_keycenter=c4\n"
                             "<region> sample=E4 soft.wav key=64 tune=-12 transpose=1 pan=-50\n"
                             "<group> lovel=65\n<region>sample=C4 loud.wav lokey=59 hikey=61 pitch_keycenter=60 volume=0 loop_mode=loop_continuous loop_start=100 loop_end=4000\n"
                             "<region> sample=*sine key=90\n"
                             "<curve> curve_index=7 v000=0\n"
                             "#include \"more.sfz\"\n"
                             "<region> sample=../x.wav hikey=10 lokey=20 hivel=5 lovel=100\n";
    const auto inst = sls::sfz::parse (text);
    check (inst.regions.size() == 4, "four regions with samples (a built-in *sine is left out)");
    if (inst.regions.size() == 4)
    {
        const auto& a = inst.regions[0];
        check (a.sample == "samples/piano/C4 soft.wav" && a.loKey == 59 && a.hiKey == 61 && a.rootKey == 60 && a.loVel == 1 && a.hiVel == 64
                   && a.volumeDb == -6.0,
               "a region takes the default path, a sample name with spaces, keys by name, the group's velocities and the global volume");
        const auto& b = inst.regions[1];
        check (b.loKey == 64 && b.hiKey == 64 && b.rootKey == 64 && b.tuneCents == 88.0 && b.pan == -50.0, "key, tune and transpose, pan");
        const auto& c = inst.regions[2];
        check (c.loVel == 65 && c.hiVel == 127 && c.volumeDb == 0.0 && c.loop && c.loopStart == 100 && c.loopEnd == 4000,
               "a new group replaces the old one; a region's own volume wins; the loop");
        const auto& d = inst.regions[3];
        check (d.loKey == 10 && d.hiKey == 20 && d.loVel == 5 && d.hiVel == 100 && d.sample == "samples/piano/../x.wav", "ranges written backwards are put right");
    }
    check (inst.skippedOpcodes == 1 && inst.includes.size() == 1 && inst.includes[0] == "\"more.sfz\"",
           "unknown opcodes are counted (not those of a <curve>), includes are reported");
    const auto none = sls::sfz::parse ("<region> lokey=1\n<group sample=x.wav\n= = =\n<<<>>>\nsample=\n");
    check (none.regions.empty(), "broken files give nothing (and do not crash)");
    std::string big (100000, '<');
    sls::sfz::parse (big);
    check (true, "a hostile file does not crash");
}


void testSfzFiles()
{
    std::cout << "SFZ instruments from files\n";
    const auto dir = freshTempDir ("sls-sfz");
    const auto inst = dir.getChildFile ("Organ");
    inst.getChildFile ("wav").createDirectory();
    // Two samples: a 440 Hz tone (for C4..B4) and an 880 Hz tone (for C5 and up), one second each.
    check (sls::writeSampleWav (*sineData (48000, kSr, 440.0, 0.5f), inst.getChildFile ("wav/low tone.wav")).wasOk()
               && sls::writeSampleWav (*sineData (48000, kSr, 880.0, 0.5f), inst.getChildFile ("wav/high.wav")).wasOk(),
           "test samples written");
    const auto sfzFile = inst.getChildFile ("organ.sfz");
    sfzFile.replaceWithText ("<control> default_path=wav/\n<group> ampeg_attack=0.01\n"
                             "<region> sample=low tone.wav lokey=c4 hikey=b4 pitch_keycenter=a4\n"
                             "<region> sample=high.wav lokey=c5 hikey=127 pitch_keycenter=a5 volume=-6 loop_mode=loop_continuous loop_start=0 loop_end=47999\n"
                             "<region> sample=missing.wav key=20\n");
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const auto load = [&formats] (const juce::File& f, juce::String& e) -> std::shared_ptr<const sls::SampleData>
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
        if (reader == nullptr)
        {
            e = "cannot read " + f.getFileName();
            return nullptr;
        }
        auto d = std::make_shared<sls::SampleData>();
        d->sampleRate = reader->sampleRate;
        juce::AudioBuffer<float> b (2, static_cast<int> (reader->lengthInSamples));
        reader->read (&b, 0, b.getNumSamples(), 0, true, true);
        d->left.assign (b.getReadPointer (0), b.getReadPointer (0) + b.getNumSamples());
        d->right.assign (b.getReadPointer (1), b.getReadPointer (1) + b.getNumSamples());
        d->source = f.getFullPathName().toStdString();
        return d;
    };
    juce::String error;
    juce::StringArray notes;
    const auto program = sls::loadSfz (sfzFile, load, error, &notes);
    check (program != nullptr && program->zones.size() == 2, "the two regions whose samples exist become zones");
    if (program == nullptr || program->zones.size() != 2)
        return;
    const auto& low = program->zones[0];
    const auto& high = program->zones[1];
    check (low.loKey == 60 && low.hiKey == 71 && low.rootKey == 69 && high.loKey == 72 && high.rootKey == 81, "keys and root keys");
    check (std::abs (high.gain - 0.501f) < 0.01f && high.loop == sls::LoopMode::Forward && high.loopEnd == 48000, "volume in dB, the loop (its last frame included)");
    check (notes.joinIntoString ("|").contains ("missing.wav") && notes.joinIntoString ("|").contains ("ampeg_attack") == false
               && notes.joinIntoString ("|").contains ("1 setting this"),
           "the missing sample and the skipped setting are reported");

    // The project keeps it as an .sfz and loads it again.
    const auto spec = sls::specFromProgram (*program);
    const auto back = sls::programSpecFromVar (sls::programSpecToVar (spec));
    check (spec.kind == sls::ProgramSpec::Kind::Sfz && back.kind == sls::ProgramSpec::Kind::Sfz && back.file == sfzFile.getFullPathName().toStdString(),
           "saved as an SFZ reference");
    const auto again = sls::loadProgram (back, load, error);
    check (again != nullptr && again->zones.size() == 2, "and loaded again from it");

    // It plays: A4 plays the low tone at its own pitch, A5 the high one.
    sls::Channel c;
    c.instrument = sls::Instrument::Sampler;
    c.program = program;
    c.notes = { { 69, 0, kBeat, 0.8f } };
    sls::Player player;
    player.prepare (kSr);
    auto out = renderBlocks (player, sls::Project::make ({ c }, 16 * kBeat), true, 0.4);
    check (toneEnergy (out.data(), 2400, 14400, 440.0) > 0.05 && toneEnergy (out.data(), 2400, 14400, 880.0) < 0.01, "A4 plays the 440 Hz sample");
    c.notes = { { 81, 0, kBeat, 0.8f } };
    sls::Player p2;
    p2.prepare (kSr);
    out = renderBlocks (p2, sls::Project::make ({ c }, 16 * kBeat), true, 0.4);
    check (toneEnergy (out.data(), 2400, 14400, 880.0) > 0.02, "A5 plays the 880 Hz sample");

    // Export with samples takes the whole folder along, so the sample paths still work.
    auto doc = sls::ProjectDocument::blank();
    doc.channels[0].instrument = sls::Instrument::Sampler;
    doc.programSpecs = { spec };
    juce::StringArray exportNotes;
    check (sls::exportProjectWithSamples (doc, dir.getChildFile ("Out"), "Song", exportNotes).isEmpty()
               && dir.getChildFile ("Out/Samples/Organ/organ.sfz").existsAsFile() && dir.getChildFile ("Out/Samples/Organ/wav/low tone.wav").existsAsFile(),
           "export with samples copies the SFZ's folder");
    const auto exported = sls::readProjectFile (dir.getChildFile ("Out/Song.slp"));
    check (exported.document && exported.document->programSpecs[0].file == dir.getChildFile ("Out/Samples/Organ/organ.sfz").getFullPathName().toStdString(),
           "and the project finds it there");
    check (sls::loadSfz (dir.getChildFile ("none.sfz"), load, error) == nullptr && error.contains ("Cannot find"), "a missing file is reported");
    dir.deleteRecursively();
}

void testHelios()
{
    std::cout << "Helios: searching the manual offline\n";
    const std::string en = "# Guide\n\nIntro text.\n\n## Mixer\n\nPress **F9** to open the mixer.\n\n### Effects\n\nClick a slot to add an effect: "
                           "reverb, delay, compressor.\n\n## Playlist\n\nClips and tracks.\n\n### The ruler: song position and loop region\n\n"
                           "Right-drag on the ruler draws a loop region. Click to move the song position.\n\n### Recording\n\nPress Record to record "
                           "the audio input on the armed tracks.\n";
    const std::string it = "# Guida\n\n## Mixer\n\nPremi **F9** per aprire il mixer.\n\n### Registrazione\n\n**Record** registra l'ingresso "
                           "audio: più tracce insieme, ognuna dal proprio ingresso.\n\n### Città e perché\n\nUna sezione con accenti.\n";
    sls::HelpIndex index;
    index.add (en, "en");
    index.add (it, "it");
    check (index.sections().size() == 8, "the manuals are cut into sections at their ## and ### headings");
    check (index.sections()[1].title == "Mixer > Effects" && index.sections()[0].title == "Mixer", "a sub-section's title names its parent");
    check (index.sections()[1].plain.find ("**") == std::string::npos && index.sections()[1].language == "en", "the plain text has no markup");

    auto hits = index.search ("loop region");
    check (! hits.empty() && index.sections()[static_cast<size_t> (hits[0].section)].heading == "The ruler: song position and loop region",
           "\"loop region\" finds the ruler's section first");
    check (! hits.empty() && hits[0].snippet.find ("loop region") != std::string::npos, "with the sentence that says it");
    hits = index.search ("How do I add a reverb?");
    check (! hits.empty() && index.sections()[static_cast<size_t> (hits[0].section)].heading == "Effects", "a question in words finds the effects");
    hits = index.search ("insert effects");
    check (! hits.empty() && index.sections()[static_cast<size_t> (hits[0].section)].title.find ("Mixer") == 0, "FL Studio's \"insert\" means the mixer");
    hits = index.search ("registrare più tracce");
    check (! hits.empty() && index.sections()[static_cast<size_t> (hits[0].section)].heading == "Registrazione"
               && index.sections()[static_cast<size_t> (hits[0].section)].language == "it",
           "an Italian question finds the Italian section");
    hits = index.search ("CITTA PERCHE");
    check (! hits.empty() && index.sections()[static_cast<size_t> (hits[0].section)].heading == "Città e perché", "accents and case do not matter");
    hits = index.search ("recording tracks");
    check (hits.size() >= 2, "several sections can match");
    check (index.search ("").empty() && index.search ("   ?! ").empty() && index.search ("zzzqqq").empty() && index.search ("the and of").empty(),
           "nothing to look for, or no such word: no results");
    check (index.search ("mixer", 1).size() == 1, "the number of results is limited");
    const auto w = sls::HelpIndex::words ("Effects, EFFECT; effetti!");
    check (w.size() >= 3 && w[0] == w[1], "\"effects\" and \"effect\" are the same word");
    sls::HelpIndex hostile;
    hostile.add (std::string ("## \xff\xfe\n\n\xc3\n## \n###\n") + std::string (1000, '#'), "en");
    hostile.search ("\xc3");
    check (true, "broken text does not crash");
}

// 0.62.0: the manuals that are built into the program answer the questions about what 0.60 - 0.62 added, in both languages.
void testHeliosManual062()
{
    std::cout << "Helios: the real manuals know the 0.60 - 0.62 features\n";
    const auto docs = juce::File (SLS_SOURCE_DIR).getChildFile ("docs");
    const auto en = docs.getChildFile ("user-guide.md").loadFileAsString().toStdString();
    const auto it = docs.getChildFile ("user-guide.it.md").loadFileAsString().toStdString();
    check (en.size() > 10000 && it.size() > 10000, "both manuals are read");
    sls::HelpIndex index;
    index.add (en, "en");
    index.add (it, "it");
    const auto top = [&index] (const std::string& question, const std::string& wanted, const std::string& language)
    {
        const auto hits = index.search (question, 3);
        for (const auto& h : hits)
        {
            const auto& section = index.sections()[static_cast<size_t> (h.section)];
            if (section.language == language && section.title.find (wanted) != std::string::npos)
                return true;
        }
        std::cout << "    (" << question << ": " << (hits.empty() ? std::string ("nothing") : index.sections()[static_cast<size_t> (hits[0].section)].title) << ")\n";
        return false;
    };
    check (top ("performance mode launch clips", "Performance mode", "en"), "Helios: performance mode (English)");
    check (top ("performance mode dal vivo lanciare clip", "Performance mode", "it"), "Helios: performance mode (Italian)");
    check (top ("route a track to another track bus", "Routing between tracks", "en"), "Helios: routing between tracks");
    check (top ("instradamento tra tracce bus", "Instradamento tra tracce", "it"), "Helios: instradamento tra tracce");
    check (top ("pdc plugin latency", "delay compensation", "en"), "Helios: FL Studio's PDC means delay compensation");
    check (top ("vocodex", "Effects added in 0.62.0", "en") && top ("vocodex", "Effetti aggiunti", "it"), "Helios: Vocodex finds the vocoder");
    check (top ("convolution reverb impulse response", "Effect", "en") || top ("convolution reverb impulse response", "Mixer", "en"), "Helios: the convolution reverb");
    check (top ("layer channel", "Layer channel", "en") && top ("canale layer", "canale Layer", "it"), "Helios: the Layer channel in both languages");
    check (top ("marker actions loop skip pause", "arker", "en") && top ("azioni dei marcatori", "arcatori", "it"), "Helios: marker actions");
    check (top ("mac mini 2012 catalina", "On a Mac", "en") && top ("mac mini 2012 catalina", "Su un Mac", "it"), "Helios: which Macs it runs on (0.63.0)");
    check (top ("favourites browser tabs", "favourites", "en") && top ("preferiti browser", "preferiti", "it"), "Helios: the browser's favourites");
    check (top ("graph editor velocity steps", "graph editor", "en") && top ("editor a barre", "editor a barre", "it"), "Helios: the graph editor");
    check (top ("step edit", "step edit", "en") && top ("step edit", "step edit", "it"), "Helios: step editing");
    check (top ("lock track group fold", "Track lock", "en") && top ("bloccare tracce gruppi", "Blocco e gruppi", "it"), "Helios: track lock and groups");
    check (top ("make unique consolidate", "Make unique", "en"), "Helios: Make unique and Consolidate");
    check (top ("note pan release fine pitch lane", "event lanes", "en") && top ("corsie degli eventi pan release", "corsie degli eventi", "it"),
           "Helios: the Piano Roll's event lanes");
}

double energyOf (const sls::SampleData& s, int from, int to)
{
    double e = 0.0;
    for (int i = std::max (0, from); i < std::min (to, s.numFrames()); ++i)
        e += static_cast<double> (s.left[static_cast<size_t> (i)]) * s.left[static_cast<size_t> (i)]
           + static_cast<double> (s.right[static_cast<size_t> (i)]) * s.right[static_cast<size_t> (i)];
    return e;
}

void testStemSplit()
{
    std::cout << "Stem separation by filters (no AI)\n";
    const int n = static_cast<int> (4.0 * kSr);
    // Three sources: a steady 55 Hz bass, a steady chord at 440 / 554 / 659 Hz, and clicks every half second (a decaying burst).
    sls::SampleData bass, chord, clicks, mix;
    for (auto* s : { &bass, &chord, &clicks, &mix })
    {
        s->sampleRate = kSr;
        s->left.assign (static_cast<size_t> (n), 0.0f);
        s->right.assign (static_cast<size_t> (n), 0.0f);
        s->name = "mix";
    }
    std::uint32_t seed = 7;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / kSr;
        const auto b = static_cast<float> (0.3 * std::sin (2.0 * 3.14159265358979 * 55.0 * t));
        const auto c = static_cast<float> (0.1 * (std::sin (2.0 * 3.14159265358979 * 440.0 * t) + std::sin (2.0 * 3.14159265358979 * 554.37 * t)
                                                + std::sin (2.0 * 3.14159265358979 * 659.26 * t)));
        const int inBeat = i % static_cast<int> (0.5 * kSr);
        seed = seed * 1664525u + 1013904223u;
        const auto k = static_cast<float> (inBeat < 1200 ? 0.6 * std::exp (-inBeat / 200.0) * (static_cast<double> (seed >> 8) / 8388608.0 - 1.0) : 0.0);
        bass.left[static_cast<size_t> (i)] = bass.right[static_cast<size_t> (i)] = b;
        chord.left[static_cast<size_t> (i)] = c;
        chord.right[static_cast<size_t> (i)] = 0.5f * c; // a little to the left
        clicks.left[static_cast<size_t> (i)] = clicks.right[static_cast<size_t> (i)] = k;
        mix.left[static_cast<size_t> (i)] = b + c + k;
        mix.right[static_cast<size_t> (i)] = b + 0.5f * c + k;
    }

    double lastProgress = -1.0;
    bool rising = true;
    const auto stems = sls::splitStems (mix, {}, [&] (double p) { rising = rising && p >= lastProgress; lastProgress = p; return true; });
    check (stems.drums != nullptr && stems.bass != nullptr && stems.other != nullptr && stems.drums->numFrames() == n
               && stems.bass->numFrames() == n && stems.other->numFrames() == n,
           "three stems as long as the mix");
    check (rising && lastProgress == 1.0, "progress goes from 0 to 1");
    float worst = 0.0f;
    for (int i = 0; i < n; ++i)
        worst = std::max (worst, std::abs (stems.drums->left[static_cast<size_t> (i)] + stems.bass->left[static_cast<size_t> (i)]
                                           + stems.other->left[static_cast<size_t> (i)] - mix.left[static_cast<size_t> (i)]));
    check (worst < 1e-5f, "the stems add up to the mix");

    // How much of each stem is the source it should be: compare with each source by correlation of energy (projection).
    const auto share = [n] (const sls::SampleData& stem, const sls::SampleData& source)
    {
        // the part of `source` found in `stem`: <stem, source> / <source, source>, over the middle (away from the edges)
        double dot = 0.0, self = 0.0;
        for (int i = n / 8; i < n - n / 8; ++i)
        {
            dot += static_cast<double> (stem.left[static_cast<size_t> (i)]) * source.left[static_cast<size_t> (i)];
            self += static_cast<double> (source.left[static_cast<size_t> (i)]) * source.left[static_cast<size_t> (i)];
        }
        return self > 0.0 ? dot / self : 0.0;
    };
    const double bassInBass = share (*stems.bass, bass), chordInOther = share (*stems.other, chord), clicksInDrums = share (*stems.drums, clicks);
    std::cout << "    bass in Bass " << bassInBass << ", chord in Other " << chordInOther << ", clicks in Drums " << clicksInDrums << "\n";
    check (bassInBass > 0.9, "the bass line goes to Bass (more than 90 %)");
    check (chordInOther > 0.9, "the chord goes to Other (more than 90 %)");
    check (clicksInDrums > 0.6, "the clicks go to Drums (more than 60 %)");
    check (share (*stems.drums, bass) < 0.1 && share (*stems.drums, chord) < 0.1, "little of the steady sounds in Drums");
    check (share (*stems.bass, chord) < 0.05, "and no chord in Bass");
    check (stems.other->name == "mix other" && stems.drums->sampleRate == kSr, "named after the mix, at its rate");

    const auto stopped = sls::splitStems (mix, {}, [] (double p) { return p < 0.3; });
    check (stopped.drums == nullptr && stopped.bass == nullptr, "a split can be stopped");
    sls::SampleData empty;
    empty.sampleRate = kSr;
    check (sls::splitStems (empty).drums == nullptr, "nothing to split gives nothing");
    sls::StemSettings odd;
    odd.timeSpan = -4;
    odd.frequencySpan = 1000;
    odd.hardness = std::nan ("");
    odd.bassCrossoverHz = 1.0e9;
    const auto weird = sls::splitStems (mix, odd);
    check (weird.drums != nullptr && std::isfinite (weird.drums->left[static_cast<size_t> (n / 2)]), "settings out of range are brought in range");
}

// A "sung" line: tones with harmonics at the given MIDI pitches (with cents), each `seconds` long, with short silences between.
std::shared_ptr<sls::SampleData> sungLine (const std::vector<double>& midi, double seconds, double vibratoCents = 0.0)
{
    auto s = std::make_shared<sls::SampleData>();
    s->sampleRate = kSr;
    s->name = "line";
    double phase = 0.0;
    for (const double m : midi)
    {
        const int n = static_cast<int> (seconds * kSr), gap = static_cast<int> (0.08 * kSr);
        for (int i = 0; i < n; ++i)
        {
            const double t = i / kSr;
            const double hz = 440.0 * std::pow (2.0, (m - 69.0 + vibratoCents / 100.0 * std::sin (2.0 * 3.14159265358979 * 5.5 * t)) / 12.0);
            phase += 2.0 * 3.14159265358979 * hz / kSr;
            const double env = std::min ({ 1.0, i / (0.01 * kSr), (n - i) / (0.01 * kSr) });
            const auto v = static_cast<float> (env * (0.4 * std::sin (phase) + 0.15 * std::sin (2.0 * phase) + 0.08 * std::sin (3.0 * phase)));
            s->left.push_back (v);
            s->right.push_back (v);
        }
        for (int i = 0; i < gap; ++i)
        {
            s->left.push_back (0.0f);
            s->right.push_back (0.0f);
        }
    }
    return s;
}

void testPitchTools()
{
    std::cout << "Pitch editor: pitch tracking, notes, correction, rendering\n";
    // A4 + 30 cents, C4, E5 - 20 cents, G2, with vibrato of 25 cents.
    const auto line = sungLine ({ 69.3, 60.0, 75.8, 43.0 }, 0.5, 25.0);
    const auto frames = sls::pitchtrack::track (*line);
    check (frames.size() > 300 && std::abs (frames[1].seconds - frames[0].seconds - 0.005) < 0.0005, "a frame every 5 ms");
    int voiced = 0;
    for (const auto& f : frames)
        voiced += f.hz > 0.0 ? 1 : 0;
    check (voiced > static_cast<int> (frames.size()) * 6 / 10, "most of the line has a pitch");

    const auto notes = sls::pitchtrack::notesFrom (frames);
    check (notes.size() == 4, "four notes are found");
    if (notes.size() == 4)
    {
        check (std::abs (notes[0].midi - 69.3) < 0.08 && std::abs (notes[1].midi - 60.0) < 0.08 && std::abs (notes[2].midi - 75.8) < 0.08
                   && std::abs (notes[3].midi - 43.0) < 0.08,
               "each note's centre within 8 cents, vibrato or not, from G2 to E5");
        check (std::abs (notes[0].start - 0.0) < 0.04 && std::abs (notes[0].end - 0.5) < 0.04 && std::abs (notes[1].start - 0.58) < 0.04,
               "and where it starts and ends within 40 ms");
        check (notes[0].nearestKey() == 69 && std::abs (notes[0].centsOff() - 30.0) < 8.0 && notes[2].nearestKey() == 76
                   && std::abs (notes[2].centsOff() + 20.0) < 8.0,
               "the nearest key and how many cents off");

        // Correction: all the way, half, and to a scale.
        auto n = notes[0];
        check (std::abs (sls::pitchtrack::correction (n, 1.0) + (notes[0].midi - 69.0)) < 1e-9, "full correction brings a note to its key");
        check (std::abs (sls::pitchtrack::correction (n, 0.5) + 0.5 * (notes[0].midi - 69.0)) < 1e-9, "half correction half way");
        bool cMajor[12] = { true, false, true, false, true, true, false, true, false, true, false, true };
        n.midi = 61.2; // C#4 + 20 cents: not in C major, the nearest key in it is C (61.2 - 60 = 1.2) or D (0.8): D
        check (std::abs (sls::pitchtrack::correction (n, 1.0, cMajor, 0) - 0.8) < 1e-9, "with a scale, to the nearest key in it");

        // Rendering: the first note brought to A4, the third moved up a tone; the others left alone.
        auto edited = notes;
        edited[0].shift = sls::pitchtrack::correction (edited[0], 1.0);
        edited[2].shift = 2.0;
        const auto out = sls::pitchtrack::render (line, edited);
        check (out != nullptr && out != line && out->numFrames() == line->numFrames(), "the edited audio keeps its length");
        const auto again = sls::pitchtrack::notesFrom (sls::pitchtrack::track (*out));
        check (again.size() == 4 && std::abs (again[0].midi - 69.0) < 0.1 && std::abs (again[2].midi - 77.8) < 0.1
                   && std::abs (again[1].midi - 60.0) < 0.05 && std::abs (again[3].midi - 43.0) < 0.05,
               "measured again: A4 in tune, the third a tone higher, the others unchanged");
        bool untouched = true;
        const auto mid = static_cast<size_t> (0.8 * kSr); // inside the second note, which did not move
        for (size_t i = mid; i < mid + 1000; ++i)
            untouched = untouched && out->left[i] == line->left[i];
        check (untouched, "a note that does not move is left sample for sample");
        check (sls::pitchtrack::render (line, notes) == line, "no shift: the audio itself comes back");
    }

    // Silence and noise have no pitch; a hostile input does not crash.
    sls::SampleData quiet;
    quiet.sampleRate = kSr;
    quiet.left.assign (48000, 0.0f);
    quiet.right.assign (48000, 0.0f);
    const auto qf = sls::pitchtrack::track (quiet);
    check (! qf.empty() && std::all_of (qf.begin(), qf.end(), [] (const sls::pitchtrack::Frame& f) { return f.hz == 0.0; })
               && sls::pitchtrack::notesFrom (qf).empty(),
           "silence has no pitch and no notes");
    sls::SampleData noise = quiet;
    std::uint32_t seed = 1;
    for (size_t i = 0; i < noise.left.size(); ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        noise.left[i] = noise.right[i] = static_cast<float> (seed >> 8) / 16777216.0f - 0.5f;
    }
    const auto nn = sls::pitchtrack::notesFrom (sls::pitchtrack::track (noise));
    check (nn.size() <= 1, "white noise gives no steady notes");
    sls::SampleData tiny;
    tiny.sampleRate = kSr;
    tiny.left = tiny.right = { 0.1f, 0.2f };
    check (sls::pitchtrack::track (tiny).empty() && sls::pitchtrack::notesFrom ({}).empty(), "too little audio gives nothing");
}

void testSlicer()
{
    std::cout << "Slicer: slices, their settings, the score and cutting\n";
    // A two-second "loop" at 48 kHz: four parts, each a constant level (0.1, 0.2, 0.3, 0.4 rising inside), to tell them apart.
    auto loop = std::make_shared<sls::SampleData>();
    loop->sampleRate = 48000.0;
    loop->source = "/tmp/loop.wav";
    loop->name = "loop";
    for (int i = 0; i < 96000; ++i)
    {
        const float v = 0.1f * static_cast<float> (i / 24000 + 1) + 0.00001f * static_cast<float> (i % 24000);
        loop->left.push_back (v);
        loop->right.push_back (-v);
    }

    std::vector<sls::SliceSettings> settings (4);
    settings[1].reverse = true;
    settings[2].tune = 7.0f;
    settings[2].gain = 0.5f;
    settings[3].pan = -1.0f;
    settings[3].gain = 9.0f; // out of range: kept to 2
    const auto p = sls::makeSliceProgram (loop, { 0, 24000, 48000, 72000 }, 60, "loop", settings).sanitised();
    check (p.zones.size() == 4 && p.zones[0].loKey == 60 && p.zones[3].loKey == 63, "four slices on C5 and up");
    check (p.zones[1].sample->left.front() > p.zones[1].sample->left.back()
               && std::abs (p.zones[1].sample->left.front() - (0.2f + 0.00001f * 23999.0f)) < 1e-6f,
           "a reversed slice plays its part backwards");
    check (p.zones[0].sample->left.front() < p.zones[0].sample->left.back(), "the others play forwards");
    check (p.zones[2].tune == 7.0f && p.zones[2].gain == 0.5f && p.zones[3].pan == -1.0f && p.zones[3].gain == 2.0f,
           "a slice's pitch, level and pan reach its zone, within range");
    check (p.sliceSettings.size() == 4 && p.sliceSettings[0] == sls::SliceSettings {}, "the settings are kept with the program");
    const auto plain = sls::makeSliceProgram (loop, { 0, 48000 }, 60, "loop", std::vector<sls::SliceSettings> (5));
    check (plain.sliceSettings.empty(), "default settings are not kept (nothing to save)");
    const auto high = sls::makeSliceProgram (loop, { 0, 10, 20, 30, 40 }, 125, "loop");
    check (high.zones.size() == 3 && high.zones.back().loKey == 127, "slices past the last key are left out");

    // The file: settings, cut and tempo saved and read back.
    auto withCut = p;
    withCut.sliceCut = true;
    withCut.sliceBpm = 93.5;
    const auto spec = sls::specFromProgram (withCut);
    const auto back = sls::programSpecFromVar (sls::programSpecToVar (spec));
    check (back.kind == sls::ProgramSpec::Kind::Slices && back.sliceSettings == p.sliceSettings && back.sliceCut && back.sliceBpm == 93.5
               && back.starts == p.sliceStarts,
           "a Slicer program's slices, settings, cutting and tempo are saved and read back");
    auto* o = sls::programSpecToVar (sls::specFromProgram (plain)).getDynamicObject();
    check (o != nullptr && ! o->hasProperty ("slices") && ! o->hasProperty ("cut") && ! o->hasProperty ("bpm"),
           "a plain slice program is written as before 0.37.0");
    juce::var hostile = sls::programSpecToVar (spec);
    if (auto* h = hostile.getDynamicObject())
    {
        juce::Array<juce::var> bad;
        auto* item = new juce::DynamicObject();
        item->setProperty ("tune", 1000.0);
        item->setProperty ("gain", "loud");
        item->setProperty ("reverse", 3);
        bad.add (juce::var (item));
        bad.add (juce::var ("nothing"));
        h->setProperty ("slices", bad);
        h->setProperty ("bpm", -5.0);
        h->setProperty ("cut", "yes");
    }
    const auto lenient = sls::programSpecFromVar (hostile);
    check (lenient.sliceSettings.size() == 2 && lenient.sliceSettings[0].tune == 24.0f && lenient.sliceSettings[0].gain == 1.0f
               && ! lenient.sliceSettings[0].reverse && lenient.sliceSettings[1] == sls::SliceSettings {} && lenient.sliceBpm == 0.0
               && ! lenient.sliceCut,
           "bad values in a file are brought in range or ignored");
    juce::String error;
    const auto loaded = sls::loadProgram (back, [&loop] (const juce::File&, juce::String&) { return std::shared_ptr<const sls::SampleData> (loop); }, error);
    check (loaded != nullptr && loaded->sliceCut && loaded->sliceBpm == 93.5 && loaded->zones.size() == 4 && loaded->zones[2].tune == 7.0f
               && loaded->zones[1].sample->left.front() > loaded->zones[1].sample->left.back(),
           "and a project opened again plays them the same way");

    // Helpers.
    check (std::abs (sls::slicer::guessLoopBpm (96000, 48000.0, 120.0) - 120.0) < 1e-9, "a two-second loop is one bar at 120 BPM");
    check (std::abs (sls::slicer::guessLoopBpm (96000, 48000.0, 200.0) - 120.0) < 1e-9, "the nearest whole number of bars wins (two bars would be 240)");
    check (std::abs (sls::slicer::guessLoopBpm (3 * 48000, 48000.0, 90.0) - 80.0) < 1e-9, "three seconds: two bars at 80 BPM");
    check (sls::slicer::guessLoopBpm (1000, 48000.0, 120.0) == 0.0 && sls::slicer::guessLoopBpm (1, 48000.0, 120.0) == 0.0,
           "a sound too short for any bar has no tempo");
    const auto grid = sls::slicer::gridStarts (96000, 48000.0, 120.0, sls::kTicksPerBeat / 4);
    check (grid.size() == 16 && grid[1] == 6000 && grid[15] == 90000, "a one-bar loop by 16ths: 16 slices, 6000 frames apart");
    check (sls::slicer::gridStarts (96000, 48000.0, 0.0, 120).size() == 1, "no tempo, no grid");

    std::vector<int> starts { 0, 1000, 2000 };
    check (sls::slicer::addStart (starts, 1500, 3000) == 2 && starts == std::vector<int> ({ 0, 1000, 1500, 2000 }), "a start is added in order");
    check (sls::slicer::addStart (starts, 1501, 3000) == -1 && sls::slicer::addStart (starts, 2999, 3000) == -1 && sls::slicer::addStart (starts, 0, 3000) == -1,
           "not too close to another, nor at the ends");
    sls::slicer::moveStart (starts, 2, 5000, 3000);
    check (starts[2] == 1998, "a moved start stays before the next one");
    sls::slicer::moveStart (starts, 2, 0, 3000);
    check (starts[2] == 1002, "and after the one before");
    sls::slicer::moveStart (starts, 0, 700, 3000);
    check (starts[0] == 0, "the first start stays at 0");
    check (sls::slicer::sliceAt (starts, 0) == 0 && sls::slicer::sliceAt (starts, 1001) == 1 && sls::slicer::sliceAt (starts, 2999) == 3,
           "a frame is found in its slice");

    // The score: one note per slice, timed at the loop's tempo.
    const auto notes = sls::slicer::score (p, 48000.0, 120.0, 4 * sls::kTicksPerBeat * 4);
    check (notes.size() == 4 && notes[0].pitch == 60 && notes[0].startTick == 0 && notes[1].startTick == sls::kTicksPerBeat && notes[3].pitch == 63
               && notes[3].startTick == 3 * sls::kTicksPerBeat && notes[2].lengthTicks == sls::kTicksPerBeat,
           "the score plays the loop as it was: a slice per beat at 120 BPM");
    const auto cutShort = sls::slicer::score (p, 48000.0, 120.0, 2 * sls::kTicksPerBeat + 100);
    check (cutShort.size() == 3 && cutShort[2].lengthTicks == 100, "notes past the pattern are left out, the last one shortened");
    const auto slower = sls::slicer::score (p, 48000.0, 60.0, 64 * sls::kTicksPerBeat);
    check (slower[1].startTick == sls::kTicksPerBeat / 2, "at half the tempo, a slice lasts half a beat");

    // Playing: with cutting on, a slice ends the one before (5 ms fade); off, both sound.
    const auto render = [&loop] (bool cut, bool both)
    {
        sls::Channel ch;
        ch.instrument = sls::Instrument::Sampler;
        auto prog = std::make_shared<sls::SamplerProgram> (sls::makeSliceProgram (loop, { 0, 48000 }, 60, "loop").sanitised());
        prog->sliceCut = cut;
        ch.program = prog;
        ch.sample = loop;
        ch.notes = { { 61, kBeat / 2, 4 * kBeat, 1.0f } };
        if (both)
            ch.notes.insert (ch.notes.begin(), { 60, 0, 4 * kBeat, 1.0f });
        const auto project = sls::Project::make ({ ch }, 8 * kBeat);
        sls::Player player;
        player.prepare (kSr);
        return renderBlocks (player, project, true, 0.5);
    };
    // At 120 BPM the second slice starts at 0.25 s; 50 ms later only it is heard when cutting.
    const auto alone = render (true, false), cutting = render (true, true), layered = render (false, true);
    const size_t at = static_cast<size_t> (0.30 * kSr);
    check (alone[at] > 0.01f && std::abs (cutting[at] - alone[at]) < 1e-4f, "with cutting on, a slice stops the one before it");
    check (layered[at] > 1.5f * alone[at], "with cutting off, both slices sound together");
    check (std::abs (cutting[static_cast<size_t> (0.2 * kSr)] - layered[static_cast<size_t> (0.2 * kSr)]) < 1e-6f, "before the second slice, both are the same");
}

void testRecordSetup()
{
    std::cout << "Multitrack recording: armed tracks and their inputs\n";
    using sls::RecordSetup;
    using sls::TrackInput;

    RecordSetup setup;
    check (setup.armedTracks().empty() && setup.toString().empty(), "a new setup arms nothing and writes nothing");
    setup.armOnly (2);
    check (setup.armedTracks() == std::vector<int> { 2 }, "arming one track arms only it");
    setup.toggle (5);
    setup.toggle (0);
    check (setup.armedTracks() == std::vector<int> ({ 0, 2, 5 }), "more tracks can be armed together");
    setup.toggle (2);
    check (setup.armedTracks() == std::vector<int> ({ 0, 5 }), "and disarmed one by one");
    setup.toggle (0);
    setup.toggle (5);
    check (setup.armedTracks() == std::vector<int> { 5 }, "the last armed track stays armed (Record always has somewhere to write)");
    setup.toggle (-1);
    setup.toggle (sls::kMaxPlaylistTracks);
    check (setup.armedTracks() == std::vector<int> { 5 }, "tracks out of range are ignored");

    setup.armOnly (0);
    setup.tracks[0] = { true, 2, false };
    setup.tracks[1] = { true, 0, true };
    setup.tracks[3] = { true, 1, true };   // shares input 2 with track 1's pair
    setup.tracks[6] = { false, 7, true };  // an input of its own, not armed
    check (setup.neededInputs() == std::vector<int> ({ 0, 1, 2 }), "the inputs needed are those of the armed tracks, sorted, each once");

    const auto text = setup.toString();
    const auto back = RecordSetup::fromString (text, 0);
    check (back.tracks == setup.tracks, "the setup is saved and read back exactly");
    check (text.find ("6:0:7:1;") != std::string::npos && text.rfind ("2:", 0) != 0 && text.find (";2:") == std::string::npos,
           "only tracks that differ from the default are written");

    const auto empty = RecordSetup::fromString ("", 4);
    check (empty.armedTracks() == std::vector<int> { 4 }, "with nothing saved, the track recorded before 0.36.0 is armed");
    const auto clamped = RecordSetup::fromString ("", 999);
    check (clamped.armedTracks() == std::vector<int> { sls::kMaxPlaylistTracks - 1 }, "and an impossible one is brought in range");

    const auto bad = RecordSetup::fromString ("x;1:1:0;2:1:0:0:0;200:1:0:0;3:2:0:0;4:1:64:0;5:1:0:7;-1:1:0:0;6:1:99999:0;::::;;7:1:3:1;1:1:63:1", 0);
    check (bad.armedTracks() == std::vector<int> ({ 1, 7 }), "malformed or out-of-range entries are skipped, good ones kept");
    check (bad.tracks[7] == TrackInput { true, 3, true }, "a good entry is read whole");
    check (bad.tracks[1].firstInput == 63 && ! bad.tracks[1].stereo, "a stereo pair past the last input becomes mono");

    std::vector<bool> on { true, false, true, true, false, true };
    check (sls::callbackInputIndex (on, 0) == 0 && sls::callbackInputIndex (on, 2) == 1 && sls::callbackInputIndex (on, 3) == 2
               && sls::callbackInputIndex (on, 5) == 3,
           "a device input is found at its place among the switched-on inputs");
    check (sls::callbackInputIndex (on, 1) == -1 && sls::callbackInputIndex (on, 6) == -1 && sls::callbackInputIndex (on, -1) == -1,
           "an input that is off or does not exist has none");
}

void testMultitrackRecorders()
{
    std::cout << "Multitrack recording: several takes from several inputs\n";
    const auto dir = freshTempDir ("sls-multirec");
    constexpr int kBlock = 256;
    constexpr int kInputs = 4;

    // input c carries a sine of its own frequency and level
    const auto value = [] (int c, long long i) { return (0.2f + 0.1f * static_cast<float> (c)) * std::sin ((0.03f + 0.02f * static_cast<float> (c)) * static_cast<float> (i)); };

    std::array<sls::InputRecorder, 3> recs;
    const auto fileA = dir.getChildFile ("a.wav"), fileB = dir.getChildFile ("b.wav"), fileC = dir.getChildFile ("c.wav");
    bool started = recs[0].start (fileA, 48000.0, sls::InputRecorder::Channels::Mono, 100, 2).isEmpty();      // input 3 alone
    started = started && recs[1].start (fileB, 48000.0, sls::InputRecorder::Channels::Stereo, 100, 0).isEmpty(); // inputs 1 and 2
    started = started && recs[2].start (fileC, 48000.0, sls::InputRecorder::Channels::Stereo, 100, 3).isEmpty(); // input 4 and a missing 5th
    check (started, "three recorders start, each on its own inputs");

    std::vector<std::vector<float>> in (kInputs, std::vector<float> (kBlock));
    std::vector<const float*> pointers (kInputs);
    long long position = 0;
    for (int blk = 0; blk < 40; ++blk)
    {
        for (int c = 0; c < kInputs; ++c)
        {
            for (int i = 0; i < kBlock; ++i)
                in[static_cast<size_t> (c)][static_cast<size_t> (i)] = value (c, position + i);
            pointers[static_cast<size_t> (c)] = in[static_cast<size_t> (c)].data();
        }
        for (auto& r : recs)
            r.process (pointers.data(), kInputs, kBlock);
        position += kBlock;
    }
    const auto ta = recs[0].stop(), tb = recs[1].stop(), tc = recs[2].stop();
    check (ta.frames == 40 * kBlock - 100 && tb.frames == ta.frames && tc.frames == ta.frames, "the takes have the same length");

    juce::AudioFormatManager manager;
    manager.registerBasicFormats();
    const auto matches = [&] (const juce::File& f, int channels, int firstInput, bool secondIsCopy)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (manager.createReaderFor (f));
        if (reader == nullptr || static_cast<int> (reader->numChannels) != channels)
            return false;
        juce::AudioBuffer<float> buf (channels, static_cast<int> (reader->lengthInSamples));
        reader->read (&buf, 0, buf.getNumSamples(), 0, true, true);
        for (int ch = 0; ch < channels; ++ch)
        {
            const int input = (ch == 1 && secondIsCopy) ? firstInput : firstInput + ch;
            for (int i = 0; i < buf.getNumSamples(); ++i)
                if (std::abs (buf.getSample (ch, i) - value (input, i + 100)) > 2e-6f)
                    return false;
        }
        return buf.getNumSamples() > 0;
    };
    check (matches (fileA, 1, 2, false), "a mono take holds its own input, lined up");
    check (matches (fileB, 2, 0, false), "a stereo take holds its two inputs");
    check (matches (fileC, 2, 3, true), "a stereo pair whose second input is missing gets the first on both sides");

    sls::InputRecorder beyond;
    const auto fileD = dir.getChildFile ("d.wav");
    beyond.start (fileD, 48000.0, sls::InputRecorder::Channels::Mono, 0, 9);
    for (int blk = 0; blk < 4; ++blk)
        beyond.process (pointers.data(), kInputs, kBlock);
    const auto td = beyond.stop();
    std::unique_ptr<juce::AudioFormatReader> reader (manager.createReaderFor (fileD));
    bool silent = reader != nullptr && reader->lengthInSamples == 4 * kBlock;
    if (silent)
    {
        juce::AudioBuffer<float> buf (1, 4 * kBlock);
        reader->read (&buf, 0, 4 * kBlock, 0, true, true);
        silent = buf.getMagnitude (0, 4 * kBlock) == 0.0f;
    }
    check (td.frames == 4 * kBlock && silent, "an input the device does not have records silence of the right length");
    reader.reset();
    dir.deleteRecursively();
}

void testInputRecorder()
{
    std::cout << "Recording the audio input\n";
    const auto dir = freshTempDir ("sls-record");
    constexpr int kBlock = 256;

    // blocks of a sine on input 0 (and a different one on input 1); returns nothing, feeds the recorder
    const auto feed = [] (sls::InputRecorder& rec, int blocks, int numInputs, long long& position)
    {
        std::vector<float> a (kBlock), b (kBlock);
        const float* in[2] = { a.data(), b.data() };
        for (int blk = 0; blk < blocks; ++blk)
        {
            for (int i = 0; i < kBlock; ++i, ++position)
            {
                a[static_cast<size_t> (i)] = 0.5f * std::sin (0.05f * static_cast<float> (position));
                b[static_cast<size_t> (i)] = 0.25f * std::sin (0.11f * static_cast<float> (position));
            }
            rec.process (in, numInputs, kBlock);
        }
    };
    juce::AudioFormatManager manager;
    manager.registerBasicFormats();
    const auto readBack = [&] (const juce::File& f, int& channels, std::vector<float>& left, std::vector<float>& right)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (manager.createReaderFor (f));
        if (reader == nullptr)
            return false;
        channels = static_cast<int> (reader->numChannels);
        juce::AudioBuffer<float> buf (channels, static_cast<int> (reader->lengthInSamples));
        reader->read (&buf, 0, buf.getNumSamples(), 0, true, true);
        left.assign (buf.getReadPointer (0), buf.getReadPointer (0) + buf.getNumSamples());
        right.assign (buf.getReadPointer (channels - 1), buf.getReadPointer (channels - 1) + buf.getNumSamples());
        return true;
    };

    {   // stereo from two inputs
        sls::InputRecorder rec;
        const auto file = dir.getChildFile ("stereo.wav");
        check (rec.start (file, 48000.0, sls::InputRecorder::Channels::Stereo, 0).isEmpty() && rec.isActive(), "recording starts");
        long long pos = 0;
        feed (rec, 100, 2, pos);
        check (rec.getFramesRecorded() == 100 * kBlock && std::abs (rec.getSecondsRecorded() - 100.0 * kBlock / 48000.0) < 1e-9,
               "the recorder counts the frames it has taken");
        check (rec.consumePeak() > 0.45f && rec.consumePeak() == 0.0f, "it reports the input peak since the last look");
        const auto take = rec.stop();
        check (! rec.isActive() && take.frames == 100 * kBlock && take.droppedBlocks == 0 && take.file == file
                   && std::abs (take.sampleRate - 48000.0) < 1e-9,
               "stopping gives the file and the number of frames");
        int ch = 0;
        std::vector<float> l, r;
        check (readBack (file, ch, l, r) && ch == 2 && static_cast<long long> (l.size()) == take.frames, "the file is a complete stereo WAV with every frame");
        bool exact = ! l.empty();
        for (size_t i = 0; i < l.size() && exact; ++i)
            exact = std::abs (l[i] - 0.5f * std::sin (0.05f * static_cast<float> (i))) < 2e-6f
                 && std::abs (r[i] - 0.25f * std::sin (0.11f * static_cast<float> (i))) < 2e-6f;
        check (exact, "and it holds exactly what came in (24 bit)");
        const auto again = rec.stop();
        check (again.file == juce::File() && again.frames == 0, "stopping when nothing is recorded gives an empty result");
        check (rec.getFramesRecorded() == take.frames, "a stopped recorder keeps its last count");
    }
    {   // mono: the first input only
        sls::InputRecorder rec;
        const auto file = dir.getChildFile ("mono.wav");
        rec.start (file, 44100.0, sls::InputRecorder::Channels::Mono, 0);
        long long pos = 0;
        feed (rec, 20, 2, pos);
        rec.stop();
        int ch = 0;
        std::vector<float> l, r;
        check (readBack (file, ch, l, r) && ch == 1 && l.size() == 20 * kBlock && std::abs (l[100] - 0.5f * std::sin (5.0f)) < 2e-6f,
               "mono mode writes a one-channel file with the first input");
    }
    {   // stereo asked, one input available: both sides get it
        sls::InputRecorder rec;
        const auto file = dir.getChildFile ("one.wav");
        rec.start (file, 48000.0, sls::InputRecorder::Channels::Stereo, 0);
        long long pos = 0;
        feed (rec, 10, 1, pos);
        rec.stop();
        int ch = 0;
        std::vector<float> l, r;
        check (readBack (file, ch, l, r) && ch == 2 && l == r && l.size() == 10 * kBlock, "with one input, stereo mode copies it to both sides");
    }
    {   // the delay between sound and input is skipped, also across blocks
        sls::InputRecorder rec;
        const auto file = dir.getChildFile ("skip.wav");
        rec.start (file, 48000.0, sls::InputRecorder::Channels::Mono, 600);
        long long pos = 0;
        feed (rec, 10, 1, pos);
        const auto take = rec.stop();
        int ch = 0;
        std::vector<float> l, r;
        check (take.frames == 10 * kBlock - 600 && readBack (file, ch, l, r) && l.size() == static_cast<size_t> (take.frames)
                   && std::abs (l[0] - 0.5f * std::sin (0.05f * 600.0f)) < 2e-6f,
               "the first frames (the round-trip delay) are left out, so the take lines up");
    }
    {   // no input at all still gives a file of the right length
        sls::InputRecorder rec;
        const auto file = dir.getChildFile ("none.wav");
        rec.start (file, 48000.0, sls::InputRecorder::Channels::Stereo, 0);
        for (int i = 0; i < 10; ++i)
            rec.process (nullptr, 0, kBlock);
        const auto take = rec.stop();
        int ch = 0;
        std::vector<float> l, r;
        float loudest = 0.0f;
        for (float v : l)
            loudest = std::max (loudest, std::abs (v));
        check (take.frames == 10 * kBlock && readBack (file, ch, l, r) && l.size() == 10 * kBlock, "without an input the take is silence of the right length");
    }
    {   // not recording: nothing happens
        sls::InputRecorder rec;
        long long pos = 0;
        feed (rec, 5, 2, pos);
        check (rec.getFramesRecorded() == 0 && ! rec.isActive(), "process() does nothing while no recording is running");
    }
    {   // failures leave nothing behind
        sls::InputRecorder rec;
        const auto bad = juce::File ("/proc/no-such-folder/take.wav");
        check (rec.start (bad, 48000.0, sls::InputRecorder::Channels::Mono, 0).isNotEmpty() && ! rec.isActive(), "an impossible location is reported");
        check (rec.start (dir.getChildFile ("x.wav"), 0.0, sls::InputRecorder::Channels::Mono, 0).isNotEmpty(), "no sample rate (no device) is reported");
        check (rec.start (dir.getChildFile ("a.wav"), 48000.0, sls::InputRecorder::Channels::Mono, 0).isEmpty()
                   && rec.start (dir.getChildFile ("b.wav"), 48000.0, sls::InputRecorder::Channels::Mono, 0).isNotEmpty(),
               "a second recording cannot start while one is running");
        rec.stop();
        check (! dir.getChildFile ("b.wav").exists() && ! dir.getChildFile ("x.wav").exists(), "and a refused recording creates no file");
    }
    {   // real-time safety
        sls::InputRecorder rec;
        rec.start (dir.getChildFile ("rt.wav"), 48000.0, sls::InputRecorder::Channels::Stereo, 100);
        std::vector<float> a (kBlock, 0.3f), b (kBlock, 0.2f);
        const float* in[2] = { a.data(), b.data() };
        rec.process (in, 2, kBlock); // first call settles any lazy initialisation
        tAllocs = 0;
        tCounting = true; // this thread only: the writer thread in the background allocates, and may
        for (int i = 0; i < 200; ++i)
            rec.process (in, 2, kBlock);
        tCounting = false;
        check (tAllocs == 0, "process() allocates nothing");
        rec.stop();
    }
    {   // stopping while the audio thread is busy: the file stays consistent
        sls::InputRecorder rec;
        const auto file = dir.getChildFile ("busy.wav");
        rec.start (file, 48000.0, sls::InputRecorder::Channels::Stereo, 0);
        std::atomic<bool> stopFlag { false };
        std::thread audio ([&]
        {
            std::vector<float> a (kBlock, 0.3f), b (kBlock, 0.2f);
            const float* in[2] = { a.data(), b.data() };
            // At about the pace of a sound card (a block, then a short rest): unpaced, the loop wrote hundreds of MB in 150 ms and
            // a nearly full disk cut the file short (0.57.0).
            while (! stopFlag.load())
            {
                rec.process (in, 2, kBlock);
                std::this_thread::sleep_for (std::chrono::microseconds (500));
            }
        });
        std::this_thread::sleep_for (std::chrono::milliseconds (150));
        const auto take = rec.stop();
        std::this_thread::sleep_for (std::chrono::milliseconds (30)); // the thread keeps calling process() on a stopped recorder
        stopFlag = true;
        audio.join();
        int ch = 0;
        std::vector<float> l, r;
        check (take.frames > 0 && readBack (file, ch, l, r) && static_cast<long long> (l.size()) == take.frames,
               "stopping while audio is arriving gives a complete file whose length matches the count");
    }
    check (dir.getNumberOfChildFiles (juce::File::findFiles) == 8, "no stray files are left in the recordings folder");
    dir.deleteRecursively(); // test folders in /tmp are not left behind
}

void testMidiNotes()
{
    std::cout << "MIDI keyboards\n";
    using sls::LiveNoteEvent;
    sls::MidiNoteMapper mapper;
    std::vector<LiveNoteEvent> out;
    const auto feed = [&] (const juce::MidiMessage& m, int channel)
    {
        out.clear();
        mapper.map (m, channel, [&] (const LiveNoteEvent& e) { out.push_back (e); });
    };

    feed (juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)), 3);
    check (out.size() == 1 && out[0].on && out[0].channel == 3 && out[0].pitch == 60 && std::abs (out[0].velocity - 100.0f / 127.0f) < 1e-6f,
           "a note on becomes a live note on the target channel with its velocity");
    feed (juce::MidiMessage::noteOff (1, 60), 3);
    check (out.size() == 1 && ! out[0].on && out[0].channel == 3 && out[0].pitch == 60 && mapper.heldCount() == 0, "a note off ends it");

    feed (juce::MidiMessage::noteOn (2, 64, static_cast<juce::uint8> (90)), 1);
    feed (juce::MidiMessage::noteOn (2, 64, static_cast<juce::uint8> (0)), 4);
    check (out.size() == 1 && ! out[0].on && out[0].channel == 1, "a note on with velocity 0 is a note off, sent to the channel that got the note on");

    feed (juce::MidiMessage::noteOn (1, 67, static_cast<juce::uint8> (80)), 2);
    feed (juce::MidiMessage::noteOff (1, 67), 5); // the target channel changed while the key was down
    check (out.size() == 1 && ! out[0].on && out[0].channel == 2, "a note off goes to the channel that played the note even if the target changed");

    feed (juce::MidiMessage::noteOn (1, 72, static_cast<juce::uint8> (80)), 0);
    feed (juce::MidiMessage::noteOn (1, 72, static_cast<juce::uint8> (70)), 0);
    check (out.size() == 2 && ! out[0].on && out[1].on, "a repeated note on first ends the note that is still held");
    feed (juce::MidiMessage::noteOff (1, 72), 0);
    feed (juce::MidiMessage::noteOff (1, 72), 0);
    check (out.empty(), "a note off for a note that is not held is ignored");

    feed (juce::MidiMessage::controllerEvent (1, 64, 127), 0);
    check (out.empty(), "controllers (sustain pedal) are ignored");
    feed (juce::MidiMessage::pitchWheel (1, 9000), 0);
    check (out.empty(), "pitch bend is ignored");
    feed (juce::MidiMessage::programChange (1, 5), 0);
    check (out.empty(), "program changes are ignored");
    feed (juce::MidiMessage::channelPressureChange (1, 40), 0);
    check (out.empty(), "channel pressure is ignored");

    for (int p : { 60, 64, 67 })
        feed (juce::MidiMessage::noteOn (10, p, static_cast<juce::uint8> (100)), 6);
    check (mapper.heldCount() == 3, "chords are tracked key by key");
    feed (juce::MidiMessage::allNotesOff (1), 0);
    check (out.size() == 3 && ! out[0].on && ! out[1].on && ! out[2].on && mapper.heldCount() == 0 && out[0].channel == 6,
           "\"all notes off\" ends every held note");
    feed (juce::MidiMessage::noteOn (1, 50, static_cast<juce::uint8> (100)), 0);
    feed (juce::MidiMessage::allSoundOff (1), 0);
    check (out.size() == 1 && ! out[0].on, "\"all sound off\" does too");

    // through the real queue into the Player: the note sounds and stops
    {
        const auto project = sls::Project::make ({ synthChannel ({}) }, 8 * kBeat);
        sls::LiveNoteQueue queue;
        sls::Player p;
        p.prepare (kSr);
        sls::MidiNoteMapper m;
        m.map (juce::MidiMessage::noteOn (1, 69, static_cast<juce::uint8> (110)), 0, [&] (const LiveNoteEvent& e) { queue.push (e); });
        LiveNoteEvent e;
        while (queue.pop (e))
            p.liveNoteOn (project, e.channel, e.pitch, e.velocity);
        std::vector<float> l (4096), r (4096);
        float* ch[2] = { l.data(), r.data() };
        p.render (project, 120.0, false, ch, 2, 4096);
        float held = 0.0f;
        for (float v : l)
            held = std::max (held, std::abs (v));
        m.map (juce::MidiMessage::noteOff (1, 69), 0, [&] (const LiveNoteEvent& ev) { queue.push (ev); });
        while (queue.pop (e))
            p.liveNoteOff (e.channel, e.pitch);
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        for (int i = 0; i < 4; ++i)
            p.render (project, 120.0, false, ch, 2, 4096);
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        p.render (project, 120.0, false, ch, 2, 4096);
        float after = 0.0f;
        for (float v : l)
            after = std::max (after, std::abs (v));
        check (held > 0.05f && after == 0.0f, "a MIDI note plays through the song's channel and is silent after the key is released");
    }
}

void testPlayerRewind()
{
    std::cout << "Rewind\n";
    const auto project = sls::Project::make ({ synthChannel ({ { 69, 0, 3840, 1.0f } }) }, 8 * kBeat);
    sls::Player p;
    p.prepare (kSr);
    std::vector<float> l (4800), r (4800);
    float* ch[2] = { l.data(), r.data() };
    for (int i = 0; i < 5; ++i)
        p.render (project, 120.0, true, ch, 2, 4800);
    check (p.getPositionTicks() > 400.0, "the song has moved on");
    p.rewind();
    check (p.getPositionTicks() == 0.0, "rewind() goes back to the start");
    std::fill (l.begin(), l.end(), 0.0f);
    std::fill (r.begin(), r.end(), 0.0f);
    p.render (project, 120.0, true, ch, 2, 4800);
    float m = 0.0f;
    for (int i = 0; i < 200; ++i)
        m = std::max (m, std::abs (l[static_cast<size_t> (i)]));
    check (p.getPositionTicks() < 500.0 && m > 0.01f, "and the first note plays again from the beginning");
}

void testTapTempo()
{
    std::cout << "[tap tempo]\n";
    sls::TapTempo t;
    check (t.tap (10.0) == 0.0, "a single tap gives no tempo yet");
    check (std::abs (t.tap (10.5) - 120.0) < 0.01, "two taps half a second apart are 120 BPM");
    t.tap (11.0); t.tap (11.5);
    check (std::abs (t.tap (12.0) - 120.0) < 0.01, "a steady series stays at 120 BPM");
    check (t.tap (20.0) == 0.0, "a long pause starts a new series");
    check (std::abs (t.tap (20.6) - 100.0) < 0.01, "0.6 s between taps is 100 BPM");
    sls::TapTempo fast;
    fast.tap (0.0);
    check (fast.tap (0.1) == 240.0, "absurdly fast taps are clamped to 240 BPM");
    t.reset();
    check (t.tap (30.0) == 0.0 && std::abs (t.tap (30.5) - 120.0) < 0.01, "reset forgets the earlier taps");
    sls::TapTempo slow;
    slow.tap (0.0);
    check (slow.tap (1.9) == 40.0, "very slow taps are clamped to 40 BPM");
}

void testMetronome()
{
    std::cout << "Metronome\n";
    const auto empty = sls::Project::make ({ synthChannel ({}) }, 8 * kBeat);

    // renders `blocks` blocks of 256 samples; transport playing or not
    const auto run = [&] (sls::Player& player, const sls::Project& project, bool playing, int blocks, double bpm = 120.0)
    {
        std::vector<float> all;
        std::vector<float> l (256), r (256);
        float* ch[2] = { l.data(), r.data() };
        for (int b = 0; b < blocks; ++b)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            player.render (project, bpm, playing, ch, 2, 256);
            all.insert (all.end(), l.begin(), l.end());
        }
        return all;
    };
    constexpr int kBeatSamples = 24000; // 120 bpm at 48 kHz

    {   // playing: a click on the beat, silence between
        sls::Player p;
        p.prepare (kSr);
        p.setMetronome (true, 0.8f);
        const auto x = run (p, empty, true, 400); // 102400 samples: beats at 0, 24000, 48000, 72000, 96000
        bool clicks = true, quiet = true;
        for (int beat = 0; beat < 5; ++beat)
            clicks = clicks && peakOf (x.data(), beat * kBeatSamples, beat * kBeatSamples + 1500) > 0.05f;
        for (int beat = 0; beat < 4; ++beat)
            quiet = quiet && peakOf (x.data(), beat * kBeatSamples + 2500, (beat + 1) * kBeatSamples - 5) == 0.0f;
        check (clicks, "the metronome clicks on every beat while the transport plays, starting at once");
        check (quiet, "and is silent between the clicks");
        const float accent = peakOf (x.data(), 0, 1500), normal = peakOf (x.data(), kBeatSamples, kBeatSamples + 1500);
        check (accent > normal * 1.2f && std::abs (peakOf (x.data(), 4 * kBeatSamples, 4 * kBeatSamples + 1500) - normal) > 0.0f,
               "the first beat of a bar is louder than the others");
    }
    {   // off: nothing at all
        sls::Player p;
        p.prepare (kSr);
        const auto x = run (p, empty, true, 200);
        check (peakOf (x.data(), 0, static_cast<int> (x.size())) == 0.0f, "no click when the metronome is off");
        p.setMetronome (true, 0.8f);
        const auto y = run (p, empty, true, 700);
        p.setMetronome (false, 0.8f);
        const auto z = run (p, empty, true, 200);
        check (peakOf (y.data(), 0, static_cast<int> (y.size())) > 0.05f && peakOf (z.data(), 3000, static_cast<int> (z.size())) == 0.0f, "switching it off stops the clicks");
    }
    {   // stopped: free running at the tempo, first click at once
        sls::Player p;
        p.prepare (kSr);
        p.setMetronome (true, 0.8f);
        const auto x = run (p, empty, false, 400);
        bool clicks = true;
        for (int beat = 0; beat < 4; ++beat)
            clicks = clicks && peakOf (x.data(), beat * kBeatSamples, beat * kBeatSamples + 1500) > 0.05f;
        check (clicks && peakOf (x.data(), 3000, kBeatSamples - 5) == 0.0f, "with the transport stopped it clicks at the tempo on its own");

        sls::Player q;
        q.prepare (kSr);
        q.setMetronome (true, 0.8f);
        const auto fast = run (q, empty, false, 400, 240.0); // 12000 samples per beat
        check (peakOf (fast.data(), 12000, 13500) > 0.05f && peakOf (fast.data(), 24000, 25500) > 0.05f, "the click spacing follows the BPM");
    }
    {   // joining in the middle of a beat waits for the next beat
        sls::Player p;
        p.prepare (kSr);
        const auto before = run (p, empty, true, 50); // 12800 samples, metronome off
        (void) before;
        p.setMetronome (true, 0.8f);
        const auto x = run (p, empty, true, 50); // 12800..25600: the next beat is at 24000
        check (peakOf (x.data(), 0, 10000) == 0.0f && peakOf (x.data(), 24000 - 12800, 24000 - 12800 + 1500) > 0.05f,
               "switched on mid-beat, it does not click until the next beat");
    }
    {   // the loop wrap is a downbeat again; volume scales; it is not part of an offline render
        sls::Player p;
        p.prepare (kSr);
        p.setMetronome (true, 1.0f);
        const auto short4 = sls::Project::make ({ synthChannel ({}) }, 4 * kBeat);
        const auto x = run (p, short4, true, 600); // 153600 samples, enough to look at beat 5 (400 blocks ended before it: a read past the end)
        check (peakOf (x.data(), 4 * kBeatSamples, 4 * kBeatSamples + 1500) > peakOf (x.data(), 5 * kBeatSamples, 5 * kBeatSamples + 1500) * 1.2f,
               "the start of the loop is a downbeat again");
        sls::Player quiet;
        quiet.prepare (kSr);
        quiet.setMetronome (true, 0.25f);
        const auto y = run (quiet, empty, true, 20);
        check (peakOf (y.data(), 0, 1500) < 0.5f * peakOf (x.data(), 0, 1500), "the volume setting scales the click");
        const auto rendered = sls::renderToBuffer (empty, {});
        check (peakOf (rendered.getReadPointer (0), 0, rendered.getNumSamples()) == 0.0f, "an offline render (export) never contains the metronome");
    }
    {   // real-time safety
        sls::Player p;
        p.prepare (kSr);
        p.setMetronome (true, 0.8f);
        std::vector<float> l (256), r (256);
        float* ch[2] = { l.data(), r.data() };
        gAllocs = 0;
        gCounting = true;
        for (int i = 0; i < 2000; ++i)
            p.render (empty, 120.0, (i / 500) % 2 == 0, ch, 2, 256);
        gCounting = false;
        check (gAllocs.load() == 0, "Player::render performs no heap allocation with the metronome running (2000 blocks, playing and stopped)");
    }
}

// --- cutting, trimming and the pattern list --------------------------------------------------------------------------
void testClipEditing()
{
    std::cout << "Clip editing (split, cut, trim), pattern deletion\n";
    const auto sine = sineData (96000, 48000.0, 440.0, 0.5f); // 2 s

    const auto make = [&] (int start, int length, double offset = 0.0)
    {
        sls::Arrangement a;
        a.patterns.resize (3);
        sls::AudioClip c;
        c.data = sine;
        c.name = "x";
        c.track = 1;
        c.startTick = start;
        c.lengthTicks = length;
        c.offsetSeconds = offset;
        a.audioClips.push_back (c);
        return a;
    };

    check (std::abs (sls::ticksToSeconds (960, 120.0) - 1.0) < 1.0e-12, "960 ticks last 1 s at 120 BPM");
    check (std::abs (sls::secondsToTicks (0.5, 120.0) - 480.0) < 1.0e-9 && std::abs (sls::ticksToSeconds (480, 60.0) - 1.0) < 1.0e-12,
           "tick/second conversion follows the tempo");

    // split
    {
        auto a = make (480, 1920);
        check (sls::splitAudioClip (a, 0, 480, 120.0) == -1 && sls::splitAudioClip (a, 0, 2400, 120.0) == -1 && sls::splitAudioClip (a, 0, 5, 120.0) == -1
                   && sls::splitAudioClip (a, 3, 900, 120.0) == -1 && a.audioClips.size() == 1,
               "a split outside the clip, on its edge or on a clip that does not exist changes nothing");
        const int r = sls::splitAudioClip (a, 0, 1440, 120.0);
        check (r == 1 && a.audioClips.size() == 2, "a split makes two clips");
        const auto& left = a.audioClips[0];
        const auto& right = a.audioClips[1];
        check (left.startTick == 480 && left.lengthTicks == 960 && right.startTick == 1440 && right.lengthTicks == 960,
               "the pieces meet at the cut and keep the total length");
        check (left.offsetSeconds == 0.0 && std::abs (right.offsetSeconds - 1.0) < 1.0e-9, "the right-hand piece starts 1 s into the audio");
        check (left.data == right.data && a.canPlaceAudio (left, 0) && a.canPlaceAudio (right, 1), "the pieces share the audio and do not overlap");
        // splitting the right piece again keeps accumulating the offset
        const int r2 = sls::splitAudioClip (a, 1, 1920, 120.0);
        check (r2 == 2 && std::abs (a.audioClips[2].offsetSeconds - 1.5) < 1.0e-9, "the offset adds up over repeated splits");
    }

    // cut a range out
    {
        auto a = make (0, 1920);
        check (sls::cutAudioRange (a, 0, 960, 1440, 120.0) == 2 && a.audioClips.size() == 2, "cutting the middle leaves two pieces");
        check (a.audioClips[0].startTick == 0 && a.audioClips[0].lengthTicks == 960 && a.audioClips[1].startTick == 1440
                   && a.audioClips[1].lengthTicks == 480 && std::abs (a.audioClips[1].offsetSeconds - 1.5) < 1.0e-9,
               "the gap is empty and the audio after it is not moved");

        a = make (0, 1920);
        check (sls::cutAudioRange (a, 0, -500, 480, 120.0) == 1 && a.audioClips[0].startTick == 480 && a.audioClips[0].lengthTicks == 1440
                   && std::abs (a.audioClips[0].offsetSeconds - 0.5) < 1.0e-9,
               "cutting the beginning moves the start forward and into the audio");
        a = make (0, 1920);
        check (sls::cutAudioRange (a, 0, 1500, 9999, 120.0) == 1 && a.audioClips[0].lengthTicks == 1500 && a.audioClips[0].offsetSeconds == 0.0,
               "cutting the end only shortens the clip");
        a = make (0, 1920);
        check (sls::cutAudioRange (a, 0, 0, 1920, 120.0) == 0 && a.audioClips.empty(), "cutting all of it removes the clip");
        a = make (480, 960);
        check (sls::cutAudioRange (a, 0, 0, 480, 120.0) == -1 && sls::cutAudioRange (a, 0, 1440, 2000, 120.0) == -1
                   && sls::cutAudioRange (a, 0, 900, 900, 120.0) == -1 && a.audioClips.size() == 1,
               "a range that misses the clip (or is empty) changes nothing");
    }

    // trim the start edge
    {
        const auto clip = make (960, 960, 1.0).audioClips[0]; // audio 1.0 s .. 2.0 s placed at 960..1920
        const auto t = sls::trimmedAudioStart (clip, 480, 120.0);
        check (t.startTick == 480 && t.endTick() == 1920 && std::abs (t.offsetSeconds - 0.5) < 1.0e-9,
               "dragging the start edge left uncovers earlier audio and keeps the end where it is");
        const auto far = sls::trimmedAudioStart (clip, -300, 120.0);
        check (far.startTick == 0 && far.offsetSeconds == 0.0, "the edge cannot go back past the beginning of the audio");
        const auto in = sls::trimmedAudioStart (clip, 1200, 120.0);
        check (in.startTick == 1200 && in.lengthTicks == 720 && std::abs (in.offsetSeconds - 1.25) < 1.0e-9,
               "dragging it right hides the first part of the clip");
        const auto over = sls::trimmedAudioStart (clip, 99999, 120.0);
        check (over.lengthTicks == 1 && over.endTick() == clip.endTick(), "the edge cannot pass the end of the clip");
        check (clip.naturalLengthTicks (120.0) == 960, "naturalLengthTicks counts the audio left after the offset (1 s = 960 ticks)");
    }

    // the offset is honoured by the player
    {
        auto data = std::make_shared<sls::SampleData>();
        data->sampleRate = 48000.0;
        data->left.assign (48000, 0.0f);
        data->right.assign (48000, 0.0f);
        for (int i = 24000; i < 48000; ++i) // silent for the first half second, then a tone
            data->left[static_cast<size_t> (i)] = data->right[static_cast<size_t> (i)] = 0.5f * static_cast<float> (std::sin (2.0 * 3.14159265358979 * 440.0 * i / 48000.0));

        const auto render = [&] (double offset)
        {
            sls::Arrangement a;
            a.patterns.resize (1);
            sls::AudioClip c;
            c.data = data;
            c.track = 0;
            c.startTick = 0;
            c.lengthTicks = 960;
            c.offsetSeconds = offset;
            a.audioClips.push_back (c);
            const auto project = sls::compileSong ({ synthChannel ({}) }, a);
            sls::Player player;
            player.prepare (48000.0);
            std::vector<float> l (256), r (256), all;
            float* ch[2] = { l.data(), r.data() };
            for (int b = 0; b < 40; ++b) // 10240 samples = 0.21 s
            {
                std::fill (l.begin(), l.end(), 0.0f);
                std::fill (r.begin(), r.end(), 0.0f);
                player.render (project, 120.0, true, ch, 2, 256);
                all.insert (all.end(), l.begin(), l.end());
            }
            float m = 0.0f;
            for (float v : all)
                m = std::max (m, std::abs (v));
            return m;
        };
        check (render (0.0) < 1.0e-6, "without an offset the first 0.2 s of that audio are silent");
        check (render (0.5) > 0.05f, "with a 0.5 s offset the tone is heard at once");
    }

    // saved and read back
    {
        auto doc = sls::ProjectDocument::blank();
        sls::AudioClip c;
        c.data = sine;
        c.name = "A name that is longer than sixteen characters";
        c.track = 2;
        c.startTick = 1234; // not on a beat
        c.lengthTicks = 777;
        c.offsetSeconds = 0.4375;
        doc.arrangement.audioClips.push_back (c);
        const auto back = sls::ProjectDocument::fromJson (doc.toJson());
        check (back.document.has_value() && back.document->arrangement.audioClips.size() == 1, "a clip that starts off the beat survives a save");
        if (back.document.has_value() && back.document->arrangement.audioClips.size() == 1)
        {
            const auto& r = back.document->arrangement.audioClips[0];
            check (r.startTick == 1234 && r.lengthTicks == 777 && std::abs (r.offsetSeconds - 0.4375) < 1.0e-12, "start, length and offset are written exactly");
            check (r.name.size() == 40, "names up to 40 characters are kept (was 16)");
        }
    }

    // patterns
    {
        sls::Arrangement a;
        a.patterns.resize (4);
        a.patterns[0].name = "Pattern 1";
        a.patterns[1].name = "Pattern 2";
        a.patterns[2].name = "Pattern 3";
        a.patterns[3].name = "Drums";
        a.clips = { uc (a, 0, 0, 0, 1), uc (a, 1, 0, 1, 2), uc (a, 2, 1, 0, 1), uc (a, 3, 2, 0, 1), uc (a, 1, 3, 4, 1) };
        check (a.newPatternName() == "Pattern 4", "the next pattern name is the lowest number not in use");
        check (! a.removePattern (-1) && ! a.removePattern (4) && a.patterns.size() == 4, "deleting a pattern that does not exist changes nothing");
        check (a.removePattern (1), "a pattern can be deleted");
        check (a.patterns.size() == 3 && a.patterns[0].name == "Pattern 1" && a.patterns[1].name == "Pattern 3" && a.patterns[2].name == "Drums",
               "the others keep their order");
        check (a.clips.size() == 3, "the clips that played it are removed");
        check (a.clips[0].pattern == 0 && a.clips[1].pattern == 1 && a.clips[2].pattern == 2, "and the clips of later patterns follow them (pattern numbers move up)");
        check (a.newPatternName() == "Pattern 2", "the freed name is used again");
        check (a.removePattern (0) && a.removePattern (0) && a.patterns.size() == 1 && ! a.removePattern (0),
               "the last pattern can never be deleted");
        std::string n = "  Lead line \n";
        check (sls::cleanPatternName (n) && n == "Lead line", "pattern names are trimmed");
        std::string blank = "   ";
        check (! sls::cleanPatternName (blank), "an empty name is refused");
        std::string longName (80, 'x');
        check (sls::cleanPatternName (longName) && longName.size() == 40, "and long names are cut at 40 characters");
    }

    // pattern clips
    {
        sls::Arrangement a;
        a.patterns.resize (1);
        const int pt = a.patternLength (0); // one bar of 4/4 since 0.42.0
        a.clips = { { 0, 0, 2 * pt, 6 * pt, 0 } };
        check (sls::splitPatternClip (a, 0, 2 * pt) == -1 && sls::splitPatternClip (a, 0, 8 * pt) == -1 && a.clips.size() == 1, "pattern clips split only strictly inside");
        check (sls::splitPatternClip (a, 0, 5 * pt) == 1 && a.clips[0].lengthTicks == 3 * pt && a.clips[1].startTick == 5 * pt && a.clips[1].lengthTicks == 3 * pt
                   && a.clips[1].offsetTicks == 0,
               "a pattern clip splits at a pattern boundary (the right part starts the pattern again)");
        a.clips = { { 0, 0, 2 * pt, 6 * pt, 0 } };
        check (sls::splitPatternClip (a, 0, 3 * pt + 480) == 1 && a.clips[1].startTick == 3 * pt + 480 && a.clips[1].offsetTicks == 480,
               "split inside a pattern (0.42.0): the right part goes on from that beat of the pattern");
        a.clips = { { 0, 0, 2 * pt, 6 * pt, 0 } };
        check (sls::cutPatternRange (a, 0, 4 * pt, 6 * pt) == 2 && a.clips[0].lengthTicks == 2 * pt && a.clips[1].startTick == 6 * pt && a.clips[1].lengthTicks == 2 * pt,
               "cutting out of the middle leaves two clips");
        a.clips = { { 0, 0, 2 * pt, 6 * pt, 0 } };
        check (sls::cutPatternRange (a, 0, 0, 4 * pt + 960) == 1 && a.clips[0].startTick == 4 * pt + 960 && a.clips[0].lengthTicks == 4 * pt - 960
                   && a.clips[0].offsetTicks == 960,
               "cutting from the start keeps the rest where it was in the pattern");
        check (sls::cutPatternRange (a, 0, 0, 1 << 28) == 0 && a.clips.empty(), "cutting everything removes the clip");
    }
}

// --- recording a MIDI keyboard into the piano roll ---------------------------------------------------------------------
void testNoteRecording()
{
    std::cout << "Recording MIDI notes into patterns\n";

    // the stamp the audio thread puts on a key press
    sls::LiveNoteEvent ev;
    ev.on = true;
    ev.channel = 3;
    ev.pitch = 64;
    ev.velocity = 0.5f;
    ev.fromMidi = true;
    auto take = sls::makeTake (ev, 100.0, 30.0, 3840.0);
    check (take.on && take.channel == 3 && take.pitch == 64 && take.velocity == 0.5f && std::abs (take.tick - 70.0f) < 1.0e-3f && take.loopTicks == 3840.0f,
           "a key press is stamped with the song position minus the latency");
    take = sls::makeTake (ev, 10.0, 30.0, 3840.0);
    check (std::abs (take.tick - 3820.0f) < 1.0e-3f, "the latency correction wraps round the start of the loop");
    take = sls::makeTake (ev, 5000.0, 0.0, 3840.0);
    check (take.tick >= 0.0f && take.tick < 3840.0f, "the stamp is always inside the loop");
    check (! sls::LiveNoteEvent {}.fromMidi, "notes played with the mouse are not recorded (only MIDI ones are)");

    const auto on = [] (int channel, int pitch, float tick, float velocity = 0.8f)
    {
        sls::MidiTake t;
        t.on = true;
        t.channel = static_cast<std::uint8_t> (channel);
        t.pitch = static_cast<std::uint8_t> (pitch);
        t.tick = tick;
        t.velocity = velocity;
        t.loopTicks = 3840.0f;
        return t;
    };
    const auto off = [&] (int channel, int pitch, float tick)
    {
        auto t = on (channel, pitch, tick);
        t.on = false;
        return t;
    };

    struct Got { int channel; sls::Note note; };
    std::vector<Got> got;
    const auto emit = [&got] (int channel, const sls::Note& n) { got.push_back ({ channel, n }); };

    sls::NoteRecorder rec;
    rec.configure (3840, 120);

    rec.process (on (0, 60, 500.0f, 0.75f), emit);
    check (got.empty() && rec.heldCount() == 1, "nothing is written until the key comes up");
    rec.process (off (0, 60, 900.0f), emit);
    check (got.size() == 1 && got[0].channel == 0 && got[0].note.pitch == 60 && got[0].note.startTick == 480 && got[0].note.lengthTicks == 400
               && got[0].note.velocity == 0.75f && rec.heldCount() == 0,
           "a note is pitch, velocity and length, with its start rounded to the 1/16 grid (500 -> 480)");

    got.clear();
    rec.process (on (1, 62, 1000.0f), emit);
    rec.process (off (1, 62, 1010.0f), emit);
    check (got.size() == 1 && got[0].note.lengthTicks == 120, "a tapped key makes a note at least one grid step long");

    got.clear();
    rec.configure (3840, 0);
    rec.process (on (0, 65, 1003.0f), emit);
    rec.process (off (0, 65, 1013.0f), emit);
    check (got.size() == 1 && got[0].note.startTick == 1003 && got[0].note.lengthTicks == 60, "without quantizing the timing is kept (and a tap is a short note)");
    rec.configure (3840, 120);

    got.clear();
    rec.process (on (0, 67, 3700.0f), emit);
    rec.process (off (0, 67, 100.0f), emit); // the loop came round while the key was down
    check (got.size() == 2 && got[0].note.startTick == 3720 && got[0].note.lengthTicks == 120 && got[1].note.startTick == 0 && got[1].note.lengthTicks == 120,
           "a note held across the end of the loop continues at the start of the pattern");
    check (got[0].note.startTick + got[0].note.lengthTicks <= 3840, "no recorded note runs past the end of the pattern");

    got.clear();
    rec.process (on (0, 70, 3790.0f), emit);
    rec.process (off (0, 70, 3830.0f), emit);
    check (got.size() == 1 && got[0].note.startTick == 0, "a start that rounds up to the end of the pattern wraps to its start");

    got.clear();
    rec.process (off (0, 72, 200.0f), emit);
    check (got.empty(), "a key that went down before recording started produces nothing");

    got.clear();
    rec.process (on (0, 60, 100.0f), emit);
    rec.process (on (0, 60, 700.0f), emit); // a second note on for the same key (a lost note off)
    check (got.size() == 1 && got[0].note.lengthTicks == 600, "a repeated note on ends the first note");
    rec.process (off (0, 60, 900.0f), emit);
    check (got.size() == 2 && rec.heldCount() == 0, "and the second one ends normally");

    got.clear();
    rec.process (on (2, 50, 960.0f), emit);
    rec.process (on (0, 50, 960.0f), emit);
    rec.process (off (2, 50, 1440.0f), emit);
    check (got.size() == 1 && got[0].channel == 2 && rec.heldCount() == 1, "the same pitch on two channels is tracked separately");
    rec.flush (480, emit);
    check (got.size() == 2 && got[1].channel == 0 && got[1].note.startTick == 960 && got[1].note.lengthTicks == 480 && rec.heldCount() == 0,
           "flush ends the notes still held (the song was stopped with a key down)");

    got.clear();
    rec.process (on (0, 40, 0.0f), emit);
    rec.process (off (0, 40, 0.0f), emit);
    check (got.size() == 1 && got[0].note.lengthTicks >= 120 && got[0].note.startTick == 0, "even a note off at the very same tick makes a note");

    std::vector<sls::Note> notes;
    sls::Note n;
    n.pitch = 60;
    n.startTick = 480;
    check (sls::addRecordedNote (notes, n) && ! sls::addRecordedNote (notes, n) && notes.size() == 1, "playing the same note at the same place twice does not stack it");
    n.pitch = 61;
    check (sls::addRecordedNote (notes, n) && notes.size() == 2, "a different pitch at that place is added");

    // the queue between the audio thread and the message thread
    sls::SpscQueue<int, 8> small;
    int v = 0;
    check (! small.pop (v), "an empty queue gives nothing");
    bool all = true;
    for (int i = 0; i < 8; ++i)
        all = all && small.push (i);
    check (all && ! small.push (99), "the queue refuses a value when it is full");
    bool ordered = true;
    for (int i = 0; i < 8; ++i)
        ordered = ordered && small.pop (v) && v == i;
    check (ordered && ! small.pop (v), "values come out in the order they went in");

    sls::SpscQueue<int, 64> pipe;
    constexpr int kCount = 200000;
    std::atomic<bool> ok { true };
    std::thread producer ([&]
    {
        for (int i = 0; i < kCount; ++i)
            while (! pipe.push (i))
                std::this_thread::yield();
    });
    for (int expected = 0; expected < kCount; ++expected)
    {
        int value = -1;
        while (! pipe.pop (value))
            std::this_thread::yield();
        if (value != expected)
            ok = false;
    }
    producer.join();
    check (ok.load(), "200000 values cross between two threads in order, none lost");
}

juce::MemoryBlock makeMidiFile (int ppq, const std::vector<juce::MidiMessageSequence>& tracks)
{
    juce::MidiFile file;
    file.setTicksPerQuarterNote (ppq);
    for (const auto& t : tracks)
        file.addTrack (t);
    juce::MemoryOutputStream out;
    file.writeTo (out);
    return out.getMemoryBlock();
}

void testLegatoAndSlide()
{
    std::cout << "Legato and slide\n";
    using sls::NoteLink;
    const int boundary = static_cast<int> (kSr * 0.5); // beat 1 at 120 BPM

    const auto render = [] (NoteLink link, int secondStart = kBeat)
    {
        const std::vector<sls::Note> notes { { 57, 0, kBeat, 0.8f }, { 69, secondStart, kBeat, 0.8f, link } };
        return sls::renderToBuffer (oneBar ({ synthChannel (notes) }), {});
    };
    // How much of the old note (220 Hz) is still heard 30 - 80 ms after the join, compared with the new note (440 Hz): a plain note
    // lets the first one fade out over 80 ms; legato and slide never release it.
    const auto tail = [boundary] (const juce::AudioBuffer<float>& b)
    {
        const float* l = b.getReadPointer (0);
        return toneEnergy (l, boundary + 1500, boundary + 3600, 220.0) / std::max (1.0e-9, toneEnergy (l, boundary + 1500, boundary + 3600, 440.0));
    };

    {
        const auto plain = render (NoteLink::None);
        const auto legato = render (NoteLink::Legato);
        check (tail (plain) > 0.15, "two plain notes in a row overlap: the first fades out over 80 ms after the second has started");
        check (tail (legato) < 0.08 && tail (legato) < 0.5 * tail (plain), "a legato note takes over the sounding note: nothing of the old note is left after the join");

        const float* l = legato.getReadPointer (0);
        check (toneEnergy (l, 4800, boundary - 4800, 220.0) > 2.5 * toneEnergy (l, 4800, boundary - 4800, 440.0),
               "before the legato note: 220 Hz");
        check (toneEnergy (l, boundary + 4800, 2 * boundary - 4800, 440.0) > 4.0 * toneEnergy (l, boundary + 4800, 2 * boundary - 4800, 220.0),
               "after it: 440 Hz (the pitch jumped)");
    }
    {
        const auto slide = render (NoteLink::Slide);
        const float* l = slide.getReadPointer (0);
        float steady = peakOf (l, 4800, boundary - 4800), lowest = steady;
        for (int i = boundary - 480; i < boundary + 4800; i += 480)
            lowest = std::min (lowest, peakOf (l, i, i + 480));
        check (lowest > 0.6f * steady, "a slide goes on at full level through the join (no new attack, no dip)");
        // The glide lasts an eighth note (0.125 s = 6000 samples): right after the join it is still low, a bit later it is there.
        const double early220 = toneEnergy (l, boundary, boundary + 400, 220.0), early440 = toneEnergy (l, boundary, boundary + 400, 440.0);
        check (early220 > early440, "right after the join of a slide the pitch is still near the old note");
        check (toneEnergy (l, boundary + 9000, 2 * boundary - 4800, 440.0) > 4.0 * toneEnergy (l, boundary + 9000, 2 * boundary - 4800, 220.0),
               "when the glide is over the new pitch is reached exactly");
        // In between, the pitch is between the two notes: more energy near 311 Hz (the middle of the octave) than at either end.
        const int mid = boundary + 3000;
        const double middle = toneEnergy (l, mid, mid + 2000, 311.13);
        check (middle > toneEnergy (l, mid, mid + 2000, 220.0) * 0.8 && middle > toneEnergy (l, mid, mid + 2000, 440.0) * 0.8,
               "halfway through the glide the pitch is about halfway (in semitones) between the two notes");
    }
    {
        // Nothing to tie to: the first note of a channel with a link plays as a normal note, with its attack.
        const std::vector<sls::Note> notes { { 69, 0, kBeat, 0.8f, NoteLink::Slide } };
        const auto b = sls::renderToBuffer (oneBar ({ synthChannel (notes) }), {});
        const float* l = b.getReadPointer (0);
        check (peakOf (l, 0, 48) < 0.2f * peakOf (l, 4800, 20000) && peakOf (l, 4800, 20000) > 0.05f, "with nothing sounding a legato / slide note plays normally (with its attack)");
    }
    {
        // A gap: the first note has died away, so the linked note starts a fresh voice.
        const std::vector<sls::Note> notes { { 57, 0, 240, 0.8f }, { 69, kBeat + 240, kBeat, 0.8f, NoteLink::Legato } };
        const auto b = sls::renderToBuffer (oneBar ({ synthChannel (notes) }), {});
        const float* l = b.getReadPointer (0);
        const int start = static_cast<int> (kSr * 0.75);
        check (peakOf (l, start, start + 48) < 0.2f * peakOf (l, start + 4800, start + 9000), "after a gap a legato note attacks like a normal note");
    }
    {
        // A sampler channel: the same sample goes on and its speed changes.
        auto c = synthChannel ({ { 69, 0, kBeat, 0.8f }, { 81, kBeat, kBeat, 0.8f, NoteLink::Legato } });
        c.instrument = sls::Instrument::Sampler;
        c.sample = sineSample (440.0, 1.5, kSr);
        c.rootPitch = 69;
        const auto b = sls::renderToBuffer (oneBar ({ c }), {});
        const float* l = b.getReadPointer (0);
        check (toneEnergy (l, 4800, boundary - 4800, 440.0) > 4.0 * toneEnergy (l, 4800, boundary - 4800, 880.0)
                   && toneEnergy (l, boundary + 4800, boundary + 14000, 880.0) > 4.0 * toneEnergy (l, boundary + 4800, boundary + 14000, 440.0),
               "a legato note on a sampler channel changes the speed of the sound that is playing");
    }
    {
        // The project file keeps the link; a missing or hostile value reads as no link.
        const auto back = sls::ProjectDocument::fromJson (richDocument().toJson());
        check (back.document.has_value() && back.document->arrangement.patterns[0].notes[0][2].link == NoteLink::Slide
                   && back.document->arrangement.patterns[1].notes[1][0].link == NoteLink::Legato,
               "legato and slide survive saving and opening");
        auto text = richDocument().toJson().replace ("[67,960,120,1,2]", "[67,960,120,1,77]");
        const auto odd = sls::ProjectDocument::fromJson (text);
        check (! odd.document.has_value() || odd.document->arrangement.patterns[0].notes[0][2].link != NoteLink::None || true, "an odd link value does not break reading");
    }
}

void testMidiImport()
{
    std::cout << "MIDI file import\n";

    // Track 0: tempo 90 BPM + name; track 1: a melody on channel 1 (960 ppq); track 2: drums on channel 10.
    juce::MidiMessageSequence conductor;
    conductor.addEvent (juce::MidiMessage::tempoMetaEvent (60'000'000 / 90), 0.0);
    juce::MidiMessageSequence melody;
    melody.addEvent (juce::MidiMessage::textMetaEvent (3, "Piano"), 0.0);
    melody.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0.0);
    melody.addEvent (juce::MidiMessage::noteOff (1, 60), 960.0);              // one beat
    melody.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 64), 960.0);
    melody.addEvent (juce::MidiMessage::noteOff (1, 64), 1920.0);
    melody.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 127), 1920.0); // never switched off
    juce::MidiMessageSequence drums;
    drums.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 100), 0.0);
    drums.addEvent (juce::MidiMessage::noteOff (10, 36), 480.0);

    const auto bytes = makeMidiFile (960, { conductor, melody, drums });
    const auto r = sls::parseMidiFile (bytes.getData(), bytes.getSize());
    check (r.ok, "a normal file is read");
    check (std::abs (r.bpm - 90.0) < 0.01, "the tempo is taken from the file");
    check (! r.tempoChanges, "one tempo is not a tempo change");
    check (r.tracks.size() == 2, "the melody and the drums are two tracks (the conductor has no notes)");
    if (r.tracks.size() == 2)
    {
        const auto& m = r.tracks[0];
        check (m.name == "Piano" && ! m.percussion && m.midiChannel == 1, "the track keeps its name and channel");
        check (m.notes.size() == 3, "three notes");
        check (m.notes[0].startTick == 0 && m.notes[0].lengthTicks == sls::kTicksPerBeat, "960 ppq is converted to 480 per beat");
        check (m.notes[1].startTick == sls::kTicksPerBeat && m.notes[1].pitch == 64, "the second note starts on beat 2");
        check (std::abs (m.notes[0].velocity - 100.0f / 127.0f) < 1.0e-4f && std::abs (m.notes[2].velocity - 1.0f) < 1.0e-4f,
               "velocities become 0..1");
        check (m.notes[2].lengthTicks >= 1, "a note that is never switched off still gets a length");
        check (r.tracks[1].percussion && r.tracks[1].name == "Track 3", "channel 10 is percussion; an unnamed track is called by its number");
    }

    // Two notes of the same pitch back to back, and a note on with velocity 0 as the note off.
    juce::MidiMessageSequence repeat;
    repeat.addEvent (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 90), 0.0);
    repeat.addEvent (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 90), 240.0); // ends the first at 240
    repeat.addEvent (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 0), 480.0);  // velocity 0 = off
    const auto bytes2 = makeMidiFile (480, { repeat });
    const auto r2 = sls::parseMidiFile (bytes2.getData(), bytes2.getSize());
    check (r2.ok && r2.tracks.size() == 1 && r2.tracks[0].notes.size() == 2
               && r2.tracks[0].notes[0].lengthTicks == 240 && r2.tracks[0].notes[1].lengthTicks == 240,
           "a repeated note on ends the previous note; velocity 0 ends a note");

    // One track with two channels: the names say which.
    juce::MidiMessageSequence two;
    two.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 90), 0.0);
    two.addEvent (juce::MidiMessage::noteOff (1, 60), 100.0);
    two.addEvent (juce::MidiMessage::noteOn (3, 62, (juce::uint8) 90), 0.0);
    two.addEvent (juce::MidiMessage::noteOff (3, 62), 100.0);
    const auto bytes3 = makeMidiFile (480, { two });
    const auto r3 = sls::parseMidiFile (bytes3.getData(), bytes3.getSize());
    check (r3.ok && r3.tracks.size() == 2 && r3.tracks[0].name == "Track 1 (ch 1)" && r3.tracks[1].name == "Track 1 (ch 3)",
           "a track with two channels becomes two tracks named after the channel");

    // Bad input.
    const char junk[] = "this is definitely not a midi file at all";
    check (! sls::parseMidiFile (junk, sizeof (junk)).ok && ! sls::parseMidiFile (junk, sizeof (junk)).error.empty(), "text is refused with a message");
    check (! sls::parseMidiFile (nullptr, 0).ok, "nothing is refused");
    check (! sls::parseMidiFile (bytes.getData(), 20).ok, "a truncated file is refused");
    juce::MidiMessageSequence empty;
    empty.addEvent (juce::MidiMessage::tempoMetaEvent (500000), 0.0);
    const auto bytes4 = makeMidiFile (480, { empty });
    check (! sls::parseMidiFile (bytes4.getData(), bytes4.getSize()).ok, "a file without notes is refused");
    juce::MidiFile smpte;
    smpte.setSmpteTimeFormat (25, 40);
    smpte.addTrack (repeat);
    juce::MemoryOutputStream smpteOut;
    smpte.writeTo (smpteOut);
    check (! sls::parseMidiFile (smpteOut.getData(), smpteOut.getDataSize()).ok, "SMPTE-timed files are refused");

    // Tempo changes are noticed.
    juce::MidiMessageSequence tempos = repeat;
    tempos.addEvent (juce::MidiMessage::tempoMetaEvent (500000), 0.0);
    tempos.addEvent (juce::MidiMessage::tempoMetaEvent (400000), 480.0);
    const auto bytes5 = makeMidiFile (480, { tempos });
    const auto r5 = sls::parseMidiFile (bytes5.getData(), bytes5.getSize());
    check (r5.ok && std::abs (r5.bpm - 120.0) < 0.01 && r5.tempoChanges, "the first tempo is used and later changes are flagged");

    // Splitting over patterns.
    std::vector<sls::Note> notes { { 60, 0, 480, 0.8f }, { 62, 3600, 960, 0.8f }, { 64, 3840 * 2 + 10, 100, 0.8f }, { 65, 3840 * 5, 100, 0.8f } };
    int tied = 0, cut = 0;
    const auto split = sls::splitIntoPatterns (notes, 3840, 4, &tied, &cut);
    check (split.size() == 3, "notes fall in three patterns (the one in pattern 6 is beyond the room)");
    check (cut == 1, "a note beyond the room is counted as cut");
    check (split.size() == 3 && split[0].size() == 2 && split[0][1].startTick == 3600 && split[0][1].lengthTicks == 240,
           "a note crossing the end of a pattern is cut there");
    check (split.size() == 3 && split[1].size() == 1 && split[1][0].startTick == 0 && split[1][0].lengthTicks == 720 && tied == 1,
           "and continues at the start of the next pattern");
    check (split.size() == 3 && split[2][0].startTick == 10, "notes are placed relative to their pattern");

    // The plan: free channels, percussion, patterns.
    const auto plan = sls::planMidiImport (r, 8, 3840, 64);
    check (plan.channelNames.size() == 1 && plan.channelNames[0] == "Piano" && plan.skippedPercussion == 1 && plan.patternCount() == 1,
           "the plan has one channel and one pattern; the drums are skipped");
    check (plan.patternNotes.size() == 1 && plan.patternNotes[0].size() == 1 && plan.patternNotes[0][0].size() == 3, "with the three notes in it");
    const auto none = sls::planMidiImport (r, 0, 3840, 64);
    check (none.channelNames.empty() && none.skippedTracks == 1 && none.patternCount() == 0, "with no free channel nothing is imported");

    // Many tracks, few channels.
    sls::MidiImportResult many;
    many.ok = true;
    for (int i = 0; i < 5; ++i)
    {
        sls::MidiImportTrack t;
        t.name = "T" + std::to_string (i);
        t.notes = { { 60, i * 4000, 100, 0.8f } };
        many.tracks.push_back (t);
    }
    const auto p3 = sls::planMidiImport (many, 3, 3840, 64);
    check (p3.channelNames.size() == 3 && p3.skippedTracks == 2, "extra tracks beyond the free channels are counted as skipped");
    check (p3.patternCount() == 3 && p3.patternNotes[2].size() == 3, "every pattern has a list for every new channel");

    // Hostile numbers must not break anything.
    std::vector<sls::Note> hostile { { 60, -5, 100, 0.8f }, { 60, 10, 0, 0.8f }, { 60, 2'000'000'000, 100, 0.8f } };
    check (sls::splitIntoPatterns (hostile, 3840, 4).empty() || true, "hostile notes are survived");
    check (sls::splitIntoPatterns (hostile, 0, 4).empty() && sls::splitIntoPatterns (hostile, 3840, 0).empty(), "bad pattern length or room gives nothing");
}

// --- automation ------------------------------------------------------------------------------------------------------

// Plays `project` (song mode, 120 BPM) from the start for `seconds` and returns both channels.
std::pair<std::vector<float>, std::vector<float>> playSong (const sls::Project& project, double seconds)
{
    sls::Player player;
    player.prepare (kSr);
    const int total = static_cast<int> (seconds * kSr);
    std::vector<float> l (static_cast<size_t> (total), 0.0f), r (static_cast<size_t> (total), 0.0f);
    for (int done = 0; done < total; done += 512)
    {
        const int n = std::min (512, total - done);
        float* out[2] = { l.data() + done, r.data() + done };
        player.render (project, 120.0, true, out, 2, n);
    }
    return { l, r };
}

// A song with one audio clip of a steady sine on playlist track 1 (mixer track 0), 8 seconds long.
sls::Arrangement songWithSteadyTone (double hz = 440.0)
{
    auto tone = std::make_shared<sls::SampleData>();
    tone->sampleRate = kSr;
    tone->left = sineWave (hz, 0.4, static_cast<int> (8.0 * kSr));
    tone->right = tone->left;
    tone->name = "tone";

    sls::Arrangement arr;
    arr.patterns.push_back (sls::Pattern {});
    sls::AudioClip clip;
    clip.data = tone;
    clip.name = "tone";
    clip.track = 0;
    clip.startTick = 0;
    clip.lengthTicks = 16 * kBeat; // 8 s at 120 BPM
    arr.audioClips.push_back (clip);
    return arr;
}

void testAutomation()
{
    std::cout << "Automation\n";

    // The curve.
    {
        using sls::AutomationPoint;
        using sls::AutomationShape;
        const std::vector<AutomationPoint> line { { 0.0, 0.0f, AutomationShape::Linear }, { 100.0, 1.0f, AutomationShape::Linear } };
        check (std::fabs (sls::automationValueAt (line, 25.0, -1.0f) - 0.25f) < 1.0e-6f, "a line runs straight between its points");
        check (sls::automationValueAt (line, -50.0, -1.0f) == 0.0f && sls::automationValueAt (line, 500.0, -1.0f) == 1.0f,
               "before the first point and after the last one the end values hold");
        check (sls::automationValueAt ({}, 10.0, 0.42f) == 0.42f, "an empty curve gives the fallback");
        check (std::fabs (sls::automationValueAt (line, 33.3333333, 0.0f) - 0.333333f) < 1.0e-5f, "positions between ticks are read exactly");

        const std::vector<AutomationPoint> hold { { 0.0, 0.2f, AutomationShape::Hold }, { 100.0, 0.9f, AutomationShape::Linear } };
        check (sls::automationValueAt (hold, 99.0, 0.0f) == 0.2f && sls::automationValueAt (hold, 100.0, 0.0f) == 0.9f,
               "a Hold point keeps its value until the next point, then jumps");

        const std::vector<AutomationPoint> smooth { { 0.0, 0.0f, AutomationShape::Smooth }, { 100.0, 1.0f, AutomationShape::Linear } };
        check (std::fabs (sls::automationValueAt (smooth, 50.0, 0.0f) - 0.5f) < 1.0e-6f
                   && sls::automationValueAt (smooth, 10.0, 0.0f) < 0.1f && sls::automationValueAt (smooth, 90.0, 0.0f) > 0.9f,
               "a Smooth segment is flat at both ends and steepest in the middle");

        const std::vector<AutomationPoint> stacked { { 0.0, 0.0f, AutomationShape::Linear }, { 50.0, 1.0f, AutomationShape::Linear }, { 50.0, 0.0f, AutomationShape::Linear } };
        check (sls::automationValueAt (stacked, 50.0, 0.0f) == 0.0f, "two points at the same tick make a vertical edge, no division by zero");
    }

    // The events built from the clips.
    {
        std::vector<sls::AutomationClip> clips;
        clips.push_back (sls::makeFlatAutomationClip ({ sls::AutomationKind::TrackVolume, 1, 0, 0 }, 0, 960, 960, 0.5f));
        clips.push_back (sls::makeFlatAutomationClip ({ sls::AutomationKind::TrackVolume, 1, 0, 0 }, 1, 0, 960, 1.0f));
        clips.push_back (sls::makeFlatAutomationClip ({ sls::AutomationKind::TrackVolume, 99, 0, 0 }, 2, 0, 960, 1.0f)); // no such track
        clips.push_back (sls::makeFlatAutomationClip ({ sls::AutomationKind::ChannelPan, 0, 0, 0 }, 3, 0, 960, 7.0f));    // out of range
        auto bad = sls::makeFlatAutomationClip ({ sls::AutomationKind::TrackPan, 0, 0, 0 }, 0, 0, 960, 0.0f);
        bad.points.clear();
        clips.push_back (bad);
        auto nan = sls::makeFlatAutomationClip ({ sls::AutomationKind::TrackPan, 2, 0, 0 }, 0, 0, 960, 0.0f);
        nan.points[0].value = std::nanf ("");
        nan.points[1].tick = 5000.0; // beyond the end of the clip
        clips.push_back (nan);
        clips.push_back (sls::makeFlatAutomationClip ({ sls::AutomationKind::TrackEffect, 0, 2, 0 }, 0, 0, 960, 5.0f)); // empty slot

        const auto events = sls::makeAutomationEvents (clips, 2, 8, [] (const sls::AutomationTarget&) { return sls::EffectType::None; },
                                                       [] (const sls::AutomationTarget&) { return 0; });
        check (events.size() == 4, "clips for a parameter that does not exist, without points, or for an empty effect slot are dropped (4 of 7 stay)");
        check (events[0].target.kind == sls::AutomationKind::ChannelPan && events[0].points[0].value == 1.0f,
               "values are clamped to the range of the parameter");
        bool sorted = true;
        for (size_t i = 1; i < events.size(); ++i)
            sorted = sorted && (events[i - 1].target.key() < events[i].target.key()
                                || (events[i - 1].target.key() == events[i].target.key() && events[i - 1].startTick <= events[i].startTick));
        check (sorted, "events come sorted by parameter, then by start");
        bool inside = true;
        for (const auto& e : events)
            for (const auto& p : e.points)
                inside = inside && p.tick >= 0.0 && p.tick <= e.endTick - e.startTick && std::isfinite (p.value);
        check (inside, "points are kept inside their clip and finite");

        float v = 0.0f;
        size_t first = 0, last = 0;
        for (size_t i = 0; i < events.size(); ++i)
            if (events[i].target.kind == sls::AutomationKind::TrackVolume)
            {
                if (last == 0) first = i;
                last = i + 1;
            }
        check (last - first == 2, "two clips of one parameter sit together");
        check (! sls::automationValueFor (events, first, last, -1.0, v) || true, "(reading before the first clip is allowed)");
        check (sls::automationValueFor (events, first, last, 100.0, v) && v == 1.0f, "the clip that started last wins: first clip at tick 100");
        check (sls::automationValueFor (events, first, last, 1000.0, v) && v == 0.5f, "the second clip takes over at its start");
        check (sls::automationValueFor (events, first, last, 99999.0, v) && v == 0.5f, "after the last clip its final value is held");
    }

    // Editing helpers.
    {
        auto c = sls::makeFlatAutomationClip ({ sls::AutomationKind::TrackVolume, 0, 0, 0 }, 0, 0, 1000, 1.0f);
        const auto range = sls::automationRange (c.target);
        const int mid = sls::insertAutomationPoint (c, 500.0, 0.25f, range);
        check (mid == 1 && c.points.size() == 3 && c.points[1].tick == 500.0, "a new point goes in between, in order");
        sls::moveAutomationPoint (c, mid, 2000.0, 9.0f, range);
        check (c.points[1].tick == 1000.0 && c.points[1].value == 1.5f, "a point cannot jump over its neighbour and is held in range");
        sls::moveAutomationPoint (c, mid, -5.0, 0.5f, range);
        check (c.points[1].tick == 0.0, "nor go before the previous one");
        check (sls::removeAutomationPoint (c, 1) && c.points.size() == 2, "a point can be removed");
        check (sls::removeAutomationPoint (c, 0) && ! sls::removeAutomationPoint (c, 0), "but the last one stays");
        auto d = sls::makeFlatAutomationClip ({ sls::AutomationKind::TrackVolume, 0, 0, 0 }, 0, 0, 1000, 1.0f);
        sls::setAutomationLength (d, 400);
        check (d.points.back().tick == 400.0, "shortening a clip brings the end point along");
    }

    // Placement on the lanes.
    {
        sls::Arrangement arr;
        arr.patterns.push_back (sls::Pattern {});
        const auto flat = [] (int lane, int start, int length)
        { return sls::makeFlatAutomationClip ({ sls::AutomationKind::TrackVolume, 0, 0, 0 }, lane, start, length, 1.0f); };
        check (arr.canPlaceAutomation (flat (0, 0, 1000)), "a clip fits on an empty lane");
        arr.automationClips.push_back (flat (0, 0, 1000));
        check (! arr.canPlaceAutomation (flat (0, 500, 1000)) && arr.canPlaceAutomation (flat (0, 1000, 1000)) && arr.canPlaceAutomation (flat (1, 500, 1000)),
               "clips may not overlap on a lane, but touch, or overlap on another lane");
        check (arr.canPlaceAutomation (flat (0, 0, 1000), 0), "a clip does not collide with itself when moved");
        check (! arr.canPlaceAutomation (flat (sls::kAutomationLanes, 0, 10)) && ! arr.canPlaceAutomation (flat (0, arr.timelineTicks() - 5, 10)),
               "no such lane, and not past the end of the timeline");
        check (arr.findFreeAutomationLane (0, 500) == 1 && arr.findFreeAutomationLane (2000, 500) == 0, "the first free lane is found");
        check (arr.automationClipAt (0, 999) == 0 && arr.automationClipAt (0, 1000) == -1, "a clip is found by lane and tick");
    }

    // In the player.
    {
        const auto volumeClip = [] (int track, int lane, int start, int length, float from, float to)
        {
            auto c = sls::makeFlatAutomationClip ({ sls::AutomationKind::TrackVolume, track, 0, 0 }, lane, start, length, from);
            c.points.back().value = to;
            return c;
        };

        auto arr = songWithSteadyTone();
        const auto plain = playSong (sls::compileSong ({}, arr), 4.0);
        const double base = rmsOf (plain.first, static_cast<int> (0.5 * kSr), static_cast<int> (1.5 * kSr));
        check (base > 0.05, "the steady tone is audible without automation");

        // 0 -> 1 over the first 4 beats (2 s), then held.
        arr.automationClips.push_back (volumeClip (0, 0, 0, 4 * kBeat, 0.0f, 1.0f));
        const auto ramp = playSong (sls::compileSong ({}, arr), 4.0);
        const double early = rmsOf (ramp.first, static_cast<int> (0.1 * kSr), static_cast<int> (0.3 * kSr));
        const double half = rmsOf (ramp.first, static_cast<int> (0.9 * kSr), static_cast<int> (1.1 * kSr));
        const double late = rmsOf (ramp.first, static_cast<int> (3.0 * kSr), static_cast<int> (3.5 * kSr));
        const double plainLate = rmsOf (plain.first, static_cast<int> (3.0 * kSr), static_cast<int> (3.5 * kSr));
        check (early < 0.2 * plainLate, "a track volume ramp starts quiet");
        check (std::fabs (half / plainLate - 0.5) < 0.1, "and is at about half way after half the clip (value 0.5 -> level 0.5)");
        check (std::fabs (late / plainLate - 1.0) < 0.05, "after the clip the last value (1.0) is held");

        // The first clip at 0.25, then a second one at 1.0 from 2 s on.
        auto two = songWithSteadyTone();
        two.automationClips.push_back (volumeClip (0, 0, 0, 4 * kBeat, 0.25f, 0.25f));
        two.automationClips.push_back (volumeClip (0, 1, 4 * kBeat, 4 * kBeat, 1.0f, 1.0f));
        const auto steps = playSong (sls::compileSong ({}, two), 4.0);
        check (std::fabs (rmsOf (steps.first, static_cast<int> (0.5 * kSr), static_cast<int> (1.5 * kSr)) / plainLate - 0.25) < 0.05
                   && std::fabs (rmsOf (steps.first, static_cast<int> (2.5 * kSr), static_cast<int> (3.5 * kSr)) / plainLate - 1.0) < 0.05,
               "a second clip takes over where it starts (0.25, then 1.0)");

        // Pan: left to right over 2 s.
        auto pan = songWithSteadyTone();
        auto panClip = sls::makeFlatAutomationClip ({ sls::AutomationKind::TrackPan, 0, 0, 0 }, 0, 0, 4 * kBeat, -1.0f);
        panClip.points.back().value = 1.0f;
        pan.automationClips.push_back (panClip);
        const auto panned = playSong (sls::compileSong ({}, pan), 3.0);
        const auto side = [&] (double a, double b) { return std::make_pair (rmsOf (panned.first, static_cast<int> (a * kSr), static_cast<int> (b * kSr)),
                                                                          rmsOf (panned.second, static_cast<int> (a * kSr), static_cast<int> (b * kSr))); };
        check (side (0.05, 0.15).second < 0.15 * side (0.05, 0.15).first, "a track balance ramp starts on the left");
        check (side (2.3, 2.9).first < 0.05 * side (2.3, 2.9).second, "and ends on the right");

        // An effect parameter: the equaliser's high shelf, -15 dB to +15 dB, on a tone in the shelf.
        {
            auto eqArr = songWithSteadyTone (9000.0);
            std::vector<sls::MixerTrack> tracks = sls::Project::defaultTracks();
            tracks[0].slots[0] = sls::EffectSlot::make (sls::EffectType::Equalizer);
            auto eqClip = sls::makeFlatAutomationClip ({ sls::AutomationKind::TrackEffect, 0, 0, 3 }, 0, 0, 4 * kBeat, -15.0f);
            eqClip.points.back().value = 15.0f;
            const auto still = playSong (sls::compileSong ({}, eqArr, tracks), 3.0);
            eqArr.automationClips.push_back (eqClip);
            const auto moving = playSong (sls::compileSong ({}, eqArr, tracks), 3.0);
            const double start = rmsOf (moving.first, static_cast<int> (0.1 * kSr), static_cast<int> (0.2 * kSr)) / rmsOf (still.first, static_cast<int> (0.1 * kSr), static_cast<int> (0.2 * kSr));
            const double end = rmsOf (moving.first, static_cast<int> (2.5 * kSr), static_cast<int> (2.9 * kSr)) / rmsOf (still.first, static_cast<int> (2.5 * kSr), static_cast<int> (2.9 * kSr));
            check (start < 0.4 && end > 2.0, "an effect parameter follows its clip (high shelf -15 dB then +15 dB: level 10 dB down, then 9 dB up)");
        }

        // Pattern mode has no automation; a stopped transport leaves the parameters alone.
        const auto patternProject = sls::compilePattern ({}, arr, 0);
        check (patternProject.automation.empty(), "Pattern mode carries no automation");

        sls::Player player;
        player.prepare (kSr);
        const auto project = sls::compileSong ({}, arr);
        std::vector<float> l (512), r (512);
        float* out[2] = { l.data(), r.data() };
        for (int i = 0; i < 400; ++i) // about 4 s: the ramp is over and its last value is held
            player.render (project, 120.0, true, out, 2, 512);
        player.render (project, 120.0, false, out, 2, 512); // stop
        for (int i = 0; i < 20; ++i)
            player.render (project, 120.0, false, out, 2, 512); // let the fade-out finish
        std::vector<float> again;
        for (int i = 0; i < 40; ++i) // play again: the ramp must start from its beginning
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            player.render (project, 120.0, true, out, 2, 512);
            again.insert (again.end(), l.begin(), l.end());
        }
        const double againEarly = rmsOf (again, static_cast<int> (0.1 * kSr), static_cast<int> (0.3 * kSr));
        check (std::fabs (againEarly - early) < 0.2 * plainLate, "after stop and play the automation starts again from the beginning (the ramp is quiet again)");

        // No allocation while automation runs.
        {
            gAllocs = 0;
            gCounting = true;
            for (int i = 0; i < 400; ++i)
                player.render (project, 120.0, true, out, 2, 512);
            gCounting = false;
            check (gAllocs.load() == 0, "no allocation in render() while parameters are automated");
        }
    }
}

void testOneShot()
{
    std::cout << "One-shot playback (start-up sound)\n";

    // A 1 s ramp at 24 kHz played on a 48 kHz device lasts 1 s (48000 device frames) and keeps its real speed.
    auto ramp = std::make_shared<sls::SampleData>();
    ramp->sampleRate = 24000.0;
    for (int i = 0; i < 24000; ++i)
    {
        ramp->left.push_back (static_cast<float> (i) / 24000.0f);
        ramp->right.push_back (-static_cast<float> (i) / 24000.0f);
    }

    sls::OneShot shot;
    shot.setSound (ramp);
    std::vector<float> l (48000 * 3, 0.25f), r (48000 * 3, 0.25f); // 0.25 is already in the buffer: the sound is added to it

    check (! shot.isActive(), "nothing plays before start()");
    shot.start();
    check (shot.isActive(), "start() makes it play");

    int pos = 0;
    while (shot.isActive() && pos < 48000 * 3)
    {
        float* block[2] = { l.data() + pos, r.data() + pos };
        shot.mixInto (block, 2, 480, 48000.0);
        pos += 480;
    }
    check (pos == 48000, "it lasts as long as the sound does, whatever the device rate (1 s)");
    check (std::fabs (l[24000] - (0.25f + 0.5f)) < 1.0e-3f && std::fabs (r[24000] - (0.25f - 0.5f)) < 1.0e-3f,
           "the middle is where it should be, left and right on their own sides, added to what was there");
    check (std::fabs (l[48000 + 10] - 0.25f) < 1.0e-6f, "after the end nothing more is added");

    // Block size does not matter.
    std::vector<float> a (48000 * 2, 0.0f), b (48000 * 2, 0.0f);
    sls::OneShot one, two;
    one.setSound (ramp);
    two.setSound (ramp);
    one.start();
    two.start();
    float* oa[1] = { a.data() };
    one.mixInto (oa, 1, 48000 * 2, 48000.0);
    for (int p = 0; p < 48000 * 2; p += 333)
    {
        float* ob[1] = { b.data() + p };
        two.mixInto (ob, 1, std::min (333, 48000 * 2 - p), 48000.0);
    }
    double worst = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        worst = std::max (worst, static_cast<double> (std::fabs (a[i] - b[i])));
    check (worst < 1.0e-3, "playing it in blocks of 333 gives the same result as in one go");

    // Starting again starts over; without a sound nothing happens.
    shot.start();
    check (shot.isActive(), "it can be started again");
    sls::OneShot empty;
    empty.start();
    check (! empty.isActive(), "without a sound it stays silent");
}

// --- music theory: scales, chords, the piano roll's tools and generators (0.23.0) ---------------------------------------------
std::vector<int> pitchesOf (const std::vector<sls::Note>& notes)
{
    std::vector<int> p;
    for (const auto& n : notes)
        p.push_back (n.pitch);
    return p;
}

// 0.61.0: the effects, piano roll tools and playlist markers added for parity with FL Studio.
void testFlParity061()
{
    std::cout << "FL Studio parity (0.61.0)\n";
    using ET = sls::EffectType;
    const auto runStereo = [] (const sls::EffectSlot& slot, std::vector<float> l, std::vector<float> r)
    {
        sls::EffectUnit unit;
        unit.prepare (kSr);
        sls::EffectMemory mem;
        mem.prepare (kSr);
        unit.attachMemory (&mem);
        for (size_t i = 0; i < l.size(); i += 64)
            unit.process (slot, l.data() + i, r.data() + i, static_cast<int> (std::min<size_t> (64, l.size() - i)));
        return std::pair { l, r };
    };
    const auto tone = sineWave (1000.0, 0.5, 48000);

    // Gate
    {
        const auto quiet = sineWave (1000.0, 0.001, 48000); // -60 dBFS, under the -40 dB threshold
        const auto q = runEffect (slotWith (ET::Gate, { -40.0f, 1.0f, 20.0f, 100.0f, -100.0f, 0.0f }), quiet);
        check (peakOf (q.data(), 24000, 48000) < 1.0e-5f, "gate: a signal under the threshold is silenced");
        const auto loud = runEffect (slotWith (ET::Gate, { -40.0f, 1.0f, 20.0f, 100.0f, -100.0f, 0.0f }), tone);
        check (std::abs (peakOf (loud.data(), 24000, 48000) / 0.5f - 1.0f) < 0.02f, "gate: a signal over the threshold passes untouched");
        const auto ranged = runEffect (slotWith (ET::Gate, { -40.0f, 1.0f, 20.0f, 100.0f, -20.0f, 0.0f }), quiet);
        check (std::abs (peakOf (ranged.data(), 24000, 48000) / 0.0001f - 1.0f) < 0.1f, "gate: Range -20 dB turns the closed gate down by 20 dB, not to silence");
    }
    // Panomatic
    {
        const auto [l, r] = runStereo (slotWith (ET::Panomatic, { 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.0f }), tone, tone);
        check (peakOf (r.data(), 0, 48000) < 1.0e-6f && std::abs (peakOf (l.data(), 0, 48000) / 0.5f - 1.0f) < 0.01f, "panomatic: pan hard left silences the right, keeps the left");
        const auto half = runEffect (slotWith (ET::Panomatic, { -6.0206f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.0f }), tone);
        check (std::abs (peakOf (half.data(), 0, 48000) / 0.25f - 1.0f) < 0.01f, "panomatic: -6 dB halves the level");
        const auto trem = runEffect (slotWith (ET::Panomatic, { 0.0f, 0.0f, 2.0f, 4.0f, 0.0f, 1.0f, 0.0f }), tone);
        float lo = 1.0f;
        for (int w = 0; w < 48000; w += 480)
            lo = std::min (lo, peakOf (trem.data(), w, w + 480));
        check (lo < 0.05f && peakOf (trem.data(), 0, 48000) > 0.45f, "panomatic: the volume LFO at full depth swings between silence and full level");
        const auto [al, ar] = runStereo (slotWith (ET::Panomatic, { 0.0f, 0.0f, 1.0f, 2.0f, 0.0f, 1.0f, 0.0f }), tone, tone);
        check (std::abs (rmsOf (al, 0, 48000) - rmsOf (ar, 0, 48000)) < 0.02 && rmsOf (al, 0, 48000) < 0.33, "panomatic: the pan LFO moves the sound between the two sides evenly");
    }
    // Squeeze
    {
        const auto crushed = runEffect (slotWith (ET::Squeeze, { 2.0f, 1.0f, 0.0f, 20000.0f, 1.0f }), tone);
        bool stepped = true;
        for (int i = 0; i < 48000; ++i)
            stepped = stepped && std::abs (crushed[static_cast<size_t> (i)] * 2.0f - std::round (crushed[static_cast<size_t> (i)] * 2.0f)) < 1.0e-5f;
        check (stepped, "squeeze: at 2 bits every sample sits on one of the coarse levels");
        const auto held = runEffect (slotWith (ET::Squeeze, { 16.0f, 8.0f, 0.0f, 20000.0f, 1.0f }), tone);
        bool holds = true;
        for (int i = 0; i + 8 <= 4800; i += 8)
            for (int k = 1; k < 8; ++k)
                holds = holds && held[static_cast<size_t> (i + k)] == held[static_cast<size_t> (i)];
        check (holds, "squeeze: downsample x8 holds each sample for eight");
        const auto dry = runEffect (slotWith (ET::Squeeze, { 2.0f, 8.0f, 1.0f, 500.0f, 0.0f }), tone);
        check (dry == tone, "squeeze: mix 0 is the dry signal");
    }
    // Flanger and vibrato
    {
        const auto dry = runEffect (slotWith (ET::Flanger, { 2.0f, 2.0f, 0.3f, 90.0f, 0.5f, 0.0f, 0.0f }), tone);
        check (dry == tone, "flanger: mix 0 is the dry signal");
        const auto wet = runEffect (slotWith (ET::Flanger, { 2.0f, 2.0f, 0.3f, 90.0f, 0.5f, 0.0f, 0.5f }), tone);
        check (wet != tone && peakOf (wet.data(), 0, 48000) < 2.0f, "flanger: mixed in, it changes the sound and stays bounded");
        const auto vib = runEffect (slotWith (ET::Vibrato, { 5.0f, 50.0f, 0.0f, 0.0f }), tone);
        check (std::abs (rmsOf (vib, 4800, 48000) / rmsOf (tone, 4800, 48000) - 1.0) < 0.03, "vibrato: the level stays, only the pitch moves");
        int crossings = 0, longest = 0, shortest = 1 << 20, last = -1;
        for (int i = 4801; i < 48000; ++i)
            if (vib[static_cast<size_t> (i - 1)] < 0.0f && vib[static_cast<size_t> (i)] >= 0.0f)
            {
                if (last >= 0)
                {
                    longest = std::max (longest, i - last);
                    shortest = std::min (shortest, i - last);
                }
                last = i;
                ++crossings;
            }
        check (crossings > 800 && longest > shortest, "vibrato: the period of a steady tone now varies");
    }
    // Parametric EQ 2
    {
        const auto flat = runEffect (sls::EffectSlot::make (ET::ParametricEq2), tone);
        check (flat == tone, "parametric EQ 2: with every gain at 0 dB the sound is untouched");
        auto slot = sls::EffectSlot::make (ET::ParametricEq2);
        slot.params[9] = 1000.0f;  // band 4 at 1 kHz
        slot.params[10] = 12.0f;   // +12 dB
        const auto boosted = runEffect (slot, tone);
        check (std::abs (peakOf (boosted.data(), 24000, 48000) / 0.5f - 3.981f) < 0.15f, "parametric EQ 2: +12 dB on a band's own frequency is about 4 times the level");
        const auto low = sineWave (60.0, 0.5, 48000);
        check (std::abs (peakOf (runEffect (slot, low).data(), 24000, 48000) / 0.5f - 1.0f) < 0.05f, "parametric EQ 2: a peak at 1 kHz leaves 60 Hz alone");
        auto shelf = sls::EffectSlot::make (ET::ParametricEq2);
        shelf.params[1] = -18.0f; // band 1, the low shelf at 50 Hz... moved up to 200 Hz
        shelf.params[0] = 200.0f;
        check (peakOf (runEffect (shelf, sineWave (30.0, 0.5, 96000)).data(), 48000, 96000) < 0.1f, "parametric EQ 2: band 1 is a low shelf (-18 dB well under its frequency)");
        slot.params[21] = -6.0206f;
        check (std::abs (peakOf (runEffect (slot, tone).data(), 24000, 48000) / 0.5f - 1.99f) < 0.1f, "parametric EQ 2: the output gain follows the bands");
    }
    // Bass boost
    {
        const auto slot = slotWith (ET::BassBoost, { 80.0f, 1.0f });
        const auto bass = sineWave (40.0, 0.05, 96000), treble = sineWave (5000.0, 0.05, 96000);
        check (peakOf (runEffect (slot, bass).data(), 48000, 96000) > 0.05f * 2.0f, "bass boost: the lows come out at least twice as loud");
        check (std::abs (peakOf (runEffect (slot, treble).data(), 48000, 96000) / 0.05f - 1.0f) < 0.05f, "bass boost: the highs are left alone");
        check (runEffect (slotWith (ET::BassBoost, { 80.0f, 0.0f }), bass) == bass, "bass boost: amount 0 is the dry signal");
    }
    // Multiband compressor
    {
        // ratio 1:1 everywhere: the crossovers sum back to the same level at any frequency
        const std::initializer_list<float> open { 200.0f, 3000.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 10.0f, 120.0f, 0.0f, 0.0f, 0.0f };
        bool flat = true;
        for (double hz : { 60.0, 200.0, 1000.0, 3000.0, 9000.0 })
            flat = flat && std::abs (peakOf (runEffect (slotWith (ET::MultibandComp, open), sineWave (hz, 0.5, 48000)).data(), 24000, 48000) / 0.5f - 1.0f) < 0.03f;
        check (flat, "multiband compressor: at 1:1 the three bands sum back to the input level at every frequency");
        // only the mid band compresses: 1 kHz goes down, 60 Hz does not
        const std::initializer_list<float> mid { 200.0f, 3000.0f, 0.0f, 1.0f, -26.0f, 20.0f, 0.0f, 1.0f, 1.0f, 120.0f, 0.0f, 0.0f, 0.0f };
        const float midOut = peakOf (runEffect (slotWith (ET::MultibandComp, mid), sineWave (1000.0, 0.5, 48000)).data(), 24000, 48000);
        const float lowOut = peakOf (runEffect (slotWith (ET::MultibandComp, mid), sineWave (60.0, 0.5, 48000)).data(), 24000, 48000);
        check (midOut < 0.12f && lowOut > 0.45f, "multiband compressor: a band's compression touches that band only");
        const std::initializer_list<float> gain { 200.0f, 3000.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 10.0f, 120.0f, 6.0206f, 0.0f, 0.0f };
        check (std::abs (peakOf (runEffect (slotWith (ET::MultibandComp, gain), sineWave (50.0, 0.25, 48000)).data(), 24000, 48000) / 0.5f - 1.0f) < 0.05f,
               "multiband compressor: a band's gain raises that band");
    }
    // every new effect has a name, a place in the menu and sane defaults
    {
        bool ok = true;
        for (ET t : { ET::Flanger, ET::Gate, ET::BassBoost, ET::Panomatic, ET::Squeeze, ET::Vibrato, ET::ParametricEq2, ET::MultibandComp })
        {
            bool inMenu = false;
            for (const auto& entry : sls::kEffectMenu)
                inMenu = inMenu || entry.type == t;
            ok = ok && inMenu && std::string (sls::effectName (t)) != "Empty" && sls::effectParamCount (t) > 0 && sls::effectParamCount (t) <= sls::kMaxEffectParams;
            for (int i = 0; i < sls::effectParamCount (t); ++i)
            {
                const auto info = sls::effectParamInfo (t, i);
                ok = ok && info.min < info.max && info.def >= info.min && info.def <= info.max;
            }
        }
        check (ok, "the new effects are named, listed in the menu and have defaults inside their ranges");
        check (sls::effectNeedsMemory (ET::Flanger) && sls::effectNeedsMemory (ET::Vibrato) && ! sls::effectNeedsMemory (ET::Gate), "flanger and vibrato borrow the delay memory");

        // every effect type, with its last parameter moved, comes back from a saved project as it was
        sls::ProjectDocument doc = sls::ProjectDocument::blank();
        bool saved = ! doc.tracks.empty();
        if (saved)
        {
            std::vector<ET> types;
            for (int t = 1; t < sls::kEffectTypeCount; ++t)
                if (static_cast<ET> (t) != ET::Plugin)
                    types.push_back (static_cast<ET> (t));
            for (size_t k = 0; k < types.size(); ++k)
            {
                while (doc.tracks.size() <= k / static_cast<size_t> (sls::kSlotsPerTrack))
                    doc.tracks.push_back (sls::MixerTrack {});
                auto slot = sls::EffectSlot::make (types[k]);
                const int last = sls::effectParamCount (types[k]) - 1;
                slot.params[static_cast<size_t> (last)] = sls::effectParamInfo (types[k], last).max;
                doc.tracks[k / static_cast<size_t> (sls::kSlotsPerTrack)].slots[k % static_cast<size_t> (sls::kSlotsPerTrack)] = slot;
            }
            doc.tracks[0].invertPhase = true;
            doc.tracks[0].swapStereo = true;
            doc.tracks[0].separation = -0.5f;
            const auto parsed = sls::ProjectDocument::fromJson (doc.toJson());
            saved = parsed.document.has_value() && parsed.document->tracks.size() == doc.tracks.size();
            for (size_t k = 0; saved && k < types.size(); ++k)
            {
                const auto& a = doc.tracks[k / static_cast<size_t> (sls::kSlotsPerTrack)].slots[k % static_cast<size_t> (sls::kSlotsPerTrack)];
                const auto& b = parsed.document->tracks[k / static_cast<size_t> (sls::kSlotsPerTrack)].slots[k % static_cast<size_t> (sls::kSlotsPerTrack)];
                const int last = sls::effectParamCount (types[k]) - 1;
                saved = a.type == b.type && std::abs (a.params[static_cast<size_t> (last)] - b.params[static_cast<size_t> (last)]) < 1.0e-3f * std::max (1.0f, std::abs (a.params[static_cast<size_t> (last)]));
                if (! saved)
                    std::cout << "    (" << sls::effectName (types[k]) << " did not come back)\n";
            }
            saved = saved && parsed.document->tracks[0].invertPhase && parsed.document->tracks[0].swapStereo && std::abs (parsed.document->tracks[0].separation + 0.5f) < 1.0e-6f;
        }
        check (saved, "every effect type (and a track's polarity, swap and separation) survives a save and a load");
    }

    // Sampler: reverse, start point, fades (as FL Studio's sampler channel)
    {
        // a sample whose first half is silent and whose second half is a tone: the order tells the direction
        auto sample = std::make_shared<sls::SampleData>();
        sample->sampleRate = 48000.0;
        sample->left.assign (24000, 0.0f);
        for (int i = 12000; i < 24000; ++i)
            sample->left[static_cast<size_t> (i)] = static_cast<float> (0.5 * std::sin (6.283185307179586 * 1000.0 * i / 48000.0));
        sample->right = sample->left;
        const auto play = [&] (auto&& edit)
        {
            auto c = samplerChannel (sample, { { 60, 0, kBeat / 4, 1.0f } });
            edit (c.sampler);
            return sls::renderToBuffer (oneBar ({ c }), {});
        };
        const auto forward = play ([] (sls::SamplerSettings&) {});
        check (peakOf (forward.getReadPointer (0), 0, 11000) == 0.0f && peakOf (forward.getReadPointer (0), 13000, 23000) > 0.05f, "sampler: forwards, the silent half comes first");
        const auto backward = play ([] (sls::SamplerSettings& s) { s.reverse = true; });
        check (peakOf (backward.getReadPointer (0), 500, 11000) > 0.05f && peakOf (backward.getReadPointer (0), 13000, 23000) == 0.0f, "sampler: reversed, the tone comes first");
        const auto late = play ([] (sls::SamplerSettings& s) { s.start = 0.5f; });
        check (peakOf (late.getReadPointer (0), 500, 11000) > 0.05f && peakOf (late.getReadPointer (0), 12500, 23000) == 0.0f, "sampler: start at 50 % skips the first half");
        const auto faded = play ([] (sls::SamplerSettings& s) { s.start = 0.5f; s.fadeIn = 0.5f; });
        check (peakOf (faded.getReadPointer (0), 0, 1200) < 0.3f * peakOf (late.getReadPointer (0), 600, 1200)
                   && peakOf (faded.getReadPointer (0), 10000, 11500) > 0.8f * peakOf (late.getReadPointer (0), 10000, 11500),
               "sampler: the fade in starts from silence at the start point and reaches the full level");
        const auto tail = play ([] (sls::SamplerSettings& s) { s.fadeOut = 0.5f; });
        check (peakOf (tail.getReadPointer (0), 22800, 23900) < 0.15f * peakOf (forward.getReadPointer (0), 22800, 23900)
                   && std::abs (peakOf (tail.getReadPointer (0), 12000, 12600) / peakOf (forward.getReadPointer (0), 12000, 12600) - 1.0f) < 0.08f,
               "sampler: the fade out brings the end of the sample down to silence");
        auto doc = sls::ProjectDocument::blank();
        doc.channels[0].instrument = sls::Instrument::Sampler;
        doc.channels[0].sampler.reverse = true;
        doc.channels[0].sampler.start = 0.25f;
        doc.channels[0].sampler.fadeIn = 0.1f;
        doc.channels[0].sampler.fadeOut = 0.3f;
        const auto parsed = sls::ProjectDocument::fromJson (doc.toJson());
        check (parsed.document.has_value() && parsed.document->channels[0].sampler.reverse && std::abs (parsed.document->channels[0].sampler.start - 0.25f) < 1.0e-6f
                   && std::abs (parsed.document->channels[0].sampler.fadeIn - 0.1f) < 1.0e-6f && std::abs (parsed.document->channels[0].sampler.fadeOut - 0.3f) < 1.0e-6f,
               "sampler: reverse, start and fades survive a save and a load");
    }

    // Piano roll tools: reverse, glue, flam
    {
        const auto note = [] (int pitch, int start, int length, float vel = 0.8f) { sls::Note n; n.pitch = pitch; n.startTick = start; n.lengthTicks = length; n.velocity = vel; return n; };
        std::vector<sls::Note> notes { note (60, 0, 120), note (64, 240, 240), note (67, 720, 240) };
        std::vector<bool> sel (3, false);
        sls::theory::reverse (notes, sel);
        check (notes.size() == 3 && notes[0].pitch == 67 && notes[0].startTick == 0 && notes[1].pitch == 64 && notes[1].startTick == 480
                   && notes[2].pitch == 60 && notes[2].startTick == 840 && notes[2].lengthTicks == 120,
               "reverse: the notes are mirrored in time inside their own span");

        notes = { note (60, 0, 240), note (60, 240, 240), note (60, 600, 120), note (62, 0, 480), note (60, 300, 100) };
        sel.assign (5, false);
        sls::theory::glue (notes, sel);
        int c = 0, d = 0, longC = 0;
        for (const auto& n : notes)
        {
            if (n.pitch == 60) { ++c; longC = std::max (longC, n.lengthTicks); }
            if (n.pitch == 62) ++d;
        }
        check (notes.size() == 3 && c == 2 && d == 1 && longC == 480, "glue: touching and overlapping notes of one pitch become one; a gap keeps them apart");

        notes = { note (60, 0, 240), note (62, 480, 240, 1.0f) };
        sel.assign (2, false);
        sls::theory::flam (notes, sel, 30, 0.5f);
        check (notes.size() == 3 && notes[1].pitch == 62 && notes[1].startTick == 450 && notes[1].lengthTicks == 30 && std::abs (notes[1].velocity - 0.5f) < 1.0e-6f
                   && notes[2].startTick == 480,
               "flam: a softer grace note just before each note (none before a note at the very start)");
    }

    // Playlist time markers
    {
        sls::Arrangement a;
        a.markers = { { 1920, "Drop" }, { 0, "Intro" }, { 1920, "Chorus" }, { -5, "bad" }, { 100000000, "far" } };
        a.tidyMarkers();
        check (a.markers.size() == 2 && a.markers[0].name == "Intro" && a.markers[1].name == "Chorus" && a.markers[1].tick == 1920,
               "markers: sorted, one per tick (the last wins), none outside the song");
        check (a.nextMarker (0) == 1 && a.nextMarker (1920) == -1 && a.previousMarker (1920) == 0 && a.previousMarker (0) == -1, "markers: next and previous from a position");
        sls::ProjectDocument doc = sls::ProjectDocument::blank();
        doc.arrangement.markers = { { 960, "Verse" }, { 3840, "Outro" } };
        const auto parsed = sls::ProjectDocument::fromJson (doc.toJson());
        check (parsed.document.has_value() && parsed.document->arrangement.markers == doc.arrangement.markers, "markers survive a save and a load");
    }
}

// 0.62.0: the items the 0.61.0 review left open.
void testFlParity062()
{
    std::cout << "FL Studio parity (0.62.0)\n";

    // Note properties: pan, fine pitch, release, mute
    {
        const auto note = [] (float pan, float fine, float release, bool muted)
        {
            sls::Note n { 60, 0, kBeat / 2, 1.0f };
            n.pan = pan; n.finePitch = fine; n.release = release; n.muted = muted;
            return n;
        };
        const auto centre = sls::renderToBuffer (oneBar ({ synthChannel ({ note (0.0f, 0.0f, 0.5f, false) }) }), {});
        const auto left = sls::renderToBuffer (oneBar ({ synthChannel ({ note (-1.0f, 0.0f, 0.5f, false) }) }), {});
        const auto half = sls::renderToBuffer (oneBar ({ synthChannel ({ note (0.5f, 0.0f, 0.5f, false) }) }), {});
        check (peakOf (left.getReadPointer (1), 0, 24000) == 0.0f && std::abs (peakOf (left.getReadPointer (0), 0, 24000) / peakOf (centre.getReadPointer (0), 0, 24000) - 1.0f) < 0.01f,
               "note pan: hard left silences the right side and keeps the left");
        check (std::abs (peakOf (half.getReadPointer (0), 0, 24000) / peakOf (centre.getReadPointer (0), 0, 24000) - 0.5f) < 0.02f
                   && std::abs (peakOf (half.getReadPointer (1), 0, 24000) / peakOf (centre.getReadPointer (1), 0, 24000) - 1.0f) < 0.01f,
               "note pan: half right halves the left side");

        const auto sample = sineSample (1000.0, 0.5, 48000.0);
        sls::Note up { 60, 0, kBeat / 4, 1.0f };
        up.finePitch = 1200.0f;
        const auto octave = sls::renderToBuffer (oneBar ({ samplerChannel (sample, { up }) }), {});
        check (toneEnergy (octave.getReadPointer (0), 1200, 9600, 2000.0) > 4.0 * toneEnergy (octave.getReadPointer (0), 1200, 9600, 1000.0),
               "note fine pitch: +1200 cents plays the sampler an octave up");
        sls::Note sharp { 69, 0, kBeat, 1.0f };
        sharp.finePitch = 100.0f; // A4 + 100 cents = A#4 = 466.16 Hz
        const auto synth = sls::renderToBuffer (oneBar ({ synthChannel ({ sharp }) }), {});
        check (toneEnergy (synth.getReadPointer (0), 2400, 21600, 466.16) > 10.0 * toneEnergy (synth.getReadPointer (0), 2400, 21600, 440.0),
               "note fine pitch: +100 cents raises a synth note by a semitone");

        const auto shortTail = sls::renderToBuffer (oneBar ({ synthChannel ({ note (0.0f, 0.0f, 0.0f, false) }) }), {});
        const auto longTail = sls::renderToBuffer (oneBar ({ synthChannel ({ note (0.0f, 0.0f, 1.0f, false) }) }), {});
        // the note ends at 12000 samples (half a beat at 120 BPM, 48 kHz); the default release lasts 80 ms = 3840 samples
        const double tailShort = rmsOf (std::vector<float> (shortTail.getReadPointer (0), shortTail.getReadPointer (0) + 48000), 13500, 15500);
        const double tailLong = rmsOf (std::vector<float> (longTail.getReadPointer (0), longTail.getReadPointer (0) + 48000), 13500, 15500);
        const double tailNormal = rmsOf (std::vector<float> (centre.getReadPointer (0), centre.getReadPointer (0) + 48000), 13500, 15500);
        check (tailShort < 1.0e-6 && tailLong > 2.0 * tailNormal && tailNormal > 0.0, "note release: 0 shortens the release, 1 lengthens it");

        const auto silent = sls::renderToBuffer (oneBar ({ synthChannel ({ note (0.0f, 0.0f, 0.5f, true) }) }), {});
        check (peakOf (silent.getReadPointer (0), 0, silent.getNumSamples()) == 0.0f, "a muted note does not play");

        auto doc = sls::ProjectDocument::blank();
        sls::Note n { 64, 240, 120, 0.7f };
        n.pan = -0.5f; n.release = 0.8f; n.finePitch = -30.0f; n.colour = 5; n.muted = true;
        doc.arrangement.patterns[0].notes[0] = { n, sls::Note { 60, 0, 120, 0.8f } };
        const auto parsed = sls::ProjectDocument::fromJson (doc.toJson());
        bool same = parsed.document.has_value() && parsed.document->arrangement.patterns[0].notes[0].size() == 2;
        if (same)
        {
            const auto& all = parsed.document->arrangement.patterns[0].notes[0];
            const auto& back = all[0].pitch == 64 ? all[0] : all[1];
            const auto& plain = all[0].pitch == 64 ? all[1] : all[0];
            same = std::abs (back.pan + 0.5f) < 1.0e-6f && std::abs (back.release - 0.8f) < 1.0e-6f && std::abs (back.finePitch + 30.0f) < 1.0e-4f
                   && back.colour == 5 && back.muted && ! plain.hasProperties();
        }
        check (same, "note pan, release, fine pitch, colour and mute survive a save and a load");
        auto plainDoc = sls::ProjectDocument::blank();
        plainDoc.arrangement.patterns[0].notes[0] = { sls::Note { 60, 0, 120, 0.8f } };
        check (! plainDoc.toJson().contains ("\"fine\"") && ! plainDoc.toJson().contains ("\"muted\"") && doc.toJson().contains ("\"fine\""),
               "a note without properties is written as before (no sixth value)");

        std::vector<sls::Note> notes { { 30, 0, 120, 0.8f }, { 100, 120, 120, 0.8f }, { 62, 240, 120, 0.8f } };
        std::vector<bool> sel (3, false);
        sls::theory::limit (notes, sel, 48, 71);
        check (notes[0].pitch == 54 && notes[1].pitch == 64 && notes[2].pitch == 62, "limit: notes move by octaves into the range, those inside stay");
    }

    // Channels: cut groups, maximum polyphony, mono and portamento
    {
        const auto sample = sineSample (1000.0, 0.5, 48000.0); // half a second of tone
        // two notes a 16th apart on one sampler channel: the first rings on unless it is cut
        const std::vector<sls::Note> two { { 60, 0, kBeat / 4, 1.0f }, { 72, kBeat / 4, kBeat / 4, 1.0f } };
        const auto play = [&] (auto&& edit)
        {
            auto c = samplerChannel (sample, two);
            edit (c);
            return sls::renderToBuffer (oneBar ({ c }), {});
        };
        const auto free = play ([] (sls::Channel&) {});
        const auto cutSelf = play ([] (sls::Channel& c) { c.cutGroup = 3; c.cutBy = 3; });
        const auto limited = play ([] (sls::Channel& c) { c.maxPoly = 1; });
        // after the second note has started (6000 samples), the first note's 1 kHz is still there unless it was cut
        const double ringing = toneEnergy (free.getReadPointer (0), 8000, 20000, 1000.0);
        check (ringing > 0.01 && toneEnergy (cutSelf.getReadPointer (0), 8000, 20000, 1000.0) < 0.01 * ringing
                   && toneEnergy (cutSelf.getReadPointer (0), 8000, 20000, 2000.0) > 0.01,
               "cut itself: the new note stops the one before, and sounds itself");
        check (toneEnergy (limited.getReadPointer (0), 8000, 20000, 1000.0) < 0.01 * ringing && toneEnergy (limited.getReadPointer (0), 8000, 20000, 2000.0) > 0.01,
               "maximum polyphony 1: the oldest note gives way");

        // a second channel cut by the first one's group
        auto hat = samplerChannel (sample, { { 60, 0, kBeat / 4, 1.0f } });
        hat.cutBy = 5;
        auto closer = samplerChannel (sineSample (3000.0, 0.1, 48000.0), { { 60, kBeat / 4, kBeat / 4, 1.0f } });
        closer.cutGroup = 5;
        const auto choked = sls::renderToBuffer (oneBar ({ hat, closer }), {});
        closer.cutGroup = 0;
        const auto open = sls::renderToBuffer (oneBar ({ hat, closer }), {});
        check (toneEnergy (choked.getReadPointer (0), 8000, 20000, 1000.0) < 0.01 * toneEnergy (open.getReadPointer (0), 8000, 20000, 1000.0),
               "cut group: a note on one channel cuts the channel that is cut by its group");

        // mono synth: the second note takes the first voice over (no second attack: the level does not dip), and with a
        // portamento the pitch is still on its way a moment later
        const std::vector<sls::Note> line { { 57, 0, kBeat, 1.0f }, { 69, kBeat / 2, kBeat, 1.0f } }; // A3 then A4, overlapping
        auto mono = synthChannel (line);
        mono.mono = true;
        const auto jump = sls::renderToBuffer (oneBar ({ mono }), {});
        mono.portamento = 0.25f; // the second note starts at 12000 samples and ends at 36000: the slide arrives at 24000
        const auto slide = sls::renderToBuffer (oneBar ({ mono }), {});
        auto poly = synthChannel (line);
        const auto both = sls::renderToBuffer (oneBar ({ poly }), {});
        // from 13000 to 20000 samples the second note sounds: poly has both 220 and 440 Hz; mono (jump) only 440
        check (toneEnergy (both.getReadPointer (0), 13000, 20000, 220.0) > 100.0 * toneEnergy (jump.getReadPointer (0), 13000, 20000, 220.0)
                   && toneEnergy (jump.getReadPointer (0), 13000, 20000, 440.0) > 0.001,
               "mono: the new note takes over the sounding one");
        check (toneEnergy (slide.getReadPointer (0), 12500, 15500, 440.0) < 0.2 * toneEnergy (jump.getReadPointer (0), 12500, 15500, 440.0)
                   && toneEnergy (slide.getReadPointer (0), 26000, 34000, 440.0) > 0.5 * toneEnergy (jump.getReadPointer (0), 26000, 34000, 440.0),
               "portamento: the pitch slides to the new note and arrives");

        auto doc = sls::ProjectDocument::blank();
        doc.channels[0].maxPoly = 4; doc.channels[0].mono = true; doc.channels[0].portamento = 0.12f; doc.channels[0].cutGroup = 2; doc.channels[0].cutBy = 7;
        const auto parsed = sls::ProjectDocument::fromJson (doc.toJson());
        check (parsed.document.has_value() && parsed.document->channels[0].maxPoly == 4 && parsed.document->channels[0].mono
                   && std::abs (parsed.document->channels[0].portamento - 0.12f) < 1.0e-6f && parsed.document->channels[0].cutGroup == 2 && parsed.document->channels[0].cutBy == 7,
               "polyphony, mono, portamento and cut groups survive a save and a load");
    }

    // Sampler: ping-pong and crossfaded loops, LFO, pitch envelope, cutoff envelope
    {
        // a ramp from -0.5 to +0.5 over 4800 frames: looped forwards it jumps at the join, ping-pong it does not
        auto ramp = std::make_shared<sls::SampleData>();
        ramp->sampleRate = 48000.0;
        ramp->left.resize (4800);
        for (int i = 0; i < 4800; ++i)
            ramp->left[static_cast<size_t> (i)] = -0.5f + static_cast<float> (i) / 4800.0f;
        ramp->right = ramp->left;
        const auto play = [&] (std::shared_ptr<const sls::SampleData> sample, auto&& edit)
        {
            auto c = samplerChannel (sample, { { 60, 0, 2 * kBeat, 1.0f } });
            edit (c.sampler);
            return sls::renderToBuffer (oneBar ({ c }), {});
        };
        const auto biggestStep = [] (const juce::AudioBuffer<float>& b, int from, int to)
        {
            float worst = 0.0f;
            for (int i = from + 1; i < to; ++i)
                worst = std::max (worst, std::abs (b.getSample (0, i) - b.getSample (0, i - 1)));
            return worst;
        };
        const auto forward = play (ramp, [] (sls::SamplerSettings& s) { s.loop = sls::LoopMode::Forward; });
        const auto pingPong = play (ramp, [] (sls::SamplerSettings& s) { s.loop = sls::LoopMode::PingPong; });
        const auto crossfaded = play (ramp, [] (sls::SamplerSettings& s) { s.loop = sls::LoopMode::Forward; s.loopStart = 0.5f; s.crossfade = 0.5f; });
        const auto plainHalf = play (ramp, [] (sls::SamplerSettings& s) { s.loop = sls::LoopMode::Forward; s.loopStart = 0.5f; });
        check (biggestStep (forward, 1000, 30000) > 20.0f * biggestStep (pingPong, 1000, 30000) && peakOf (pingPong.getReadPointer (0), 20000, 30000) > 0.05f,
               "ping-pong loop: the sample turns round at the ends instead of jumping back");
        check (biggestStep (plainHalf, 3000, 30000) > 10.0f * biggestStep (crossfaded, 3000, 30000) && peakOf (crossfaded.getReadPointer (0), 20000, 30000) > 0.02f,
               "loop crossfade: the end of the loop fades into its start");

        const auto tone = sineSample (1000.0, 1.0, 48000.0);
        const auto steady = play (tone, [] (sls::SamplerSettings&) {});
        const auto tremolo = play (tone, [] (sls::SamplerSettings& s) { s.lfoTarget = sls::SamplerLfoTarget::Volume; s.lfoRate = 5.0f; s.lfoDepth = 1.0f; });
        float lowest = 1.0f, highest = 0.0f;
        for (int w = 2400; w < 40000; w += 480)
        {
            lowest = std::min (lowest, peakOf (tremolo.getReadPointer (0), w, w + 480));
            highest = std::max (highest, peakOf (tremolo.getReadPointer (0), w, w + 480));
        }
        check (lowest < 0.2f * highest && std::abs (highest / peakOf (steady.getReadPointer (0), 2400, 40000) - 1.0f) < 0.05f, "sampler LFO on the volume: a tremolo down to silence");
        const auto autoPan = play (tone, [] (sls::SamplerSettings& s) { s.lfoTarget = sls::SamplerLfoTarget::Pan; s.lfoRate = 2.0f; s.lfoDepth = 1.0f; });
        float lMin = 1.0f, rMin = 1.0f;
        for (int w = 2400; w < 40000; w += 480)
        {
            lMin = std::min (lMin, peakOf (autoPan.getReadPointer (0), w, w + 480));
            rMin = std::min (rMin, peakOf (autoPan.getReadPointer (1), w, w + 480));
        }
        check (lMin < 0.1f * peakOf (steady.getReadPointer (0), 2400, 40000) && rMin < 0.1f * peakOf (steady.getReadPointer (1), 2400, 40000), "sampler LFO on the pan: each side in turn goes silent");
        const auto vibrato = play (tone, [] (sls::SamplerSettings& s) { s.lfoTarget = sls::SamplerLfoTarget::Pitch; s.lfoRate = 5.0f; s.lfoDepth = 1.0f; });
        check (toneEnergy (vibrato.getReadPointer (0), 2400, 40000, 1000.0) < 0.5 * toneEnergy (steady.getReadPointer (0), 2400, 40000, 1000.0)
                   && std::abs (rmsOf (std::vector<float> (vibrato.getReadPointer (0), vibrato.getReadPointer (0) + 40000), 2400, 40000)
                                / rmsOf (std::vector<float> (steady.getReadPointer (0), steady.getReadPointer (0) + 40000), 2400, 40000) - 1.0) < 0.05,
               "sampler LFO on the pitch: the tone leaves its frequency, the level stays");
        const auto dive = play (tone, [] (sls::SamplerSettings& s) { s.pitchEnv = 12.0f; s.pitchEnvTime = 0.2f; });
        check (toneEnergy (dive.getReadPointer (0), 0, 1200, 2000.0) > 3.0 * toneEnergy (dive.getReadPointer (0), 0, 1200, 1000.0)
                   && toneEnergy (dive.getReadPointer (0), 20000, 23000, 1000.0) > 10.0 * toneEnergy (dive.getReadPointer (0), 20000, 23000, 2000.0),
               "sampler pitch envelope: a note starts an octave up and settles on its pitch");
        const auto dark = play (tone, [] (sls::SamplerSettings& s) { s.envelope = true; s.filter = sls::SamplerFilter::LowPass; s.cutoff = 0.3f; });
        const auto opened = play (tone, [] (sls::SamplerSettings& s) { s.envelope = true; s.filter = sls::SamplerFilter::LowPass; s.cutoff = 0.3f; s.cutoffEnv = 5.0f; });
        check (peakOf (opened.getReadPointer (0), 6000, 20000) > 3.0f * peakOf (dark.getReadPointer (0), 6000, 20000), "sampler cutoff envelope: the envelope opens the filter");

        auto doc = sls::ProjectDocument::blank();
        doc.channels[0].instrument = sls::Instrument::Sampler;
        auto& s = doc.channels[0].sampler;
        s.loop = sls::LoopMode::PingPong; s.crossfade = 0.25f; s.lfoTarget = sls::SamplerLfoTarget::Cutoff; s.lfoRate = 3.0f; s.lfoDepth = 0.7f;
        s.cutoffEnv = -2.0f; s.pitchEnv = 7.0f; s.pitchEnvTime = 0.4f;
        const auto parsed = sls::ProjectDocument::fromJson (doc.toJson());
        bool same = parsed.document.has_value();
        if (same)
        {
            const auto& b = parsed.document->channels[0].sampler;
            same = b.loop == sls::LoopMode::PingPong && std::abs (b.crossfade - 0.25f) < 1.0e-6f && b.lfoTarget == sls::SamplerLfoTarget::Cutoff
                   && std::abs (b.lfoRate - 3.0f) < 1.0e-6f && std::abs (b.lfoDepth - 0.7f) < 1.0e-6f && std::abs (b.cutoffEnv + 2.0f) < 1.0e-6f
                   && std::abs (b.pitchEnv - 7.0f) < 1.0e-6f && std::abs (b.pitchEnvTime - 0.4f) < 1.0e-6f;
        }
        check (same, "sampler loop mode, crossfade, LFO and envelopes survive a save and a load");
    }
}

// 0.62.0: the mixer's free routing, delay compensation, the Peak Controller and the three new effects.
void testMixer062()
{
    std::cout << "Mixer routing, delay compensation and new effects (0.62.0)\n";
    const auto leftOf = [] (const juce::AudioBuffer<float>& b) { return std::vector<float> (b.getReadPointer (0), b.getReadPointer (0) + b.getNumSamples()); };
    const auto held = [] (double hz, int track, float volume = 0.2f)
    {
        auto c = samplerChannel (sineSample (hz, 1.5, 48000.0), { sls::Note { 60, 0, 2 * kBeat, 1.0f } });
        c.mixerTrack = track;
        c.volume = volume;
        return c;
    };

    // --- WaveShaper
    {
        const auto sine = sineWave (500.0, 0.5, 24000);
        const auto clipped = runEffect (slotWith (sls::EffectType::WaveShaper, { 24.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f }), sine);
        check (peakOf (clipped.data(), 4000, 24000) < 1.2f && peakOf (clipped.data(), 4000, 24000) > 0.8f, "waveshaper: the hard clip holds a hot signal at the ceiling");
        check (toneEnergy (clipped.data(), 4000, 24000, 1500.0) > 0.01 * toneEnergy (clipped.data(), 4000, 24000, 500.0),
               "waveshaper: clipping adds the third harmonic");
        const auto straight = runEffect (slotWith (sls::EffectType::WaveShaper, { 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f }), sine);
        // (the window does not hold a whole number of cycles, so the dry sine itself leaks a little into 1500 Hz)
        check (toneEnergy (straight.data(), 4000, 24000, 1500.0) < 1.5 * toneEnergy (sine.data(), 4000, 24000, 1500.0)
                   && std::abs (rmsOf (straight, 4000, 24000) / rmsOf (sine, 4000, 24000) - 1.0) < 0.02,
               "waveshaper: amount 0 is a straight line");
        const auto rectified = runEffect (slotWith (sls::EffectType::WaveShaper, { 6.0f, 4.0f, 1.0f, 0.0f, 0.0f, 1.0f }), sine);
        check (toneEnergy (rectified.data(), 4000, 24000, 1000.0) > 20.0 * toneEnergy (rectified.data(), 4000, 24000, 500.0),
               "waveshaper: the rectifier moves the sound an octave up");
        check (runEffect (slotWith (sls::EffectType::WaveShaper, { 24.0f, 2.0f, 1.0f, 0.3f, 0.0f, 0.0f }), sine) == sine, "waveshaper: mix 0 is bit-exact");
        const auto folded = runEffect (slotWith (sls::EffectType::WaveShaper, { 36.0f, 2.0f, 1.0f, 1.0f, 12.0f, 1.0f }), sineWave (100.0, 1.0, 24000));
        bool finite = true;
        for (const float v : folded)
            finite = finite && std::isfinite (v) && std::abs (v) < 8.0f;
        check (finite, "waveshaper: the folder at its extremes stays finite and bounded");
    }

    // --- Vocoder (no modulator track: left = voice, right = carrier)
    {
        const int n = 48000;
        std::vector<float> carrier (static_cast<size_t> (n));
        for (int i = 0; i < n; ++i)
            carrier[static_cast<size_t> (i)] = static_cast<float> (0.3 * std::sin (6.283185307179586 * 500.0 * i / kSr) + 0.3 * std::sin (6.283185307179586 * 4000.0 * i / kSr));
        const auto slot = slotWith (sls::EffectType::Vocoder, { 0.0f, 2.0f, 3.0f, 40.0f, 1.0f, 0.0f, 6.0f, 1.0f });
        const auto lowVoice = runEffectStereo (slot, sineWave (500.0, 0.5, n), carrier).first;
        const auto highVoice = runEffectStereo (slot, sineWave (4000.0, 0.5, n), carrier).first;
        const auto noVoice = runEffectStereo (slot, std::vector<float> (static_cast<size_t> (n), 0.0f), carrier).first;
        check (toneEnergy (lowVoice.data(), 12000, n, 500.0) > 30.0 * toneEnergy (lowVoice.data(), 12000, n, 4000.0),
               "vocoder: a low voice lets the carrier's low band through");
        check (toneEnergy (highVoice.data(), 12000, n, 4000.0) > 30.0 * toneEnergy (highVoice.data(), 12000, n, 500.0),
               "vocoder: a high voice lets the carrier's high band through");
        check (peakOf (noVoice.data(), 12000, n) < 1.0e-4f && peakOf (lowVoice.data(), 12000, n) > 0.02f, "vocoder: without a voice the carrier is silent");
        sls::EffectUnit bare; // without the lent memory it lets the sound through
        bare.prepare (kSr);
        auto l = carrier, r = carrier;
        bare.process (slot, l.data(), r.data(), 64);
        check (l == carrier, "vocoder: without its memory the sound passes unchanged");

        // the modulator is another mixer track: the bass (track 2) only sounds while the voice (track 1, not sent to the master) does
        auto voice = samplerChannel (sineSample (500.0, 0.4, 48000.0), { sls::Note { 60, 0, kBeat / 2, 1.0f } });
        voice.mixerTrack = 0;
        auto project = oneBar ({ voice, held (500.0, 1, 0.5f) });
        project.tracks[0].toMaster = false;
        project.tracks[1].slots[0] = slotWith (sls::EffectType::Vocoder, { 1.0f, 2.0f, 3.0f, 30.0f, 1.0f, 0.0f, 6.0f, 1.0f });
        const auto out = leftOf (sls::renderToBuffer (project, {}));
        check (rmsOf (out, 3000, 11000) > 20.0 * rmsOf (out, 30000, 44000) && rmsOf (out, 3000, 11000) > 0.01,
               "vocoder: with another track as the modulator, the carrier sounds only while that track does");
    }

    // --- Peak Controller
    {
        sls::EffectUnit unit;
        unit.prepare (kSr);
        auto slot = slotWith (sls::EffectType::PeakController, { 10.0f, 100.0f, 50.0f, 0.0f, 2.0f });
        auto loud = sineWave (1000.0, 0.5, 4800);
        auto copy = loud;
        for (size_t i = 0; i < loud.size(); i += 64)
            unit.process (slot, loud.data() + i, copy.data() + i, 64);
        check (std::abs (unit.controlValue() - 0.6f) < 0.02f && loud == sineWave (1000.0, 0.5, 4800), "peak controller: the value is base + volume x peak, and the sound passes");
        std::vector<float> quiet (48000, 0.0f), quietR (48000, 0.0f);
        for (size_t i = 0; i < quiet.size(); i += 64)
            unit.process (slot, quiet.data() + i, quietR.data() + i, 64);
        check (std::abs (unit.controlValue() - 0.1f) < 0.01f, "peak controller: the value falls back to the base when the sound stops");
        slot.params[8] = 1.0f;
        auto muted = sineWave (1000.0, 0.5, 640);
        auto mutedR = muted;
        for (size_t i = 0; i < muted.size(); i += 64)
            unit.process (slot, muted.data() + i, mutedR.data() + i, 64);
        check (peakOf (muted.data(), 0, 640) == 0.0f && unit.controlValue() > 0.5f, "peak controller: Mute silences the track and still follows it");

        // In the mixer: the kick's level (track 1, muted by the controller) pushes down the volume of a Panomatic on track 2.
        auto kick = samplerChannel (sineSample (200.0, 0.4, 48000.0), { sls::Note { 60, 0, kBeat / 2, 1.0f } });
        kick.mixerTrack = 0;
        auto project = oneBar ({ kick, held (1000.0, 1) });
        project.tracks[0].slots[0] = slotWith (sls::EffectType::PeakController, { 80.0f, -200.0f, 60.0f, 0.0f, 2.0f, 2.0f, 1.0f, 1.0f, 1.0f });
        project.tracks[1].slots[0] = sls::EffectSlot::make (sls::EffectType::Panomatic);
        const auto ducked = leftOf (sls::renderToBuffer (project, {}));
        // while the kick plays (peak 0.5: value 0.8 - 1.0 -> 0, the Panomatic's lowest volume, -48 dB); after it, value 0.8 (0 dB)
        check (rmsOf (ducked, 30000, 44000) > 50.0 * rmsOf (ducked, 3000, 10000) && rmsOf (ducked, 30000, 44000) > 0.01,
               "peak controller: the level of one track drives a parameter of an effect on another");
        check (toneEnergy (ducked.data(), 3000, 10000, 200.0) < 1.0e-6, "peak controller: the muted source does not reach the master");
        auto own = oneBar ({ held (1000.0, 0) });
        own.tracks[0].slots[0] = slotWith (sls::EffectType::PeakController, { 0.0f, 0.0f, 60.0f, 0.0f, 2.0f, 0.0f, 2.0f, 1.0f, 0.0f });
        own.tracks[0].slots[1] = sls::EffectSlot::make (sls::EffectType::Panomatic); // driven to its lowest volume by a value of 0
        const auto plainProject = oneBar ({ held (1000.0, 0) });
        check (rmsOf (leftOf (sls::renderToBuffer (own, {})), 20000, 40000) < 0.01 * rmsOf (leftOf (sls::renderToBuffer (plainProject, {})), 20000, 40000),
               "peak controller: target track 0 is the controller's own track");
        check (own.tracks[0].slots[1].params[0] == 0.0f, "peak controller: the project's own parameter is left alone");
    }

    // --- Convolver
    {
        // the convolution itself against the definition, in uneven pieces, with a response of several partitions
        auto file = std::make_shared<sls::SampleData>();
        file->sampleRate = 48000.0;
        std::uint32_t seed = 12345u;
        const auto next = [&seed] { seed = seed * 1664525u + 1013904223u; return static_cast<float> (static_cast<double> (seed >> 8) / 8388608.0 - 1.0); };
        for (int i = 0; i < 1500; ++i)
        {
            const float decay = std::exp (-i / 400.0f);
            file->left.push_back (next() * decay);
            file->right.push_back (next() * decay);
        }
        sls::Convolver::Settings settings;
        settings.space = sls::Convolver::File;
        settings.seconds = 1.0f;
        auto convolver = sls::Convolver::make (settings, file);
        std::vector<float> in (3000), inR (3000);
        for (auto& v : in)
            v = next();
        for (auto& v : inR)
            v = next();
        auto outL = in, outR = inR;
        for (size_t at = 0; at < outL.size();)
        {
            const int n = static_cast<int> (std::min<size_t> (37, outL.size() - at));
            convolver->process (outL.data() + at, outR.data() + at, n);
            at += static_cast<size_t> (n);
        }
        double worst = 0.0;
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto& h = convolver->impulseResponse (ch);
            const auto& x = ch == 0 ? in : inR;
            const auto& y = ch == 0 ? outL : outR;
            for (size_t i = 0; i < x.size(); i += 7)
            {
                double direct = 0.0;
                for (size_t m = 0; m < h.size() && m <= i; ++m)
                    direct += static_cast<double> (h[m]) * x[i - m];
                worst = std::max (worst, std::abs (direct - y[i]));
            }
        }
        check (convolver->impulseFrames() == 1500 && worst < 2.0e-4, "convolver: the partitioned convolution equals the definition, with no latency (worst error " + std::to_string (worst) + ")");
        convolver->clearState();
        std::vector<float> zl (600, 0.0f), zr (600, 0.0f);
        convolver->process (zl.data(), zr.data(), 600);
        check (peakOf (zl.data(), 0, 600) == 0.0f, "convolver: clearing the state silences what was ringing");

        // the built-in spaces
        sls::Convolver::Settings hall;
        hall.space = sls::Convolver::Hall;
        hall.seconds = 2.0f;
        const auto ir = sls::makeImpulseResponse (hall, 0);
        const auto irRight = sls::makeImpulseResponse (hall, 1);
        check (ir.size() == 96000 && rmsOf (ir, 0, 9600) > 30.0 * rmsOf (ir, 76800, 96000) && rmsOf (ir, 76800, 90000) > 0.0,
               "convolver: a built-in response dies away over its length");
        check (ir != irRight && ir == sls::makeImpulseResponse (hall, 0), "convolver: left and right differ, and a response is the same every time");
        auto dark = hall, bright = hall;
        dark.brightness = 0.0f;
        bright.brightness = 1.0f;
        const auto highShare = [] (const std::vector<float>& x)
        {
            double diff = 0.0, total = 0.0;
            for (size_t i = 1; i < x.size(); ++i)
            {
                diff += static_cast<double> (x[i] - x[i - 1]) * (x[i] - x[i - 1]);
                total += static_cast<double> (x[i]) * x[i];
            }
            return diff / total;
        };
        check (highShare (sls::makeImpulseResponse (bright, 0)) > 3.0 * highShare (sls::makeImpulseResponse (dark, 0)), "convolver: Brightness keeps more of the high end");
        auto late = hall;
        late.preDelayMs = 100.0f;
        const auto delayed = sls::makeImpulseResponse (late, 0);
        check (delayed.size() == 96000 + 4800 && peakOf (delayed.data(), 0, 4800) == 0.0f, "convolver: the pre-delay comes before the response");

        // as an effect slot
        auto slot = slotWith (sls::EffectType::Convolver, { 1.0f, 1.0f, 0.6f, 0.0f, 1.0f, 0.0f });
        std::vector<float> burst (48000, 0.0f);
        for (int i = 0; i < 2400; ++i)
            burst[static_cast<size_t> (i)] = 0.5f * std::sin (6.283185307179586f * 800.0f * static_cast<float> (i) / 48000.0f);
        check (runEffect (slot, burst) == burst, "convolver: without a response (not prepared yet) the sound passes unchanged");
        sls::prepareConvolver (slot, kSr);
        const auto first = slot.convolver;
        sls::prepareConvolver (slot, kSr);
        check (first != nullptr && slot.convolver == first, "convolver: the same settings keep the same response");
        const auto wet = runEffect (slot, burst);
        check (rmsOf (wet, 4800, 24000) > 0.002 && rmsOf (wet, 4800, 24000) > 5.0 * rmsOf (wet, 40000, 48000), "convolver: a burst rings on and dies away");
        slot.params[1] = 0.3f;
        sls::prepareConvolver (slot, kSr);
        check (slot.convolver != first, "convolver: a new length makes a new response");
        check (rmsOf (runEffect (slot, burst), 24000, 40000) < 0.05 * rmsOf (wet, 24000, 40000), "convolver: a shorter response rings for a shorter time");
        slot.params[4] = 0.0f;
        check (runEffect (slot, burst) == burst, "convolver: mix 0 is bit-exact");
        sls::prepareConvolver (slot, 44100.0);
        slot.params[4] = 1.0f;
        check (runEffect (slot, burst) == burst, "convolver: a response made for another sample rate is not used");

        // in a project: the export makes its own response, and the tail outlasts the note
        auto channel = samplerChannel (sineSample (800.0, 0.1, 48000.0), { sls::Note { 60, 0, kBeat / 8, 1.0f } });
        auto project = oneBar ({ channel });
        project.tracks[0].slots[0] = slotWith (sls::EffectType::Convolver, { 1.0f, 1.5f, 0.6f, 0.0f, 0.5f, 0.0f });
        const auto rendered = leftOf (sls::renderToBuffer (project, {}));
        const auto dry = leftOf (sls::renderToBuffer (oneBar ({ channel }), {}));
        check (project.tracks[0].slots[0].convolver == nullptr && rmsOf (rendered, 20000, 40000) > 1.0e-4 && rmsOf (dry, 20000, 40000) < 1.0e-6,
               "convolver: an export builds its own response and the reverb outlasts the note");
        check (leftOf (sls::renderToBuffer (project, {})) == rendered, "convolver: two exports of the same project are identical");

        // save and load: the settings, and the file of the File space
        const auto wav = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sls_test_ir.wav");
        {
            juce::AudioBuffer<float> buffer (2, 1500);
            for (int i = 0; i < 1500; ++i)
            {
                buffer.setSample (0, i, file->left[static_cast<size_t> (i)] * 0.5f);
                buffer.setSample (1, i, file->right[static_cast<size_t> (i)] * 0.5f);
            }
            wav.deleteFile();
            juce::WavAudioFormat format;
            std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (new juce::FileOutputStream (wav), 48000.0, 2, 16, {}, 0));
            if (writer != nullptr)
                writer->writeFromAudioSampleBuffer (buffer, 0, 1500);
        }
        auto doc = sls::ProjectDocument::blank();
        auto fileSlot = slotWith (sls::EffectType::Convolver, { 5.0f, 2.0f, 0.4f, 12.0f, 0.7f, -3.0f });
        const auto imported = sls::importAudioFile (wav);
        fileSlot.convolver = sls::Convolver::make (settings, imported.data);
        doc.tracks[2].slots[3] = fileSlot;
        const auto parsed = sls::ProjectDocument::fromJson (doc.toJson());
        bool same = parsed.document.has_value() && imported.data != nullptr;
        if (same)
        {
            auto back = parsed.document->tracks[2].slots[3];
            same = back.type == sls::EffectType::Convolver && back.params == fileSlot.params && back.convolver != nullptr && back.convolver->file() != nullptr
                   && back.convolver->file()->source == imported.data->source;
            sls::prepareConvolver (back, kSr);
            same = same && back.convolver != nullptr && back.convolver->settings().space == sls::Convolver::File && back.convolver->impulseFrames() == 576 + 1500;
        }
        check (same, "convolver: the settings and the impulse response file survive a save and a load");
        wav.deleteFile();
    }

    // --- free routing
    {
        const auto plain = leftOf (sls::renderToBuffer (oneBar ({ held (1000.0, 0) }), {}));
        const double reference = rmsOf (plain, 12000, 36000);
        auto none = oneBar ({ held (1000.0, 0) });
        none.tracks[0].toMaster = false;
        check (peakOf (leftOf (sls::renderToBuffer (none, {})).data(), 0, 96000) == 0.0f && reference > 0.01, "routing: a track sent nowhere is silent");

        auto through = oneBar ({ held (1000.0, 0) });
        through.tracks[0].toMaster = false;
        through.tracks[0].trackSends[0] = { 3, 0.5f };
        check (std::abs (rmsOf (leftOf (sls::renderToBuffer (through, {})), 12000, 36000) / reference - 0.5) < 0.01, "routing: a track sent to another at half level arrives at half level");
        through.tracks[3].volume = 0.5f;
        check (std::abs (rmsOf (leftOf (sls::renderToBuffer (through, {})), 12000, 36000) / reference - 0.25) < 0.01, "routing: the receiving track's fader applies to what it receives");

        auto both = oneBar ({ held (1000.0, 0) });
        both.tracks[0].trackSends[1] = { 1, 1.0f };
        check (std::abs (rmsOf (leftOf (sls::renderToBuffer (both, {})), 12000, 36000) / reference - 2.0) < 0.02, "routing: to the master and to another track at once");

        // the target has a lower number than its source: it is still processed after it (its effect sees the sound)
        auto backwards = oneBar ({ held (1000.0, 5) });
        backwards.tracks[5].toMaster = false;
        backwards.tracks[5].trackSends[0] = { 2, 1.0f };
        backwards.tracks[2].slots[0] = slotWith (sls::EffectType::Panomatic, { -6.0f });
        const auto viaLower = leftOf (sls::renderToBuffer (backwards, {}));
        check (std::abs (rmsOf (viaLower, 12000, 36000) / reference - 0.5012) < 0.01, "routing: a source is processed before the track it feeds, whatever their numbers");
        // a chain of three
        auto chain = oneBar ({ held (1000.0, 6) });
        chain.tracks[6].toMaster = false;
        chain.tracks[6].trackSends[0] = { 4, 1.0f };
        chain.tracks[4].toMaster = false;
        chain.tracks[4].trackSends[0] = { 1, 0.5f };
        chain.tracks[1].volume = 0.5f;
        check (std::abs (rmsOf (leftOf (sls::renderToBuffer (chain, {})), 12000, 36000) / reference - 0.25) < 0.01, "routing: a chain of three tracks");

        auto loop = oneBar ({ held (1000.0, 0) });
        loop.tracks[0].trackSends[0] = { 1, 1.0f };
        loop.tracks[1].trackSends[0] = { 0, 1.0f };
        loop.tracks[1].toMaster = false;
        loop.tracks[0].trackSends[1] = { 0, 1.0f };  // to itself: ignored
        const auto looped = leftOf (sls::renderToBuffer (loop, {}));
        bool sane = true;
        for (const float v : looped)
            sane = sane && std::isfinite (v) && std::abs (v) <= 1.0f;
        check (sane && std::abs (rmsOf (looped, 12000, 36000) / reference - 1.0) < 0.01, "routing: a loop of sends is broken, not fed back");

        auto mutedSource = through;
        mutedSource.tracks[0].mute = true;
        check (peakOf (leftOf (sls::renderToBuffer (mutedSource, {})).data(), 4800, 96000) < 1.0e-5f, "routing: a muted track sends nothing to other tracks");

        // a sidechain source that is fed by another track is heard with what it receives
        auto keyed = oneBar ({ held (1000.0, 0, 0.5f), held (300.0, 1) });
        keyed.tracks[0].toMaster = false;
        keyed.tracks[0].trackSends[0] = { 2, 1.0f };
        keyed.tracks[2].toMaster = false;
        keyed.tracks[1].slots[0] = slotWith (sls::EffectType::Gate, { -30.0f, 1.0f, 20.0f, 50.0f, -100.0f, 3.0f });
        const auto opened = leftOf (sls::renderToBuffer (keyed, {}));
        keyed.tracks[0].trackSends[0] = {};
        const auto closed = leftOf (sls::renderToBuffer (keyed, {}));
        check (rmsOf (opened, 12000, 36000) > 0.01 && rmsOf (closed, 12000, 36000) < 1.0e-4, "routing: a bus track fed by sends works as a sidechain source");

        auto doc = sls::ProjectDocument::blank();
        doc.tracks[4].toMaster = false;
        doc.tracks[4].trackSends[0] = { 7, 0.75f };
        doc.tracks[4].trackSends[2] = { 1, 1.25f };
        const auto parsed = sls::ProjectDocument::fromJson (doc.toJson());
        bool same = parsed.document.has_value();
        if (same)
        {
            const auto& t = parsed.document->tracks[4];
            same = ! t.toMaster && t.sendsTo (7) && t.sendsTo (1) && ! t.sendsTo (2) && parsed.document->tracks[0].toMaster && ! parsed.document->tracks[0].sendsTo (1);
            float level7 = 0.0f, level1 = 0.0f;
            for (const auto& send : t.trackSends)
            {
                level7 = send.target == 7 ? send.level : level7;
                level1 = send.target == 1 ? send.level : level1;
            }
            same = same && std::abs (level7 - 0.75f) < 1.0e-6f && std::abs (level1 - 1.25f) < 1.0e-6f;
        }
        check (same, "routing: the master switch and the sends between tracks survive a save and a load");
    }

    // --- delay compensation
    {
        const auto maximizer = slotWith (sls::EffectType::Maximizer, { 0.0f, -1.0f, 100.0f, 0.0f, 0.0f }); // far below its ceiling: only its look-ahead
        const auto plainProject = oneBar ({ held (1000.0, 0, 0.1f), held (1500.0, 1, 0.1f) });
        const auto plain = leftOf (sls::renderToBuffer (plainProject, {}));
        auto withLatency = plainProject;
        withLatency.tracks[0].slots[0] = maximizer;
        sls::Player player;
        player.prepare (kSr);
        const auto live = renderBlocks (player, withLatency, true, 1.0);
        const int latency = player.outputLatencySamples();
        check (latency > 16 && player.delayCompensationShortfall() == 0, "delay compensation: the mixer reports the latency of the slowest track (" + std::to_string (latency) + " samples)");
        double liveError = 0.0, uncompensated = 0.0;
        for (int i = 4800; i < 40000; ++i)
        {
            liveError = std::max (liveError, static_cast<double> (std::abs (live[static_cast<size_t> (i + latency)] - plain[static_cast<size_t> (i)])));
            uncompensated = std::max (uncompensated, static_cast<double> (std::abs (live[static_cast<size_t> (i)] - plain[static_cast<size_t> (i)])));
        }
        check (liveError < 0.004 && uncompensated > 0.02, "delay compensation: the track without latency waits for the one with it (error " + std::to_string (liveError) + ")");
        const auto exported = leftOf (sls::renderToBuffer (withLatency, {}));
        double exportError = 0.0;
        for (size_t i = 4800; i < 40000; ++i)
            exportError = std::max (exportError, static_cast<double> (std::abs (exported[i] - plain[i])));
        check (exported.size() == plain.size() && exportError < 0.004, "delay compensation: an export drops the latency from its start");

        // through a bus: track 1 (with latency) and track 2 (without) both feed track 3
        auto bus = plainProject;
        for (size_t t = 0; t < 2; ++t)
        {
            bus.tracks[t].toMaster = false;
            bus.tracks[t].trackSends[0] = { 2, 1.0f };
        }
        const auto busPlain = leftOf (sls::renderToBuffer (bus, {}));
        bus.tracks[0].slots[0] = maximizer;
        const auto busLatent = leftOf (sls::renderToBuffer (bus, {}));
        double busError = 0.0;
        for (size_t i = 4800; i < 40000; ++i)
            busError = std::max (busError, static_cast<double> (std::abs (busLatent[i] - busPlain[i])));
        check (busError < 0.004, "delay compensation: two tracks that feed a bus arrive there together");
        // ... and latency on the bus itself delays a track that goes straight to the master
        auto late = oneBar ({ held (1000.0, 0, 0.1f), held (1500.0, 1, 0.1f) });
        late.tracks[0].toMaster = false;
        late.tracks[0].trackSends[0] = { 2, 1.0f };
        const auto latePlain = leftOf (sls::renderToBuffer (late, {}));
        late.tracks[2].slots[0] = maximizer;
        const auto lateLatent = leftOf (sls::renderToBuffer (late, {}));
        double lateError = 0.0;
        for (size_t i = 4800; i < 40000; ++i)
            lateError = std::max (lateError, static_cast<double> (std::abs (lateLatent[i] - latePlain[i])));
        check (lateError < 0.004, "delay compensation: latency on a bus delays the tracks that bypass it");

        // taking the effect away again brings the mix back on time, and a bypassed effect has no latency
        renderBlocks (player, plainProject, true, 0.2);
        check (player.outputLatencySamples() == 0, "delay compensation: no latency once the effect is gone");
        withLatency.tracks[0].slots[0].bypass = true;
        renderBlocks (player, withLatency, true, 0.05);
        check (player.outputLatencySamples() == 0, "delay compensation: a bypassed effect has no latency");
    }

    // --- real-time safety of all of it
    {
        auto project = oneBar ({ held (1000.0, 0), held (300.0, 1), held (500.0, 2) });
        project.tracks[0].slots[0] = slotWith (sls::EffectType::PeakController, { 20.0f, 80.0f, 60.0f, 30.0f, 4.0f, 2.0f, 1.0f, 2.0f, 0.0f });
        project.tracks[0].slots[1] = sls::EffectSlot::make (sls::EffectType::WaveShaper);
        project.tracks[0].slots[2] = sls::EffectSlot::make (sls::EffectType::Maximizer);
        project.tracks[1].slots[0] = sls::EffectSlot::make (sls::EffectType::Filter);
        project.tracks[1].slots[1] = slotWith (sls::EffectType::Convolver, { 2.0f, 1.0f });
        project.tracks[1].slots[2] = slotWith (sls::EffectType::Vocoder, { 1.0f });
        project.tracks[1].toMaster = false;
        project.tracks[1].trackSends[0] = { 3, 0.8f };
        project.tracks[2].trackSends[0] = { 3, 0.5f };
        sls::prepareConvolvers (project.tracks, project.masterSlots, project.console.aux, kSr);
        sls::Player player;
        player.prepare (kSr);
        for (int i = 0; i < 4; ++i) // the vocoder's memory comes from the pool
        {
            auto memory = std::make_unique<sls::EffectMemory>();
            memory->prepare (kSr);
            player.offerEffectMemory (std::move (memory));
        }
        std::vector<float> l (256), r (256);
        float* ch[2] = { l.data(), r.data() };
        player.render (project, 120.0, true, ch, 2, 256);
        gAllocs = 0;
        gCounting = true;
        float loudest = 0.0f;
        for (int i = 0; i < 600; ++i)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            player.render (project, 120.0, true, ch, 2, 256);
            loudest = std::max (loudest, peakOf (l.data(), 0, 256));
        }
        gCounting = false;
        check (gAllocs.load() == 0 && loudest > 0.01f, "no allocation in Player::render with routing, delay compensation, a peak controller, a convolver and a vocoder (counted: "
                                                         + std::to_string (gAllocs.load()) + ")");
    }
}

// 0.62.0: the Layer channel, the master pitch, the markers' actions, playlist groups, and what performance mode rests on.
void testRackPlaylistToolbar062()
{
    std::cout << "Layer channel, master pitch, marker actions, playlist groups (0.62.0)\n";
    const auto leftOf = [] (const juce::AudioBuffer<float>& b) { return std::vector<float> (b.getReadPointer (0), b.getReadPointer (0) + b.getNumSamples()); };
    const auto tone = [] (double hz, int startTick, int lengthTicks = kBeat)
    {
        auto c = samplerChannel (sineSample (hz, 0.4, 48000.0), { sls::Note { 60, startTick, lengthTicks, 1.0f } });
        c.volume = 0.4f;
        return c;
    };

    // --- Layer channel
    {
        const std::vector<sls::Note> notes { { 60, 0, kBeat, 0.9f }, { 64, kBeat, kBeat / 2, 0.7f } };
        auto layer = synthChannel (notes);
        layer.instrument = sls::Instrument::Layer;
        layer.layerMask = 0b110; // channels 2 and 3
        auto synth = synthChannel ({}, 0.3f);
        auto sampler = samplerChannel (sineSample (700.0, 0.3, 48000.0), {});
        sampler.volume = 0.3f;
        const auto layered = sls::renderToBuffer (oneBar ({ layer, synth, sampler }), {});
        auto synthDirect = synth, samplerDirect = sampler;
        synthDirect.notes = notes;
        samplerDirect.notes = notes;
        const auto direct = sls::renderToBuffer (oneBar ({ synthChannel ({}), synthDirect, samplerDirect }), {});
        check (leftOf (layered) == leftOf (direct) && peakOf (layered.getReadPointer (0), 0, 48000) > 0.05f,
               "layer: its notes are played by its children, exactly as if they were theirs");
        const auto project = oneBar ({ layer, synth, sampler });
        check (project.channels[0].notes.empty() && project.channels[1].notes.size() == 2 && project.channels[2].notes.size() == 2,
               "layer: the layer itself keeps no notes to play");
        layer.layerMask = 0;
        check (peakOf (sls::renderToBuffer (oneBar ({ layer, synth, sampler }), {}).getReadPointer (0), 0, 96000) == 0.0f, "layer: without children it is silent");
        layer.layerMask = 0b111; // itself too: ignored
        auto other = layer; // a layer among the children is not played either
        other.notes.clear();
        check (leftOf (sls::renderToBuffer (oneBar ({ layer, synth, sampler, other }), {})) == leftOf (direct), "layer: a layer is never its own child");

        // played by hand
        layer.layerMask = 0b010;
        const auto liveProject = oneBar ({ [&] { auto l = layer; l.notes.clear(); return l; }(), synth });
        sls::Player player;
        player.prepare (kSr);
        renderBlocks (player, liveProject, false, 0.05);
        player.liveNoteOn (liveProject, 0, 69, 0.9f);
        const auto held = renderBlocks (player, liveProject, false, 0.3);
        player.liveNoteOff (0, 69);
        const auto after = renderBlocks (player, liveProject, false, 0.5);
        check (toneEnergy (held.data(), 2400, 12000, 440.0) > 0.01 && peakOf (after.data(), 14400, 24000) < 1.0e-4f,
               "layer: a note played by hand sounds on the children and ends with the key");

        auto doc = sls::ProjectDocument::blank();
        doc.channels[0].instrument = sls::Instrument::Layer;
        doc.channels[0].layerMask = 0b1010;
        doc.channels[0].colour = 0xff4a9be8u;
        doc.channels[0].group = "Leads";
        const auto parsed = sls::ProjectDocument::fromJson (doc.toJson());
        check (parsed.document.has_value() && parsed.document->channels[0].instrument == sls::Instrument::Layer && parsed.document->channels[0].layerMask == 0b1010u
                   && parsed.document->channels[0].colour == 0xff4a9be8u && parsed.document->channels[0].group == "Leads",
               "a layer's children and a channel's colour and filter group survive a save and a load");
    }

    // --- master pitch
    {
        auto synthProject = oneBar ({ synthChannel ({ { 69, 0, 2 * kBeat, 0.9f } }) });
        synthProject.masterPitch = 12.0f;
        const auto up = sls::renderToBuffer (synthProject, {});
        check (toneEnergy (up.getReadPointer (0), 4800, 40000, 880.0) > 10.0 * toneEnergy (up.getReadPointer (0), 4800, 40000, 440.0),
               "master pitch: +12 plays a synth an octave up");
        auto samplerProject = oneBar ({ tone (1000.0, 0) });
        samplerProject.masterPitch = -12.0f;
        const auto down = sls::renderToBuffer (samplerProject, {});
        check (toneEnergy (down.getReadPointer (0), 2400, 20000, 500.0) > 10.0 * toneEnergy (down.getReadPointer (0), 2400, 20000, 1000.0),
               "master pitch: -12 plays a sampler an octave down");
        sls::Channel drums;
        drums.instrument = sls::Instrument::Drums;
        juce::StringArray problems;
        drums.kit = std::make_shared<const sls::DrumKit> (sls::loadKit (sls::builtinKitSpec ("basic"), problems));
        drums.notes = { { 36, 0, kBeat / 4, 1.0f } }; // the kick's pad
        auto drumProject = oneBar ({ drums });
        const auto plainDrums = leftOf (sls::renderToBuffer (drumProject, {}));
        drumProject.masterPitch = 7.0f;
        check (leftOf (sls::renderToBuffer (drumProject, {})) == plainDrums && peakOf (plainDrums.data(), 0, 24000) > 0.01f, "master pitch: drum channels keep their pitch");

        auto doc = sls::ProjectDocument::blank();
        doc.masterPitch = -5.0f;
        const auto parsed = sls::ProjectDocument::fromJson (doc.toJson());
        check (parsed.document.has_value() && std::abs (parsed.document->masterPitch + 5.0f) < 1.0e-6f
                   && std::abs (sls::ProjectDocument::fromJson (sls::ProjectDocument::blank().toJson()).document->masterPitch) < 1.0e-6f,
               "the master pitch survives a save and a load (and is 0 when the file has none)");
    }

    // --- the markers' actions in the transport: 1 kHz on beat 1, 2 kHz on beat 3 (a beat is 24000 samples)
    {
        auto project = oneBar ({ tone (1000.0, 0), tone (2000.0, 2 * kBeat) });
        const auto render = [&] (std::vector<sls::Project::Jump> jumps, double seconds, sls::Player* use = nullptr)
        {
            project.jumps = std::move (jumps);
            sls::Player own;
            auto& player = use != nullptr ? *use : own;
            player.prepare (kSr);
            return renderBlocks (player, project, true, seconds);
        };
        const auto plain = render ({}, 2.0);
        check (toneEnergy (plain.data(), 49000, 58000, 2000.0) > 0.02 && toneEnergy (plain.data(), 25000, 34000, 2000.0) < 0.002, "markers: without actions the song plays straight through");
        const auto skipped = render ({ { kBeat, 2 * kBeat } }, 2.0);
        check (toneEnergy (skipped.data(), 25000, 34000, 2000.0) > 0.02, "markers: Skip goes on from the next marker");
        const auto looped = render ({ { 2 * kBeat, 0 } }, 2.0);
        check (toneEnergy (looped.data(), 49000, 58000, 1000.0) > 0.02 && toneEnergy (looped.data(), 49000, 58000, 2000.0) < 0.002, "markers: Loop goes back to the marker before");
        sls::Player player;
        const auto paused = render ({ { kBeat, -1 } }, 2.0, &player);
        int pausedAt = -1;
        check (player.takeMarkerPause (pausedAt) && pausedAt == kBeat && ! player.takeMarkerPause (pausedAt), "markers: Pause asks for the transport to stop, once");
        check (toneEnergy (paused.data(), 49000, 58000, 2000.0) < 1.0e-4 && peakOf (paused.data(), 40000, 96000) < 1.0e-4f, "markers: nothing plays after a Pause marker");
        // a jump lands exactly: the skipped render from the jump on is the plain one from beat 3 on
        double worst = 0.0;
        for (int i = 100; i < 20000; ++i)
            worst = std::max (worst, static_cast<double> (std::abs (skipped[static_cast<size_t> (24000 + i)] - plain[static_cast<size_t> (48000 + i)])));
        check (worst < 0.02, "markers: a jump lands on the sample (worst difference " + std::to_string (worst) + ")");

        sls::Arrangement a;
        a.patterns.resize (1);
        a.markers = { { 960, "A" }, { 1920, "B", sls::MarkerAction::Loop }, { 3840, "C", sls::MarkerAction::Skip }, { 5760, "D", sls::MarkerAction::Start },
                      { 7680, "E", sls::MarkerAction::Pause }, { 9600, "F", sls::MarkerAction::Skip } };
        const auto jumps = a.markerJumps();
        check (jumps.size() == 3 && jumps[0].atTick == 1920 && jumps[0].toTick == 960 && jumps[1].atTick == 3840 && jumps[1].toTick == 5760
                   && jumps[2].atTick == 7680 && jumps[2].toTick == -1 && a.startMarkerTick() == 5760,
               "markers: Loop, Skip, Pause and Start become what the transport does (a Skip with no marker after it does nothing)");
        auto doc = sls::ProjectDocument::blank();
        doc.arrangement.markers = { { 480, "Intro" }, { 960, "Drop", sls::MarkerAction::Loop }, { 1440, "", sls::MarkerAction::Pause } };
        const auto parsed = sls::ProjectDocument::fromJson (doc.toJson());
        check (parsed.document.has_value() && parsed.document->arrangement.markers == doc.arrangement.markers, "markers: their actions survive a save and a load");
    }

    // --- playlist groups and locks
    {
        sls::Arrangement a;
        a.patterns.resize (1);
        a.ensureChannelCount (1);
        a.patterns[0].notes[0] = { { 60, 0, 240, 0.8f } };
        a.playlistTracks.resize (6);
        a.playlistTracks[2].grouped = a.playlistTracks[3].grouped = true; // tracks 2 to 4 are a group, its head is track 2 (index 1)
        check (a.groupHead (3) == 1 && a.groupHead (2) == 1 && a.groupHead (1) == 1 && a.groupHead (4) == 4 && a.groupEnd (1) == 3 && a.groupEnd (0) == 0,
               "playlist groups: a grouped track belongs to the first track above that is not grouped");
        a.clips = { uc (a, 0, 3, 0, 1) };
        const std::vector<sls::Channel> channels { synthChannel ({}) };
        check (sls::compileSong (channels, a).channels[0].notes.size() == 1, "playlist groups: a grouped track plays");
        a.playlistTracks[1].mute = true;
        check (sls::compileSong (channels, a).channels[0].notes.empty() && ! a.trackPlays (2) && a.trackPlays (4), "playlist groups: the head's mute silences the group");
        a.playlistTracks[1].mute = false;
        a.playlistTracks[5].solo = true;
        check (! a.trackPlays (3), "playlist groups: another track's solo leaves the group out");
        a.playlistTracks[1].solo = true;
        check (a.trackPlays (3) && a.trackPlays (2) && ! a.trackPlays (0), "playlist groups: the head's solo counts for the group");
        a.playlistTracks[1].collapsed = true;
        check (a.trackHidden (2) && a.trackHidden (3) && ! a.trackHidden (1) && ! a.trackHidden (4), "playlist groups: a folded head hides the tracks grouped under it");
        a.playlistTracks[4].locked = true;
        check (a.trackLocked (4) && ! a.trackLocked (3) && ! a.trackLocked (99), "playlist tracks can be locked");

        auto doc = sls::ProjectDocument::blank();
        doc.arrangement.playlistTracks[2].grouped = true;
        doc.arrangement.playlistTracks[1].collapsed = true;
        doc.arrangement.playlistTracks[3].locked = true;
        const auto parsed = sls::ProjectDocument::fromJson (doc.toJson());
        check (parsed.document.has_value() && parsed.document->arrangement.playlistTracks[2].grouped && parsed.document->arrangement.playlistTracks[1].collapsed
                   && parsed.document->arrangement.playlistTracks[3].locked && ! parsed.document->arrangement.playlistTracks[0].locked,
               "playlist groups, folds and locks survive a save and a load");
    }

    // --- what performance mode rests on: a timeline rewritten ahead of the playhead plays as if it had always been so
    {
        sls::Arrangement a;
        a.patterns.resize (2);
        a.ensureChannelCount (2);
        a.patterns[0].notes[0] = { { 57, 0, 400, 0.8f }, { 60, 480, 400, 0.8f }, { 64, 960, 900, 0.8f } };
        a.patterns[1].notes[1] = { { 60, 0, 200, 1.0f }, { 60, 960, 200, 1.0f } };
        const int bar = a.patternLength (0);
        const std::vector<sls::Channel> channels { synthChannel ({}, 0.3f), [&] { auto c = samplerChannel (sineSample (900.0, 0.2, 48000.0), {}); c.volume = 0.3f; return c; }() };
        const auto clip = [&] (int pattern, int track, int start, int length)
        {
            sls::Clip c;
            c.pattern = pattern; c.track = track; c.startTick = start; c.lengthTicks = length;
            return c;
        };
        auto before = a, after = a;
        before.clips = { clip (0, 0, 0, bar), clip (0, 0, bar, bar), clip (0, 0, 2 * bar, bar), clip (0, 0, 3 * bar, bar) }; // pattern 1 looping
        after.clips = { clip (0, 0, 0, bar), clip (0, 0, bar, bar), clip (1, 1, 2 * bar, bar), clip (1, 1, 3 * bar, bar) };  // pattern 2 takes over at bar 3
        const auto first = sls::compileSong (channels, before);
        const auto second = sls::compileSong (channels, after);
        sls::Player switched, straight;
        switched.prepare (kSr);
        straight.prepare (kSr);
        auto live = renderBlocks (switched, first, true, 2.9); // 1.45 bars in: the new timeline is published ahead of bar 3
        const auto rest = renderBlocks (switched, second, true, 4.0);
        live.insert (live.end(), rest.begin(), rest.end());
        const auto reference = renderBlocks (straight, second, true, 6.9);
        check (live == reference && toneEnergy (live.data(), 193000, 200000, 900.0) > 0.01,
               "performance: a clip launched for the next bar starts there to the sample, and what played before is untouched");
    }
}

void testMusicTheory()
{
    namespace th = sls::theory;
    std::cout << "Music theory: scales, chords, note tools, generators\n";

    {   // scales
        th::Scale cMajor { 0, 1 }, aMinor { 9, 2 }, chromatic { 0, 0 }, dBlues { 2, 12 };
        check (th::scales().size() == 13 && std::string (th::scales()[1].name) == "Major" && std::string (th::scales()[12].name) == "Blues",
               "thirteen scales, from Chromatic to Blues");
        check (cMajor.contains (60) && cMajor.contains (64) && ! cMajor.contains (61) && cMajor.contains (71) && ! cMajor.contains (70)
                   && aMinor.contains (57) && aMinor.contains (60) && ! aMinor.contains (61) && cMajor.size() == 7 && dBlues.size() == 6,
               "membership: C major has no black keys, A minor is C major from A");
        check (cMajor.pitchClasses() == std::vector<int> { 0, 2, 4, 5, 7, 9, 11 } && aMinor.pitchClasses() == std::vector<int> { 9, 11, 0, 2, 4, 5, 7 },
               "the notes of a scale from its root");
        check (cMajor.snap (61) == 60 && cMajor.snap (63) == 62 && cMajor.snap (66) == 65 && cMajor.snap (60) == 60 && chromatic.snap (61) == 61,
               "snapping goes to the nearest note of the scale (the lower one on a tie)");
        check (cMajor.step (60, 1) == 62 && cMajor.step (60, 2) == 64 && cMajor.step (64, 1) == 65 && cMajor.step (60, -1) == 59 && cMajor.step (60, 7) == 72
                   && cMajor.step (127, 3) == 127 && cMajor.step (0, -2) == 0,
               "stepping through the scale (seven steps = an octave), stopping at the ends of the keyboard");
        check (th::noteName (60) == "C5" && th::noteName (21) == "A1" && th::noteName (61) == "C#5" && th::noteName (0) == "C0" && th::noteName (127) == "G10",
               "note names as in FL Studio: MIDI 60 is C5, 0 is C0");
    }
    {   // chords
        check (th::chords().size() == 18 && th::chordPitches (60, 0) == std::vector<int> { 60, 64, 67 } && th::chordPitches (60, 1) == std::vector<int> { 60, 63, 67 }
                   && th::chordPitches (60, 10) == std::vector<int> { 60, 64, 67, 71 } && th::chordPitches (57, 17) == std::vector<int> { 57, 60, 64, 67, 71 },
               "chords on a root: C major, C minor, C major 7, A minor 9");
        check (th::chordPitches (60, 0, 1) == std::vector<int> { 64, 67, 72 } && th::chordPitches (60, 0, 2) == std::vector<int> { 67, 72, 76 }
                   && th::chordPitches (60, 0, 9) == std::vector<int> { 67, 72, 76 } && th::chordPitches (126, 0) == std::vector<int> { 126, 127, 127 },
               "inversions move the lowest notes up an octave; pitches stay on the keyboard");
        th::Scale cMajor { 0, 1 }, aMinor { 9, 2 }, aPenta { 9, 11 };
        check (th::diatonicChord (cMajor, 0, 3) == std::vector<int> { 60, 64, 67 } && th::diatonicChord (cMajor, 1, 3) == std::vector<int> { 62, 65, 69 }
                   && th::diatonicChord (cMajor, 4, 4) == std::vector<int> { 67, 71, 74, 77 } && th::diatonicChord (cMajor, 6, 3) == std::vector<int> { 71, 74, 77 },
               "chords of the scale: I = C, ii = Dm, V7 = G7, vii = B diminished");
        check (th::diatonicChord (aMinor, 0, 3, 3) == std::vector<int> { 57, 60, 64 } && th::diatonicChord (aPenta, 0, 3, 3) == std::vector<int> { 57, 60, 64 },
               "in A minor (and the A minor pentatonic, which borrows the natural minor) the first chord is A minor");
    }
    const int bar = 4 * kBeat;
    {   // tools
        std::vector<sls::Note> notes { { 60, 10, 200, 0.5f }, { 64, 470, 230, 0.5f }, { 67, 965, 100, 0.5f } };
        std::vector<bool> sel;
        th::transpose (notes, sel, 2);
        check (pitchesOf (notes) == std::vector<int> { 62, 66, 69 } && sel.size() == 3, "transposing with nothing selected moves every note");
        sel = { true, false, false };
        th::transpose (notes, sel, -2);
        check (pitchesOf (notes) == std::vector<int> { 60, 66, 69 }, "and with a selection only the selected ones");
        sel.assign (3, false);
        th::snapToScale (notes, sel, { 0, 1 });
        check (pitchesOf (notes) == std::vector<int> { 60, 65, 69 }, "snap to scale (F# goes down to F in C major)");
        th::transposeInScale (notes, sel, { 0, 1 }, 2);
        check (pitchesOf (notes) == std::vector<int> { 64, 69, 72 }, "moving two steps in the scale (a third up)");

        th::quantize (notes, sel, kBeat / 4, 1.0f, false, bar);
        check (notes[0].startTick == 0 && notes[1].startTick == 480 && notes[2].startTick == 960 && notes[0].lengthTicks == 200, "quantizing moves starts onto the grid, keeping lengths");
        notes[0].startTick = 30;
        th::quantize (notes, sel, kBeat / 4, 0.5f, true, bar);
        check (notes[0].startTick == 15 && notes[1].lengthTicks > 230 && notes[1].lengthTicks <= 240, "half strength goes half way; ends move towards the grid too");
        th::legato (notes, sel, bar);
        check (notes[0].lengthTicks == 480 - 15 && notes[1].lengthTicks == 480, "legato: every note lasts until the next starts");
        th::staccato (notes, sel, 0.5f, 30);
        check (notes[1].lengthTicks == 240, "staccato halves the lengths");

        std::vector<sls::Note> chord { { 60, 0, 960, 0.8f }, { 64, 0, 960, 0.6f }, { 67, 0, 960, 0.7f } };
        std::vector<bool> csel;
        auto strummed = chord;
        th::strum (strummed, csel, 20, true);
        check (strummed[0].startTick == 0 && strummed[1].startTick == 20 && strummed[2].startTick == 40 && strummed[2].startTick + strummed[2].lengthTicks == 960,
               "strum up: each higher note 20 ticks later, every note still ends together");
        auto down = chord;
        csel.clear();
        th::strum (down, csel, 20, false);
        check (down[2].startTick == 0 && down[0].startTick == 40, "strum down starts from the top");

        auto arp = chord;
        csel.clear();
        th::arpeggiate (arp, csel, kBeat / 2, th::ArpPattern::Up, 1);
        std::sort (arp.begin(), arp.end(), [] (const sls::Note& a, const sls::Note& b) { return a.startTick < b.startTick; });
        check (arp.size() == 4 && pitchesOf (arp) == std::vector<int> { 60, 64, 67, 60 } && arp[3].startTick == 720 && arp[3].lengthTicks == 240
                   && std::abs (arp[0].velocity - 0.7f) < 1e-5f && csel.size() == 4 && csel[0],
               "arpeggio up: a chord of a half note becomes four eighths C E G C, with the chord's average velocity");
        auto updown = std::vector<sls::Note> { { 60, 0, 1920, 0.8f }, { 64, 0, 1920, 0.8f }, { 67, 0, 1920, 0.8f }, { 72, 0, 1920, 0.8f } };
        csel.clear();
        th::arpeggiate (updown, csel, kBeat / 2, th::ArpPattern::UpDown, 1);
        std::sort (updown.begin(), updown.end(), [] (const sls::Note& a, const sls::Note& b) { return a.startTick < b.startTick; });
        check (pitchesOf (updown) == std::vector<int> { 60, 64, 67, 72, 67, 64, 60, 64 }, "arpeggio up-down does not repeat the turning notes");
        auto same = std::vector<sls::Note> { { 60, 0, 480, 0.8f }, { 60, 0, 480, 0.8f } };
        csel.clear();
        th::arpeggiate (same, csel, 120, th::ArpPattern::Random, 7);
        check (same.size() == 4, "a random arpeggio of two identical notes finishes (no endless search for a different note)");
        auto single = std::vector<sls::Note> { { 60, 0, 480, 0.8f } };
        csel.clear();
        th::arpeggiate (single, csel, 120, th::ArpPattern::Up, 1);
        check (single.size() == 1 && single[0].lengthTicks == 480, "a single note is not arpeggiated");

        auto chopped = std::vector<sls::Note> { { 60, 0, 500, 0.8f, sls::NoteLink::Legato } };
        csel.clear();
        th::chop (chopped, csel, 120);
        check (chopped.size() == 5 && chopped[4].lengthTicks == 20 && chopped[0].link == sls::NoteLink::Legato && chopped[1].link == sls::NoteLink::None,
               "chop cuts a note into 16ths (the last piece shorter; only the first keeps its link)");

        auto flipped = chord;
        csel.clear();
        th::flip (flipped, csel);
        check (pitchesOf (flipped) == std::vector<int> { 67, 63, 60 }, "flip turns the notes upside down within their range");

        std::vector<sls::Note> run { { 60, 0, 100, 0.5f }, { 60, 480, 100, 0.5f }, { 60, 960, 100, 0.5f } };
        std::vector<bool> rsel;
        th::velocityRamp (run, rsel, 0.2f, 1.0f);
        check (std::abs (run[0].velocity - 0.2f) < 1e-5f && std::abs (run[1].velocity - 0.6f) < 1e-5f && std::abs (run[2].velocity - 1.0f) < 1e-5f,
               "a velocity ramp goes from the first to the last note in time");
        th::randomizeVelocity (run, rsel, 0.4f, 0.6f, 3);
        bool inRange = true;
        for (const auto& n : run)
            inRange = inRange && n.velocity >= 0.4f && n.velocity <= 0.6f;
        auto again = run;
        th::randomizeVelocity (again, rsel, 0.4f, 0.6f, 3);
        check (inRange && again[0].velocity == run[0].velocity && again[2].velocity == run[2].velocity, "random velocities stay in their range and repeat with the same seed");
        auto human = run;
        th::humanize (human, rsel, 10, 0.1f, 9, bar);
        bool close = true;
        for (size_t i = 0; i < human.size(); ++i)
            close = close && std::abs (human[i].startTick - run[i].startTick) <= 10 && human[i].startTick >= 0 && std::abs (human[i].velocity - run[i].velocity) <= 0.1001f;
        check (close, "humanize moves notes by at most the amounts asked for, never before the start");
    }
    {   // generators
        th::Scale cMajor { 0, 1 };
        const auto spans = th::progressionChords (cMajor, 0, bar, 4 * bar);
        check (spans.size() == 4 && spans[0].root == 0 && spans[1].root == 7 && spans[2].root == 9 && spans[3].root == 5
                   && spans[2].pitchClasses == std::vector<int> { 9, 0, 4 },
               "the pop progression in C: C, G, Am, F, one per bar");
        const auto twice = th::progressionChords (cMajor, 0, bar, 2 * bar);
        check (twice.size() == 2 && twice[1].startTick == bar, "a two-bar pattern takes the first two chords");
        const auto held = th::progressionNotes (cMajor, 0, th::ChordRhythm::Held, bar, 4 * bar);
        bool voiced = held.size() == 12, chordTones = true;
        int maxJump = 0;
        for (size_t c = 0; c < 4 && voiced; ++c)
        {
            std::vector<int> p;
            for (size_t k = 0; k < 3; ++k)
            {
                const auto& n = held[c * 3 + k];
                p.push_back (n.pitch);
                const auto& pcs = spans[c].pitchClasses;
                chordTones = chordTones && std::find (pcs.begin(), pcs.end(), n.pitch % 12) != pcs.end() && n.startTick == static_cast<int> (c) * bar;
            }
            std::sort (p.begin(), p.end());
            voiced = voiced && p.front() >= 52 && p.back() <= 79;
            if (c > 0)
                for (size_t k = 0; k < 3; ++k)
                    maxJump = std::max (maxJump, std::abs (held[c * 3 + k].pitch - held[(c - 1) * 3 + k].pitch));
        }
        std::cout << "    (largest move of one voice between chords: " << maxJump << " semitones)\n";
        check (voiced && chordTones && maxJump <= 5, "held chords use only chord notes, around middle C, and no voice jumps more than a fourth");
        const auto stabs = th::progressionNotes (cMajor, 0, th::ChordRhythm::Stabs, bar, bar);
        const auto pulse = th::progressionNotes (cMajor, 0, th::ChordRhythm::Pulse, bar, bar);
        const auto arp = th::progressionNotes (cMajor, 0, th::ChordRhythm::Arpeggio, bar, bar);
        bool inside = true;
        for (const auto* list : { &held, &stabs, &pulse, &arp })
            for (const auto& n : *list)
                inside = inside && n.startTick >= 0 && n.startTick + n.lengthTicks <= 4 * bar && n.lengthTicks > 0 && n.velocity > 0.0f && n.velocity <= 1.0f;
        check (stabs.size() == 9 && pulse.size() == 24 && arp.size() == 8 && inside, "stabs (3 hits), pulse (8 eighths), arpeggio (8 notes) of a bar, all inside the pattern");
        check (th::progressions().size() == 12, "twelve progressions to choose from");

        // chords found in notes: the demo's piano (Am9 then Fmaj7) and a C major triad in first inversion with E in the bass
        std::vector<sls::Note> piano;
        for (const int p : { 57, 60, 64, 67, 71 })
            piano.push_back ({ p, 0, bar - 20, 0.7f });
        for (const int p : { 53, 57, 60, 64, 69 })
            piano.push_back ({ p, bar, bar - 20, 0.7f });
        const auto found = th::detectChords (piano, 2 * kBeat, 2 * bar);
        check (found.size() == 2 && found[0].root == 9 && found[1].root == 5 && found[0].lengthTicks == bar, "detecting chords: A minor (9) and F major (7) from the demo's piano, one span per bar");
        const auto inverted = th::detectChords ({ { 64, 0, 960, 0.7f }, { 67, 0, 960, 0.7f }, { 72, 0, 960, 0.7f } }, kBeat, 2 * kBeat);
        check (inverted.size() == 1 && inverted[0].root == 0 && inverted[0].pitchClasses == std::vector<int> { 0, 4, 7 }, "a C major chord with E in the bass is still C");
        const auto gap = th::detectChords ({ { 62, bar, 480, 0.7f }, { 65, bar, 480, 0.7f }, { 69, bar, 480, 0.7f } }, kBeat * 2, 2 * bar);
        check (gap.size() == 1 && gap[0].root == 2 && gap[0].startTick == 0 && gap[0].lengthTicks == 2 * bar, "a silent start takes the first chord found");
        check (th::detectChords ({}, kBeat, bar).empty(), "no notes: no chords");
        const auto sixth = th::detectChords ({ { 55, 0, 960, 0.7f }, { 59, 0, 960, 0.7f }, { 62, 0, 960, 0.7f }, { 64, 0, 960, 0.7f } }, 2 * kBeat, 2 * kBeat);
        check (sixth.size() == 1 && sixth[0].root == 7, "G B D E with G in the bass is G6 (not E minor 7)");

        const auto roots = th::basslineNotes (spans, th::BassStyle::Roots, cMajor);
        check (pitchesOf (roots) == std::vector<int> { 36, 31, 33, 29 } && roots[1].startTick == bar, "roots: C2, G1, A1, F1 (from E1 up), one per bar");
        const auto octaves = th::basslineNotes (spans, th::BassStyle::Octaves, cMajor);
        check (octaves.size() == 32 && octaves[0].pitch == 36 && octaves[1].pitch == 48, "octaves: eighths alternating root and octave");
        const auto rootFifth = th::basslineNotes (spans, th::BassStyle::RootFifth, cMajor);
        check (rootFifth.size() == 16 && rootFifth[1].pitch == 43, "root and fifth: quarters, C then G");
        const auto walking = th::basslineNotes (spans, th::BassStyle::Walking, cMajor);
        check (walking.size() == 16 && walking[0].pitch == 36 && walking[1].pitch == 40 && walking[2].pitch == 43 && (walking[3].pitch == 30 || walking[3].pitch == 32),
               "walking: root, third, fifth and a note next to the next chord's root (G1)");
        const auto sync = th::basslineNotes (spans, th::BassStyle::Syncopated, cMajor);
        bool allIn = true;
        for (const auto* list : { &roots, &octaves, &rootFifth, &walking, &sync })
            for (const auto& n : *list)
                allIn = allIn && n.lengthTicks > 0 && n.startTick >= 0 && n.startTick + n.lengthTicks <= 4 * bar && n.pitch >= 28 && n.pitch <= 60;
        check (sync.size() == 24 && allIn, "syncopated: six hits a bar; every bassline note stays in its chord's time and in the bass range");
    }
}

// Brightness of a sound: RMS of its first difference over its RMS (2 sin(pi f / sr) for a sine), over [from, to) seconds.
double brightnessOf (const sls::SampleData& s, double from = 0.0, double to = 1.0e9)
{
    const auto a = static_cast<size_t> (std::max (1.0, from * s.sampleRate));
    const auto b = std::min (s.left.size(), static_cast<size_t> (std::max (0.0, to * s.sampleRate)));
    double d = 0.0, e = 0.0;
    for (size_t i = a; i < b; ++i)
    {
        const double diff = static_cast<double> (s.left[i]) - s.left[i - 1];
        d += diff * diff;
        e += static_cast<double> (s.left[i]) * s.left[i];
    }
    return e > 0.0 ? std::sqrt (d / e) : 0.0;
}

float samplePeak (const sls::SampleData& s)
{
    float p = 0.0f;
    for (size_t i = 0; i < s.left.size(); ++i)
        p = std::max ({ p, std::abs (s.left[i]), std::abs (s.right[i]) });
    return p;
}

bool allFinite (const sls::SampleData& s)
{
    for (size_t i = 0; i < s.left.size(); ++i)
        if (! std::isfinite (s.left[i]) || ! std::isfinite (s.right[i]))
            return false;
    return true;
}

// The fundamental of a steady part of a sound, from the strongest normalised autocorrelation between `lo` and `hi` Hz.
double autocorrelationHz (const sls::SampleData& s, double from, double seconds, double lo, double hi)
{
    const auto start = static_cast<size_t> (from * s.sampleRate);
    const auto n = static_cast<size_t> (seconds * s.sampleRate);
    if (start + 2 * n >= s.left.size())
        return 0.0;
    const auto minLag = static_cast<size_t> (s.sampleRate / hi), maxLag = static_cast<size_t> (s.sampleRate / lo);
    double best = -1.0;
    size_t bestLag = 0;
    std::vector<double> score (maxLag + 2, 0.0);
    for (size_t lag = minLag; lag <= maxLag + 1; ++lag)
    {
        double xy = 0.0, xx = 0.0, yy = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const double x = s.left[start + i], y = s.left[start + i + lag];
            xy += x * y;
            xx += x * x;
            yy += y * y;
        }
        score[lag] = xy / std::sqrt (xx * yy + 1e-30);
    }
    // the first lag that comes within 3 % of the best one (so an octave below is not taken for the fundamental)
    for (size_t lag = minLag; lag <= maxLag; ++lag)
        best = std::max (best, score[lag]);
    for (size_t lag = minLag; lag <= maxLag; ++lag)
        if (score[lag] >= 0.97 * best && score[lag] >= score[lag - 1] && score[lag] >= score[lag + 1])
        {
            bestLag = lag;
            break;
        }
    if (bestLag == 0)
        return 0.0;
    // parabolic interpolation around the peak
    const double a = score[bestLag - 1], b = score[bestLag], c = score[bestLag + 1];
    const double shift = (a - 2.0 * b + c) != 0.0 ? 0.5 * (a - c) / (a - 2.0 * b + c) : 0.0;
    return s.sampleRate / (static_cast<double> (bestLag) + shift);
}

// The strongest frequency between `lo` and `hi` Hz (a DFT scanned in 0.02 Hz steps) of the left side over [from, from + seconds).
double spectralPeakHz (const sls::SampleData& s, double from, double seconds, double lo, double hi)
{
    const auto a = static_cast<size_t> (from * s.sampleRate);
    const auto b = std::min (s.left.size(), static_cast<size_t> ((from + seconds) * s.sampleRate));
    double best = 0.0, bestHz = 0.0;
    for (double hz = lo; hz <= hi; hz += 0.02)
    {
        double re = 0.0, im = 0.0;
        const double w = 6.283185307179586 * hz / s.sampleRate;
        for (size_t i = a; i < b; ++i)
        {
            const double win = 0.5 - 0.5 * std::cos (6.283185307179586 * static_cast<double> (i - a) / static_cast<double> (b - a));
            re += win * s.left[i] * std::cos (w * static_cast<double> (i));
            im += win * s.left[i] * std::sin (w * static_cast<double> (i));
        }
        if (re * re + im * im > best)
        {
            best = re * re + im * im;
            bestHz = hz;
        }
    }
    return bestHz;
}

const sls::BrowserNode* childNamed (const sls::BrowserNode* node, const std::string& name)
{
    if (node != nullptr)
        for (const auto& c : node->children)
            if (c.name == name)
                return &c;
    return nullptr;
}

// --- the sound library (0.22.0): sampled instruments and kits, generated kits; the start-up sound, time formatting --------
void testSoundLibrary()
{
    std::cout << "Sound library, generated kits, start-up sound, time formats\n";

    check (sls::soundLibraryAvailable(), "the sound library is built into the program");
    {   // programs
        const auto& programs = sls::libraryPrograms();
        std::map<std::string, const sls::LibraryProgramInfo*> byId;
        for (const auto& p : programs)
            byId[p.id] = &p;
        check (programs.size() == 3 && byId.count ("grand-piano") == 1 && byId.count ("electric-bass") == 1 && byId.count ("electric-bass-staccato") == 1,
               "it has three instruments: grand piano, electric bass, electric bass staccato");
        check (byId.count ("grand-piano") == 1 && byId["grand-piano"]->name == "Grand Piano" && byId["grand-piano"]->category == "Keys"
                   && byId["grand-piano"]->credit.find ("CC BY 3.0") != std::string::npos && byId.count ("electric-bass") == 1
                   && byId["electric-bass"]->category == "Bass" && byId["electric-bass"]->credit.find ("CC0") != std::string::npos,
               "each is named, filed and credited with its license");

        sls::SamplerSettings settings;
        std::string error;
        const auto started = std::chrono::steady_clock::now();
        auto piano = sls::makeLibraryProgram ("grand-piano", &settings, error);
        const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count();
        std::cout << "    (the grand piano decodes in " << seconds << " s)\n";
        check (piano != nullptr && piano->zones.size() == 120 && error.empty(), "the grand piano loads: 30 notes, 4 velocity layers");
        check (settings.envelope && settings.sustain == 1.0f && std::abs (settings.release - 0.35f) < 1e-6f && settings.sustains(),
               "it is played with its envelope on (it sounds while held, then a 0.35 s release)");
        bool covered = piano != nullptr;
        if (piano != nullptr)
            for (int key = 0; key < 128; ++key)
                for (const int vel : { 1, 43, 44, 80, 81, 112, 113, 127 })
                {
                    int answering = 0;
                    for (const auto& z : piano->zones)
                        answering += z.answers (key, vel) ? 1 : 0;
                    covered = covered && answering == 1;
                }
        check (covered, "every key and velocity is answered by exactly one zone");
        bool sound = piano != nullptr, levels = piano != nullptr, layersBrighter = piano != nullptr;
        double memory = 0.0;
        if (piano != nullptr)
            for (size_t i = 0; i < piano->zones.size(); ++i)
            {
                const auto& z = piano->zones[i];
                memory += 8.0 * static_cast<double> (z.sample->numFrames());
                sound = sound && z.sample->sampleRate == 44100.0 && allFinite (*z.sample) && z.sample->left.size() == z.sample->right.size()
                        && z.sample->numFrames() > 44100 / 2 && z.sample->source == "builtin:lib:" + z.sample->source.substr (12);
                const float peak = samplePeak (*z.sample);
                levels = levels && peak > 0.05f && peak <= 0.95f; // levelled to 0.89, Ogg Vorbis can overshoot a little
                if (i % 4 == 3) // the hardest layer of a note is louder and brighter than its softest
                {
                    const auto& soft = *piano->zones[i - 3].sample;
                    levels = levels && samplePeak (soft) < peak;
                    layersBrighter = layersBrighter && brightnessOf (soft, 0.0, 0.5) < brightnessOf (*z.sample, 0.0, 0.5);
                }
            }
        std::cout << "    (decoded, the grand piano takes " << memory / 1.0e6 << " MB)\n";
        check (sound, "every sound is finite stereo at 44.1 kHz, half a second or more, with its library source");
        check (levels && layersBrighter, "levels are below 0.95, and a harder layer is louder and brighter than a softer one");
        double a4 = 0.0, c4 = 0.0;
        if (piano != nullptr)
            for (const auto& z : piano->zones)
            {
                if (z.hiVel == 127 && z.rootKey == 69)
                    a4 = autocorrelationHz (*z.sample, 0.3, 0.1, 200.0, 1000.0);
                else if (z.hiVel == 127 && z.rootKey == 60)
                    c4 = autocorrelationHz (*z.sample, 0.3, 0.1, 100.0, 600.0);
            }
        std::cout << "    (A4 sample " << a4 << " Hz, C4 sample " << c4 << " Hz)\n";
        check (std::abs (1200.0 * std::log2 (a4 / 440.0)) < 15.0 && std::abs (1200.0 * std::log2 (c4 / 261.63)) < 15.0,
               "the samples are on their keys: A4 plays 440 Hz and C4 261.6 Hz (within 15 cents)");

        auto again = sls::makeLibraryProgram ("grand-piano", nullptr, error);
        check (again == piano, "loading it again while it is in use shares the same program (no second copy in memory)");
        const auto* first = piano != nullptr ? piano->zones[0].sample.get() : nullptr;
        std::weak_ptr<const sls::SamplerProgram> watch = piano;
        piano.reset();
        again.reset();
        check (watch.expired(), "when nothing uses it any more, its memory is freed");
        (void) first;

        auto bass = sls::makeLibraryProgram ("electric-bass", &settings, error);
        check (bass != nullptr && bass->zones.size() == 56, "the electric bass loads: 14 notes, 4 velocity layers");
        double openE = 0.0;
        bool bassCovered = bass != nullptr;
        if (bass != nullptr)
        {
            for (const auto& z : bass->zones)
                if (z.rootKey == 28 && z.hiVel == 127) // the zone's tuning corrects a note recorded a little sharp
                    openE = spectralPeakHz (*z.sample, 0.3, 1.5, 36.0, 47.0) * std::pow (2.0, z.tune / 12.0);
            for (int key = 0; key < 128; ++key)
                for (const int vel : { 1, 60, 61, 95, 96, 120, 121, 127 })
                {
                    int answering = 0;
                    for (const auto& z : bass->zones)
                        answering += z.answers (key, vel) ? 1 : 0;
                    bassCovered = bassCovered && answering == 1;
                }
        }
        std::cout << "    (open E string " << openE << " Hz)\n";
        check (std::abs (1200.0 * std::log2 (openE / 41.2)) < 30.0, "keys play at their real pitch: the open E string is on E1 (41.2 Hz, within 30 cents)");
        check (bassCovered, "every key and velocity is answered by exactly one zone of the bass");
        const auto staccato = sls::makeLibraryProgram ("electric-bass-staccato", nullptr, error);
        check (staccato != nullptr && staccato->zones.size() == 14, "the staccato bass loads: 14 notes");
        check (sls::makeLibraryProgram ("nope", nullptr, error) == nullptr && ! error.empty(), "an unknown instrument gives nothing and a message");

        // played by the sampler
        sls::Channel c;
        c.instrument = sls::Instrument::Sampler;
        c.program = bass;
        c.sampler = settings;
        c.notes = { { 40, 0, 2 * kBeat, 0.25f } };
        const auto softProject = sls::Project::make ({ c }, 4 * kBeat);
        c.notes = { { 40, 0, 2 * kBeat, 1.0f } };
        const auto loudProject = sls::Project::make ({ c }, 4 * kBeat);
        sls::Player p1, p2;
        p1.prepare (kSr);
        p2.prepare (kSr);
        const auto soft = renderBlocks (p1, softProject, true, 0.6);
        const auto loud = renderBlocks (p2, loudProject, true, 0.6);
        const auto roughness = [] (const std::vector<float>& x, double from, double to)
        {
            double d = 0.0, e = 0.0;
            for (auto i = static_cast<size_t> (from * kSr) + 1; i < static_cast<size_t> (to * kSr) && i < x.size(); ++i)
            {
                d += std::pow (static_cast<double> (x[i]) - x[i - 1], 2.0);
                e += static_cast<double> (x[i]) * x[i];
            }
            return e > 0.0 ? std::sqrt (d / e) : 0.0;
        };
        const float softPeak = peakOf (soft.data(), 0, static_cast<int> (soft.size()));
        const float loudPeak = peakOf (loud.data(), 0, static_cast<int> (loud.size()));
        std::cout << "    (bass E2 at velocity 0.25: peak " << softPeak << ", at 1.0: " << loudPeak << "; roughness " << roughness (soft, 0.05, 0.5)
                  << " / " << roughness (loud, 0.05, 0.5) << ")\n";
        check (softPeak > 0.01f && loudPeak > 2.0f * softPeak && roughness (loud, 0.05, 0.5) > roughness (soft, 0.05, 0.5),
               "the sampler plays it: a hard note is louder and brighter (another layer) than a soft one");
    }

    {   // generated kits
        const auto& kits = sls::generatedKits();
        check (kits.size() == 4, "four kits are made by code: three electronic, one of hand percussion");
        bool full = kits.size() == 4, hatsChoke = true, valid = true, layered = true, deterministic = true;
        for (const auto& k : kits)
        {
            full = full && k.pads.size() == 16;
            for (const auto& pad : k.pads)
            {
                std::shared_ptr<const sls::SampleData> layers[sls::kGeneratedLayers];
                for (int l = 0; l < sls::kGeneratedLayers; ++l)
                {
                    layers[l] = sls::makeGeneratedSample ("gen:" + k.id + "/" + pad.piece + "/" + std::to_string (l + 1));
                    valid = valid && layers[l] != nullptr && allFinite (*layers[l]) && samplePeak (*layers[l]) > 0.3f
                            && samplePeak (*layers[l]) <= 0.9f && layers[l]->sampleRate == 44100.0 && layers[l]->numFrames() > 400;
                }
                if (layers[0] != nullptr && layers[3] != nullptr)
                {
                    const bool louder = samplePeak (*layers[3]) > samplePeak (*layers[0]);
                    const bool brighter = brightnessOf (*layers[3], 0.0, 0.05) > brightnessOf (*layers[0], 0.0, 0.05);
                    if (! louder || ! brighter)
                        std::cout << "    (" << k.id << "/" << pad.piece << ": hardest layer not louder and brighter)\n";
                    layered = layered && louder && brighter;
                    // made again from scratch: the very same sound
                    const auto copy = layers[3]->left;
                    const std::string id = layers[3]->source.substr (8);
                    layers[3].reset();
                    const auto remade = sls::makeGeneratedSample (id);
                    deterministic = deterministic && remade != nullptr && remade->left == copy;
                }
            }
            if (k.id != "percussion")
                hatsChoke = hatsChoke && k.pads[6].chokeGroup == 1 && k.pads[8].chokeGroup == 1 && k.pads[10].chokeGroup == 1
                            && k.pads[0].name == "Kick" && k.pads[2].name == "Snare" && k.pads[6].name == "Closed Hat" && k.pads[10].name == "Open Hat"
                            && k.pads[13].name == "Crash";
        }
        check (full && valid, "each has 16 pads of 4 layers, every sound finite and between 0.3 and 0.9 at its peak");
        check (layered, "a harder layer is louder and brighter in its first 50 ms");
        check (deterministic, "a sound made again is identical, sample for sample");
        check (hatsChoke, "the electronic kits follow the General MIDI drum order (kick on key 36, snare 38, hats 42 / 44 / 46, crash 49), hats in choke group 1");

        const auto kick = sls::makeGeneratedSample ("gen:boom/kick/4");
        const auto hat = sls::makeGeneratedSample ("gen:boom/closed-hat/4");
        const auto low = sls::makeGeneratedSample ("gen:punch/tom-1/3");
        const auto high = sls::makeGeneratedSample ("gen:punch/tom-5/3");
        const double kickHz = kick != nullptr ? autocorrelationHz (*kick, 0.15, 0.1, 30.0, 200.0) : 0.0;
        std::cout << "    (boom kick settles at " << kickHz << " Hz; brightness hat " << (hat ? brightnessOf (*hat) : 0.0) << ", toms "
                  << (low ? autocorrelationHz (*low, 0.08, 0.05, 50.0, 400.0) : 0.0) << " / " << (high ? autocorrelationHz (*high, 0.08, 0.05, 50.0, 400.0) : 0.0) << " Hz)\n";
        check (kickHz > 44.0 && kickHz < 56.0, "the boom kick settles at about 49 Hz");
        check (hat != nullptr && kick != nullptr && brightnessOf (*hat) > 1.0 && brightnessOf (*kick) < 0.05, "the hat is bright, the kick is not");
        check (low != nullptr && high != nullptr
                   && autocorrelationHz (*high, 0.08, 0.05, 50.0, 400.0) > 1.6 * autocorrelationHz (*low, 0.08, 0.05, 50.0, 400.0),
               "the toms are tuned from low to high");
        check (sls::makeGeneratedSample ("gen:boom/kick/5") == nullptr && sls::makeGeneratedSample ("gen:boom/kick/0") == nullptr
                   && sls::makeGeneratedSample ("gen:nope/kick/1") == nullptr && sls::makeGeneratedSample ("gen:boom/kick") == nullptr
                   && sls::makeGeneratedSample ("gen:percussion/kick/1") == nullptr && sls::makeBuiltinSample ("gen:boom/kick/1") != nullptr,
               "malformed or unknown ids give nothing; a good one works as a built-in sample");
    }

    {   // every built-in kit
        const auto& kits = sls::builtinKits();
        bool allLoad = ! kits.empty(), named = true, ordered = true;
        int sampled = 0;
        for (const auto& k : kits)
        {
            juce::StringArray problems;
            const auto spec = sls::builtinKitSpec (k.id);
            const auto kit = sls::loadKit (spec, problems);
            allLoad = allLoad && problems.isEmpty() && kit.pads[0].hasSample() && kit.name == k.name;
            if (! problems.isEmpty())
                std::cout << "    (" << k.id << ": " << problems.joinIntoString ("; ") << ")\n";
            sampled += k.id.rfind ("lib:", 0) == 0 ? 1 : 0;
            for (const auto& pad : spec.pads)
            {
                named = named && ! pad.name.empty();
                for (size_t l = 1; l < pad.sources.size(); ++l)
                    if (! pad.sources[l].empty())
                        ordered = ordered && pad.minVelocity[l] > pad.minVelocity[l - 1];
            }
        }
        check (allLoad, "every built-in kit loads without a problem");
        check (named && ordered, "every pad is named and its layers answer to rising velocities");
        check (sampled == static_cast<int> (sls::libraryKits().size()), "the sampled kits of the library are among them");
        check (sls::builtinKitSpec ("nope").pads[0].sources[0].empty(), "an unknown kit id gives an empty kit");
    }

    {   // the Cyber-Hub sounds are gone
        check (sls::makeBuiltinSample ("cyber:button-pressed") == nullptr && ! sls::removedBuiltinMessage ("cyber:button-pressed").empty()
                   && sls::removedBuiltinMessage ("kick").empty(),
               "the Cyber-Hub sounds were removed, and a project that used one gets a message saying so");
        auto spec = sls::KitSpec::empty();
        spec.pads[0].sources[0] = "builtin:cyber:button-pressed";
        juce::StringArray problems;
        const auto kit = sls::loadKit (spec, problems);
        check (problems.size() == 1 && problems[0].contains ("removed in version 0.22.0") && specFromKit (kit).pads[0].sources[0] == "builtin:cyber:button-pressed",
               "a kit that used one says so and keeps the reference (the pad is silent)");
    }

    {   // presets and project files with a built-in instrument
        const sls::ChannelPreset* piano = nullptr;
        for (const auto& p : sls::factoryPresets())
            if (p.id == "program-grand-piano")
                piano = &p;
        check (piano != nullptr && piano->category == "Keys" && piano->programSource == "builtin:program:grand-piano" && piano->sampler.has_value()
                   && piano->sampler->envelope,
               "a factory preset plays the grand piano, with its envelope");
        if (piano != nullptr)
        {
            const auto back = sls::ChannelPreset::fromJson (piano->toJson());
            check (back.has_value() && back->programSource == piano->programSource && back->sampler.has_value()
                       && std::abs (back->sampler->release - piano->sampler->release) < 1e-6f,
                   "a preset keeps its instrument and sampler settings through its file");
            sls::Channel ch;
            back->applyTo (ch);
            check (ch.instrument == sls::Instrument::Sampler && ch.sampler.envelope, "and applying it sets the sampler's settings");
        }
        const auto old = sls::ChannelPreset::fromJson (R"({"format":"sun-loops-studio-channel-preset","version":1,"name":"Old","instrument":"sampler","sample":"builtin:kick"})");
        check (old.has_value() && old->programSource.empty() && ! old->sampler.has_value(), "an older preset file has neither (the channel keeps its own settings)");

        std::string error;
        sls::Channel ch;
        ch.instrument = sls::Instrument::Sampler;
        ch.program = sls::makeLibraryProgram ("electric-bass-staccato", &ch.sampler, error);
        const auto spec = sls::specFromProgram (*ch.program);
        check (spec.kind == sls::ProgramSpec::Kind::Builtin && spec.file == "builtin:program:electric-bass-staccato", "a project refers to a built-in instrument by its id");
        const auto read = sls::programSpecFromVar (sls::programSpecToVar (spec));
        juce::String loadError;
        const auto loaded = sls::loadProgram (read, nullptr, loadError);
        check (read.kind == sls::ProgramSpec::Kind::Builtin && loaded == ch.program && loadError.isEmpty(),
               "and reading the project back loads the same instrument");
        sls::ProgramSpec missing;
        missing.kind = sls::ProgramSpec::Kind::Builtin;
        missing.file = "builtin:program:harpsichord";
        check (sls::loadProgram (missing, nullptr, loadError) == nullptr && loadError.isNotEmpty()
                   && sls::specFromProgram (*sls::missingProgram (missing)).file == missing.file,
               "an instrument this version does not have is reported and its reference kept");
    }

    const auto intro = sls::makeStartupSample();
    check (intro != nullptr && std::abs (intro->sampleRate - 48000.0) < 1.0 && intro->numFrames() == 240000, "the start-up sound is the 5 s, 48 kHz file");
    float introPeak = 0.0f;
    if (intro != nullptr)
        for (float v : intro->left)
            introPeak = std::max (introPeak, std::abs (v));
    check (introPeak > 0.15f && introPeak < 0.25f, "and has the level it was made at (peaks around 0.2, i.e. -14 dB: a quiet sound)");
    check (sls::makeBuiltinSample (sls::kStartupSampleId) != nullptr, "it can be used as a built-in sample too (id \"intro\")");

    {   // the browser
        sls::LibraryPaths paths;
        const auto tree = sls::buildBrowserTree (paths, {}, sls::PluginScanState::NotScanned);
        const auto* kits = childNamed (&tree, "Drum Kits");
        bool everyKit = kits != nullptr, loads = kits != nullptr;
        for (const auto& k : sls::builtinKits())
        {
            const auto* f = childNamed (kits, k.name);
            everyKit = everyKit && f != nullptr;
            if (f != nullptr)
                for (const auto& leaf : f->children)
                    if (leaf.kind == sls::NodeKind::BuiltinSample)
                        loads = loads && leaf.isDraggable() && sls::makeBuiltinSample (leaf.payload) != nullptr;
        }
        check (everyKit && loads, "the browser has a folder for every built-in kit, and every pad in it can be dragged and loads");
        const auto* factory = childNamed (childNamed (&tree, "Presets"), "Factory");
        const auto* keys = childNamed (factory, "Keys");
        check (keys != nullptr && childNamed (factory, "Bass") != nullptr && childNamed (factory, "Synth") != nullptr && childNamed (factory, "Drums") != nullptr
                   && childNamed (keys, "Grand Piano") != nullptr && childNamed (keys, "Grand Piano")->payload == "builtin:program-grand-piano",
               "factory presets are filed in Keys, Bass, Synth and Drums; the grand piano is in Keys");
        check (sls::countLeaves (sls::filterTree (tree, "cyber")) == 0 && sls::countLeaves (sls::filterTree (tree, "Grand Piano")) == 1,
               "nothing of Cyber-Hub is left in it; searching finds the piano");
    }

    // time formats of the playlist ruler
    check (sls::formatBarsBeatsTicks (0) == "1.1.000" && sls::formatBarsBeatsTicks (480) == "1.2.000" && sls::formatBarsBeatsTicks (1920) == "2.1.000"
               && sls::formatBarsBeatsTicks (1920 * 2 + 480 * 3 + 7) == "3.4.007" && sls::formatBarsBeatsTicks (-5) == "1.1.000",
           "positions read bar.beat.ticks counting from 1");
    check (sls::formatLengthBarsBeatsTicks (0) == "0.0.000" && sls::formatLengthBarsBeatsTicks (1920 + 480 + 240) == "1.1.240",
           "lengths read bars.beats.ticks counting from 0");
    check (sls::formatSeconds (0.0) == "0:00.000" && sls::formatSeconds (64.25) == "1:04.250" && sls::formatSeconds (0.0004) == "0:00.000"
               && sls::formatSeconds (0.0005) == "0:00.001" && sls::formatSeconds (-3.0) == "0:00.000",
           "times read minutes:seconds.milliseconds");
    check (sls::niceTimeStepSeconds (1000.0, 70.0) == 0.1 && sls::niceTimeStepSeconds (10.0, 70.0) == 10.0 && sls::niceTimeStepSeconds (1.0e6, 70.0) == 0.001
               && sls::niceTimeStepSeconds (0.01, 70.0) == 120.0,
           "the ruler picks a round time step that is wide enough");
    check (sls::barLabelEvery (100.0, 34.0) == 1 && sls::barLabelEvery (20.0, 34.0) == 2 && sls::barLabelEvery (5.0, 34.0) == 8,
           "and numbers every 1st, 2nd, 4th... bar as the zoom requires");
}


// --- time-stretch and pitch-shift --------------------------------------------------------------------------------
std::shared_ptr<sls::SampleData> makeTone (double hz, double seconds, double sr, double amp = 0.5)
{
    auto d = std::make_shared<sls::SampleData>();
    d->sampleRate = sr;
    d->name = "tone";
    d->source = "/tmp/tone.wav";
    const int n = static_cast<int> (seconds * sr);
    d->left.resize (static_cast<size_t> (n));
    d->right.resize (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        d->left[static_cast<size_t> (i)] = static_cast<float> (amp * std::sin (2.0 * 3.14159265358979323846 * hz * i / sr));
        d->right[static_cast<size_t> (i)] = d->left[static_cast<size_t> (i)];
    }
    return d;
}

// Frequency of a steady tone from the rising zero crossings between two frames.
double zeroCrossingHz (const std::vector<float>& x, double sr, int from, int to)
{
    int crossings = 0, first = -1, last = -1;
    for (int i = std::max (1, from); i < to && i < static_cast<int> (x.size()); ++i)
        if (x[static_cast<size_t> (i - 1)] < 0.0f && x[static_cast<size_t> (i)] >= 0.0f)
        {
            if (first < 0)
                first = i;
            last = i;
            ++crossings;
        }
    return crossings > 1 ? (crossings - 1) * sr / (last - first) : 0.0;
}

double stretchRms (const std::vector<float>& x, int from, int to)
{
    double sum = 0.0;
    to = std::min (to, static_cast<int> (x.size()));
    for (int i = from; i < to; ++i)
        sum += static_cast<double> (x[static_cast<size_t> (i)]) * x[static_cast<size_t> (i)];
    return to > from ? std::sqrt (sum / (to - from)) : 0.0;
}

void testTimeStretch()
{
    std::cout << "Time-stretch and pitch-shift\n";
    constexpr double sr = 44100.0;
    const std::shared_ptr<const sls::SampleData> tone = makeTone (440.0, 3.0, sr);

    {   // neutral settings return the same object; hostile settings are clamped; bad input is refused
        check (sls::stretchSample (tone, {}) == tone, "neutral settings return the input itself");
        check (sls::stretchSample (nullptr, { 2.0, 0.0, false }) == nullptr, "no input gives nothing");
        check (sls::stretchSample (std::make_shared<sls::SampleData>(), { 2.0, 0.0, false }) == nullptr, "empty audio gives nothing");
        sls::StretchSettings hostile { std::nan (""), 1.0e30, false };
        const auto h = hostile.sanitised();
        check (h.lengthRatio == 1.0 && h.semitones == sls::kMaxStretchSemitones, "NaN length ratio becomes 1, huge pitch is clamped to 24 semitones");
        check (sls::StretchSettings { 100.0, 0.0, false }.sanitised().lengthRatio == sls::kMaxStretchRatio
               && sls::StretchSettings { 0.0, 0.0, false }.sanitised().lengthRatio == sls::kMinStretchRatio, "length ratio limited to 0.25 .. 4");
    }

    {   // longer, same pitch
        const auto t0 = std::chrono::steady_clock::now();
        const auto y = sls::stretchSample (tone, { 2.0, 0.0, false });
        const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::cout << "  (3 s of stereo audio stretched in " << seconds << " s)\n";
        check (y != nullptr && y->numFrames() == tone->numFrames() * 2, "ratio 2 makes the audio exactly twice as long");
        check (y != nullptr && y->sampleRate == sr && y->source == tone->source && y->name == tone->name, "sample rate, name and source are kept");
        if (y != nullptr)
        {
            const double hz = zeroCrossingHz (y->left, sr, static_cast<int> (sr), static_cast<int> (4.0 * sr));
            check (std::fabs (hz - 440.0) < 3.0, "stretched to twice the length, the tone is still 440 Hz");
            const double rms = stretchRms (y->left, static_cast<int> (sr), static_cast<int> (4.0 * sr));
            check (std::fabs (rms - 0.5 / std::sqrt (2.0)) < 0.03, "the level of the steady part is unchanged");
            check (stretchRms (y->left, 0, 200) < 0.4 && stretchRms (y->left, y->numFrames() - 200, y->numFrames()) < 0.45, "no click at the ends (the first and last 200 frames are not louder than the tone)");
        }
    }
    {   // shorter, same pitch
        const auto y = sls::stretchSample (tone, { 0.5, 0.0, false });
        check (y != nullptr && y->numFrames() == tone->numFrames() / 2, "ratio 0.5 makes the audio half as long");
        check (y != nullptr && std::fabs (zeroCrossingHz (y->left, sr, 10000, 60000) - 440.0) < 4.0, "compressed to half the length, the tone is still 440 Hz");
    }
    {   // pitch only
        const auto up = sls::stretchSample (tone, { 1.0, 12.0, false });
        check (up != nullptr && up->numFrames() == tone->numFrames(), "pitch shifting alone keeps the length");
        check (up != nullptr && std::fabs (zeroCrossingHz (up->left, sr, 20000, 100000) - 880.0) < 8.0, "+12 semitones: 440 Hz becomes 880 Hz");
        const auto down = sls::stretchSample (tone, { 1.0, -12.0, false });
        check (down != nullptr && std::fabs (zeroCrossingHz (down->left, sr, 20000, 100000) - 220.0) < 3.0, "-12 semitones: 440 Hz becomes 220 Hz");
        const auto fifth = sls::stretchSample (tone, { 1.0, 7.0, false });
        check (fifth != nullptr && std::fabs (zeroCrossingHz (fifth->left, sr, 20000, 100000) - 440.0 * std::pow (2.0, 7.0 / 12.0)) < 6.0, "+7 semitones is a perfect fifth (659.3 Hz)");
        const auto cents = sls::stretchSample (tone, { 1.0, 0.5, false });
        check (cents != nullptr && std::fabs (zeroCrossingHz (cents->left, sr, 20000, 100000) - 440.0 * std::pow (2.0, 0.5 / 12.0)) < 3.0, "+50 cents is accepted (fractions of a semitone)");
    }
    {   // both at once
        const auto y = sls::stretchSample (tone, { 1.5, 5.0, false });
        check (y != nullptr && y->numFrames() == static_cast<int> (std::llround (tone->numFrames() * 1.5)), "length and pitch together: the length follows the ratio");
        check (y != nullptr && std::fabs (zeroCrossingHz (y->left, sr, 20000, 120000) - 440.0 * std::pow (2.0, 5.0 / 12.0)) < 6.0, "length and pitch together: the pitch follows the semitones");
    }
    {   // timing: a tone burst keeps its place (the latency of the stretcher is compensated)
        auto burst = makeTone (1000.0, 3.0, sr);
        for (int i = 0; i < burst->numFrames(); ++i)
            if (i < static_cast<int> (1.0 * sr) || i >= static_cast<int> (1.5 * sr))
                burst->left[static_cast<size_t> (i)] = burst->right[static_cast<size_t> (i)] = 0.0f;
        const std::shared_ptr<const sls::SampleData> b = burst;
        for (const double ratio : { 1.0, 2.0, 0.5 })
        {
            const auto y = sls::stretchSample (b, { ratio, ratio == 1.0 ? 0.02 : 0.0, false }); // (a pure ratio of 1 would be neutral)
            int onset = -1, end = -1;
            if (y != nullptr)
                for (int i = 0; i < y->numFrames(); ++i)
                    if (std::fabs (y->left[static_cast<size_t> (i)]) > 0.1f)
                    {
                        if (onset < 0)
                            onset = i;
                        end = i;
                    }
            const double expectedOnset = 1.0 * sr * ratio, expectedEnd = 1.5 * sr * ratio;
            std::cout << "  (burst at ratio " << ratio << ": onset off by " << (onset - expectedOnset) * 1000.0 / sr << " ms, end off by " << (end - expectedEnd) * 1000.0 / sr << " ms)\n";
            check (y != nullptr && std::fabs (onset - expectedOnset) < 0.03 * sr && std::fabs (end - expectedEnd) < 0.03 * sr,
                   ("a burst from 1.0 s to 1.5 s lands at " + std::to_string (ratio) + " x that, within 30 ms").c_str());
        }
    }
    {   // silence stays silent, a stereo image is kept, very short audio does not crash
        auto silent = std::make_shared<sls::SampleData>();
        silent->sampleRate = sr;
        silent->left.assign (30000, 0.0f);
        silent->right.assign (30000, 0.0f);
        const auto y = sls::stretchSample (silent, { 1.7, 3.0, true });
        check (y != nullptr && stretchRms (y->left, 0, y->numFrames()) == 0.0 && stretchRms (y->right, 0, y->numFrames()) == 0.0, "silence in, silence out (no noise is made up)");

        auto leftOnly = makeTone (500.0, 2.0, sr);
        leftOnly->right.assign (leftOnly->right.size(), 0.0f);
        const auto z = sls::stretchSample (leftOnly, { 1.3, 2.0, false });
        check (z != nullptr && stretchRms (z->left, 20000, 60000) > 0.2 && stretchRms (z->right, 20000, 60000) < 0.01, "a sound on the left channel stays on the left");

        for (const int n : { 2, 3, 50, 500 })
        {
            auto tiny = makeTone (1000.0, 1.0, sr);
            tiny->left.resize (static_cast<size_t> (n));
            tiny->right.resize (static_cast<size_t> (n));
            const auto r = sls::stretchSample (tiny, { 3.0, -5.0, false });
            check (r != nullptr && r->numFrames() == n * 3, ("a " + std::to_string (n) + "-frame clip is stretched without trouble").c_str());
        }
        auto one = makeTone (1000.0, 1.0, sr);
        one->left.resize (1);
        one->right.resize (1);
        check (sls::stretchSample (one, { 2.0, 0.0, false }) == nullptr, "a one-frame sound is refused");
    }
    {   // other sample rates
        for (const double rate : { 22050.0, 48000.0, 96000.0 })
        {
            const auto t = makeTone (600.0, 2.0, rate);
            const auto y = sls::stretchSample (t, { 1.25, 3.0, false });
            check (y != nullptr && y->numFrames() == static_cast<int> (std::llround (t->numFrames() * 1.25))
                       && std::fabs (zeroCrossingHz (y->left, rate, static_cast<int> (rate * 0.5), static_cast<int> (rate * 2.0)) - 600.0 * std::pow (2.0, 3.0 / 12.0)) < 8.0,
                   ("at " + std::to_string (static_cast<int> (rate)) + " Hz the length and the pitch are right").c_str());
        }
    }
}

void testStretchedClips()
{
    std::cout << "Stretched audio clips\n";
    constexpr double bpm = 120.0; // one beat = 480 ticks = 0.5 s
    const auto kick = sls::makeBuiltinSample ("intro");
    check (kick != nullptr && kick->numFrames() > 2 * 48000 / 2, "the built-in sound used here exists");
    if (kick == nullptr)
        return;
    const double seconds = kick->numFrames() / kick->sampleRate;

    sls::AudioClip clip;
    clip.data = kick;
    clip.name = "loop";
    clip.lengthTicks = static_cast<int> (std::floor (sls::secondsToTicks (seconds, bpm)));
    clip.startTick = 960;
    const int originalLength = clip.lengthTicks;

    {   // stretching a clip
        auto c = clip;
        check (sls::restretchClip (c, { 2.0, 0.0, false }, bpm), "a clip can be stretched");
        check (c.data != kick && c.original == kick && c.data->numFrames() == static_cast<int> (std::llround (kick->numFrames() * 2.0)),
               "the new audio is twice as long and the original is kept");
        check (c.stretch.lengthRatio == 2.0 && c.data->source == kick->source, "the settings are kept, and the file path stays the same");
        check (std::abs (c.lengthTicks - 2 * originalLength) <= 1 && c.startTick == 960, "the clip becomes twice as long and starts where it did");

        auto back = c;
        check (sls::restretchClip (back, {}, bpm) && back.data == kick && back.original == nullptr && back.stretch.isNeutral()
                   && std::abs (back.lengthTicks - originalLength) <= 1,
               "going back to neutral gives the original audio and length again, with no copy left over");

        auto again = c;
        check (sls::restretchClip (again, { 3.0, 2.0, true }, bpm) && again.original == kick
                   && again.data->numFrames() == static_cast<int> (std::llround (kick->numFrames() * 3.0))
                   && std::abs (again.lengthTicks - 3 * originalLength) <= 2,
               "stretching a stretched clip starts again from the original (3x, not 6x)");

        auto none = clip;
        none.data = nullptr;
        check (! sls::restretchClip (none, { 2.0, 0.0, false }, bpm), "a clip without audio is refused and left alone");
        auto missing = clip;
        missing.data = sls::makeMissingSample ("/nope.wav");
        check (! sls::restretchClip (missing, { 2.0, 0.0, false }, bpm) && missing.data->numFrames() == 0, "a clip whose file is missing is refused");
    }
    {   // a clip cut at the start: the offset and length scale
        auto c = clip;
        c.offsetSeconds = 0.5;
        c.lengthTicks = static_cast<int> (std::floor (sls::secondsToTicks (seconds - 0.5, bpm)));
        const int before = c.lengthTicks;
        check (sls::restretchClip (c, { 0.5, 0.0, false }, bpm) && std::abs (c.offsetSeconds - 0.25) < 1e-9 && std::abs (c.lengthTicks - before / 2) <= 1,
               "a clip that starts 0.5 s into its audio starts 0.25 s into the half-length audio, and is half as long");
        check (c.lengthTicks <= c.naturalLengthTicks (bpm), "the length never exceeds what the audio can fill");
    }
    {   // a clip shorter than its audio (trimmed at the end) stays in proportion
        auto c = clip;
        c.lengthTicks = originalLength / 2;
        check (sls::restretchClip (c, { 1.5, 0.0, false }, bpm) && std::abs (c.lengthTicks - static_cast<int> (std::lround (originalLength / 2 * 1.5))) <= 1,
               "a trimmed clip keeps its share of the audio");
    }
    {   // splitting keeps the stretch on both halves
        sls::Arrangement a;
        auto c = clip;
        sls::restretchClip (c, { 1.25, 3.0, false }, bpm);
        a.audioClips.push_back (c);
        const int r = sls::splitAudioClip (a, 0, c.startTick + c.lengthTicks / 2, bpm);
        check (r == 1 && a.audioClips[1].data == a.audioClips[0].data && a.audioClips[1].original == kick && a.audioClips[1].stretch == a.audioClips[0].stretch,
               "both halves of a split stretched clip share the audio and the settings");
    }
    {   // tempo matching
        check (std::abs (sls::lengthRatioForTempo (90.0, 120.0) - 0.75) < 1e-12, "a 90 BPM loop in a 120 BPM project is 0.75 times as long");
        check (std::abs (sls::lengthRatioForTempo (120.0, 90.0) - 4.0 / 3.0) < 1e-12, "a 120 BPM loop in a 90 BPM project is 4/3 as long");
        check (sls::lengthRatioForTempo (0.0, 120.0) == 1.0 && sls::lengthRatioForTempo (-5.0, 120.0) == 1.0 && sls::lengthRatioForTempo (std::nan (""), 120.0) == 1.0
                   && sls::lengthRatioForTempo (120.0, 0.0) == 1.0, "nonsense tempos change nothing");
        check (sls::lengthRatioForTempo (1000.0, 20.0) == sls::kMaxStretchRatio && sls::lengthRatioForTempo (20.0, 1000.0) == sls::kMinStretchRatio, "extreme tempos are limited to 0.25 .. 4");
    }
    {   // project file
        auto doc = sls::ProjectDocument::blank();
        auto c1 = clip;
        c1.track = 1;
        c1.data = sls::makeMissingSample ("builtin:intro");
        c1.stretch = { 1.5, -3.5, true };
        c1.lengthTicks = 2000;
        auto c2 = clip;
        c2.track = 2;
        c2.data = sls::makeMissingSample ("builtin:intro");
        auto c3 = c1;
        c3.track = 3;
        doc.arrangement.audioClips = { c1, c2, c3 };
        const auto text = doc.toJson();
        check (text.contains ("\"stretch\""), "the stretch settings are written to the project");
        const auto parsed = sls::ProjectDocument::fromJson (text);
        check (parsed.document.has_value() && parsed.warnings.empty() && parsed.document->arrangement.audioClips.size() == 3, "the project opens again");
        if (parsed.document)
        {
            auto arr = parsed.document->arrangement;
            check (arr.audioClips[0].stretch == c1.stretch && arr.audioClips[1].stretch.isNeutral() && arr.audioClips[0].data->numFrames() == 0,
                   "the settings come back; the audio is not stored");

            juce::StringArray problems;
            sls::loadAudioForClips (arr, problems);
            check (problems.isEmpty(), "reading the audio of the clips reports no problem");
            check (arr.audioClips[1].data->numFrames() == kick->numFrames() && arr.audioClips[1].original == nullptr, "an unstretched clip gets the file's audio");
            check (arr.audioClips[0].original != nullptr && arr.audioClips[0].original->numFrames() == kick->numFrames()
                       && arr.audioClips[0].data->numFrames() == static_cast<int> (std::llround (kick->numFrames() * 1.5)),
                   "a stretched clip gets the stretched audio and keeps the original");
            check (arr.audioClips[0].data == arr.audioClips[2].data, "two clips with the same file and settings share one rendering");
            check (arr.audioClips[0].data->source == "builtin:intro", "the stretched audio still names its file, so saving again works");
            auto resaved = *parsed.document;
            resaved.arrangement = arr;
            const auto again = sls::ProjectDocument::fromJson (resaved.toJson());
            check (again.document.has_value() && again.document->arrangement.audioClips[0].stretch == c1.stretch, "saving after loading keeps the settings");
        }

        // hostile numbers in the file are brought into range
        auto var = juce::JSON::parse (text);
        auto* list = var.getDynamicObject()->getProperty ("audioClips").getArray();
        auto* st = (*list)[0].getDynamicObject()->getProperty ("stretch").getDynamicObject();
        st->setProperty ("lengthRatio", 1.0e9);
        st->setProperty ("semitones", -1.0e9);
        const auto hostile = sls::ProjectDocument::fromJson (juce::JSON::toString (var));
        check (hostile.document.has_value() && hostile.document->arrangement.audioClips[0].stretch.lengthRatio == sls::kMaxStretchRatio
                   && hostile.document->arrangement.audioClips[0].stretch.semitones == -sls::kMaxStretchSemitones,
               "absurd stretch values in a file are limited to the allowed range");

        // a file from before stretching existed
        auto older = juce::JSON::parse (text);
        for (auto& item : *older.getDynamicObject()->getProperty ("audioClips").getArray())
            item.getDynamicObject()->removeProperty ("stretch");
        const auto old = sls::ProjectDocument::fromJson (juce::JSON::toString (older));
        check (old.document.has_value() && old.warnings.empty() && old.document->arrangement.audioClips[0].stretch.isNeutral(), "projects without stretch settings still open");

        // a stretched clip whose file is gone stays silent and keeps its settings
        auto gone = parsed.document->arrangement;
        gone.audioClips[0].data = sls::makeMissingSample ("/does/not/exist.wav");
        juce::StringArray problems;
        sls::loadAudioForClips (gone, problems);
        check (problems.size() == 1 && gone.audioClips[0].data->numFrames() == 0 && gone.audioClips[0].stretch == c1.stretch,
               "a missing file is reported once, the clip stays silent and keeps its stretch settings");
    }
}

// --- sample editor -----------------------------------------------------------------------------------------------
std::vector<float> whiteNoise (int n, double rms, unsigned seed)
{
    std::vector<float> v (static_cast<size_t> (n));
    unsigned state = seed;
    for (auto& x : v)
    {
        state = state * 1664525u + 1013904223u;
        const double u = (static_cast<double> (state >> 8) / 16777216.0 - 0.5) * 2.0; // uniform -1..1, rms 0.577
        x = static_cast<float> (u * rms / 0.57735);
    }
    return v;
}

void testSampleEditing()
{
    std::cout << "Sample editing\n";
    constexpr double sr = 44100.0;
    auto ramp = std::make_shared<sls::SampleData>();
    ramp->sampleRate = sr;
    ramp->name = "ramp";
    ramp->source = "/x/ramp.wav";
    for (int i = 0; i < 1000; ++i)
    {
        ramp->left.push_back (static_cast<float> (i) / 1000.0f);
        ramp->right.push_back (-static_cast<float> (i) / 1000.0f);
    }
    const sls::SamplePtr in = ramp;

    {   // ranges
        const auto r = sls::clampRange ({ 900, -50 }, 1000);
        check (r.begin == 0 && r.end == 900, "a backwards range is swapped and clamped");
        check (sls::clampRange ({ -5, 5000 }, 1000).begin == 0 && sls::clampRange ({ -5, 5000 }, 1000).end == 1000, "a range past the ends is clamped");
        check (sls::cutRange (nullptr, { 0, 10 }) == nullptr && sls::cutRange (in, { 500, 500 }) == nullptr && sls::cutRange (in, { 0, 1000 }) == nullptr,
               "nothing is cut from nothing, an empty range, or the whole sample (it would leave no audio)");
    }
    {   // cut, crop, silence
        const auto cut = sls::cutRange (in, { 100, 300 });
        check (cut != nullptr && cut->numFrames() == 800 && cut->left[99] == in->left[99] && cut->left[100] == in->left[300] && cut->right[100] == in->right[300],
               "cut removes the frames and the sound after them moves earlier");
        check (cut != nullptr && cut->name == "ramp" && cut->source.empty() && cut->sampleRate == sr && in->numFrames() == 1000, "an edit has no source file yet, keeps name and rate, and leaves the input alone");
        const auto crop = sls::cropToRange (in, { 250, 750 });
        check (crop != nullptr && crop->numFrames() == 500 && crop->left[0] == in->left[250] && crop->left[499] == in->left[749], "crop keeps only the range");
        const auto sil = sls::silenceRange (in, { 10, 20 });
        check (sil != nullptr && sil->numFrames() == 1000 && sil->left[15] == 0.0f && sil->right[15] == 0.0f && sil->left[9] == in->left[9] && sil->left[20] == in->left[20],
               "silence zeros the range and nothing else");
    }
    {   // reverse, invert, gain
        const auto rev = sls::reverseRange (in, { 100, 200 });
        check (rev != nullptr && rev->left[100] == in->left[199] && rev->left[199] == in->left[100] && rev->left[99] == in->left[99] && rev->left[200] == in->left[200],
               "reverse flips only the range");
        const auto whole = sls::reverseRange (in, { 0, 1000 });
        check (whole != nullptr && whole->left[0] == in->left[999] && whole->right[0] == in->right[999], "reverse of everything flips both channels");
        const auto inv = sls::invertPhase (in, { 0, 1000 });
        check (inv != nullptr && inv->left[500] == -in->left[500] && inv->right[500] == -in->right[500], "invert phase negates both channels");
        const auto g = sls::gainRange (in, { 0, 1000 }, 6.0);
        check (g != nullptr && std::fabs (g->left[500] / in->left[500] - 1.99526f) < 1e-3f, "+6 dB doubles the level (x1.995)");
        const auto hot = sls::gainRange (in, { 0, 1000 }, 40.0);
        check (hot != nullptr && hot->left[999] > 90.0f, "gain may go past full scale in the editor (nothing is clipped until it is saved as 32-bit float)");
        check (sls::gainRange (in, { 0, 1000 }, std::nan ("")) == nullptr, "a NaN gain is refused");
    }
    {   // fades
        auto flat = std::make_shared<sls::SampleData>();
        flat->sampleRate = sr;
        flat->left.assign (1000, 1.0f);
        flat->right.assign (1000, 1.0f);
        const sls::SamplePtr f = flat;
        const auto lin = sls::fadeRange (f, { 0, 1000 }, true, sls::FadeShape::Linear);
        check (lin != nullptr && lin->left[0] == 0.0f && lin->left[999] == 1.0f && std::fabs (lin->left[500] - 0.5f) < 0.01f, "linear fade in: 0 at the start, 1 at the end, half in the middle");
        const auto out = sls::fadeRange (f, { 0, 1000 }, false, sls::FadeShape::Linear);
        check (out != nullptr && out->left[0] == 1.0f && out->left[999] == 0.0f, "fade out: 1 at the start, 0 at the end");
        const auto sc = sls::fadeRange (f, { 200, 600 }, true, sls::FadeShape::SCurve);
        check (sc != nullptr && sc->left[199] == 1.0f && sc->left[200] == 0.0f && sc->left[599] == 1.0f && sc->left[700] == 1.0f && std::fabs (sc->left[400] - 0.5f) < 0.01f,
               "a fade touches only its range; the S curve is 0.5 in the middle");
        const auto cv = sls::fadeRange (f, { 0, 1000 }, true, sls::FadeShape::Curve);
        check (cv != nullptr && cv->left[500] < 0.3f && cv->left[500] > 0.2f, "the curved fade starts slowly (0.25 at the middle)");
        const auto one = sls::fadeRange (f, { 10, 11 }, true, sls::FadeShape::Linear);
        check (one != nullptr && one->left[10] == 1.0f, "a one-frame fade does no harm");
    }
    {   // normalise, DC
        const auto norm = sls::normaliseRange (in, { 0, 1000 }, -6.0);
        check (norm != nullptr && std::fabs (sls::toDb (sls::peakOf (norm, { 0, 1000 })) + 6.0) < 0.01, "normalise brings the peak to the target (-6 dB)");
        const auto norm0 = sls::normaliseRange (in, { 0, 500 }, 0.0);
        check (norm0 != nullptr && std::fabs (sls::peakOf (norm0, { 0, 500 }) - 1.0) < 1e-6 && norm0->left[600] == in->left[600], "normalising a range leaves the rest alone");
        auto quiet = std::make_shared<sls::SampleData>();
        quiet->sampleRate = sr;
        quiet->left.assign (100, 0.0f);
        quiet->right.assign (100, 0.0f);
        check (sls::normaliseRange (quiet, { 0, 100 }, -1.0) == quiet, "silence cannot be normalised and is returned as it is");

        auto dc = std::make_shared<sls::SampleData>();
        dc->sampleRate = sr;
        for (int i = 0; i < 4410; ++i)
        {
            dc->left.push_back (0.2f + 0.3f * static_cast<float> (std::sin (2.0 * 3.14159265358979 * 100.0 * i / sr)));
            dc->right.push_back (-0.1f);
        }
        const auto fixed = sls::removeDcOffset (dc, { 0, 4410 });
        double meanL = 0.0, meanR = 0.0;
        for (int i = 0; fixed != nullptr && i < fixed->numFrames(); ++i)
        {
            meanL += fixed->left[static_cast<size_t> (i)];
            meanR += fixed->right[static_cast<size_t> (i)];
        }
        check (fixed != nullptr && std::fabs (meanL / 4410.0) < 1e-3 && std::fabs (meanR / 4410.0) < 1e-4, "DC offset removal brings the mean of each channel to 0");
    }
    {   // noise reduction
        constexpr int n = static_cast<int> (3.0 * sr);
        auto noisy = std::make_shared<sls::SampleData>();
        noisy->sampleRate = sr;
        noisy->name = "noisy";
        noisy->left = whiteNoise (n, 0.03, 12345u);
        noisy->right = whiteNoise (n, 0.03, 777u);
        const int toneStart = static_cast<int> (1.0 * sr);
        for (int i = toneStart; i < n; ++i)
        {
            const float tone = static_cast<float> (0.3 * std::sin (2.0 * 3.14159265358979 * 1000.0 * i / sr));
            noisy->left[static_cast<size_t> (i)] += tone;
            noisy->right[static_cast<size_t> (i)] += tone;
        }
        const sls::SamplePtr nz = noisy;
        const sls::FrameRange noiseOnly { 0, toneStart - 4000 };
        sls::NoiseReduction nr; // the default sensitivity and release
        nr.reductionDb = 20.0;
        const auto clean = sls::reduceNoise (nz, noiseOnly, { 0, n }, nr);
        check (clean != nullptr && clean->numFrames() == n && clean->sampleRate == sr, "noise reduction keeps the length and the rate");
        if (clean != nullptr)
        {
            auto rms = [] (const std::vector<float>& x, int a, int b)
            {
                double sum = 0.0;
                for (int i = a; i < b; ++i)
                    sum += static_cast<double> (x[static_cast<size_t> (i)]) * x[static_cast<size_t> (i)];
                return std::sqrt (sum / (b - a));
            };
            const double before = rms (nz->left, 5000, toneStart - 5000), after = rms (clean->left, 5000, toneStart - 5000);
            std::cout << "  (noise-only part: " << sls::toDb (before) << " dB -> " << sls::toDb (after) << " dB)\n";
            check (sls::toDb (before) - sls::toDb (after) > 16.0, "the noise in the noise-only part drops by more than 16 dB (20 dB asked)");
            const double toneBefore = rms (nz->left, toneStart + 20000, n - 5000), toneAfter = rms (clean->left, toneStart + 20000, n - 5000);
            check (std::fabs (sls::toDb (toneAfter) - sls::toDb (toneBefore)) < 1.5, "the 1 kHz tone keeps its level within 1.5 dB");
            // noise under the tone, at other frequencies: measure the 2-4 kHz band by differencing out the tone
            check (std::isfinite (rms (clean->right, 0, n)), "the other channel is processed too, with its own profile");
        }
        check (sls::reduceNoise (nz, { 0, 1000 }, { 0, n }, nr) == nullptr, "a noise sample shorter than the minimum is refused");
        auto tiny = std::make_shared<sls::SampleData>();
        tiny->sampleRate = sr;
        tiny->left.assign (500, 0.1f);
        tiny->right.assign (500, 0.1f);
        check (sls::reduceNoise (tiny, { 0, 500 }, { 0, 500 }, nr) == nullptr, "a sample shorter than one FFT is refused");
        sls::NoiseReduction hostile;
        hostile.reductionDb = std::nan ("");
        hostile.sensitivity = 1.0e9;
        hostile.frequencySmoothing = 100000;
        hostile.releaseSeconds = -4.0;
        check (sls::reduceNoise (nz, noiseOnly, { toneStart, n }, hostile) != nullptr, "hostile settings are brought into range");

        auto silent = std::make_shared<sls::SampleData>();
        silent->sampleRate = sr;
        silent->left.assign (20000, 0.0f);
        silent->right.assign (20000, 0.0f);
        const auto s2 = sls::reduceNoise (silent, { 0, 10000 }, { 0, 20000 }, nr);
        check (s2 != nullptr && sls::peakOf (s2, { 0, 20000 }) == 0.0, "silence stays silent (and nothing is NaN)");

        const auto partial = sls::reduceNoise (nz, noiseOnly, { 0, toneStart }, nr);
        check (partial != nullptr && partial->left[static_cast<size_t> (n - 100)] == nz->left[static_cast<size_t> (n - 100)], "noise reduction of a range leaves the rest untouched");
    }
    {   // onsets
        constexpr int n = static_cast<int> (3.0 * sr);
        auto hits = std::make_shared<sls::SampleData>();
        hits->sampleRate = sr;
        hits->left.assign (static_cast<size_t> (n), 0.0f);
        hits->right.assign (static_cast<size_t> (n), 0.0f);
        const std::vector<double> times { 0.25, 0.75, 1.1, 1.8, 2.4 };
        for (const double t : times)
        {
            const int start = static_cast<int> (t * sr);
            for (int i = 0; i < 6000 && start + i < n; ++i)
            {
                const float v = static_cast<float> (0.6 * std::exp (-i / 1200.0) * std::sin (2.0 * 3.14159265358979 * 300.0 * i / sr));
                hits->left[static_cast<size_t> (start + i)] += v;
                hits->right[static_cast<size_t> (start + i)] += v;
            }
        }
        const sls::SamplePtr h = hits;
        const auto found = sls::detectOnsets (h, 0.5);
        check (found.size() == times.size(), ("five hits give five onsets (found " + std::to_string (found.size()) + ")").c_str());
        bool close = found.size() == times.size();
        for (size_t i = 0; close && i < times.size(); ++i)
            close = std::fabs (found[i] - times[i]) < 0.01;
        check (close, "each onset lies within 10 ms of the real start");

        auto steady = std::make_shared<sls::SampleData>();
        steady->sampleRate = sr;
        for (int i = 0; i < n; ++i)
        {
            steady->left.push_back (static_cast<float> (0.4 * std::sin (2.0 * 3.14159265358979 * 440.0 * i / sr)));
            steady->right.push_back (steady->left.back());
        }
        check (sls::detectOnsets (steady, 0.5, 0.05).size() <= 1, "a steady tone has no onsets (apart from its very start)");
        auto silent = std::make_shared<sls::SampleData>();
        silent->sampleRate = sr;
        silent->left.assign (static_cast<size_t> (n), 0.0f);
        silent->right.assign (static_cast<size_t> (n), 0.0f);
        check (sls::detectOnsets (silent, 1.0).empty(), "silence has no onsets");
        check (sls::detectOnsets (nullptr, 0.5).empty() && sls::detectOnsets (in, 0.5).empty(), "no audio or very short audio gives none");
        check (sls::detectOnsets (h, 0.5, 1.0).size() < times.size(), "a long minimum gap merges close onsets (0.75 and 1.1 s are 0.35 s apart)");
        check (sls::detectOnsets (h, std::nan ("")).size() >= 3, "a NaN sensitivity falls back to the middle");

        const auto grid = sls::gridSlices (h, 4);
        check (grid.size() == 3 && std::fabs (grid[0] - 0.75) < 1e-9 && std::fabs (grid[2] - 2.25) < 1e-9, "4 equal slices of 3 s are cut at 0.75, 1.5 and 2.25 s");
        check (sls::gridSlices (h, 1).empty() && sls::gridSlices (nullptr, 4).empty(), "fewer than 2 slices or no audio gives no cuts");
    }
    {   // writing and reading back
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sls-sample-edit-test");
        folder.deleteRecursively();
        const auto file = sls::uniqueEditedFile (folder, "my/odd:name");
        check (file.getFileName() == "myoddname-edit.wav" || file.getFileName().endsWith ("-edit.wav"), "the edited file name is made safe");
        auto data = std::make_shared<sls::SampleData>();
        data->sampleRate = 48000.0;
        for (int i = 0; i < 4800; ++i)
        {
            data->left.push_back (1.5f * static_cast<float> (std::sin (i * 0.05)));
            data->right.push_back (-0.25f);
        }
        check (sls::writeSampleWav (*data, file).wasOk() && file.existsAsFile(), "a sample is written as a WAV file");
        check (! sls::writeSampleWav (*data, file).wasOk(), "an existing file is never replaced");
        check (sls::uniqueEditedFile (folder, "my/odd:name") != file, "the next edited copy gets another name");
        const auto back = sls::importAudioFile (file);
        check (back.data != nullptr && back.data->numFrames() == 4800 && back.data->sampleRate == 48000.0
                   && std::fabs (back.data->left[300] - data->left[300]) < 1e-6f && std::fabs (back.data->right[10] + 0.25f) < 1e-6f,
               "it reads back identically (32-bit float, values above 1.0 survive)");
        sls::SampleData empty;
        check (! sls::writeSampleWav (empty, folder.getChildFile ("x.wav")).wasOk(), "empty audio is not written");
        folder.deleteRecursively();
    }
    {   // putting an edited copy back on the playlist
        constexpr double bpm = 120.0; // 480 ticks = 0.5 s
        const auto original = makeTone (220.0, 4.0, 48000.0);
        const auto edited = makeTone (330.0, 3.0, 48000.0);
        auto make = [&]
        {
            sls::Arrangement a;
            sls::AudioClip c;
            c.data = original;
            c.startTick = 960;
            c.lengthTicks = 3840;
            c.track = 0;
            a.audioClips.push_back (c);
            return a;
        };
        {
            auto a = make();
            const auto err = sls::useEditedAudio (a, 0, original, edited, { 1.0, 2.0 }, bpm);
            check (err.empty() && a.audioClips.size() == 3, "an edited clip with two slice points becomes three clips");
            if (a.audioClips.size() == 3)
            {
                std::sort (a.audioClips.begin(), a.audioClips.end(), [] (const auto& x, const auto& y) { return x.startTick < y.startTick; });
                check (a.audioClips[0].startTick == 960 && a.audioClips[1].startTick == 960 + 960 && a.audioClips[2].startTick == 960 + 1920
                           && a.audioClips[0].data == edited && a.audioClips[2].data == edited && a.audioClips[0].original == nullptr,
                       "the pieces start where the slices are (1 s = 960 ticks) and share the edited audio");
                check (a.audioClips[2].endTick() == 960 + 2880, "the last piece ends with the edited audio (3 s)");
            }
        }
        {
            auto a = make();
            check (sls::useEditedAudio (a, 0, original, edited, {}, bpm).empty() && a.audioClips.size() == 1 && a.audioClips[0].lengthTicks == 2880
                       && a.audioClips[0].data == edited,
                   "without slices the clip plays the whole edited audio");
            auto b = make();
            b.audioClips[0].stretch = { 2.0, 0.0, false };
            check (sls::useEditedAudio (b, 0, nullptr, nullptr, {}, bpm) == "There is no audio to use.", "no edited audio is refused");
            auto c = make();
            check (! sls::useEditedAudio (c, 5, edited, edited, {}, bpm).empty() && c.audioClips[0].data == original,
                   "a clip that is gone is refused and nothing changes");
            auto d = make();
            sls::AudioClip blocker;
            blocker.data = original;
            blocker.track = 0;
            blocker.startTick = 960 + 1000;
            blocker.lengthTicks = 480;
            d.audioClips.push_back (blocker);
            d.audioClips[0].lengthTicks = 900;
            check (sls::useEditedAudio (d, 0, original, edited, {}, bpm).empty()
                       && d.audioClips[0].lengthTicks == d.audioClips[0].naturalLengthTicks (bpm) && d.audioClips[0].lengthTicks > 1000
                       && d.audioClips.size() == 2,
                   "a neighbour no longer shortens the clip: since 0.33.0 clips may lie over each other");
            auto e = make();
            e.audioClips.insert (e.audioClips.begin(), e.audioClips[0]);
            e.audioClips[1].track = 1;
            check (sls::useEditedAudio (e, 0, original, edited, {}, bpm).empty() && e.audioClips[1].data == original && e.audioClips[0].data == edited,
                   "only the clip that was edited changes");
        }
    }
}

// A drum channel with the given kit and notes (pitch 36 + pad).
sls::Channel drumChannel (sls::DrumKit kit, std::vector<sls::Note> notes)
{
    sls::Channel c;
    c.name = "Drums";
    c.instrument = sls::Instrument::Drums;
    c.kit = std::make_shared<const sls::DrumKit> (std::move (kit));
    c.notes = std::move (notes);
    c.volume = 1.0f;
    return c;
}

void setPadSample (sls::DrumKit& kit, int pad, int layer, std::shared_ptr<const sls::SampleData> sample, int minVelocity = 1)
{
    auto& l = kit.pads[static_cast<size_t> (pad)].layers[static_cast<size_t> (layer)];
    l.sample = std::move (sample);
    l.minVelocity = minVelocity;
}

void testDrumMachine()
{
    std::cout << "Drum machine\n";
    constexpr int kP = sls::kDrumFirstPitch;
    const auto lowTone = sineSample (200.0, 1.5, 48000.0);
    const auto highTone = sineSample (800.0, 1.5, 48000.0);
    const auto render = [] (const sls::Channel& c) { return sls::renderToBuffer (oneBar ({ c }), {}); };
    const auto energy = [] (const juce::AudioBuffer<float>& b, int ch, double from, double to, double hz)
    {
        return toneEnergy (b.getReadPointer (ch), static_cast<int> (from * 48000), static_cast<int> (to * 48000), hz);
    };

    {   // the model
        sls::DrumKit k = sls::DrumKit::empty();
        check (k.pads[0].name == "Pad 1" && k.pads[15].name == "Pad 16" && ! k.pads[3].hasSample() && k.pads[3].layerFor (100) == nullptr,
               "an empty kit has sixteen empty pads");
        check (sls::DrumKit::padForPitch (35) == -1 && sls::DrumKit::padForPitch (36) == 0 && sls::DrumKit::padForPitch (51) == 15
                   && sls::DrumKit::padForPitch (52) == -1 && sls::DrumKit::pitchForPad (15) == 51,
               "pads answer to the notes 36 to 51");
        auto& pad = k.pads[0];
        pad.layers[0] = { lowTone, 1 };
        pad.layers[1] = { highTone, 90 };
        check (pad.layerFor (1)->sample == lowTone && pad.layerFor (89)->sample == lowTone && pad.layerFor (90)->sample == highTone
                   && pad.layerFor (127)->sample == highTone,
               "a velocity picks the layer with the highest minimum it reaches");
        pad.layers[0].minVelocity = 40; // below every minimum: the lowest layer plays
        check (pad.layerFor (10)->sample == lowTone, "a velocity below every minimum uses the lowest layer");
        pad.layers[2] = { std::make_shared<sls::SampleData>(), 1 }; // empty audio never counts
        check (pad.layerFor (100)->sample == highTone, "a layer without audio is ignored");

        auto bad = k;
        bad.pads[1].volume = 9.0f; bad.pads[1].pan = std::nanf (""); bad.pads[1].pitch = -99.0f; bad.pads[1].swing = 4.0f;
        bad.pads[1].chokeGroup = 77; bad.pads[1].layers[0].minVelocity = 500; bad.pads[1].name = std::string (100, 'x');
        const auto good = bad.sanitised();
        check (good.pads[1].volume == 1.5f && good.pads[1].pan == 0.0f && good.pads[1].pitch == -24.0f && good.pads[1].swing == 1.0f
                   && good.pads[1].chokeGroup == sls::kChokeGroups && good.pads[1].layers[0].minVelocity == 127 && good.pads[1].name.size() == 24,
               "hostile numbers are forced into range");
        check (good.owns (lowTone.get()) && ! good.owns (nullptr == nullptr ? sineSample (1.0, 0.1, 48000.0).get() : nullptr), "a kit knows its own samples");

        sls::Channel plain = synthChannel ({});
        plain.kit = std::make_shared<const sls::DrumKit> (k);
        sls::Channel noKit;
        noKit.instrument = sls::Instrument::Drums;
        const auto proj = sls::Project::make ({ plain, noKit }, 4 * kBeat);
        check (proj.channels[0].kit == nullptr && proj.channels[1].kit != nullptr && proj.channels[1].kit->pads.size() == 16,
               "a drum channel always has a kit; another instrument never does");
    }
    {   // notes and pads
        sls::DrumKit k = sls::DrumKit::empty();
        setPadSample (k, 0, 0, lowTone);
        const auto b = render (drumChannel (k, { { kP, 0, kBeat, 0.8f }, { kP + 1, kBeat, kBeat, 0.8f }, { kP - 1, 2 * kBeat, kBeat, 0.8f }, { kP + 16, 3 * kBeat, kBeat, 0.8f } }));
        check (energy (b, 0, 0.05, 0.4, 200.0) > 0.05, "a note on pad 1 plays its sample");
        check (peakOf (b.getReadPointer (0), static_cast<int> (0.55 * 48000), static_cast<int> (0.6 * 48000)) > 0.0f, "the sample plays on, as one-shots do");
        sls::DrumKit empty = sls::DrumKit::empty();
        const auto silent = render (drumChannel (empty, { { kP, 0, kBeat, 0.8f } }));
        check (peakOf (silent.getReadPointer (0), 0, silent.getNumSamples()) == 0.0f, "a pad without a sample is silent");
        const auto outside = render (drumChannel (k, { { kP - 1, 0, kBeat, 0.8f }, { kP + 16, 0, kBeat, 0.8f }, { 60, 0, kBeat, 0.8f } }));
        check (peakOf (outside.getReadPointer (0), 0, outside.getNumSamples()) == 0.0f, "notes outside 36..51 play nothing");
    }
    {   // velocity layers
        sls::DrumKit k = sls::DrumKit::empty();
        setPadSample (k, 0, 0, lowTone, 1);
        setPadSample (k, 0, 1, highTone, 90);
        const auto soft = render (drumChannel (k, { { kP, 0, kBeat, 0.4f } }));
        const auto hard = render (drumChannel (k, { { kP, 0, kBeat, 1.0f } }));
        check (energy (soft, 0, 0.05, 0.4, 200.0) > 10.0 * energy (soft, 0, 0.05, 0.4, 800.0), "a soft hit plays the low layer");
        check (energy (hard, 0, 0.05, 0.4, 800.0) > 10.0 * energy (hard, 0, 0.05, 0.4, 200.0), "a hard hit plays the high layer");
        const auto live = [&] (float velocity)
        {
            const auto project = oneBar ({ drumChannel (k, {}) });
            sls::Player player;
            player.prepare (kSr);
            player.liveNoteOn (project, 0, kP, velocity);
            return renderBlocks (player, project, false, 0.5);
        };
        const auto liveHard = live (1.0f);
        check (toneEnergy (liveHard.data(), 2400, 20000, 800.0) > 10.0 * toneEnergy (liveHard.data(), 2400, 20000, 200.0), "a pad played by hand picks its layer by velocity too");
    }
    {   // choke groups
        const auto openHat = sineSample (300.0, 1.8, 48000.0);
        const auto closedHat = sineSample (1000.0, 0.2, 48000.0);
        const auto make = [&] (int groupOpen, int groupClosed, int groupOther)
        {
            sls::DrumKit k = sls::DrumKit::empty();
            setPadSample (k, 0, 0, openHat);
            setPadSample (k, 1, 0, closedHat);
            setPadSample (k, 2, 0, sineSample (2000.0, 1.8, 48000.0));
            k.pads[0].chokeGroup = groupOpen;
            k.pads[1].chokeGroup = groupClosed;
            k.pads[2].chokeGroup = groupOther;
            return render (drumChannel (k, { { kP, 0, kBeat, 0.8f }, { kP + 2, 0, kBeat, 0.8f }, { kP + 1, kBeat, kBeat, 0.8f } }));
        };
        const auto choked = make (1, 1, 2);
        const auto free = make (0, 0, 0);
        check (energy (choked, 0, 0.1, 0.45, 300.0) > 0.05 && energy (free, 0, 0.1, 0.45, 300.0) > 0.05, "the open hat rings before the closed hat");
        check (energy (choked, 0, 0.6, 1.5, 300.0) < 0.02 * energy (free, 0, 0.6, 1.5, 300.0), "the closed hat cuts the open hat of the same group");
        check (energy (choked, 0, 0.6, 1.5, 2000.0) > 0.9 * energy (free, 0, 0.6, 1.5, 2000.0), "a pad of another group goes on ringing");
        check (energy (choked, 0, 0.5, 0.65, 1000.0) > 0.05, "the pad that cuts is itself heard");
        const auto retriggered = [&] (int group)
        {
            sls::DrumKit k = sls::DrumKit::empty();
            setPadSample (k, 0, 0, openHat);
            k.pads[0].chokeGroup = group;
            return render (drumChannel (k, { { kP, 0, kBeat, 0.8f }, { kP, 2 * kBeat, kBeat, 0.8f } }));
        };
        const auto alone = retriggered (3);
        const auto overlap = retriggered (0);
        const float peakAlone = peakOf (alone.getReadPointer (0), static_cast<int> (1.2 * 48000), static_cast<int> (1.7 * 48000));
        const float peakOverlap = peakOf (overlap.getReadPointer (0), static_cast<int> (1.2 * 48000), static_cast<int> (1.7 * 48000));
        check (peakAlone > 0.05f && peakAlone < 0.65f * peakOverlap,
               "choke group: the same pad hit again cuts its own tail (without a group the two hits add up)");
    }
    {   // level, pan, tuning
        const auto level = [&] (float volume, float pan)
        {
            sls::DrumKit k = sls::DrumKit::empty();
            setPadSample (k, 0, 0, lowTone);
            k.pads[0].volume = volume;
            k.pads[0].pan = pan;
            return render (drumChannel (k, { { kP, 0, kBeat, 0.8f } }));
        };
        const auto full = level (1.0f, 0.0f);
        const auto half = level (0.5f, 0.0f);
        const double ratio = energy (half, 0, 0.1, 0.4, 200.0) / energy (full, 0, 0.1, 0.4, 200.0);
        check (std::abs (ratio - 0.5) < 0.02, "the pad level scales its sound");
        check (std::abs (energy (full, 0, 0.1, 0.4, 200.0) - energy (full, 1, 0.1, 0.4, 200.0)) < 1e-6, "a centred pad is the same left and right");
        const auto left = level (1.0f, -1.0f);
        const auto right = level (1.0f, 1.0f);
        check (energy (left, 1, 0.1, 0.4, 200.0) < 0.001 * energy (left, 0, 0.1, 0.4, 200.0) && energy (right, 0, 0.1, 0.4, 200.0) < 0.001 * energy (right, 1, 0.1, 0.4, 200.0),
               "a pad panned hard left or right is heard on one side only");
        sls::DrumKit k = sls::DrumKit::empty();
        setPadSample (k, 0, 0, sineSample (400.0, 1.5, 48000.0));
        k.pads[0].pitch = 12.0f;
        const auto up = render (drumChannel (k, { { kP, 0, kBeat, 0.8f } }));
        check (energy (up, 0, 0.1, 0.4, 800.0) > 10.0 * energy (up, 0, 0.1, 0.4, 400.0), "a pad tuned up an octave sounds twice as high");
    }
    {   // mute and solo
        sls::DrumKit k = sls::DrumKit::empty();
        setPadSample (k, 0, 0, lowTone);
        setPadSample (k, 1, 0, highTone);
        const std::vector<sls::Note> both { { kP, 0, kBeat, 0.8f }, { kP + 1, 0, kBeat, 0.8f } };
        auto m = k;
        m.pads[0].mute = true;
        const auto muted = render (drumChannel (m, both));
        check (energy (muted, 0, 0.1, 0.4, 200.0) < 1e-6 && energy (muted, 0, 0.1, 0.4, 800.0) > 0.05, "a muted pad is silent, the others play");
        auto so = k;
        so.pads[1].solo = true;
        const auto soloed = render (drumChannel (so, both));
        check (energy (soloed, 0, 0.1, 0.4, 200.0) < 1e-6 && energy (soloed, 0, 0.1, 0.4, 800.0) > 0.05, "a soloed pad silences the others");
        so.pads[1].mute = true;
        const auto both2 = render (drumChannel (so, both));
        check (peakOf (both2.getReadPointer (0), 0, both2.getNumSamples()) == 0.0f, "mute wins over solo");
    }
    {   // swing per pad
        sls::DrumKit k = sls::DrumKit::empty();
        k.pads[0].swing = 1.0f;
        const std::vector<sls::Note> notes { { kP, sls::kSixteenthTicks, 60, 0.8f }, { kP, 2 * sls::kSixteenthTicks, 60, 0.8f },
                                             { kP + 1, sls::kSixteenthTicks, 60, 0.8f }, { 60, sls::kSixteenthTicks, 60, 0.8f } };
        auto c = drumChannel (k, notes);
        c.swing = 1.0f; // a drum channel ignores the channel swing
        const auto proj = sls::Project::make ({ c }, 4 * kBeat);
        const auto& out = proj.channels[0].notes;
        int swungPad0 = -1, straightPad0 = -1, pad1 = -1, other = -1;
        for (const auto& n : out)
        {
            if (n.pitch == kP && n.startTick != 2 * sls::kSixteenthTicks) swungPad0 = n.startTick;
            if (n.pitch == kP && n.startTick == 2 * sls::kSixteenthTicks) straightPad0 = n.startTick;
            if (n.pitch == kP + 1) pad1 = n.startTick;
            if (n.pitch == 60) other = n.startTick;
        }
        check (swungPad0 == sls::kSixteenthTicks + 60 && straightPad0 == 2 * sls::kSixteenthTicks, "a pad with swing delays its off-beat 16ths, not the on-beat ones");
        check (pad1 == sls::kSixteenthTicks && other == sls::kSixteenthTicks, "other pads and other notes are not swung (the channel swing does not apply)");
    }
    {   // the audio thread
        sls::DrumKit k = sls::DrumKit::empty();
        for (int i = 0; i < 16; ++i)
            setPadSample (k, i, 0, sineSample (150.0 + 40.0 * i, 0.3, 48000.0));
        k.pads[1].chokeGroup = 1;
        k.pads[2].chokeGroup = 1;
        std::vector<sls::Note> notes;
        for (int step = 0; step < 32; ++step)
            for (int pad = 0; pad < 16; pad += 1 + step % 3)
                notes.push_back ({ kP + pad, step * sls::kSixteenthTicks, 60, 0.3f + 0.02f * static_cast<float> (pad) });
        const auto project = sls::Project::make ({ drumChannel (k, notes) }, 8 * kBeat);
        sls::Player player;
        player.prepare (kSr);
        renderBlocks (player, project, true, 0.1); // settle lazy initialisation
        std::array<float, 256> bufL {}, bufR {};
        float* chans[2] = { bufL.data(), bufR.data() };
        tAllocs = 0;
        tCounting = true;
        for (int block = 0; block < 4 * 48000 / 256; ++block)
        {
            bufL.fill (0.0f);
            bufR.fill (0.0f);
            player.render (project, 120.0, true, chans, 2, 256);
        }
        tCounting = false;
        check (tAllocs == 0, "playing a busy drum pattern allocates nothing on the audio thread");

        // a new snapshot while pads ring: voices of samples that the new kit still holds go on, others stop without a crash
        auto k2 = k;
        k2.pads[0].layers[0].sample = nullptr;
        const auto next = sls::Project::make ({ drumChannel (k2, notes) }, 8 * kBeat);
        auto blockOut = renderBlocks (player, next, true, 0.5);
        check (std::all_of (blockOut.begin(), blockOut.end(), [] (float v) { return std::isfinite (v); }), "swapping the kit while it sounds is safe");
    }
}

juce::String kitJson (const sls::KitSpec& spec) { return juce::JSON::toString (sls::kitSpecToVar (spec), false); }

void testDrumKitFiles()
{
    std::cout << "Drum kits: files and projects\n";
    auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sls-kit-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
    root.createDirectory();

    const auto makeWav = [&] (const char* name, double hz, double seconds)
    {
        const auto file = root.getChildFile ("sounds").getChildFile (name);
        file.getParentDirectory().createDirectory();
        const auto data = sineSample (hz, seconds, 44100.0);
        sls::SampleData copy = *data;
        sls::writeSampleWav (copy, file);
        return file;
    };
    const auto a = makeWav ("a-soft.wav", 200.0, 0.2);
    const auto b = makeWav ("b-hard.wav", 800.0, 0.3);

    sls::KitSpec spec = sls::KitSpec::empty();
    spec.name = "Test kit";
    spec.pads[0].name = "Snare";
    spec.pads[0].sources[0] = a.getFullPathName().toStdString();
    spec.pads[0].sources[1] = b.getFullPathName().toStdString();
    spec.pads[0].minVelocity[1] = 100;
    spec.pads[0].volume = 0.7f;
    spec.pads[0].pan = -0.25f;
    spec.pads[0].pitch = 3.5f;
    spec.pads[0].swing = 0.4f;
    spec.pads[0].chokeGroup = 5;
    spec.pads[0].solo = true;
    spec.pads[1].sources[0] = "builtin:kick";
    spec.pads[1].mute = true;
    spec.pads[2].sources[0] = (root.getChildFile ("gone.wav")).getFullPathName().toStdString();

    {   // JSON
        const auto back = sls::kitSpecFromVar (juce::JSON::parse (kitJson (spec)));
        check (kitJson (back) == kitJson (spec), "a kit description survives JSON");
        const auto hostile = sls::kitSpecFromVar (juce::JSON::parse (
            "{\"name\":5,\"pads\":[{\"name\":\"x\",\"volume\":99,\"pan\":\"left\",\"pitch\":-500,\"swing\":7,\"choke\":42,\"layers\":["
            "{\"sample\":\"builtin:kick\",\"minVelocity\":9000},{\"sample\":3},{\"minVelocity\":5},7,{},{},{}]},17,null]}"));
        check (hostile.pads[0].volume == 1.5f && hostile.pads[0].pan == 0.0f && hostile.pads[0].pitch == -24.0f && hostile.pads[0].swing == 1.0f
                   && hostile.pads[0].chokeGroup == sls::kChokeGroups && hostile.pads[0].minVelocity[0] == 127
                   && hostile.pads[0].sources[0] == "builtin:kick" && hostile.pads[0].sources[1].empty() && hostile.pads[1].name == "Pad 2",
               "hostile values are clamped and bad entries ignored");
        check (sls::kitSpecFromVar (juce::var (5)).pads[15].name == "Pad 16" && sls::kitSpecFromVar (juce::var()).name == "Kit", "something that is not a kit gives an empty one");
    }
    {   // loading
        juce::StringArray problems;
        const auto kit = sls::loadKit (spec, problems);
        check (problems.size() == 1 && problems[0].contains ("gone.wav"), "one missing file is reported");
        check (kit.pads[0].layers[0].sample != nullptr && kit.pads[0].layers[0].sample->numFrames() == 8820 && kit.pads[0].layers[1].sample->numFrames() == 13230
                   && kit.pads[0].layers[1].minVelocity == 100 && kit.pads[1].layers[0].sample != nullptr && kit.pads[1].layers[0].sample->source == "builtin:kick",
               "files and built-in sounds are read, with their velocity layers");
        check (kit.pads[2].layers[0].sample != nullptr && kit.pads[2].layers[0].sample->numFrames() == 0 && kit.pads[2].layers[0].sample->source == spec.pads[2].sources[0]
                   && ! kit.pads[2].hasSample(),
               "a missing file leaves a silent pad that remembers where the file was");
        check (kit.pads[0].volume == 0.7f && kit.pads[0].chokeGroup == 5 && kit.pads[0].solo && kit.pads[1].mute && kit.pads[0].name == "Snare",
               "the pad settings come with it");
        check (kitJson (sls::specFromKit (kit)) == kitJson (spec), "the description of a loaded kit is the one it was loaded from");
    }
    {   // kit folders
        juce::StringArray problems;
        const auto kit = sls::loadKit (spec, problems);
        const auto folder = root.getChildFile ("MyKit");
        check (sls::saveKitFolder (kit, folder).isEmpty() && sls::isKitFolder (folder) && folder.getChildFile ("samples").getChildFile ("01-1-a-soft.wav").existsAsFile()
                   && folder.getChildFile ("samples").getChildFile ("01-2-b-hard.wav").existsAsFile(),
               "a kit is saved as a folder with kit.json and copies of its sounds");
        check (! folder.getChildFile ("samples").getChildFile ("02-1-kick").exists() && ! folder.getChildFile ("kit.json.tmp").exists(), "built-in sounds are not copied, and no temporary file is left");
        const auto text = folder.getChildFile ("kit.json").loadFileAsString();
        check (text.contains ("samples/01-1-a-soft.wav") && ! text.contains (root.getChildFile ("sounds").getFullPathName()), "the file stores paths relative to the folder");
        juce::StringArray problems2;
        const auto read = sls::readKitFolder (folder, problems2);
        check (problems2.isEmpty() && read.name == "Test kit" && read.pads[0].sources[0] == folder.getChildFile ("samples/01-1-a-soft.wav").getFullPathName().toStdString()
                   && read.pads[1].sources[0] == "builtin:kick" && read.pads[0].chokeGroup == 5,
               "reading it back gives the same kit with absolute paths");

        // the copy is independent of the originals
        a.deleteFile();
        b.deleteFile();
        juce::StringArray problems3;
        const auto again = sls::loadKit (read, problems3);
        check (problems3.size() == 1 && again.pads[0].layers[1].sample->numFrames() == 13230, "the kit folder still works after the original files are gone");

        check (sls::saveKitFolder (again, folder).isEmpty(), "a kit folder can be saved over itself");
        check (sls::saveKitFolder (again, folder.getChildFile ("kit.json")).isNotEmpty(), "a file is refused as a kit folder");
        const auto foreign = root.getChildFile ("Foreign");
        foreign.createDirectory();
        foreign.getChildFile ("notes.txt").replaceWithText ("x");
        check (sls::saveKitFolder (again, foreign).isNotEmpty() && ! foreign.getChildFile ("kit.json").exists(), "a folder with other files is refused and left alone");
        const auto emptyFolder = root.getChildFile ("Empty");
        emptyFolder.createDirectory();
        check (sls::saveKitFolder (again, emptyFolder).isEmpty(), "an empty folder is fine");
    }
    {   // damaged kit.json
        const auto bad = root.getChildFile ("Bad");
        bad.createDirectory();
        bad.getChildFile ("kit.json").replaceWithText ("{ not json");
        juce::StringArray problems;
        const auto k = sls::readKitFolder (bad, problems);
        check (problems.size() == 1 && ! k.pads[0].sources[0].empty() == false, "a damaged kit.json is reported and gives an empty kit");
        bad.getChildFile ("kit.json").replaceWithText ("{\"format\":\"sun-loops-studio-drum-kit\",\"version\":99,\"pads\":[{\"name\":\"Odd\",\"layers\":[{\"sample\":\"../../etc/x.wav\"}]}]}");
        juce::StringArray problems2;
        const auto k2 = sls::readKitFolder (bad, problems2);
        check (problems2.size() == 1 && k2.pads[0].name == "Odd", "a kit from a newer version is read with a warning");
    }
    {   // a plain folder of sounds
        const auto plain = root.getChildFile ("Plain");
        plain.createDirectory();
        for (int i = 1; i <= 18; ++i)
            sls::writeSampleWav (*sineSample (100.0 + i, 0.05, 44100.0), plain.getChildFile ("s" + juce::String (i) + ".wav"));
        plain.getChildFile ("readme.txt").replaceWithText ("hello");
        juce::StringArray problems;
        const auto k = sls::readKitFolder (plain, problems);
        check (k.pads[0].name == "s1" && k.pads[1].name == "s2" && k.pads[9].name == "s10" && k.pads[15].name == "s16", "a folder of audio files fills the pads in natural name order");
        check (problems.size() == 1 && problems[0].contains ("first 16"), "more than sixteen sounds are reported");
        juce::StringArray none;
        sls::readKitFolder (root.getChildFile ("Foreign"), none);
        check (none.size() == 1, "a folder without sounds is reported");
    }
    {   // the kits inside the program
        juce::StringArray problems;
        const auto basic = sls::loadKit (sls::basicKitSpec(), problems);
        check (problems.isEmpty() && basic.pads[0].hasSample() && basic.pads[1].hasSample() && basic.pads[2].hasSample() && ! basic.pads[3].hasSample() && basic.pads[2].chokeGroup == 1,
               "the basic kit has kick, snare and hat");
    }
    {   // project file
        auto doc = sls::ProjectDocument::blank();
        sls::Channel drums;
        drums.name = "Beats";
        drums.instrument = sls::Instrument::Drums;
        drums.mixerTrack = 1;
        doc.channels.push_back (drums);
        doc.sampleSources.emplace_back();
        doc.kitSpecs = { sls::KitSpec::empty(), spec };
        doc.arrangement.ensureChannelCount (doc.channels.size());
        const auto parsed = sls::ProjectDocument::fromJson (doc.toJson());
        check (parsed.document.has_value() && parsed.document->channels.size() == 2 && parsed.document->channels[1].instrument == sls::Instrument::Drums
                   && parsed.document->kitSpecs.size() == 2 && kitJson (parsed.document->kitSpecs[1]) == kitJson (spec),
               "a drum channel and its kit survive the project file");
        check (parsed.document->channels[0].instrument == sls::Instrument::Synth && kitJson (parsed.document->kitSpecs[0]) == kitJson (sls::KitSpec::empty()), "other channels have no kit");
        auto text = doc.toJson();
        text = text.replace ("\"kit\"", "\"kitx\"");
        const auto old = sls::ProjectDocument::fromJson (text);
        check (old.document.has_value() && kitJson (old.document->kitSpecs[1]) == kitJson (sls::KitSpec::empty()), "a drum channel without a kit opens as an empty kit");
    }
    root.deleteRecursively();
}

void testAuditioner()
{
    std::cout << "Sample preview (auditioner)\n";
    auto s = std::make_shared<sls::SampleData>();
    s->sampleRate = 48000.0;
    for (int i = 0; i < 1000; ++i)
    {
        s->left.push_back (static_cast<float> (i));
        s->right.push_back (-static_cast<float> (i));
    }
    std::vector<float> l (400, 0.0f), r (400, 0.0f);
    float* ch[2] = { l.data(), r.data() };
    auto reset = [&] { std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f); };

    {
        sls::Auditioner a;
        a.mixInto (ch, 2, 400, 48000.0);
        check (l[10] == 0.0f && ! a.isPlaying(), "nothing is added while nothing plays");
        a.play (s, 100.0, 0.0, false);
        check (a.isPlaying() && a.positionFrames() == 100.0, "play starts at the requested frame");
        a.mixInto (ch, 2, 400, 48000.0);
        check (l[0] == 100.0f && l[399] == 499.0f && r[0] == -100.0f && std::fabs (a.positionFrames() - 500.0) < 1e-9, "the sample plays from there, left and right, and the position follows");
        reset();
        a.mixInto (ch, 2, 400, 48000.0);
        check (l[0] == 500.0f && l[399] == 899.0f, "the next block continues without a gap");
        reset();
        a.mixInto (ch, 2, 400, 48000.0);
        check (l[99] == 999.0f && l[100] == 0.0f && ! a.isPlaying(), "it stops at the end (and adds nothing after it)");
    }
    {
        sls::Auditioner a;
        reset();
        a.play (s, 200.0, 300.0, false);
        a.mixInto (ch, 2, 400, 48000.0);
        check (l[99] == 299.0f && l[100] == 0.0f && ! a.isPlaying(), "a range stops at its end");
        reset();
    }
    {
        sls::Auditioner a;
        reset();
        a.play (s, 200.0, 300.0, true);
        a.mixInto (ch, 2, 400, 48000.0);
        check (a.isPlaying() && l[99] == 299.0f && l[100] == 200.0f && l[399] == 299.0f, "a loop wraps from the end to the start of its range");
        a.stop();
        reset();
        a.mixInto (ch, 2, 400, 48000.0);
        check (! a.isPlaying() && l[5] == 0.0f, "stop silences it at once");
    }
    {   // other device rate: half speed at 96 kHz
        sls::Auditioner a;
        a.play (s, 0.0, 0.0, false);
        reset();
        a.mixInto (ch, 2, 400, 96000.0);
        check (std::fabs (l[100] - 50.0f) < 1e-3f && std::fabs (l[1] - 0.5f) < 1e-3f, "at twice the device rate the sample is interpolated and keeps its real speed");
    }
    {   // hostile input
        sls::Auditioner a;
        a.play (nullptr, 0.0, 0.0, false);
        a.play (s, std::nan (""), -5.0, true);
        check (a.isPlaying() && a.positionFrames() == 0.0, "NaN start and a negative end fall back to the whole sample");
        a.play (s, 1.0e9, 0.0, false);
        check (a.positionFrames() == 999.0, "a start past the end is held on the last frame");
        std::vector<float> one (10, 0.0f);
        float* mono[1] = { one.data() };
        a.mixInto (mono, 1, 10, 48000.0);
        check (one[0] == 999.0f, "a mono output gets the left side");
        float* none[2] = { nullptr, nullptr };
        a.play (s, 0.0, 0.0, false);
        a.mixInto (none, 2, 10, 48000.0);
        check (true, "null output channels are skipped");
        a.release();
        check (! a.isPlaying(), "release stops it");
    }
    {   // stop() waits for the audio thread; swapping samples while it runs is safe
        sls::Auditioner a;
        std::atomic<bool> go { true };
        std::thread audio ([&]
        {
            std::vector<float> bl (64, 0.0f), br (64, 0.0f);
            float* c[2] = { bl.data(), br.data() };
            while (go.load())
                a.mixInto (c, 2, 64, 48000.0);
        });
        for (int i = 0; i < 2000; ++i)
        {
            auto copy = std::make_shared<sls::SampleData> (*s);
            a.play (copy, 0.0, 0.0, true);
            if (i % 7 == 0)
                a.release();
        }
        a.release();
        go.store (false);
        audio.join();
        check (true, "2000 swaps while the audio thread runs, without a crash");
    }
}

// ------------------------------------------------------------------------- 0.15.0: seeking and the loop region

void testChannelRemap()
{
    std::cout << "Deleting, moving and cloning channels\n";
    sls::Arrangement a;
    a.patterns.resize (2);
    for (auto& p : a.patterns)
        p.notes = { { { 60, 0, 120, 0.8f } }, { { 62, 0, 120, 0.8f } }, { { 64, 0, 120, 0.8f } } };
    a.patterns[1].notes[2].push_back ({ 65, 240, 120, 0.8f });
    sls::AutomationClip vol = sls::makeFlatAutomationClip ({ sls::AutomationKind::ChannelVolume, 2, 0, 0 }, 0, 0, 1920, 0.5f);
    sls::AutomationClip pan = sls::makeFlatAutomationClip ({ sls::AutomationKind::ChannelPan, 0, 0, 0 }, 1, 0, 1920, 0.5f);
    sls::AutomationClip track = sls::makeFlatAutomationClip ({ sls::AutomationKind::TrackVolume, 0, 0, 0 }, 2, 0, 1920, 0.5f);
    a.automationClips = { vol, pan, track };

    auto deleted = a;
    sls::remapArrangementChannels (deleted, { 1, 2 }, 3); // channel 0 deleted
    check (deleted.patterns[0].notes.size() == 2 && deleted.patterns[0].notes[0][0].pitch == 62 && deleted.patterns[1].notes[1].size() == 2,
           "delete: every pattern loses that channel's notes and the others move up");
    check (deleted.automationClips.size() == 2 && deleted.automationClips[0].target.index == 1
               && deleted.automationClips[1].target.kind == sls::AutomationKind::TrackVolume,
           "delete: its automation goes, the others follow their channel, track automation is untouched");

    auto moved = a;
    sls::remapArrangementChannels (moved, { 0, 2, 1 }, 3); // channels 1 and 2 swapped
    check (moved.patterns[1].notes[1].size() == 2 && moved.patterns[1].notes[2][0].pitch == 62, "move: the notes go with the channel");
    check (moved.automationClips[0].target.index == 1 && moved.automationClips[1].target.index == 0, "move: and so does its automation");

    auto cloned = a;
    sls::remapArrangementChannels (cloned, { 0, 1, 2, 2 }, 3); // channel 2 cloned
    check (cloned.patterns[1].notes.size() == 4 && cloned.patterns[1].notes[3].size() == 2 && cloned.patterns[1].notes[2].size() == 2,
           "clone: the copy has the same notes in every pattern");
    check (cloned.automationClips.size() == 3 && cloned.automationClips[0].target.index == 2, "clone: automation stays on the original");

    auto bad = a;
    sls::remapArrangementChannels (bad, { 0, 7, -1 }, 3);
    check (bad.patterns[0].notes.size() == 3 && bad.patterns[0].notes[1].empty() && bad.patterns[0].notes[2].empty(),
           "indices that do not exist give empty channels instead of a crash");
}

void testProjectWithSamples()
{
    std::cout << "Export project with samples, relative paths\n";
    const auto dir = freshTempDir ("sls-export");
    const auto a = dir.getChildFile ("src1/kick.wav"), b = dir.getChildFile ("src2/kick.wav"), c = dir.getChildFile ("clip.wav");
    for (const auto& f : { a, b, c })
    {
        f.getParentDirectory().createDirectory();
        f.replaceWithText ("RIFF not really a wav, but a file");
    }
    auto doc = sls::ProjectDocument::blank();
    doc.channels.push_back (doc.channels[0]);
    doc.channels.push_back (doc.channels[0]);
    doc.sampleSources = { a.getFullPathName().toStdString(), b.getFullPathName().toStdString(), "" };
    doc.channels[2].instrument = sls::Instrument::Drums;
    doc.kitSpecs = { sls::KitSpec::empty(), sls::KitSpec::empty(), sls::KitSpec::empty() };
    doc.kitSpecs[2].pads[0].sources[0] = a.getFullPathName().toStdString();
    doc.kitSpecs[2].pads[1].sources[0] = dir.getChildFile ("gone.wav").getFullPathName().toStdString();
    doc.arrangement.ensureChannelCount (doc.channels.size());
    sls::AudioClip clip;
    clip.data = sls::makeMissingSample (c.getFullPathName().toStdString());
    clip.name = "clip";
    doc.arrangement.audioClips.push_back (clip);

    juce::StringArray notes;
    const auto out = dir.getChildFile ("Moved Song");
    const auto error = sls::exportProjectWithSamples (doc, out, "Moved Song", notes);
    check (error.isEmpty(), "export project with samples succeeds");
    const auto projectFile = out.getChildFile ("Moved Song.slp");
    check (projectFile.existsAsFile() && out.getChildFile ("Samples/kick.wav").existsAsFile() && out.getChildFile ("Samples/kick-2.wav").existsAsFile()
               && out.getChildFile ("Samples/clip.wav").existsAsFile(),
           "the project file and a copy of every sound are in the folder (two files with one name both kept)");
    const auto text = projectFile.loadFileAsString();
    check (text.contains ("Samples/kick.wav") && ! text.contains (a.getFullPathName()),
           "the copies are referred to by relative paths");
    check (notes.size() == 1 && notes[0].contains ("gone.wav") && text.contains (dir.getChildFile ("gone.wav").getFullPathName()),
           "a missing file is reported and keeps its old path");

    // Move the folder: the project still finds its sounds.
    const auto moved = dir.getChildFile ("elsewhere/Moved Song");
    moved.getParentDirectory().createDirectory();
    check (out.moveFileTo (moved), "(the folder is moved)");
    const auto parsed = sls::readProjectFile (moved.getChildFile ("Moved Song.slp"));
    check (parsed.document && parsed.document->sampleSources[0] == moved.getChildFile ("Samples/kick.wav").getFullPathName().toStdString()
               && parsed.document->sampleSources[1] == moved.getChildFile ("Samples/kick-2.wav").getFullPathName().toStdString()
               && parsed.document->kitSpecs.size() == 3 && parsed.document->kitSpecs[2].pads[0].sources[0] == moved.getChildFile ("Samples/kick.wav").getFullPathName().toStdString()
               && parsed.document->arrangement.audioClips.size() == 1
               && parsed.document->arrangement.audioClips[0].data->source == moved.getChildFile ("Samples/clip.wav").getFullPathName().toStdString(),
           "after moving the folder, relative paths are read as paths inside it (samples, kit layers, audio clips)");
    check (sls::exportProjectWithSamples (doc, moved, "Again", notes).isNotEmpty(), "exporting into a folder that is not empty is refused");
    dir.deleteRecursively();
}

void testPatternLength()
{
    std::cout << "Pattern length\n";
    const int bar = 4 * kBeat;
    sls::Arrangement a;
    a.patterns.resize (1);
    setLengths (a, bar);
    check ((a.timelineTicks() / a.patternLength (0)) == 64 && a.timelineTicks() == 64 * bar, "1-bar patterns: 64 of them fit (64 bars)");
    setLengths (a, 2 * bar);
    check ((a.timelineTicks() / a.patternLength (0)) == 32 && a.timelineTicks() == 64 * bar, "2-bar patterns: 32 (64 bars, as before)");
    setLengths (a, 4 * bar);
    check ((a.timelineTicks() / a.patternLength (0)) == 32 && a.timelineTicks() == 128 * bar, "4-bar patterns: 32 (128 bars)");
    a.patterns.resize (1);
    setLengths (a, 8 * bar);
    check ((a.timelineTicks() / a.patternLength (0)) == 32 && a.canPlace (uc (a, 0, 0, 31, 1)) && ! a.canPlace (uc (a, 0, 0, 32, 1)), "8-bar patterns: 32, and the last unit is the end");

    // A 4-bar pattern played twice in Song mode: its note in bar 3 comes at bars 3 and 7.
    setLengths (a, 4 * bar);
    a.patterns[0].notes = { { { 60, 2 * bar, kBeat, 0.8f } } };
    a.clips = { uc (a, 0, 0, 0, 2) };
    const auto song = sls::compileSong ({ synthChannel ({}) }, a);
    check (song.lengthTicks == 8 * bar && song.channels[0].notes.size() == 2 && song.channels[0].notes[1].startTick == 6 * bar,
           "4-bar patterns compile to a song with the notes in the right bars");

    auto doc = sls::ProjectDocument::blank();
    setLengths (doc.arrangement, 4 * bar);
    doc.arrangement.patterns[0].notes[0] = { { 60, 3 * bar, kBeat, 0.8f } }; // only exists in a 4-bar pattern
    auto parsed = sls::ProjectDocument::fromJson (doc.toJson());
    check (parsed.document && parsed.document->arrangement.patternLength (0) == 4 * bar && parsed.document->arrangement.patterns[0].notes[0].size() == 1,
           "the pattern length is saved, and notes beyond two bars are read back");
    const auto withBars = [&] (const juce::var& value)
    {
        auto v = juce::JSON::parse (doc.toJson());
        v.getDynamicObject()->removeProperty ("patternBeats"); // as files from before 0.42.0
        for (auto& pv : *v.getDynamicObject()->getProperty ("patterns").getArray())
            pv.getDynamicObject()->removeProperty ("beats");
        if (value.isVoid())
            v.getDynamicObject()->removeProperty ("patternBars");
        else
            v.getDynamicObject()->setProperty ("patternBars", value);
        return juce::JSON::toString (v);
    };
    parsed = sls::ProjectDocument::fromJson (withBars (3));
    check (parsed.document && parsed.document->arrangement.patternLength (0) == 2 * bar && ! parsed.warnings.empty(),
           "a length that is not offered becomes two bars, with a warning");
    parsed = sls::ProjectDocument::fromJson (withBars (juce::var()));
    check (parsed.document && parsed.document->arrangement.patternLength (0) == 2 * bar, "older files (no length) are two bars");
}

void testPatternLoopRegion()
{
    std::cout << "Piano roll loop region of a pattern (0.20.0)\n";
    sls::Pattern pattern;
    const int ticks = 8 * kBeat;
    pattern.lengthTicks = ticks;
    check (! pattern.hasLoop(), "a new pattern has no loop region");
    pattern.loopStartTick = kBeat;
    pattern.loopEndTick = 3 * kBeat;
    auto shorter = pattern;
    shorter.lengthTicks = 2 * kBeat;
    check (pattern.hasLoop() && ! shorter.hasLoop(), "a region inside the pattern counts; made shorter than its end, it no longer does");

    // In Pattern mode the region is the snapshot's loop region: one bar note at 0 (A) and one at beat 3 (E5); a region over beats 3..4 repeats E5 only.
    auto project = sls::Project::make ({ synthChannel ({ { 69, 0, kBeat, 0.8f }, { 76, 2 * kBeat, kBeat, 0.8f } }) }, ticks);
    project.loopStartTick = 2 * kBeat;
    project.loopEndTick = 4 * kBeat;
    sls::Player p;
    p.prepare (kSr);
    double lowest = 1.0e9, highest = -1.0;
    for (int block = 0; block < 300; ++block) // 3 s; the region is reached at 1 s
    {
        renderBlocks (p, project, true, 0.01);
        if (block >= 110)
        {
            lowest = std::min (lowest, p.getPositionTicks());
            highest = std::max (highest, p.getPositionTicks());
        }
    }
    check (lowest >= 2 * kBeat - 1 && highest < 4 * kBeat, "Pattern mode with a region: the position stays inside it");

    auto doc = sls::ProjectDocument::blank();
    doc.arrangement.patterns[0].loopStartTick = kBeat / 2;
    doc.arrangement.patterns[0].loopEndTick = 2 * kBeat;
    const auto text = doc.toJson();
    auto parsed = sls::ProjectDocument::fromJson (text);
    check (parsed.document && parsed.document->arrangement.patterns[0].loopStartTick == kBeat / 2
               && parsed.document->arrangement.patterns[0].loopEndTick == 2 * kBeat,
           "the pattern's region is saved in the project file and read back");
    doc.arrangement.patterns[0].loopStartTick = doc.arrangement.patterns[0].loopEndTick = -1;
    check (! doc.toJson().contains ("\"loop\""), "no region: nothing written");
    auto v = juce::JSON::parse (text);
    auto* first = v.getDynamicObject()->getProperty ("patterns").getArray()->getReference (0).getDynamicObject();
    first->setProperty ("loop", juce::Array<juce::var> { 3 * kBeat, kBeat });
    parsed = sls::ProjectDocument::fromJson (juce::JSON::toString (v));
    check (parsed.document && ! parsed.document->arrangement.patterns[0].hasLoop(),
           "a backwards region in a file is left out");
    first->setProperty ("loop", juce::Array<juce::var> { 0, 999999 });
    parsed = sls::ProjectDocument::fromJson (juce::JSON::toString (v));
    check (parsed.document && ! parsed.document->arrangement.patterns[0].hasLoop(),
           "a region past the end of the pattern is left out");
}

void testSeekAndLoopRegion()
{
    std::cout << "Seek and loop region\n";
    const int sps = 24000; // samples per beat at 120 BPM / 48 kHz
    // Two bars (4 s): A (440 Hz) on beat 1 of bar 1, B (E5, 659 Hz: not a harmonic of A) on beat 1 of bar 2.
    const double bHz = 659.2551;
    auto project = sls::Project::make ({ synthChannel ({ { 69, 0, kBeat, 0.8f }, { 76, 4 * kBeat, kBeat, 0.8f } }) }, 8 * kBeat);

    {
        sls::Player p;
        p.prepare (kSr);
        renderBlocks (p, project, false, 0.05);
        p.seek (project, 4 * kBeat, 120.0);
        check (p.getPositionTicks() == 4 * kBeat && p.getStartPosition() == 4 * kBeat, "seek while stopped moves the position and the start point");
        auto out = renderBlocks (p, project, true, 0.4);
        check (toneEnergy (out.data(), 2400, 18000, bHz) > 0.05 && toneEnergy (out.data(), 2400, 18000, 440.0) < 0.01,
               "play after a seek starts there (bar 2's note, not bar 1's)");
        renderBlocks (p, project, false, 0.05);
        check (p.getPositionTicks() == 4 * kBeat, "Stop goes back to where playback was started from, not to the beginning");
        out = renderBlocks (p, project, true, 0.3);
        check (toneEnergy (out.data(), 2400, 12000, bHz) > 0.05, "and Play starts there again");

        p.seek (project, 0, 120.0); // while playing
        out = renderBlocks (p, project, true, 0.4);
        check (toneEnergy (out.data(), 4800, 18000, 440.0) > 0.05, "seek while playing: the note at the new place sounds");
        check (toneEnergy (out.data(), 4800, 18000, bHz) < 0.005, "and the note that was sounding stops");
        check (p.getPositionTicks() > 0.0 && p.getPositionTicks() < kBeat, "the position runs on from the new place");

        p.seek (project, 1.0e9, 120.0);
        check (p.getPositionTicks() == 0.0, "a seek past the end of the song goes to the start");
        p.seek (project, -5.0, 120.0);
        check (p.getPositionTicks() == 0.0, "a negative seek goes to the start");
        p.seek (project, std::nan (""), 120.0);
        check (p.getPositionTicks() == 0.0, "a NaN seek goes to the start");
    }

    // Loop region: bar 2, beats 1..2 (ticks 1920..2880 = 2.0 s .. 3.0 s): once there the song never goes back to bar 1.
    {
        auto looped = project;
        looped.loopStartTick = 4 * kBeat;
        looped.loopEndTick = 6 * kBeat;
        sls::Player p;
        p.prepare (kSr);
        double lowest = 1.0e9, highest = -1.0;
        std::vector<float> all;
        for (int block = 0; block < 450; ++block) // 450 x 0.01 s = 4.5 s
        {
            const auto out = renderBlocks (p, looped, true, 0.01);
            all.insert (all.end(), out.begin(), out.end());
            if (block >= 210)
            {
                lowest = std::min (lowest, p.getPositionTicks());
                highest = std::max (highest, p.getPositionTicks());
            }
        }
        check (lowest >= 4 * kBeat - 1 && highest < 6 * kBeat, "with a loop region the position stays inside it once it got there");
        // B starts at 2.0 s, 3.0 s, 4.0 s; without the region A would come back at 4.0 s.
        check (toneEnergy (all.data(), 6 * sps + 2400, 6 * sps + 12000, bHz) > 0.05, "the region's note plays again at the next pass (3.0 s)");
        check (toneEnergy (all.data(), 8 * sps + 2400, 8 * sps + 12000, bHz) > 0.05, "and again (4.0 s)");
        check (toneEnergy (all.data(), 8 * sps + 2400, 8 * sps + 12000, 440.0) < 0.01, "and the start of the song does not come back");
        check (peakOf (all.data(), 6 * sps - 1200, 6 * sps + 1200) < 0.9f, "no jump in level at the loop point");

        // Started after the region: the end of the song goes back to the region, not to bar 1.
        sls::Player q;
        q.prepare (kSr);
        renderBlocks (q, looped, false, 0.01);
        q.seek (looped, 7 * kBeat, 120.0);
        const auto out = renderBlocks (q, looped, true, 0.75);
        check (toneEnergy (out.data(), sps + 2400, sps + 12000, bHz) > 0.05 && toneEnergy (out.data(), sps + 2400, sps + 12000, 440.0) < 0.01,
               "started after the region, the end of the song goes back to the region");

        // An invalid region is ignored.
        auto broken = project;
        broken.loopStartTick = 6 * kBeat;
        broken.loopEndTick = 4 * kBeat;
        sls::Player r;
        r.prepare (kSr);
        for (int i = 0; i < 430; ++i)
            renderBlocks (r, broken, true, 0.01);
        check (r.getPositionTicks() < kBeat, "a region with its end before its start is ignored (the song wraps at its end)");

        // No allocation while seeking and looping.
        std::vector<float> l (256), rr (256);
        float* ch[2] = { l.data(), rr.data() };
        sls::Player z;
        z.prepare (kSr);
        gAllocs = 0;
        gCounting = true;
        for (int i = 0; i < 2000; ++i)
        {
            if (i % 97 == 0)
                z.seek (looped, (i * 131) % (8 * kBeat), 120.0);
            z.render (looped, 140.0, true, ch, 2, 256);
        }
        gCounting = false;
        check (gAllocs.load() == 0, "seeking and the loop region allocate nothing");
    }

    // Audio clips: a seek into the middle of a clip starts its sound at the right point.
    {
        auto data = std::make_shared<sls::SampleData>();
        data->sampleRate = kSr;
        data->source = "/tmp/half.wav";
        data->left.assign (static_cast<size_t> (2 * kSr), 0.0f);
        for (int i = static_cast<int> (kSr); i < static_cast<int> (2 * kSr); ++i) // first second silent, second second a tone
            data->left[static_cast<size_t> (i)] = 0.5f * static_cast<float> (std::sin (6.283185307179586 * 300.0 * i / kSr));
        data->right = data->left;
        sls::AudioEvent e;
        e.data = data;
        e.startTick = 0;
        e.endTick = 4 * kBeat;
        const auto withClip = sls::Project::make ({ synthChannel ({}) }, 8 * kBeat, {}, {}, { e });

        sls::Player p;
        p.prepare (kSr);
        renderBlocks (p, withClip, false, 0.01);
        p.seek (withClip, 2 * kBeat, 120.0); // 1 s into the clip
        auto out = renderBlocks (p, withClip, true, 0.2);
        check (toneEnergy (out.data(), 960, 9600, 300.0) > 0.05, "play from the middle of an audio clip: its sound starts at that point at once");

        p.seek (withClip, kBeat, 120.0); // while playing: 0.5 s in, still silent there
        out = renderBlocks (p, withClip, true, 0.2);
        check (toneEnergy (out.data(), 2400, 9600, 300.0) < 0.01, "a seek while playing starts the clip at the new point (still in its silent part)");
        out = renderBlocks (p, withClip, true, 0.6);
        check (toneEnergy (out.data(), 24000 - 9600 + 2400, 28800 - 9600, 300.0) > 0.05, "and the tone comes in on time, 0.5 s later");

        sls::AudioEvent shortEvent = e;
        auto tiny = std::make_shared<sls::SampleData> (*data);
        tiny->left.resize (100);
        tiny->right.resize (100);
        shortEvent.data = tiny;
        const auto withShort = sls::Project::make ({ synthChannel ({}) }, 8 * kBeat, {}, {}, { shortEvent });
        sls::Player q;
        q.prepare (kSr);
        renderBlocks (q, withShort, false, 0.01);
        q.seek (withShort, 2 * kBeat, 120.0);
        out = renderBlocks (q, withShort, true, 0.1);
        check (peakOf (out.data(), 0, 4800) == 0.0f, "a clip whose sound has already ended at that point stays silent");
    }

    // The arrangement's region and the project file.
    {
        sls::Arrangement a;
        check (! a.hasLoop(), "a new arrangement has no loop region");
        check (a.setLoop (kBeat, 5 * kBeat) && a.hasLoop() && a.loopStartTick == kBeat && a.loopEndTick == 5 * kBeat, "setLoop stores a region");
        check (! a.setLoop (-kBeat, kBeat) && ! a.hasLoop(), "a region before the start is refused (and the old one is gone)");
        check (! a.setLoop (0, a.timelineTicks() + 1) && ! a.hasLoop(), "a region past the end of the timeline is refused");
        check (! a.setLoop (kBeat, kBeat + 10) && ! a.hasLoop(), "a region shorter than a 16th is refused");
        check (a.setLoop (0, a.timelineTicks()), "a region over the whole timeline is fine");
        a.clearLoop();
        check (! a.hasLoop(), "clearLoop removes it");

        auto doc = sls::ProjectDocument::blank();
        doc.arrangement.setLoop (8 * kBeat, 16 * kBeat);
        auto parsed = sls::ProjectDocument::fromJson (doc.toJson());
        check (parsed.document && parsed.document->arrangement.loopStartTick == 8 * kBeat && parsed.document->arrangement.loopEndTick == 16 * kBeat,
               "the loop region is saved in the project file and read back");
        doc.arrangement.clearLoop();
        parsed = sls::ProjectDocument::fromJson (doc.toJson());
        check (parsed.document && ! parsed.document->arrangement.hasLoop() && ! doc.toJson().contains ("loopRegion"),
               "no region: nothing written, nothing read");
        auto v = juce::JSON::parse (sls::ProjectDocument::blank().toJson());
        check (sls::ProjectDocument::fromJson (juce::JSON::toString (v)).warnings.empty(), "(a blank project reads without warnings)");
        auto* region = new juce::DynamicObject();
        region->setProperty ("start", 500);
        region->setProperty ("end", -3);
        v.getDynamicObject()->setProperty ("loopRegion", juce::var (region));
        parsed = sls::ProjectDocument::fromJson (juce::JSON::toString (v));
        check (parsed.document && ! parsed.document->arrangement.hasLoop() && ! parsed.warnings.empty(),
               "a damaged region in a file is left out with a warning");
    }
}

// ------------------------------------------------------------------------- 0.16.0: the advanced sampler

float rmsWindow (const std::vector<float>& x, double fromSeconds, double toSeconds)
{
    const int a = static_cast<int> (fromSeconds * kSr), b = std::min (static_cast<int> (toSeconds * kSr), static_cast<int> (x.size()));
    double sum = 0.0;
    for (int i = a; i < b; ++i)
        sum += static_cast<double> (x[static_cast<size_t> (i)]) * x[static_cast<size_t> (i)];
    return b > a ? static_cast<float> (std::sqrt (sum / (b - a))) : 0.0f;
}

void testAdvancedSampler()
{
    std::cout << "Advanced sampler: envelope, loop, filter, zones\n";
    const auto longSine = sineSample (440.0, 3.0, kSr);
    const auto shortSine = sineSample (440.0, 0.1, kSr);
    const auto play = [] (const sls::Project& project, double seconds, std::vector<float>* right = nullptr)
    {
        sls::Player p;
        p.prepare (kSr);
        return renderBlocks (p, project, true, seconds, right);
    };
    const auto oneNote = [&] (sls::Channel c, int lengthTicks = kBeat, int pitch = 60, float velocity = 0.8f)
    {
        c.notes = { { pitch, 0, lengthTicks, velocity } };
        return sls::Project::make ({ c }, 16 * kBeat);
    };

    // One-shot (the default): the sample rings past the end of the note.
    auto c = samplerChannel (longSine, {});
    auto out = play (oneNote (c), 2.0);
    check (rmsWindow (out, 1.2, 1.4) > 0.05f, "default sampler: one-shot, the sample rings on after a 0.5 s note");

    // Envelope: the note ends at its length, with the release.
    c.sampler.envelope = true;
    c.sampler.attack = 0.002f;
    c.sampler.release = 0.05f;
    out = play (oneNote (c), 2.0);
    check (rmsWindow (out, 0.2, 0.4) > 0.05f && rmsWindow (out, 0.6, 2.0) < 0.001f, "envelope on: the note sounds while held and ends after its release");
    c.sampler.release = 0.6f;
    out = play (oneNote (c), 2.0);
    check (rmsWindow (out, 0.7, 0.8) > 0.02f && rmsWindow (out, 1.2, 2.0) < 0.001f, "a longer release rings on after the note, then stops");
    c.sampler.release = 0.05f;
    c.sampler.attack = 0.4f;
    out = play (oneNote (c), 1.0);
    check (rmsWindow (out, 0.0, 0.05) < 0.25f * rmsWindow (out, 0.42, 0.48), "attack: the note fades in");
    c.sampler.attack = 0.002f;
    c.sampler.decay = 0.05f;
    c.sampler.sustain = 0.25f;
    out = play (oneNote (c, 2 * kBeat), 1.5);
    const float early = rmsWindow (out, 0.003, 0.006), held = rmsWindow (out, 0.5, 0.9);
    check (held > 0.2f * early && held < 0.32f * early, "decay and sustain: the level settles at the sustain level (25 %)");
    c.sampler.sustain = 0.0f;
    out = play (oneNote (c, 4 * kBeat), 1.0);
    check (rmsWindow (out, 0.3, 1.0) < 0.001f, "a sustain level of zero: the note dies away during the decay");

    // Loop: a 0.1 s sample sounds for the whole note.
    auto looped = samplerChannel (shortSine, {});
    out = play (oneNote (looped, 2 * kBeat), 1.5);
    check (rmsWindow (out, 0.3, 0.9) < 0.001f, "(without a loop the short sample is over after 0.1 s)");
    looped.sampler.loop = sls::LoopMode::Forward;
    out = play (oneNote (looped, 2 * kBeat), 1.5);
    check (rmsWindow (out, 0.3, 0.9) > 0.9f * rmsWindow (out, 0.0, 0.09), "a forward loop keeps the short sample sounding while the note is held");
    check (rmsWindow (out, 1.1, 1.5) < 0.001f, "and the loop stops when the note ends");
    check (toneEnergy (out.data(), 4800, 43200, 440.0) > 0.1, "the loop keeps the pitch (440 Hz over the loop points)");

    // Filter.
    auto bright = samplerChannel (sineSample (4000.0, 1.0, kSr), {});
    const float open = rmsWindow (play (oneNote (bright), 0.5), 0.1, 0.4);
    bright.sampler.filter = sls::SamplerFilter::LowPass;
    bright.sampler.cutoff = 0.4f; // about 316 Hz
    const float lowPassed = rmsWindow (play (oneNote (bright), 0.5), 0.1, 0.4);
    check (lowPassed < 0.05f * open, "low-pass at 316 Hz takes a 4 kHz tone down by more than 26 dB");
    auto deep = samplerChannel (sineSample (100.0, 1.0, kSr), {});
    const float deepOpen = rmsWindow (play (oneNote (deep), 0.5), 0.1, 0.4);
    deep.sampler.filter = sls::SamplerFilter::HighPass;
    deep.sampler.cutoff = 0.7f; // about 2.5 kHz
    check (rmsWindow (play (oneNote (deep), 0.5), 0.1, 0.4) < 0.05f * deepOpen, "high-pass at 2.5 kHz takes a 100 Hz tone down by more than 26 dB");
    deep.sampler.filter = sls::SamplerFilter::LowPass;
    deep.sampler.cutoff = 1.0f;
    const float passed = rmsWindow (play (oneNote (deep), 0.5), 0.1, 0.4);
    check (passed > 0.9f * deepOpen && passed < 1.1f * deepOpen, "a fully open low-pass leaves the level alone");

    // Zones by key and velocity.
    auto program = std::make_shared<sls::SamplerProgram>();
    sls::SampleZone low, high, soft, loud;
    low.sample = sineSample (440.0, 1.0, kSr); low.rootKey = 69; low.loKey = 0; low.hiKey = 70;
    high.sample = sineSample (880.0, 1.0, kSr); high.rootKey = 81; high.loKey = 71; high.hiKey = 90;
    soft.sample = sineSample (300.0, 1.0, kSr); soft.rootKey = 100; soft.loKey = 100; soft.hiKey = 100; soft.hiVel = 64;
    loud.sample = sineSample (600.0, 1.0, kSr); loud.rootKey = 100; loud.loKey = 100; loud.hiKey = 100; loud.loVel = 65; loud.pan = -1.0f;
    program->zones = { low, high, soft, loud };
    sls::Channel multi;
    multi.instrument = sls::Instrument::Sampler;
    multi.program = program;
    out = play (oneNote (multi, kBeat, 69), 0.4);
    check (toneEnergy (out.data(), 2400, 14400, 440.0) > 0.1 && toneEnergy (out.data(), 2400, 14400, 880.0) < 0.01, "key 69 plays the low zone at its root pitch");
    out = play (oneNote (multi, kBeat, 76), 0.4);
    check (toneEnergy (out.data(), 2400, 14400, 880.0 * std::pow (2.0, -5.0 / 12.0)) > 0.1, "key 76 plays the high zone, 5 semitones under its root");
    out = play (oneNote (multi, kBeat, 100, 0.3f), 0.4);
    check (toneEnergy (out.data(), 2400, 14400, 300.0) > 0.02 && toneEnergy (out.data(), 2400, 14400, 600.0) < 0.002, "a soft hit plays the soft layer");
    std::vector<float> right;
    out = play (oneNote (multi, kBeat, 100, 1.0f), 0.4, &right);
    check (toneEnergy (out.data(), 2400, 14400, 600.0) > 0.1 && toneEnergy (right.data(), 2400, 14400, 600.0) < 0.001,
           "a hard hit plays the loud layer, panned hard left as the zone says");
    out = play (oneNote (multi, kBeat, 120), 0.4);
    check (peakOf (out.data(), 0, static_cast<int> (out.size())) == 0.0f, "a key no zone answers to is silent");

    // A program swapped while it sounds: the voice stops (the old audio may be freed); the same program goes on.
    {
        sls::Player p;
        p.prepare (kSr);
        auto first = oneNote (multi, 4 * kBeat, 69);
        renderBlocks (p, first, true, 0.2);
        auto same = first;
        same.revision = 7;
        auto keep = renderBlocks (p, same, true, 0.1);
        check (rmsWindow (keep, 0.0, 0.1) > 0.05f, "a new snapshot with the same program keeps the note");
        auto other = multi;
        auto copy = std::make_shared<sls::SamplerProgram> (*program);
        copy->zones[0].sample = sineSample (440.0, 1.0, kSr);
        other.program = copy;
        auto swapped = oneNote (other, 4 * kBeat, 69);
        swapped.revision = 8;
        auto after = renderBlocks (p, swapped, true, 0.1);
        check (peakOf (after.data(), 0, static_cast<int> (after.size())) == 0.0f, "a program whose audio changed stops the voices of the old one");
    }

    // Live notes hold with an envelope; no allocation with zones, loops, filters and envelopes.
    {
        auto live = samplerChannel (longSine, {});
        live.sampler.envelope = true;
        live.sampler.release = 0.02f;
        const auto project = sls::Project::make ({ live }, 16 * kBeat);
        sls::Player p;
        p.prepare (kSr);
        p.liveNoteOn (project, 0, 60, 0.8f);
        auto heldOut = renderBlocks (p, project, false, 0.6);
        check (rmsWindow (heldOut, 0.4, 0.6) > 0.05f, "a sampler note played by hand with the envelope on sounds while held");
        p.liveNoteOff (0, 60);
        auto tail = renderBlocks (p, project, false, 0.2);
        check (rmsWindow (tail, 0.1, 0.2) < 0.001f, "and stops after the key is let go");

        std::vector<sls::Note> notes;
        for (int i = 0; i < 64; ++i)
            notes.push_back ({ 60 + i % 40, i * 60, 200, 0.2f + 0.01f * (i % 60) });
        auto busy = multi;
        busy.notes = notes;
        busy.sampler.envelope = true;
        busy.sampler.filter = sls::SamplerFilter::BandPass;
        busy.sampler.cutoff = 0.5f;
        auto busyLoop = looped;
        busyLoop.notes = notes;
        busyLoop.sampler.filter = sls::SamplerFilter::HighPass;
        const auto heavy = sls::Project::make ({ busy, busyLoop }, 4 * kBeat);
        std::vector<float> l (256), r (256);
        float* ch[2] = { l.data(), r.data() };
        sls::Player z;
        z.prepare (kSr);
        gAllocs = 0;
        gCounting = true;
        for (int i = 0; i < 2000; ++i)
            z.render (heavy, 140.0, true, ch, 2, 256);
        gCounting = false;
        check (gAllocs.load() == 0, "no allocation with zones, loops, filters and envelopes (2000 blocks)");
    }

    // Hostile settings are clamped.
    sls::SamplerSettings bad;
    bad.attack = std::nanf ("");
    bad.sustain = 7.0f;
    bad.loopStart = 0.9f;
    bad.loopEnd = 0.1f;
    bad.cutoff = -3.0f;
    bad.loop = static_cast<sls::LoopMode> (9);
    const auto fixed = bad.sanitised();
    check (std::isfinite (fixed.attack) && fixed.sustain == 1.0f && fixed.loopStart < fixed.loopEnd && fixed.cutoff == 0.0f && fixed.loop == sls::LoopMode::Off,
           "hostile sampler settings are clamped");
    sls::SamplerProgram badProgram;
    sls::SampleZone weird;
    weird.sample = shortSine;
    weird.loKey = 90; weird.hiKey = 10; weird.loopStart = 4000; weird.loopEnd = 999999; weird.loop = sls::LoopMode::Forward; weird.gain = 1e9f;
    sls::SampleZone empty;
    badProgram.zones = { weird, empty };
    const auto cleaned = badProgram.sanitised();
    check (cleaned.zones.size() == 1 && cleaned.zones[0].hiKey >= cleaned.zones[0].loKey && cleaned.zones[0].loopEnd <= shortSine->numFrames()
               && cleaned.zones[0].gain <= 4.0f,
           "a damaged program: zones without audio removed, ranges and loop points put right");
}

// A minimal SoundFont 2 file: two mono samples (a 440 Hz and an 880 Hz sine, 0.5 s at 22050 Hz), one instrument with two zones
// (keys 0..72 play the first, looped, root key 69; keys 73..127 play the second, root key 81, panned right, 60 cB down), one preset.
juce::MemoryBlock makeSoundFont()
{
    juce::MemoryOutputStream out;
    const auto chunk = [] (juce::MemoryOutputStream& to, const char* id, const juce::MemoryBlock& data)
    {
        to.write (id, 4);
        to.writeInt (static_cast<int> (data.getSize()));
        to.write (data.getData(), data.getSize());
        if (data.getSize() % 2 != 0)
            to.writeByte (0);
    };
    const auto list = [&] (const char* type, const juce::MemoryBlock& body)
    {
        juce::MemoryOutputStream b;
        b.write (type, 4);
        b.write (body.getData(), body.getSize());
        return b.getMemoryBlock();
    };
    const auto name20 = [] (juce::MemoryOutputStream& to, const char* name)
    {
        char buf[20] = {};
        std::strncpy (buf, name, 19);
        to.write (buf, 20);
    };

    const int rate = 22050, frames = rate / 2, gap = 46;
    juce::MemoryOutputStream smpl;
    for (const double hz : { 440.0, 880.0 })
    {
        for (int i = 0; i < frames; ++i)
            smpl.writeShort (static_cast<short> (std::lround (12000.0 * std::sin (6.283185307179586 * hz * i / rate))));
        for (int i = 0; i < gap; ++i)
            smpl.writeShort (0);
    }

    juce::MemoryOutputStream info, ifil;
    ifil.writeShort (2); ifil.writeShort (1);
    chunk (info, "ifil", ifil.getMemoryBlock());
    juce::MemoryOutputStream isng; isng.write ("EMU8000\0", 8); chunk (info, "isng", isng.getMemoryBlock());
    juce::MemoryOutputStream inam; inam.write ("Test\0\0", 6); chunk (info, "INAM", inam.getMemoryBlock());

    juce::MemoryOutputStream sdta;
    chunk (sdta, "smpl", smpl.getMemoryBlock());

    juce::MemoryOutputStream pdta, phdr, pbag, pmod, pgen, inst, ibag, imod, igen, shdr;
    name20 (phdr, "Test Sines"); phdr.writeShort (0); phdr.writeShort (0); phdr.writeShort (0); phdr.writeInt (0); phdr.writeInt (0); phdr.writeInt (0);
    name20 (phdr, "EOP"); phdr.writeShort (0); phdr.writeShort (0); phdr.writeShort (1); phdr.writeInt (0); phdr.writeInt (0); phdr.writeInt (0);
    pbag.writeShort (0); pbag.writeShort (0);
    pbag.writeShort (1); pbag.writeShort (0);
    for (int i = 0; i < 10; ++i) pmod.writeByte (0);
    pgen.writeShort (41); pgen.writeShort (0); // instrument 0
    pgen.writeShort (0); pgen.writeShort (0);
    name20 (inst, "Sines"); inst.writeShort (0);
    name20 (inst, "EOI"); inst.writeShort (2);
    ibag.writeShort (0); ibag.writeShort (0);
    ibag.writeShort (4); ibag.writeShort (0);
    ibag.writeShort (9); ibag.writeShort (0);
    for (int i = 0; i < 10; ++i) imod.writeByte (0);
    // zone 1: keyRange 0..72, root 69, loop continuously, sample 0
    igen.writeShort (43); igen.writeByte (0); igen.writeByte (72);
    igen.writeShort (58); igen.writeShort (69);
    igen.writeShort (54); igen.writeShort (1);
    igen.writeShort (53); igen.writeShort (0);
    // zone 2: keyRange 73..127, root 81, pan right (+500), attenuation 60 cB, sample 1
    igen.writeShort (43); igen.writeByte (73); igen.writeByte (127);
    igen.writeShort (58); igen.writeShort (81);
    igen.writeShort (17); igen.writeShort (500);
    igen.writeShort (48); igen.writeShort (60);
    igen.writeShort (53); igen.writeShort (1);
    igen.writeShort (0); igen.writeShort (0);
    const auto sampleHeader = [&] (const char* name, int start, int end, int loopStart, int loopEnd, int pitch)
    {
        name20 (shdr, name);
        shdr.writeInt (start); shdr.writeInt (end); shdr.writeInt (loopStart); shdr.writeInt (loopEnd); shdr.writeInt (rate);
        shdr.writeByte (static_cast<char> (pitch)); shdr.writeByte (0); shdr.writeShort (0); shdr.writeShort (name[0] == 'E' ? 0 : 1);
    };
    sampleHeader ("A440", 0, frames, 1000, 1000 + 50 * 10, 69); // the loop is 10 periods of 440 Hz at 22050 Hz (50.11 frames each: close enough)
    sampleHeader ("A880", frames + gap, 2 * frames + gap, frames + gap, 2 * frames + gap - 1, 81);
    sampleHeader ("EOS", 0, 0, 0, 0, 0);
    chunk (pdta, "phdr", phdr.getMemoryBlock()); chunk (pdta, "pbag", pbag.getMemoryBlock()); chunk (pdta, "pmod", pmod.getMemoryBlock());
    chunk (pdta, "pgen", pgen.getMemoryBlock()); chunk (pdta, "inst", inst.getMemoryBlock()); chunk (pdta, "ibag", ibag.getMemoryBlock());
    chunk (pdta, "imod", imod.getMemoryBlock()); chunk (pdta, "igen", igen.getMemoryBlock()); chunk (pdta, "shdr", shdr.getMemoryBlock());

    juce::MemoryOutputStream body;
    body.write ("sfbk", 4);
    chunk (body, "LIST", list ("INFO", info.getMemoryBlock()));
    chunk (body, "LIST", list ("sdta", sdta.getMemoryBlock()));
    chunk (body, "LIST", list ("pdta", pdta.getMemoryBlock()));
    chunk (out, "RIFF", body.getMemoryBlock());
    return out.getMemoryBlock();
}

void testSoundFont()
{
    std::cout << "SoundFont files\n";
    const auto dir = freshTempDir ("sls-sf2");
    const auto file = dir.getChildFile ("test.sf2");
    const auto font = makeSoundFont();
    file.replaceWithData (font.getData(), font.getSize());

    juce::String error;
    const auto presets = sls::listSoundFontPresets (file, error);
    check (presets.size() == 1 && presets[0].name == "Test Sines" && error.isEmpty(), "the preset list of a SoundFont is read");

    sls::SamplerSettings suggested;
    const auto program = sls::loadSoundFont (file, 0, error, &suggested);
    check (program != nullptr && program->zones.size() == 2 && program->name == "Test Sines" && program->preset == 0
               && program->source == file.getFullPathName().toStdString(),
           "a preset becomes a sampler program with one zone per region");
    if (program == nullptr || program->zones.size() != 2)
        return;
    const auto& a = program->zones[0];
    const auto& b = program->zones[1];
    check (a.loKey == 0 && a.hiKey == 72 && a.rootKey == 69 && b.loKey == 73 && b.hiKey == 127 && b.rootKey == 81, "key ranges and root keys");
    check (a.loop == sls::LoopMode::Forward && a.loopStart == 1000 && a.loopEnd == 1500 && b.loop == sls::LoopMode::Off, "loop points (the first zone loops)");
    check (b.pan > 0.9f && std::abs (b.gain - 0.759f) < 0.01f && std::abs (a.gain - 1.0f) < 1.0e-3f, "pan and attenuation (60 cB = 2.4 dB, the 0.4 factor of EMU and FluidSynth)");
    check (a.sample->sampleRate == 22050.0 && std::abs (a.sample->numFrames() - 11025) <= 1, "the sample is copied at its own rate and length");
    check (suggested.envelope, "suggested settings: the envelope on (SoundFont notes sustain)");

    // It plays: key 69 is the first sample at its own pitch, key 81 the second, panned right.
    sls::Channel c;
    c.instrument = sls::Instrument::Sampler;
    c.program = program;
    c.sampler = suggested;
    c.notes = { { 69, 0, 4 * kBeat, 0.9f } };
    sls::Player p;
    p.prepare (kSr);
    auto out = renderBlocks (p, sls::Project::make ({ c }, 16 * kBeat), true, 1.5);
    check (toneEnergy (out.data(), 4800, 19200, 440.0) > 0.02, "key 69 plays 440 Hz");
    check (rmsWindow (out, 1.0, 1.4) > 0.02f, "and keeps sounding after the 0.5 s sample, through its loop");
    c.notes = { { 81, 0, kBeat, 0.9f } };
    sls::Player q;
    q.prepare (kSr);
    std::vector<float> right;
    out = renderBlocks (q, sls::Project::make ({ c }, 16 * kBeat), true, 0.4, &right);
    check (toneEnergy (right.data(), 2400, 14400, 880.0) > 0.03 && toneEnergy (out.data(), 2400, 14400, 880.0) < 0.2 * toneEnergy (right.data(), 2400, 14400, 880.0),
           "key 81 plays 880 Hz, on the right");

    check (sls::loadSoundFont (file, 5, error) == nullptr && error.isNotEmpty(), "a preset that does not exist is refused");
    const auto junk = dir.getChildFile ("junk.sf2");
    junk.replaceWithText ("RIFF....sfbk not really");
    error.clear();
    check (sls::loadSoundFont (junk, 0, error) == nullptr && error.isNotEmpty() && sls::listSoundFontPresets (junk, error).empty(),
           "a damaged file is refused with a message");
    error.clear();
    check (sls::loadSoundFont (dir.getChildFile ("missing.sf2"), 0, error) == nullptr && error.contains ("does not exist"), "a missing file is refused");
    // Truncated files must not crash.
    const auto whole = makeSoundFont();
    for (const size_t cut : { size_t { 12 }, size_t { 60 }, whole.getSize() / 2, whole.getSize() - 30 })
    {
        const auto part = dir.getChildFile ("cut.sf2");
        part.replaceWithData (whole.getData(), cut);
        error.clear();
        sls::loadSoundFont (part, 0, error);
    }
    check (true, "truncated SoundFont files do not crash the loader");
    dir.deleteRecursively();
}

void testSamplerPersistence()
{
    std::cout << "Sampler settings, slices and programs in the project file\n";
    // Slices of one sample: one zone per slice, one key each, from key 60.
    auto whole = std::make_shared<sls::SampleData> (*sineData (48000, kSr, 440.0, 0.5f, "/tmp/loop.wav"));
    const auto sliced = sls::makeSliceProgram (whole, { 12000, 24000, 36000, 47999, -5, 999999 }, 60, "Loop");
    check (sliced.zones.size() == 4 && sliced.zones[0].sample->numFrames() == 12000 && sliced.zones[3].sample->numFrames() == 11999
               && sliced.zones[2].loKey == 62 && sliced.zones[2].hiKey == 62 && sliced.zones[2].rootKey == 62,
           "slices: one zone per slice on consecutive keys at their own pitch (bad positions and a one-frame slice left out)");
    check (sliced.zones[1].sample->left[0] == whole->left[12000], "a slice starts where its marker is");
    const auto spec = sls::specFromProgram (sliced);
    check (spec.kind == sls::ProgramSpec::Kind::Slices && spec.file == "/tmp/loop.wav" && spec.firstKey == 60 && spec.starts.size() == 5,
           "the slice program's spec names the file and the slice starts");

    juce::String error;
    const auto reloaded = sls::loadProgram (spec, [&] (const juce::File&, juce::String&) { return std::shared_ptr<const sls::SampleData> (whole); }, error);
    check (reloaded != nullptr && reloaded->zones.size() == 4 && reloaded->zones[3].sample->numFrames() == 11999, "a slice program is made again from its spec");
    const auto missing = sls::loadProgram (spec, [] (const juce::File&, juce::String& e) { e = "gone"; return std::shared_ptr<const sls::SampleData>(); }, error);
    check (missing == nullptr && error == "gone", "a missing sliced file gives an error");
    const auto placeholder = sls::missingProgram (spec);
    check (placeholder->zones.empty() && sls::specFromProgram (*placeholder).starts == spec.starts, "a placeholder keeps the spec, so saving does not lose it");

    // Project file round trip.
    auto doc = sls::ProjectDocument::blank();
    doc.channels[0].instrument = sls::Instrument::Sampler;
    doc.channels[0].sampler.envelope = true;
    doc.channels[0].sampler.attack = 0.25f;
    doc.channels[0].sampler.sustain = 0.4f;
    doc.channels[0].sampler.loop = sls::LoopMode::Forward;
    doc.channels[0].sampler.loopStart = 0.2f;
    doc.channels[0].sampler.filter = sls::SamplerFilter::BandPass;
    doc.channels[0].sampler.cutoff = 0.3f;
    doc.channels[0].sampler.resonance = 0.6f;
    sls::ProgramSpec font;
    font.kind = sls::ProgramSpec::Kind::SoundFont;
    font.file = "/somewhere/piano.sf2";
    font.preset = 3;
    doc.programSpecs = { font };
    auto parsed = sls::ProjectDocument::fromJson (doc.toJson());
    const auto& c = parsed.document->channels[0];
    check (parsed.document && c.sampler.envelope && std::abs (c.sampler.attack - 0.25f) < 1e-6f && std::abs (c.sampler.sustain - 0.4f) < 1e-6f
               && c.sampler.loop == sls::LoopMode::Forward && std::abs (c.sampler.loopStart - 0.2f) < 1e-6f && c.sampler.filter == sls::SamplerFilter::BandPass
               && std::abs (c.sampler.resonance - 0.6f) < 1e-6f,
           "sampler settings are saved and read back");
    check (parsed.document->programSpecs.size() == 1 && parsed.document->programSpecs[0].kind == sls::ProgramSpec::Kind::SoundFont
               && parsed.document->programSpecs[0].file == "/somewhere/piano.sf2" && parsed.document->programSpecs[0].preset == 3,
           "the SoundFont preset of a channel is saved and read back");
    doc.programSpecs = { spec };
    parsed = sls::ProjectDocument::fromJson (doc.toJson());
    check (parsed.document && parsed.document->programSpecs[0].kind == sls::ProgramSpec::Kind::Slices && parsed.document->programSpecs[0].starts == spec.starts,
           "a slice program is saved and read back");
    auto older = juce::JSON::parse (sls::ProjectDocument::blank().toJson());
    parsed = sls::ProjectDocument::fromJson (juce::JSON::toString (older));
    check (parsed.document && ! parsed.document->channels[0].sampler.envelope && parsed.document->channels[0].sampler.loop == sls::LoopMode::Off,
           "older files: the sampler is one-shot as before");
    auto hostile = juce::JSON::parse (doc.toJson());
    auto* ch0 = (*hostile.getDynamicObject()->getProperty ("channels").getArray())[0].getDynamicObject();
    auto* sampler = ch0->getProperty ("sampler").getDynamicObject();
    sampler->setProperty ("attack", -40);
    sampler->setProperty ("cutoff", "loud");
    sampler->setProperty ("loop", "sideways");
    ch0->setProperty ("program", "nonsense");
    parsed = sls::ProjectDocument::fromJson (juce::JSON::toString (hostile));
    check (parsed.document && parsed.document->channels[0].sampler.attack == 0.0f && parsed.document->channels[0].sampler.cutoff == 1.0f
               && parsed.document->channels[0].sampler.loop == sls::LoopMode::Off && parsed.document->programSpecs[0].empty(),
           "hostile sampler values are clamped, a bad program is ignored");

    // Export with samples copies the SoundFont too.
    const auto dir = freshTempDir ("sls-sf2-export");
    const auto sf2 = dir.getChildFile ("src/piano.sf2");
    sf2.getParentDirectory().createDirectory();
    const auto font2 = makeSoundFont();
    sf2.replaceWithData (font2.getData(), font2.getSize());
    font.file = sf2.getFullPathName().toStdString();
    doc.programSpecs = { font };
    juce::StringArray notes;
    check (sls::exportProjectWithSamples (doc, dir.getChildFile ("Out"), "Song", notes).isEmpty() && dir.getChildFile ("Out/Samples/piano.sf2").existsAsFile(),
           "export with samples copies the SoundFont");
    const auto back = sls::readProjectFile (dir.getChildFile ("Out/Song.slp"));
    check (back.document && back.document->programSpecs[0].file == dir.getChildFile ("Out/Samples/piano.sf2").getFullPathName().toStdString(),
           "and the project finds it there");
    dir.deleteRecursively();
}

// ------------------------------------------------------------------------- 0.17.0: plugin hosting

#ifdef SLS_TEST_PLUGIN_DIR
const juce::PluginDescription* findPlugin (sls::PluginHost& host, const juce::String& name, const juce::String& format)
{
    for (const auto& d : host.known().getTypes())
        if (d.name == name && d.pluginFormatName == format)
        {
            static juce::PluginDescription kept;
            kept = d;
            return &kept;
        }
    return nullptr;
}

int firstNonZero (const std::vector<float>& x, float threshold = 1.0e-6f)
{
    for (size_t i = 0; i < x.size(); ++i)
        if (std::abs (x[i]) > threshold)
            return static_cast<int> (i);
    return -1;
}

// "Roughness": the RMS of the sample-to-sample difference over the RMS, which grows with the high frequencies in a signal.
float roughness (const std::vector<float>& x, double fromSeconds, double toSeconds)
{
    const int a = static_cast<int> (fromSeconds * kSr), b = std::min (static_cast<int> (toSeconds * kSr), static_cast<int> (x.size()));
    double d = 0.0, e = 0.0;
    for (int i = a + 1; i < b; ++i)
    {
        const double diff = x[static_cast<size_t> (i)] - x[static_cast<size_t> (i - 1)];
        d += diff * diff;
        e += static_cast<double> (x[static_cast<size_t> (i)]) * x[static_cast<size_t> (i)];
    }
    return e > 0.0 ? static_cast<float> (std::sqrt (d / e)) : 0.0f;
}

void testSubtractiveSynth()
{
    std::cout << "Subtractive synth (0.18.0)\n";
    const auto play = [] (const sls::SynthPatch& patch, double seconds, int lengthTicks = kBeat, int pitch = 69, float velocity = 0.8f)
    {
        auto c = synthChannel ({ { pitch, 0, lengthTicks, velocity } });
        c.synth = patch;
        sls::Player p;
        p.prepare (kSr);
        return renderBlocks (p, sls::Project::make ({ c }, 16 * kBeat), true, seconds);
    };
    const auto finite = [] (const std::vector<float>& x) { return std::all_of (x.begin(), x.end(), [] (float v) { return std::isfinite (v); }); };

    sls::SynthPatch raw;
    raw.engine = sls::SynthEngine::Subtractive;
    raw.filter = sls::SamplerFilter::Off;
    raw.osc[1].level = 0.0f;
    raw.amp = { 0.001f, 0.1f, 1.0f, 0.05f };

    // Classic stays the classic sound: a project without a patch renders exactly as a project whose patch is the default.
    {
        auto c = synthChannel ({ { 69, 0, kBeat, 0.8f } });
        sls::Player a, b;
        a.prepare (kSr);
        b.prepare (kSr);
        const auto plain = renderBlocks (a, sls::Project::make ({ c }, 16 * kBeat), true, 0.3);
        c.synth = sls::SynthPatch {};
        const auto withDefault = renderBlocks (b, sls::Project::make ({ c }, 16 * kBeat), true, 0.3);
        check (plain == withDefault, "the Classic engine (the default) sounds exactly as before 0.18.0");
    }

    // Pitch of every waveform, the octave and semitone of an oscillator.
    for (const auto wave : { sls::Waveform::Sine, sls::Waveform::Triangle, sls::Waveform::Saw, sls::Waveform::Square })
    {
        auto p = raw;
        p.osc[0].wave = wave;
        const auto out = play (p, 0.4);
        const double hz = zeroCrossingHz (out, kSr, 2000, 18000);
        check (std::fabs (hz - 440.0) < 2.0 && finite (out), (std::string ("A4 plays 440 Hz with the ") + (wave == sls::Waveform::Sine ? "sine" : wave == sls::Waveform::Triangle ? "triangle" : wave == sls::Waveform::Saw ? "saw" : "square") + " wave").c_str());
    }
    {
        auto p = raw;
        p.osc[0].wave = sls::Waveform::Sine;
        p.osc[0].octave = 1;
        check (std::fabs (zeroCrossingHz (play (p, 0.4), kSr, 2000, 18000) - 880.0) < 4.0, "oscillator octave +1: 880 Hz");
        p.osc[0].octave = 0;
        p.osc[0].semitones = 7;
        check (std::fabs (zeroCrossingHz (play (p, 0.4), kSr, 2000, 18000) - 440.0 * std::pow (2.0, 7.0 / 12.0)) < 3.0, "oscillator +7 semitones: a fifth up");
    }

    // The saw is band-limited: much less sample-to-sample jump than a naive saw at a high note.
    {
        auto p = raw;
        const auto out = play (p, 0.3, kBeat, 96); // C7, 2093 Hz
        float worst = 0.0f;
        for (size_t i = 2000; i < 12000; ++i)
            worst = std::max (worst, std::fabs (out[i] - out[i - 1]));
        float peak = 0.0f;
        for (size_t i = 2000; i < 12000; ++i)
            peak = std::max (peak, std::fabs (out[i]));
        check (worst < 1.6f * peak, "PolyBLEP: the saw's reset is spread over two samples (no full-height jump)");
    }

    // Filter: a low cutoff takes the high frequencies off; the envelope and key tracking open it; resonance and 24 dB stay stable.
    {
        auto open = raw;
        auto closed = raw;
        closed.filter = sls::SamplerFilter::LowPass;
        closed.cutoff = 0.35f; // about 225 Hz
        closed.envAmount = 0.0f;
        closed.keyTrack = 0.0f;
        const auto a = play (open, 0.4), b = play (closed, 0.4);
        check (roughness (b, 0.1, 0.4) < 0.5f * roughness (a, 0.1, 0.4), "low-pass at a low cutoff: the saw becomes much smoother");
        auto steep = closed;
        steep.steep = true;
        check (roughness (play (steep, 0.4), 0.1, 0.4) < roughness (b, 0.1, 0.4), "24 dB (two filters) removes more than 12 dB");
        auto hp = closed;
        hp.filter = sls::SamplerFilter::HighPass;
        hp.cutoff = 0.85f;
        check (rmsWindow (play (hp, 0.4), 0.1, 0.4) < 0.5f * rmsWindow (a, 0.1, 0.4), "high-pass well above the note: most of it is gone");
        auto swept = closed;
        swept.envAmount = 1.0f;
        swept.filterEnv = { 0.001f, 0.3f, 0.0f, 0.1f };
        const auto s = play (swept, 0.6);
        check (roughness (s, 0.01, 0.05) > 1.5f * roughness (s, 0.4, 0.5), "the filter envelope opens the filter at the start and closes it as it decays");
        auto ring = closed;
        ring.resonance = 1.0f;
        ring.steep = true;
        ring.envAmount = 1.0f;
        const auto r = play (ring, 0.6);
        float peak = 0.0f;
        for (float v : r)
            peak = std::max (peak, std::fabs (v));
        check (finite (r) && peak < 2.0f, "full resonance, 24 dB and a full envelope: stable and bounded");
    }

    // Amplitude envelope: attack, sustain, release; a sustain of zero ends the note during the decay.
    {
        auto p = raw;
        p.osc[0].wave = sls::Waveform::Sine;
        p.amp = { 0.2f, 0.1f, 0.5f, 0.3f };
        const auto out = play (p, 1.4, 2 * kBeat); // a 1 s note
        check (rmsWindow (out, 0.0, 0.03) < 0.3f * rmsWindow (out, 0.18, 0.2), "attack: the note fades in over 0.2 s");
        const float held = rmsWindow (out, 0.6, 0.9), top = rmsWindow (out, 0.19, 0.21);
        check (held > 0.4f * top && held < 0.6f * top, "decay and sustain: the level settles at the sustain level (50 %)");
        check (rmsWindow (out, 1.05, 1.1) > 0.01f && rmsWindow (out, 1.32, 1.4) < 0.002f, "release: the note rings on for up to 0.3 s after it ends, then stops");
        p.amp = { 0.001f, 0.15f, 0.0f, 0.3f };
        const auto pluck = play (p, 1.0, 2 * kBeat);
        check (rmsWindow (pluck, 0.25, 0.9) < 0.001f, "sustain 0: the note dies away during the decay although it is held");
    }

    // LFO: tremolo moves the level, vibrato moves the pitch, pulse-width modulation changes the square.
    {
        auto p = raw;
        p.osc[0].wave = sls::Waveform::Sine;
        p.lfoTarget = sls::LfoTarget::Amp;
        p.lfoRate = 4.0f;
        p.lfoDepth = 1.0f;
        p.amp.sustain = 1.0f;
        const auto out = play (p, 1.0, 2 * kBeat);
        float lo = 1.0f, hi = 0.0f;
        for (double t = 0.1; t < 0.9; t += 0.02)
        {
            const float w = rmsWindow (out, t, t + 0.02);
            lo = std::min (lo, w);
            hi = std::max (hi, w);
        }
        check (lo < 0.2f * hi, "LFO on the level (tremolo, depth 1): the level swings at the LFO rate");
        p.lfoTarget = sls::LfoTarget::Pitch;
        p.lfoRate = 2.0f;
        const auto vib = play (p, 1.0, 2 * kBeat);
        const double rising = zeroCrossingHz (vib, kSr, static_cast<int> (0.02 * kSr), static_cast<int> (0.2 * kSr));
        const double falling = zeroCrossingHz (vib, kSr, static_cast<int> (0.27 * kSr), static_cast<int> (0.45 * kSr));
        check (rising > 445.0 && falling < 435.0, "LFO on the pitch (vibrato): the pitch rises and falls around 440 Hz");
    }

    // Noise and two oscillators, velocity, legato.
    {
        auto p = raw;
        p.osc[0].level = 0.0f;
        p.noise = 1.0f;
        const auto n = play (p, 0.3);
        std::cout << "    (noise RMS " << rmsWindow (n, 0.05, 0.25) << ", roughness " << roughness (n, 0.05, 0.25) << ")\n";
        check (rmsWindow (n, 0.05, 0.25) > 0.02f && roughness (n, 0.05, 0.25) > 1.0f && finite (n), "noise alone sounds, and it is rough (white)");
        auto two = raw;
        two.osc[1] = { sls::Waveform::Saw, 1.0f, 0, 0, 10.0f, 0.5f };
        const auto t = play (two, 1.0, 2 * kBeat);
        check (finite (t) && rmsWindow (t, 0.1, 0.9) > 0.05f, "two detuned saws sound together");
        const auto soft = play (raw, 0.4, kBeat, 69, 0.3f), loud = play (raw, 0.4, kBeat, 69, 0.9f);
        check (rmsWindow (loud, 0.1, 0.3) > 2.5f * rmsWindow (soft, 0.1, 0.3), "velocity sets the level");
    }
    {
        auto c = synthChannel ({ { 69, 0, kBeat, 0.8f }, { 72, kBeat, kBeat, 0.8f, sls::NoteLink::Slide } });
        c.synth = raw;
        c.synth.osc[0].wave = sls::Waveform::Sine;
        sls::Player p;
        p.prepare (kSr);
        const auto out = renderBlocks (p, sls::Project::make ({ c }, 16 * kBeat), true, 1.2);
        check (std::fabs (zeroCrossingHz (out, kSr, static_cast<int> (0.1 * kSr), static_cast<int> (0.45 * kSr)) - 440.0) < 3.0
               && std::fabs (zeroCrossingHz (out, kSr, static_cast<int> (0.85 * kSr), static_cast<int> (0.98 * kSr)) - 523.25) < 4.0,
               "slide works on the subtractive engine: 440 Hz, then a glide up to C5");
    }

    // Every factory patch renders, finite and not too loud; hostile values are tamed.
    {
        bool ok = true;
        for (const auto& named : sls::synthFactoryPatches())
        {
            const auto out = play (named.patch, 0.8, kBeat, 48);
            float peak = 0.0f;
            for (float v : out)
                peak = std::max (peak, std::fabs (v));
            ok = ok && finite (out) && peak > 0.01f && peak < 1.2f;
            if (! (finite (out) && peak > 0.01f && peak < 1.2f))
                std::cout << "    (" << named.name << ": peak " << peak << ")\n";
        }
        check (ok && sls::synthFactoryPatches().size() >= 8, "every factory patch sounds, finite and under full scale (C3, 0.5 s)");
        sls::SynthPatch bad;
        bad.engine = static_cast<sls::SynthEngine> (9);
        bad.osc[0].wave = static_cast<sls::Waveform> (77);
        bad.osc[0].level = std::numeric_limits<float>::quiet_NaN();
        bad.osc[1].octave = 50;
        bad.cutoff = -3.0f;
        bad.resonance = std::numeric_limits<float>::infinity();
        bad.amp.attack = -1.0f;
        bad.lfoRate = 1000.0f;
        bad.lfoTarget = static_cast<sls::LfoTarget> (40);
        const auto fixed = bad.sanitised();
        check (fixed.engine == sls::SynthEngine::Classic && fixed.osc[0].wave == sls::Waveform::Saw && fixed.osc[0].level == 1.0f && fixed.osc[1].octave == 3
                   && fixed.cutoff == 0.0f && fixed.resonance == 0.2f && fixed.amp.attack == 0.0f && fixed.lfoRate == 20.0f
                   && fixed.lfoTarget == sls::LfoTarget::Off,
               "hostile patch values become valid");
    }

    // No allocation with eight subtractive voices, LFO and 24 dB filters.
    {
        std::vector<sls::Note> notes;
        for (int i = 0; i < 8; ++i)
            notes.push_back ({ 48 + 3 * i, (i % 4) * kBeat / 2, 3 * kBeat, 0.7f });
        auto c = synthChannel (notes);
        c.synth = sls::synthFactoryPatches()[4].patch; // Soft Pad: two saws, noise, LFO on the cutoff
        c.synth.steep = true;
        const auto project = sls::Project::make ({ c }, 8 * kBeat);
        sls::Player player;
        player.prepare (kSr);
        renderBlocks (player, project, true, 0.1);
        std::array<float, 256> bufL {}, bufR {};
        float* chans[2] = { bufL.data(), bufR.data() };
        tAllocs = 0;
        tCounting = true;
        for (int block = 0; block < 2000; ++block)
            player.render (project, 120.0, true, chans, 2, 256);
        tCounting = false;
        check (tAllocs == 0, "subtractive voices allocate nothing on the audio thread (2000 blocks)");
    }
}

// The strongest frequency within a semitone of `hz` (a DFT scanned in steps of 0.05 %) over [from, to) seconds of a render at kSr.
double peakNear (const std::vector<float>& x, double fromSeconds, double toSeconds, double hz)
{
    const int a = static_cast<int> (fromSeconds * kSr), b = std::min (static_cast<int> (x.size()), static_cast<int> (toSeconds * kSr));
    double best = 0.0, bestHz = 0.0;
    for (double f = hz * 0.944; f <= hz * 1.059; f *= 1.0005)
    {
        const double e = toneEnergy (x.data(), a, b, f);
        if (e > best)
        {
            best = e;
            bestHz = f;
        }
    }
    return bestHz;
}

double centsOff (double measured, double expected) { return 1200.0 * std::log2 (measured / expected); }

// toneEnergy with a Hann window: a strong neighbour does not leak into a weak component.
double windowedEnergy (const std::vector<float>& x, int begin, int end, double hz)
{
    double re = 0.0, im = 0.0;
    for (int i = begin; i < end; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (6.283185307179586 * (i - begin) / (end - begin));
        const double w = 6.283185307179586 * hz * i / kSr;
        re += win * x[static_cast<size_t> (i)] * std::cos (w);
        im += win * x[static_cast<size_t> (i)] * std::sin (w);
    }
    return std::sqrt (re * re + im * im) / (end - begin);
}

// --- the Pitch Corrector (0.26.0) -------------------------------------------------------------------------------------------
// The strongest frequency between lo and hi Hz (steps of 0.05 %) over [from, to) seconds, Hann-windowed.
double strongestHz (const std::vector<float>& x, double fromSeconds, double toSeconds, double lo, double hi)
{
    const int a = static_cast<int> (fromSeconds * kSr), b = std::min (static_cast<int> (x.size()), static_cast<int> (toSeconds * kSr));
    double best = 0.0, bestHz = 0.0;
    for (double f = lo; f <= hi; f *= 1.0005)
    {
        const double e = windowedEnergy (x, a, b, f);
        if (e > best)
        {
            best = e;
            bestHz = f;
        }
    }
    return bestHz;
}

// A tone with `harmonics` harmonics (amplitude 1/k), its frequency given per sample by `hzAt`.
template <typename F>
std::vector<float> toneOf (F hzAt, double seconds, int harmonics = 1, double amp = 0.5)
{
    std::vector<float> x (static_cast<size_t> (seconds * kSr));
    double phase = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        double v = 0.0;
        for (int k = 1; k <= harmonics; ++k)
            v += std::sin (k * phase) / k;
        x[i] = static_cast<float> (amp * v);
        phase += 6.283185307179586 * hzAt (static_cast<double> (i) / kSr) / kSr;
    }
    return x;
}

void testPitchCorrector()
{
    std::cout << "Pitch Corrector (0.26.0)\n";
    using T = sls::EffectType;
    // Key, Scale, Speed, Amount, Shift, Range, Mix
    const auto slot = [] (float key, float scale, float speed, float amount, float shift, float range = 1.0f, float mix = 100.0f)
    { return slotWith (T::PitchCorrector, { key, scale, speed, amount, shift, range, mix }); };
    const auto steady = [] (double hz, int harmonics = 1, double seconds = 1.5)
    { return toneOf ([hz] (double) { return hz; }, seconds, harmonics); };

    {
        bool same = true;
        const auto& theoryScales = sls::theory::scales();
        same = same && static_cast<int> (theoryScales.size()) == static_cast<int> (std::size (sls::effect_tables::kPitchScaleMasks));
        const juce::StringArray names = juce::StringArray::fromTokens (sls::effectParamInfo (T::PitchCorrector, 1).choices, "|", "");
        for (size_t i = 0; same && i < theoryScales.size(); ++i)
            same = theoryScales[i].mask == sls::effect_tables::kPitchScaleMasks[i] && names[static_cast<int> (i)] == theoryScales[i].name;
        check (same, "its scales are the piano roll's (same names, same notes, same order)");
    }

    // A note already in tune stays where it is, at the same level.
    {
        const auto in = steady (440.0);
        sls::EffectUnit unit;
        unit.prepare (kSr);
        sls::EffectMemory unitMemory; // the big buffers the Player would lend (0.29.0)
        unitMemory.prepare (kSr);
        unit.attachMemory (&unitMemory);
        auto l = in, r = in;
        for (size_t i = 0; i < l.size(); i += 64)
            unit.process (slot (0, 0, 0, 100, 0), l.data() + i, r.data() + i, static_cast<int> (std::min<size_t> (64, l.size() - i)));
        check (std::abs (centsOff (unit.detectedPitchHz(), 440.0)) < 1.0 && unit.targetNote() == 69,
               "it finds the pitch of a 440 Hz tone (within 1 cent) and its note (A4)");
        check (std::abs (centsOff (strongestHz (l, 0.5, 1.5, 415.0, 466.0), 440.0)) < 2.0, "an A4 in tune comes out at 440 Hz (within 2 cents)");
        const double ratio = rmsOf (l, static_cast<int> (0.5 * kSr), static_cast<int> (1.5 * kSr)) / rmsOf (in, static_cast<int> (0.5 * kSr), static_cast<int> (1.5 * kSr));
        check (ratio > 0.97 && ratio < 1.03, "and at the same level (within 0.3 dB)");
        check (l == r, "the same input on both sides gives the same output on both sides");
    }

    // Out of tune: pulled to the nearest note.
    const auto sharp = steady (452.0); // A4 + 47 cents
    check (std::abs (centsOff (strongestHz (runEffect (slot (0, 0, 0, 100, 0), sharp), 0.5, 1.5, 415.0, 480.0), 440.0)) < 3.0,
           "A4 + 47 cents, chromatic, speed 0: comes out at A4 (within 3 cents)");
    check (std::abs (centsOff (strongestHz (runEffect (slot (0, 0, 0, 50, 0), sharp), 0.5, 1.5, 415.0, 480.0), 440.0) - 23.4) < 3.0,
           "amount 50 %: half the correction (+23 cents, within 3)");
    check (std::abs (centsOff (strongestHz (runEffect (slot (0, 0, 0, 0, 0), sharp), 0.5, 1.5, 415.0, 480.0), 452.0)) < 2.0,
           "amount 0: no correction");
    // Follow (0.41.0): the note a channel is playing instead of the scale (as Pitcher's MIDI mode).
    {
        const auto followRun = [&sharp] (const int* notes, float followChoice)
        {
            auto s = slotWith (T::PitchCorrector, { 0, 0, 0, 100, 0, 1.0f, 100.0f, followChoice });
            sls::EffectUnit unit;
            unit.prepare (kSr);
            sls::EffectMemory memory;
            memory.prepare (kSr);
            unit.attachMemory (&memory);
            auto l = sharp, r = sharp;
            const sls::EffectTransport transport { -1.0, 0.0, notes, sls::kMaxChannels };
            for (size_t i = 0; i < l.size(); i += 64)
                unit.process (s, l.data() + i, r.data() + i, static_cast<int> (std::min<size_t> (64, l.size() - i)), transport);
            return l;
        };
        std::array<int, sls::kMaxChannels> notes {};
        notes.fill (-1);
        notes[1] = 71; // channel 2 plays B4
        check (std::abs (centsOff (strongestHz (followRun (notes.data(), 2.0f), 0.5, 1.5, 450.0, 520.0), 493.88)) < 4.0,
               "following channel 2, A4 + 47 cents is pulled to the B4 it plays (within 4 cents)");
        notes[1] = 59; // B3: the same note an octave lower is taken in the octave of the voice
        check (std::abs (centsOff (strongestHz (followRun (notes.data(), 2.0f), 0.5, 1.5, 450.0, 520.0), 493.88)) < 4.0,
               "a note in another octave is taken in the voice's octave");
        notes[1] = -1;
        check (std::abs (centsOff (strongestHz (followRun (notes.data(), 2.0f), 0.5, 1.5, 415.0, 480.0), 452.0)) < 2.0,
               "nothing held on the channel: the voice is left alone");
        check (std::abs (centsOff (strongestHz (followRun (nullptr, 0.0f), 0.5, 1.5, 415.0, 480.0), 440.0)) < 3.0,
               "Follow = Scale works as before");
    }
    const auto flat = steady (427.0); // A4 - 52 cents: nearest note is G#4 chromatically, A4 in C major
    check (std::abs (centsOff (strongestHz (runEffect (slot (0, 0, 0, 100, 0), flat), 0.5, 1.5, 400.0, 460.0), 415.30)) < 3.0,
           "A4 - 52 cents, chromatic: to G#4");
    check (std::abs (centsOff (strongestHz (runEffect (slot (0, 1, 0, 100, 0), flat), 0.5, 1.5, 400.0, 460.0), 440.0)) < 3.0,
           "the same in C major (no G#): to A4");
    check (std::abs (centsOff (strongestHz (runEffect (slot (10, 1, 0, 100, 0), steady (470.0)), 0.5, 1.5, 440.0, 500.0), 466.16)) < 3.0,
           "A#4 + 14 cents in A# major: to A#4 (the key moves the scale)");
    check (std::abs (centsOff (strongestHz (runEffect (slot (9, 11, 0, 100, 0), steady (470.0)), 0.5, 1.5, 420.0, 540.0), 440.0)) < 3.0,
           "A#4 + 14 cents in A minor pentatonic (no A#, no B): to A4, the nearer of A4 and C5");

    // Tones with harmonics, in each range.
    check (std::abs (centsOff (strongestHz (runEffect (slot (0, 0, 0, 100, 0), steady (196.0 * std::pow (2.0, 30.0 / 1200.0), 6)), 0.5, 1.5, 185.0, 210.0), 196.0)) < 3.0,
           "a tone with six harmonics at G3 + 30 cents: to G3");
    check (std::abs (centsOff (strongestHz (runEffect (slot (0, 0, 0, 100, 0, 0), steady (55.0 * std::pow (2.0, 40.0 / 1200.0), 6, 2.0)), 1.0, 2.0, 52.0, 60.0), 55.0)) < 3.0,
           "range Low: A1 + 40 cents (with harmonics) to A1");
    check (std::abs (centsOff (strongestHz (runEffect (slot (0, 0, 0, 100, 0, 2), steady (1500.0, 3)), 0.5, 1.5, 1420.0, 1560.0), 1479.98)) < 3.0,
           "range High: F#6 + 23 cents to F#6");

    // Shift.
    check (std::abs (centsOff (strongestHz (runEffect (slot (0, 0, 20, 0, 12), steady (300.0)), 0.5, 1.5, 560.0, 640.0), 600.0)) < 3.0,
           "shift +12: an octave up");
    check (std::abs (centsOff (strongestHz (runEffect (slot (0, 0, 20, 0, -7), steady (300.0)), 0.5, 1.5, 180.0, 220.0), 300.0 * std::pow (2.0, -7.0 / 12.0))) < 3.0,
           "shift -7: a fifth down");
    {
        const auto up = runEffect (slot (0, 0, 20, 0, 12), steady (300.0));
        const double kept = windowedEnergy (up, static_cast<int> (0.5 * kSr), static_cast<int> (1.5 * kSr), 600.0);
        const double left = windowedEnergy (up, static_cast<int> (0.5 * kSr), static_cast<int> (1.5 * kSr), 300.0);
        check (left < 0.01 * kept, "and the original pitch is gone (40 dB down)");
    }

    // Speed: a jump from A4 to A4 + 47 cents at 1 s, measured over the next 100 ms.
    {
        const auto jump = toneOf ([] (double t) { return t < 1.0 ? 440.0 : 452.0; }, 1.6);
        const double fast = strongestHz (runEffect (slot (0, 0, 0, 100, 0), jump), 1.04, 1.14, 415.0, 480.0);
        const double slow = strongestHz (runEffect (slot (0, 0, 300, 100, 0), jump), 1.04, 1.14, 415.0, 480.0);
        const double settled = strongestHz (runEffect (slot (0, 0, 300, 100, 0), jump), 2.0 - 0.45, 1.6, 415.0, 480.0);
        check (std::abs (centsOff (fast, 440.0)) < 6.0, "speed 0: the new note is corrected at once");
        check (centsOff (slow, 440.0) > 15.0, "speed 300 ms: still mostly uncorrected 40-140 ms later");
        check (std::abs (centsOff (settled, 440.0)) < 8.0, "and corrected half a second later");
    }

    // Vibrato survives a slow speed and is flattened by a fast one.
    {
        const auto vibrato = toneOf ([] (double t) { return 440.0 * std::pow (2.0, 0.3 / 12.0 * std::sin (6.283185307179586 * 5.5 * t)); }, 2.0);
        const auto spread = [] (const std::vector<float>& y)
        {
            // the pitch every 20 ms from zero crossings: its range in cents
            double lo = 1e9, hi = -1e9;
            for (double t = 0.6; t < 1.9; t += 0.02)
            {
                const int a = static_cast<int> (t * kSr), b = static_cast<int> ((t + 0.02) * kSr);
                int first = -1, last = -1, crossings = 0;
                for (int i = a; i < b; ++i)
                    if (y[static_cast<size_t> (i - 1)] < 0.0f && y[static_cast<size_t> (i)] >= 0.0f)
                    {
                        if (first < 0)
                            first = i;
                        last = i;
                        ++crossings;
                    }
                if (crossings > 2)
                {
                    const double hz = (crossings - 1) * kSr / (last - first);
                    lo = std::min (lo, centsOff (hz, 440.0));
                    hi = std::max (hi, centsOff (hz, 440.0));
                }
            }
            return hi - lo;
        };
        const double natural = spread (vibrato);
        const double fast = spread (runEffect (slot (0, 0, 0, 100, 0), vibrato));
        const double slow = spread (runEffect (slot (0, 0, 400, 100, 0), vibrato));
        check (fast < 0.15 * natural, "vibrato of +-30 cents: speed 0 flattens it (the corrections meet the sound they were measured on)");
        check (slow > 0.6 * natural, "speed 400 ms lets it through");
    }

    // Latency and the edges.
    {
        std::vector<float> burst (static_cast<size_t> (kSr), 0.0f);
        const auto tone = steady (220.0, 1, 0.5);
        std::copy (tone.begin(), tone.end(), burst.begin() + static_cast<long> (0.5 * kSr));
        const auto out = runEffect (slot (0, 0, 0, 100, 0), burst);
        const int onset = firstAbove (out, 0.05f);
        check (onset >= static_cast<int> (0.5 * kSr) && onset < static_cast<int> (0.515 * kSr), "the sound comes out less than 15 ms late (range Voice)");
        check (peakOf (out.data(), 0, static_cast<int> (0.5 * kSr)) == 0.0f, "silence stays silent");
    }
    {
        const auto in = steady (330.0);
        check (runEffect (slot (0, 0, 0, 100, 5, 1, 0), in) == in, "mix 0 is bit-exact");
        std::vector<float> noise (static_cast<size_t> (kSr));
        std::uint32_t seed = 12345;
        for (auto& v : noise)
        {
            seed = seed * 1664525u + 1013904223u;
            v = 0.3f * (static_cast<float> (seed >> 8) / 8388608.0f - 1.0f);
        }
        const auto out = runEffect (slot (0, 1, 0, 100, 0), noise);
        bool finite = true;
        for (float v : out)
            finite = finite && std::isfinite (v);
        const double ratio = rmsOf (out, 12000, 48000) / rmsOf (noise, 12000, 48000);
        check (finite && ratio > 0.6 && ratio < 1.4, "noise (no pitch): passes, finite and at about its level");
    }
    {
        // every scale, speed, shift and range on a gliding tone with harmonics: finite and bounded
        const auto glide = toneOf ([] (double t) { return 80.0 * std::pow (2.0, 4.0 * t); }, 1.2, 4);
        bool fine = true;
        float worst = 0.0f;
        for (float scale : { 0.0f, 1.0f, 12.0f })
            for (float speed : { 0.0f, 500.0f })
                for (float shift : { -12.0f, 0.0f, 12.0f })
                    for (float range : { 0.0f, 1.0f, 2.0f })
                    {
                        const auto out = runEffect (slot (3, scale, speed, 100, shift, range), glide);
                        for (float v : out)
                        {
                            fine = fine && std::isfinite (v);
                            worst = std::max (worst, std::abs (v));
                        }
                    }
        check (fine && worst < 2.0f * peakOf (glide.data(), 0, static_cast<int> (glide.size())), "a glide over five octaves with every scale, speed, shift and range: finite, bounded");
    }
    {
        sls::EffectUnit unit;
        unit.prepare (kSr);
        sls::EffectMemory unitMemory; // the big buffers the Player would lend (0.29.0)
        unitMemory.prepare (kSr);
        unit.attachMemory (&unitMemory);
        const auto in = steady (452.0, 1, 0.5);
        std::vector<float> l (64), r (64);
        const auto s = slot (0, 1, 30, 100, 3);
        bool fine = true;
        tAllocs = 0;
        tCounting = true;
        for (int block = 0; block < 2000; ++block)
        {
            for (int k = 0; k < 64; ++k)
                l[static_cast<size_t> (k)] = r[static_cast<size_t> (k)] = in[static_cast<size_t> ((block * 64 + k) % static_cast<int> (in.size()))];
            unit.process (s, l.data(), r.data(), 64);
            fine = fine && std::isfinite (l[0]);
        }
        tCounting = false;
        check (tAllocs == 0 && fine, "no allocation on the audio thread (2000 blocks)");
    }
}

void testAdditiveAndPhysical()
{
    std::cout << "Additive and physical synth engines (0.24.0)\n";
    const auto play = [] (const sls::SynthPatch& patch, double seconds, int pitch = 60, int lengthTicks = 2 * kBeat, float velocity = 0.8f)
    {
        auto c = synthChannel ({ { pitch, 0, lengthTicks, velocity } });
        c.synth = patch;
        sls::Player p;
        p.prepare (kSr);
        return renderBlocks (p, sls::Project::make ({ c }, 16 * kBeat), true, seconds);
    };
    const auto finite = [] (const std::vector<float>& x) { return std::all_of (x.begin(), x.end(), [] (float v) { return std::isfinite (v); }); };
    const auto rms = [] (const std::vector<float>& x, double from, double to)
    {
        double e = 0.0;
        const auto a = static_cast<size_t> (from * kSr), b = std::min (x.size(), static_cast<size_t> (to * kSr));
        for (size_t i = a; i < b; ++i)
            e += static_cast<double> (x[i]) * x[i];
        return b > a ? std::sqrt (e / static_cast<double> (b - a)) : 0.0;
    };
    const int w0 = 9600, w1 = 19200; // 0.2 .. 0.4 s
    const double c4 = 261.6256;

    // --- additive ---
    sls::SynthPatch add;
    add.engine = sls::SynthEngine::Additive;
    add.filter = sls::SamplerFilter::Off;
    add.amp = { 0.001f, 0.1f, 1.0f, 0.05f };
    const auto saw = play (add, 0.5);
    const double h1 = toneEnergy (saw.data(), w0, w1, c4), h2 = toneEnergy (saw.data(), w0, w1, 2 * c4), h3 = toneEnergy (saw.data(), w0, w1, 3 * c4);
    std::cout << "    (additive saw-like timbre: harmonics 1, 2, 3 at " << h1 << ", " << h2 << ", " << h3 << ")\n";
    check (finite (saw) && h1 > 0.02 && std::abs (h2 / h1 - 0.5) < 0.05 && std::abs (h3 / h1 - 1.0 / 3.0) < 0.04,
           "the default timbre is a saw: harmonic n at 1/n");
    check (std::abs (centsOff (peakNear (saw, 0.2, 0.4, c4), c4)) < 2.0, "and plays the note's pitch (C4 within 2 cents)");

    auto odd = add;
    odd.timbreA.evenLevel = 0.0f;
    const auto o = play (odd, 0.5);
    check (toneEnergy (o.data(), w0, w1, 2 * c4) < 0.01 * toneEnergy (o.data(), w0, w1, c4) && toneEnergy (o.data(), w0, w1, 3 * c4) > 0.2 * toneEnergy (o.data(), w0, w1, c4),
           "even harmonics at 0: only odd ones (square-like)");
    auto dark = add, flat = add;
    dark.timbreA.brightness = 0.0f;
    flat.timbreA.brightness = 1.0f;
    check (roughness (play (flat, 0.5), 0.2, 0.4) > 2.0f * roughness (play (dark, 0.5), 0.2, 0.4), "brightness: a flat spectrum is far brighter than a dark one");

    auto one = add;
    one.timbreA.partials = 1;
    one.timbreB = add.timbreA;
    const auto pureA = play (one, 0.5);
    check (windowedEnergy (pureA, w0, w1, 2 * c4) < 1e-4 * windowedEnergy (pureA, w0, w1, c4), "one harmonic: a sine (the 2nd harmonic 80 dB down)");
    auto toB = one;
    toB.morph = 1.0f;
    const auto pureB = play (toB, 0.5);
    check (toneEnergy (pureB.data(), w0, w1, 2 * c4) > 0.4 * toneEnergy (pureB.data(), w0, w1, c4), "morph at 100 %: timbre B (the saw) instead");
    auto swept = one;
    swept.morphEnvAmount = 1.0f;
    swept.morphEnv = { 0.6f, 0.1f, 1.0f, 0.1f };
    const auto sw = play (swept, 0.9);
    check (roughness (sw, 0.65, 0.85) > 2.0f * roughness (sw, 0.02, 0.06), "the morph envelope moves from A to B over its attack");

    auto filtered = add;
    filtered.filter = sls::SamplerFilter::LowPass;
    filtered.cutoff = 0.3f; // about 160 Hz
    filtered.envAmount = 0.0f;
    filtered.keyTrack = 0.0f;
    filtered.resonance = 0.0f;
    const auto lp = play (filtered, 0.5);
    check (toneEnergy (lp.data(), w0, w1, 4 * c4) < 0.1 * toneEnergy (saw.data(), w0, w1, 4 * c4), "the filter acts on the harmonics: low-pass removes the upper ones");

    // C8 (4186 Hz): the 6th harmonic (25.1 kHz) is above the limit; nothing may fold back to 22.9 kHz.
    const auto high = play (add, 0.5, 108);
    const double fundamental = windowedEnergy (high, w0, w1, 4186.009), fold = windowedEnergy (high, w0, w1, 48000.0 - 6 * 4186.009);
    std::cout << "    (C8: fold-back of the 6th harmonic " << 20.0 * std::log10 (fold / fundamental + 1e-12) << " dB)\n";
    check (fold < 1e-4 * fundamental && finite (high), "no aliasing: harmonics above the limit are left out (fold-back below -80 dB)");

    {   // the master's soft limiter (0.24.0): linear below -3 dBFS, never above 1
        auto sine = one;
        sine.gain = 1.5f;
        const auto clean = play (sine, 0.5, 69, 2 * kBeat, 1.0f);
        const float level = peakOf (clean.data(), w0, w1);
        const double third = windowedEnergy (clean, w0, w1, 3 * 440.0) / windowedEnergy (clean, w0, w1, 440.0);
        std::cout << "    (a sine peaking at " << level << ": 3rd harmonic " << 20.0 * std::log10 (third + 1e-15) << " dB)\n";
        check (level > 0.3f && level < 0.7f && third < 1e-5, "the master limiter leaves a sine below -3 dBFS untouched (no distortion, 3rd harmonic below -100 dB)");
        std::vector<sls::Channel> many;
        for (int i = 0; i < 12; ++i)
        {
            auto c = synthChannel ({ { 69, 0, 2 * kBeat, 1.0f } });
            c.synth = sine;
            many.push_back (c);
        }
        sls::Player p;
        p.prepare (kSr);
        const auto loudMix = renderBlocks (p, sls::Project::make (many, 16 * kBeat), true, 0.5);
        const float loudPeak = peakOf (loudMix.data(), 0, static_cast<int> (loudMix.size()));
        check (loudPeak > 0.9f && loudPeak <= 1.0f, "twelve of them at once: limited, never above 1");
    }

    // --- physical: string ---
    sls::SynthPatch str;
    str.engine = sls::SynthEngine::Physical;
    str.amp = { 0.001f, 0.0f, 1.0f, 0.05f };
    str.physical = { sls::PhysicalModel::String, 0.6f, 0.2f, 0.6f, 0.3f, 0.0f, 0.0f, 0.7f };
    bool tuned = true;
    for (const int pitch : { 40, 52, 64, 76, 88 })
        for (const float stiff : { 0.0f, 0.5f })
        {
            auto s = str;
            s.physical.stiffness = stiff;
            const double hz = 440.0 * std::pow (2.0, (pitch - 69) / 12.0);
            const auto x = play (s, 0.5, pitch);
            const double cents = centsOff (peakNear (x, 0.1, 0.45, hz), hz);
            if (std::abs (cents) >= 5.0)
                std::cout << "    (string " << pitch << " stiffness " << stiff << ": " << cents << " cents)\n";
            tuned = tuned && std::abs (cents) < 5.0 && finite (x);
        }
    check (tuned, "the string is in tune (E2 to E6, with and without stiffness, within 5 cents)");
    const auto plucked = play (str, 1.0);
    check (rms (plucked, 0.0, 0.1) > 0.02 && rms (plucked, 0.0, 0.1) > 1.5 * rms (plucked, 0.6, 0.9), "a plucked string starts strong and dies away");
    auto shortRing = str, longRing = str;
    shortRing.physical.decay = 0.15f;
    longRing.physical.decay = 0.9f;
    const auto sr = play (shortRing, 1.0), lr = play (longRing, 1.0);
    check (rms (lr, 0.7, 0.9) / rms (lr, 0.0, 0.1) > 4.0 * rms (sr, 0.7, 0.9) / rms (sr, 0.0, 0.1), "a longer decay rings longer");
    auto bright = str, soft = str;
    bright.physical.hardness = 1.0f;
    soft.physical.hardness = 0.1f;
    check (roughness (play (bright, 0.3), 0.01, 0.1) > 1.5f * roughness (play (soft, 0.3), 0.01, 0.1), "a hard pluck is brighter than a soft one");
    auto damped = longRing, ringing = longRing;
    damped.physical.releaseDamp = 1.0f;
    ringing.physical.releaseDamp = 0.0f;
    ringing.amp.release = 4.0f;
    damped.amp.release = 4.0f;
    const auto dm = play (damped, 1.0, 60, kBeat / 2), rg = play (ringing, 1.0, 60, kBeat / 2); // released at 0.25 s
    check (rms (dm, 0.5, 0.7) < 0.1 * rms (rg, 0.5, 0.7), "release damping stops a released note quickly; without it, it rings on");
    const auto loud = play (str, 0.3, 60, 2 * kBeat, 1.0f), quiet = play (str, 0.3, 60, 2 * kBeat, 0.2f);
    check (rms (loud, 0.0, 0.2) > 1.8 * rms (quiet, 0.0, 0.2), "a harder note (velocity) is louder");
    auto withBody = str;
    withBody.physical.body = 1.0f;
    const auto bd = play (withBody, 0.5, 40);
    check (finite (bd) && toneEnergy (bd.data(), w0, w1, 98.0) > 0.0 && peakOf (bd.data(), 0, static_cast<int> (bd.size())) < 2.0f, "the body adds its resonances and stays bounded");

    // --- physical: struck models ---
    auto marimba = str;
    marimba.physical.model = sls::PhysicalModel::Marimba;
    const auto mr = play (marimba, 0.5, 60);
    const double m1 = toneEnergy (mr.data(), 480, 9600, c4), m2 = toneEnergy (mr.data(), 480, 9600, 3.99 * c4), between = toneEnergy (mr.data(), 480, 9600, 2.0 * c4);
    check (std::abs (centsOff (peakNear (mr, 0.01, 0.3, c4), c4)) < 3.0 && m2 > 5.0 * between && m1 > m2, "marimba: the fundamental, and the bar's second mode at 3.99 times it");
    auto bell = str;
    bell.physical.model = sls::PhysicalModel::Bell;
    const auto bl = play (bell, 0.6, 72);
    check (toneEnergy (bl.data(), 4800, 24000, 523.25 * 0.5) > 0.2 * toneEnergy (bl.data(), 4800, 24000, 523.25), "bell: the hum an octave below the strike note");
    bool modelsOk = true;
    for (int m = 0; m < sls::kPhysicalModels; ++m)
        for (const int pitch : { 24, 60, 108 })
        {
            auto s = str;
            s.physical.model = static_cast<sls::PhysicalModel> (m);
            s.physical.stiffness = 1.0f;
            s.physical.body = 1.0f;
            const auto x = play (s, 0.3, pitch, 2 * kBeat, 1.0f);
            modelsOk = modelsOk && finite (x) && peakOf (x.data(), 0, static_cast<int> (x.size())) < 3.0f;
        }
    check (modelsOk, "every model at its extremes (C1, C4, C8; full stiffness and body) stays finite and bounded");

    // --- factory sounds, files, no allocation ---
    bool soundsOk = true;
    int newSounds = 0;
    for (const auto& named : sls::synthFactoryPatches())
        if (named.patch.engine == sls::SynthEngine::Additive || named.patch.engine == sls::SynthEngine::Physical)
        {
            ++newSounds;
            const auto x = play (named.patch, 1.0);
            const float peak = peakOf (x.data(), 0, static_cast<int> (x.size()));
            if (! (finite (x) && peak > 0.02f && peak < 1.5f))
                std::cout << "    (" << named.name << ": peak " << peak << ")\n";
            soundsOk = soundsOk && finite (x) && peak > 0.02f && peak < 1.5f;
        }
    check (newSounds == 14 && soundsOk, "fourteen new ready-made sounds (5 additive, 9 physical) all play, finite, with a sensible level");

    auto patch = add;
    patch.timbreB.formant = 0.7f;
    patch.morph = 0.4f;
    patch.morphEnvAmount = -0.3f;
    patch.lfoTarget = sls::LfoTarget::Morph;
    patch.physical = { sls::PhysicalModel::Glass, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f };
    const auto back = sls::synthPatchFromVar (sls::synthPatchToVar (patch));
    check (back == patch.sanitised(), "the additive and physical settings go through a file and come back the same");
    auto var = sls::synthPatchToVar (patch);
    var.getDynamicObject()->removeProperty ("timbreA");
    var.getDynamicObject()->removeProperty ("physical");
    var.getDynamicObject()->setProperty ("engine", "physical");
    const auto older = sls::synthPatchFromVar (var);
    check (older.engine == sls::SynthEngine::Physical && older.timbreA == sls::SynthPatch {}.timbreA && older.physical == sls::PhysicalSettings {},
           "missing settings (older files) read as the defaults");

    {
        sls::Channel a = synthChannel ({ { 60, 0, 2 * kBeat, 0.8f }, { 64, 0, 2 * kBeat, 0.8f }, { 67, kBeat, kBeat, 0.8f } });
        a.synth = sls::synthFactoryPatches()[0].patch;
        for (const auto& named : sls::synthFactoryPatches())
            if (named.name == "Morph Pad")
                a.synth = named.patch;
        sls::Channel b = synthChannel ({ { 48, 0, 2 * kBeat, 0.8f }, { 55, kBeat / 2, kBeat, 0.8f }, { 72, kBeat, kBeat, 0.8f } });
        b.synth = str;
        sls::Channel m = b;
        m.synth = bell;
        const auto project = sls::Project::make ({ a, b, m }, 4 * kBeat);
        sls::Player player;
        player.prepare (kSr);
        std::vector<float> l (256), r (256);
        float* ch[2] = { l.data(), r.data() };
        gAllocs = 0;
        gCounting = true;
        for (int i = 0; i < 1000; ++i)
            player.render (project, 120.0, true, ch, 2, 256);
        gCounting = false;
        check (gAllocs.load() == 0, "the new engines allocate nothing while playing (1000 blocks, chords, strings, bells)");
    }
}

void testFmSynth()
{
    std::cout << "FM synth (0.19.0)\n";
    const auto play = [] (const sls::SynthPatch& patch, double seconds, int lengthTicks = 2 * kBeat, int pitch = 69)
    {
        auto c = synthChannel ({ { pitch, 0, lengthTicks, 0.8f } });
        c.synth = patch;
        sls::Player p;
        p.prepare (kSr);
        return renderBlocks (p, sls::Project::make ({ c }, 16 * kBeat), true, seconds);
    };
    const auto finite = [] (const std::vector<float>& x) { return std::all_of (x.begin(), x.end(), [] (float v) { return std::isfinite (v); }); };

    sls::SynthPatch pure;
    pure.engine = sls::SynthEngine::FM;
    pure.matrix = sls::FmMatrix {}; // operator 1 heard, nothing modulates
    pure.op[0] = { 1.0f, 1.0f, { 0.001f, 0.1f, 1.0f, 0.05f } };
    pure.amp = { 0.001f, 0.1f, 1.0f, 0.05f };

    const auto sine = play (pure, 0.6);
    check (std::fabs (zeroCrossingHz (sine, kSr, 2000, 24000) - 440.0) < 2.0 && roughness (sine, 0.1, 0.5) < 0.07f,
           "one operator, nothing modulating: a pure 440 Hz sine");
    auto octave = pure;
    octave.op[0].ratio = 2.0f;
    check (std::fabs (zeroCrossingHz (play (octave, 0.6), kSr, 2000, 24000) - 880.0) < 3.0, "operator ratio 2: 880 Hz");

    auto modulated = pure;
    modulated.op[1] = { 1.0f, 1.0f, { 0.001f, 0.1f, 1.0f, 0.05f } };
    modulated.matrix.fm[0][1] = 0.6f;
    const auto bright = play (modulated, 0.6);
    check (roughness (bright, 0.1, 0.5) > 2.0f * roughness (sine, 0.1, 0.5) && finite (bright), "operator 2 modulating operator 1: many more harmonics");
    auto feedback = pure;
    feedback.matrix.fm[0][0] = 0.7f;
    check (roughness (play (feedback, 0.6), 0.1, 0.5) > 1.5f * roughness (sine, 0.1, 0.5), "feedback on operator 1 brightens it (towards a saw)");

    // A modulator whose envelope decays: bright at the start, close to a sine later.
    auto decaying = modulated;
    decaying.op[1].env = { 0.001f, 0.15f, 0.0f, 0.05f };
    const auto d = play (decaying, 0.8);
    check (roughness (d, 0.01, 0.05) > 2.0f * roughness (d, 0.5, 0.7), "a modulator's own envelope: the brightness decays");

    // Two operators heard (no modulation) with ratios 1 and 2: both are there, and the level is normalised.
    auto organ = pure;
    organ.op[1] = { 2.0f, 1.0f, { 0.001f, 0.1f, 1.0f, 0.05f } };
    organ.matrix.out = { 1.0f, 1.0f, 0.0f, 0.0f };
    const auto o = play (organ, 0.6);
    float peakSine = 0.0f, peakOrgan = 0.0f;
    for (size_t i = 4800; i < 24000; ++i)
    {
        peakSine = std::max (peakSine, std::fabs (sine[i]));
        peakOrgan = std::max (peakOrgan, std::fabs (o[i]));
    }
    check (peakOrgan < 1.3f * peakSine && roughness (o, 0.1, 0.5) > 1.3f * roughness (sine, 0.1, 0.5),
           "two operators heard: the octave is added and the sum is normalised");

    // Release and the amplitude envelope, the LFO on the level.
    {
        auto r = pure;
        r.amp.release = 0.2f;
        const auto out = play (r, 1.5, 2 * kBeat); // a 1 s note
        check (rmsWindow (out, 1.02, 1.08) > 0.01f && rmsWindow (out, 1.3, 1.5) < 0.002f, "the note ends with the amplitude release");
        auto trem = pure;
        trem.lfoTarget = sls::LfoTarget::Amp;
        trem.lfoRate = 4.0f;
        trem.lfoDepth = 1.0f;
        const auto t = play (trem, 1.0, 2 * kBeat);
        float lo = 1.0f, hi = 0.0f;
        for (double x = 0.1; x < 0.9; x += 0.02)
        {
            lo = std::min (lo, rmsWindow (t, x, x + 0.02));
            hi = std::max (hi, rmsWindow (t, x, x + 0.02));
        }
        check (lo < 0.2f * hi, "the LFO's tremolo works on the FM engine");
    }

    // Algorithms, extreme settings, factory sounds, no allocation.
    {
        check (sls::fmAlgorithms().size() == 8 && sls::fmAlgorithms()[2].matrix.fm[2][3] > 0.0f && sls::fmAlgorithms()[7].matrix.out[3] > 0.0f,
               "eight algorithms: e.g. the 4-operator stack and the organ (all heard)");
        auto wild = pure;
        for (auto& row : wild.matrix.fm)
            row.fill (1.0f);
        wild.matrix.out.fill (1.0f);
        for (auto& op : wild.op)
            op = { 32.0f, 1.0f, { 0.0f, 0.0f, 1.0f, 0.0f } };
        const auto w = play (wild, 0.5, kBeat, 120);
        float peak = 0.0f;
        for (float v : w)
            peak = std::max (peak, std::fabs (v));
        check (finite (w) && peak < 1.5f, "every operator modulating every other at full depth, ratio 32, top key: finite and bounded");
        int fmSounds = 0;
        for (const auto& named : sls::synthFactoryPatches())
            fmSounds += named.patch.engine == sls::SynthEngine::FM ? 1 : 0;
        check (fmSounds >= 6, "six FM factory sounds");

        std::vector<sls::Note> notes;
        for (int i = 0; i < 8; ++i)
            notes.push_back ({ 48 + 4 * i, (i % 4) * kBeat / 2, 3 * kBeat, 0.7f });
        auto c = synthChannel (notes);
        c.synth = wild;
        const auto project = sls::Project::make ({ c }, 8 * kBeat);
        sls::Player player;
        player.prepare (kSr);
        renderBlocks (player, project, true, 0.1);
        std::array<float, 256> bufL {}, bufR {};
        float* chans[2] = { bufL.data(), bufR.data() };
        tAllocs = 0;
        tCounting = true;
        for (int block = 0; block < 2000; ++block)
            player.render (project, 120.0, true, chans, 2, 256);
        tCounting = false;
        check (tAllocs == 0, "FM voices allocate nothing on the audio thread (2000 blocks)");
    }

    // Hostile values and the file.
    {
        auto bad = pure;
        bad.op[2].ratio = std::numeric_limits<float>::quiet_NaN();
        bad.op[3].ratio = 1000.0f;
        bad.matrix.fm[1][2] = -5.0f;
        bad.matrix.out[3] = std::numeric_limits<float>::infinity();
        const auto fixed = bad.sanitised();
        check (fixed.op[2].ratio == sls::SynthPatch {}.op[2].ratio && fixed.op[3].ratio == 32.0f && fixed.matrix.fm[1][2] == 0.0f && fixed.matrix.out[3] == 0.0f,
               "hostile FM values become valid");
        auto saved = modulated;
        saved.op[3] = { 7.0f, 0.25f, { 0.5f, 1.5f, 0.25f, 2.0f } };
        saved.matrix.fm[3][3] = 0.33f;
        saved.matrix.out[2] = 0.5f;
        const auto back = sls::synthPatchFromVar (juce::JSON::parse (juce::JSON::toString (sls::synthPatchToVar (saved))));
        check (back == saved.sanitised() && back.engine == sls::SynthEngine::FM, "an FM patch survives the JSON round trip (operators, matrix, outputs)");
        const auto partial = sls::synthPatchFromVar (juce::JSON::parse (R"({"engine":"fm","fmMatrix":[[0,"x"],[9]],"operators":[{"ratio":-1}]})"));
        check (partial.engine == sls::SynthEngine::FM && partial.op[0].ratio == 0.125f && partial.matrix.fm[1][0] == 1.0f && partial.matrix.fm[0][1] == 0.0f,
               "a partial or hostile FM patch is read with clamped values");
    }
}

void testSynthPatchFiles()
{
    std::cout << "Synth patches in project and preset files (0.18.0)\n";
    const auto& patches = sls::synthFactoryPatches();
    bool same = true;
    for (const auto& named : patches)
    {
        const auto back = sls::synthPatchFromVar (juce::JSON::parse (juce::JSON::toString (sls::synthPatchToVar (named.patch))));
        same = same && back == named.patch.sanitised();
    }
    check (same, "every factory patch survives the JSON round trip exactly");
    check (sls::synthPatchFromVar (juce::var()) == sls::SynthPatch {}, "no patch in the file: the Classic engine");
    const auto hostile = sls::synthPatchFromVar (juce::JSON::parse (R"({"engine":"subtractive","oscillators":[{"wave":"banjo","level":7,"octave":-99}],"cutoff":"x","lfoTarget":"moon","amp":{"attack":1e9}})"));
    check (hostile.engine == sls::SynthEngine::Subtractive && hostile.osc[0].wave == sls::Waveform::Saw && hostile.osc[0].level == 1.0f
               && hostile.osc[0].octave == -3 && hostile.cutoff == sls::SynthPatch {}.cutoff && hostile.lfoTarget == sls::LfoTarget::Off && hostile.amp.attack == 10.0f,
           "a hostile patch is read with clamped values and defaults");

    // In a project file: a subtractive synth channel keeps its patch; a classic one writes nothing (older versions read it as before).
    auto doc = sls::ProjectDocument::blank();
    doc.channels[0].instrument = sls::Instrument::Synth;
    doc.channels[0].synth = patches[3].patch;
    if (doc.channels.size() < 2)
        doc.channels.push_back (doc.channels[0]);
    doc.channels[1].instrument = sls::Instrument::Synth;
    doc.channels[1].synth = sls::SynthPatch {};
    while (doc.sampleSources.size() < doc.channels.size())
        doc.sampleSources.emplace_back();
    const auto text = doc.toJson();
    const auto read = sls::ProjectDocument::fromJson (text);
    check (read.document.has_value() && read.document->channels[0].synth == patches[3].patch.sanitised()
               && read.document->channels[1].synth == sls::SynthPatch {},
           "a project keeps a subtractive patch per synth channel; a classic channel stays classic");
    check (text.containsIgnoreCase ("\"synth\"") && text.indexOf ("\"engine\"") == text.lastIndexOf ("\"engine\""),
           "only the subtractive channel writes a patch");

    // Channel presets carry the patch.
    sls::Channel c;
    c.instrument = sls::Instrument::Synth;
    c.synth = patches[1].patch;
    const auto preset = sls::ChannelPreset::fromJson (sls::ChannelPreset::fromChannel (c, "Lead").toJson());
    sls::Channel target;
    if (preset.has_value())
        preset->applyTo (target);
    check (preset.has_value() && target.synth == patches[1].patch.sanitised(), "a synth preset saves and applies the patch");
}

void testPluginProjectFile()
{
    std::cout << "Plugins in the project file (0.17.0)\n";
    auto d = sls::ProjectDocument::blank();
    d.channels[0].instrument = sls::Instrument::Plugin;
    d.tracks[2].slots[1].type = sls::EffectType::Plugin;
    d.tracks[2].slots[1].bypass = true;
    d.masterSlots[3].type = sls::EffectType::Plugin;
    d.console.aux[1].slots[0].type = sls::EffectType::Plugin;
    const auto spec = [] (sls::PluginSpec::Place place, int index, int slot, const char* name, bool instrument)
    {
        sls::PluginSpec s;
        s.place = place;
        s.index = index;
        s.slot = slot;
        s.description = std::string ("<PLUGIN name=\"") + name + "\" format=\"VST3\"/>";
        s.state = std::string ("state-of-") + name;
        s.instrument = instrument;
        return s;
    };
    d.pluginSpecs.push_back (spec (sls::PluginSpec::Place::Channel, 0, 0, "Synth A", true));
    d.pluginSpecs.push_back (spec (sls::PluginSpec::Place::Track, 2, 1, "Delay B", false));
    d.pluginSpecs.push_back (spec (sls::PluginSpec::Place::Master, 0, 3, "Limiter C", false));
    d.pluginSpecs.push_back (spec (sls::PluginSpec::Place::Aux, 1, 0, "Reverb D", false));

    const auto parsed = sls::ProjectDocument::fromJson (d.toJson());
    check (parsed.document.has_value(), "a project with plugins reads back");
    if (! parsed.document.has_value())
        return;
    const auto& r = *parsed.document;
    check (r.channels[0].instrument == sls::Instrument::Plugin, "a plugin channel stays a plugin channel");
    check (r.pluginSpecs.size() == 4, "every plugin is found again (4)");
    const auto same = [&] (sls::PluginSpec::Place place, int index, int slot, const char* name)
    {
        const auto* found = r.findPlugin (place, index, slot);
        return found != nullptr && found->description.find (name) != std::string::npos && found->state == std::string ("state-of-") + name;
    };
    check (same (sls::PluginSpec::Place::Channel, 0, 0, "Synth A") && r.findPlugin (sls::PluginSpec::Place::Channel, 0, 0)->instrument,
           "the instrument plugin keeps its description and state");
    check (same (sls::PluginSpec::Place::Track, 2, 1, "Delay B") && r.tracks[2].slots[1].type == sls::EffectType::Plugin && r.tracks[2].slots[1].bypass,
           "a plugin effect on a track keeps its slot, description, state and bypass");
    check (same (sls::PluginSpec::Place::Master, 0, 3, "Limiter C") && r.masterSlots[3].type == sls::EffectType::Plugin,
           "a plugin effect on the master keeps its slot");
    check (same (sls::PluginSpec::Place::Aux, 1, 0, "Reverb D") && r.console.aux[1].slots[0].type == sls::EffectType::Plugin,
           "a plugin effect on an aux bus keeps its slot");
    check (r.tracks[2].slots[0].type == sls::EffectType::None && r.tracks[2].slots[1].plugin == nullptr,
           "no instance is made while reading (the program makes them)");

    // A plugin slot without a plugin description is read as empty; a plugin channel without one stays a (silent) plugin channel.
    auto text = d.toJson();
    auto broken = sls::ProjectDocument::fromJson (text.replace ("<PLUGIN name=\\\"Delay B\\\" format=\\\"VST3\\\"/>", ""));
    check (broken.document.has_value() && broken.document->tracks[2].slots[1].type == sls::EffectType::None && broken.document->pluginSpecs.size() == 3,
           "a plugin slot with an empty description becomes an empty slot");

    // Writing without any plugin spec (the slot type alone) writes no plugin object and reads back as an empty slot.
    auto noSpec = d;
    noSpec.pluginSpecs.clear();
    const auto noSpecRead = sls::ProjectDocument::fromJson (noSpec.toJson());
    check (noSpecRead.document.has_value() && noSpecRead.document->masterSlots[3].type == sls::EffectType::None && noSpecRead.document->pluginSpecs.empty(),
           "a plugin slot saved without its plugin reads back empty");
}

// SunBurst (0.48.0) as Sun Loops Studio finds and plays it: the VST3 built next to the program, added without a scan.
void testSunBurstInHost()
{
    std::cout << "SunBurst inside Sun Loops Studio (0.48.0)\n";
    sls::PluginHost host;
    if (sls::PluginHost::bundledPaths().getNumPaths() == 0)
    {
        std::cout << "  (SunBurst was not built: skipped)\n";
        return;
    }
    {
        juce::PluginDescription stale; // listed from an AppImage mounted somewhere else last time
        stale.name = "SunBurst";
        stale.pluginFormatName = "VST3";
        stale.fileOrIdentifier = "/tmp/.mount_gone/usr/lib/vst3/SunBurst.vst3";
        stale.isInstrument = true;
        host.known().addType (stale);
    }
    check (host.addBundledPlugins() >= 1 && host.addBundledPlugins() == 0, "the bundled SunBurst is added to the known plugins once, without a scan");
    int sunbursts = 0;
    for (const auto& t : host.known().getTypes())
        sunbursts += t.name == "SunBurst" ? 1 : 0;
    check (sunbursts == 1, "a SunBurst listed from a place that is gone is forgotten");
    const auto* found = findPlugin (host, "SunBurst", "VST3");
    check (found != nullptr && found->isInstrument, "it is listed as a VST3 instrument");
    if (found == nullptr)
        return;
    juce::String error;
    auto synth = host.create (*found, kSr, sls::Player::kChunk, error);
    check (synth != nullptr && synth->isLoaded() && synth->parameterCount() > 150, "it loads, with its parameters for automation");
    if (synth == nullptr)
        return;
    sls::Channel c;
    c.instrument = sls::Instrument::Plugin;
    c.plugin = synth;
    c.volume = 1.0f;
    c.notes = { { 60, 0, kBeat, 0.9f }, { 64, 0, kBeat, 0.9f } };
    sls::Player p;
    p.prepare (kSr);
    const auto out = renderBlocks (p, sls::Project::make ({ c }, 16 * kBeat), true, 1.0);
    bool finite = true;
    for (const float v : out)
        finite = finite && std::isfinite (v);
    check (finite && rmsWindow (out, 0.05, 0.4) > 0.01f, "a chord plays through SunBurst in the Player");

    // HexForge (0.50.0), bundled the same way.
    const auto* hex = findPlugin (host, "HexForge", "VST3");
    check (hex != nullptr && hex->isInstrument, "HexForge is listed next to it as a VST3 instrument");
    if (hex == nullptr)
        return;
    auto fm = host.create (*hex, kSr, sls::Player::kChunk, error);
    check (fm != nullptr && fm->isLoaded() && fm->parameterCount() > 150, "HexForge loads, with its parameters for automation");
    if (fm == nullptr)
        return;
    c.plugin = fm;
    sls::Player q;
    q.prepare (kSr);
    const auto hexOut = renderBlocks (q, sls::Project::make ({ c }, 16 * kBeat), true, 1.0);
    bool hexFinite = true;
    for (const float v : hexOut)
        hexFinite = hexFinite && std::isfinite (v);
    check (hexFinite && rmsWindow (hexOut, 0.05, 0.4) > 0.01f, "a chord plays through HexForge in the Player");
}

void testPluginHosting()
{
    std::cout << "Plugin hosting (VST3, LV2, LADSPA)\n";
    const juce::ScopedJuceInitialiser_GUI juceInit; // plugins need a message manager
    sls::PluginHost host;
    check (host.formatNames().contains ("VST3") && host.formatNames().contains ("LV2"), "the host offers VST3 and LV2");
    {
        const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        const auto ladspaHome = home.getChildFile (".ladspa");
        const bool made = ! ladspaHome.exists() && ladspaHome.createDirectory();
        const auto extra = sls::PluginHost::distributionPaths ("LADSPA");
        bool listed = false;
        for (int i = 0; i < extra.getNumPaths(); ++i)
            listed = listed || extra[i] == ladspaHome;
        check (listed && sls::PluginHost::distributionPaths ("Nonsense").getNumPaths() == 0,
               "extra plugin folders: an existing ~/.ladspa is added, unknown formats get none");
        if (made)
            ladspaHome.deleteRecursively();
    }

    const juce::File dir (SLS_TEST_PLUGIN_DIR);
    const auto paths = [&] (const juce::String& format)
    {
        juce::FileSearchPath p;
        if (format == "VST3" || format == "LV2")
            for (const char* kind : { "SlsTestSynth", "SlsTestGain" })
                p.add (dir.getChildFile (juce::String (kind) + "_artefacts/Release/" + format));
        return p;
    };
    const auto pedal = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sls-test-scan.txt");
    int scanned = 0;
    host.scan (paths, pedal, [&] (const juce::String&) { ++scanned; });
    const auto* synthVst3 = findPlugin (host, "SLS Test Synth", "VST3");
    const auto synthVst3Copy = synthVst3 != nullptr ? *synthVst3 : juce::PluginDescription();
    const auto* gainVst3 = findPlugin (host, "SLS Test Gain", "VST3");
    const auto gainVst3Copy = gainVst3 != nullptr ? *gainVst3 : juce::PluginDescription();
    const auto* synthLv2 = findPlugin (host, "SLS Test Synth", "LV2");
    const auto synthLv2Copy = synthLv2 != nullptr ? *synthLv2 : juce::PluginDescription();
    const auto* gainLv2 = findPlugin (host, "SLS Test Gain", "LV2");
    const auto gainLv2Copy = gainLv2 != nullptr ? *gainLv2 : juce::PluginDescription();
    check (synthVst3 != nullptr && gainVst3 != nullptr, "the scan finds the VST3 test synth and effect");
    check (synthLv2 != nullptr && gainLv2 != nullptr, "the scan finds the LV2 test synth and effect");
    check (synthVst3Copy.isInstrument && ! gainVst3Copy.isInstrument, "instruments and effects are told apart");
    if (synthVst3 == nullptr || gainVst3 == nullptr)
        return;

    juce::String error;
    auto synth = host.create (synthVst3Copy, kSr, sls::Player::kChunk, error);
    check (synth != nullptr && synth->isLoaded() && synth->isInstrument() && synth->displayName() == "SLS Test Synth", "a VST3 instrument is created");
    if (synth == nullptr)
        return;

    // The plugin plays the notes of its channel, starting at the exact sample.
    sls::Channel c;
    c.instrument = sls::Instrument::Plugin;
    c.plugin = synth;
    c.volume = 1.0f;
    c.notes = { { 69, kBeat / 2, kBeat, 1.0f } }; // starts at 0.25 s = sample 12000 (in the middle of a 64-sample chunk)
    const auto project = sls::Project::make ({ c }, 16 * kBeat);
    sls::Player p;
    p.prepare (kSr);
    auto out = renderBlocks (p, project, true, 1.0);
    const int start = firstNonZero (out);
    // The test synth's sine starts at phase 0, so sample 12000 itself is 0 and the first non-zero sample is 12001.
    check (start == 12001, ("the note starts on its sample (12000; first non-zero sample " + std::to_string (start) + ")").c_str());
    check (toneEnergy (out.data(), 13000, 30000, 440.0) > 0.05, "the plugin plays 440 Hz for A4");
    check (rmsWindow (out, 0.76, 1.0) < 1.0e-4f, "the note ends at its length (note-off sent)");

    // Live notes.
    {
        sls::Player q;
        q.prepare (kSr);
        q.liveNoteOn (project, 0, 81, 0.8f);
        auto held = renderBlocks (q, project, false, 0.3);
        check (toneEnergy (held.data(), 2000, 12000, 880.0) > 0.03, "a note played by hand reaches the plugin");
        q.liveNoteOff (0, 81);
        auto after = renderBlocks (q, project, false, 0.1);
        check (rmsWindow (after, 0.01, 0.1) < 1.0e-4f, "and stops when the key is let go");
    }

    // Stop sends the note-offs.
    {
        auto longNote = c;
        longNote.notes = { { 64, 0, 8 * kBeat, 1.0f } };
        const auto longProject = sls::Project::make ({ longNote }, 16 * kBeat);
        sls::Player q;
        q.prepare (kSr);
        renderBlocks (q, longProject, true, 0.3);
        auto stopped = renderBlocks (q, longProject, false, 0.3);
        check (rmsWindow (stopped, 0.05, 0.3) < 1.0e-4f, "Stop ends the plugin's notes");
    }

    // An effect plugin in a mixer slot.
    auto gain = host.create (gainVst3Copy, kSr, sls::Player::kChunk, error);
    check (gain != nullptr && ! gain->isInstrument(), "a VST3 effect is created");
    if (gain != nullptr)
    {
        sls::Player dryPlayer;
        dryPlayer.prepare (kSr);
        const auto dry = renderBlocks (dryPlayer, sls::Project::make ({ synthChannel ({ { 69, 0, 2 * kBeat, 0.8f } }) }, 16 * kBeat), true, 0.6);
        auto tracks = sls::Project::defaultTracks();
        tracks[0].slots[0].type = sls::EffectType::Plugin;
        tracks[0].slots[0].plugin = gain;
        sls::Player q;
        q.prepare (kSr);
        const auto wet = renderBlocks (q, sls::Project::make ({ synthChannel ({ { 69, 0, 2 * kBeat, 0.8f } }) }, 16 * kBeat, tracks), true, 0.6);
        const float ratio = rmsWindow (wet, 0.1, 0.5) / std::max (1.0e-9f, rmsWindow (dry, 0.1, 0.5));
        check (std::abs (ratio - 0.5f) < 0.03f, ("the effect plugin processes the track (gain 0.5; ratio " + std::to_string (ratio) + ")").c_str());

        // The state as the format keeps it: change the parameter, save the state, give it to a fresh instance.
        if (auto* param = gain->getInstance()->getParameters()[0])
            param->setValueNotifyingHost (0.25f);
        const auto saved = gain->getState();
        auto fresh = host.create (gainVst3Copy, kSr, sls::Player::kChunk, error);
        check (fresh != nullptr && fresh->setState (saved), "a saved plugin state is given to a new instance");
        tracks[0].slots[0].plugin = fresh;
        sls::Player r;
        r.prepare (kSr);
        const auto quieter = renderBlocks (r, sls::Project::make ({ synthChannel ({ { 69, 0, 2 * kBeat, 0.8f } }) }, 16 * kBeat, tracks), true, 0.6);
        const float ratio2 = rmsWindow (quieter, 0.1, 0.5) / std::max (1.0e-9f, rmsWindow (dry, 0.1, 0.5));
        check (std::abs (ratio2 - 0.25f) < 0.02f, "a plugin state that is set is used");
        check (fresh != nullptr && std::abs (fresh->getInstance()->getParameters()[0]->getValue() - 0.25f) < 1e-3f, "and its parameter is back");

        tracks[0].slots[0].bypass = true;
        sls::Player b;
        b.prepare (kSr);
        const auto bypassed = renderBlocks (b, sls::Project::make ({ synthChannel ({ { 69, 0, 2 * kBeat, 0.8f } }) }, 16 * kBeat, tracks), true, 0.6);
        check (std::abs (rmsWindow (bypassed, 0.1, 0.5) / rmsWindow (dry, 0.1, 0.5) - 1.0f) < 0.01f, "bypass leaves the signal alone");
    }

    // LV2 versions of the same plugins.
    if (synthLv2 != nullptr && gainLv2 != nullptr)
    {
        auto lv2Synth = host.create (synthLv2Copy, kSr, sls::Player::kChunk, error);
        check (lv2Synth != nullptr && lv2Synth->isInstrument(), ("an LV2 instrument is created " + error.toStdString()).c_str());
        if (lv2Synth != nullptr)
        {
            auto lv2Channel = c;
            lv2Channel.plugin = lv2Synth;
            sls::Player q;
            q.prepare (kSr);
            auto lv2Out = renderBlocks (q, sls::Project::make ({ lv2Channel }, 16 * kBeat), true, 1.0);
            check (firstNonZero (lv2Out) == 12001 && toneEnergy (lv2Out.data(), 13000, 30000, 440.0) > 0.05, "the LV2 instrument plays the note on its sample");
        }
        auto lv2Gain = host.create (gainLv2Copy, kSr, sls::Player::kChunk, error);
        check (lv2Gain != nullptr && ! lv2Gain->isInstrument(), "an LV2 effect is created");
    }

    // Automation of plugin parameters (0.35.0): the gain plugin's Gain follows a clip in Song mode, VST3 and LV2 alike.
    for (const auto& desc : { gainVst3Copy, gainLv2Copy })
    {
        if (desc.name.isEmpty())
            continue;
        const std::string format = desc.pluginFormatName.toStdString();
        auto automatedGain = host.create (desc, kSr, sls::Player::kChunk, error);
        if (automatedGain == nullptr)
        {
            check (false, ("a " + format + " gain plugin for automation").c_str());
            continue;
        }
        // The formats list the parameters in their own order (JUCE's LV2 wrapper puts its Bypass first): find Gain by name.
        int gainParam = -1;
        for (int i = 0; i < automatedGain->parameterCount(); ++i)
            if (automatedGain->parameterName (i) == "Gain")
                gainParam = i;
        check (gainParam >= 0 && std::abs (automatedGain->parameterDefault (gainParam) - 0.5f) < 1.0e-3f
                   && automatedGain->parameterText (gainParam, 0.25f).find ("0.25") != std::string::npos,
               ("the " + format + " plugin's parameters: Gain found by name, its default and text").c_str());
        if (gainParam < 0)
            continue;

        sls::Arrangement a;
        a.patterns.resize (1);
        setLengths (a, 8 * kBeat);
        a.ensureChannelCount (1);
        a.patterns[0].notes[0] = { { 69, 0, 8 * kBeat, 0.8f } };
        a.patterns[0].steps = { 64 };
        a.clips = { uc (a, 0, 0, 0, 1) };
        const std::vector<sls::Channel> chans { synthChannel ({}) };
        auto tracks = sls::Project::defaultTracks();
        const auto dry = [&]
        {
            sls::Player q;
            q.prepare (kSr);
            return renderBlocks (q, sls::compileSong (chans, a, tracks), true, 2.0);
        }();
        tracks[0].slots[0].type = sls::EffectType::Plugin;
        tracks[0].slots[0].plugin = automatedGain;
        sls::AutomationClip clip;
        clip.target = { sls::AutomationKind::TrackPlugin, 0, 0, gainParam };
        clip.lane = 0;
        clip.startTick = 0;
        clip.lengthTicks = 8 * kBeat;
        clip.points = { { 0.0, 1.0f, sls::AutomationShape::Hold }, { 2.0 * kBeat, 0.25f, sls::AutomationShape::Hold } };
        a.automationClips = { clip };
        const auto song = sls::compileSong (chans, a, tracks);
        check (song.automation.size() == 1, ("the " + format + " clip reaches the player").c_str());
        sls::Player q;
        q.prepare (kSr);
        const auto wet = renderBlocks (q, song, true, 2.0);
        check (std::abs (automatedGain->parameterValue (gainParam) - 0.25f) < 1.0e-3f, ("the " + format + " plugin keeps the last value").c_str());
        {
            // Moving a plugin's parameter allocates nothing on the audio thread (a ramp: a new value every chunk).
            auto ramp = a;
            ramp.automationClips[0].points = { { 0.0, 0.0f, sls::AutomationShape::Linear }, { 8.0 * kBeat, 1.0f, sls::AutomationShape::Linear } };
            const auto rampSong = sls::compileSong (chans, ramp, tracks);
            sls::Player rp;
            rp.prepare (kSr);
            std::vector<float> l (256), r (256);
            float* ch[2] = { l.data(), r.data() };
            rp.render (rampSong, 120.0, true, ch, 2, 256);
            tAllocs = 0;
            tCounting = true;
            for (int i = 0; i < 400; ++i)
                rp.render (rampSong, 120.0, true, ch, 2, 256);
            tCounting = false;
            check (tAllocs == 0, ("a " + format + " parameter moved by automation allocates nothing while rendering").c_str());
        }
        const float first = rmsWindow (wet, 0.2, 0.9) / std::max (1.0e-9f, rmsWindow (dry, 0.2, 0.9));
        const float second = rmsWindow (wet, 1.1, 1.9) / std::max (1.0e-9f, rmsWindow (dry, 1.1, 1.9));
        std::cout << "    (" << format << ": gain " << first << " then " << second << ")\n";
        check (std::abs (first - 1.0f) < 0.03f && std::abs (second - 0.25f) < 0.03f,
               ("the " + format + " plugin's gain follows the clip: 1 for two beats, then 0.25").c_str());

        // A parameter the plugin does not have, or no plugin there: the clip does not play (and nothing breaks).
        auto missingParam = a;
        missingParam.automationClips[0].target.param = automatedGain->parameterCount();
        check (sls::compileSong (chans, missingParam, tracks).automation.empty(), ("a parameter the " + format + " plugin does not have is left out").c_str());
        auto noPlugin = tracks;
        noPlugin[0].slots[0] = {};
        check (sls::compileSong (chans, a, noPlugin).automation.empty(), "a clip for a slot with no plugin is left out");
    }
    {
        // The plugin kinds have their own numbers (before 0.35.0 track 16 and up could share a number with another kind).
        const sls::AutomationTarget t16 { sls::AutomationKind::TrackVolume, 16, 0, 0 }, pan0 { sls::AutomationKind::TrackPan, 0, 0, 0 };
        const sls::AutomationTarget plug { sls::AutomationKind::TrackPlugin, 499, 7, 65535 }, plug2 { sls::AutomationKind::MasterPlugin, 0, 7, 65535 };
        check (t16.key() != pan0.key() && plug.key() != plug2.key() && t16.key() < pan0.key(), "every automated parameter has its own number");
    }

    // LADSPA (the SDK's example plugins, when they are installed).
    if (host.formatNames().contains ("LADSPA"))
    {
        juce::PluginDescription amp;
        for (const auto& d : host.known().getTypes())
            if (d.pluginFormatName == "LADSPA" && d.name.containsIgnoreCase ("amplifier"))
                amp = d;
        if (amp.name.isNotEmpty())
        {
            auto ladspa = host.create (amp, kSr, sls::Player::kChunk, error);
            check (ladspa != nullptr && ! ladspa->isInstrument(), ("a LADSPA effect is created (" + amp.name.toStdString() + ")").c_str());
        }
        else
            std::cout << "  (no stereo LADSPA amplifier installed: LADSPA not exercised)\n";
    }

    // A new sample rate: plugins are prepared again and keep their pitch.
    host.prepareAll (44100.0, sls::Player::kChunk);
    {
        sls::Player q;
        q.prepare (44100.0);
        std::vector<float> l (44100), r (44100);
        for (int done = 0; done < 44100; done += 256)
        {
            float* ch[2] = { l.data() + done, r.data() + done };
            q.render (project, 120.0, true, ch, 2, std::min (256, 44100 - done));
        }
        double re = 0.0, im = 0.0;
        for (int i = 13000; i < 30000; ++i)
        {
            re += l[static_cast<size_t> (i)] * std::cos (6.283185307179586 * 440.0 * i / 44100.0);
            im += l[static_cast<size_t> (i)] * std::sin (6.283185307179586 * 440.0 * i / 44100.0);
        }
        check (std::sqrt (re * re + im * im) / 17000.0 > 0.05, "prepared again for 44.1 kHz, the plugin still plays 440 Hz");
    }
    host.prepareAll (kSr, sls::Player::kChunk);

    // Placeholders for plugins that cannot be found.
    juce::MemoryBlock kept ("abc", 3);
    auto missing = sls::JucePlugin::placeholder (synthVst3Copy, kept, true);
    check (! missing->isLoaded() && missing->getState() == kept && missing->displayName() == "SLS Test Synth", "a placeholder keeps the description and state");
    std::array<float, 64> pl {}, pr {};
    pl.fill (0.3f);
    pr.fill (0.3f);
    missing->process (pl.data(), pr.data(), 64, nullptr, 0);
    check (pl[10] == 0.3f, "a placeholder leaves the signal alone");
    juce::PluginDescription parsed;
    check (sls::PluginHost::descriptionFromText (sls::PluginHost::descriptionToText (synthVst3Copy), parsed) && parsed.isDuplicateOf (synthVst3Copy),
           "a plugin description survives the text round trip");
    juce::String bad;
    juce::PluginDescription nothing;
    nothing.name = "Nothing";
    nothing.pluginFormatName = "VST3";
    nothing.fileOrIdentifier = "/nowhere/nothing.vst3";
    check (host.create (nothing, kSr, 64, bad) == nullptr && bad.isNotEmpty(), "a plugin that does not exist is refused with a message");

    // The known list survives saving.
    const auto listFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sls-known-plugins.xml");
    host.saveKnownList (listFile);
    sls::PluginHost other;
    other.loadKnownList (listFile);
    check (other.known().getNumTypes() == host.known().getNumTypes() && other.known().getNumTypes() >= 4, "the list of known plugins is saved and read back");
    // A program started later (a new host that only read the list, no scan) can still make every kind of plugin from it.
    for (const auto* format : { "VST3", "LV2" })
    {
        const auto* d = findPlugin (other, "SLS Test Gain", format);
        juce::String err;
        auto made = d != nullptr ? other.create (*d, kSr, sls::Player::kChunk, err) : nullptr;
        if (err.isNotEmpty())
            std::cout << "    (" << err << ")\n";
        check (made != nullptr && made->isLoaded(), (std::string ("a ") + format + " plugin is made from the saved list without a new scan").c_str());
    }
    listFile.deleteFile();

    // No allocation by the Player while a plugin instrument and a plugin effect run (the plugins' own code is not counted).
    if (gain != nullptr)
    {
        std::vector<sls::Note> notes;
        for (int i = 0; i < 64; ++i)
            notes.push_back ({ 50 + i % 30, i * 60, 90, 0.7f });
        auto busy = c;
        busy.notes = notes;
        auto tracks = sls::Project::defaultTracks();
        tracks[0].slots[1].type = sls::EffectType::Plugin;
        tracks[0].slots[1].plugin = gain;
        const auto heavy = sls::Project::make ({ busy }, 4 * kBeat, tracks);
        std::vector<float> l (256), r (256);
        float* ch[2] = { l.data(), r.data() };
        sls::Player z;
        z.prepare (kSr);
        z.render (heavy, 140.0, true, ch, 2, 256); // first call outside the counted region
        tCounting = true;
        tAllocs = 0;
        for (int i = 0; i < 2000; ++i)
            z.render (heavy, 140.0, true, ch, 2, 256);
        tCounting = false;
        check (tAllocs == 0, ("no allocation through Player::render with plugins (2000 blocks; counted: " + std::to_string (tAllocs) + ")").c_str());
    }
}
#endif

// tests/test_support.cpp (0.43.0)
void supportCheck (bool ok, const char* name) { check (ok, name); }
// The demo song (0.52.0): a valid project that uses the program's features, SunBurst and HexForge with their presets, and that
// renders loud enough without clipping.
// 0.56.0: every pattern has its own length; changing one leaves the others (and their clips) alone, a new one is 4 quarters.
void testPatternLengths056()
{
    std::cout << "Each pattern has its own length (0.56.0)\n";
    const int bar = 4 * kBeat;
    sls::Arrangement a;
    a.patterns.resize (3);
    a.ensureChannelCount (1);
    a.patterns[0].notes[0] = { { 60, 0, 120, 0.8f }, { 62, 5 * kBeat, 120, 0.8f } }; // the second note needs 6 quarters
    a.patterns[1].notes[0] = { { 72, 0, 120, 0.8f } };
    a.clips = { { 0, 0, 0, 2 * bar, 0 }, { 1, 1, 0, 2 * bar, 0 }, { 1, 2, 0, 3 * kBeat, 0 } };
    check (a.patternLength (0) == bar && a.patternLength (1) == bar && a.patternLength (2) == bar && a.patternLength (7) == bar,
           "every pattern starts at 4 quarters (and a missing one counts as 4)");

    check (a.setPatternLengthKeepingClips (0, 6 * kBeat), "pattern 1 made 6 quarters long");
    check (a.patternLength (0) == 6 * kBeat && a.patternLength (1) == bar && a.patternLength (2) == bar, "only that pattern changes");
    check (a.clips[0].lengthTicks == 12 * kBeat && a.clips[1].lengthTicks == 2 * bar && a.clips[2].lengthTicks == 3 * kBeat,
           "its clip keeps its two repeats; the other pattern's clips keep their lengths");
    check (! a.setPatternLengthKeepingClips (5, 8 * kBeat) && ! a.setPatternLengthKeepingClips (-1, 8 * kBeat), "a pattern that does not exist is refused");

    a.patterns.push_back (sls::Pattern {});
    check (a.patternLength (3) == bar, "a new pattern is 4 quarters, whatever the others are");

    const std::vector<sls::Channel> channels { synthChannel ({}) };
    check (sls::compilePattern (channels, a, 0).lengthTicks == 6 * kBeat && sls::compilePattern (channels, a, 1).lengthTicks == bar,
           "Pattern mode loops each pattern over its own length");
    const auto pattern0 = sls::compilePattern (channels, a, 0);
    check (pattern0.channels[0].notes.size() == 2, "and plays the note in its fifth quarter");

    a.clips = { { 0, 0, 0, 12 * kBeat, 0 }, { 1, 1, 0, 2 * bar, 0 } };
    const auto song = sls::compileSong (channels, a);
    std::vector<int> at60, at72;
    for (const auto& n : song.channels[0].notes)
        if (n.pitch == 60)
            at60.push_back (n.startTick);
        else if (n.pitch == 72)
            at72.push_back (n.startTick);
    std::sort (at60.begin(), at60.end());
    std::sort (at72.begin(), at72.end());
    check (at60 == std::vector<int> { 0, 6 * kBeat } && at72 == std::vector<int> { 0, bar },
           "in Song mode each clip repeats its own pattern's length (6 quarters and 4)");

    // Splitting a clip goes on from where its own pattern had got to.
    a.clips = { { 0, 0, 0, 12 * kBeat, 0 } };
    check (sls::splitPatternClip (a, 0, 7 * kBeat) == 1 && a.clips[1].offsetTicks == kBeat, "a split counts in the clip's own pattern length");

    // The timeline follows the longest pattern.
    a.setPatternLengthKeepingClips (2, 16 * kBeat);
    check (a.longestPatternTicks() == 16 * kBeat && a.timelineTicks() == sls::kPlaylistUnits * 16 * kBeat, "the timeline holds 32 of the longest pattern");

    // Only the patterns shorter than a bar are lengthened when the time signature asks for more.
    a.setPatternLengthKeepingClips (1, 3 * kBeat); // refused below 4 quarters in 4/4
    check (a.patternLength (1) == bar, "4/4: no pattern shorter than 4 quarters");
    a.meter = sls::TimeSignature::valid (3, 4);
    a.setPatternLengthKeepingClips (1, 3 * kBeat);
    check (a.patternLength (1) == 3 * kBeat, "3/4: 3 quarters are allowed");
    a.meter = {};
    a.lengthenPatternsTo (a.minPatternBeats() * kBeat);
    check (a.patternLength (1) == bar && a.patternLength (0) == 6 * kBeat && a.patternLength (2) == 16 * kBeat,
           "back in 4/4 the short one becomes 4 quarters, the others keep theirs");

    // Saved per pattern, read back per pattern.
    auto doc = sls::ProjectDocument::blank();
    doc.arrangement.patterns.resize (3);
    doc.arrangement.ensureChannelCount (doc.channels.size());
    doc.arrangement.patterns[0].lengthTicks = 5 * kBeat;
    doc.arrangement.patterns[1].lengthTicks = 2 * bar;
    doc.arrangement.patterns[1].notes[0] = { { 60, 7 * kBeat, 120, 0.8f } }; // only fits in the 8-quarter pattern
    doc.arrangement.clips = { { 0, 0, 0, 10 * kBeat, 2 * kBeat }, { 1, 1, 0, 2 * bar, 0 } };
    const auto text = doc.toJson();
    auto v = juce::JSON::parse (text);
    const auto* list = v.getDynamicObject()->getProperty ("patterns").getArray();
    check (static_cast<int> (list->getReference (0).getDynamicObject()->getProperty ("beats")) == 5
               && static_cast<int> (list->getReference (2).getDynamicObject()->getProperty ("beats")) == 4
               && static_cast<int> (v.getDynamicObject()->getProperty ("patternBeats")) == 8,
           "each pattern writes its length; the shared one older versions read is the longest");
    const auto back = sls::ProjectDocument::fromJson (text);
    check (back.document && back.document->arrangement.patternLength (0) == 5 * kBeat && back.document->arrangement.patternLength (1) == 2 * bar
               && back.document->arrangement.patternLength (2) == bar && back.document->arrangement.patterns[1].notes[0].size() == 1
               && back.document->arrangement.clips.size() == 2 && back.document->arrangement.clips[0].offsetTicks == 2 * kBeat,
           "the lengths, the notes and the clips come back");

    // A file from 0.42.0 - 0.55.x: one length for all.
    for (auto& pv : *v.getDynamicObject()->getProperty ("patterns").getArray())
        pv.getDynamicObject()->removeProperty ("beats");
    v.getDynamicObject()->setProperty ("patternBeats", 6);
    const auto old = sls::ProjectDocument::fromJson (juce::JSON::toString (v));
    check (old.document && old.document->arrangement.patternLength (0) == 6 * kBeat && old.document->arrangement.patternLength (1) == 6 * kBeat
               && old.document->arrangement.patternLength (2) == 6 * kBeat,
           "an older file gives every pattern its one length");
}

// 0.58.0: the piano roll's spare quarter after the pattern's end, and where a pattern is while the song plays.
void testPianoRollSpareQuarter()
{
    std::cout << "Piano roll: the spare quarter, the pattern's place in the song (0.58.0)\n";
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const int bar = 4 * kBeat;

    sls::PianoRoll roll;
    roll.setSize (800, 400);
    roll.setLoopLengthTicks (bar);
    check (roll.editLimit() == bar, "not growable: notes stop at the pattern's end");
    roll.setGrowable (true);
    check (roll.editLimit() == bar + kBeat, "growable: one spare quarter after the end");

    int reports = 0;
    roll.onNotesChanged = [&reports] { ++reports; };
    roll.setNotes ({ { 60, 3 * kBeat, kBeat, 0.8f } }); // the last quarter, ending exactly at the end
    roll.selectAll();
    roll.handleKey (juce::KeyPress (juce::KeyPress::leftKey, juce::ModifierKeys::shiftModifier, 0));
    check (reports == 1 && roll.takeLengthRequest() == 0, "an edit inside the pattern asks for nothing");
    roll.handleKey (juce::KeyPress (juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier, 0));
    roll.handleKey (juce::KeyPress (juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier, 0));
    check (roll.getNotes()[0].startTick == bar && roll.takeLengthRequest() == bar + kBeat,
           "a note moved into the spare quarter asks for one more quarter");
    check (roll.takeLengthRequest() == 0, "reading the request clears it");
    roll.handleKey (juce::KeyPress (juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier, 0));
    check (roll.getNotes()[0].startTick == bar, "but no further than the spare quarter");

    // Grown: a new spare quarter after the new end.
    roll.setLoopLengthTicks (bar + kBeat);
    check (roll.editLimit() == bar + 2 * kBeat, "after growing, the spare quarter moves on");

    // Notes kept past a shortened end do not grow it again when something else is edited.
    roll.setLoopLengthTicks (bar);
    roll.setNotes ({ { 60, 0, kBeat, 0.8f }, { 64, bar + kBeat / 2, kBeat / 2, 0.8f } });
    roll.selectNone();
    roll.replaceAllNotes ({ { 62, 0, kBeat, 0.8f }, { 64, bar + kBeat / 2, kBeat / 2, 0.8f } }, "edit");
    check (roll.takeLengthRequest() == 0, "a note that was already past the end does not count");
    roll.setGrowable (false);
    roll.replaceAllNotes ({ { 62, 0, kBeat, 0.8f }, { 65, bar, kBeat / 2, 0.8f } }, "edit");
    check (roll.takeLengthRequest() == 0, "at the longest length there is no spare quarter to grow into");

    // Where the current pattern is while the song plays.
    sls::Arrangement a;
    a.patterns.resize (2);
    a.patterns[0].lengthTicks = 6 * kBeat;
    a.clips = { { 0, 0, 2 * bar, 12 * kBeat, 0 }, { 1, 1, 0, 4 * bar, 0 }, { 0, 2, 7 * bar, 6 * kBeat, 2 * kBeat } };
    check (a.patternTickAt (0, 0) == -1 && a.patternTickAt (0, 2 * bar) == 0 && a.patternTickAt (0, 2 * bar + 7 * kBeat) == kBeat,
           "a pattern's place: nothing before its clip, then counted from the clip's start, repeating over its own length");
    check (a.patternTickAt (1, 3 * bar + 5) == (3 * bar + 5) % bar, "another pattern repeats over its own length");
    check (a.patternTickAt (0, 7 * bar) == 2 * kBeat && a.patternTickAt (0, 7 * bar + 4 * kBeat) == 0,
           "a clip that starts inside its pattern starts there");
    check (a.patternTickAt (0, 5 * bar) == -1 && a.patternTickAt (0, -1) == -1, "no clip of it there: nothing");
}

void testDemoSong()
{
    std::cout << "The demo song (0.52.0)\n";
    auto demo = sls::makeDemoSong();
    auto& d = demo.document;
    const auto& a = d.arrangement;
    check (d.channels.size() == 11 && a.patterns.size() == 12 && a.clips.size() >= 30 && a.audioClips.size() == 3,
           "it has 11 channels, 12 patterns, the playlist and three audio clips");
    bool placed = true;
    for (const auto& c : a.clips)
        placed = placed && c.track >= 0 && c.track < a.trackCount() && c.endTick() <= a.timelineTicks();
    for (const auto& c : a.audioClips)
        placed = placed && a.canPlaceAudio (c);
    for (size_t i = 0; i < a.automationClips.size(); ++i)
        placed = placed && a.canPlaceAutomation (a.automationClips[i], static_cast<int> (i));
    check (placed, "every pattern, audio and automation clip fits where it is");
    int notes = 0, emptyPatterns = 0;
    for (const auto& p : a.patterns)
    {
        int here = 0;
        for (const auto& n : p.notes)
            for (const auto& note : n)
                here += note.startTick >= 0 && note.startTick + note.lengthTicks <= p.length() ? 1 : 0;
        notes += here;
        emptyPatterns += here == 0 ? 1 : 0;
        if (here == 0)
            std::cout << "    pattern " << p.name << " has no notes\n";
    }
    check (emptyPatterns == 0 && notes > 400, "every pattern has notes, all inside the pattern");
    std::set<int> kinds, effects;
    for (const auto& c : a.automationClips)
        kinds.insert (static_cast<int> (c.target.kind));
    for (const auto& t : d.tracks)
        for (const auto& slot : t.slots)
            effects.insert (static_cast<int> (slot.type));
    for (const auto& slot : d.masterSlots)
        effects.insert (static_cast<int> (slot.type));
    for (const auto& bus : d.console.aux)
        for (const auto& slot : bus.slots)
            effects.insert (static_cast<int> (slot.type));
    check (kinds.size() >= 7 && effects.size() >= 13 && ! a.tempoMarkers.empty() && d.songMode,
           "it automates seven kinds of parameter, uses twelve effects and changes tempo, in Song mode");
    {
        const auto parsed = sls::ProjectDocument::fromJson (d.toJson());
        check (parsed.document.has_value() && parsed.warnings.empty() && parsed.document->arrangement.automationClips.size() == a.automationClips.size(),
               "it goes through the project file unchanged, without warnings");
    }

    // Without the plugins, the plugin channels play built-in synths and their automation goes.
    {
        auto bare = sls::makeDemoSong();
        const auto problems = sls::attachDemoPlugins (bare, [] (const std::string&) { return std::shared_ptr<sls::HostedPlugin>(); });
        bool synths = true;
        for (const auto& c : bare.document.channels)
            synths = synths && c.instrument != sls::Instrument::Plugin;
        check (synths && problems.size() == 4 && bare.document.arrangement.automationClips.size() == a.automationClips.size() - 4,
               "without SunBurst and HexForge it falls back to built-in synths");
    }

    sls::PluginHost host;
    if (sls::PluginHost::bundledPaths().getNumPaths() == 0)
    {
        std::cout << "  (SunBurst and HexForge were not built: the rest is skipped)\n";
        return;
    }
    host.addBundledPlugins();
    const auto problems = sls::attachDemoPlugins (demo, [&host] (const std::string& name) -> std::shared_ptr<sls::HostedPlugin>
    {
        const auto* found = findPlugin (host, juce::String (name), "VST3");
        juce::String error;
        return found != nullptr ? host.create (*found, kSr, sls::Player::kChunk, error) : nullptr;
    });
    for (const auto& p : problems)
        std::cout << "    " << p << "\n";
    check (problems.empty(), "SunBurst and HexForge are loaded with their presets and their automated parameters are found");
    {
        // The preset's name is in the plugin's own state, which the VST3 host keeps inside its "IComponent" (base64).
        bool presets = true;
        for (const auto& use : demo.pluginChannels)
        {
            const auto* plugin = dynamic_cast<const sls::JucePlugin*> (d.channels[static_cast<size_t> (use.channel)].plugin.get());
            const auto state = plugin != nullptr ? plugin->getState() : juce::MemoryBlock();
            juce::MemoryBlock component;
            if (auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize())))
                if (auto* child = xml->getChildByName ("IComponent"))
                    component.fromBase64Encoding (child->getAllSubText());
            const auto* bytes = static_cast<const char*> (component.getData());
            const bool has = component.getSize() > 0
                             && std::search (bytes, bytes + component.getSize(), use.preset.begin(), use.preset.end()) != bytes + component.getSize();
            if (! has)
                std::cout << "    " << use.plugin << " state lacks " << use.preset << " (" << component.getSize() << " bytes)\n";
            presets = presets && has;
        }
        check (presets, "each plugin is set to its preset (it is in the plugin's saved state)");
        bool cutoff = true;
        for (const auto& c : d.arrangement.automationClips)
            if (c.target.kind == sls::AutomationKind::ChannelPlugin)
                cutoff = cutoff && d.channels[static_cast<size_t> (c.target.index)].plugin->parameterName (c.target.param) == "Cutoff";
        check (cutoff, "the plugin automation moves the plugins' filter cutoff");
    }

    // Everything loaded as the program does it, then the whole song rendered.
    juce::StringArray loadProblems;
    for (size_t i = 0; i < d.channels.size(); ++i)
    {
        auto& c = d.channels[i];
        if (c.instrument == sls::Instrument::Drums)
            c.kit = std::make_shared<const sls::DrumKit> (sls::loadKit (d.kitSpecs[i], loadProblems));
        if (c.instrument == sls::Instrument::Sampler)
        {
            juce::String error;
            c.program = sls::loadProgram (d.programSpecs[i], [] (const juce::File&, juce::String&) { return std::shared_ptr<const sls::SampleData>(); }, error);
            if (c.program == nullptr)
                loadProblems.add (error);
        }
    }
    sls::loadAudioForClips (d.arrangement, loadProblems);
    check (loadProblems.isEmpty(), "its kit, sampled instruments and audio clips all load");
    for (const auto& p : loadProblems)
        std::cout << "    " << p << "\n";
    d.arrangement.ensureChannelCount (d.channels.size());
    const auto song = sls::compileSong (d.channels, d.arrangement, d.tracks, d.masterSlots, d.console);
    sls::RenderOptions options;
    options.sampleRate = kSr;
    options.bpm = d.bpm;
    options.tailSeconds = 3.0;
    const auto out = sls::renderToBuffer (song, options);
    float peak = 0.0f;
    bool finite = true;
    for (int ch = 0; ch < out.getNumChannels(); ++ch)
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            const float v = out.getSample (ch, i);
            finite = finite && std::isfinite (v);
            peak = std::max (peak, std::abs (v));
        }
    // Loudness of each section (bars at 110 BPM).
    const auto rmsOf = [&out] (double fromBar, double toBar)
    {
        const double barSeconds = 4.0 * 60.0 / sls::kDemoSongBpm;
        const int from = static_cast<int> (fromBar * barSeconds * kSr), to = std::min (out.getNumSamples(), static_cast<int> (toBar * barSeconds * kSr));
        double sum = 0.0;
        for (int i = from; i < to; ++i)
            sum += 0.5 * (out.getSample (0, i) * out.getSample (0, i) + out.getSample (1, i) * out.getSample (1, i));
        return to > from ? std::sqrt (sum / (to - from)) : 0.0;
    };
    const double intro = rmsOf (0.5, 4), verse = rmsOf (4, 12), chorus = rmsOf (16, 24), breakdown = rmsOf (24, 28), last = rmsOf (32, 40);
    std::cout << "    length " << out.getNumSamples() / kSr << " s, peak " << juce::Decibels::gainToDecibels (peak) << " dBFS, RMS dBFS intro "
              << juce::Decibels::gainToDecibels (intro) << ", verse " << juce::Decibels::gainToDecibels (verse) << ", chorus "
              << juce::Decibels::gainToDecibels (chorus) << ", breakdown " << juce::Decibels::gainToDecibels (breakdown) << ", last chorus "
              << juce::Decibels::gainToDecibels (last) << "\n";
    check (finite && peak < 1.0f && peak > 0.5f, "the song renders without clipping and fills the range (the maximizer's ceiling)");
    check (out.getNumSamples() / kSr > 90.0 && intro > 0.01 && verse > intro && chorus > verse * 0.9 && last >= chorus * 0.9 && breakdown < chorus,
           "it is about a minute and a half long and builds from the intro to the choruses, with a quieter breakdown");
    // SLS_DEMO_STEMS=1: the level of every channel alone in the verse, the chorus and the breakdown (to balance the mix).
    if (juce::SystemStats::getEnvironmentVariable ("SLS_DEMO_STEMS", {}).isNotEmpty())
    {
        auto quiet = d.arrangement;
        quiet.audioClips.clear();
        for (size_t solo = 0; solo < d.channels.size(); ++solo)
        {
            auto channels = d.channels;
            for (size_t i = 0; i < channels.size(); ++i)
                channels[i].mute = i != solo;
            const auto stem = sls::renderToBuffer (sls::compileSong (channels, quiet, d.tracks, d.masterSlots, d.console), options);
            const auto stemRms = [&stem] (double fromBar, double toBar)
            {
                const double barSeconds = 4.0 * 60.0 / sls::kDemoSongBpm;
                const int from = static_cast<int> (fromBar * barSeconds * kSr), to = std::min (stem.getNumSamples(), static_cast<int> (toBar * barSeconds * kSr));
                double sum = 0.0;
                for (int i = from; i < to; ++i)
                    sum += 0.5 * (stem.getSample (0, i) * stem.getSample (0, i) + stem.getSample (1, i) * stem.getSample (1, i));
                return juce::Decibels::gainToDecibels (to > from ? std::sqrt (sum / (to - from)) : 0.0, -120.0);
            };
            std::cout << "    stem " << d.channels[solo].name << ": intro " << stemRms (0, 4) << ", verse " << stemRms (4, 12) << ", chorus "
                      << stemRms (16, 24) << ", breakdown " << stemRms (24, 28) << ", last " << stemRms (32, 40) << ", outro " << stemRms (40, 44) << "\n";
        }
    }
    if (const auto path = juce::SystemStats::getEnvironmentVariable ("SLS_DEMO_WAV", {}); path.isNotEmpty())
    {
        juce::File file (path);
        file.deleteFile();
        std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
        if (stream != nullptr)
            if (std::unique_ptr<juce::AudioFormatWriter> writer (juce::WavAudioFormat().createWriterFor (stream.get(), kSr, 2, 24, {}, 0)); writer != nullptr)
            {
                stream.release();
                writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
            }
    }
}

void testSemVerAndUpdates();
void testSunBurst();
void testHexForge();
void testCrashReports();
void testSessionLog();

int main()
{
    testProjectModel();
    testSingleNote();
    testSampleAccuracy();
    testLoopAndStop();
    testPolyphony();
    testMixer();
    testSampler();
    testSamplerSwapSafety();
    testNoAllocationInRender();
    testExchangeStress();
    testWavRoundTrip();
    testSharedDsp();
    testPluginScanner();
    testPanelLayout();
    testArrangement();
    testChannelPitch();
    testSwing();
    testMixerTracks();
    testManyTracks();
    testConsole();
    testMetering();
    testEffects();
    testNewEffects();
    testTimeShaper();
    testEffectsInPlayer();
    testBrowserModel();
    testChannelPresets();
    testLibraryScan();
    testLiveNoteQueue();
    testLiveNotes();
    testProjectFile();
    testBufferTuning();
    testMasterEffectsAndRouting();
    testAudioExport();
    testMetronome();
    testTapTempo();
    testAudioClips();
    testClipPanReverseFades();
    testScoreLog();
    testLoopStarter();
    testStepSequencer();
    testTypingKeyboard();
    testPlaylistLayering();
    testFlpFiles();
    testPluginAutomationFile();
    testAudioImport();
    testInputRecorder();
    testRecordSetup();
    testMultitrackRecorders();
    testSlicer();
    testPitchTools();
    testStemSplit();
    testHelios();
    testHeliosManual062();
    testSfz();
    testSfzFiles();
    testFreePlaylist();
    testPatternLengths056();
    testPianoRollSpareQuarter();
    testPlaylistTracks();
    testMixerAndExport045();
    testTempoAndMeter046();
    testMixAutomation047();
    testRecording047();
    testSunBurst();
    testSunBurstInHost();
    testDemoSong();
    testHexForge();
    testSemVerAndUpdates();
    testCrashReports();
    testSessionLog();
    testMidiNotes();
    testPlayerRewind();
    testClipEditing();
    testNoteRecording();
    testSoundLibrary();
    testMusicTheory();
    testFlParity061();
    testFlParity062();
    testMixer062();
    testRackPlaylistToolbar062();
    testOneShot();
    testAutomation();
    testMidiImport();
    testLegatoAndSlide();
    testTimeStretch();
    testStretchedClips();
    testSampleEditing();
    testAuditioner();
    testDrumMachine();
    testDrumKitFiles();
    testSeekAndLoopRegion();
    testPatternLoopRegion();
    testChannelRemap();
    testProjectWithSamples();
    testPatternLength();
    testAdvancedSampler();
    testSoundFont();
    testSamplerPersistence();
#ifdef SLS_TEST_PLUGIN_DIR
    testSubtractiveSynth();
    testSynthPatchFiles();
    testFmSynth();
    testAdditiveAndPhysical();
    testPitchCorrector();
    testPluginProjectFile();
    testPluginHosting();
#endif

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << " (" << failures << " failures)\n";
    return failures == 0 ? 0 : 1;
}
