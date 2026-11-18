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
    // 0.66.1, read for SunSymphony (the program's own sampler does not use them, and still counts them as left out):
    int seqLength = 1, seqPosition = 1; // round robin: this region plays on every seqLength-th note, the seqPosition-th
    double keytrack = 100.0;            // pitch_keytrack: cents per key (0: every key plays the sample at its own pitch)
    bool oneShot = false;               // loop_mode=one_shot: the sample plays to its end whatever the key does
    double ampRelease = -1.0;           // ampeg_release in seconds (-1: not given)
    // 0.84.0, keyswitches (for SunSymphony): the region plays only after the key `swLast` was pressed (-1: always); `swLabel` names
    // that articulation; `swDefault` is the keyswitch that holds before any is pressed (-1: not given).
    int swLast = -1;
    std::string swLabel;
    int swDefault = -1;
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
