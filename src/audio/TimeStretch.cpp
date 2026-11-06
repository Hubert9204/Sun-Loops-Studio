#include "audio/TimeStretch.h"

#include <algorithm>
#include <cmath>

#include "signalsmith-stretch.h"

namespace sls
{
StretchSettings StretchSettings::sanitised() const noexcept
{
    StretchSettings s = *this;
    s.lengthRatio = std::isfinite (s.lengthRatio) ? std::clamp (s.lengthRatio, kMinStretchRatio, kMaxStretchRatio) : 1.0;
    s.semitones = std::isfinite (s.semitones) ? std::clamp (s.semitones, -kMaxStretchSemitones, kMaxStretchSemitones) : 0.0;
    return s;
}

namespace
{
// Plain buffers as the stretcher's input and output: buffer[channel][index].
struct Planar
{
    float* left;
    float* right;
    float* operator[] (int c) const noexcept { return c == 0 ? left : right; }
};
} // namespace

std::shared_ptr<const SampleData> stretchSample (const std::shared_ptr<const SampleData>& input, const StretchSettings& requested)
{
    if (input == nullptr || input->numFrames() < 2 || input->numFrames() > kMaxStretchFrames || input->sampleRate <= 0.0
        || input->right.size() != input->left.size())
        return nullptr;

    const auto settings = requested.sanitised();
    if (settings.isNeutral())
        return input;

    const int frames = input->numFrames();
    const auto outFrames = std::max (1, static_cast<int> (std::llround (frames * settings.lengthRatio)));

    signalsmith::stretch::SignalsmithStretch<float> stretch;
    stretch.presetDefault (2, static_cast<float> (input->sampleRate));
    stretch.setTransposeSemitones (static_cast<float> (settings.semitones),
                                   settings.keepTimbre ? static_cast<float> (8000.0 / input->sampleRate) : 0.0f);

    const int inLatency = stretch.inputLatency();
    const int outLatency = stretch.outputLatency();

    // The exact recipe for a fixed-length sound (the library's README): give the first `inLatency` samples with seek(),
    // process the rest followed by `inLatency` samples of silence, then flush. The result starts `outLatency` early.
    std::vector<float> inL (input->left), inR (input->right);
    inL.resize (static_cast<size_t> (frames + inLatency), 0.0f);
    inR.resize (static_cast<size_t> (frames + inLatency), 0.0f);

    const int total = outFrames + outLatency;
    std::vector<float> outL (static_cast<size_t> (total), 0.0f), outR (static_cast<size_t> (total), 0.0f);

    const int seekLength = std::min (inLatency, frames + inLatency);
    stretch.seek (Planar { inL.data(), inR.data() }, seekLength, 1.0 / settings.lengthRatio);

    // What is left of the input is spread over all the output but the last `outLatency` samples (flushed below).
    const int restIn = frames + inLatency - seekLength;
    const int restOut = std::max (0, static_cast<int> (std::llround (restIn * settings.lengthRatio)));
    const int mainOut = std::min (restOut, total);
    stretch.process (Planar { inL.data() + seekLength, inR.data() + seekLength }, restIn, Planar { outL.data(), outR.data() }, mainOut);
    if (total > mainOut)
        stretch.flush (Planar { outL.data() + mainOut, outR.data() + mainOut }, total - mainOut, 1.0 / settings.lengthRatio);

    auto out = std::make_shared<SampleData>();
    out->sampleRate = input->sampleRate;
    out->name = input->name;
    out->source = input->source;
    out->left.assign (outL.begin() + outLatency, outL.begin() + outLatency + outFrames);
    out->right.assign (outR.begin() + outLatency, outR.begin() + outLatency + outFrames);
    for (auto* channel : { &out->left, &out->right })
        for (auto& v : *channel)
            if (! std::isfinite (v))
                v = 0.0f;
    return out;
}
} // namespace sls
