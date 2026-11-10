#include "audio/PitchTrack.h"

#include <algorithm>
#include <cmath>

#include "audio/TimeStretch.h"

namespace sls::pitchtrack
{
int Note::nearestKey() const noexcept
{
    return std::clamp (static_cast<int> (std::lround (midi + shift)), 0, 127);
}

double Note::centsOff() const noexcept
{
    return (midi + shift - std::round (midi + shift)) * 100.0;
}

std::vector<Frame> track (const SampleData& in, double hopSeconds, double minHz, double maxHz)
{
    std::vector<Frame> frames;
    const int total = in.numFrames();
    const double rate = in.sampleRate;
    if (total < 64 || rate <= 0.0 || hopSeconds <= 0.0 || minHz <= 0.0 || maxHz <= minHz)
        return frames;

    // Mono, decimated to 12-24 kHz (enough for pitches up to 1.5 kHz and four times cheaper at 48 kHz).
    const int factor = std::max (1, static_cast<int> (rate / 12000.0));
    const double r = rate / factor;
    std::vector<float> x (static_cast<size_t> (total / factor));
    for (size_t i = 0; i < x.size(); ++i)
    {
        float sum = 0.0f;
        for (int k = 0; k < factor; ++k)
        {
            const auto f = i * static_cast<size_t> (factor) + static_cast<size_t> (k);
            sum += 0.5f * (in.left[f] + in.right[f]);
        }
        x[i] = sum / static_cast<float> (factor);
    }

    const int maxLag = std::min (static_cast<int> (r / minHz) + 2, static_cast<int> (x.size()) / 2);
    const int minLag = std::max (2, static_cast<int> (r / maxHz));
    const int window = maxLag; // one period of the lowest pitch
    if (maxLag <= minLag + 2 || static_cast<int> (x.size()) < window + maxLag)
        return frames;
    const int hop = std::max (1, static_cast<int> (std::lround (hopSeconds * r)));
    std::vector<double> d (static_cast<size_t> (maxLag + 1));

    for (int start = 0; start + window + maxLag <= static_cast<int> (x.size()); start += hop)
    {
        Frame fr;
        fr.seconds = (start + 0.5 * (window + maxLag)) / r;
        double energy = 0.0;
        for (int i = 0; i < window; ++i)
            energy += static_cast<double> (x[static_cast<size_t> (start) + static_cast<size_t> (i)]) * x[static_cast<size_t> (start) + static_cast<size_t> (i)];
        fr.level = std::sqrt (energy / window);

        if (fr.level > 1.0e-3) // about -60 dBFS: quieter is silence
        {
            // YIN: difference function, cumulative mean normalised; the first dip under the threshold, refined by a parabola.
            d[0] = 1.0;
            double running = 0.0;
            for (int lag = 1; lag <= maxLag; ++lag)
            {
                double sum = 0.0;
                for (int i = 0; i < window; ++i)
                {
                    const double diff = static_cast<double> (x[static_cast<size_t> (start) + static_cast<size_t> (i)]) - x[static_cast<size_t> (start) + static_cast<size_t> (i) + static_cast<size_t> (lag)];
                    sum += diff * diff;
                }
                running += sum;
                d[static_cast<size_t> (lag)] = running > 0.0 ? sum * lag / running : 1.0;
            }
            int best = -1;
            for (int lag = minLag; lag < maxLag; ++lag)
                if (d[static_cast<size_t> (lag)] < 0.15)
                {
                    while (lag + 1 < maxLag && d[static_cast<size_t> (lag) + 1] < d[static_cast<size_t> (lag)])
                        ++lag;
                    best = lag;
                    break;
                }
            if (best > 0)
            {
                const double a = d[static_cast<size_t> (best) - 1], b = d[static_cast<size_t> (best)], c = d[static_cast<size_t> (best) + 1];
                const double denom = a - 2.0 * b + c;
                const double period = best + (std::abs (denom) > 1.0e-12 ? 0.5 * (a - c) / denom : 0.0);
                if (period > 0.0)
                {
                    fr.hz = r / period;
                    fr.midi = 69.0 + 12.0 * std::log2 (fr.hz / 440.0);
                }
            }
        }
        frames.push_back (fr);
    }
    return frames;
}

std::vector<Note> notesFrom (const std::vector<Frame>& frames, double minSeconds, double toleranceSemitones, double bridgeSeconds)
{
    std::vector<Note> notes;
    if (frames.size() < 2)
        return notes;
    const double hop = frames[1].seconds - frames[0].seconds;
    if (hop <= 0.0)
        return notes;

    std::vector<double> run; // the pitches of the note being built
    double runStart = 0.0, runEnd = 0.0, runLevel = 0.0;
    int gap = 0;
    const int bridge = static_cast<int> (bridgeSeconds / hop);

    const auto median = [] (std::vector<double> v)
    {
        std::nth_element (v.begin(), v.begin() + static_cast<std::ptrdiff_t> (v.size() / 2), v.end());
        return v[v.size() / 2];
    };
    const auto close = [&]
    {
        if (! run.empty() && runEnd - runStart >= minSeconds)
            notes.push_back ({ runStart, runEnd, median (run), 0.0, runLevel });
        run.clear();
        gap = 0;
    };

    for (const auto& f : frames)
    {
        if (f.hz <= 0.0)
        {
            if (! run.empty() && ++gap > bridge)
                close();
            continue;
        }
        if (! run.empty())
        {
            // the running centre: the median of the last 10 frames (a slide or vibrato moves it slowly)
            std::vector<double> recent (run.end() - std::min<std::ptrdiff_t> (10, static_cast<std::ptrdiff_t> (run.size())), run.end());
            if (std::abs (f.midi - median (recent)) > toleranceSemitones)
                close();
        }
        if (run.empty())
        {
            runStart = f.seconds - 0.5 * hop;
            runLevel = 0.0;
        }
        gap = 0;
        run.push_back (f.midi);
        runEnd = f.seconds + 0.5 * hop;
        runLevel = std::max (runLevel, f.level);
    }
    close();
    return notes;
}

double correction (const Note& note, double amount, const bool* scale, int root)
{
    amount = std::clamp (amount, 0.0, 1.0);
    double target = std::round (note.midi);
    if (scale != nullptr)
    {
        double best = 1.0e9;
        for (int key = static_cast<int> (std::floor (note.midi)) - 6; key <= static_cast<int> (std::ceil (note.midi)) + 6; ++key)
            if (scale[((key - root) % 12 + 12) % 12] && std::abs (key - note.midi) < std::abs (best - note.midi))
                best = key;
        if (best < 1.0e8)
            target = best;
    }
    return (target - note.midi) * amount;
}

std::shared_ptr<const SampleData> render (const std::shared_ptr<const SampleData>& in, const std::vector<Note>& notes)
{
    if (in == nullptr || in->numFrames() < 2)
        return in;
    const bool anyMove = std::any_of (notes.begin(), notes.end(), [] (const Note& n) { return std::abs (n.shift) > 1.0e-4; });
    if (! anyMove)
        return in;

    auto out = std::make_shared<SampleData> (*in);
    const double rate = in->sampleRate;
    const int total = in->numFrames();
    const int fade = std::max (1, static_cast<int> (0.010 * rate));

    for (const auto& n : notes)
    {
        if (std::abs (n.shift) <= 1.0e-4)
            continue;
        // The note's span with a fade's room on each side, shifted on its own (same length), then crossfaded in.
        const int a = std::clamp (static_cast<int> (n.start * rate) - fade, 0, total);
        const int b = std::clamp (static_cast<int> (n.end * rate) + fade, 0, total);
        if (b - a < 4 * fade)
            continue;
        auto part = std::make_shared<SampleData>();
        part->sampleRate = rate;
        part->left.assign (in->left.begin() + a, in->left.begin() + b);
        part->right.assign (in->right.begin() + a, in->right.begin() + b);
        StretchSettings s;
        s.semitones = std::clamp (n.shift, -kMaxStretchSemitones, kMaxStretchSemitones);
        const auto shifted = stretchSample (part, s);
        if (shifted == nullptr)
            continue;
        const int len = std::min (b - a, shifted->numFrames());
        for (int i = 0; i < len; ++i)
        {
            float w = 1.0f;
            if (i < fade)
                w = static_cast<float> (i) / fade;
            else if (i >= len - fade)
                w = static_cast<float> (len - 1 - i) / fade;
            const auto at = static_cast<size_t> (a) + static_cast<size_t> (i);
            out->left[at] = (1.0f - w) * out->left[at] + w * shifted->left[static_cast<size_t> (i)];
            out->right[at] = (1.0f - w) * out->right[at] + w * shifted->right[static_cast<size_t> (i)];
        }
    }
    return out;
}
} // namespace sls::pitchtrack
