# Sun Loops Studio

*[English](README.md)*

Un programma open source per produrre musica a loop su Linux, scritto in C++20 con JUCE 8. Costruisci ritmi e
melodie nel **Channel Rack**, scrivi le note nel **Piano Roll**, disponi i pattern nella **Playlist**, bilanciali nel
**Mixer** con effetti ed esporti il risultato in WAV. Un **Browser** a sinistra raccoglie i tuoi suoni; ogni altro
strumento sta in un pannello mobile che puoi spostare, ridimensionare, sovrapporre, aprire e chiudere.

![Sun Loops Studio: browser, channel rack e playlist](docs/images/overview.png)

![Il mixer espanso con una catena di effetti](docs/images/mixer.png)

Sun Loops Studio si ispira al modo di lavorare dei DAW a loop come FL Studio. È un progetto indipendente: non usa
codice, nomi, grafica o risorse di altri prodotti e non è affiliato né approvato dai loro produttori.

**Stato: alpha (0.7.0).** Compila, i test automatici passano e l'interfaccia è stata provata in un display virtuale.
La riproduzione è stata confermata su un vero PC Nobara per la versione 0.3; tutto ciò che è stato aggiunto dopo
(pannelli, pattern, playlist, browser, pitch/swing, mixer espanso ed effetti, salvataggio dei progetti, AppImage) è stato provato con test automatici e
in un display virtuale con un dispositivo audio muto, non ancora con audio reale. **L'hosting di plugin VST3 non è
implementato**: gli slot effetti del mixer contengono gli effetti propri di Sun Loops Studio (Equalizer, Reverb,
Delay, Compressor). Vedi [docs/roadmap.md](docs/roadmap.md) per sapere con precisione cosa è verificato, cosa no e
cosa manca (hosting VST3 e annulla/ripristina sono le mancanze principali).

## Funzioni

- **Pannelli mobili** - Channel Rack, Playlist, Piano Roll e Mixer. Si trascinano dalla barra del titolo, si
  ridimensionano dai bordi, si chiudono con la ×, il doppio clic sul titolo li massimizza. I pannelli si
  sovrappongono; un clic su uno lo porta davanti. `F5` / `F6` / `F7` / `F9` aprono, portano davanti o chiudono
  Playlist / Rack / Piano Roll / Mixer. La disposizione viene ricordata.
- **Browser** (barra laterale a sinistra, `F8` la ripiega) - un albero con *Samples*, *Drum Kits*, *Instruments*,
  *Presets* e *Plugin*, una casella di ricerca rapida e il trascinamento (o il doppio clic) dei suoni su un canale
  del Channel Rack. I tuoi file stanno in `~/Music/Sun Loops Studio/`.
- **Channel Rack** - fino a 8 canali, ognuno synth o sampler. Sequencer a step da 16esimi sui canali sampler,
  anteprima delle note sui canali synth. Per canale: volume, pan, manopola **Pitch** (semitoni), manopola **Swing** e
  un display **Track N** che manda il canale a una traccia del mixer. Tasto destro su un canale: *Piano roll*,
  *Instrument*, *Load sample...*, *Rename...*, *Assign free mixer track*, *Save as preset...*.
- **Piano Roll** - per il canale selezionato: clic per aggiungere una nota, trascina per spostarla, trascina il bordo
  destro per ridimensionarla, tasto destro per eliminarla, scorrimento e zoom. Si apre da un canale (menu o doppio
  clic), come in FL Studio.
- **Pattern e Playlist** - più pattern (nuovi o copiati) disposti come clip su 8 tracce. Il pulsante *Pattern / Song*
  sceglie se ripetere il pattern selezionato o suonare tutta la playlist.
- **Mixer** (`F9`) - 8 tracce verticali, ognuna con fader e dB, bilanciamento stereo, mute, solo, indicatore
  Peak/RMS e 4 slot per effetti in serie (Equalizer, Reverb, Delay, Compressor, con bypass). La strip master a destra
  ha gli stessi 4 slot, applicati a tutto il mix. Più canali possono usare la stessa traccia.
- **Sampler** - carica file WAV, FLAC, OGG e AIFF (fino a 30 s), li suona una volta sola, ritonati in base alla nota.
  Cassa, rullante e hi-hat sono sintetizzati: non servono file per iniziare.
- **Progetti** - menu `File` e `Ctrl+N` / `Ctrl+O` / `Ctrl+S` / `Ctrl+Maiusc+S`: *Save* scrive un progetto con nome;
  un progetto che non ha ancora un nome viene salvato subito con un nome automatico (`Untitled <data ora>.slsproj`)
  in `~/Music/Sun Loops Studio/Projects`, così non si perde nulla; *Save as...* chiede il nome. Progetti recenti,
  barra del titolo con il nome e `*` per le modifiche non salvate, avviso prima di chiudere con lavoro non
  salvato. I sample sono referenziati con il percorso, non copiati nel file.
- **Esportazione** - pattern o brano in WAV a 24 bit / 48 kHz, generato dallo stesso motore che lo suona.
- **Audio** - scelta del dispositivo, **buffer** regolabile (predefinito 1024 campioni) e un tasto **Auto** che prova
  il tuo computer e sceglie una dimensione che non gracchia, più la lettura di carico e blocchi persi. La scelta viene
  ricordata.

## Avvio rapido (Nobara / Fedora)

```bash
git clone https://github.com/Hubert9204/Sun-Loops-Studio.git
cd Sun-Loops-Studio
./scripts/setup_nobara.sh                      # installa le dipendenze (chiede sudo)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/SunLoopsStudio_artefacts/Release/"Sun Loops Studio"
```

Alla prima configurazione CMake scarica JUCE 8.0.4 (serve la rete una volta). Per metterlo nel menu delle
applicazioni: `cmake --install build --prefix ~/.local`. Per creare un'unica **AppImage**
(`dist/Sun-Loops-Studio-<versione>-x86_64.AppImage`): `./scripts/build_appimage.sh`. Altri dettagli e risoluzione dei problemi:
[docs/building.md](docs/building.md) (in inglese).

**Spazio** per suonare, **F5 / F6 / F7 / F9** per i pannelli e **F8** per il browser. Una guida all'uso è in
[docs/user-guide.it.md](docs/user-guide.it.md).

## Documentazione

| Documento | Contenuto |
| --- | --- |
| [docs/user-guide.it.md](docs/user-guide.it.md) | Come usare ogni pannello, scorciatoie, limiti ([English](docs/user-guide.md)) |
| [docs/building.md](docs/building.md) | Dipendenze, build, installazione, test, problemi comuni (inglese) |
| [docs/architecture.md](docs/architecture.md) | Thread, regole real-time, flusso dei dati, pattern e playlist, pannelli, test (inglese) |
| [docs/roadmap.md](docs/roadmap.md) | Cosa è verificato, cosa no, cosa viene dopo (inglese) |
| [CHANGELOG.md](CHANGELOG.md) | Modifiche per versione (inglese) |

## Test

```bash
./build/SunLoopsStudioTests_artefacts/Release/SunLoopsStudioTests
```

Non servono dispositivo audio né display. Controllano, tra l'altro, intonazione del synth e tempi esatti al
campione, loop, polifonia, tracce del mixer, pitch e swing, i quattro effetti, sampler, che `Player::render` non
allochi mai memoria (effetti compresi), uno stress test concorrente del passaggio dati interfaccia→audio, andata e
ritorno WAV, lo scanner dei plugin, l'albero e la scansione del browser, i preset dei canali, le regole dei pannelli, la
playlist, le note dal vivo e il file di progetto (andata e ritorno, file danneggiati o ostili, sample mancanti,
scrittura atomica).

## Licenza

Il repository contiene una [LICENSE](LICENSE) MIT. Attenzione: JUCE è disponibile solo con licenza GPLv3/AGPLv3 o con
licenza commerciale a pagamento, e un programma che usa JUCE con la licenza gratuita deve a sua volta essere
distribuito con GPL. Prima di distribuire eseguibili, o si passa questo progetto a GPLv3 oppure si acquista una
licenza commerciale JUCE.
