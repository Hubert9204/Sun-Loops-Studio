// Headless tests for the audio core: project model, player (synth, sampler, mixer),
// snapshot exchange, offline renderer and WAV output. No audio device or display needed.

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <thread>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

#include "audio/BuiltinSamples.h"
#include "audio/OfflineRenderer.h"
#include "audio/Player.h"
#include "audio/Project.h"
#include "audio/SnapshotExchange.h"
#include "plugins/PluginScanner.h"

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

void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

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

    std::thread audio ([&]
    {
        while (! stop.load())
        {
            const auto* p = exchange.acquire();
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
    for (int round = 0; round < 3000; ++round)
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
    }

    stop = true;
    audio.join();
    check (! corrupt.load() && reads.load() > 0, "3000 publishes against a concurrent reader: no torn or freed snapshot");
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

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << " (" << failures << " failures)\n";
    return failures == 0 ? 0 : 1;
}
