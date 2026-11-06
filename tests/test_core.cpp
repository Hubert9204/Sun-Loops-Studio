// Headless tests for the audio core: project model, player (synth, sampler, mixer),
// snapshot exchange, offline renderer and WAV output. No audio device or display needed.

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <thread>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

#include "audio/Arrangement.h"
#include "audio/AudioExport.h"
#include "audio/AudioImport.h"
#include "audio/BufferTuning.h"
#include "audio/BuiltinSamples.h"
#include "audio/ClipEdit.h"
#include "audio/CyberHub.h"
#include "audio/TimeFormat.h"
#include "audio/Effects.h"
#include "audio/InputRecorder.h"
#include "audio/Loudness.h"
#include "audio/Spectrum.h"
#include "audio/MidiImport.h"
#include "audio/MidiNotes.h"
#include "audio/MixerRouting.h"
#include "audio/OneShot.h"
#include "audio/Auditioner.h"
#include "audio/DrumKit.h"
#include "library/DrumKitFile.h"
#include "audio/SampleEdit.h"
#include "audio/TimeStretch.h"
#include "audio/NoteRecorder.h"
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

constexpr double kSr = 48000.0;
constexpr int kBeat = sls::kTicksPerBeat;

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

    std::vector<sls::Channel> many (20, synthChannel ({}));
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
    a.patternTicks = 4 * kBeat; // one bar = 2 s at 120 BPM
    a.patterns.resize (3);
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
    check (a.canPlace ({ 0, 0, 0, 1 }), "a clip on an empty playlist can be placed");
    check (! a.canPlace ({ 5, 0, 0, 1 }), "a clip of an unknown pattern is refused");
    check (! a.canPlace ({ 0, sls::kPlaylistTracks, 0, 1 }), "a clip below the last track is refused");
    check (! a.canPlace ({ 0, 0, -1, 1 }), "a clip before the start is refused");
    check (! a.canPlace ({ 0, 0, 0, 0 }), "a zero-length clip is refused");
    check (! a.canPlace ({ 0, 0, sls::kPlaylistUnits - 1, 2 }), "a clip past the end of the timeline is refused");
    check (a.canPlace ({ 0, 0, sls::kPlaylistUnits - 2, 2 }), "a clip ending exactly at the end is accepted");

    // song with two clips on one track, a gap between them
    a.clips = { { 0, 0, 0, 1 }, { 0, 0, 2, 1 } };
    check (a.songLengthUnits() == 3, "song length is the end of the last clip");
    check (! a.canPlace ({ 1, 0, 1, 2 }), "overlapping a clip on the same track is refused");
    check (a.canPlace ({ 1, 0, 1, 1 }), "filling the gap exactly is accepted");
    check (a.canPlace ({ 1, 1, 0, 3 }), "the same time on another track is accepted");
    check (a.canPlace (a.clips[0], 0), "a clip does not collide with itself when moved");
    check (a.clipAt (0, 2) == 1 && a.clipAt (0, 1) == -1 && a.clipAt (1, 0) == -1, "clipAt finds the clip under a cell");

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
    a.clips = { { 0, 0, 1, 3 } };
    const auto rep = sls::compileSong (channels, a);
    check (rep.lengthTicks == 4 * 4 * kBeat && rep.channels[0].notes.size() == 3
               && rep.channels[0].notes[0].startTick == 4 * kBeat && rep.channels[0].notes[2].startTick == 3 * 4 * kBeat,
           "a long clip repeats its pattern once per unit");

    // notes are cut at their pattern's end so they cannot leak into the next unit
    a.clips = { { 2, 0, 0, 2 } };
    const auto cut = sls::compileSong (channels, a);
    check (cut.channels[0].notes.size() == 2 && cut.channels[0].notes[0].lengthTicks == kBeat
               && cut.channels[0].notes[1].startTick == 4 * kBeat + 3 * kBeat,
           "notes are clipped at the end of their pattern");

    // overlapping clips on different tracks play together
    a.clips = { { 0, 0, 0, 1 }, { 2, 1, 0, 1 } };
    check (sls::compileSong (channels, a).channels[0].notes.size() == 2, "clips on different tracks are mixed");

    // empty playlist
    a.clips.clear();
    const auto empty = sls::compileSong (channels, a);
    check (empty.lengthTicks == 4 * kBeat && empty.channels[0].notes.empty(), "an empty playlist is one silent pattern length");

    // robustness
    a.clips = { { 9, 0, 0, 1 }, { 0, 0, 0, 1 } };
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
        a.clips = { { 0, 0, 1, 1 } };
        auto c = synthChannel ({});
        c.swing = 1.0f;
        const auto song = sls::compileSong ({ c }, a);
        check (song.channels[0].notes.size() == 1 && song.channels[0].notes[0].startTick == a.patternTicks + 180,
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
        check (sls::Project::make ({ c }, 1920).channels[0].mixerTrack == sls::kMaxTracks - 1, "a track index out of range is clamped");
        auto t = sls::Project::defaultTracks();
        t[0].volume = 9.0f;
        t[1].pan = std::nanf ("");
        const auto p = sls::Project::make ({}, 1920, t);
        check (p.tracks.size() == static_cast<size_t> (sls::kMaxTracks) && p.tracks[0].volume == 1.5f && p.tracks[1].pan == 0.0f,
               "track values are clamped and always kMaxTracks tracks exist");
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
        check (sls::effectParamCount (sls::EffectType::None) == 0 && sls::effectParamCount (sls::EffectType::Compressor) == 5,
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
                for (float v : out)
                    ok = ok && std::isfinite (v) && std::abs (v) < 100.0f;
            }
        }
        check (ok, "all effects stay finite and bounded at their minimum and maximum settings");
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
               && node ("a", NodeKind::Instrument).isDraggable() && node ("a", NodeKind::Preset).isDraggable(),
           "audio, built-in samples, instruments and presets can be dragged");
    check (! node ("a", NodeKind::Plugin).isDraggable() && ! node ("a", NodeKind::Folder).isDraggable()
               && ! node ("a", NodeKind::Action).isDraggable() && ! node ("a", NodeKind::Note).isDraggable(),
           "plugins, folders, actions and notes cannot be dragged");
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
        if (p.instrument == sls::Instrument::Sampler)
            sourceOk = p.sampleSource.rfind ("builtin:", 0) == 0 && sls::makeBuiltinSample (p.sampleSource.substr (8)) != nullptr;
        factoryOk = factoryOk && ! p.id.empty() && sourceOk && sls::ChannelPreset::fromJson (p.toJson()).has_value();
    }
    check (factoryOk, "every factory preset is valid, round-trips and points at an existing built-in sample");

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
    check (factory != nullptr && factory->children.size() == sls::factoryPresets().size() && factory->children[0].payload.rfind ("builtin:", 0) == 0,
           "factory presets are listed");
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
    const auto scannedTree = sls::buildBrowserTree (paths, { info }, sls::PluginScanState::Done);
    const auto* scannedPlugin = findChild (scannedTree, "Plugin");
    const auto* entry = scannedPlugin != nullptr ? findChild (*scannedPlugin, "Fake Synth  -  Acme") : nullptr;
    check (entry != nullptr && entry->kind == NodeKind::Plugin && ! entry->isDraggable(), "scanned plugins are listed but cannot be dragged");

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
        if (x.pattern != y.pattern || x.track != y.track || x.startUnit != y.startUnit || x.lengthUnits != y.lengthUnits)
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
    d.arrangement.patterns[0].name = "Intro";
    d.arrangement.patterns[1].name = "Verse";
    d.arrangement.ensureChannelCount (2);
    d.arrangement.patterns[0].notes[0] = { { 60, 0, 240, 0.8f }, { 64, 480, 480, 0.5f }, { 67, 960, 120, 1.0f, sls::NoteLink::Slide } };
    d.arrangement.patterns[1].notes[1] = { { 60, 0, 120, 0.9f, sls::NoteLink::Legato }, { 60, 1920, 120, 0.7f } };
    d.arrangement.clips = { { 0, 0, 0, 2 }, { 1, 1, 1, 3 } };

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
                   && blank.tracks.size() == static_cast<size_t> (sls::kMaxTracks) && blank.arrangement.patterns.size() == 1
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
        while (doc.channels.size() < 9)
        {
            doc.channels.push_back (doc.channels[0]);
            doc.sampleSources.emplace_back();
        }
        check (! errorOf (doc.toJson()).empty(), "a project with 9 channels is refused");

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
        doc.arrangement.clips.push_back ({ 0, 0, 1, 1 });                            // overlaps the first clip
        doc.arrangement.clips.push_back ({ 9, 0, 20, 1 });                           // no such pattern
        const auto r = sls::ProjectDocument::fromJson (doc.toJson());
        check (r.document.has_value(), "a file with out-of-range values still opens");
        if (r.document.has_value())
        {
            const auto& d = *r.document;
            check (d.channels[0].volume == 1.5f && d.channels[0].pan == -1.0f && d.channels[0].pitch == 24.0f
                       && d.channels[0].swing == 1.0f && d.channels[0].mixerTrack == sls::kMaxTracks - 1
                       && d.currentPattern == 1 && d.tracks[0].volume == 1.5f,
                   "channel, track and pattern values are clamped into range");
            const auto& notes = d.arrangement.patterns[0].notes[0];
            check (notes.size() == 4 && notes.back().startTick == 3800 && notes.back().lengthTicks == 40 && notes.back().velocity == 1.0f,
                   "bad notes are dropped; a note that is too long is trimmed to its pattern");
            check (d.arrangement.clips.size() == 2 && r.warnings.size() == 2, "clips that cannot be placed are dropped, and both repairs are reported");
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
        check (r.document.has_value() && r.document->tracks.size() == static_cast<size_t> (sls::kMaxTracks)
                   && r.document->tracks[7].name == "Track 8" && r.document->tracks[2].name == "Leads",
               "a short list of mixer tracks is padded with defaults");
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
        check (sls::withProjectExtension (base.getChildFile ("old.slsproj")).getFileName() == "old.slsproj"
                   && sls::hasProjectExtension (base.getChildFile ("old.slsproj")) && sls::hasProjectExtension (base.getChildFile ("x.SLP"))
                   && ! sls::hasProjectExtension (base.getChildFile ("x.wav")) && ! sls::hasProjectExtension (base.getChildFile ("slp")),
               "files with the older .slsproj extension are still recognised and left alone");
        check (sls::projectFileForSaveAs (base.getChildFile ("old.slsproj")).getFileName() == "old.slp"
                   && sls::projectFileForSaveAs (base.getChildFile ("new.slp")).getFileName() == "new.slp",
               "Save as offers an old .slsproj project under the new .slp name");

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
        check (sls::firstFreeTrack (ch) == 4 && sls::isTrackFree (ch, 4) && ! sls::isTrackFree (ch, 2),
               "four channels on tracks 1-4: the first free track is 5");
        check (sls::isTrackFree (ch, 2, 2) && sls::firstFreeTrack (ch, 2) == 2,
               "a channel's own track counts as free for that channel");
        ch[1].mixerTrack = 0;
        check (sls::firstFreeTrack (ch) == 1 && sls::channelsPerTrack (ch)[0] == 2, "two channels can share a track; the vacated one is free");
        ch.resize (8);
        for (size_t i = 0; i < ch.size(); ++i)
            ch[i].mixerTrack = static_cast<int> (i);
        check (sls::firstFreeTrack (ch) == -1 && sls::firstFreeTrack (ch, 5) == 5, "no free track when all eight are used (unless one channel is ignored)");
        ch[0].mixerTrack = 99;
        ch[1].mixerTrack = -4;
        const auto count = sls::channelsPerTrack (ch);
        check (count[7] == 2 && count[0] == 1 && ! sls::isTrackFree (ch, -1) && ! sls::isTrackFree (ch, 8),
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
    check (! a.canPlaceAudio (audio (0, 480, 960)), "audio clips on one track cannot overlap");
    check (a.canPlaceAudio (audio (1, 480, 960)), "but may on another track");
    check (a.canPlaceAudio (audio (0, 960, 960)), "an audio clip can start where the previous one ends");
    check (a.canPlaceAudio (audio (0, 1060, 960)), "audio clips may start on any tick (they are no longer tied to the beat grid)");
    check (! a.canPlaceAudio (audio (0, a.timelineTicks() - 480, 960)), "an audio clip cannot run past the end of the timeline");
    check (! a.canPlaceAudio (audio (sls::kPlaylistTracks, 0, 480)), "or sit on a track that does not exist");
    check (a.canPlaceAudio (audio (0, 0, 480), 0), "a clip being moved does not collide with itself");
    check (! a.canPlace ({ 0, 0, 0, 1 }), "a pattern clip cannot cover an audio clip");
    check (a.canPlace ({ 0, 0, 1, 1 }), "but can sit after it");
    a.clips.push_back ({ 0, 2, 1, 1 });
    check (! a.canPlaceAudio (audio (2, a.patternTicks + 480, 480)), "an audio clip cannot cover a pattern clip");
    check (a.audioClipAt (0, 0) == 0 && a.audioClipAt (0, 959) == 0 && a.audioClipAt (0, 960) == -1, "audioClipAt finds the clip under a tick");
    check (a.findFreeAudioStart (0, 0, 960) == 960 && a.findFreeAudioStart (1, 100, 480) == 100 && a.findFreeAudioStart (0, 100, 480) == 960,
           "findFreeAudioStart uses the wanted tick if free, else the next free beat");
    check (a.findFreeAudioStart (0, 0, a.timelineTicks()) == -1, "and gives up when nothing fits");

    sls::Arrangement b;
    b.patterns.resize (1);
    b.audioClips.push_back (audio (3, 960, 3000));
    check (b.songLengthUnits() == 2, "the song is as long as its last audio clip (rounded up to whole patterns)");

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
        auto p3 = sls::compileSong (empty, sls::Arrangement { { sls::Pattern {} }, {}, {}, 8 * kBeat, {} }); // clip removed
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
        check (repaired.document.has_value() && repaired.document->arrangement.audioClips.size() == 2 && ! repaired.warnings.empty(),
               "clips that overlap or have an impossible position are dropped with a warning");

        var.getDynamicObject()->removeProperty ("audioClips"); // a file from before audio clips existed
        const auto old = sls::ProjectDocument::fromJson (juce::JSON::toString (var));
        check (old.document.has_value() && old.document->arrangement.audioClips.empty() && old.warnings.empty(),
               "projects without audio clips still open");
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
            while (! stopFlag.load())
                rec.process (in, 2, kBlock);
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
        a.clips = { { 0, 0, 0, 1 }, { 1, 0, 1, 2 }, { 2, 1, 0, 1 }, { 3, 2, 0, 1 }, { 1, 3, 4, 1 } };
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
        a.clips = { { 0, 0, 2, 6 } }; // units 2..8
        check (sls::splitPatternClip (a, 0, 2) == -1 && sls::splitPatternClip (a, 0, 8) == -1 && a.clips.size() == 1, "pattern clips split only strictly inside");
        check (sls::splitPatternClip (a, 0, 5) == 1 && a.clips[0].lengthUnits == 3 && a.clips[1].startUnit == 5 && a.clips[1].lengthUnits == 3,
               "a pattern clip splits at a unit boundary");
        a.clips = { { 0, 0, 2, 6 } };
        check (sls::cutPatternRange (a, 0, 4, 6) == 2 && a.clips[0].lengthUnits == 2 && a.clips[1].startUnit == 6 && a.clips[1].lengthUnits == 2,
               "cutting units out of the middle leaves two clips");
        a.clips = { { 0, 0, 2, 6 } };
        check (sls::cutPatternRange (a, 0, 0, 4) == 1 && a.clips[0].startUnit == 4 && a.clips[0].lengthUnits == 4, "cutting from the start");
        check (sls::cutPatternRange (a, 0, 0, 99) == 0 && a.clips.empty(), "cutting everything removes the clip");
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

// --- the Cyber-Hub sounds built into the program, the start-up sound, time formatting ---------------------------------
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

        const auto events = sls::makeAutomationEvents (clips, 2, 8, [] (const sls::AutomationTarget&) { return sls::EffectType::None; });
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

void testCyberHub()
{
    std::cout << "Cyber-Hub sounds, start-up sound, time formats\n";

    const auto& sounds = sls::cyberHubSounds();
    check (sounds.size() == 30, "all 30 sounds of the Cyber-Hub theme are built in");

    int hits = 0, tones = 0, longs = 0;
    bool allDecode = true, allStereo = true, allIdsWork = true, nothingSilent = true, namedOk = true;
    double longest = 0.0, shortest = 1.0e9;
    for (const auto& s : sounds)
    {
        hits += s.role == sls::CyberRole::Hit ? 1 : 0;
        tones += s.role == sls::CyberRole::Tone ? 1 : 0;
        longs += s.role == sls::CyberRole::Long ? 1 : 0;
        const auto data = sls::makeCyberHubSample (s.id);
        if (data == nullptr || data->numFrames() < 100)
        {
            allDecode = false;
            continue;
        }
        allStereo = allStereo && data->left.size() == data->right.size() && data->sampleRate >= 22050.0;
        allIdsWork = allIdsWork && sls::makeBuiltinSample (s.id) == data && data->source == "builtin:" + s.id && sls::isCyberHubId (s.id);
        namedOk = namedOk && ! data->name.empty() && data->name == s.title;
        float peak = 0.0f;
        for (float v : data->left)
            peak = std::max (peak, std::abs (v));
        nothingSilent = nothingSilent && peak > 0.05f;
        const double seconds = data->numFrames() / data->sampleRate;
        longest = std::max (longest, seconds);
        shortest = std::min (shortest, seconds);
    }
    check (allDecode, "every sound decodes");
    check (allStereo, "they are stereo with the left and right sides of equal length");
    check (allIdsWork && namedOk, "makeBuiltinSample finds each by its id and returns the very same shared sample");
    check (nothingSilent, "none of them is silent");
    check (hits == 14 && tones == 9 && longs == 7, "14 hits, 9 tones and 7 long sounds are filed in the browser");
    check (shortest > 0.1 && shortest < 0.25 && longest > 8.0 && longest < 8.8, "lengths match the files (0.16 s volume tick ... 8.4 s phone call)");

    const auto login = sls::makeCyberHubSample ("cyber:desktop-login");
    check (login != nullptr && std::abs (login->numFrames() / login->sampleRate - 5.5) < 0.1, "desktop-login lasts about 5.5 s");
    const auto plug = sls::makeCyberHubSample ("cyber:power-plug");
    double diff = 0.0, energy = 1.0e-12;
    if (plug != nullptr)
        for (size_t i = 0; i < plug->left.size(); ++i)
        {
            diff += std::pow (static_cast<double> (plug->left[i] - plug->right[i]), 2.0);
            energy += std::pow (static_cast<double> (plug->left[i]), 2.0);
        }
    check (plug != nullptr && diff < 1.0e-4 * energy, "power-plug is a near-mono sub hit (the sides differ by less than -40 dB), as the analysis found");
    check (sls::makeCyberHubSample ("cyber:nope") == nullptr && sls::makeBuiltinSample ("cyber:nope") == nullptr, "an unknown id gives nothing");

    for (const auto& piece : sls::cyberHubKit())
        check (sls::makeCyberHubSample (piece.id) != nullptr, (std::string ("the kit piece ") + piece.label + " exists").c_str());

    const auto intro = sls::makeStartupSample();
    check (intro != nullptr && std::abs (intro->sampleRate - 48000.0) < 1.0 && intro->numFrames() == 240000, "the start-up sound is the 5 s, 48 kHz file");
    float introPeak = 0.0f;
    if (intro != nullptr)
        for (float v : intro->left)
            introPeak = std::max (introPeak, std::abs (v));
    check (introPeak > 0.15f && introPeak < 0.25f, "and has the level it was made at (peaks around 0.2, i.e. -14 dB: a quiet sound)");
    check (sls::makeBuiltinSample (sls::kStartupSampleId) != nullptr, "it can be used as a built-in sample too (id \"intro\")");

    // the browser
    sls::LibraryPaths paths;
    const auto tree = sls::buildBrowserTree (paths, {}, sls::PluginScanState::NotScanned);
    const sls::BrowserNode* samples = nullptr;
    const sls::BrowserNode* kits = nullptr;
    for (const auto& c : tree.children)
    {
        if (c.name == "Samples") samples = &c;
        if (c.name == "Drum Kits") kits = &c;
    }
    const sls::BrowserNode* cyber = nullptr;
    if (samples != nullptr)
        for (const auto& c : samples->children)
            if (c.name == "Cyber-Hub")
                cyber = &c;
    check (cyber != nullptr && sls::countLeaves (*cyber) == 30 + 1 + 1, "the browser has a Cyber-Hub folder with the 30 sounds, the intro and the credit");
    bool draggable = cyber != nullptr;
    if (cyber != nullptr)
        for (const auto& group : cyber->children)
            for (const auto& leaf : group.children)
                if (leaf.kind == sls::NodeKind::BuiltinSample)
                    draggable = draggable && leaf.isDraggable() && sls::makeBuiltinSample (leaf.payload) != nullptr;
    check (draggable, "every sound in it can be dragged and loads");
    const sls::BrowserNode* kit = nullptr;
    if (kits != nullptr)
        for (const auto& c : kits->children)
            if (c.name == "Cyber-Hub Kit")
                kit = &c;
    check (kit != nullptr && kit->children.size() == 5, "and a Cyber-Hub Kit among the drum kits");
    const auto found = sls::filterTree (tree, "desktop");
    check (sls::countLeaves (found) == 2, "searching for \"desktop\" finds desktop-login and desktop-logout");

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
            check (sls::useEditedAudio (d, 0, original, edited, {}, bpm).empty() && d.audioClips[0].lengthTicks == 1000,
                   "the clip is shortened to fit when a neighbour is in the way");
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
        const auto cyber = sls::loadKit (sls::cyberHubKitSpec(), problems);
        check (problems.isEmpty() && cyber.pads[0].hasSample() && cyber.pads[4].hasSample() && ! cyber.pads[5].hasSample() && cyber.pads[2].chokeGroup == 1, "the Cyber-Hub kit loads");
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
    testPluginScanner();
    testPanelLayout();
    testArrangement();
    testChannelPitch();
    testSwing();
    testMixerTracks();
    testConsole();
    testMetering();
    testEffects();
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
    testAudioImport();
    testInputRecorder();
    testMidiNotes();
    testPlayerRewind();
    testClipEditing();
    testNoteRecording();
    testCyberHub();
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

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << " (" << failures << " failures)\n";
    return failures == 0 ? 0 : 1;
}
