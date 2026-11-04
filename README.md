# FL Studio Clone DAW

**Motore audio real-time-safe per Nobara Linux 44** (Fedora-based, PipeWire, KDE Plasma)

## 📋 Panoramica

DAW open source con architettura modulare in C++20 + JUCE 8. La Fase 1 implementa:

- ✅ Finestra principale stile FL Studio (tema scuro)
- ✅ Motore audio con callback real-time-safe (zero allocazioni/lock)
- ✅ Dispositivo audio PipeWire/JACK via JUCE AudioDeviceManager
- ✅ Oscillatore sinusoidale con lookup table (4096 punti)
- ✅ Buffer circolare lock-free per comunicazione audio↔UI
- ✅ VU Meter con gradazioni verde/giallo/rosso
- ✅ Controllo frequenza (20 Hz — 20 kHz) e volume master

La Fase 2 aggiunge:

- ✅ Plugin host VST3 base
- ✅ Caricamento plugin da file .vst3
- ✅ Processo audio con plugin attivo

## 🏗️ Architettura

```
┌─────────────────────────────────────────────────────┐
│                   MainWindow (UI)                    │
│  ┌──────────┬──────────────┬─────────────────────┐  │
│  │ Transport│ Frequency    │ Volume              │  │
│  │ Play/Stop│ Slider       │ Slider              │  │
│  ├──────────┴──────────────┴─────────────────────┤  │
│  │  AudioMeter (L)    │    AudioMeter (R)        │  │
│  └───────────────────────────────────────────────┘  │
└─────────────────────────┬───────────────────────────┘
                          │ AtomicFloat (30fps UI thread)
┌─────────────────────────▼───────────────────────────┐
│                   AudioEngine                        │
│  ┌──────────────┐    ┌──────────────────────────┐   │
│  │ Oscillator   │───▶│ RingBuffer (lock-free)   │   │
│  │ (lookup tab.)│    │ audio thread → UI thread │   │
│  └──────────────┘    └──────────────────────────┘   │
│                                                      │
│  ┌──────────────────────────────────────────────┐   │
│  │ AudioDevice (JUCE → PipeWire/JACK backend)   │   │
│  └──────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────┘

┌─────────────────────────▼───────────────────────────┐
│                  PluginHost (VST3)                   │
│  ┌──────────────────────────────────────────────┐   │
│  │ juce::AudioProcessor                         │   │
│  └──────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────┘
```

### Scelte architetturali

| Componente | Tecnologia | Motivazione |
|---|---|---|
| **Framework GUI** | JUCE 8 | PipeWire nativo su Fedora, tema scuro, cross-platform |
| **Backend audio** | PipeWire (via JUCE) | Default su Nobara/Fedora |
| **Buffer circolare** | RingBuffer lock-free | Zero lock, zero allocazioni nel thread audio |
| **Oscillatore** | Lookup table 4096 punti | Evita `sinf()` nel callback real-time |
| **Comunicazione UI↔Audio** | AtomicFloat + RingBuffer | Lock-free, cache-line aligned |
| **Plugin VST3** | JUCE AudioProcessor | Cross-platform plugin support |

## 🚀 Installazione su Nobara Linux 44

### Requisiti di sistema

- Nobara Linux 44 (o Fedora 40+)
- KDE Plasma desktop
- PipeWire con supporto JACK (già incluso in Nobara)
- GPU con driver Vulkan/Mesa (per JUCE OpenGL)

### Script di setup

```bash
git clone https://github.com/youruser/flstudio-clone.git
cd flstudio-clone
chmod +x scripts/setup_nobara.sh
./scripts/setup_nobara.sh
```

### Compilazione manuale

```bash
# 1. Clona il progetto
git clone https://github.com/youruser/flstudio-clone.git
cd flstudio-clone

# 2. Installa dipendenze (se lo script non è stato usato)
sudo dnf install -y gcc-c++ cmake git ninja-build \
    pipewire-devel libjack2-devel alsa-lib-devel fontconfig-devel freetype-devel vulkan-devel vulkan-tools

# 3. Configura e compila
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja

# 4. Esegui
./FLStudioClone
```

## 🔧 Configurazioni avanzate

### Buffer size e latenza

| Buffer Size | Latenza teorica | Stabilità | Uso consigliato |
|---|---|---|---|
| 32 samples | ~0.67 ms | Bassa | Monitoring in tempo reale |
| 64 samples | ~1.33 ms | Media-Bassa | Registrazione live |
| **128 samples** | **~2.67 ms** | **Media** | **Default (bilanciato)** |
| 256 samples | ~5.33 ms | Alta | Mixing / produzione |
| 512+ samples | >10 ms | Molto alta | CPU limitata |

### Flag CMake

```bash
# Abilita JACK nativo (oltre a PipeWire)
cmake .. -DENABLE_JACK=ON

# Log timing nel callback audio (solo debug)
cmake .. -DDEBUG_AUDIO_TIMING=ON
```

## 📊 Thread Model

### Thread Audio (callback JUCE)
```
onAudioReady() {
    oscillator_.setFrequency(freq_);   // atomic load
    oscillator_.generate(tempBuffer);  // lookup table, zero cosf
    applyVolume(tempBuffer, vol_);     // atomic load
    ringBuffer_.push(tempBuffer);      // lock-free push
    copyToOutput(tempBuffer);          // memcpy
}
```
**Regole real-time-safe:**
- ✅ Stack allocation solo (`float[2048]`)
- ✅ Atomic loads per frequenza/volume
- ✅ Lock-free RingBuffer push
- ❌ No `new`/`delete`/`malloc`/`free`
- ❌ No `std::mutex`/`std::lock_guard`
- ❌ No `cout`/`cerr`/`printf`
- ❌ No syscall (tranne read/write del dispositivo)

### Thread UI (JUCE MessageManager)
```
timerCallback() {
    peak = audioEngine_.readPeakLevel();  // AtomicFloat load
    meter_->setPeakLevel(peak);           // repaint()
}
```
- Aggiornamento a ~30fps (non real-time)
- Smoothing esponenziale per evitare flickering

## ⚠️ Punti critici di latenza

1. **Buffer size PipeWire**: il parametro più influente. Su Nobara, il default è spesso 256.
   ```bash
   # Verifica buffer corrente
   pw-cli info all | grep -A5 "node" | grep "latency"
   
   # Riduci con pipewire.conf personalizzato
   ```

2. **Periodicità del callback**: PipeWire chiama il callback ogni `buffer_size / sample_rate` secondi.
   Con 128 samples a 48kHz = 2.67ms per callback.

3. **Lock-free RingBuffer**: se l'UI non legge abbastanza veloce, i campioni vengono scartati.
   Monitora `dropped_samples` nella status bar.

4. **Schedulazione thread**: su KDE Plasma con PipeWire, il thread audio dovrebbe avere
   priorità alta. Verifica con:
   ```bash
   ps -eLo pid,pri,class,rtprio,comm | grep pipewire
   ```

5. **Power saving**: su laptop, CPU scaling governor può causare jitter.
   ```bash
   # Imposta performance governor
   sudo cpupower frequency-set -g performance
   ```

## 📁 Struttura del progetto

```
flstudio-clone/
├── CMakeLists.txt                    # Build system CMake
├── README.md                         # Questo file
├── src/
│   ├── main.cpp                      # Entry point JUCE
│   ├── audio/
│   │   ├── AudioEngine.h/cpp         # Motore audio centrale (aggiunto PluginHost)
│   │   ├── Oscillator.h/cpp          # Generatore onda sinusoidale
│   │   └── RingBuffer.h              # Buffer circolare lock-free
│   ├── ui/
│   │   ├── MainWindow.h/cpp          # Finestra principale (aggiunta plugin UI)
│   │   └── AudioMeter.h/cpp          # VU Meter real-time
│   └── common/
│       ├── AtomicFloat.h             # Float thread-safe
│       └── Constants.h               # Costanti globali
├── resources/icons/                  # Icone applicazione
├── scripts/setup_nobara.sh           # Setup automatico Nobara
└── docs/
    └── architecture.md               # Documentazione architetturale
```

## 🔮 Roadmap

- [x] **Fase 1**: Scheletro progetto + oscillatore sinusoidale
- [x] **Fase 2**: Plugin host VST3 base (JUCE PluginHost)
- [ ] **Fase 3**: Piano roll editor (stile FL Studio)
- [ ] **Fase 4**: Mixer con canali, EQ, effects
- [ ] **Fase 5**: Arranger/Sequencer multi-track
- [ ] **Fase 6**: Renderizzazione audio offline

## 📄 Licenza

MIT License — vedi `LICENSE` per dettagli.

## 🤝 Contributi

PRs benvenuti! Per favore apri un issue prima di lavorare su feature grandi.
