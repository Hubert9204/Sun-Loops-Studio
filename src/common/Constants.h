#pragma once

// =============================================================================
// Costanti globali del progetto
// =============================================================================

namespace FLStudioClone {

// --- Audio ---
constexpr int   kDefaultSampleRate  = 48000;       // Hz — standard video/audio pro
constexpr int   kDefaultBufferSize  = 128;         // samples — compromesso latenza/stabilità
constexpr int   kMinBufferSize      = 32;          // Hz — minimo per < 1ms latenza teorica
constexpr int   kMaxBufferSize      = 2048;        // Hz — massimo per stabilità
constexpr int   kNumChannels        = 2;           // Stereo

// --- RingBuffer ---
constexpr size_t kRingBufferCapacity = 65536;      // 64K samples — potenza di 2, lock-free

// --- UI ---
constexpr int    kMeterUpdateFPS     = 30;         // VU meter refresh rate
constexpr int    kMeterSmoothingMs   = 150;        // ms smoothing per display

// --- Finestra principale ---
constexpr int    kMainWindowWidth    = 1280;
constexpr int    kMainWindowHeight   = 800;

// --- Debug audio callback (solo dev) ---
#ifdef DEBUG_AUDIO_TIMING
    #define AUDIO_LOG(fmt, ...) \
        jassertfalse; /* break in debug */
#else
    #define AUDIO_LOG(fmt, ...) ((void)0)
#endif

} // namespace FLStudioClone
