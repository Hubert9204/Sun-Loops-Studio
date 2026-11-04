#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <memory>

// =============================================================================
// Sequencer: gestore della sequenza musicale.
// Gestisce note, tracce, tempo (BPM) e timeline.
// =============================================================================

class Sequencer : public juce::Component {
public:
    struct Note {
        int pitch;     // 0-127 (MIDI note)
        int start;     // posizione in samples
        int duration;  // durata in samples
        int channel;   // canale MIDI
        float velocity; // 0.0 - 1.0

        Note(int p, int s, int d, int c, float v)
            : pitch(p), start(s), duration(d), channel(c), velocity(v) {}
    };

    struct Track {
        std::string name;
        std::vector<Note> notes;
        bool muted = false;
        bool solo = false;
        float volume = 1.0f;

        Track(const std::string& n) : name(n) {}
    };

    Sequencer();
    ~Sequencer() override;

    // --- API UI ---

    /** Aggiungi una nuova traccia */
    void addTrack(const std::string& name);

    /** Rimuovi una traccia */
    void removeTrack(int index);

    /** Aggiungi una nota a una traccia */
    void addNote(int trackIndex, const Note& note);

    /** Rimuovi una nota da una traccia */
    void removeNote(int trackIndex, int noteIndex);

    /** Imposta BPM (tempo) */
    void setBPM(float bpm);

    /** @return BPM corrente */
    float getBPM() const noexcept { return bpm_; }

    /** @return lista tracce */
    const std::vector<Track>& getTracks() const noexcept { return tracks_; }

    // --- API audio (per AudioEngine) ---

    /** Genera campioni per tutte le tracce attive */
    void generateAudio(float* const* outputs, size_t numSamples) noexcept;

private:
    void paint(juce::Graphics& g) override;
    void resized() override;

    std::vector<Track> tracks_;
    float bpm_ = 120.0f;
    int sampleRate_ = 48000;

    // --- Dati di rendering ---
    static constexpr int kPixelsPerBeat = 100;
    static constexpr int kNotesRangeMin = 0;   // C-1 (MIDI note)
    static constexpr int kNotesRangeMax = 127; // G9
};