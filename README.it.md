# Sun Loops Studio

*[English](README.md)*

Un programma open source per produrre musica a loop su Linux, scritto in C++20 con JUCE 8. Costruisci ritmi e
melodie nel **Channel Rack**, scrivi le note nel **Piano Roll**, disponi i pattern nella **Playlist**, bilanciali nel
**Mixer** ed esporti il risultato in WAV. Ogni strumento sta in un pannello mobile che puoi spostare,
ridimensionare, sovrapporre, aprire e chiudere.

![Sun Loops Studio: channel rack, playlist e piano roll](docs/images/overview.png)

Sun Loops Studio si ispira al modo di lavorare dei DAW a loop come FL Studio. È un progetto indipendente: non usa
codice, nomi, grafica o risorse di altri prodotti e non è affiliato né approvato dai loro produttori.

**Stato: alpha (0.4.0).** Compila, i test automatici passano e l'interfaccia è stata provata in un display virtuale.
La riproduzione è stata confermata su un vero PC Nobara per la versione 0.3; pannelli, pattern e playlist aggiunti
dopo sono stati provati senza interfaccia audio reale (test automatici e display virtuale). Vedi
[docs/roadmap.md](docs/roadmap.md) per sapere con precisione cosa è verificato, cosa no e cosa manca (salvataggio dei
progetti e plugin VST sono le mancanze principali).

## Funzioni

- **Pannelli mobili** - Channel Rack, Playlist, Piano Roll e Mixer. Si trascinano dalla barra del titolo, si
  ridimensionano dai bordi, si chiudono con la ×, il doppio clic sul titolo li massimizza. I pannelli si
  sovrappongono; un clic su uno lo porta davanti. `F5` / `F6` / `F7` / `F9` aprono, portano davanti o chiudono
  Playlist / Rack / Piano Roll / Mixer. La disposizione viene ricordata.
- **Channel Rack** - fino a 8 canali, ognuno synth o sampler. Sequencer a step da 16esimi sui canali sampler,
  anteprima delle note sui canali synth. Tasto destro su un canale: *Piano roll*, *Instrument*, *Load sample...*,
  *Rename...*.
- **Piano Roll** - per il canale selezionato: clic per aggiungere una nota, trascina per spostarla, trascina il bordo
  destro per ridimensionarla, tasto destro per eliminarla, scorrimento e zoom. Si apre da un canale (menu o doppio
  clic), come in FL Studio.
- **Pattern e Playlist** - più pattern (nuovi o copiati) disposti come clip su 8 tracce. Il pulsante *Pattern / Song*
  sceglie se ripetere il pattern selezionato o suonare tutta la playlist.
- **Mixer** - una strip verticale per canale: fader con dB, pan, mute, solo, indicatore di picco.
- **Sampler** - carica file WAV, FLAC, OGG e AIFF (fino a 30 s), li suona una volta sola, ritonati in base alla nota.
  Cassa, rullante e hi-hat sono sintetizzati: non servono file per iniziare.
- **Esportazione** - pattern o brano in WAV a 24 bit / 48 kHz, generato dallo stesso motore che lo suona.
- **Audio** - scelta del dispositivo e lettura di frequenza di campionamento, buffer e latenza dichiarata.

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
applicazioni: `cmake --install build --prefix ~/.local`. Altri dettagli e risoluzione dei problemi:
[docs/building.md](docs/building.md) (in inglese).

**Spazio** per suonare, **F5 / F6 / F7 / F9** per i pannelli. Una guida all'uso è in
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
campione, loop, polifonia, mixer, sampler, che `Player::render` non allochi mai memoria, uno stress test concorrente
del passaggio dati interfaccia→audio, andata e ritorno WAV, lo scanner dei plugin, le regole dei pannelli e la
playlist.

## Licenza

Il repository contiene una [LICENSE](LICENSE) MIT. Attenzione: JUCE è disponibile solo con licenza GPLv3/AGPLv3 o con
licenza commerciale a pagamento, e un programma che usa JUCE con la licenza gratuita deve a sua volta essere
distribuito con GPL. Prima di distribuire eseguibili, o si passa questo progetto a GPLv3 oppure si acquista una
licenza commerciale JUCE.
