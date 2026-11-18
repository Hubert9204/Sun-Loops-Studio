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
    int defaultKeyswitch = -1;
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
        r.seqLength = z.seqLength;
        r.seqPosition = std::min (z.seqPosition, z.seqLength);
        r.keytrack = static_cast<float> (z.keytrack);
        r.oneShot = z.oneShot;
        r.releaseSeconds = static_cast<float> (z.ampRelease);
        if (z.swLast >= 0) // 0.84.0: an SFZ keyswitch: the zone belongs to the articulation that key chooses
        {
            int found = inst->keyswitchArticulation (z.swLast);
            if (found < 0)
            {
                inst->articulations.push_back ({ z.swLabel.empty() ? "Keyswitch " + std::to_string (z.swLast) : z.swLabel, z.swLast });
                found = static_cast<int> (inst->articulations.size()) - 1;
            }
            else if (! z.swLabel.empty() && inst->articulations[static_cast<size_t> (found)].name.rfind ("Keyswitch ", 0) == 0)
                inst->articulations[static_cast<size_t> (found)].name = z.swLabel;
            r.articulation = found;
        }
        if (z.swDefault >= 0)
            defaultKeyswitch = z.swDefault;
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
    if (const int a = inst->keyswitchArticulation (defaultKeyswitch); a >= 0)
        inst->defaultArticulation = a;
    return inst;
}

std::shared_ptr<const Instrument> Instrument::combine (const std::string& id, const std::string& name,
                                                       const std::vector<std::pair<std::string, std::shared_ptr<const Instrument>>>& parts)
{
    std::vector<std::pair<std::string, std::shared_ptr<const Instrument>>> given;
    for (const auto& p : parts)
        if (p.second != nullptr)
            given.push_back (p);
    if (given.empty())
        return nullptr;
    auto inst = std::make_shared<Instrument>();
    inst->id = id;
    inst->name = name;
    inst->attackSeconds = given.front().second->attackSeconds;
    inst->releaseSeconds = given.front().second->releaseSeconds;
    int lowest = 127, lo = 127, hi = 0;
    for (const auto& [n, part] : given)
    {
        lo = std::min (lo, part->playableLo);
        hi = std::max (hi, part->playableHi);
        for (const auto& r : part->regions)
            lowest = std::min (lowest, r.loKey);
    }
    const int count = static_cast<int> (given.size());
    const int first = std::clamp (std::min (24, std::max (lowest, lo) - count), 0, 127 - count);
    for (int a = 0; a < count; ++a)
    {
        const auto& part = *given[static_cast<size_t> (a)].second;
        inst->articulations.push_back ({ given[static_cast<size_t> (a)].first, first + a });
        for (auto r : part.regions)
        {
            r.articulation = a;
            if (r.releaseSeconds < 0.0f)
                r.releaseSeconds = part.releaseSeconds;
            if (r.loKey <= first + count - 1 && r.hiKey >= first) // (a zone over the keyswitches would never be heard there)
                r.loKey = std::min (r.hiKey, first + count);
            inst->regions.push_back (std::move (r));
        }
    }
    // the keyswitches must be playable keys of the instrument (they sound nothing)
    inst->playableLo = std::min (lo, first);
    inst->playableHi = hi;
    return inst;
}
} // namespace orchestra
