#include "audio/SampleEdit.h"
#include "audio/Fft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>

#include <juce_audio_formats/juce_audio_formats.h>

namespace sls
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

bool usable (const SamplePtr& s) noexcept
{
    return s != nullptr && s->numFrames() > 0 && s->right.size() == s->left.size();
}

std::shared_ptr<SampleData> copyOf (const SampleData& in)
{
    auto out = std::make_shared<SampleData>();
    out->sampleRate = in.sampleRate;
    out->name = in.name;
    out->left = in.left;
    out->right = in.right;
    // `source` stays empty: an edited sample is not the file it came from until it is saved.
    return out;
}

template <typename F>
SamplePtr mapRange (const SamplePtr& in, FrameRange r, F&& f)
{
    if (! usable (in))
        return nullptr;
    r = clampRange (r, in->numFrames());
    if (r.length() <= 0)
        return nullptr;
    auto out = copyOf (*in);
    for (int i = r.begin; i < r.end; ++i)
        f (out->left[static_cast<size_t> (i)], out->right[static_cast<size_t> (i)], i - r.begin, r.length());
    return out;
}


std::vector<double> hann (int n)
{
    std::vector<double> w (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
        w[static_cast<size_t> (i)] = 0.5 - 0.5 * std::cos (2.0 * kPi * i / n);
    return w;
}

// Magnitudes of the Hann-windowed frames of `x` starting at begin, begin + hop, ... while a whole frame fits before `end`.
std::vector<std::vector<double>> frameMagnitudes (const std::vector<float>& x, int begin, int end, int fftSize, int hop, const Fft& fft,
                                                  const std::vector<double>& window)
{
    std::vector<std::vector<double>> frames;
    std::vector<std::complex<double>> work (static_cast<size_t> (fftSize));
    for (int start = begin; start + fftSize <= end; start += hop)
    {
        for (int i = 0; i < fftSize; ++i)
            work[static_cast<size_t> (i)] = x[static_cast<size_t> (start) + static_cast<size_t> (i)] * window[static_cast<size_t> (i)];
        fft.forward (work);
        std::vector<double> mags (static_cast<size_t> (fftSize / 2 + 1));
        for (size_t b = 0; b < mags.size(); ++b)
            mags[b] = std::abs (work[b]);
        frames.push_back (std::move (mags));
    }
    return frames;
}

double fadeCurve (double t, FadeShape shape) noexcept // t 0..1 -> gain 0..1
{
    t = std::clamp (t, 0.0, 1.0);
    switch (shape)
    {
        case FadeShape::Curve: return t * t;
        case FadeShape::SCurve: return 0.5 - 0.5 * std::cos (kPi * t);
        case FadeShape::Linear: break;
    }
    return t;
}
} // namespace

FrameRange clampRange (FrameRange r, int numFrames) noexcept
{
    if (r.begin > r.end)
        std::swap (r.begin, r.end);
    r.begin = std::clamp (r.begin, 0, std::max (0, numFrames));
    r.end = std::clamp (r.end, 0, std::max (0, numFrames));
    return r;
}

double toDb (double linear) noexcept
{
    return linear > 1.0e-9 ? 20.0 * std::log10 (linear) : -180.0;
}

SamplePtr cutRange (const SamplePtr& in, FrameRange r)
{
    if (! usable (in))
        return nullptr;
    r = clampRange (r, in->numFrames());
    if (r.length() <= 0 || r.length() >= in->numFrames())
        return nullptr; // nothing to cut, or it would leave nothing
    auto out = copyOf (*in);
    out->left.erase (out->left.begin() + r.begin, out->left.begin() + r.end);
    out->right.erase (out->right.begin() + r.begin, out->right.begin() + r.end);
    return out;
}

SamplePtr cropToRange (const SamplePtr& in, FrameRange r)
{
    if (! usable (in))
        return nullptr;
    r = clampRange (r, in->numFrames());
    if (r.length() <= 0)
        return nullptr;
    auto out = copyOf (*in);
    out->left.assign (in->left.begin() + r.begin, in->left.begin() + r.end);
    out->right.assign (in->right.begin() + r.begin, in->right.begin() + r.end);
    return out;
}

SamplePtr silenceRange (const SamplePtr& in, FrameRange r)
{
    return mapRange (in, r, [] (float& l, float& rr, int, int) { l = 0.0f; rr = 0.0f; });
}

SamplePtr fadeRange (const SamplePtr& in, FrameRange r, bool fadeIn, FadeShape shape)
{
    return mapRange (in, r,
                     [=] (float& l, float& rr, int i, int n)
                     {
                         const double t = n > 1 ? static_cast<double> (i) / (n - 1) : 1.0;
                         const auto g = static_cast<float> (fadeCurve (fadeIn ? t : 1.0 - t, shape));
                         l *= g;
                         rr *= g;
                     });
}

SamplePtr reverseRange (const SamplePtr& in, FrameRange r)
{
    if (! usable (in))
        return nullptr;
    r = clampRange (r, in->numFrames());
    if (r.length() <= 0)
        return nullptr;
    auto out = copyOf (*in);
    std::reverse (out->left.begin() + r.begin, out->left.begin() + r.end);
    std::reverse (out->right.begin() + r.begin, out->right.begin() + r.end);
    return out;
}

SamplePtr gainRange (const SamplePtr& in, FrameRange r, double gainDb)
{
    if (! std::isfinite (gainDb))
        return nullptr;
    const auto g = static_cast<float> (std::pow (10.0, std::clamp (gainDb, -120.0, 48.0) / 20.0));
    return mapRange (in, r, [g] (float& l, float& rr, int, int) { l *= g; rr *= g; });
}

SamplePtr invertPhase (const SamplePtr& in, FrameRange r)
{
    return mapRange (in, r, [] (float& l, float& rr, int, int) { l = -l; rr = -rr; });
}

SamplePtr removeDcOffset (const SamplePtr& in, FrameRange r)
{
    if (! usable (in))
        return nullptr;
    double sumL = 0.0, sumR = 0.0;
    for (int i = 0; i < in->numFrames(); ++i)
    {
        sumL += in->left[static_cast<size_t> (i)];
        sumR += in->right[static_cast<size_t> (i)];
    }
    const auto meanL = static_cast<float> (sumL / in->numFrames());
    const auto meanR = static_cast<float> (sumR / in->numFrames());
    return mapRange (in, r, [=] (float& l, float& rr, int, int) { l -= meanL; rr -= meanR; });
}

double peakOf (const SamplePtr& in, FrameRange r)
{
    if (! usable (in))
        return 0.0;
    r = clampRange (r, in->numFrames());
    double peak = 0.0;
    for (int i = r.begin; i < r.end; ++i)
        peak = std::max ({ peak, static_cast<double> (std::fabs (in->left[static_cast<size_t> (i)])),
                           static_cast<double> (std::fabs (in->right[static_cast<size_t> (i)])) });
    return peak;
}

SamplePtr normaliseRange (const SamplePtr& in, FrameRange r, double targetPeakDb)
{
    if (! usable (in) || ! std::isfinite (targetPeakDb))
        return nullptr;
    r = clampRange (r, in->numFrames());
    const double peak = peakOf (in, r);
    if (r.length() <= 0 || peak < 1.0e-9)
        return in; // silence cannot be normalised
    return gainRange (in, r, std::clamp (targetPeakDb, -60.0, 0.0) - toDb (peak));
}

SamplePtr reduceNoise (const SamplePtr& in, FrameRange noise, FrameRange range, const NoiseReduction& requested)
{
    if (! usable (in) || in->numFrames() < kNoiseFftSize)
        return nullptr;
    noise = clampRange (noise, in->numFrames());
    if (noise.length() < kNoiseMinProfileFrames)
        return nullptr;
    range = clampRange (range, in->numFrames());
    if (range.length() <= 0)
        return nullptr;

    NoiseReduction s = requested;
    s.reductionDb = std::isfinite (s.reductionDb) ? std::clamp (s.reductionDb, 0.0, 60.0) : 12.0;
    s.sensitivity = std::isfinite (s.sensitivity) ? std::clamp (s.sensitivity, 0.0, 8.0) : 3.0;
    s.frequencySmoothing = std::clamp (s.frequencySmoothing, 0, 16);
    s.releaseSeconds = std::isfinite (s.releaseSeconds) ? std::clamp (s.releaseSeconds, 0.005, 1.0) : 0.05;

    constexpr int n = kNoiseFftSize;
    constexpr int hop = n / 4;
    constexpr int bins = n / 2 + 1;
    const Fft fft (n);
    const auto window = hann (n);
    const double floorGain = std::pow (10.0, -s.reductionDb / 20.0);
    // Hann^2 summed over a 75 % overlap is 1.5 everywhere.
    const double synthesisScale = 1.0 / 1.5;
    // Per frame the gate may open at once but closes by at most this factor (release).
    const double releasePerFrame = std::exp (-static_cast<double> (hop) / (in->sampleRate * s.releaseSeconds));

    auto out = copyOf (*in);
    for (int channel = 0; channel < 2; ++channel)
    {
        const auto& x = channel == 0 ? in->left : in->right;

        // Noise profile: mean and standard deviation of every bin over the noise frames.
        const auto noiseFrames = frameMagnitudes (x, noise.begin, noise.end, n, hop, fft, window);
        std::vector<double> threshold (static_cast<size_t> (bins));
        for (int b = 0; b < bins; ++b)
        {
            double mean = 0.0, sq = 0.0;
            for (const auto& f : noiseFrames)
            {
                mean += f[static_cast<size_t> (b)];
                sq += f[static_cast<size_t> (b)] * f[static_cast<size_t> (b)];
            }
            mean /= static_cast<double> (noiseFrames.size());
            const double variance = std::max (0.0, sq / static_cast<double> (noiseFrames.size()) - mean * mean);
            threshold[static_cast<size_t> (b)] = mean + s.sensitivity * std::sqrt (variance);
        }

        // Work on a copy of the range with n/2 of silence at both ends so that every sample is covered by 4 frames.
        const int length = range.length();
        const int padded = length + 2 * n;
        std::vector<double> src (static_cast<size_t> (padded), 0.0);
        for (int i = 0; i < length; ++i)
            src[static_cast<size_t> (n) + static_cast<size_t> (i)] = x[static_cast<size_t> (range.begin) + static_cast<size_t> (i)];
        std::vector<double> acc (static_cast<size_t> (padded), 0.0);

        std::vector<std::complex<double>> work (static_cast<size_t> (n));
        std::vector<double> mask (static_cast<size_t> (bins)), open (static_cast<size_t> (bins)), gain (static_cast<size_t> (bins), floorGain);
        for (int start = 0; start + n <= padded; start += hop)
        {
            for (int i = 0; i < n; ++i)
                work[static_cast<size_t> (i)] = src[static_cast<size_t> (start) + static_cast<size_t> (i)] * window[static_cast<size_t> (i)];
            fft.forward (work);

            for (int b = 0; b < bins; ++b)
                open[static_cast<size_t> (b)] = std::abs (work[static_cast<size_t> (b)]) > threshold[static_cast<size_t> (b)] ? 1.0 : 0.0;
            for (int b = 0; b < bins; ++b)
            {
                double sum = 0.0;
                int count = 0;
                for (int k = std::max (0, b - s.frequencySmoothing); k <= std::min (bins - 1, b + s.frequencySmoothing); ++k, ++count)
                    sum += open[static_cast<size_t> (k)];
                mask[static_cast<size_t> (b)] = sum / count;
            }
            for (int b = 0; b < bins; ++b)
            {
                const double target = floorGain + (1.0 - floorGain) * mask[static_cast<size_t> (b)];
                auto& g = gain[static_cast<size_t> (b)];
                g = target >= g ? target : std::max (target, g * releasePerFrame);
            }

            for (int b = 0; b < bins; ++b)
            {
                work[static_cast<size_t> (b)] *= gain[static_cast<size_t> (b)];
                if (b > 0 && b < n / 2)
                    work[static_cast<size_t> (n - b)] = std::conj (work[static_cast<size_t> (b)]);
            }
            fft.inverse (work);
            for (int i = 0; i < n; ++i)
                acc[static_cast<size_t> (start) + static_cast<size_t> (i)] += work[static_cast<size_t> (i)].real() * window[static_cast<size_t> (i)] * synthesisScale;
        }

        auto& y = channel == 0 ? out->left : out->right;
        for (int i = 0; i < length; ++i)
            y[static_cast<size_t> (range.begin) + static_cast<size_t> (i)] = static_cast<float> (acc[static_cast<size_t> (n) + static_cast<size_t> (i)]);
    }
    return out;
}

std::vector<double> detectOnsets (const SamplePtr& in, double sensitivity, double minGapSeconds)
{
    std::vector<double> onsets;
    constexpr int n = 1024, hop = 256;
    if (! usable (in) || in->numFrames() < 2 * n || in->sampleRate <= 0.0)
        return onsets;
    sensitivity = std::isfinite (sensitivity) ? std::clamp (sensitivity, 0.0, 1.0) : 0.5;
    minGapSeconds = std::isfinite (minGapSeconds) ? std::clamp (minGapSeconds, 0.005, 5.0) : 0.05;

    // One mono signal (left + right); the flux is the growth of the compressed magnitude summed over the bins.
    std::vector<float> mono (in->left.size());
    for (size_t i = 0; i < mono.size(); ++i)
        mono[i] = 0.5f * (in->left[i] + in->right[i]);

    const Fft fft (n);
    const auto window = hann (n);
    const auto frames = frameMagnitudes (mono, 0, in->numFrames(), n, hop, fft, window);
    if (frames.size() < 4)
        return onsets;

    std::vector<double> flux (frames.size(), 0.0);
    for (size_t t = 1; t < frames.size(); ++t)
        for (size_t b = 0; b < frames[t].size(); ++b)
            flux[t] += std::max (0.0, std::log1p (50.0 * frames[t][b]) - std::log1p (50.0 * frames[t - 1][b]));

    // A real onset also makes the frame louder; a sudden cut to silence (which spreads a faint click over every bin) does not.
    std::vector<double> energy (frames.size(), 0.0);
    for (size_t t = 0; t < frames.size(); ++t)
        for (const auto m : frames[t])
            energy[t] += m * m;

    const double maxFlux = *std::max_element (flux.begin(), flux.end());
    if (maxFlux < 1.0e-6)
        return onsets;

    const double multiplier = 4.0 - 2.8 * sensitivity; // 4.0 (strict) .. 1.2 (everything)
    const int halfWindow = std::max (2, static_cast<int> (0.25 * in->sampleRate / hop));
    const int minGapFrames = std::max (1, static_cast<int> (std::lround (minGapSeconds * in->sampleRate / hop)));
    int lastFrame = -1000000;
    for (int t = 1; t < static_cast<int> (flux.size()); ++t)
    {
        const int from = std::max (0, t - halfWindow), to = std::min (static_cast<int> (flux.size()) - 1, t + halfWindow);
        std::vector<double> local (flux.begin() + from, flux.begin() + to + 1);
        std::nth_element (local.begin(), local.begin() + static_cast<long> (local.size() / 2), local.end());
        const double median = local[local.size() / 2];
        const double threshold = median * multiplier + 0.05 * maxFlux * (1.0 - 0.5 * sensitivity);
        if (flux[static_cast<size_t> (t)] <= threshold)
            continue;
        const double growth = 1.6 - 0.5 * sensitivity;
        const double before = std::max (1.0e-12, energy[static_cast<size_t> (std::max (0, t - 2))]);
        if (std::max (energy[static_cast<size_t> (t)], energy[static_cast<size_t> (t - 1)]) < growth * before)
            continue;
        bool peak = true; // a local maximum within +-3 frames
        for (int k = std::max (1, t - 3); k <= std::min (static_cast<int> (flux.size()) - 1, t + 3); ++k)
            if (flux[static_cast<size_t> (k)] > flux[static_cast<size_t> (t)])
                peak = false;
        if (! peak || t - lastFrame < minGapFrames)
            continue;
        lastFrame = t;

        // Frame t covers [t*hop, t*hop + n): the sound started somewhere in it. Refine to where the waveform first rises
        // above a quarter of its local peak.
        const int a = std::max (0, (t - 1) * hop), b = std::min (in->numFrames(), t * hop + n);
        float localPeak = 0.0f;
        for (int i = a; i < b; ++i)
            localPeak = std::max (localPeak, std::fabs (mono[static_cast<size_t> (i)]));
        int position = t * hop;
        for (int i = a; i < b; ++i)
            if (std::fabs (mono[static_cast<size_t> (i)]) >= 0.25f * localPeak)
            {
                position = i;
                break;
            }
        const double seconds = position / in->sampleRate;
        if (onsets.empty() || seconds - onsets.back() >= minGapSeconds)
            onsets.push_back (seconds);
    }
    return onsets;
}

std::vector<double> gridSlices (const SamplePtr& in, int n)
{
    std::vector<double> cuts;
    if (! usable (in) || in->sampleRate <= 0.0 || n < 2)
        return cuts;
    n = std::min (n, 256);
    const double seconds = in->numFrames() / in->sampleRate;
    for (int i = 1; i < n; ++i)
        cuts.push_back (seconds * i / n);
    return cuts;
}

juce::Result writeSampleWav (const SampleData& sample, const juce::File& file)
{
    if (sample.numFrames() < 1 || sample.right.size() != sample.left.size() || sample.sampleRate < 1000.0)
        return juce::Result::fail ("There is no audio to write.");
    if (file.exists())
        return juce::Result::fail (file.getFullPathName() + " already exists.");
    const auto folder = file.getParentDirectory();
    if (! folder.isDirectory() && ! folder.createDirectory().wasOk())
        return juce::Result::fail ("Cannot create the folder " + folder.getFullPathName());

    auto stream = std::make_unique<juce::FileOutputStream> (file);
    if (! stream->openedOk())
        return juce::Result::fail ("Cannot write to " + file.getFullPathName());
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sample.sampleRate, 2, 32, {}, 0));
    if (writer == nullptr)
    {
        stream.reset();
        file.deleteFile();
        return juce::Result::fail ("Cannot create a WAV file at " + file.getFullPathName());
    }
    [[maybe_unused]] auto* owned = stream.release(); // the writer owns it now

    const float* channels[2] = { sample.left.data(), sample.right.data() };
    const bool ok = writer->writeFromFloatArrays (channels, 2, sample.numFrames());
    writer.reset(); // flushes and closes
    if (! ok)
    {
        file.deleteFile();
        return juce::Result::fail ("Writing " + file.getFullPathName() + " failed (disk full?).");
    }
    return juce::Result::ok();
}

juce::File uniqueEditedFile (const juce::File& folder, const juce::String& baseName)
{
    auto clean = juce::File::createLegalFileName (baseName).trim();
    if (clean.isEmpty())
        clean = "sample";
    if (clean.length() > 60)
        clean = clean.substring (0, 60);
    auto candidate = folder.getChildFile (clean + "-edit.wav");
    for (int i = 2; candidate.exists() && i < 10000; ++i)
        candidate = folder.getChildFile (clean + "-edit-" + juce::String (i) + ".wav");
    return candidate;
}
} // namespace sls
