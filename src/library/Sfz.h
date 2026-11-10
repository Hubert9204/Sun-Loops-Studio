#pragma once

#include <string>
#include <vector>

namespace sls
{
// SFZ instruments (0.41.0): a text file that maps sample files to keys and velocities. Only what a sampler channel can play is
// read: <control> default_path, <global> / <master> / <group> / <region> with sample, key, lokey, hikey, pitch_keycenter, lovel,
// hivel, tune, transpose, volume, pan, loop_mode, loop_start, loop_end (and their older spellings). Everything else is skipped
// and counted. Plain C++, tested.
namespace sfz
{
struct Region
{
    std::string sample;  // path as written, with default_path in front, '\' turned into '/'
    int loKey = 0, hiKey = 127, rootKey = 60;
    int loVel = 1, hiVel = 127;
    double tuneCents = 0.0;  // tune + 100 x transpose
    double volumeDb = 0.0;
    double pan = 0.0;        // -100..100
    bool loop = false;
    long long loopStart = -1, loopEnd = -1; // frames; -1: the sample file's own loop, or none
};

struct Instrument
{
    std::vector<Region> regions;
    int skippedOpcodes = 0;  // opcodes it does not use (filters, envelopes, round robin...)
    std::vector<std::string> includes; // #include lines it could not follow
};

// "c4" = 60, "c#4" / "db4" = 61, or a number. -1 when it is neither.
int keyFromText (const std::string& text);

Instrument parse (const std::string& text);
} // namespace sfz
} // namespace sls
