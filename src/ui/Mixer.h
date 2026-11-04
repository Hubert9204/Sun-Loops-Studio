#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <memory>

// =============================================================================
// Mixer: mixer multi-canale per tracce audio.
// Gestisce volume, pan, mute/solo e effetti.
// =============================================================================

class Mixer : public juce::Component {
public:
    struct Track {
        std::string name;
        float volume = 0.7f;     // 0.0 - 1.0
        float pan = 0.0f;        // -1.0 (sinistra) a 1.0 (destra)
        bool muted = false;
        bool solo = false;
        std::vector<std::string> effects; // Nomi effetti attivi

        Track(const std::string& n) : name(n) {}
    };

    Mixer();
    ~Mixer() override;

    // --- API UI ---

    /** Aggiungi una nuova traccia */
    void addTrack(const std::string& name);

    /** Rimuovi una traccia */
    void removeTrack(int index);

    /** Imposta volume traccia */
    void setTrackVolume(int index, float volume);

    /** Imposta pan traccia */
    void setTrackPan(int index, float pan);

    /** Muta/dismuta traccia */
    void toggleMute(int index);

    /** Solo/disolo traccia */
    void toggleSolo(int index);

    /** @return lista tracce */
    const std::vector<Track>& getTracks() const noexcept { return tracks_; }

private:
    void paint(juce::Graphics& g) override;
    void resized() override;

    std::vector<Track> tracks_;
    static constexpr int kTrackHeight = 80;
    static constexpr int kNumTracks = 8; // Default
};