# Guida all'uso

*[English](user-guide.md)*

Questa guida descrive cosa fa oggi Sun Loops Studio 0.4. Limiti e funzioni mancanti sono elencati alla fine.

## La finestra

Una **barra degli strumenti** su due righe, una riga di stato e sotto l'**area di lavoro**, dove i pannelli
fluttuano.

Barra, prima riga: **Play / Stop**, pulsante **Pattern / Song**, il **menu dei pattern** con **+**, **BPM**
(40-240), volume **Master** con il suo indicatore, **Audio...** (impostazioni del dispositivo) ed **Export WAV...**.

Barra, seconda riga: i pulsanti che mostrano o nascondono i pannelli (**Rack F6**, **Playlist F5**, **Piano F7**,
**Mixer F9**), **Reset** (disposizione predefinita dei pannelli) e **Clear notes** (svuota il canale selezionato nel
pattern corrente).

La riga di stato mostra il dispositivo audio (frequenza, buffer, latenza dichiarata) e l'ultimo messaggio.

## Pannelli

I pannelli sono quattro: Channel Rack, Playlist, Piano Roll e Mixer. Si comportano tutti allo stesso modo.

| Azione | Come |
| --- | --- |
| Spostare | Trascina la barra del titolo. Il pannello resta dentro l'area di lavoro. |
| Ridimensionare | Trascina un bordo o un angolo. Ogni pannello ha una dimensione minima. |
| Chiudere | Clic sulla **×** nella barra del titolo. |
| Massimizzare / ripristinare | Doppio clic sulla barra del titolo. |
| Portare davanti | Clic in un punto qualsiasi del pannello. Il pannello attivo ha la barra del titolo arancione. |
| Aprire / portare davanti / chiudere | Pulsante della barra o tasto. Un pannello chiuso si apre; uno aperto ma coperto passa davanti; uno già in primo piano si chiude. |
| Disposizione predefinita | **Reset** |

I pannelli possono sovrapporsi: puoi tenere il piano roll sopra il rack mentre scrivi e far salire il mixer quando
serve. Posizioni, dimensioni e stato aperto/chiuso vengono salvati a ogni modifica e ripristinati all'avvio
successivo (file: `~/.config/SunLoopsStudio/SunLoopsStudio.settings`; cancellalo per tornare alla disposizione
predefinita).

Disposizione predefinita: Channel Rack e Playlist aperti in alto, Piano Roll e Mixer chiusi.

## Tastiera

| Tasto | Azione |
| --- | --- |
| Spazio | Play / Stop |
| F5 | Playlist |
| F6 | Channel Rack |
| F7 | Piano Roll |
| F9 | Mixer |

I tasti funzionano quando la finestra principale è attiva. Mentre scrivi in un campo di testo (per esempio per
rinominare un canale) la tastiera è del campo.

## Channel Rack

Una riga per canale: nome, **M** (mute), **S** (solo), volume, pan, indicatore di livello e, a destra, l'area degli
step.

- **Volume** da 0 a 1,5 (predefinito 0,8) e **pan** da sinistra a destra; doppio clic su un cursore per
  riportarlo al valore predefinito.
- I **canali sampler** mostrano 32 pulsanti step (due battute di sedicesimi). Clic su uno step per accenderlo o
  spegnerlo. Il pulsante **Load** sceglie un file audio.
- I **canali synth** mostrano un'anteprima delle note. Le note si modificano nel Piano Roll.
- Clic su una riga per selezionare il canale (Piano Roll e Mixer seguono la selezione).
- **+ Add channel** sotto l'ultima riga aggiunge un canale synth o sampler, fino a 8 canali in tutto.

### Menu del tasto destro su un canale

| Voce | Effetto |
| --- | --- |
| Piano roll | Seleziona il canale e apre il Piano Roll per quel canale. |
| Instrument > Synth / Sampler | Sceglie cosa produce il suono. Passando a Sampler su un canale senza sample ne viene chiesto uno. Il sample resta se torni a Synth. |
| Load sample... | Scegli un file WAV, FLAC, OGG o AIFF (si usano i primi 30 secondi). Il canale prende il nome del file. |
| Rename... | Fino a 16 caratteri. |

Anche il doppio clic sull'anteprima delle note di un canale synth, o sul nome di un qualsiasi canale, apre il suo
Piano Roll. Sui canali sampler il doppio clic sugli step non ha un significato speciale: ogni clic attiva o
disattiva uno step.

## Piano Roll

Modifica le note del canale selezionato nel pattern corrente. Il titolo mostra il canale ("Piano Roll - Lead"). È
chiuso all'avvio e si apre da un canale, come sopra.

| Azione | Come |
| --- | --- |
| Aggiungere una nota | Clic su una cella vuota; tieni premuto e trascina a destra per scegliere la lunghezza. |
| Spostare una nota | Trascinala. |
| Cambiare la lunghezza | Trascina il bordo destro. |
| Eliminare una nota | Tasto destro sulla nota. |
| Scorrere in altezza | Rotella del mouse. |
| Scorrere nel tempo | Maiusc + rotella. |
| Zoom nel tempo | Ctrl + rotella. Finché non zoomi, tutto il pattern entra nella larghezza del pannello. |

Le note si agganciano ai sedicesimi. Un pattern dura due battute. Una nota synth termina alla sua lunghezza o alla
fine del pattern, quale viene prima. Quando apri un canale la vista scorre fino alle sue note.

## Pattern

Un **pattern** contiene le note di tutti i canali. Il menu dei pattern nella barra sceglie il pattern corrente: il
Channel Rack e il Piano Roll mostrano e modificano quello. Il pulsante **+** accanto offre **New empty pattern** e
**Clone current pattern**. Il progetto dimostrativo parte con "Pattern 1" (il groove completo) e "Drums" (solo la
batteria).

Le impostazioni di mixer (volume, pan, mute, solo, strumento, sample) appartengono al canale, non al pattern: sono le
stesse in ogni pattern.

## Playlist

La Playlist dispone i pattern nel tempo. Ha 8 tracce e una linea del tempo lunga 32 pattern. Il righello mostra i
numeri di battuta (un pattern dura due battute, quindi i numeri vanno 1, 3, 5, ...). La linea verticale arancione
segna la fine del brano: in modalità Song la riproduzione ricomincia da lì.

| Azione | Come |
| --- | --- |
| Piazzare una clip | Clic su una cella vuota. La clip contiene il pattern scelto nel menu ed è lunga un pattern. Trascina subito a destra per allungarla. |
| Spostare una clip | Trascinala (si aggancia ai confini dei pattern e può cambiare traccia). |
| Ripetere / accorciare | Trascina il bordo destro della clip: il pattern si ripete una volta per ogni lunghezza di pattern. |
| Eliminare | Tasto destro sulla clip. |
| Selezionare il suo pattern | Doppio clic sulla clip. |
| Scorrere nel tempo | Rotella del mouse. |
| Zoom | Ctrl + rotella. |

Le clip non possono sovrapporsi sulla stessa traccia: uno spostamento o un ridimensionamento che si sovrapporrebbe
semplicemente non viene applicato. Le clip su tracce diverse suonano insieme.

## Modalità Pattern e Song

Il pulsante **Pattern / Song** decide cosa fa Play.

- **Pattern**: ripete il pattern corrente. Il cursore di riproduzione corre nel Piano Roll e nel Rack.
- **Song**: suona la Playlist dall'inizio alla fine dell'ultima clip, poi ricomincia. Il cursore corre nella
  Playlist. Una playlist vuota è silenziosa.

In entrambe le modalità puoi modificare mentre suona; il cambiamento si sente subito.

## Mixer

Una strip verticale per canale: nome, pan, mute / solo, un fader con la lettura in dB sotto (dal silenzio fino a
+3,5 dB; il valore predefinito 0,8 vale circa -1,9 dB; doppio clic per ripristinarlo) e un indicatore di livello. Le
strip sono collegate alle righe del rack: cambi una, l'altra segue. Clic su una strip per selezionare il canale. Il
volume master e il suo indicatore sono nella barra degli strumenti.

## Esportazione

**Export WAV...** scrive la modalità corrente: il pattern (due battute) o il brano (fino alla fine dell'ultima clip),
più un secondo di coda per non tagliare i rilasci e le code della batteria. Formato: WAV stereo a 24 bit, 48 kHz, al
BPM impostato nella barra. Il file è prodotto dallo stesso motore che suona, quindi suona come ciò che ascolti.

## Impostazioni audio

**Audio...** apre la scelta del dispositivo. Su Linux JUCE parla con ALSA o JACK; su Nobara i livelli di
compatibilità di PipeWire fanno funzionare entrambi. Se la riga di stato dice "No audio device", scegli un'uscita in
**Audio...** e controlla che il sistema abbia un'uscita disponibile. La latenza è quella dichiarata dal dispositivo:
il programma non la misura.

## Limiti

- Fino a 8 canali, 64 pattern, 8 tracce di playlist, una linea del tempo lunga 32 pattern, 32 voci simultanee.
- Un pattern dura sempre due battute, 4/4, griglia di sedicesimi.
- I sample sono mono o stereo, al massimo 30 secondi, suonati una volta sola (niente loop né editing
  dell'inviluppo).
- Non ancora disponibili: salvare e aprire progetti, annulla, plugin VST come strumenti o effetti, effetti di
  qualsiasi tipo, automazione, ingresso e registrazione MIDI, eliminare o rinominare i pattern, spostare il cursore
  con un clic sul righello. Vedi [roadmap.md](roadmap.md) (in inglese).
