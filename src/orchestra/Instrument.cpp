#include "orchestra/Instrument.h"

#include <algorithm>
#include <map>

#include "library/Sfz.h"

namespace orchestra
{
std::shared_ptr<const Instrument> Instrument::makeTestTone (double sampleRate)
{
    auto inst = std::make_shared<Instrument>();
    inst->id = "test-tone";
    inst->name = "Test Tone";
    inst->attackSeconds = 0.01f;
    inst->releaseSeconds = 0.3f;
    // One sample per octave, rooted on each C, covering the octave around it (the sample is transposed by at most six semitones).
    for (int root = 24; root <= 108; root += 12)
    {
        auto buffer = std::make_shared<SampleBuffer>();
        buffer->sampleRate = sampleRate;
        buffer->source = "test-tone";
        const double f = midiToHz (root);
        // An exact number of periods, so the loop over the whole sample has no seam.
        const int periods = std::max (8, static_cast<int> (std::round (f * 0.25)));
        const int frames = static_cast<int> (std::round (periods * sampleRate / f));
        const double exactF = periods * sampleRate / frames;
        buffer->left.resize (static_cast<size_t> (frames));
        for (int i = 0; i < frames; ++i)
        {
            const double ph = 2.0 * 3.14159265358979323846 * exactF * i / sampleRate;
            buffer->left[static_cast<size_t> (i)] = static_cast<float> (0.5 * std::sin (ph) + 0.15 * std::sin (2.0 * ph) + 0.06 * std::sin (3.0 * ph));
        }
        Region r;
        r.sample = buffer;
        r.rootKey = root;
        r.tuneCents = static_cast<float> (1200.0 * std::log2 (f / exactF)); // the few cents the whole-period length moved it
        r.loKey = root == 24 ? 0 : root - 6;
        r.hiKey = root == 108 ? 127 : root + 5;
        r.loop = LoopMode::Forward;
        r.loopStart = 0;
        r.loopEnd = frames;
        inst->regions.push_back (std::move (r));
    }
    return inst;
}

std::shared_ptr<const Instrument> Instrument::fromSfz (const std::string& sfzText, const std::string& id, const std::string& name,
                                                       const std::function<std::shared_ptr<const SampleBuffer> (const std::string&)>& loadSample,
                                                       std::string& error)
{
    const auto parsed = sls::sfz::parse (sfzText);
    auto inst = std::make_shared<Instrument>();
    inst->id = id;
    inst->name = name;
    std::map<std::string, std::shared_ptr<const SampleBuffer>> loaded; // a sample used by several zones is read once
    int missing = 0;
    for (const auto& z : parsed.regions)
    {
        auto found = loaded.find (z.sample);
        if (found == loaded.end())
            found = loaded.emplace (z.sample, loadSample != nullptr ? loadSample (z.sample) : nullptr).first;
        if (found->second == nullptr)
        {
            ++missing;
            continue;
        }
        Region r;
        r.sample = found->second;
        r.loKey = std::clamp (z.loKey, 0, 127);
        r.hiKey = std::clamp (z.hiKey, r.loKey, 127);
        r.rootKey = std::clamp (z.rootKey, 0, 127);
        r.loVel = std::clamp (z.loVel, 1, 127);
        r.hiVel = std::clamp (z.hiVel, r.loVel, 127);
        r.tuneCents = static_cast<float> (z.tuneCents);
        r.gainDb = static_cast<float> (z.volumeDb);
        r.pan = static_cast<float> (std::clamp (z.pan / 100.0, -1.0, 1.0));
        if (z.loop && z.loopEnd > z.loopStart && z.loopStart >= 0)
        {
            r.loop = LoopMode::Forward;
            r.loopStart = static_cast<int> (z.loopStart);
            r.loopEnd = static_cast<int> (std::min<long long> (z.loopEnd + 1, r.sample->frames())); // SFZ's loop_end is inclusive
        }
        inst->regions.push_back (std::move (r));
    }
    if (inst->regions.empty())
    {
        error = parsed.regions.empty() ? "The SFZ file has no zones." : std::to_string (missing) + " sample(s) could not be read.";
        return nullptr;
    }
    if (missing > 0)
        error = std::to_string (missing) + " sample(s) could not be read; their zones are left out.";
    return inst;
}
} // namespace orchestra
