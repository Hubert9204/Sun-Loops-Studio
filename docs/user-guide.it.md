# Guida all'uso

*[English](user-guide.md)*

Questa guida descrive cosa fa oggi Sun Loops Studio 0.6. Limiti e funzioni mancanti sono elencati alla fine.

## La finestra

Una **barra degli strumenti** su due righe, una riga di stato e sotto il **browser** (barra laterale a sinistra) e
l'**area di lavoro**, dove i pannelli fluttuano.

Barra, prima riga: **File** (progetti, vedi sotto), **Play / Stop**, pulsante **Pattern / Song**, il **menu dei pattern** con **+**, **BPM**
(40-240), volume **Master** con il suo indicatore, **Audio...** (impostazioni del dispositivo) ed **Export WAV...**.

Barra, seconda riga: i pulsanti che mostrano o nascondono il browser (**Browser F8**) e i pannelli (**Rack F6**,
**Playlist F5**, **Piano F7**, **Mixer F9**), **Reset** (disposizione predefinita dei pannelli) e **Clear notes** (svuota il canale selezionato nel
pattern corrente).

La riga di stato mostra il dispositivo audio (frequenza, buffer, latenza dichiarata) e l'ultimo messaggio.

## Progetti

Tutto il brano è salvato in un unico file `.slsproj`: canali (strumento, sample, volume, pan, pitch, swing, traccia
del mixer), tutti i pattern con le loro note, la playlist, il mixer (fader, bilanciamento, mute/solo, catene di
effetti e loro impostazioni), BPM, volume master e se la modalità Song è attiva. Il titolo della finestra mostra il
nome del progetto, seguito da `*` quando ci sono modifiche non salvate.

| Comando | Tasto | Cosa fa |
| --- | --- | --- |
| **New** | `Ctrl+N` | Parte con un progetto vuoto (un canale synth, un pattern vuoto). |
| **Open...** | `Ctrl+O` | Sceglie un file `.slsproj`. |
| **Open recent** | | Gli ultimi otto progetti che esistono ancora. |
| **Save** | `Ctrl+S` | Scrive il progetto. |
| **Save as...** | `Ctrl+Maiusc+S` | Chiede un nome (se manca, aggiunge `.slsproj`) e scrive il progetto lì. |

**Salvare senza nome.** Un progetto a cui non hai ancora dato un nome si chiama *Untitled*. **Save** (o `Ctrl+S`)
non si ferma a chiedere: scrive subito il progetto come `Untitled <data> <ora>.slsproj` in
`~/Music/Sun Loops Studio/Projects`, e da quel momento quello è il file del progetto, quindi il **Save** successivo
aggiorna lo stesso file. Usa **Save as...** quando vuoi un nome vero. Salvare un progetto con nome è sicuro: il
nuovo testo va in un file temporaneo che poi sostituisce il vecchio, quindi un salvataggio fallito lascia intatto il
file precedente.

**Lavoro non salvato.** **New**, **Open...** e la chiusura della finestra chiedono conferma se ci sono modifiche
non salvate: *Save*, *Don't save* o *Cancel*. Il tasto Esc annulla. Scegliere un pattern o passare da Pattern a Song
non conta come modifica. Puoi anche avviare il programma con un progetto come argomento
(`sun-loops-studio brano.slsproj`); aprire un file `.slsproj` dal file manager fa lo stesso quando il desktop
conosce il tipo (installato da `cmake --install`).

**I sample non sono copiati nel file.** Un canale sampler ricorda da dove viene il suo sample: un suono incluso o il
percorso completo del tuo file. Se sposti o cancelli il file il progetto si apre lo stesso: quel canale resta muto,
un messaggio elenca i file mancanti e il riferimento viene conservato, quindi rimettere il file al suo posto (o
caricarne un altro con *Load sample...*) risolve. Tieni i sample dove sono, oppure in
`~/Music/Sun Loops Studio/Samples`.

**File danneggiati.** Un file che non è un progetto di Sun Loops Studio, è rovinato o è stato scritto da una versione
più recente viene rifiutato con un messaggio e il progetto corrente resta intatto. I valori fuori limite vengono
riportati nei limiti e le note o clip che non si possono collocare vengono scartate, con un messaggio.

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

Disposizione predefinita: browser aperto a sinistra; Channel Rack lungo tutto il lato alto dell'area di lavoro con la
Playlist sotto; Piano Roll e Mixer chiusi. Il Mixer si apre grande, sopra tutto il resto.

Le disposizioni salvate da versioni precedenti alla 0.5 vengono ignorate una volta (le impostazioni dei pannelli sono
state rinominate); **Reset**, o semplicemente spostare i pannelli, salva quelle nuove.

## Tastiera

| Tasto | Azione |
| --- | --- |
| Spazio | Play / Stop |
| Ctrl+N / Ctrl+O | Nuovo progetto / Apri progetto |
| Ctrl+S / Ctrl+Maiusc+S | Salva / Salva con nome |
| F5 | Playlist |
| F6 | Channel Rack |
| F7 | Piano Roll |
| F8 | Browser (ripiega / apri) |
| F9 | Mixer |

I tasti funzionano quando la finestra principale è attiva. Mentre scrivi in un campo di testo (per esempio per
rinominare un canale) la tastiera è del campo.

## Browser

La barra laterale a sinistra elenca i tuoi suoni come un albero. Clic sulla freccia di una cartella per aprirla.

| Cartella | Contenuto |
| --- | --- |
| Samples | I suoni incorporati (cassa, rullante, hi-hat), poi i file audio (WAV, FLAC, OGG, AIFF) trovati in `~/Music/Sun Loops Studio/Samples` e nelle sue sottocartelle. |
| Drum Kits | Un kit incorporato, poi una voce per ogni sottocartella di `~/Music/Sun Loops Studio/Drum Kits`. |
| Instruments | *Synth* e *Sampler*. |
| Presets | Sei preset di canale di fabbrica, poi i tuoi file `.slspreset` da `~/Music/Sun Loops Studio/Presets`. |
| Plugin | All'inizio è vuota. *Scan for VST3 plugins...* cerca nelle cartelle VST3 abituali in background ed elenca ciò che trova. **I plugin non si possono ancora usare** (l'hosting non è implementato): sono mostrati solo a titolo informativo. |

Le tre cartelle in `~/Music/Sun Loops Studio` vengono create all'avvio del programma. **Folder** apre quella posizione
nel file manager; **Refresh** cerca file nuovi (il browser non controlla il disco in continuazione).

- **Ricerca**: scrivi nella casella in alto per filtrare tutte le cartelle insieme; le cartelle con risultati si
  aprono da sole e la riga sotto l'albero conta i risultati. **Esc** cancella la ricerca.
- **Trascinamento**: trascina un suono su un canale del Channel Rack (la riga si evidenzia con un contorno verde) per
  caricarlo lì; rilascialo nello spazio vuoto sotto le righe per creare un nuovo canale. Un preset imposta strumento,
  sample, volume, pan, pitch e swing del canale; una voce di *Instruments* cambia lo strumento del canale.
- **Doppio clic** su una voce: la applica al canale selezionato.
- **Ripiegare**: il pulsante con la freccia nel titolo del browser, **F8** o il pulsante **Browser** della barra. Resta
  una linguetta sottile sul bordo sinistro per riaprirlo. Lo stato viene ricordato.

Sono supportati solo i trascinamenti che partono dal browser; trascinare file dal file manager del desktop sul programma
non è ancora implementato (copiali nella cartella Samples e premi Refresh).

## Channel Rack

Una riga per canale: un display **Track N** e il nome, **M** (mute), **S** (solo), volume, pan, le manopole **Pitch** e
**Swing**, un indicatore di livello e, a destra, l'area degli step.

- **Volume** da 0 a 1,5 (predefinito 0,8) e **pan** da sinistra a destra; doppio clic su un cursore per
  riportarlo al valore predefinito.
- **Pitch** intona tutte le note del canale da -12 a +12 semitoni a passi di 0,1 (su un canale sampler cambia la
  velocità del sample, quindi anche la sua durata). Trascina la manopola su o giù; doppio clic per 0.
- **Swing** da 0 a 100 %: i sedicesimi in levare (il 2°, 4°, 6°... di ogni quarto) vengono ritardati, fino a mezzo
  sedicesimo al 100 %. Le note che verrebbero spinte oltre la fine del pattern restano dove sono.
- **Track N** è la traccia del mixer attraverso cui suona il canale. Clic per un menu delle 8 tracce, oppure
  trascinalo su e giù, o usa la rotella sopra di esso. All'inizio il canale 1 va alla Track 1, il canale 2 alla
  Track 2 e così via.
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
| Save as preset... | Chiede un nome e scrive un file `.slspreset` (strumento, sample, volume, pan, pitch, swing; non le note) in `~/Music/Sun Loops Studio/Presets`; compare subito nella cartella Presets del browser. |

Anche il doppio clic sull'anteprima delle note di un canale synth, o sul nome di un qualsiasi canale, apre il suo
Piano Roll. Sui canali sampler il doppio clic sugli step non ha un significato speciale: ogni clic attiva o
disattiva uno step.

## Piano Roll

Modifica le note del canale selezionato nel pattern corrente. Il titolo mostra il canale ("Piano Roll - Lead"). È
chiuso all'avvio e si apre da un canale, come sopra.

| Azione | Come |
| --- | --- |
| Suonare una nota | Pulsante sinistro su un tasto della tastiera (tenuto premuto = suona). |
| Aggiungere una nota | Clic su una cella vuota; tieni premuto e trascina a destra per scegliere la lunghezza. Suona finché tieni premuto. |
| Spostare una nota | Trascinala. |
| Cambiare la lunghezza | Trascina il bordo destro. |
| Eliminare una nota | Tasto destro sulla nota. |
| Scorrere in altezza | Rotella del mouse. |
| Scorrere nel tempo | Maiusc + rotella. |
| Zoom nel tempo | Ctrl + rotella. Finché non zoomi, tutto il pattern entra nella larghezza del pannello. |

**Suonare le note.** La tastiera a sinistra e le note che disegni si possono suonare:

- Premi il pulsante sinistro su un **tasto** e la nota suona finché tieni premuto; il tasto si illumina. Tenendo
  premuto, muoviti in su o in giù per suonare i tasti uno dopo l'altro (glissando).
- Quando **crei** una nota (clic su una cella vuota), la **sposti** o la **ridimensioni**, suona finché il pulsante è
  premuto. Spostandola su un'altra riga cambia la nota che senti. Il tasto destro non suona niente.
- Il suono è quello del canale selezionato: strumento o sample, volume, pan, **Pitch**, traccia del mixer ed effetti.
  Funziona sia con la riproduzione in corso sia ferma. Su un canale sampler ogni pressione suona il sample una volta,
  all'altezza del tasto.
- Se non senti nulla, controlla il pulsante **M** del canale, quello della traccia e il volume master.

Le note si agganciano ai sedicesimi. Un pattern dura due battute. Una nota synth termina alla sua lunghezza o alla
fine del pattern, quale viene prima. Quando apri un canale la vista scorre fino alle sue note.

## Pattern

Un **pattern** contiene le note di tutti i canali. Il menu dei pattern nella barra sceglie il pattern corrente: il
Channel Rack e il Piano Roll mostrano e modificano quello. Il pulsante **+** accanto offre **New empty pattern** e
**Clone current pattern**. Il progetto dimostrativo parte con "Pattern 1" (il groove completo) e "Drums" (solo la
batteria).

Le impostazioni del canale (volume, pan, mute, solo, pitch, swing, strumento, sample, traccia del mixer) appartengono
al canale, non al pattern: sono le stesse in ogni pattern. Le tracce del mixer e i loro effetti non appartengono né
all'uno né all'altro.

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

Premi **F9** o il pulsante **Mixer F9**. Il Mixer si apre grande sopra gli altri pannelli; F9 di nuovo lo chiude. Ha
8 **tracce** verticali, la strip **master** a destra e il **pannello effetti** della traccia selezionata.

Percorso del suono: ogni canale va alla traccia scelta con il suo display **Track N** (più canali possono condividerne
una), poi attraversa i quattro slot effetti della traccia, poi fader e bilanciamento della traccia, poi il master.

Ogni traccia, dall'alto in basso:

- il nome (Track 1...) e i canali che le sono assegnati;
- **quattro slot effetti**, uno sopra l'altro (vedi sotto);
- **bilanciamento** (stereo) - doppio clic per centrarlo. È un bilanciamento: al centro il suono non cambia;
- **M** (mute) e **S** (solo) - appartengono alla traccia e sono indipendenti dai pulsanti del canale nel rack. Se
  una traccia è in solo, si sentono solo quelle in solo;
- un **fader** verticale con la lettura in dB (dal silenzio a +3,5 dB, 1,0 = 0 dB; doppio clic per tornare a 0 dB);
- un **indicatore di livello**: la barra larga è il livello RMS (media su circa 300 ms), la barra stretta è il picco,
  con una linea di tenuta e una spia in alto che si accende se il segnale ha raggiunto o superato 0 dB. Clic
  sull'indicatore per spegnere la spia. La scala va da -60 a +6 dB.

Clic su una traccia per selezionarla: il pannello effetti mostra allora i suoi slot.

### Effetti

Clic su uno slot vuoto (o tasto destro su uno slot qualsiasi) per il menu: **Equalizer**, **Reverb**, **Delay**,
**Compressor**, **Bypass**, **Remove effect**. Gli effetti agiscono in ordine, dall'alto in basso, prima del fader
della traccia. Il pannello effetti ha un pulsante di accensione per slot (bypass) e un cursore per ogni parametro dello
slot selezionato.

| Effetto | Parametri |
| --- | --- |
| Equalizer | Shelf basso (120 Hz), Mid (una campana con frequenza propria, 200-5000 Hz), shelf alto (6 kHz); ciascuno da -15 a +15 dB |
| Reverb | Size, Damping, Width, Mix |
| Delay | Time (10-1000 ms), Feedback, Tone, Mix |
| Compressor | Threshold (da -60 a 0 dB), Ratio (1-20:1), Attack, Release, Makeup |

Sono effetti propri di Sun Loops Studio. **I plugin VST3 non si possono ancora caricare negli slot**: il menu dello slot
mostra una voce "VST3 plugin..." in grigio come promemoria.

Il volume master è il cursore della barra degli strumenti e il fader master è lo stesso controllo. La strip master ha un
indicatore e nessun effetto.

## Esportazione

**Export WAV...** scrive la modalità corrente: il pattern (due battute) o il brano (fino alla fine dell'ultima clip),
più un secondo di coda per non tagliare i rilasci e le code della batteria. Formato: WAV stereo a 24 bit, 48 kHz, al
BPM impostato nella barra. Il file è prodotto dallo stesso motore che suona, quindi suona come ciò che ascolti.

## Impostazioni audio

**Audio...** apre la scelta del dispositivo. Su Linux JUCE parla con ALSA o JACK; su Nobara i livelli di
compatibilità di PipeWire fanno funzionare entrambi. Se la riga di stato dice "No audio device", scegli un'uscita in
**Audio...** e controlla che il sistema abbia un'uscita disponibile. La latenza è quella dichiarata dal dispositivo:
il programma non la misura.

### Dimensione del buffer (gracchiare)

Il buffer audio è quanto suono il programma prepara in anticipo. **Un buffer più grande è più sicuro** (niente
gracchiare né interruzioni) ma aggiunge ritardo tra la pressione di una nota e il suono; uno più piccolo reagisce
prima ma richiede un computer più veloce e meno carico. Il programma parte con **1024 campioni** (circa 21-23 ms), una
scelta sicura per la maggior parte dei computer.

In **Audio...**, sotto *Buffer size*:

- **Il menu** imposta la dimensione che vuoi (da 64 a 8192 campioni, con il ritardo in millisecondi accanto a
  ciascuna). Si applica subito e la riproduzione continua. Se senti gracchiare, scegli la dimensione successiva. Se le
  note suonate nel piano roll ti sembrano in ritardo, scegline una più piccola.
- **Auto: set for this computer** serve quando non vuoi andare a tentativi. Suona il brano corrente (avviandolo se è
  fermo) a 256, 512, 1024, ... campioni, per circa due secondi e mezzo ciascuno, aggiungendo un carico extra a ogni
  blocco, così un computer con poco margine lo mostra. Per ogni dimensione guarda il conteggio dei blocchi persi
  riportato dal sistema audio e il tempo più lungo impiegato da un blocco. Si ferma alla prima dimensione pulita *la
  cui successiva è pulita a sua volta* e usa quella successiva: un passo di margine di sicurezza, misurato. La
  finestra elenca cosa è stato provato (per esempio `256 ok  512 ok`). Se anche la dimensione massima perde blocchi lo
  dice (il computer è molto occupato: chiudi altri programmi). **Cancel** (o la chiusura della finestra) rimette la
  dimensione precedente.
- **La lettura** mostra il carico del computer ora, il *busiest block* (quanta parte del suo tempo ha usato il blocco
  più lento: 1 % è molto comodo, oltre il 50 % è troppo stretto) e il numero di **blocchi persi** dall'ultima modifica
  del buffer. È un modo dal vivo per vedere se una dimensione funziona: fai suonare il brano e guarda.

Dispositivo e dimensione del buffer vengono ricordati al prossimo avvio. Con JACK il buffer si imposta nel server JACK,
quindi menu e Auto non sono disponibili. Auto è una misura sul tuo computer in quel momento: un desktop molto
occupato più tardi, un brano più pesante o un mixer pieno di effetti possono richiedere comunque un buffer più grande.

## Limiti

- Fino a 8 canali, 8 tracce del mixer con 4 slot effetti ciascuna, 64 pattern, 8 tracce di playlist, una linea del
  tempo lunga 32 pattern, 32 voci simultanee.
- Gli effetti usano circa 16 MB di memoria a 48 kHz (di più a frequenze di campionamento maggiori).
- Un pattern dura sempre due battute, 4/4, griglia di sedicesimi.
- I sample sono mono o stereo, al massimo 30 secondi, suonati una volta sola (niente loop né editing
  dell'inviluppo).
- Non ancora disponibili: suonare le note con la tastiera del computer o con una tastiera MIDI, annulla, incorporare i sample nel progetto, salvataggio automatico e recupero dopo un crash, plugin VST come strumenti o effetti, send ed effetti sul master, automazione, ingresso e
  registrazione MIDI, eliminare o rinominare i pattern, spostare il cursore con un clic sul righello, trascinare file
  dal desktop sul programma. Vedi [roadmap.md](roadmap.md) (in inglese).
