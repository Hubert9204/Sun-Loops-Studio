# Documentazione Architetturale

## Flusso dati audio

```
┌──────────────┐     ┌───────────────┐     ┌────────────────┐
│ Oscillator   │────▶│ RingBuffer    │────▶│ AudioDevice    │
│ (UI thread)  │     │ (lock-free)   │     │ (PipeWire)     │
│              │     │               │     │                │
│ • Lookup tab │     │ • push()      │     │ • outputs[]    │
│ • phase incr │     │ • capacity:   │     │ • stereo out   │
│ • zero cosf  │     │   65536       │     │ • 48kHz/128s   │
└──────────────┘     │ samples       │     └────────────────┘
                     └───────▲───────┘
                             │ pop()
                     ┌───────┴────────┐
                     │ AudioMeter (UI)│
                     │ • smoothed peak│
                     │ • dBFS display │
                     └────────────────┘

┌─────────────────────────▼───────────────────────────┐
│                  PluginHost (VST3)                   │
│  ┌──────────────────────────────────────────────┐   │
│  │ juce::AudioProcessor                         │   │
│  └──────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────┘
```

## Thread safety

### RingBuffer<T, Capacity>
- **Producer**: thread audio (solo push)
- **Consumer**: thread UI (solo pop)
- **Single producer, single consumer** = lock-free senza atomics complessi
- **Memory order**: `relaxed` per lettura/scrittura, `acquire/release` per visibilità
- **Cache-line alignment**: 64 byte per evitare false sharing

### AtomicFloat
- Reinterpret cast `float ↔ uint32_t` su `std::atomic<uint32_t>`
- Garantisce che la lettura sia atomicamente consistente (non il calcolo)
- Perfetto per valori scalari che cambiano raramente (frequenza, volume, peak)

## Scelte di design

### Perché lookup table invece di sinf()?
`sinf()` è una funzione costosa (~100+ cicli CPU). La lookup table:
- Occupa solo 16 KB (4096 × 4 byte) — sta nella L1 cache
- Accesso array: ~1 ciclo CPU
- Precisione sufficiente per audio musicale (< 0.01% errore)

### Perché buffer circolare invece di mutex?
- Il mutex introduce jitter non deterministico
- Lock-free = latenza massima garantita (worst-case = accesso memoria)
- Single-producer/single-consumer = implementazione più semplice

### Perché AtomicFloat invece di std::atomic<float>?
- C++20 ha `std::atomic<float>` ma su alcune architetture usa lock interno
- Reinterpret cast a uint32_t è sempre lock-free su x86_64
- Compatibilità garantita con tutte le piattaforme JUCE supportate