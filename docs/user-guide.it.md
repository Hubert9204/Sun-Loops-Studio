# Guida all'uso

*[English](user-guide.md)*

Questa guida descrive cosa fa oggi Sun Loops Studio 0.6. Limiti e funzioni mancanti sono elencati alla fine.

## La finestra

Una **barra degli strumenti** su due righe, una riga di stato e sotto il **browser** (barra laterale a sinistra) e
l'**area di lavoro**, dove i pannelli fluttuano.

Barra, prima riga: **File** (progetti, vedi sotto), **Play / Stop**, pulsante **Pattern / Song**, il **menu dei pattern** con **+**, **BPM**
(40-240), volume **Master** con il suo indicatore, il pulsante **Metronome**, **Record** (con una freccia per le opzioni) e **Audio...**
(impostazioni del dispositivo). L'esportazione è nel menu **File**.

Barra, seconda riga: i pulsanti che mostrano o nascondono il browser (**Browser F8**) e i pannelli (**Rack F6**,
**Playlist F5**, **Piano F7**, **Mixer F9**), **Reset** (disposizione predefinita dei pannelli) e **Clear notes** (svuota il canale selezionato nel
pattern corrente).

La riga di stato mostra il dispositivo audio (frequenza, buffer, latenza dichiarata) e l'ultimo messaggio.

## Progetti

Tutto il brano è salvato in un unico file `.slp` (le versioni prima della 0.8 scrivevano `.slsproj`; quei file si aprono ancora): canali (strumento, sample, volume, pan, pitch, swing, traccia
del mixer), tutti i pattern con le loro note, la playlist, il mixer (fader, bilanciamento, mute/solo, catene di
effetti e loro impostazioni), le clip audio della playlist (come percorsi dei loro file), BPM, volume master e se la modalità Song è attiva. Il titolo della finestra mostra il
nome del progetto, seguito da `*` quando ci sono modifiche non salvate.

| Comando | Tasto | Cosa fa |
| --- | --- | --- |
| **New** | `Ctrl+N` | Parte con un progetto vuoto (un canale synth, un pattern vuoto). |
| **Open...** | `Ctrl+O` | Sceglie un file `.slp` (o `.slsproj` vecchio). |
| **Open recent** | | Gli ultimi otto progetti che esistono ancora. |
| **Save** | `Ctrl+S` | Scrive il progetto. |
| **Save as...** | `Ctrl+Maiusc+S` | Chiede un nome (se manca, aggiunge `.slp`) e scrive il progetto lì. Un vecchio `.slsproj` viene proposto come `.slp`. |
| **Import audio or video into the playlist...** | | Sceglie file audio o video e li mette nella playlist (vedi *Clip audio*). |
| **Export audio** | | Un sottomenu con i formati (vedi *Esportazione*). |
| **Show the projects folder** | | Apre `~/Music/Sun Loops Studio/Projects`. |

**Salvare senza nome.** Un progetto a cui non hai ancora dato un nome si chiama *Untitled*. **Save** (o `Ctrl+S`)
non si ferma a chiedere: scrive subito il progetto come `Untitled <data> <ora>.slp` in
`~/Music/Sun Loops Studio/Projects`, e da quel momento quello è il file del progetto, quindi il **Save** successivo
aggiorna lo stesso file. Usa **Save as...** quando vuoi un nome vero. Salvare un progetto con nome è sicuro: il
nuovo testo va in un file temporaneo che poi sostituisce il vecchio, quindi un salvataggio fallito lascia intatto il
file precedente.

**Lavoro non salvato.** **New**, **Open...** e la chiusura della finestra chiedono conferma se ci sono modifiche
non salvate: *Save*, *Don't save* o *Cancel*. Il tasto Esc annulla. Scegliere un pattern o passare da Pattern a Song
non conta come modifica. Puoi anche avviare il programma con un progetto come argomento
(`sun-loops-studio brano.slp`); aprire un file `.slp` dal file manager fa lo stesso quando il desktop
conosce il tipo (installato da `cmake --install`).

**I sample e le clip audio non sono copiati nel file.** Un canale sampler o una clip audio ricorda da dove viene il suo audio: un suono incluso o il
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

*(Novità della 0.9.0: **Samples > Cyber-Hub** e **Drum Kits > Cyber-Hub Kit** contengono i suoni di sistema incorporati di Hubert, e all'apertura il programma suona un suono di avvio: si disattiva in **File > Play the start-up sound**. Vedi [cyber-hub.md](cyber-hub.md), in inglese.)*

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
| Assign free mixer track | Apre una finestra con le 8 tracce del mixer: quelle libere (senza strumenti) in verde, le altre con gli strumenti che ci sono sopra, e quella attuale segnata. La prima traccia libera è già selezionata; **Select first free track** la riseleziona, oppure scegli una traccia qualsiasi (condividerla è permesso), poi **Assign** (o Invio; Esc annulla). Il display **Track N** del canale e il mixer si aggiornano, e il mixer seleziona quella traccia. |
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
**Clone current pattern**. I nuovi pattern prendono da soli il nome successivo (Pattern 3, Pattern 4, ...). Il progetto dimostrativo parte con "Pattern 1" (il groove completo) e "Drums" (solo la
batteria), suonati con i suoni Cyber-Hub (vedi [cyber-hub.md](cyber-hub.md), in inglese).

Il **menu dei pattern** in cima alla Playlist ha tre pulsanti: **New** (vuoto o copia, come il **+**), **Rename** (un nome di
al massimo 40 caratteri) e **Delete**. Delete elimina il pattern selezionato, le sue note e **tutte le clip della playlist che lo
suonano**; l'ultimo pattern rimasto non si può eliminare. Scegliere un pattern in quel menu lo apre anche nel Rack e nel Piano Roll.

Le impostazioni del canale (volume, pan, mute, solo, pitch, swing, strumento, sample, traccia del mixer) appartengono
al canale, non al pattern: sono le stesse in ogni pattern. Le tracce del mixer e i loro effetti non appartengono né
all'uno né all'altro.

## Playlist

La Playlist dispone i pattern nel tempo. Ha 8 tracce e una linea del tempo lunga 32 pattern. Il righello mostra i
numeri di battuta (un pattern dura due battute, quindi i numeri vanno 1, 3, 5, ...). La linea verticale arancione
segna la fine del brano: in modalità Song la riproduzione ricomincia da lì.

| Azione | Come |
| --- | --- |
| Piazzare una clip (strumento Select) | Clic su una cella vuota. La clip contiene il pattern scelto nel menu ed è lunga un pattern. Trascina subito a destra per allungarla. |
| Spostare una clip | Trascinala (si aggancia ai confini dei pattern e può cambiare traccia). |
| Ripetere / accorciare | Trascina il bordo destro della clip: il pattern si ripete una volta per ogni lunghezza di pattern. |
| Eliminare | Tasto destro sulla clip > Delete, oppure selezionala e premi Canc (o usa lo strumento Erase). |
| Selezionare il suo pattern | Doppio clic sulla clip. |
| Scorrere nel tempo | Rotella del mouse. |
| Zoom | Ctrl + rotella. |

### Strumenti, griglia, zoom e annulla

La barra sotto il menu dei pattern ha cinque strumenti (tasti **1** - **5** con il mouse sopra la linea del tempo):

| Strumento | Cosa fa |
| --- | --- |
| **Select** (1) | Lo strumento normale: clic su una clip per selezionarla, trascina per spostarla, trascina il **bordo sinistro o destro** per accorciarla o allungarla (una clip audio mantiene il suono al suo posto: se ne sente solo di più o di meno), tasto destro per un menu (*Split here, Duplicate, Copy, Delete*, più *Position, length and gain...* su una clip audio oppure *Open this pattern* su una clip di pattern), doppio clic su una clip di pattern per aprirla. |
| **Split** (2) | Clic su una clip per tagliarla in due in quel punto (agganciato alla griglia). Il tasto **S** divide la clip selezionata dove si trova il mouse. |
| **Cut** (3) | Trascina su una clip per togliere quel tratto: il suono dopo il vuoto resta dov'era. |
| **Erase** (4) | Clic su una clip per eliminarla, o trascina su più clip. |
| **Zoom** (5) | Il clic ingrandisce, il tasto destro o Maiusc+clic rimpicciolisce, trascinando un tratto si ingrandisce su quel tratto. |

**Grid** (il menu accanto agli strumenti) è la griglia a cui si agganciano le clip audio quando vengono spostate, ritagliate, divise
o tagliate: da una battuta fino al singolo tick (1 battito = 480 tick); tieni premuto **Alt** mentre trascini per ignorarla. Le clip
di pattern stanno sempre su pattern interi. **-**, **+** e **Fit** rimpiccioliscono, ingrandiscono e mostrano tutto il brano;
**Ctrl+rotella** ingrandisce intorno al mouse, la rotella e la barra di scorrimento muovono nel tempo. Il righello mostra
**minuti:secondi.millisecondi** e i numeri di battuta (solo quanti ne stanno allo zoom attuale); le due righe sotto la linea del
tempo indicano, per la posizione sotto il mouse, *tempo - battuta:battito:tick* e, per la clip selezionata, nome, inizio, durata e fine
(in battuta:battito:tick e in tempo), per una clip audio anche durata e frequenza di campionamento del file, il punto del file da cui
parte la clip (in secondi e in campioni) e il guadagno. Su una clip audio, *Position, length and gain...* (tasto destro) permette di
scrivere valori esatti.

**Annulla / Ripristina** (pulsanti, **Ctrl+Z** e **Ctrl+Y**) coprono tutto ciò che si fa alle clip nella Playlist. **Ctrl+C / X / V**
copiano, tagliano e incollano la clip selezionata dove sta il mouse, **Ctrl+D** la duplica, **Canc** la elimina. La cronologia viene
dimenticata quando qualcosa fuori dalla Playlist cambia la disposizione (apertura di un progetto, una nuova registrazione,
l'eliminazione di un pattern). Split e Cut funzionano anche sulle clip di pattern, ma solo su lunghezze intere di pattern. Una clip
ottenuta dividendo una clip audio ricorda dove inizia nel suono **in secondi, al BPM a cui è stata tagliata**: se in seguito cambi il
BPM il suono continua a suonare alla velocità originale, quindi i punti di taglio non cadono più sul battito su cui erano stati fatti.

Le clip non possono sovrapporsi sulla stessa traccia: uno spostamento o un ridimensionamento che si sovrapporrebbe
semplicemente non viene applicato. Le clip su tracce diverse suonano insieme. Un clic sul **nome** di una traccia sceglie la
traccia in cui scrive **Record** (segnata da un punto rosso).

### Clip audio

Le clip audio sono registrazioni e file audio messi nella playlist. Sono disegnate in verde acqua con la forma d'onda.

| Azione | Come |
| --- | --- |
| Aggiungere un file audio o video | Trascinalo dal file manager sulla Playlist (cade sulla traccia e sul battito sotto il mouse, che viene evidenziato), oppure usa *File > Import audio or video into the playlist...*. Più file vanno su tracce consecutive. |
| Spostare | Trascina la clip (si aggancia alla griglia scelta in **Grid** e può cambiare traccia). |
| Accorciare / allungare | Trascina il bordo destro o sinistro: al massimo la durata dell'audio. |
| Eliminare | Tasto destro sulla clip > Delete, oppure selezionala e premi Canc. |

File supportati: `.wav`, `.aiff` / `.aif`, `.flac`, `.ogg`, `.oga` e `.mp3` sono letti dal programma stesso. `.aac` /
`.m4a` e la **traccia audio di un video** (`.mp4`, `.m4v`, `.mkv`, `.mov`, `.avi`, `.webm`, `.wmv`, `.flv`, `.mpg`, `.mpeg`,
`.ogv`, `.3gp`) richiedono il programma **ffmpeg**: Nobara di solito ce l'ha; altrimenti installalo con
`sudo dnf install ffmpeg` (su Fedora normale è il pacchetto di RPM Fusion; il pacchetto `ffmpeg-free` potrebbe non avere
l'encoder MP3). L'audio di un video viene estratto in `~/Music/Sun Loops Studio/Samples/From videos/<nome>.wav` (senza mai
sostituire un file) e la clip usa quel WAV. La decodifica avviene in background; la riga di stato dice quando le clip sono
dentro. Un file più lungo di 5 minuti viene tagliato a 5 minuti (con un messaggio) e una clip più lunga dello spazio libero
viene tagliata per entrarci.

Una clip audio suona in **modalità Song** (il programma ci passa quando aggiungi la prima clip) attraverso la traccia del
mixer con lo stesso numero della sua traccia di playlist: la **Track 3** della playlist suona sulla **traccia 3 del mixer**,
quindi valgono il suo fader, mute, solo ed effetti, ed è inclusa in ogni esportazione. Una clip dura tanti battiti quanti
ne dura l'audio al tempo in cui è stata aggiunta; se poi cambi il BPM il suono continua a suonare alla sua velocità
originale, quindi la clip può finire prima o dopo l'audio (la forma d'onda mostra dove il suono finisce davvero). Se il file
viene poi spostato o cancellato, il progetto si apre con una clip muta segnata *(file not found)* che conserva il percorso.

### Registrazione

**Record** (barra) registra l'**ingresso audio**: un microfono, una chitarra in un'interfaccia audio, un ingresso di linea.

1. In **Audio...** scegli il dispositivo di ingresso e attiva i suoi canali; parla o suona e guarda la barra **Input
   level**. (Se salti questo passo, premendo Record il programma attiva da solo i primi canali di ingresso del dispositivo.)
2. Nella Playlist clicca sul **nome di una traccia vuota**: ottiene un punto rosso. La freccia accanto a Record offre la
   stessa scelta, più il tipo di ingresso: **Mono** (il primo ingresso, scritto come file a un canale: usalo per un
   microfono o una chitarra) o **Stereo** (ingressi 1 e 2), e *Hear the input while recording* (usa le cuffie: con casse e
   microfono si crea un fischio; questo ascolto aggiunge la latenza del dispositivo).
3. Premi **Record**. Il brano parte dall'inizio, in modalità Song, e la riga di stato mostra il tempo e il livello
   d'ingresso (avvisa se satura). Premi di nuovo **Record** o **Stop** per finire; la registrazione compare come clip audio
   all'inizio della traccia ed è conservata anche come WAV a 24 bit in `~/Music/Sun Loops Studio/Samples/Recordings/`.

La registrazione è spostata della latenza di andata e ritorno dichiarata dal dispositivo audio, per allinearla a ciò che
suonava. La traccia deve essere libera all'inizio della playlist (altrimenti Record lo dice); una registrazione si ferma da
sola alla fine della playlist o dopo 5 minuti. Le registrazioni partono sempre dall'inizio del brano: non c'è ancora
punch-in né conteggio iniziale (accendi il **Metronome** per suonare a tempo).

### Tastiere MIDI e altri dispositivi

**Audio...** elenca anche gli **ingressi MIDI**. Spunta le tastiere che vuoi: suonano il **canale selezionato** del Channel
Rack (clicca un canale per selezionarlo) con la dinamica dei tasti, sia che il brano suoni o no. Una tastiera collegata mentre
il programma è in esecuzione viene attivata da sola; la tua scelta viene ricordata. La riga di stato nomina gli ingressi MIDI
attivi e dice quale canale suona. Si usano solo le note (niente pitch bend, pedali o manopole) e il MIDI serve a suonare: le note si
possono registrare nel piano roll con **Rec notes** (sotto).

### Registrare note nel piano roll (Rec notes)

**Rec notes** (barra) scrive nel pattern ciò che suona una tastiera MIDI, così puoi costruire pattern suonando:

1. Seleziona il canale in cui registrare (clic nel Channel Rack) e controlla che la tastiera sia spuntata in **Audio...**.
2. Clicca **Rec notes** (diventa rosso) e premi **Play**. La registrazione funziona solo mentre il brano suona, in modalità Pattern
   (il pattern corrente va in loop) o Song.
3. Suona. Ogni nota va nel **pattern corrente** del **canale selezionato**, nel punto in cui l'hai sentita (la latenza d'uscita viene
   sottratta), con la dinamica del tasto. Una nota tenuta oltre la fine del pattern viene divisa e continua dall'inizio; una nota
   ancora tenuta quando premi Stop finisce lì. Le note compaiono subito nel Piano Roll e si modificano come le altre.

Tasto destro su **Rec notes** per scegliere la **quantizzazione** (1/4, 1/8, **1/16** di default, 1/32 o nessuna). Le note già presenti
nello stesso punto e con la stessa altezza non vengono duplicate, quindi registrando più volte sul loop si aggiungono solo note nuove.
L'altezza di una chitarra o di una voce **non** viene trasformata in note: per l'audio usa *Record* (sopra).

La freccia accanto a **Record** ha *The recording becomes*: **A clip on a playlist track** (predefinito) oppure **A new sampler
channel**. Con la seconda, una registrazione da microfono o chitarra diventa il sample di un nuovo canale, pronto da suonare con una
tastiera MIDI o da sequenziare nel piano roll (senza canali liberi viene messa nella playlist).

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
una; oppure usa *Assign free mixer track* nel menu del tasto destro del canale), poi attraversa i quattro slot effetti
della traccia, poi fader e bilanciamento della traccia; la somma di tutte le tracce passa poi dai quattro slot effetti
del master e dal fader master.

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

### Effetti sul master

La **strip master** (a destra delle tracce) ha gli stessi quattro slot effetti di una traccia. Clicca uno slot per
scegliere un effetto (Equalizer, Reverb, Delay, Compressor); cliccando la strip o uno slot lo vedi nel pannello
effetti, intitolato *Effects - Master*. La catena master elabora la **somma di tutte le tracce**, in ordine, **prima
del fader master** e del limitatore morbido finale, quindi un compressore o un equalizzatore lì modella tutto il mix,
e il WAV esportato lo include. Serve per compressione di insieme, un EQ finale o un filo di riverbero su tutto. Gli
slot si possono escludere (bypass) o rimuovere come quelli di una traccia, e la catena viene salvata nel progetto.

Il volume master è il cursore della barra degli strumenti e il fader master è lo stesso controllo. La strip master ha un
indicatore.

## Esportazione

## Metronomo

Il pulsante **Metronome** accende o spegne un click. Tasto destro per un piccolo pannello con il **tempo** (lo stesso BPM
della barra), il **volume** e il pulsante **Tap tempo**: battilo a tempo con un brano (quattro o più colpi) e il BPM lo
segue. Il click è più alto e più forte sul primo battito di ogni battuta e più basso sugli altri tre. Mentre il brano
suona segue il brano; da fermo tiene il tempo da solo al BPM, così puoi esercitarti o registrare. Il metronomo non fa mai
parte di un'esportazione. Viene ricordato tra una sessione e l'altra.

## Esportazione

**File > Export audio** scrive la modalità corrente: il pattern (due battute) o il brano (fino alla fine dell'ultima clip,
clip audio comprese), più un secondo di coda per non tagliare i rilasci e le code della batteria. Viene generato a 48 kHz
stereo, al BPM impostato nella barra, dallo stesso motore che suona, quindi suona come ciò che ascolti. Scegli il formato
nel sottomenu:

| Formato | Tipo | Note |
| --- | --- | --- |
| WAV `.wav` | non compresso | 24 bit |
| AIFF `.aiff` | non compresso | 24 bit |
| FLAC `.flac` | senza perdita | 24 bit |
| MP3 `.mp3` | con perdita | 192 kbit/s, richiede `ffmpeg` |
| Ogg Vorbis `.ogg` | con perdita | circa 192 kbit/s |
| Ogg audio `.oga` | con perdita | lo stesso audio Vorbis con il nome `.oga` |
| AAC `.aac` | con perdita | 192 kbit/s, un flusso AAC semplice, richiede `ffmpeg` |

Le voci che richiedono `ffmpeg` lo dicono nel menu quando non è installato. Il file viene scritto con un nome temporaneo e
spostato a posto alla fine, quindi un'esportazione fallita non rovina mai un file che c'era già.

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
- Gli effetti usano circa 18 MB di memoria a 48 kHz (di più a frequenze di campionamento maggiori).
- Un pattern dura sempre due battute, 4/4, griglia di sedicesimi.
- I sample sono mono o stereo, al massimo 30 secondi, suonati una volta sola (niente loop né editing
  dell'inviluppo). Clip audio: fino a 5 minuti per file, 64 clip.
- Non ancora disponibili: suonare le note con la tastiera del computer, annulla fuori dalla Playlist, incorporare i sample nel progetto,
  salvataggio automatico e recupero dopo un crash, plugin VST come strumenti o effetti, send, automazione, trasformare l'altezza di
  chitarra o voce in note, punch-in e conteggio iniziale per la registrazione, dissolvenze ed effetti per clip, spostare il cursore con un clic sul righello. Vedi [roadmap.md](roadmap.md) (in inglese).
