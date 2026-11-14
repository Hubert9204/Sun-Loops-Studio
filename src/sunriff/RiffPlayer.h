#pragma once

#include <array>
#include <cstdint>

// SunRiff's playing modes (0.68.0), after FL Slayer: what the guitarist does with the keys that are played.
//   None        - every key is a note, as on a synthesizer;
//   Autochords  - a key (or the keys played together) becomes a six-string guitar chord, strummed;
//   Powerchords - a key becomes a power chord (above C5 root-fifth-octave, below root-fourth-octave), strummed;
//   Strumming   - single keys play as they are; keys played together are strummed, down and up in turn;
//   SoloFixed   - one string: a key pressed while another is held slides to it in a fixed time (Speed, in beats);
//   SoloDynamic - the same, the slide's time growing with its distance.
// Plain C++: the processor feeds it the keys and takes back timed string events. Keys played together are those within 12 ms
// of each other, so the chord modes answer 12 ms late (as a guitarist's hand does).
namespace sunriff
{
enum class PlayMode : int { None, Autochords, Powerchords, Strumming, SoloFixed, SoloDynamic };

struct StringEvent
{
    enum Type : std::uint8_t { On, Off, Glide };
    Type type = On;
    int note = 60;
    float velocity = 0.8f;
    int glideSamples = 0;
    std::int64_t time = 0; // in samples since the player started
};

class Player
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void configure (PlayMode m, float speed, double bpm) noexcept;

    // `when`: the sample (on the player's clock) the key was played at.
    void keyOn (int key, float velocity, std::int64_t when) noexcept;
    void keyOff (int key, std::int64_t when) noexcept;
    void allOff (std::int64_t when) noexcept;

    // The next event due before `until` (in time order); false when there is none.
    bool next (std::int64_t until, StringEvent& out) noexcept;
    int pending() const noexcept { return count; }

    // The chord a key (or the lowest of a group) makes in Autochords, from the other keys played with it. For the tests.
    static int autochord (const int* keys, int numKeys, int* out) noexcept;
    static int powerchord (int key, int* out) noexcept;

private:
    void push (StringEvent e) noexcept;
    void flushGroup (std::int64_t when) noexcept;
    void strum (const int* notes, int n, float velocity, std::int64_t when, int key) noexcept;
    int glideTime (int from, int to) const noexcept;
    void noteOffsFor (int key, std::int64_t when) noexcept;

    double rate = 48000.0, bpm = 120.0;
    PlayMode mode = PlayMode::None;
    float speed = 0.5f;
    std::array<StringEvent, 512> queue {};
    int count = 0;

    // keys played together (the chord modes)
    std::array<int, 16> group {};
    std::array<float, 16> groupVel {};
    int groupSize = 0;
    std::int64_t groupStart = -1;
    bool downStroke = true;
    // what each key plays (so its release stops exactly those notes)
    struct Sounding
    {
        int key = -1;
        std::array<int, 8> notes {};
        int n = 0;
        std::int64_t lastOn = 0; // a release never comes before the strum has reached the last string
    };
    std::array<Sounding, 32> sounding {};
    // the solo modes: the keys held, the last one sounds
    std::array<int, 16> stack {};
    int stackSize = 0;
    int soloNote = -1;
};
} // namespace sunriff
