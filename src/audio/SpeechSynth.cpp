#include "audio/SpeechSynth.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace sls
{
namespace
{
constexpr double kPi = 3.141592653589793;

// What a phoneme is made of. Formants in Hz for a man's voice (0: the formants stay where they were, as in a consonant made
// with the noise only); `voice` and `noise` are the levels of the voice source and of the hiss.
enum class Kind { Vowel, Sonorant, Fricative, Plosive, Pause };
struct Phone
{
    const char* name;
    Kind kind;
    float f1, f2, f3;
    float voice, noise;
    float noiseHz, noiseBw;
    float ms;
};

// clang-format off
const std::array<Phone, 29> kPhones { {
    { "a",  Kind::Vowel,     730, 1090, 2440, 1.0f,  0.0f,    0,    0, 105 },
    { "e",  Kind::Vowel,     500, 1850, 2500, 1.0f,  0.0f,    0,    0, 100 },
    { "i",  Kind::Vowel,     280, 2250, 3000, 1.0f,  0.0f,    0,    0,  90 },
    { "o",  Kind::Vowel,     540,  850, 2400, 1.0f,  0.0f,    0,    0, 100 },
    { "u",  Kind::Vowel,     310,  800, 2250, 1.0f,  0.0f,    0,    0,  95 },
    { "m",  Kind::Sonorant,  280,  950, 2200, 0.55f, 0.0f,    0,    0,  70 },
    { "n",  Kind::Sonorant,  280, 1700, 2600, 0.55f, 0.0f,    0,    0,  65 },
    { "ny", Kind::Sonorant,  280, 2100, 2900, 0.55f, 0.0f,    0,    0,  80 },
    { "l",  Kind::Sonorant,  360, 1300, 2700, 0.7f,  0.0f,    0,    0,  60 },
    { "ly", Kind::Sonorant,  300, 2000, 2800, 0.7f,  0.0f,    0,    0,  80 },
    { "r",  Kind::Sonorant,  450, 1300, 1700, 0.65f, 0.0f,    0,    0,  55 },
    { "w",  Kind::Sonorant,  300,  650, 2150, 0.8f,  0.0f,    0,    0,  45 },
    { "j",  Kind::Sonorant,  260, 2100, 3000, 0.8f,  0.0f,    0,    0,  45 },
    { "f",  Kind::Fricative,   0,    0,    0, 0.0f,  0.7f, 5500, 4000,  95 },
    { "s",  Kind::Fricative,   0,    0,    0, 0.0f,  1.0f, 6500, 2500, 100 },
    { "S",  Kind::Fricative,   0,    0,    0, 0.0f,  1.0f, 3000, 1800, 105 },
    { "v",  Kind::Fricative, 300, 1200, 2300, 0.45f, 0.4f, 4500, 4000,  70 },
    { "z",  Kind::Fricative, 300, 1600, 2500, 0.4f,  0.7f, 6000, 2500,  80 },
    { "Z",  Kind::Fricative, 300, 1800, 2500, 0.4f,  0.7f, 3000, 1800,  80 },
    { "p",  Kind::Plosive,     0,    0,    0, 0.0f,  0.8f, 1000, 1500,  80 },
    { "t",  Kind::Plosive,     0,    0,    0, 0.0f,  0.9f, 4000, 3000,  75 },
    { "k",  Kind::Plosive,     0,    0,    0, 0.0f,  0.9f, 2000, 1800,  80 },
    { "b",  Kind::Plosive,   250,  900, 2200, 0.2f,  0.6f, 1000, 1500,  65 },
    { "d",  Kind::Plosive,   250, 1700, 2600, 0.2f,  0.7f, 4000, 3000,  60 },
    { "g",  Kind::Plosive,   250, 1900, 2400, 0.2f,  0.7f, 2000, 1800,  65 },
    { "_",  Kind::Pause,       0,    0,    0, 0.0f,  0.0f,    0,    0,  35 }, // between words
    { ",",  Kind::Pause,       0,    0,    0, 0.0f,  0.0f,    0,    0, 200 }, // a comma
    { ".",  Kind::Pause,       0,    0,    0, 0.0f,  0.0f,    0,    0, 330 }, // the end of a sentence
    { "?",  Kind::Pause,       0,    0,    0, 0.0f,  0.0f,    0,    0, 330 }, // the end of a question
} };
// clang-format on

int phoneIndex (const std::string& name) noexcept
{
    for (size_t i = 0; i < kPhones.size(); ++i)
        if (name == kPhones[i].name)
            return static_cast<int> (i);
    return -1;
}

struct Spoken
{
    int phone = 0;
    bool stressed = false;
    bool longer = false;  // a double consonant
    bool question = false; // in a sentence that ends with a question mark
    float position = 0.0f; // 0..1 along its sentence
};

// UTF-8 to lower-case letters, with the accented vowels Italian uses (an accent marks the stress).
std::u32string decode (const std::string& text)
{
    std::u32string out;
    for (size_t i = 0; i < text.size();)
    {
        const auto c = static_cast<unsigned char> (text[i]);
        char32_t code = c;
        size_t length = 1;
        if (c >= 0xf0)
            length = 4, code = c & 0x07u;
        else if (c >= 0xe0)
            length = 3, code = c & 0x0fu;
        else if (c >= 0xc0)
            length = 2, code = c & 0x1fu;
        for (size_t k = 1; k < length && i + k < text.size(); ++k)
            code = (code << 6) | (static_cast<unsigned char> (text[i + k]) & 0x3fu);
        i += length;
        if (code >= 'A' && code <= 'Z')
            code += 'a' - 'A';
        if (code >= 0xc0 && code <= 0xde && code != 0xd7)
            code += 0x20; // Latin-1 capitals
        out.push_back (code);
    }
    return out;
}

bool isVowelLetter (char32_t c) noexcept { return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u'; }
char32_t plainVowel (char32_t c, bool& accented) noexcept
{
    accented = true;
    switch (c)
    {
        case 0xe0: case 0xe1: case 0xe2: return 'a';
        case 0xe8: case 0xe9: case 0xea: return 'e';
        case 0xec: case 0xed: case 0xee: return 'i';
        case 0xf2: case 0xf3: case 0xf4: return 'o';
        case 0xf9: case 0xfa: case 0xfb: return 'u';
        default: break;
    }
    accented = false;
    return c;
}

const char* const kDigits[] = { "zero", "uno", "due", "tre", "quattro", "cinque", "sei", "sette", "otto", "nove" };

// One word (letters only) as phonemes, the Italian way.
void readWord (const std::u32string& rawWord, std::vector<Spoken>& out)
{
    std::u32string w;
    int accentAt = -1;
    for (const char32_t c : rawWord)
    {
        bool accented = false;
        const char32_t plain = plainVowel (c, accented);
        if (accented)
            accentAt = static_cast<int> (w.size());
        w.push_back (plain);
    }
    const auto at = [&w] (size_t i) -> char32_t { return i < w.size() ? w[i] : 0; };
    const auto front = [] (char32_t c) { return c == 'e' || c == 'i'; };
    const size_t first = out.size();
    std::vector<int> vowelLetter; // for every phone of the word, the letter it came from (vowels only), else -1
    const auto add = [&] (const char* name, int letter = -1)
    {
        const int p = phoneIndex (name);
        if (p < 0)
            return;
        // a double consonant is one longer consonant
        if (out.size() > first && out.back().phone == p && kPhones[static_cast<size_t> (p)].kind != Kind::Vowel)
        {
            out.back().longer = true;
            return;
        }
        Spoken s;
        s.phone = p;
        out.push_back (s);
        vowelLetter.push_back (letter);
    };

    for (size_t i = 0; i < w.size(); ++i)
    {
        const char32_t c = w[i];
        const char32_t n1 = at (i + 1), n2 = at (i + 2);
        switch (c)
        {
            case 'a': case 'e': case 'o':
                add (std::string (1, static_cast<char> (c)).c_str(), static_cast<int> (i));
                break;
            case 'i': case 'u':
            {
                // between a consonant and a vowel, i and u glide (piano, buono)
                const bool glide = i > 0 && ! isVowelLetter (w[i - 1]) && isVowelLetter (n1) && accentAt != static_cast<int> (i);
                if (glide)
                    add (c == 'i' ? "j" : "w");
                else
                    add (c == 'i' ? "i" : "u", static_cast<int> (i));
                break;
            }
            case 'y':
                add (isVowelLetter (n1) ? "j" : "i", isVowelLetter (n1) ? -1 : static_cast<int> (i));
                break;
            case 'c':
                if (n1 == 'h') { add ("k"); ++i; }
                else if (n1 == 'c' && front (n2)) { add ("t"); }
                else if (front (n1))
                {
                    add ("t"); add ("S");
                    if (n1 == 'i' && isVowelLetter (n2) && accentAt != static_cast<int> (i + 1)) ++i; // ciao: the i is silent
                }
                else add ("k");
                break;
            case 'g':
                if (n1 == 'h') { add ("g"); ++i; }
                else if (n1 == 'n') { add ("ny"); ++i; }
                else if (n1 == 'l' && n2 == 'i')
                {
                    add ("ly");
                    ++i;
                    if (isVowelLetter (at (i + 2))) ++i; // figlio: the i is silent
                }
                else if (n1 == 'g' && front (n2)) { add ("d"); }
                else if (front (n1))
                {
                    add ("d"); add ("Z");
                    if (n1 == 'i' && isVowelLetter (n2) && accentAt != static_cast<int> (i + 1)) ++i; // giallo
                }
                else add ("g");
                break;
            case 's':
                if (n1 == 'c' && front (n2))
                {
                    add ("S");
                    ++i;
                    if (n2 == 'i' && isVowelLetter (at (i + 2))) ++i; // sciare
                }
                else if (n1 == 'h') { add ("S"); ++i; } // English "sh"
                else if (i > 0 && isVowelLetter (w[i - 1]) && isVowelLetter (n1)) add ("z"); // casa
                else add ("s");
                break;
            case 'z': add ("t"); add ("s"); break;
            case 'q': add ("k"); break;
            case 'x': add ("k"); add ("s"); break;
            case 'j': add ("j"); break;
            case 'h': break; // silent
            case 'k': add ("k"); break;
            case 'w': add ("w"); break;
            case 'b': case 'd': case 'f': case 'l': case 'm': case 'n': case 'p': case 'r': case 't': case 'v':
                add (std::string (1, static_cast<char> (c)).c_str());
                break;
            default: break;
        }
    }

    // The stress: the accented vowel, else the second-last vowel (the last one of a word with one).
    std::vector<size_t> vowels;
    for (size_t k = first; k < out.size(); ++k)
        if (kPhones[static_cast<size_t> (out[k].phone)].kind == Kind::Vowel)
            vowels.push_back (k);
    if (vowels.empty())
        return;
    size_t stressed = vowels.size() >= 2 ? vowels[vowels.size() - 2] : vowels.back();
    if (accentAt >= 0)
        for (const size_t k : vowels)
            if (vowelLetter[k - first] == accentAt)
                stressed = k;
    out[stressed].stressed = true;
}

std::vector<Spoken> toPhonemes (const std::string& text)
{
    const auto letters = decode (text);
    std::vector<Spoken> out;
    std::u32string word;
    const auto pause = [&out] (const char* name)
    {
        // a stronger pause replaces a weaker one before it; nothing starts with a pause
        const int p = phoneIndex (name);
        if (out.empty())
            return;
        if (kPhones[static_cast<size_t> (out.back().phone)].kind == Kind::Pause)
        {
            if (kPhones[static_cast<size_t> (p)].ms >= kPhones[static_cast<size_t> (out.back().phone)].ms)
                out.back().phone = p;
            return;
        }
        Spoken s;
        s.phone = p;
        out.push_back (s);
    };
    const auto flush = [&]
    {
        if (! word.empty())
        {
            readWord (word, out);
            word.clear();
        }
    };
    for (const char32_t c : letters)
    {
        bool accented = false;
        const char32_t plain = plainVowel (c, accented);
        if ((plain >= 'a' && plain <= 'z') || accented)
            word.push_back (c);
        else if (c >= '0' && c <= '9')
        {
            flush();
            readWord (decode (kDigits[c - '0']), out);
            pause ("_");
        }
        else if (c == '\'')
        {
            flush(); // l'acqua: run on
        }
        else
        {
            flush();
            if (c == ',' || c == ';' || c == ':')
                pause (",");
            else if (c == '.' || c == '!' || c == '\n')
                pause (".");
            else if (c == '?')
                pause ("?");
            else
                pause ("_");
        }
    }
    flush();
    // drop a trailing pause, then place every phone along its sentence and mark questions
    while (! out.empty() && kPhones[static_cast<size_t> (out.back().phone)].kind == Kind::Pause)
        out.pop_back();
    size_t begin = 0;
    for (size_t k = 0; k <= out.size(); ++k)
    {
        const bool end = k == out.size() || out[k].phone == phoneIndex (".") || out[k].phone == phoneIndex ("?");
        if (! end)
            continue;
        const bool question = k < out.size() && out[k].phone == phoneIndex ("?");
        const size_t count = std::max<size_t> (1, k - begin);
        for (size_t j = begin; j < k; ++j)
        {
            out[j].position = static_cast<float> (j - begin) / static_cast<float> (count);
            out[j].question = question;
        }
        begin = k + 1;
    }
    return out;
}

// A formant: a two-pole resonator with unity gain at 0 Hz (as in Klatt's synthesizer), its frequency moved every sample.
struct Resonator
{
    double y1 = 0.0, y2 = 0.0;
    double process (double x, double hz, double bw, double sampleRate) noexcept
    {
        hz = std::min (hz, 0.45 * sampleRate);
        const double c = -std::exp (-2.0 * kPi * bw / sampleRate);
        const double b = 2.0 * std::exp (-kPi * bw / sampleRate) * std::cos (2.0 * kPi * hz / sampleRate);
        const double a = 1.0 - b - c;
        const double y = a * x + b * y1 + c * y2;
        y2 = y1;
        y1 = y;
        return y;
    }
};

// A band-pass with a peak gain of one (the hiss of a consonant).
struct BandPass
{
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    double process (double x, double hz, double bw, double sampleRate) noexcept
    {
        hz = std::min (hz, 0.45 * sampleRate);
        const double w = 2.0 * kPi * hz / sampleRate;
        const double q = std::max (0.3, hz / std::max (1.0, bw));
        const double alpha = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        const double y = (alpha * x - alpha * x2 + 2.0 * std::cos (w) * y1 - (1.0 - alpha) * y2) / a0;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
};

std::string percentEncode (const std::string& text)
{
    std::string out;
    for (const char c : text)
    {
        const auto u = static_cast<unsigned char> (c);
        if ((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u == '-' || u == '.' || u == '_')
            out.push_back (c);
        else
        {
            char hex[4];
            std::snprintf (hex, sizeof (hex), "%%%02X", u);
            out += hex;
        }
    }
    return out;
}

std::string percentDecode (const std::string& text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '%' && i + 2 < text.size())
        {
            out.push_back (static_cast<char> (std::strtol (text.substr (i + 1, 2).c_str(), nullptr, 16)));
            i += 2;
        }
        else
            out.push_back (text[i]);
    }
    return out;
}
} // namespace

SpeechSettings SpeechSettings::sanitised() const noexcept
{
    SpeechSettings s = *this;
    if (static_cast<int> (s.voice) < 0 || static_cast<int> (s.voice) >= kSpeechVoices)
        s.voice = SpeechVoice::Male;
    s.pitch = std::clamp (finiteOr (s.pitch, 0.0f), -12.0f, 12.0f);
    s.speed = std::clamp (finiteOr (s.speed, 1.0f), 0.5f, 2.0f);
    s.intonation = std::clamp (finiteOr (s.intonation, 0.5f), 0.0f, 1.0f);
    return s;
}

const char* speechVoiceName (SpeechVoice voice) noexcept
{
    switch (voice)
    {
        case SpeechVoice::Female:  return "Female";
        case SpeechVoice::Child:   return "Child";
        case SpeechVoice::Robot:   return "Robot";
        case SpeechVoice::Whisper: return "Whisper";
        case SpeechVoice::Male:    break;
    }
    return "Male";
}

std::string speechId (const std::string& text, const SpeechSettings& settings)
{
    const auto s = settings.sanitised();
    char head[96];
    std::snprintf (head, sizeof (head), "speech:%d,%.2f,%.2f,%.2f:", static_cast<int> (s.voice), static_cast<double> (s.pitch),
                   static_cast<double> (s.speed), static_cast<double> (s.intonation));
    return head + percentEncode (text);
}

bool isSpeechId (const std::string& id) noexcept { return id.rfind ("speech:", 0) == 0; }

bool parseSpeechId (const std::string& id, std::string& text, SpeechSettings& settings)
{
    if (! isSpeechId (id))
        return false;
    const auto colon = id.find (':', 7);
    if (colon == std::string::npos)
        return false;
    int voice = 0;
    double pitch = 0.0, speed = 1.0, intonation = 0.5;
    if (std::sscanf (id.substr (7, colon - 7).c_str(), "%d,%lf,%lf,%lf", &voice, &pitch, &speed, &intonation) != 4)
        return false;
    settings.voice = static_cast<SpeechVoice> (voice);
    settings.pitch = static_cast<float> (pitch);
    settings.speed = static_cast<float> (speed);
    settings.intonation = static_cast<float> (intonation);
    settings = settings.sanitised();
    text = percentDecode (id.substr (colon + 1));
    return true;
}

std::string speechPhonemes (const std::string& text)
{
    std::string out;
    for (const auto& s : toPhonemes (text))
    {
        if (! out.empty())
            out += ' ';
        out += kPhones[static_cast<size_t> (s.phone)].name;
        if (s.stressed)
            out += '\'';
        if (s.longer)
            out += ':';
    }
    return out;
}

std::shared_ptr<const SampleData> speak (const std::string& text, const SpeechSettings& settingsIn, double sampleRate)
{
    const auto settings = settingsIn.sanitised();
    const auto phones = toPhonemes (text);
    if (phones.empty() || ! (sampleRate > 1000.0))
        return nullptr;

    // The voice: its pitch, the size of its vocal tract (the formants move up for a smaller one) and its source.
    double f0Base = 110.0, scale = 1.0, liveliness = settings.intonation;
    switch (settings.voice)
    {
        case SpeechVoice::Female:  f0Base = 205.0; scale = 1.16; break;
        case SpeechVoice::Child:   f0Base = 290.0; scale = 1.3; break;
        case SpeechVoice::Robot:   f0Base = 120.0; liveliness = 0.0; break;
        case SpeechVoice::Whisper: f0Base = 150.0; scale = 1.08; break;
        case SpeechVoice::Male:    break;
    }
    f0Base *= std::pow (2.0, static_cast<double> (settings.pitch) / 12.0);
    const bool robot = settings.voice == SpeechVoice::Robot;
    const bool whisper = settings.voice == SpeechVoice::Whisper;

    struct Segment
    {
        int phone, samples;
        bool stressed, question;
        float position;
    };
    std::vector<Segment> segments;
    size_t total = static_cast<size_t> (0.03 * sampleRate); // a little silence before
    for (const auto& s : phones)
    {
        const auto& p = kPhones[static_cast<size_t> (s.phone)];
        double ms = p.ms * (s.stressed ? 1.45 : 1.0) * (s.longer ? 1.8 : 1.0);
        const int samples = std::max (1, static_cast<int> (ms / 1000.0 / static_cast<double> (settings.speed) * sampleRate));
        segments.push_back ({ s.phone, samples, s.stressed, s.question, s.position });
        total += static_cast<size_t> (samples);
    }
    const size_t tail = static_cast<size_t> (0.12 * sampleRate);
    total += tail;

    std::vector<float> voiced (total, 0.0f), hiss (total, 0.0f);
    std::vector<std::uint8_t> vowelHere (total, 0), hissHere (total, 0);

    Resonator r1, r2, r3, r4, r5;
    BandPass noiseFilter;
    std::uint32_t seed = 0x2545f491u;
    const auto white = [&seed]
    {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return static_cast<double> (seed) / 2147483648.0 - 1.0;
    };
    const auto smoothing = [sampleRate] (double ms) { return 1.0 - std::exp (-1.0 / (ms * 0.001 * sampleRate)); };
    const double formantGlide = smoothing (14.0), ampGlide = smoothing (6.0), pitchGlide = smoothing (40.0);

    double f1 = 500.0, f2 = 1500.0, f3 = 2500.0;
    double voiceAmp = 0.0, noiseAmp = 0.0, f0 = f0Base, phase = 0.0, lastPulse = 0.0, noiseHz = 4000.0, noiseBw = 3000.0;
    size_t n = static_cast<size_t> (0.03 * sampleRate);
    for (size_t k = 0; k < segments.size(); ++k)
    {
        const auto& seg = segments[k];
        const auto& p = kPhones[static_cast<size_t> (seg.phone)];
        // the formants a consonant made with the hiss only keeps are those of the next vowel (its mouth is already there)
        double t1 = p.f1, t2 = p.f2, t3 = p.f3;
        if (! (t1 > 0.0))
            for (size_t j = k + 1; j < segments.size(); ++j)
                if (const auto& q = kPhones[static_cast<size_t> (segments[j].phone)]; q.f1 > 0.0f)
                {
                    t1 = q.f1;
                    t2 = q.f2;
                    t3 = q.f3;
                    break;
                }
        if (! (t1 > 0.0))
        {
            t1 = f1 / scale;
            t2 = f2 / scale;
            t3 = f3 / scale;
        }
        t1 *= scale;
        t2 *= scale;
        t3 *= scale;
        // the intonation: a sentence starts high and falls, a stressed vowel rises, a question rises at its end
        double target = 1.0 + liveliness * (0.16 - 0.32 * static_cast<double> (seg.position));
        if (seg.stressed)
            target += 0.22 * liveliness;
        if (seg.question && seg.position > 0.7f)
            target += 1.6 * liveliness * (static_cast<double> (seg.position) - 0.7);
        const double f0Target = f0Base * target;
        const bool plosive = p.kind == Kind::Plosive;
        const int burst = plosive ? std::min (seg.samples / 2, static_cast<int> (0.018 * sampleRate)) : 0;
        if (p.noiseHz > 0.0f)
        {
            noiseHz = p.noiseHz * (scale > 1.0 ? std::sqrt (scale) : 1.0);
            noiseBw = p.noiseBw;
        }
        for (int i = 0; i < seg.samples && n < total; ++i, ++n)
        {
            const bool inBurst = plosive && i >= seg.samples - burst;
            double wantVoice = p.voice, wantNoise = inBurst ? p.noise : (plosive ? 0.0 : p.noise);
            if (seg.phone == phoneIndex ("r")) // a rolled r
                wantVoice *= 0.6 + 0.4 * std::cos (2.0 * kPi * 26.0 * i / sampleRate);
            if (p.kind == Kind::Pause)
                wantVoice = wantNoise = 0.0;
            f1 += (t1 - f1) * formantGlide;
            f2 += (t2 - f2) * formantGlide;
            f3 += (t3 - f3) * formantGlide;
            voiceAmp += (wantVoice - voiceAmp) * ampGlide;
            noiseAmp = inBurst ? wantNoise * (1.0 - static_cast<double> (i - (seg.samples - burst)) / std::max (1, burst))
                               : noiseAmp + (wantNoise - noiseAmp) * ampGlide;
            f0 += (f0Target - f0) * (robot ? 1.0 : pitchGlide);

            // the source: glottal pulses (Rosenberg's shape, differentiated by the lips), a buzz, or breath
            double source;
            if (whisper)
                source = 0.35 * white();
            else
            {
                const double vibrato = robot ? 1.0 : 1.0 + 0.004 * std::sin (2.0 * kPi * 5.2 * static_cast<double> (n) / sampleRate);
                phase += f0 * vibrato / sampleRate;
                if (phase >= 1.0)
                    phase -= 1.0;
                double pulse;
                if (robot)
                    pulse = 2.0 * phase - 1.0;
                else if (phase < 0.4)
                    pulse = 0.5 * (1.0 - std::cos (kPi * phase / 0.4));
                else if (phase < 0.56)
                    pulse = std::cos (kPi * (phase - 0.4) / 0.32);
                else
                    pulse = 0.0;
                source = robot ? pulse : (pulse - lastPulse) * sampleRate / (8.0 * f0) + 0.02 * white();
                lastPulse = pulse;
            }
            double y = r1.process (source * voiceAmp, f1, 70.0, sampleRate);
            y = r2.process (y, f2, 100.0, sampleRate);
            y = r3.process (y, f3, 140.0, sampleRate);
            y = r4.process (y, 3400.0 * scale, 250.0, sampleRate);
            y = r5.process (y, 4200.0 * scale, 300.0, sampleRate);
            voiced[n] = static_cast<float> (y);
            hiss[n] = static_cast<float> (noiseFilter.process (white(), noiseHz, noiseBw, sampleRate) * noiseAmp);
            vowelHere[n] = p.kind == Kind::Vowel ? 1 : 0;
            hissHere[n] = p.kind == Kind::Fricative && p.voice <= 0.0f ? 1 : 0;
        }
    }
    // let the filters ring out
    for (; n < total; ++n)
    {
        voiceAmp *= 1.0 - ampGlide;
        double y = r1.process (0.0, f1, 70.0, sampleRate);
        y = r2.process (y, f2, 100.0, sampleRate);
        y = r3.process (y, f3, 140.0, sampleRate);
        y = r4.process (y, 3400.0 * scale, 250.0, sampleRate);
        voiced[n] = static_cast<float> (r5.process (y, 4200.0 * scale, 300.0, sampleRate));
    }

    // The levels: the vowels at one level, the hiss of s, f and sh a little under it, whatever the filters' gains.
    const auto rmsWhere = [] (const std::vector<float>& x, const std::vector<std::uint8_t>& where)
    {
        double sum = 0.0;
        size_t count = 0;
        for (size_t i = 0; i < x.size(); ++i)
            if (where[i] != 0)
            {
                sum += static_cast<double> (x[i]) * x[i];
                ++count;
            }
        return count > 0 ? std::sqrt (sum / static_cast<double> (count)) : 0.0;
    };
    const double voicedRms = rmsWhere (voiced, vowelHere), hissRms = rmsWhere (hiss, hissHere);
    const double voicedGain = voicedRms > 1.0e-9 ? 0.25 / voicedRms : 1.0;
    const double hissGain = hissRms > 1.0e-9 ? 0.11 / hissRms : (voicedRms > 1.0e-9 ? voicedGain : 1.0);
    auto data = std::make_shared<SampleData>();
    data->sampleRate = sampleRate;
    data->left.resize (total);
    float peak = 0.0f;
    for (size_t i = 0; i < total; ++i)
    {
        const double x = voiced[i] * voicedGain + hiss[i] * hissGain;
        data->left[i] = static_cast<float> (x);
        peak = std::max (peak, std::abs (data->left[i]));
    }
    if (peak > 0.0f)
        for (auto& x : data->left)
            x *= 0.8f / peak;
    data->right = data->left;
    std::string name = text.substr (0, 40);
    for (auto& c : name)
        if (c == '\n' || c == '\r' || c == '\t')
            c = ' ';
    data->name = "Speech: " + name;
    data->source = "builtin:" + speechId (text, settings);
    return data;
}

std::shared_ptr<const SampleData> makeSpeechSample (const std::string& id)
{
    std::string text;
    SpeechSettings settings;
    if (! parseSpeechId (id, text, settings))
        return nullptr;
    return speak (text, settings);
}
} // namespace sls
