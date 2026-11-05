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
#include "audio/BuiltinSamples.h"
#include "audio/Effects.h"
#include "audio/LiveNotes.h"
#include "audio/OfflineRenderer.h"
#include "audio/Player.h"
#include "audio/Project.h"
#include "audio/SnapshotExchange.h"
#include "library/BrowserModel.h"
#include "library/ChannelPreset.h"
#include "library/Library.h"
#include "plugins/PluginScanner.h"
#include "ui/PanelLayout.h"

// Several checks below deliberately compare floats with ==: "exactly zero" means silence or
// "no contribution at all", which is the property being tested.
#pragma GCC diagnostic ignored "-Wfloat-equal"

// --- allocation counter, used to prove that Player::render never allocates ---------
static std::atomic<long> gAllocs { 0 };
static std::atomic<bool> gCounting { false };

void* operator new (std::size_t n)
{
    if (gCounting.load (std::memory_order_relaxed))
        gAllocs.fetch_add (1, std::memory_order_relaxed);
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
    check (paths.ensureExists() && paths.samples.isDirectory() && paths.drumKits.isDirectory() && paths.presets.isDirectory(),
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
    check (instruments != nullptr && instruments->children.size() == 2 && instruments->children[0].payload == "synth",
           "instruments: Synth and Sampler");

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

} // namespace

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
    testEffects();
    testEffectsInPlayer();
    testBrowserModel();
    testChannelPresets();
    testLibraryScan();
    testLiveNoteQueue();
    testLiveNotes();

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << " (" << failures << " failures)\n";
    return failures == 0 ? 0 : 1;
}
