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

**Il brano dimostrativo (0.52.0).** Al primo avvio il programma si apre con "Sunrise Drive" (dalla 0.53.0 solo allora; gli avvii
successivi partono da un progetto nuovo e vuoto), 44 battute di synthwave in La minore a 110 BPM, in
modalità Song: premi **Spazio** per ascoltarlo. È fatto per mostrare il programma al lavoro: apri i pannelli mentre suona.

| Battute | Sezione | Cosa suona |
| --- | --- | --- |
| 1-4 | Intro | il suono di avvio come clip audio, pianoforte a coda, "Warm Pad" di SunBurst con il filtro che si apre (automazione) |
| 5-12 | Strofa | la drum machine (Electronic Punch), il basso elettrico campionato, il piano, una marimba (modello fisico) che si sposta a destra e a sinistra (automazione del pan), campane FM |
| 13-16 | Crescendo | un rullo di rullante, un piatto al contrario (clip audio, reverse, fade-in) e un tape stop sulla batteria (automazione del Time Shaper) |
| 17-24 | Ritornello | "Sun Lead" di SunBurst, "Chrome Bass" di HexForge con il filtro che oscilla (automazione del plugin), il pad che pompa con la batteria (sidechain) |
| 25-28 | Pausa | un coro additivo e archi sottrattivi, il passa-basso del master che si apre (automazione di un effetto del master), la mandata al riverbero del piano che cresce |
| 29-32 | Crescendo | il secondo riser, lo stesso piatto abbassato di una terza minore con il time-stretch |
| 33-40 | Ultimo ritornello | tutto, più gli arpeggi di "Neon Pluck" di HexForge e il bus del delay che sale (automazione dell'aux) |
| 41-44 | Finale | il gruppo VCA dei synth che sfuma, il tempo che rallenta da 110 a 80 BPM (marcatori di tempo) |

Ogni canale ha la sua traccia del mixer con effetti in insert (EQ, compressore, transient, chorus, phaser, delay, overdrive, filtro,
stereo imager, Time Shaper); l'aux A è un riverbero hall e l'aux B un delay ping-pong; il master ha un filtro, un EQ e il maximizer;
due gruppi VCA raccolgono i synth e batteria e basso. Senza SunBurst e HexForge (una build senza di loro) i loro quattro canali suonano
synth incorporati. *File > Open the demo song* lo riapre in qualsiasi momento; non viene salvato da nessuna parte finché non lo salvi tu.

Tutto il brano è salvato in un unico file `.slp`: canali (strumento, sample, volume, pan, pitch, swing, traccia
del mixer), tutti i pattern con le loro note, la playlist, il mixer (fader, bilanciamento, mute/solo, catene di
effetti e loro impostazioni), le clip audio della playlist (come percorsi dei loro file), BPM, volume master e se la modalità Song è attiva. Il titolo della finestra mostra il
nome del progetto, seguito da `*` quando ci sono modifiche non salvate.

| Comando | Tasto | Cosa fa |
| --- | --- | --- |
| **New** | `Ctrl+N` | Parte con un progetto vuoto (un canale synth, un pattern vuoto). |
| **Open...** | `Ctrl+O` | Sceglie un file `.slp`. |
| **Open recent** | | Gli ultimi otto progetti che esistono ancora. |
| **Save** | `Ctrl+S` | Scrive il progetto. |
| **Save as...** | `Ctrl+Maiusc+S` | Chiede un nome (se manca, aggiunge `.slp`) e scrive il progetto lì. |
| **Export project with samples...** | | Chiede il nome di una nuova cartella e ci scrive il progetto insieme a una copia di ogni suono che usa (vedi sotto). |
| **Undo / Redo** | `Ctrl+Z` / `Ctrl+Y` | Annulla o ripristina l'ultima modifica, di qualunque tipo (vedi *Annulla e ripristina*). |
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

**Portare un progetto su un altro computer.** *File > Export project with samples...* chiede il nome di una nuova cartella (non deve esistere
ancora, o deve essere vuota) e ci scrive `<nome>.slp` più una cartella `Samples` con una copia di ogni suono usato dal progetto: canali sampler,
layer dei kit di batteria e clip audio. Il progetto si riferisce alle copie con **percorsi relativi**, quindi l'intera cartella si può spostare,
comprimere o copiare su un altro computer e lì si apre con i suoi suoni. I suoni inclusi restano inclusi; un file che non si trova viene elencato e
mantiene il vecchio percorso.

**Copia di sicurezza, salvataggio automatico e recupero.** Salvando sopra un progetto esistente, la versione precedente resta accanto come
`<nome>.slp.bak` (una sola generazione). Finché ci sono modifiche non salvate, il progetto viene anche scritto **una volta al minuto** in
`~/Music/Sun Loops Studio/Autosave/Autosave.slp`. Quel file viene cancellato quando il programma si chiude normalmente; se il programma si è
bloccato (o è stato terminato), al prossimo avvio compare **Recover your work?** (dopo la domanda sul report del crash, vedi *Report dei crash* sotto) e *Open the recovered project* lo apre (segnato come non salvato,
con il vecchio nome se ne aveva uno: `Ctrl+S` lo rimette al suo posto).

**File danneggiati.** Un file che non è un progetto di Sun Loops Studio, è rovinato o è stato scritto da una versione
più recente viene rifiutato con un messaggio e il progetto corrente resta intatto. I valori fuori limite vengono
riportati nei limiti e le note o clip che non si possono collocare vengono scartate, con un messaggio.

## Report dei crash, log di sessione e aggiornamenti (0.43.0)

**Log di sessione.** Ogni sessione scrive un log in `~/.local/state/SunLoopsStudio/logs/session.log` (o `$XDG_STATE_HOME/SunLoopsStudio`):
la versione e il sistema all'avvio, il dispositivo audio, ogni messaggio della riga di stato, i problemi e il controllo degli
aggiornamenti. Quando il file arriva a **5 MB** diventa `session.1.log` (poi `session.2.log`, `session.3.log`; i più vecchi vengono
eliminati). *File > Updates and crash reports > Show the session log folder* apre la cartella.

**Quando il programma va in crash.** Se Sun Loops Studio viene chiuso da un crash (SIGSEGV, SIGABRT, SIGFPE, SIGILL o SIGBUS, o un
errore non gestito), prima di chiudersi scrive cosa è successo in `crash_last_session.log` nella stessa cartella: il segnale, lo stack
trace (le funzioni in esecuzione), la versione, il sistema e driver, dispositivo, frequenza di campionamento e buffer audio. Al
prossimo avvio chiede:

> *Sun Loops Studio si è chiuso in modo anomalo durante l'ultima sessione. Vuoi inviare un report anonimo del bug su GitHub per aiutarci a risolverlo?*

(in italiano se la lingua del sistema è l'italiano, altrimenti in inglese)

| Pulsante | Cosa fa |
| --- | --- |
| **Invia Report** | Crea una issue nel repository GitHub del progetto (Hubert9204/Sun-Loops-Studio) con il crash, il sistema e lo stack trace in blocchi di codice Markdown, e ne mostra il numero. Il file del crash viene spostato in `crashes/` solo dopo la conferma di GitHub; se l'invio non riesce, il motivo viene mostrato e il file resta. |
| **Mostra Log prima di inviare** | Mostra tutto il report esattamente come verrà inviato, poi Invia o Ignora. |
| **Ignora/Elimina** | Elimina il file del crash; non viene inviato nulla. |

Chiudere la finestra conserva il file: la domanda torna al prossimo avvio. Poi segue come prima **Recover your work?**.

**Cosa viene inviato e cosa no.** Il report contiene il segnale, lo stack trace, la versione del programma, il sistema operativo,
il kernel e il processore, driver audio, nome del dispositivo, frequenza di campionamento e dimensione del buffer, e le ultime 40
righe del log di sessione prima del crash. La tua cartella home viene sostituita da `~` e il tuo nome utente da `<user>`. Non vengono
inviati progetti, suoni o file, e non viene inviato nulla senza il tuo clic.

**Il token GitHub.** Creare una issue con l'API di GitHub richiede un token. *File > Updates and crash reports > GitHub token for crash
reports...* ne accetta uno (un token fine-grained con *Issues: Read and write* solo sul repository); viene conservato nel file delle
impostazioni, leggibile solo da te. Senza token, **Invia Report** apre nel browser la pagina *new issue* di GitHub con il report già
compilato (serve un account GitHub): premi lì *Submit new issue*, poi nel programma *L'ho inviato: elimina il file*.

**Controllo degli aggiornamenti.** Qualche secondo dopo l'avvio il programma chiede a GitHub, in background, l'ultima release
(`releases/latest`, al massimo 5 secondi; senza rete o senza risposta semplicemente non succede nulla). Se esiste una versione più
recente (confronto Semantic Versioning: 0.43.0 è più recente di 0.42.0, 1.0.0 di 1.0.0-rc.1), una finestra la mostra con l'inizio
delle note: **Download** apre l'AppImage (o la pagina della release), **Later** richiede al prossimo avvio, **Skip this version** non
chiede più di quella. *Check for updates at start-up* nello stesso menu spegne il controllo automatico; *Check for updates now* chiede
subito e dice anche quando sei aggiornato. Il controllo non invia dati personali (solo nome e versione del programma, come
User-Agent richiesto da GitHub). Le release sono pubblicate dalla 0.51.0 in poi, quindi il controllo trova ogni versione successiva.

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

## Helios: chiedere aiuto (0.40.0)

Premi **F1** o il pulsante **Helios** e scrivi cosa vuoi fare, in italiano o in inglese ("come aggiungo un riverbero?", "how do I
record two microphones?"). Helios cerca in **questo manuale** e tra i **comandi** del programma, sul tuo computer: niente IA, niente
internet, non viene inviato nulla.

- I risultati con **Do:** sono comandi (aprire il Mixer, registrare, esportare, aprire un progetto FL Studio, tagliare un loop...):
  doppio clic, **Invio** o **Do it** e Helios lo fa.
- Gli altri sono sezioni del manuale (in italiano, segnate *(italiano)*, o in inglese): clicca per leggerle a destra.

Come trova le cose: cerca le parole della domanda (senza accenti, plurali e desinenze comuni, ignorando parole come "come", "un",
"how", "the"), dà più peso ai titoli, capisce alcuni nomi di FL Studio (insert = mixer, Slicex = Slicer, Newtone = editor di
intonazione, VST = plugin) e preferisce il manuale nella lingua della domanda. Non capisce le frasi: se non esce nulla di utile, prova
con meno parole o con altre.

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

### Suonare con la tastiera del computer (Typing keys)

Con **Typing keys** acceso (il pulsante nella barra, acceso all'inizio; la scelta viene ricordata), la tastiera del computer
suona il **canale selezionato** come una tastiera MIDI (0.32.0), che il brano suoni o no; **Rec notes** la registra e lo
score log la conserva.

```
  W E   T Y U   O P          C#  D#    F#  G#  A#    C#  D#
 A S D F G H J K L ò à       C  D  E  F  G  A  B  C  D  E  F
```

**A** suona **C5** (la nota MIDI 60). **Z** / **X** spostano la tastiera di un'ottava in giù / in su, **C** / **V** rendono
le note più piano / più forte (velocity 1, 20, 40, 60, 80, 100, 110, 127; all'inizio 100); la riga di stato dice dov'è la A e
la velocity. Sulla tastiera italiana **ò** e **à** suonano Mi e Fa (sulle tastiere americane sono ; e '); contano le lettere,
quindi su una tastiera tedesca Z e Y si scambiano di posto. I tasti funzionano quando nessun campo di testo ha la tastiera; con
Ctrl o Alt premuti mantengono il loro significato abituale, e con il mouse sopra la Playlist vengono prima i suoi tasti (S divide,
da 1 a 5 scelgono uno strumento). I nomi delle note seguono FL Studio: il Do centrale (MIDI 60)
è **C5**.

## Browser

*(Novità della 0.22.0: **Drum Kits** elenca tutti i kit incorporati (ogni pad si può trascinare) e **Presets > Factory** è diviso in **Keys**
(il Grand Piano), **Bass** (l'Electric Bass e la sua versione staccato), **Synth** e **Drums**: trascina un preset su un canale o sulla
parte vuota del rack. I suoni Cyber-Hub delle versioni precedenti sono stati tolti: un progetto che li usava si apre con quei suoni
muti e un messaggio. All'apertura il programma suona ancora il suono di avvio: si disattiva in **File > Play the start-up sound**.)*

La barra laterale a sinistra elenca i tuoi suoni come un albero. Clic sulla freccia di una cartella per aprirla.

| Cartella | Contenuto |
| --- | --- |
| Samples | I suoni incorporati (cassa, rullante, hi-hat), poi i file audio (WAV, FLAC, OGG, AIFF) trovati in `~/Music/Sun Loops Studio/Samples` e nelle sue sottocartelle. |
| Drum Kits | Un kit incorporato, poi una voce per ogni sottocartella di `~/Music/Sun Loops Studio/Drum Kits`. |
| Instruments | *Synth* e *Sampler*. |
| Presets | I preset di canale di fabbrica (sei di base e, dalla 0.18.0, i suoni del synth sottrattivo), poi i tuoi file `.slspreset` da `~/Music/Sun Loops Studio/Presets`. |
| Plugin | *Scan for plugins (VST3, LV2, LADSPA)...* cerca nelle cartelle abituali dei plugin in background; l'elenco resta per l'avvio successivo. I plugin trovati sono divisi in **Instruments** ed **Effects** (vedi *Plugin* più sotto). |

Le tre cartelle in `~/Music/Sun Loops Studio` vengono create all'avvio del programma. **Folder** apre quella posizione
nel file manager; **Refresh** cerca file nuovi (il browser non controlla il disco in continuazione).

- **Ricerca**: scrivi nella casella in alto per filtrare tutte le cartelle insieme; le cartelle con risultati si
  aprono da sole e la riga sotto l'albero conta i risultati. **Esc** cancella la ricerca.
- **Trascinamento**: trascina un suono su un canale del Channel Rack (la riga si evidenzia con un contorno verde) per
  caricarlo lì; rilascialo nello spazio vuoto sotto le righe per creare un nuovo canale. Un preset imposta strumento,
  sample, volume, pan, pitch e swing del canale; una voce di *Instruments* cambia lo strumento del canale.
- **Un clic** su un suono (un file audio o un suono incorporato) lo fa ascoltare una volta, che il brano suoni o no
  (0.31.0); un altro clic lo riascolta dall'inizio, un clic su un altro suono fa ascoltare quello. Dei file lunghi si
  ascoltano i primi 30 secondi.
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
- **Track N** è la traccia del mixer attraverso cui suona il canale. Clic per un menu delle tracce del mixer (a gruppi di 50 quando sono tante), oppure
  trascinalo su e giù, o usa la rotella sopra di esso. All'inizio il canale 1 va alla Track 1, il canale 2 alla
  Track 2 e così via.
- **L'area degli step** (0.31.0, come in FL Studio) mostra i **quadratini** dello step sequencer oppure
  un'**anteprima del Piano Roll**, a seconda delle note stesse:
  - Ogni nuovo canale parte con i quadratini, uno per sedicesimo del pattern. **Clic sinistro** (o trascinamento)
    scrive gli step, **clic destro** (o trascinamento) li cancella. Uno step suona la nota base del canale (la nota MIDI
    60, salvo un'altra impostazione *Root* del sampler), o la cassa (pad 1) su un canale drum. Durante la riproduzione si illumina lo
    step in corso.
  - **La griglia è esattamente il pattern** (0.42.0). Un progetto nuovo parte con **una battuta in 4/4: 4 quarti, 16
    step**, tutti utilizzabili; niente step grigi, niente che si ripete o si allunga da solo.
  - La barra in cima al Channel Rack indica la lunghezza (*Pattern: 4 quarters (1 bar) - 16 steps*) e ha due pulsanti:
    **+** aggiunge **un quarto** (4 step in più) e **-** ne toglie uno. Il minimo è 4 quarti (il pulsante - è allora
    disattivato), il massimo 64 (16 battute). La lunghezza è la stessa per tutti i pattern; gli step oltre una fine più
    corta restano conservati ma non suonano, e tornano con +. Le clip della Playlist restano dove sono (vedi *Pattern*).
    Ctrl+Z annulla.
  - Appena il canale ha una nota che non è un semplice step - un'altra altezza, una nota più lunga, una nota fuori
    dalla griglia dei sedicesimi, scritta nel Piano Roll, nella drum machine, registrata o generata - la riga mostra
    invece l'**anteprima del Piano Roll**. Il **clic
    sinistro** sull'anteprima apre il Piano Roll o lo porta in primo piano, dalla 0.53.0 anche per i canali drum (la drum machine
    si apre con il doppio clic sul canale o dal suo menu); il clic destro apre il menu del canale.
  - Se cancelli tutte le note nel Piano Roll, la riga torna ai quadratini.
  - I quadratini vengono disegnati ogni volta dalle note, quindi mostrano sempre ciò che suona.
  - I progetti di prima della 0.31.0 si aprono con la sequenza di ogni canale lunga quanto il pattern, quindi suonano
    esattamente come prima. I progetti dalla 0.31.0 alla 0.41.0 le cui sequenze ripetevano una parte più corta si aprono
    con quelle ripetizioni scritte come step, quindi suonano uguali anche loro.
- Il pulsante **Load** di un canale sampler sceglie un file audio.
- Clic su una riga per selezionare il canale (Piano Roll e Mixer seguono la selezione).
- **+ Add channel** sotto l'ultima riga aggiunge un canale synth, sampler o drum machine, fino a 32 canali in tutto
  (i canali possono condividere le tracce del mixer, che possono essere fino a 500).

### Menu del tasto destro su un canale

| Voce | Effetto |
| --- | --- |
| Drum machine... | (Canali drum) apre la finestra della drum machine. |
| Piano roll | Seleziona il canale e apre il Piano Roll per quel canale. |
| Instrument > Synth / Sampler | Sceglie cosa produce il suono. Passando a Sampler su un canale senza sample ne viene chiesto uno. Il sample resta se torni a Synth. |
| Instrument > Plugin instruments | I plugin strumento trovati dall'ultima scansione: il canale suona il plugin scelto (vedi *Plugin*). |
| Plugin window... | (Canali plugin) apre la finestra del plugin. Anche il doppio clic sul canale la apre. |
| Load sample... | Scegli un file WAV, FLAC, OGG o AIFF (si usano i primi 30 secondi). Il canale prende il nome del file. |
| Rename... | Fino a 16 caratteri. |
| Clone channel | Aggiunge una copia subito sotto: stesso strumento, suono e impostazioni, e le stesse note in ogni pattern. |
| Move up / Move down | Sposta il canale di una riga; le sue note in ogni pattern e le sue clip di automazione lo seguono. |
| Delete channel... | Dopo una conferma elimina il canale, le sue note in ogni pattern e le clip di automazione del suo volume o pan. L'ultimo canale non si può eliminare. `Ctrl+Z` lo riporta indietro. |
| Assign free mixer track | Apre una finestra con le tracce del mixer (un elenco che scorre; l'ultima riga offre una traccia nuova): quelle libere (senza strumenti) in verde, le altre con gli strumenti che ci sono sopra, e quella attuale segnata. La prima traccia libera è già selezionata; **Select first free track** la riseleziona, oppure scegli una traccia qualsiasi (condividerla è permesso), poi **Assign** (o Invio; Esc annulla). Il display **Track N** del canale e il mixer si aggiornano, e il mixer seleziona quella traccia. |
| Save as preset... | Chiede un nome e scrive un file `.slspreset` (strumento, sample, volume, pan, pitch, swing; non le note, le impostazioni del sampler o un SoundFont) in `~/Music/Sun Loops Studio/Presets`; compare subito nella cartella Presets del browser. |
| Synth settings... | (Canali synth) apre la finestra del synth: suoni pronti, il motore e tutte le impostazioni del synth sottrattivo (vedi *Synth*). |
| Sampler settings... | (Canali sampler) apre la finestra del sampler: inviluppo, loop, filtro, nota di base (vedi *Sampler e SoundFont*). |
| Load SoundFont / SFZ (.sf2, .sfz)... | Sceglie un file `.sf2`, poi uno dei suoi preset, oppure uno strumento `.sfz`: il canale diventa un sampler che lo suona (vedi sotto). Entrambi si possono anche trascinare dal browser su un canale. |

Anche il doppio clic sull'anteprima delle note di un canale synth, o sul nome di un qualsiasi canale, apre il suo
Piano Roll. Sui canali sampler il doppio clic sugli step non ha un significato speciale: ogni clic attiva o
disattiva uno step.

## Synth

Clic destro su un canale synth > **Synth settings...** apre la finestra del synth. Ogni canale synth parte con il motore **Classic**, il
suono originale di Sun Loops Studio (un seno con due armoniche), così i progetti vecchi suonano come prima. Imposta **Engine** su
**Subtractive**, oppure scegli un suono pronto dal menu in alto (*Saw Lead, Square Bass, Acid Bass, Soft Pad, Pluck, Brass, 808 Sub, Organ,
PWM Strings*), per modellare il suono. **Play C5** suona il canale finché lo tieni premuto. Gli stessi suoni sono nel browser in *Presets > Factory*.

| Sezione | Impostazioni |
| --- | --- |
| Oscillator 1 e 2 | Forma d'onda (seno, triangolo, dente di sega, quadra), livello, ottava (-3..+3), semitono (-12..+12), intonazione fine in centesimi, larghezza dell'impulso (solo quadra). Dente di sega e quadra sono a banda limitata (PolyBLEP), quindi le note alte hanno poco aliasing. |
| Noise | Livello di rumore bianco aggiunto agli oscillatori. |
| Filter | Spento, passa-basso, passa-alto o passa-banda; **24 dB / octave** mette due filtri in serie; cutoff, risonanza, quanto l'inviluppo del filtro sposta il cutoff (fino a 6 ottave in su o in giù) e key tracking (il cutoff segue la nota, a partire da C5, MIDI 60). |
| Amplifier envelope | Attacco, decadimento, sostegno, rilascio: la nota suona finché è tenuta, poi sfuma con il rilascio. |
| Filter envelope | Attacco, decadimento, sostegno, rilascio del movimento del cutoff. |
| LFO | Destinazione (pitch: vibrato fino a 2 semitoni; cutoff: fino a 3 ottave; livello: tremolo; larghezza dell'impulso), forma d'onda, velocità (0,05-20 Hz), profondità. L'LFO riparte a ogni nota. |
| Output | Il livello del synth prima del volume del canale. |

### Motore FM

**Engine > FM (4 operators)** sostituisce oscillatori, rumore e filtro con quattro **operatori** sinusoidali. Ognuno ha un **rapporto** di
frequenza rispetto alla nota (x 1 è la nota, x 2 un'ottava sopra, x 3,5 un armonico da campana), un **livello** e un suo **inviluppo**. La
**matrice** dice chi modula chi: la riga è l'operatore modulato, la colonna il modulatore, la diagonale (arancione) è il feedback di un
operatore e la colonna **Out** (verde) è quanto di ogni operatore si sente. Trascina una cella in su o in giù per cambiarla. Il menu
**Preset** sopra di essa imposta una disposizione classica (*2 > 1*, catene di tre e quattro, due coppie, tre modulatori su uno, uno su
tre, feedback, tutti udibili); dopo una modifica a mano mostra *(your own)*. L'inviluppo di ampiezza, l'LFO (pitch o livello) e il livello
d'uscita valgono come per il motore sottrattivo. Suoni FM pronti: *FM E-Piano, FM Bell, FM Bass, FM Brass, FM Drawbars, FM Growl*.

### Motore additivo (0.24.0)

**Engine > Additive** costruisce il suono con fino a 64 armoniche. **Timbre A** e **Timbre B** le impostano: quante (**Harmonics**),
**Bright** (la pendenza: 60 % è una dente di sega, 100 % piatta, 0 % molto scura), **Even** (il livello delle armoniche pari: 0 % dà un
suono vuoto, tipo onda quadra), **Stretch** (inarmonicità: le armoniche alte crescono di intonazione, come nelle campane e nelle corde acute
del pianoforte) e un **Formant** (un picco simile a una vocale: quantità, **Freq** 150 Hz - 6 kHz, **Width**). **Morph** dice dove sta il
suono fra A e B, e l'**inviluppo del morph** (con la sua quantità, anche negativa) lo sposta nel tempo; anche l'LFO lo può muovere
(target *Morph*). Il **filtro** e il suo inviluppo agiscono sulle armoniche stesse, quindi non producono aliasing. Le armoniche oltre metà
della frequenza di campionamento vengono tolte. Suoni pronti: *Additive Organ, Morph Pad, Vowel Choir, Glass Keys, Additive Bass*.

### Motore fisico (0.24.0)

**Engine > Physical model** modella lo strumento invece di una forma d'onda. **Model**: una **corda pizzicata** (Karplus-Strong: un
periodo di rumore che risuona in un anello e perde un po' di acuti a ogni giro), **marimba**, **vibrafono**, **vetro**, **campana** o
**lastra di metallo** (risonatori sui modi della barra, del vetro, della campana o della lastra, eccitati da un battente). **Hardness** è il
plettro o il battente (morbido e scuro, duro e brillante; la velocity si aggiunge e rende la nota più forte), **Position** dove si pizzica o
si colpisce. **Decay** è quanto risuona (da 0,15 a 15 s), **Damping** quanto più in fretta si spengono gli acuti, **Stiffness** (corde)
rende crescenti le parziali alte come in una corda d'acciaio, **Body** (corde) aggiunge le risonanze di una cassa di legno, **Rel. damp**
quanto in fretta si smorza una nota rilasciata (0: continua a suonare, come un'arpa o una campana). L'inviluppo ha solo attacco e
rilascio: il modello decade da solo. Suoni pronti: *Nylon Guitar, Steel Guitar, Harp, Plucked Bass, Marimba, Vibraphone, Glass Harmonics,
Church Bell, Metal Hit*.

Legato e slide (Shift+clic nel Piano Roll) funzionano come con il motore classico. Un preset di synth (*Save as preset...*) salva anche il
suono. Ogni modifica si annulla con `Ctrl+Z`.

## Sampler e SoundFont

Tasto destro su un canale sampler > **Sampler settings...** apre una finestra per quel canale:

- **Sound**: il sample (o il preset SoundFont / programma di slice) che il canale suona, **Load sample...**, **Load SoundFont (.sf2)...** e **Use the
  sample** (dimentica il programma: il canale torna a suonare il suo sample). **Root key**: il tasto a cui il sample suona alla sua altezza (C5 = 60).
- **Envelope** (inviluppo): spento (predefinito), una nota suona il sample una volta fino in fondo, qualunque sia la sua lunghezza, come in tutte le
  versioni precedenti. Acceso, una nota suona finché è tenuta: **Attack** (entrata), **Decay** fino al livello di **Sustain**, e **Release** dopo la fine
  della nota. Anche le note suonate a mano (piano roll, tastiera MIDI) vengono tenute.
- **Loop**: *Forward* ripete la parte tra **Loop start** e **Loop end** (in % del sample) finché la nota è tenuta, così un suono corto può durare.
- **Filter**: *Low-pass*, *High-pass* o *Band-pass*, con **Cutoff** (da 20 Hz a 20 kHz) e **Resonance**. Ogni nota è filtrata per conto suo.

**SoundFont.** *Load SoundFont (.sf2)...* (nel menu del canale o nella finestra del sampler) chiede un file ed elenca i suoi preset (per banco se ce
ne sono più di uno); scegline uno. I campioni del preset diventano **zone**: ognuna risponde a un intervallo di tasti e di velocity, con la sua nota di
base, accordatura, livello, pan e loop, esattamente come dice il file, e l'inviluppo viene preso dal file (puoi cambiarlo). Il progetto ricorda il file
e il preset, ed *Export project with samples* copia il `.sf2`. Non vengono presi dal file: modulatori, LFO e vibrato, il suo filtro e l'inviluppo di
modulazione, mandate di chorus / riverbero e classi esclusive. File fino a 512 MB.

**Strumenti SFZ (0.41.0).** Un file `.sfz` (un file di testo che associa file audio a tasti e velocity, comune negli strumenti
campionati gratuiti) si carica allo stesso modo: *Load SoundFont / SFZ...*, o trascinandolo dal browser. Vengono letti: `<control>`
*default_path*, i livelli `<global>` / `<master>` / `<group>` / `<region>` con *sample*, *key*, *lokey*, *hikey*, *pitch_keycenter*
(tasti per numero o per nome: c4 = 60), *lovel*, *hivel*, *tune*, *transpose*, *volume*, *pan*, *loop_mode* (loop continuo / sustain)
con *loop_start* e *loop_end*. Il resto (filtri, inviluppi, round robin, dissolvenze, `#include`) viene tralasciato e la riga di stato
dice quante impostazioni. I campioni si cercano a partire dalla cartella del `.sfz`. Il progetto memorizza il `.sfz`; *Export project
with samples* ne copia tutta la cartella.

**Dalle slice alle note.** Nell'editor di campioni (una clip o un canale sampler) metti dei marcatori (*Find hits*, *Equal parts...* o doppio clic) e
premi **Slices to sampler channel**: l'audio modificato viene salvato come nuovo file WAV, un **nuovo canale sampler** suona la slice 1 su C5, la 2 su
C#5 e così via (un tasto ciascuna, alla loro altezza), e il pattern corrente riceve **una nota per slice nel momento in cui era nell'audio**, quindi il
pattern suona il loop com'era e puoi spostare, ripetere o eliminare le note per fare un groove nuovo.

### Slicer (0.37.0)

Lo **Slicer** taglia un loop in fette (slice), come Slicex di FL Studio: la slice 1 suona su **C5**, la 2 su C#5 e così via, e il pattern le
suona con una nota per slice. Poiché le note sono in battiti, il loop **segue il tempo del progetto**: rallenta il brano e le slice vengono
suonate più distanti, ognuna alla propria velocità e intonazione.

- **Un loop nuovo**: *+ Add channel* > *Slicer (slice a loop)...*, scegli un file audio. Viene tagliato dove iniziano i suoi suoni, il
  pattern corrente riceve una nota per slice e si apre la finestra dello Slicer.
- **Il sample di un canale**: tasto destro su un canale sampler > *Slicer...* (anche per le slice fatte con *Slices to sampler channel*).

Nella finestra:

| Azione | Come |
| --- | --- |
| Ascoltare una slice | Cliccala (suona sul suo tasto attraverso il canale, finché tieni premuto) |
| Spostare un marcatore | Trascinalo (la prima slice inizia sempre dall'inizio) |
| Aggiungere un marcatore | Doppio clic |
| Eliminare un marcatore | Tasto destro su di esso |
| Ritagliare | *Slice by sound* (con **Sensitivity**: più alta trova più colpi), *Slice by grid* (da 1/4 a 1/32 al **Loop tempo**), *Equal parts...* |
| Una slice alla volta | *A slice cuts the one before* (attivo di default, come in Slicex): una nuova slice ferma quella che suona ancora (sfumatura di 5 ms); se spento, si sovrappongono |
| Cambiare una slice | Selezionala, poi **Pitch** (da -24 a +24 semitoni), **Level**, **Pan**, **Reverse**; *Reset slice* li riporta come prima |
| Le note del loop | *Write the loop into the pattern*: le note di questo canale nel pattern corrente vengono sostituite da una nota per slice |

Il **Loop tempo** è il tempo a cui il loop è stato suonato: viene stimato all'apertura della finestra (il tempo più vicino a quello del
progetto per cui il loop dura 1, 2, 4, 8 o 16 battute) e si può scrivere. Decide dove *Write the loop into the pattern* mette le note e
la griglia di *Slice by grid*. Ogni modifica si sente subito e si annulla con Ctrl+Z (che chiude la finestra). Tutto si salva con il
progetto (il loop resta il suo file audio: conservalo, o usa *Export project with samples*). Al massimo 128 slice; quelle oltre il
tasto G10 non suonano.

## Drum machine

Un **canale drum** è un canale con 16 pad. Si aggiunge con **+ Add channel** > *Drum machine (16 pads)*, trascinando *Instruments > Drum machine* dal browser sul rack, oppure con il tasto destro
su un canale > *Instrument* > *Drum machine (16 pads)*. Un nuovo canale drum parte con il Sun Loops Basic Kit (cassa, rullante e hi-hat sui pad da 1 a 3). Il **doppio clic** sul canale (o tasto destro >
*Drum machine...*) apre la sua finestra; volume, pan, pitch, mute / solo, traccia del mixer e automazione funzionano come per ogni canale.

**Pad.** La griglia 4 x 4 ha il pad 1 in basso a sinistra. Il pad *n* suona quando il canale riceve la nota MIDI **36 + n** (pad 1 = C3, pad 16 = D#4): le stesse note compaiono nel Piano Roll, una tastiera
MIDI suona i pad e le note importate in quell'intervallo li suonano. Clic su un pad per sentirlo e selezionarlo (Maiusc+clic = piano, Ctrl+clic = forte, così senti i layer di velocità). Tasto destro su un pad:
*Load a sound*, *Clear this pad*, *Mute*, *Solo*. Trascina un suono dal browser su un pad per metterlo sul layer 1.

**Il pad selezionato** (a destra):

| Controllo | Significato |
| --- | --- |
| Nome, Mute, Solo | Il nome del pad (fino a 24 caratteri, Invio per confermare), e silenziarlo / suonare solo i pad in solo (il mute vince sul solo). *Play pad* lo suona. |
| Choke group | *None*, o gruppo da 1 a 8. Quando un pad parte, i pad dello stesso gruppo ancora in suono vengono interrotti (con una breve dissolvenza): un hi-hat chiuso ferma uno aperto. Un pad in un gruppo interrompe anche il proprio colpo precedente. |
| Layer da 1 a 4 | Ogni layer contiene un suono (*Load*, oppure trascinane uno dal browser sulla sua riga; *x* lo svuota) e la **velocità minima a cui risponde** (da 1 a 127). Una nota suona il layer con il minimo più alto che la sua velocità raggiunge; sotto tutti i minimi suona il layer più basso. Un secondo, terzo e quarto layer nuovi partono da 64, 96 e 112. |
| Level, Pan, Tuning | Livello da 0 a 150 %, pan da -100 a +100, intonazione da -24 a +24 semitoni (cambia anche la lunghezza del suono). Doppio clic su un cursore per riportarlo al valore iniziale. |
| Swing | Da 0 a 100 %: ritarda gli step di sedicesimo in levare di questo pad (fino a mezzo sedicesimo). La manopola Swing del canale è nascosta: un canale drum non la usa. |

**Step.** La griglia in basso ha una riga per pad e uno step per ogni sedicesimo del pattern (16 per la battuta predefinita). Clic su uno step per accenderlo o spegnerlo (senti il pad), trascina per dipingere o cancellare una serie, tasto destro
per cancellare. Maiusc+clic mette uno step piano (velocità 0,45), Ctrl+clic uno accentato (1,0), un clic semplice 0,8; la luminosità di uno step mostra la sua velocità. Un clic sull'etichetta di una riga
seleziona (e suona) quel pad. La griglia modifica le note del canale nel **pattern corrente** (annulla e ripristina delle note funzionano), mostra le note fatte altrove (una nota appartiene allo step in cui
inizia) e in modalità Pattern una linea segue il playhead.

**Kit incorporati** (0.22.0). Ogni kit ha i pad nell'ordine General MIDI della batteria (pad 1 = tasto 36 = cassa, 3 = rullante, 7 / 9 / 11 =
charleston chiuso, a pedale e aperto nel choke group 1, 14 = crash), così una parte di batteria General MIDI suona i suoni giusti, e fino
a quattro livelli di velocity per pad (i limiti sono accanto a ogni livello: una nota più forte suona una registrazione più forte e più
brillante):

| Kit | Cos'è |
|---|---|
| Acoustic Studio | Kit registrato (DrumGizmo MuldjordKit, CC BY 4.0): due casse, rullante e rullante con la mano appoggiata, quattro tom, charleston, due crash, china, due ride e campana del ride |
| Acoustic Rock | Kit registrato (DrumGizmo DRSKit, CC BY 4.0): cassa e cassa col clic del battente, rullante, rest e rimshot, tre tom, charleston chiuso / semiaperto / a pedale / aperto, due crash, ride e campana |
| Electronic Boom | Fatto dal programma alla maniera delle classiche drum machine analogiche: cassa lunga e rotonda, rullante secco, battimani, charleston metallici, conga, campanaccio, clave, shaker |
| Electronic Punch | Cassa corta con clic, rullante rumoroso, sei tom, crash e ride: il genere house e techno |
| Electronic Modern | Cassa lunga distorta, rullante e battimani sovrapposti, charleston nitidi, un sub "808" intonato sul pad 8 |
| Hand Percussion | Fatto dal programma con la sintesi modale: conga (aperta, smorzata, bassa), bongo, timbales, campanaccio, agogo, wood block, claves, shaker, tamburello, triangolo |
| Sun Loops Basic Kit | I tre piccoli suoni sintetizzati delle prime versioni |

I nuovi canali di batteria partono con *Electronic Punch*. I kit registrati sono mixati in stereo da tutti i loro microfoni (microfoni
ravvicinati panoramicati, overhead e ambiente larghi), quindi suonano come un kit intero in una stanza; come sono stati preparati è in
`tools/sounds`.

**Kit.** *Load kit...* sostituisce tutti i 16 pad (gli step restano): i kit incorporati qui sopra, *From a folder...* (una cartella con un `kit.json`, o semplicemente una cartella di file audio:
i primi 16 file in ordine di nome riempiono i pad e li battezzano) e ogni sottocartella di `~/Music/Sun Loops Studio/Drum Kits` sotto *My kits*. *Save kit...* chiede un nome e scrive in *Drum Kits* una cartella con quel
nome, con `kit.json` e copie dei suoni in `samples/` (i suoni inclusi nel programma restano riferimenti); chiede conferma prima di sostituire un kit con lo stesso nome. *Empty kit* svuota tutti i pad dopo conferma.

**Nel progetto.** Il file di progetto salva il kit di ogni canale drum: nomi e impostazioni dei pad e da dove viene ogni suono (un percorso di file, o un suono interno), non l'audio. All'apertura i suoni vengono riletti;
un file sparito lascia un layer silenzioso che ricorda il percorso (e un messaggio), così salvando di nuovo il riferimento non si perde. Salva il kit come cartella per renderlo portabile.

Non disponibili: annulla per le modifiche al kit (gli step si annullano), un'uscita o effetti per pad (tutto il canale va su una traccia del mixer), layer round-robin, modificare il suono di un pad nell'editor di
campioni (modificalo su un canale sampler e poi carica il file salvato), più di 16 pad o 4 layer, suoni più lunghi di 30 secondi, salvare un canale drum come preset (salva il kit), assegnare ai pad le percussioni
di un MIDI importato. I progetti con una drum machine non si aprono con versioni precedenti alla 0.14.

## Plugin

Sun Loops Studio ospita plugin esterni **VST3**, **LV2** e **LADSPA**, come strumenti e come effetti.

1. Nel browser apri **Plugin** e clicca **Scan for plugins (VST3, LV2, LADSPA)...**. La scansione gira in background
   (la riga di stato mostra il file che sta leggendo) e cerca nelle cartelle abituali, comprese quelle `lib64` (dove li
   installano Fedora e Nobara): VST3 in `~/.vst3`, `/usr/lib/vst3`, `/usr/lib64/vst3` e le stesse sotto `/usr/local`; LV2 in `~/.lv2`,
   `/usr/lib/lv2`, `/usr/lib64/lv2` e sotto `/usr/local`; LADSPA in `LADSPA_PATH` (se impostata), `/usr/lib/ladspa`, `/usr/lib64/ladspa`,
   sotto `/usr/local` e in `~/.ladspa`. L'elenco viene salvato e ricompare all'avvio successivo; rifai la scansione dopo aver installato nuovi plugin.
2. **Strumenti**: doppio clic su uno (un nuovo canale lo suona), trascinalo nello spazio vuoto sotto il rack (nuovo
   canale) o su un canale (quel canale ora lo suona), oppure clic destro su un canale > *Instrument > Plugin instruments*.
   Il canale riceve le note dagli step, dal piano roll, dalla playlist e da una tastiera MIDI come ogni altro strumento.
   La sua finestra si apre da sola; in seguito fai doppio clic sul canale o scegli *Plugin window...*.
3. **Effetti**: nel mixer clicca uno slot > *Plugin effects* e scegline uno, oppure trascina un effetto dal browser su un
   canale (va nel primo slot libero della traccia mixer di quel canale). *Open plugin window* mostra i suoi controlli;
   il pulsante di accensione lo esclude come per gli effetti interni.

Le impostazioni di un plugin (quello che cambi nella sua finestra) si salvano nel progetto. Un progetto che usa un plugin
non installato su questo computer si apre lo stesso: il plugin resta muto (un effetto lascia passare il suono), le sue
impostazioni sono conservate e salvando il progetto restano per il computer che lo ha. Le esportazioni usano gli stessi
plugin; mentre un'esportazione è in corso, l'audio dal vivo è muto.

I plugin girano dentro Sun Loops Studio: **un plugin che va in crash chiude il programma** (il salvataggio automatico e il
recupero conservano il lavoro). Un plugin che ha mandato in crash la scansione viene saltato alla scansione successiva.
Clonare un canale plugin crea una seconda copia del plugin con le stesse impostazioni.

### SunBurst, il synth plugin incluso (0.48.0)

**SunBurst** è un sintetizzatore polifonico per suoni elettronici incluso in Sun Loops Studio. È un vero plugin **VST3**
(funziona anche in altri programmi che caricano VST3, e da solo come programma, *SunBurst* Standalone), installato accanto a
Sun Loops Studio che lo trova all'avvio senza scansione. Si aggiunge con **Browser > Instruments > SunBurst (synthesizer)**
(doppio clic o trascina) o con il **+** del Channel Rack > *SunBurst synthesizer*. La sua finestra si apre subito; si riapre
come quella di ogni plugin. Suono e impostazioni si salvano col progetto, e ogni parametro si può automatizzare.

La finestra ha tre pagine e una tastiera per provare i suoni col mouse:

- **SYNTH**
  - **Oscillatori 1-3** (il LED accende ciascuno): *Sine, Triangle, Saw, Square* (con *Width*, l'ampiezza dell'impulso), *Noise*
    e **Wavetable** (14 tabelle: Analog Morph, Pulse Width, Harmonic Sweep, Formant, Digital Bells, Organ, Hard Sync, Growl e dalla
    0.49.0 Brass, FM Bell, Wavefold, Vowel Bass, Chord Saw, Metallic;
    *WT POS* le percorre). *Octave, Semi, Fine, Level, Pan*. **Unison** sovrappone fino a 16 voci: *Detune* ne allarga
    l'intonazione (fino a un "fat saw" molto largo), *Blend* dosa le voci esterne rispetto a quella centrale, *Spread* le apre
    nello stereo; *Random phase* fa partire ogni nota da un punto casuale (suono più pieno; spento per attacchi secchi). Tutte le
    tabelle sono a banda limitata: niente aliasing sulle note acute.
  - **Sub & noise**: un sub-oscillatore una o due ottave sotto (sinusoide o quadra) e un generatore di rumore con colore.
  - **Filter**: passa-basso, passa-alto, passa-banda (12 o 24 dB/ottava), notch e peak, con *Cutoff, Reso* (risonanza),
    *Drive*, *Env amt* (dall'inviluppo 2), *Key* (segue la nota) e *Velocity*.
  - **Envelopes**: *AMP* (il volume, con *Velocity*), *FILTER* (inviluppo 2) e *MOD* (inviluppo 3, con *To pitch* per glissati,
    zap e riser), ciascuno con attacco, decadimento, sostegno, rilascio.
  - **LFO 1-3**: sinusoide, triangolo, dente di sega su e giù, quadra, sample & hold, random morbido; *Rate* in Hz o a tempo
    (*Sync*, *Division* da 4 battute a 1/32 con puntati e terzine), *Fade in*, *Retrigger* (riparte a ogni nota, o libero),
    *Unipolar*.
  - **Voice**: *Poly*, *Mono* o *Legato* (nessun nuovo attacco finché un tasto è premuto), polifonia (fino a 32), *Glide*,
    estensione del pitch bend.
- **MOD MATRIX**: 16 slot, ognuno *sorgente > destinazione* con una quantità da -1 a +1. Sorgenti: i tre LFO, gli inviluppi 2 e
  3, velocity, mod wheel, aftertouch, pitch bend, tasto, un valore casuale per nota e le quattro **macro**. Destinazioni: per ogni
  oscillatore intonazione, posizione nella wavetable, ampiezza dell'impulso, livello, detune e pan; intonazione generale, livelli
  di sub e rumore; cutoff, risonanza, drive, ampiezza, pan; la velocità degli LFO; e negli effetti drive della distorsione, cutoff
  del filtro, mix di chorus, delay e riverbero.
- **EFFECTS & ARP**: distorsione (soft, hard, tube, fold, bit crush), EQ a tre bande, filtro multimodo, chorus, delay stereo o
  ping-pong (a tempo col brano o in ms, con feedback e tono) e riverbero (dimensione, smorzamento, larghezza, pre-delay, mix), in
  quest'ordine, ciascuno col suo interruttore. L'**arpeggiatore** suona i tasti premuti in su, in giù, su e giù, a caso, come
  suonati o ad accordi, da 1/4 a 1/32 (con terzine) su 1-4 ottave, con la durata delle note (gate).

La barra in alto ha il **browser dei preset** (clic sul nome per il menu per categoria, **<** / **>** per scorrerli), **Save**
(nome e categoria; i tuoi preset vanno in `~/.config/SunBurst/Presets` e compaiono nel menu), le quattro **macro**, il volume
**master** e il misuratore d'uscita. **101 preset di fabbrica** (43 fino alla 0.48) in otto categorie, con volumi pareggiati,: Lead, Pad, Strings, Bass (fat saw, Reese, sub,
acid, growl, wobble), Arp, Pluck, Keys e FX. In tutti la mod wheel apre il filtro, Macro 1 = brillantezza, Macro 2 = riverbero,
Macro 3 = larghezza dell'unison, Macro 4 = drive del filtro.

Tempo: dentro Sun Loops Studio SunBurst segue il tempo del brano (LFO a tempo, delay e arpeggiatore; dalla 0.48.0 tempo e
posizione arrivano a ogni strumento plugin). La finestra si può ridimensionare (trascina l'angolo) e mantiene le proporzioni.

### HexForge, il synth FM incluso (0.50.0)

**HexForge** è un secondo synth plugin incluso in Sun Loops Studio (VST3 e Standalone, trovato all'avvio come SunBurst): un
synth **ibrido FM + sottrattivo** per timbri taglienti, aggressivi e metallici - cyberpunk, dark synth, industrial, drum &
bass, dubstep old school e neurofunk. Si aggiunge con **Browser > Instruments > HexForge (FM synthesizer)** o il **+** del
Channel Rack > *HexForge FM synthesizer*. La finestra ha un aspetto in acciaio scuro con luci rosse e azzurre e quattro pagine:

- **OPERATORS**: sei **operatori**, ognuno con un'**onda** - 32 onde classiche a banda limitata (sinusoide, triangolo, dente
  di sega, rampa, quadra, impulsi, mezza / intera / quarto di sinusoide, camel, organo, clarinetto, seghe brillanti e morbide,
  trapezio, scala, esponenziale, parabola, sinusoide ripiegata, sync, vocale, campana, metallo, digitale...) o una delle due
  **onde spettrali** - un **rapporto** con la nota (da 1/4 a 16), *Fine*, *Level* (quanto è forte, anche come modulatore),
  *Out* (quanto si sente: 0 = solo modulatore), *Pan*, *Velocity* e un **inviluppo** proprio (attacco, decadimento, sostegno,
  rilascio). La **matrice FM** accanto è un'unica griglia 6 x 6 di manopole: quella in riga *R*, colonna *C* dice quanto
  l'operatore *C* modula l'operatore *R*; la diagonale (illuminata) è il **feedback** di ogni operatore. Niente algoritmi da
  scegliere: giri le manopole che servono. **Engine**: *FM depth* (tutta la matrice insieme), **unison** (1-8 voci, *Detune*,
  *Spread*), *Octave* e un **pitch drop** (una caduta veloce all'inizio di ogni nota, per zap e bassi d'impatto).
- **FILTER & AMP**: il **filtro ladder**, modellato sul ladder a quattro poli a transistor di Moog (la risonanza fischia e
  auto-oscilla, un segnale forte satura): *Ladder LP 24 / LP 12 / HP 24 / HP 12 / BP*, *Cutoff, Resonance, Drive*, la quantità
  del suo **inviluppo dedicato** (pannello *Filter envelope*, ADSR), *Key track*, *Velocity*; il **Filter LFO** (quantità,
  forma, velocità o divisione a tempo) muove il cutoff per i wobble. *Amplifier* (ADSR, velocity), *LFO 2* per la pagina delle
  modulazioni e *Voice* (poly / mono / legato, polifonia fino a 16, glide, estensione del bend).
- **SPECTRAL & MOD**: disegni **Spectral A** e **Spectral B** come 64 armoniche col mouse, parti da *Saw, Square, Odd, Bright,
  Formant, Metal, Random* o *Clear*, oppure **Load WAV...** carica un'onda a ciclo singolo (se ne leggono le prime 64
  armoniche). Si salvano col suono. **Modulation**: 8 slot *sorgente > destinazione* con una quantità.
- **EFFECTS**: distorsione (accesa di default, tube), EQ, chorus, delay a tempo, riverbero.

Le voci girano a frequenza doppia e vengono filtrate al ritorno, così l'FM spinto e il ladder restano puliti. **40 preset di
fabbrica** in otto categorie - Neuro, Reese, Dubstep, DnB, Cyberpunk, Dark Synth, Industrial, FX & Metal - con volumi
pareggiati; in tutti la mod wheel aggiunge FM, Macro 1 = cutoff, Macro 2 = profondità FM, Macro 3 = distorsione, Macro 4 =
larghezza dell'unison. I tuoi preset vanno in `~/.config/HexForge/Presets`.

## Piano Roll

Modifica le note del canale selezionato nel pattern corrente. Il titolo mostra il canale ("Piano Roll - Lead"). È
chiuso all'avvio e si apre da un canale, come sopra.

| Azione | Come |
| --- | --- |
| Suonare una nota | Pulsante sinistro su un tasto della tastiera (tenuto premuto = suona). |
| Aggiungere una nota | Clic su una cella vuota (premi e rilascia senza muovere: dalla 0.44.0 un trascinamento sulle celle vuote seleziona). Il tasto suona finché tieni premuto. Trascina il bordo destro della nota nuova per cambiarne la lunghezza. |
| Spostare una nota | Trascinala (Alt mentre trascini: senza snap). Trascinando una nota **selezionata** si spostano tutte le note selezionate. |
| Cambiare la lunghezza | Trascina il bordo destro, oppure il **bordo sinistro** per spostarne l'inizio tenendo ferma la fine (0.32.0); sopra i due bordi il cursore diventa una freccia destra-sinistra. Su una nota selezionata tutte le selezionate cambiano della stessa quantità. |
| Riusare una lunghezza | Clic su una nota: la sua lunghezza diventa quella delle prossime note che disegni (0.32.0, come in FL Studio). |
| Eliminare una nota | Tasto destro sulla nota (su una nota selezionata: tutta la selezione). |
| Selezionare note | **Trascina su una parte vuota** (0.44.0, come sul desktop): compare un rettangolo di selezione e al rilascio vengono selezionate tutte le note che tocca o contiene (con Maiusc aggiunge alla selezione; **Ctrl + trascinamento** fa lo stesso); **Ctrl + clic** su una nota la aggiunge o la toglie; **Ctrl + A** seleziona tutto, **Esc** niente. Le note selezionate sono chiare con il bordo bianco. |
| Copiare, tagliare, incollare, duplicare | **Ctrl + C / X / V** con il mouse sul piano roll (incolla mettendo la prima nota sotto il mouse; le note copiate si incollano nello stesso piano roll o in quello di un altro canale o pattern - scegli il canale o il pattern, poi Ctrl + V), **Ctrl + D** (una copia subito dopo la selezione). **Canc** elimina la selezione. |
| Spostare con i tasti | **Su / Giù**: un semitono (Maiusc: un'ottava, Ctrl: un grado della scala); **Sinistra / Destra**: un passo dello snap (Maiusc: un movimento). |
| Legato / slide | Maiusc + clic su una nota: normale -> **legato** (blu, "L") -> **slide** (arancione, "S") -> normale. |
| Scorrere in altezza | Rotella del mouse. |
| Scorrere nel tempo | Maiusc + rotella. |
| Zoom nel tempo | Ctrl + rotella. Finché non zoomi, tutto il pattern entra nella larghezza del pannello. |
| Spostare la posizione nel pattern | Clic sinistro (o trascinamento) sul **righello**, anche durante la riproduzione (a passi di 16esimi; Alt: libero). A riproduzione ferma il segno arancione mostra da dove parte Play. La riproduzione passa in modalità Pattern. |
| Ripetere una parte del pattern | **Trascinamento col destro sul righello**: disegna una zona di loop (blu, "LOOP") e la modalità Pattern ripete solo quella parte. A riproduzione ferma Play parte dalla zona. |
| Spostare / ridimensionare la zona | Trascinamento col destro dentro la zona la sposta; su uno dei suoi bordi la ridimensiona. |
| Menu della zona | **Clic destro sulla zona**: **Move** (la zona segue il mouse; clic per posarla), **Resize** (il bordo più vicino segue il mouse; clic per finire), **Delete loop region** (toglie solo l'evidenziazione: nessuna nota viene toccata). Durante Move e Resize un clic destro o Esc la rimettono com'era. |

**La barra degli strumenti** (0.23.0), sopra le note:

- **Snap**: Bar, 1/2, 1/4 (un quarto), 1/8, 1/16 (predefinito), 1/32, 1/64, terzine di 1/8 e 1/16, None. Le note si disegnano, spostano e ridimensionano su questa griglia.
- **Scale**: una tonalità e una scala (maggiore, le minori, i modi, le pentatoniche, blues). Le righe delle note fuori scala sono più
  scure e la tonica è scritta in verde sulla tastiera. **Snap to scale** fa cadere le note nuove e spostate sulla scala.
- **Chord**: scelto un accordo (maggiore, minore, settime, none, sus, sesta, power...) e il suo rivolto, un clic posa l'accordo intero
  con la fondamentale sul tasto cliccato; trascina per la lunghezza di tutte le sue note.
- **Velocity**: mostra la **corsia della velocity** sotto le note, una barretta per nota. Trascina su di essa per disegnare le velocity
  (solo delle note selezionate, se ce ne sono).
- **Tools** agisce sulle note selezionate (o su tutte se nessuna è selezionata): Quantize (inizi; inizi e fini; a metà), Humanize
  (leggero / medio / forte), Legato, Staccato, Strum degli accordi (su / giù, veloce / lento), Arpeggio degli accordi (su, giù, su e
  giù, casuale; ottavi o sedicesimi), Chop (ottavi, sedicesimi, trentaduesimi), Flip, trasposizione di un'ottava, Snap alla scala,
  Velocity (crescendo, diminuendo, casuale, fissa).
- **Generate** scrive nel pattern di questo canale (sostituendo le sue note; **Ctrl+Z** annulla), nella tonalità scelta in Scale:
  una **progressione di accordi** (dodici, dal pop I-V-vi-IV al jazz ii-V-I e a quelle minori; accordi tenuti, stab sincopati, pulsazione
  di ottavi o arpeggio; uno o due accordi per battuta, disposti in modo che ogni voce si muova il meno possibile) oppure una **linea di
  basso** (fondamentali, ottavi, ottave, fondamentale e quinta, walking, sincopata) su una di quelle progressioni o **seguendo gli
  accordi di un altro canale**: il programma riconosce l'accordo suonato in ogni mezza battuta (la nota più bassa decide fra due che
  vanno ugualmente bene).

**Suonare le note.** La tastiera a sinistra e le note che disegni si possono suonare:

- Premi il pulsante sinistro su un **tasto** e la nota suona finché tieni premuto; il tasto si illumina. Tenendo
  premuto, muoviti in su o in giù per suonare i tasti uno dopo l'altro (glissando).
- Quando **crei** una nota (clic su una cella vuota), la **sposti** o la **ridimensioni**, suona finché il pulsante è
  premuto. Spostandola su un'altra riga cambia la nota che senti. Il tasto destro non suona niente.
- Il suono è quello del canale selezionato: strumento o sample, volume, pan, **Pitch**, traccia del mixer ed effetti.
  Funziona sia con la riproduzione in corso sia ferma. Su un canale sampler ogni pressione suona il sample una volta,
  all'altezza del tasto.
- Se non senti nulla, controlla il pulsante **M** del canale, quello della traccia e il volume master.

**Legato e slide.** Due note dello stesso canale si possono unire: con il **legato** il suono che sta suonando continua e cambia solo
l'altezza, che salta alla nuova nota (nessun nuovo attacco, come in un monosynth); con lo **slide** l'altezza scivola fino alla nuova nota in
al massimo un ottavo (o nella durata della nota, se più breve). La linea disegnata dalla vecchia alla nuova nota mostra il collegamento. Perché
funzioni la nota precedente deve suonare ancora quando parte quella collegata, quindi le due devono toccarsi o sovrapporsi (attivando il legato
una nota che finisce fino a un sedicesimo prima viene allungata; se finisce prima resta un vuoto e la nota collegata suona normalmente, con il
suo attacco). Funziona su canali synth e sampler e viene salvato nel progetto.

Le note si agganciano ai sedicesimi. Un pattern dura una battuta (4 quarti), se non hai scelto un'altra lunghezza (vedi *Pattern*). Una nota synth termina alla sua lunghezza o alla
fine del pattern, quale viene prima. Quando apri un canale la vista scorre fino alle sue note.

## Pattern

Un **pattern** contiene le note di tutti i canali. Il menu dei pattern nella barra sceglie il pattern corrente: il
Channel Rack e il Piano Roll mostrano e modificano quello. Il pulsante **+** accanto offre **New empty pattern**,
**Clone current pattern** e **Pattern length (all patterns)**: 1 (predefinito), 2, 4 o 8 battute; i pulsanti **-** e **+** del Channel
Rack la cambiano di **un quarto** alla volta (da 4 a 64 quarti). La lunghezza vale per tutti i pattern; step
sequencer, piano roll e drum machine si adeguano. Le clip dei pattern restano dove sono (0.42.0): una clip lunga un numero intero di
pattern mantiene quel numero di ripetizioni, le altre mantengono la loro lunghezza. Le note oltre la fine di un pattern più corto restano conservate ma non suonano, e tornano se allunghi di nuovo i pattern. Il
cambio viene rifiutato se la playlist non entrerebbe più nella linea del tempo. I nuovi pattern prendono da soli il nome successivo (Pattern 3, Pattern 4, ...). Il brano dimostrativo
(0.52.0) ha 12 pattern di quattro battute (Piano, Beat, Build, Bass, Marimba, Lead, Pad, Growl, Bells, Choir, Arp, Outro), ognuno sulla
sua traccia.

**Loop Starter (0.30.0).** *Loop Starter...* nel menu del **+** crea un loop di partenza per un brano. Scegli il **genere** (house,
techno, hip-hop, trap, drum & bass, pop, lo-fi, reggaeton, rock o synthwave, ognuno con il suo tempo), la **tonalità**, il **modo**
(minore, maggiore o quello tipico del genere), le **parti** (batteria, basso, accordi e melodia, o meno), se usare il **tempo** del
genere e una **variante** (un numero). *Make it* aggiunge un pattern nuovo che porta il nome del genere e della variante, con le parti
su canali chiamati "House Drums", "House Bass" e così via, ognuno sulla sua traccia del mixer e con un suono adatto: un kit di batteria
incorporato, il pianoforte a coda o il basso elettrico della libreria, o una patch di fabbrica del synth. Il programma passa in
modalità Pattern, così **Play** suona il loop.

Ogni genere mantiene la sua ossatura (cassa in quattro per house, techno e reggaeton; rullante sul 2 e sul 4 per hip-hop, drum &
bass, pop, rock e synthwave; il clap in half-time della trap con i rulli di hi-hat; il dembow del reggaeton) e varia il resto: figure
di cassa, ghost note, hi-hat, fill, la progressione di accordi, lo stile del basso e la melodia (un motivo a cui risponde ogni quarta
battuta, su note dell'accordo all'inizio di ogni battuta, nella tonalità). Lo stesso genere, tonalità e variante danno sempre lo stesso
loop, così uno che ti è piaciuto si può rifare; la finestra ricorda le scelte e propone la variante successiva. Un altro loop dello
stesso genere riusa i suoi canali (con i suoni del nuovo loop) invece di aggiungerne altri. Tutto è fatto con regole e caso, senza un
modello addestrato e senza internet. Annulla toglie il loop.

Il **menu dei pattern** in cima alla Playlist ha tre pulsanti: **New** (vuoto o copia, come il **+**), **Rename** (un nome di
al massimo 40 caratteri) e **Delete**. Delete elimina il pattern selezionato, le sue note e **tutte le clip della playlist che lo
suonano**; l'ultimo pattern rimasto non si può eliminare. Scegliere un pattern in quel menu lo apre anche nel Rack e nel Piano Roll.

Le impostazioni del canale (volume, pan, mute, solo, pitch, swing, strumento, sample, traccia del mixer) appartengono
al canale, non al pattern: sono le stesse in ogni pattern. Le tracce del mixer e i loro effetti non appartengono né
all'uno né all'altro.

## Annulla e ripristina

**Ctrl+Z** annulla e **Ctrl+Y** (o Ctrl+Maiusc+Z) ripristina l'ultima modifica, **di qualunque tipo**: note (Piano Roll, step sequencer, *Clear notes*,
un take di **Rec notes**), impostazioni dei canali (volume, pan, pitch, swing, mute/solo, strumento, sample, nome, traccia del mixer), canali aggiunti,
clonati, spostati o eliminati, modifiche ai kit di batteria, pattern aggiunti, rinominati o eliminati, la lunghezza dei pattern, la playlist, il mixer
(fader, bilanciamento, effetti e loro impostazioni, send, gruppi VCA), tempo e volume master. I due comandi sono anche nel menu File. Tutto ciò che
cambia mentre un tasto del mouse resta premuto (una manopola girata, una nota trascinata, step dipinti) è un solo passo, registrato quando si
rilascia il tasto; si ricordano gli ultimi 100 passi. Aprire o creare un progetto fa partire una nuova cronologia.

Con il mouse sulla Playlist gli stessi tasti usano la cronologia propria della Playlist, più fine, per le modifiche alle clip (i suoi pulsanti
**Undo** / **Redo** fanno lo stesso).

## Progetti FL Studio (.flp)

I progetti si salvano nel formato del programma, **.slp**, come prima. Dalla 0.34.0 il menu **File** sa anche leggere e scrivere i
file **.flp** di FL Studio, con codice scritto per questo programma (non si usa codice né librerie di FL Studio):

- **Open an FL Studio project (.flp)...** ne apre uno come **nuovo progetto senza nome**: il .flp non viene mai modificato e **Save**
  scrive un .slp. Vengono portati: il tempo, i canali (nomi, file dei campioni e la loro nota base), le note di ogni pattern
  (altezza, posizione, durata, velocity), la playlist del primo arrangement (clip di pattern e clip audio sulle loro tracce) e
  nomi e colori delle tracce. I synth e i plugin di FL Studio qui non esistono: le loro note le suona un synth predefinito. Un
  campione il cui percorso salvato non esiste (un percorso Windows, le cartelle di FL Studio) viene cercato per nome accanto al
  .flp e nelle cartelle vicine. Qui i pattern hanno una sola lunghezza (1, 2, 4 o 8 battute: la più corta che contiene il più
  lungo), le tracce della playlist di FL Studio che contengono qualcosa finiscono su altrettante tracce qui (almeno 4, al massimo 128) nel loro ordine e le clip mantengono posizione e
  lunghezza.
- **Export as FL Studio project (.flp)...** scrive il progetto corrente nella struttura di FL Studio 20.8 a 960 PPQ (esatto: il
  doppio dei 480 di questo programma). I canali sampler mantengono il loro file; synth, drum machine, plugin e suoni incorporati
  diventano canali Sampler vuoti che mantengono le note; una clip di pattern ripetuta su più lunghezze diventa una clip per
  ripetizione; le clip audio suonano il loro file dall'inizio. Una clip che parte a metà del suo pattern (dopo uno Split) viene
  scritta dall'inizio del pattern, e il messaggio lo dice.
- In nessuna delle due direzioni passano effetti, mixer, automazioni e impostazioni dei plugin; dopo ognuna un messaggio elenca
  cosa è stato lasciato fuori.
- **Verificato**: i file scritti qui sono stati riletti da questo programma e da un lettore indipendente (PyFLP), ed è stato
  aperto un vero progetto di FL Studio 20.8 dai test di PyFLP. **Non verificato**: l'apertura dei file esportati in FL Studio
  stesso, che qui non è disponibile.

## Importare un file MIDI

*File > Import a MIDI file into patterns...* (oppure trascina un file `.mid` / `.midi` sulla Playlist) trasforma un file MIDI standard in **nuovi
canali synth** (uno per ogni traccia e canale MIDI con note, col nome della traccia) e **nuovi pattern** lunghi quanto la lunghezza dei pattern del progetto, messi uno dopo l'altro
dopo le clip esistenti, ognuno sulla propria traccia della playlist (0.44.0; prima tutti sulla traccia 1). Le note lunghe che superano la fine di un pattern vengono tagliate lì e suonano di nuovo
all'inizio del pattern successivo. Il primo tempo del file imposta il BPM. La riga di stato dice cosa è stato fatto e cosa è stato lasciato fuori.

Lasciati fuori: le tracce di **batteria** General MIDI (canale MIDI 10), i cambi di tempo dopo il primo, controller, pitch bend, cambi di programma; le
tracce che non entrano nei canali liberi (in tutto sono 32); file oltre 8 MB o con più di 200 000 note; file con tempo in frame SMPTE. I canali importati
sono semplici synth: dopo puoi dare loro un sampler o un altro suono.

## Playlist

La Playlist dispone i pattern nel tempo. Una **clip di pattern** (0.49.0) è un blocco trasparente del colore del pattern con
dentro un piccolo piano roll delle sue note, ripetuto quante volte la clip ripete il pattern, così i pattern si riconoscono a
colpo d'occhio; sulle tracce alte il nome sta in una barra sopra le note, su quelle basse sopra le note stesse. Le sue
**tracce** (0.44.0):

- Un progetto nuovo parte con **4 tracce libere** (*Track 1-4*, per registrazioni, file audio o qualsiasi uso) e, sotto, **una
  traccia per ogni pattern**, col suo nome e colore. Ogni pattern creato dopo (*New*, *Clone*, il Loop Starter, un'importazione
  MIDI) aggiunge la propria traccia in fondo e la playlist scorre fino a lei: con 4 pattern ci sono 4 tracce dei pattern + le 4
  libere = 8.
- Un clic in un punto vuoto della traccia di un pattern mette lì **quel pattern**; su una traccia libera mette il pattern scelto
  nel menu dei pattern. Le clip si possono comunque spostare su qualsiasi traccia.
- I pulsanti **-** e **+** nell'angolo in alto a sinistra, sopra i nomi delle tracce, tolgono l'**ultima** traccia e aggiungono
  una traccia libera in fondo. Per togliere una traccia precisa: tasto destro sul nome > *Delete this track* (nello stesso menu
  c'è *Add a track at the end*). Una traccia che contiene clip viene tolta solo dopo una domanda, e le clip se ne vanno con lei;
  **Ctrl+Z** riporta tutto. Resta almeno una traccia; al massimo 128.
- Eliminando un pattern se ne va anche la sua traccia, se non contiene altro; una traccia che contiene ancora clip (una
  registrazione, per esempio) resta, come traccia libera con il vecchio nome del pattern.
- Fino a 8 tracce si dividono l'altezza del pannello; se sono di più scorrono: la barra a destra, la **rotella sopra i nomi delle
  tracce**, o **Alt + rotella** ovunque.
- I progetti salvati prima della 0.44.0 si aprono con le loro 8 tracce com'erano.

La linea del tempo è lunga almeno 32 pattern e almeno 64 battute. Il righello mostra i
numeri di battuta. La linea verticale arancione segna la fine del brano: in modalità Song la riproduzione ricomincia da lì (o dalla zona di
loop, sotto).

### Il righello: posizione del brano e zona di loop

- **Clic sinistro sul righello** sposta lì la posizione del brano (il cursore arancione), **anche mentre il brano suona**: la riproduzione continua
  subito dal nuovo punto. Trascina lungo il righello per scorrere. Il clic si aggancia alla griglia (**Alt**: senza aggancio) e passa alla modalità
  Song. **Stop** torna all'ultimo punto cliccato, quindi Play riparte da lì. Una clip audio che si trova già sotto il cursore parte da quel punto del
  suo suono.
- **Clic destro e trascinamento sul righello** disegna la **zona di loop**, una barra blu con scritto *LOOP* (ombreggiata anche sulle tracce). In
  modalità Song il brano la ripete: quando il cursore arriva alla fine torna all'inizio della zona; se parti dopo la zona, la fine del brano torna
  alla zona. A brano fermo il cursore va all'inizio della nuova zona.
- **Clic destro sulla zona di loop** apre il suo menu: **Move** (la zona segue il mouse; clic per posarla), **Resize** (il bordo più vicino segue il
  mouse; clic per finire) e **Delete loop region**. *Delete* toglie solo l'evidenziazione: nessuna clip, pattern o nota viene
  toccata. Durante *Move* e *Resize* un clic destro o Esc rimettono la zona com'era.
- **Trascinamento col destro sulla zona** la sposta subito; trascinamento col destro su uno dei suoi **bordi** la ridimensiona.
- La zona si aggancia alla griglia scelta in Grid (almeno un sedicesimo) e viene salvata nel progetto. Vale solo per la riproduzione dal vivo in
  modalità Song: le esportazioni producono sempre l'intero brano.

| Azione | Come |
| --- | --- |
| Piazzare una clip (strumento Select) | Clic in un punto qualsiasi di una traccia. La clip parte dal **quadrato della griglia** cliccato (il menu Grid: una battuta, un quarto, un sedicesimo...; con **Alt**, esattamente dove hai cliccato), contiene il pattern scelto nel menu ed è lunga un pattern. Trascina subito a destra per allungarla. |
| Spostare una clip | Trascinala: si muove per **quadrati della griglia** (0.42.0), non per pattern interi, quindi le clip possono partire sfasate tra loro (un quarto o un sedicesimo dopo); tieni **Alt** per nessun aggancio (micro-timing). Può cambiare traccia. |
| Ripetere / accorciare | Trascina il bordo destro della clip, per quadrati della griglia (Alt: liberamente): il pattern si ripete finché dura la clip; una lunghezza che non è un numero intero di pattern si ferma a metà dell'ultima ripetizione (sottili tacche mostrano dove inizia ogni ripetizione). |
| Eliminare | Tasto destro sulla clip > Delete, oppure selezionala e premi Canc (o usa lo strumento Erase). |
| Selezionare il suo pattern | Doppio clic sulla clip. |
| Ingrandire / rimpicciolire | Rotella del mouse (su: ingrandisce, giù: rimpicciolisce), intorno al mouse, in tutta la playlist. |
| Scorrere nel tempo | Shift + rotella, rotella laterale o touchpad, oppure la barra di scorrimento. |

### Strumenti, griglia, zoom e annulla

La barra sotto il menu dei pattern ha cinque strumenti (tasti **1** - **5** con il mouse sopra la linea del tempo):

| Strumento | Cosa fa |
| --- | --- |
| **Select** (1) | Lo strumento normale: clic su una clip per selezionarla, trascina per spostarla, trascina il **bordo sinistro o destro** per accorciarla o allungarla (una clip audio mantiene il suono al suo posto: se ne sente solo di più o di meno), tasto destro per un menu (*Split here, Duplicate, Copy, Delete*, più *Position, gain, pan and fades...*, *Reverse* e *Stretch and pitch...* su una clip audio oppure *Open this pattern* su una clip di pattern), doppio clic su una clip di pattern per aprirla. |
| **Split** (2) | Clic su una clip per tagliarla in due in quel punto (agganciato alla griglia). Il tasto **S** divide la clip selezionata dove si trova il mouse. |
| **Cut** (3) | Trascina su una clip per togliere quel tratto: il suono dopo il vuoto resta dov'era. |
| **Erase** (4) | Clic su una clip per eliminarla, o trascina su più clip. |
| **Zoom** (5) | Il clic ingrandisce, il tasto destro o Maiusc+clic rimpicciolisce, trascinando un tratto si ingrandisce su quel tratto. |

**Grid** (il menu accanto agli strumenti) è la griglia a cui si agganciano le clip audio quando vengono spostate, ritagliate, divise
o tagliate: una battuta, mezza battuta (1/2, 0.32.0), un quarto e così via fino al singolo tick (1 battito = 480 tick); tieni premuto **Alt** mentre trascini per ignorarla. Dalla 0.42.0
anche le clip di pattern si agganciano alla stessa griglia (prima stavano su pattern interi). **-**, **+** e **Fit** rimpiccioliscono, ingrandiscono e mostrano tutto il brano;
la **rotella del mouse** ingrandisce e rimpicciolisce intorno al mouse, Shift+rotella e la barra di scorrimento muovono nel tempo. Il righello mostra
**minuti:secondi.millisecondi** e i numeri di battuta (solo quanti ne stanno allo zoom attuale); le due righe sotto la linea del
tempo indicano, per la posizione sotto il mouse, *tempo - battuta:battito:tick* e, per la clip selezionata, nome, inizio, durata e fine
(in battuta:battito:tick e in tempo), per una clip audio anche durata e frequenza di campionamento del file, il punto del file da cui
parte la clip (in secondi e in campioni) e il guadagno. Su una clip audio, *Position, gain, pan and fades...* (tasto destro) permette di
scrivere valori esatti.

**Impostazioni per clip (0.27.0).** Ogni clip audio ha il suo **pan** (da -100 sinistra a 100 destra: un bilanciamento come quello di
una traccia del mixer, al centro il suono resta com'è), **fade in** e **fade out** (secondi, curve sinusoidali, disegnate sulla clip e
applicate alla forma d'onda) e la **direzione**: *Reverse* nel menu del tasto destro (o *Direction* nella finestra) suona la stessa
parte dell'audio al contrario, e la forma d'onda si gira con lei. Insieme a *Stretch and pitch...* ogni clip audio ha quindi la sua
intonazione, durata, direzione, pan e dissolvenze. Dividere o tagliare una clip lascia i fade ai bordi esterni (il pezzo di sinistra
tiene il fade in, quello di destra il fade out). Fade più lunghi della clip se la dividono in proporzione. Una clip al contrario non
si apre nell'editor dei campioni: prima togli *Reverse*. Le impostazioni si salvano col progetto; i progetti precedenti si aprono senza.

**Annulla / Ripristina** (pulsanti, **Ctrl+Z** e **Ctrl+Y**) coprono tutto ciò che si fa alle clip nella Playlist. **Ctrl+C / X / V**
copiano, tagliano e incollano la clip selezionata dove sta il mouse, **Ctrl+D** la duplica, **Canc** la elimina. La cronologia viene
dimenticata quando qualcosa fuori dalla Playlist cambia la disposizione (apertura di un progetto, una nuova registrazione,
l'eliminazione di un pattern). Split e Cut funzionano anche sulle clip di pattern, su qualsiasi quadrato della griglia (0.42.0): la parte destra prosegue dallo
stesso punto del pattern (ricorda quanto dentro il pattern parte), quindi una clip divisa suona esattamente come prima. Una clip
ottenuta dividendo una clip audio ricorda dove inizia nel suono **in secondi, al BPM a cui è stata tagliata**: se in seguito cambi il
BPM il suono continua a suonare alla velocità originale, quindi i punti di taglio non cadono più sul battito su cui erano stati fatti.

**Le clip possono stare una sopra l'altra sulla stessa traccia** (0.33.0, come in FL Studio): clip di pattern, registrazioni e
file audio si sovrappongono liberamente e suonano tutti; quella aggiunta o spostata per ultima è disegnata sopra ed è quella
che un clic prende (ogni clip ha un contorno, così i bordi di quelle sotto restano visibili). Anche le clip su tracce diverse
suonano insieme.

**Nomi delle tracce** (0.33.0): un clic sul **nome** di una traccia sceglie la traccia in cui scrive **Record** (segnata da un
punto rosso); un **doppio clic** la rinomina, il **tasto destro** apre *Rename...*, **Colour** (dodici colori, o nessuno: il
colore segna il nome e tinge la traccia) e *Default name and colour*. Nomi e colori si salvano con il progetto e si annullano
con Ctrl+Z.

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

### Stretch e pitch

Tasto destro su una clip audio > **Stretch and pitch...** per cambiarne la lunghezza o l'intonazione, o entrambe, in modo **indipendente**:

| Campo | Significato |
| --- | --- |
| Length (%) | 100 = invariata, 200 = lunga il doppio (metà del tempo), 50 = lunga la metà. Da 25 a 400. L'intonazione non cambia. |
| Pitch (semitoni) | Da -24 a +24; i decimali funzionano (0,5 = 50 cent). La lunghezza non cambia. |
| Tempo of the audio (BPM) | Facoltativo. Se conosci il tempo a cui è stato registrato un loop, scrivilo: la lunghezza diventa *suo tempo / tempo del progetto*, quindi un loop a 90 BPM in un progetto a 120 BPM diventa lungo il 75 %. Ha la precedenza sul campo Length. |
| Pitch mode | *Normal*, oppure *Keep the timbre*, che limita quanto si sposta lo spettro sopra gli 8 kHz (un po' meglio su voci e piatti quando si trasporta di vari semitoni). |

**Apply** elabora di nuovo l'audio, una volta sola (qualche secondo per un file lungo) e la clip diventa più lunga o più corta dello stesso fattore;
**Back to the original** torna al suono e alla lunghezza del file; funziona anche **Undo** della playlist. Il file su disco non viene mai modificato.
Una clip modificata lo mostra nel nome: `[x1.50, +3 st]`. Il risultato parte sempre dall'audio originale, quindi due volte 150 % fanno
150 %, non 225 %. Se la clip più lunga supera la fine della playlist, non cambia nulla e il programma te lo dice.

Il file di progetto salva solo i numeri; l'audio viene elaborato di nuovo all'apertura (le clip con lo stesso file e gli stessi
numeri condividono un'unica elaborazione). Dividere una clip modificata lascia entrambe le metà modificate.

Quanto è buono? Il metodo (Signalsmith Stretch) rende meglio tra il 75 % e il 150 % e per trasposizioni di pochi semitoni; agli
estremi (4x, un'ottava o più) il suono si impasta e diventa un po' metallico, soprattutto batterie e note percussive,
perché i transienti non hanno un trattamento separato. È stato misurato su toni di prova (lunghezza, intonazione e tempi sono
esatti entro pochi millisecondi; un attacco netto si sfuma di circa 10-25 ms) e **non giudicato a orecchio su musica vera**.

### Editor di campioni

Tasto destro su una **clip audio** (playlist) o su un **canale sampler** (Channel Rack) > **Edit sample...** apre l'editor. Per una clip mostra esattamente la parte
di audio che la clip suona. Lavora su una copia: fuori non cambia nulla finché non premi il pulsante in basso a destra.

- **Forma d'onda.** Trascina per selezionare; Maiusc+clic estende la selezione; la rotella fa zoom, Maiusc+rotella scorre, Home mostra tutto. **Select all** (Ctrl+A).
- **Anteprima.** *Play* (Spazio) suona la selezione, o dal cursore alla fine se non c'è selezione; *Loop* ripete la selezione. Il suono passa dal motore audio (dopo la catena master,
  quindi non dipende dalle impostazioni del mixer). Lo **spettro** sotto la forma d'onda segue ciò che suona (FFT a 4096 punti, asse delle frequenze logaritmico).
- **Modifiche** sulla selezione (o su tutto se non c'è selezione); ognuna si annulla con *Undo* (Ctrl+Z) e si ripristina con *Redo* (Ctrl+Maiusc+Z o Ctrl+Y):
  **Cut** (Canc; toglie la selezione e chiude il vuoto), **Crop** (tiene solo la selezione), **Silence**, **Reverse**, **Invert** (fase), **Gain...** (dB),
  **Normalise...** (il punto più forte a un livello, predefinito -0,3 dB), **Remove DC** (centra la forma d'onda sullo zero) e **Fade in / Fade out** con una forma:
  *Linear*, *Curve* o *S curve*. Cut e Crop richiedono una selezione e, cambiando la lunghezza, cancellano la selezione e i marcatori.
- **Reduce noise...** Seleziona prima un tratto che contiene **solo il rumore** (almeno circa 0,08 s a 48 kHz, per esempio il silenzio prima dell'inizio della registrazione).
  Il programma misura il rumore in ogni banda di frequenza e abbassa, in **tutto il campione**, ciò che non supera chiaramente il rumore. *Reduction* (da 0 a 60 dB,
  predefinito 12) è di quanto si abbassa il rumore; *Sensitivity* (da 0 a 8, predefinito 3) è di quanto un suono deve superare il rumore per essere tenuto.
  Troppo di entrambi rende il suono "acquoso": prova, ascolta e usa Undo.
- **Slicing**. *Find hits* mette un marcatore all'inizio di ogni colpo, nota o parola che trova; il **cursore Sensitivity** accanto (basso = solo i
  colpi più chiari) decide quanti. *Equal parts...* mette da 2 a 128 marcatori equidistanti. Doppio clic sulla forma d'onda aggiunge un marcatore o toglie quello vicino al clic;
  *Clear markers* li toglie tutti. Con **Save as new file and use** la clip viene tagliata nei marcatori in clip separate della playlist (un solo passo di Undo), che poi puoi spostare, ripetere o eliminare.
  **Slices to sampler channel** le trasforma invece in un nuovo canale sampler con una nota per slice (vedi *Sampler e SoundFont*); funziona anche sul sample di un canale sampler.
- Il pulsante in basso a destra (**Save as new file and use**) scrive il risultato come **nuovo file WAV a 32 bit float in `Samples/Edited`**
  accanto ai tuoi campioni (se il nome esiste si aggiunge un numero: una modifica precedente non viene mai sostituita) e lo mette sulla clip o nel canale. Il file
  originale non viene mai toccato e il file di progetto punta semplicemente al nuovo. **Cancel** (o chiudere la finestra) scarta la modifica.

Da sapere: una clip **allungata o trasposta** va prima riportata com'era con *Stretch and pitch... > Back to the original*, modificata e poi di nuovo allungata. Dopo il salvataggio una clip
suona tutto l'audio modificato dall'inizio, finché c'è posto sulla sua traccia (si accorcia se un'altra clip è in mezzo; il tempo è quello attuale). Si può aprire un solo editor alla
volta. Il sample di un canale sampler non si può tagliare in clip (non ci sono clip), ma le sue slice possono diventare un nuovo canale sampler.

La riduzione del rumore e il rilevamento dei colpi sono stati misurati su segnali di prova (rumore abbassato di circa 18 dB con un tono mantenuto entro 1,5 dB; cinque colpi di prova
trovati entro 0,2 ms) e **non giudicati ad orecchio su registrazioni reali**: lo spectral gating scambia sempre rumore con una certa perdita del dettaglio più debole del suono.

### Editor di intonazione (0.38.0)

Tasto destro su una clip audio > **Edit pitch...** mostra una registrazione di **una sola voce o strumento alla volta** (un cantante,
un basso, una linea solista) come **note sopra la curva della sua intonazione**, come Newtone di FL Studio: ogni nota all'altezza a cui
è stata cantata, con il tasto più vicino e di quanti cent se ne discosta (nomi come nel piano roll: C5 = MIDI 60).

| Azione | Come |
| --- | --- |
| Spostare una nota | Trascinala su o giù: di semitoni; tieni **Alt** per muoverla liberamente. La linea sottile mostra dove era cantata |
| Intonare una nota | Doppio clic: va al tasto più vicino |
| Intonarne diverse | Selezionale (clic, **Shift+clic** per aggiungere) - o nessuna per tutte - e premi **Correct**: **Amount** 100 % le porta sui loro tasti, 50 % a metà strada; con una tonica e una scala vanno al tasto più vicino **della scala** |
| Annullare le modifiche | **Reset** (le note selezionate, o tutte) |
| Ascoltare | **Play** suona l'audio modificato |
| Tenerlo | **Save as new file and use**: un nuovo WAV in `Samples/Edited`, che la clip poi suona (il file originale non viene mai toccato; Ctrl+Z nella playlist riporta l'audio di prima) |
| Dall'intonazione al MIDI | **Notes to pattern**: le note diventano le note del **canale selezionato** nel pattern corrente (le sue note lì vengono sostituite; Ctrl+Z annulla), ognuna sul tasto più vicino, dall'inizio del pattern, le più forti con velocity più alta |

Come funziona: l'intonazione si misura ogni 5 ms (YIN), le note sono i tratti in cui resta stabile, e ogni nota spostata viene
trasposta da sola (stessa lunghezza, dissolvenze di 10 ms ai bordi); l'audio tra le note non cambia. Cosa non sa fare: accordi o più
voci insieme, registrazioni molto rumorose, appiattire il vibrato o le derive dentro una nota (una nota si sposta tutta insieme),
cambiare la durata di una nota. Una clip allungata, trasposta o invertita va prima riportata all'originale (come per l'editor di campioni).

### Stem: batteria, basso e il resto (0.39.0)

Tasto destro su una clip audio > **Split into stems...** divide un mix in tre parti, **senza intelligenza artificiale** e sul tuo
computer:

- **Drums**: ciò che è breve e largo in frequenza (i colpi);
- **Bass**: ciò che è stabile e basso (sotto circa 150 Hz);
- **Other**: ciò che è stabile e più alto (voci, accordi, soli).

Una finestra mostra l'avanzamento (pochi secondi per minuto di audio; *Cancel* la ferma). Le tre parti vengono salvate come WAV in
`Samples/Stems` e sostituiscono la clip: Drums sulla traccia della clip, Bass e Other sulle due successive (quelle sopra, in fondo
alla playlist), tutte nello stesso punto. **Sommate danno esattamente l'originale**, quindi suonando le tre si sente quello che
suonava la clip; **Ctrl+Z** riporta la clip originale.

Come funziona: separazione armonico-percussiva con filtri mediani sullo spettro (Fitzgerald, 2010), poi un incrocio di frequenza
per la parte stabile. Cosa non sa fare: distinguere una voce da una chitarra (vanno entrambe in Other), o togliere da Drums
l'attacco di un basso pizzicato. Rende meglio su mix chiari, elettronici o acustici; una rete neurale fa di meglio, ma non è ciò che
usa questo programma.

### Registrazione

**Record** (barra) registra l'**ingresso audio**: un microfono, una chitarra in un'interfaccia audio, un ingresso di linea - su una
traccia o, dalla 0.36.0, su **più tracce insieme**, ognuna dal proprio ingresso.

1. In **Audio...** scegli il dispositivo di ingresso; parla o suona e guarda la barra **Input level**. (Premendo Record si
   accendono gli ingressi che servono alle tracce armate, se sono spenti.)
2. **Arma** le tracce da registrare: ogni traccia della playlist ha un **pallino di registrazione** dopo il nome. Clicca il
   pallino (o Ctrl+clic sul nome) per armare o disarmare la traccia: le tracce armate hanno il pallino rosso. Una traccia resta
   sempre armata. Con una sola traccia armata, un clic sul nome di un'altra sposta lì l'armamento; con più tracce armate, la
   seleziona soltanto.
3. Scegli **l'ingresso di ogni traccia**: tasto destro sul nome della traccia > *Recording input...* (o la freccia accanto a
   Record > *Input of each track*): un ingresso (file a un canale: un microfono, una chitarra) o due ingressi vicini (file
   stereo). I nomi vengono dal dispositivo audio quando un ingresso è aperto; prima, si offrono gli ingressi 1-8 per numero. Una
   traccia nuova registra l'ingresso 1.
4. Premi **Record**. Il brano suona in modalità Song e la riga di stato mostra il tempo, il livello d'ingresso più alto e le
   tracce che saturano. Premi di nuovo **Record** o **Stop** per finire; ogni ripresa compare come clip audio sulla sua traccia
   ed è conservata anche come WAV a 24 bit in `~/Music/Sun Loops Studio/Samples/Recordings/` (con *T1*, *T2*... nel nome quando
   si sono registrate più tracce).

La freccia accanto a Record offre anche:

- **The recording becomes**: clip sulle tracce armate, o *New sampler channels* (uno per traccia armata, da suonare dal piano
  roll).
- **The takes start**: *At the start of the song* (il brano riparte dall'inizio), o *Where the song is (punch in)*: dal
  marcatore di inizio della playlist, o da dove il brano sta suonando se è già in riproduzione. Le riprese vengono messe dove
  sono partite.
- **Count-in** (0.47.0): *None*, *1 bar* o *2 bars* di metronomo (nel metro del brano, al tempo iniziale) prima che partano brano e
  riprese; il metronomo non deve essere acceso. La riga di stato dice *Count-in...*; Record o Stop lo annullano.
- **With a loop region in Song mode** (0.47.0):
  - *Keep recording: every pass is a take* (predefinito): il brano gira nella regione e la registrazione continua. Alla fine la
    clip copre la regione e suona l'**ultimo passaggio completo**; tasto destro sulla clip > **Take** ne sceglie un altro
    (*Take 1*, *Take 2*...). Un passaggio conta se in esso è stata registrata tutta la regione (il primo solo se la ripresa è
    partita prima della regione o al suo inizio).
  - *Stop at the end of the region (punch out)*: la ripresa si ferma quando il brano torna indietro e la sua clip finisce
    esattamente alla fine della regione.
- *Hear the input while recording* (usa le cuffie: con casse e microfono si crea un fischio; questo ascolto aggiunge la latenza
  del dispositivo e riproduce solo gli ingressi 1 e 2).

Tutte le riprese partono nello stesso blocco audio, quindi sono allineate tra loro, e sono spostate della latenza di andata e
ritorno dichiarata dal dispositivo, per allinearle a ciò che suonava. Una ripresa si ferma da sola alla fine della playlist (con
le take in loop: dopo 5 minuti), o alla fine della regione di loop con il *punch out*. Un ingresso che il dispositivo non ha
viene segnalato e non si registra nulla.

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

**Score log (0.28.0).** Tutto ciò che suoni su una tastiera MIDI viene ricordato per **cinque minuti**, anche quando non si sta
registrando e il brano è fermo, così un'idea suonata mentre provavi non va persa. Tasto destro su **Rec notes**: *Put the last
phrase into the pattern* prende ciò che hai suonato dopo l'ultima pausa di almeno 3 secondi (senza tasti premuti), *Put all of it
into the pattern* prende tutto il registro e *Clear the score log* lo cancella. Le note vanno nel **pattern corrente**, ognuna sul
canale su cui è stata suonata, a partire dall'inizio del pattern e con il tuo tempo al BPM attuale (gli attacchi vanno sulla griglia
di quantizzazione scelta nello stesso menu). Le note che iniziano dopo la fine del pattern vengono lasciate fuori e contate nella
riga di stato: allunga il pattern o prendi solo l'ultima frase. Annulla toglie tutto l'inserimento in un colpo.

La freccia accanto a **Record** ha *The recording becomes*: **A clip on a playlist track** (predefinito) oppure **A new sampler
channel**. Con la seconda, una registrazione da microfono o chitarra diventa il sample di un nuovo canale, pronto da suonare con una
tastiera MIDI o da sequenziare nel piano roll (senza canali liberi viene messa nella playlist).

## Modalità Pattern e Song

Il pulsante **Pattern / Song** decide cosa fa Play.

- **Pattern**: ripete il pattern corrente. Il cursore di riproduzione corre nel Piano Roll e nel Rack.
- **Song**: suona la Playlist dall'inizio (o dal punto cliccato sul righello) alla fine dell'ultima clip, poi ricomincia, oppure ripete la zona di
  loop se c'è. Il cursore corre nella Playlist e resta visibile da fermo (mostra da dove partirà Play). Una playlist vuota è silenziosa.

In entrambe le modalità puoi modificare mentre suona; il cambiamento si sente subito.

### Metro e cambi di tempo (0.46.0)

Il pulsante accanto ai BPM mostra il **metro** (4/4 all'inizio). Cliccalo:

- **Time signature**: 2/4, 3/4, 4/4, 5/4, 6/4, 7/4, 6/8 o 12/8. Le battute di Playlist e Piano Roll e le posizioni
  (battuta.battito.tick) lo seguono, il metronomo accenta il primo battito di ogni sua battuta e lo snap **Bar** vale una sua
  battuta. In 6/8 e 12/8 un battito è un ottavo. Un pattern dura almeno una battuta di 4/4, oppure una battuta se la battuta è
  più corta (2/4 permette 2 quarti, 3/4 e 6/8 ne permettono 3); l'etichetta del rack dice quante battute del metro sono.
- **Cambi di tempo**: *Add a tempo change at the song position...* (dove hai cliccato il righello, sul suo battito; alla battuta
  2 se la posizione è all'inizio) chiede i BPM. Da lì il brano suona a quel tempo, fino al cambio successivo. Ogni cambio è una
  bandierina arancione con il suo tempo nel righello, e i secondi sotto il righello seguono i tempi. Nel menu ogni cambio si può
  modificare, spostare alla posizione del brano o eliminare; *Delete every tempo change* torna a un solo tempo. Un `*` sul
  pulsante indica che il brano ha cambi di tempo.

I cambi sono a gradino, come i tempo marker di FL Studio (niente rampe graduali), e li suona solo la **modalità Song**: un
pattern suona ai BPM della barra, che sono anche il tempo con cui parte il brano. Annulla e ripeti li comprendono, si salvano
con il progetto e le esportazioni ne tengono conto. Le **clip audio** sono ancora disegnate e allungate per il tempo iniziale
del brano: dopo un cambio di tempo una clip audio non lo segue (tieni l'audio prima del primo cambio, o usa lì dei pattern).

## Automazione

L'automazione fa muovere un controllo da solo mentre il brano suona. Funziona su:

- **volume** e **pan** di un canale (i due slider del Channel Rack);
- **fader** e **bilanciamento** di una traccia (Mixer);
- ogni **parametro di un effetto** in uno slot di traccia o del master (gli slider del pannello effetti);
- ogni **parametro di un plugin** (VST3, LV2, LADSPA): di uno strumento plugin nel Channel Rack e di un effetto plugin in uno slot di
  traccia o del master (dalla 0.35.0, vedi *Parametri dei plugin* più sotto);
- dalla 0.47.0: i **send** di una traccia verso Aux A e Aux B (i piccoli slider A / B), **fader** e **bilanciamento** di un bus aux e
  il fader di un **gruppo VCA**.

Mentre il brano suona in modalità Song, i controlli che hanno un'automazione **si muovono con essa** (0.47.0): fader,
bilanciamento e send del Mixer, volume e pan del Channel Rack. Quando il brano si ferma tornano a mostrare i propri valori
(l'automazione non li cambia mai). Toccare un controllo mentre si muove ne imposta il valore, come sempre.

**Non ancora automatizzabili:** le manopole Pitch e Swing, il volume master, mute e solo, i parametri degli effetti di un bus aux,
il tempo (usa i cambi di tempo, 0.46.0) e tutto il resto.

**Creare una clip.** Tasto destro sul controllo e *Create automation clip*. Una clip lunga un pattern compare su una delle quattro
**corsie di automazione** (*Auto 1-4*, sotto le tracce della Playlist) e si apre il suo editor. La clip è una linea piatta al
valore attuale del controllo. Parte dal pattern sotto il cursore se il brano suona in modalità Song, altrimenti dall'inizio del brano. Se tutte e quattro le corsie sono
già occupate in quel punto (o esistono 64 clip), non viene creato nulla e la barra di stato lo dice; sposta o elimina una clip e riprova.

**Nelle corsie** le clip si comportano come le altre clip della Playlist: trascina per spostare, trascina il bordo destro per allungare o
accorciare, *Duplicate* (Ctrl+D) e *Delete* dal menu del tasto destro o dalla tastiera, lo strumento Erase ne toglie una, Zoom ingrandisce. La clip mostra
il nome (per esempio *Track 1 Reverb Mix*) e un disegno della curva. Le clip di una corsia non si sovrappongono. Tutto si annulla
con l'Undo della Playlist.

**L'editor dei punti** (doppio clic su una clip, oppure tasto destro > *Edit the points...*):

| Azione | Come |
| --- | --- |
| Aggiungere un punto | Clic su uno spazio vuoto |
| Spostare un punto | Trascinalo. Il tempo si aggancia a un sedicesimo; tieni premuto **Alt** per muoverlo liberamente (i punti possono stare a una frazione di tick). Il valore è sempre libero. Un punto non può scavalcare i vicini |
| Forma della linea dopo un punto | Tasto destro sul punto: *linear*, *hold* (a gradino) o *smooth* (curva a S) |
| Eliminare un punto | Tasto destro > *Delete point*, oppure selezionalo e premi Canc o Backspace (una clip tiene almeno un punto) |

L'asse a sinistra mostra l'unità del controllo (dB per i volumi, numeri semplici, Hz, ms...). Una linea gialla segna il valore predefinito.

**Come suona.** L'automazione si sente **solo in modalità Song**; in modalità Pattern i controlli funzionano come sempre. Per ogni parametro decide la clip partita per
ultima; al suo interno il valore segue la curva, dopo la fine resta l'ultimo valore, prima della prima clip vale il valore del controllo. Mentre il brano suona con automazione i **controlli sullo schermo non si muovono**
(mostrano il valore che hai impostato) e toccarne uno non ha effetto duraturo su un parametro che ha una clip sotto il cursore. Il valore si aggiorna
ogni 64 campioni (1,3 ms a 48 kHz): rapido per qualsiasi uso musicale, ma non è una rampa campione per campione. Le clip si salvano nel progetto.

### Parametri dei plugin

I controlli di un plugin stanno nella sua finestra, dove non c'è il nostro menu del tasto destro, quindi i parametri si scelgono da un elenco:

- uno **strumento plugin**: tasto destro sul canale nel Channel Rack > *Automate a plugin parameter...* (grigio per gli altri
  strumenti e per un plugin senza parametri o non trovato);
- un **effetto plugin**: apri il Mixer, seleziona lo slot che lo contiene, poi *Automate a parameter...* sotto *Open plugin window*
  (non per i bus aux, come per gli altri effetti).

L'elenco mostra tutti i parametri del plugin con il valore attuale, scritto come lo scrive il plugin. Scrivi nella casella in alto per
filtrarlo per nome (più parole: devono esserci tutte). **Muovi prima un controllo nella finestra del plugin** e quel parametro è già
scelto quando l'elenco si apre: la riga sopra la casella lo nomina (il "last tweaked" di FL Studio). Doppio clic su una riga, Invio
o *Automate*. La clip parte dal valore attuale del parametro; il suo editor mostra i valori del plugin (On / Off, dB, Hz...) quando il
plugin li fornisce, altrimenti 0-100 %.

Note: molti plugin elencano anche parametri tecnici (i plugin VST3 fatti con JUCE aggiungono voci *MIDI CC* e un *Bypass*); il filtro
è il modo più rapido per superarli. Una clip il cui plugin manca quando il progetto si apre, o il cui parametro il plugin non ha più,
resta nel progetto e non fa nulla. La finestra del plugin può non seguire l'automazione mentre il brano suona, come gli altri controlli.

## Mixer

Premi **F9** o il pulsante **Mixer F9**. Il Mixer si apre grande sopra gli altri pannelli; F9 di nuovo lo chiude. Ha
8 **tracce** verticali all'inizio, la strip **master** a destra e il **pannello effetti** della traccia selezionata.

**Più tracce (0.29.0).** Il pulsante **+** dopo l'ultima traccia ne aggiunge una; con il tasto destro se ne aggiungono 10 o 100 alla
volta, oppure si toglie l'ultima (solo se nessun canale ci suona, non ha effetti e nessuna automazione la controlla). Un progetto può
avere fino a **500** tracce; scorri il mixer in orizzontale per raggiungerle (aggiungendo, il mixer scorre a quella nuova). Aggiungere
e togliere si annulla con Annulla, e il file del progetto ricorda il numero. Le tracce senza suono e senza effetti non costano quasi
nulla. Riverberi, delay, chorus e Pitch Corrector hanno bisogno di circa 0,8 MB di memoria ciascuno, che viene preparata man mano che
si aggiungono: nel primissimo momento dopo averne aggiunti tanti insieme, gli ultimi possono far passare il suono senza effetto finché
la loro memoria non è pronta (qualche decimo di secondo).

Percorso del suono: ogni canale va alla traccia scelta con il suo display **Track N** (più canali possono condividerne
una; oppure usa *Assign free mixer track* nel menu del tasto destro del canale), poi attraversa i dieci slot effetti (quattro prima della 0.45.0)
della traccia, poi fader e bilanciamento della traccia; la somma di tutte le tracce passa poi dai dieci slot effetti
del master e dal fader master.

Ogni traccia, dall'alto in basso:

- il nome (Track 1...) e i canali che le sono assegnati;
- **dieci slot effetti** (0.45.0; quattro prima), uno sopra l'altro (vedi sotto);
- **bilanciamento** (stereo) - doppio clic per centrarlo. È un bilanciamento: al centro il suono non cambia;
- **M** (mute) e **S** (solo) - appartengono alla traccia e sono indipendenti dai pulsanti del canale nel rack. Se
  una traccia è in solo, si sentono solo quelle in solo;
- un **fader** verticale con la lettura in dB (dal silenzio a +3,5 dB, 1,0 = 0 dB; doppio clic per tornare a 0 dB);
- un **indicatore di livello**: la barra larga è il livello RMS (media su circa 300 ms), la barra stretta è il picco,
  con una linea di tenuta e una spia in alto che si accende se il segnale ha raggiunto o superato 0 dB. Clic
  sull'indicatore per spegnere la spia. La scala va da -60 a +6 dB.

Clic su una traccia per selezionarla: il pannello effetti mostra allora i suoi slot.

### Effetti

**Catene di effetti (0.45.0).** Il pulsante **Chain...** del pannello effetti salva gli slot della traccia selezionata come preset
(*Save this chain as a preset...*, un file `.slchain` in `~/Music/Sun Loops Studio/Presets/Effect chains`), ne carica uno su
qualsiasi traccia, bus aux o master (sostituisce tutti gli slot) o svuota tutti gli slot. I preset conservano gli effetti interni con
impostazioni e bypass; gli slot con plugin vengono lasciati fuori (la riga di stato lo dice).

Clic su uno slot per il menu: gli effetti, a gruppi (**EQ and filter**, **Dynamics**, **Drive**, **Modulation**, **Space and time**,
**Stereo**), poi **Plugin effects**, **Bypass** e **Remove effect**. Gli effetti agiscono in ordine, dall'alto in basso, prima del
fader della traccia. Il pannello effetti ha un pulsante di accensione per slot (bypass), un cursore per ogni parametro dello slot
selezionato (una scelta come *Type* o *Mode* scorre tra i suoi nomi; doppio clic su un cursore per il valore predefinito) e, per
quasi tutti gli effetti, **Presets...** con impostazioni pronte.

| Effetto | Parametri | Preset |
| --- | --- | --- |
| Equalizer | Shelf basso (120 Hz), Mid (una campana con frequenza propria, 200-5000 Hz), shelf alto (6 kHz); ciascuno da -15 a +15 dB | |
| Parametric EQ | Shelf basso (frequenza, guadagno), una campana (frequenza, guadagno, Q), shelf alto (frequenza, guadagno), ciascuno da -18 a +18 dB; uscita | Vocal presence, Kick punch, Warm master |
| Filter | Tipo (passa-basso, passa-alto, passa-banda, notch), Cutoff, Risonanza, Pendenza (12 o 24 dB), Drive, velocità e profondità dell'LFO (sposta il cutoff fino a 3 ottave), Mix | Low-pass 24 dB, High-pass, Wobble, Telephone |
| Compressor | Threshold (da -60 a 0 dB), Ratio (1-20:1), Attack, Release, Makeup, **Sidechain** (0.45.0: *Off*, oppure una traccia del mixer il cui suono - prima dei suoi effetti e del fader, quindi funziona anche una traccia muta - comanda la riduzione: il classico basso che "pompa" sotto la cassa) | Gentle, Drum bus, Vocal (un preset non cambia il sidechain) |
| Maximizer | Un limiter da mastering in tre stadi: guadagno d'ingresso, Soft clip facoltativo, Glue (un compressore morbido a ginocchio dolce), poi un limiter con look-ahead che tiene ogni picco sotto il **Ceiling**; Release. Ritarda il suono di 1,5 ms (il suo look-ahead). | Transparent, Loud, Streaming (-1 dB) |
| Transient | Divide ogni suono in attacco e sustain: guadagno di Attack e Sustain (da -24 a +24 dB), Speed (quanto dura un attacco), Listen (entrambi, solo transienti, solo sustain), Mix, Output | Punchy drums, Tighten, More room, Transients only |
| Overdrive | Drive, Boost (x10), Color (più scuro / più brillante prima della saturazione), Tone (dopo), Character (aggiunge armoniche pari), Mix, Output. Lavora a 4 volte la frequenza di campionamento con anti-aliasing. | Warm, Crunch, Fuzz, Parallel grit |
| Chorus | Rate, Depth, Delay, Feedback (feedback alto e delay corto fanno un flanger), Voci (1-3), Width, Mix | Subtle, Wide, Ensemble, Flanger |
| Phaser | Rate, Depth, frequenza centrale, Feedback, Stadi (4, 6, 8, 12), Width, Mix | Slow sweep, Fast, Deep 12 |
| Reverb | Size, Damping, Width, Mix, **Type** (Hall, Room, Plate), Pre-delay (0-250 ms) | Hall, Large hall, Room, Small room, Plate, Vocal plate |
| Delay | Time (10-1000 ms), Feedback, Tone, Mix, **Mode** (Stereo, Ping-pong: le ripetizioni rimbalzano tra i lati, Tape: ripetizioni più scure e sature), Spread (il lato destro fino a 1,5 volte più tardi), Wobble (wow e flutter da nastro) | Stereo 1/8, Ping-pong 1/4, Tape echo, Slapback, Dub space |
| Stereo Imager | Width (0-200 %), guadagno di Mid e Side, Mono bass (sotto questa frequenza il suono diventa mono), Balance | Wider, Mono bass, Mono |
| Time Shaper (0.25.0) | Nello stile di Gross Beat, sincronizzato alla song. **Time**: None, Half speed, Reverse (ogni ciclo suona al contrario quello prima), Tape stop, Slow down, Stutter 1/8 e 1/16, Repeat 1/4 (il primo quarto del ciclo quattro volte), Scratch. **Volume**: None, Gate 1/8, 1/16, 1/32, Trance gate, Pump (tipo sidechain), Fade in, Fade out. **Cycle** (da 1 movimento a 2 battute) è la durata del pattern di tempo; **Depth** del pattern di volume; **Smooth** (dissolve i salti e ammorbidisce i gate); Mix. Fino a quattro Time Shaper in un progetto suonano i pattern di tempo (ognuno ha bisogno di una registrazione degli ultimi 8,5 s); gli altri modellano solo il volume, e dalla 0.41.0 il pannello effetti lo dice (in arancione) quando ne selezioni uno | Half-time bar, Reverse every bar, Tape stop, Stutter 1/16, Beat repeat, Scratch, Trance gate, Gate 1/16, Sidechain pump, Half-time gated |
| Pitch Corrector (0.26.0) | Nello stile di Pitcher, per una voce o uno strumento alla volta (non accordi). **Key** e **Scale** (le scale del piano roll): ogni nota viene portata alla nota più vicina della scala. **Speed**: quanto in fretta (0 ms = subito, il suono "intonato" netto; 50-100 ms suona naturale e lascia passare il vibrato). **Amount** della correzione. **Shift** sposta tutto in su o in giù (fino a un'ottava, anche a frazioni di semitono). **Range** della voce: Low (40-400 Hz), Voice (70-900 Hz), High (150-2000 Hz). Mix (con Shift e Mix al 50 %, un'armonia). Il suono corretto arriva 10-15 ms dopo (il tempo che serve per sentire l'intonazione); le formanti si spostano con l'intonazione, quindi spostamenti grandi suonano come un'altra voce. Respiri, consonanti e rumore passano invariati. **Follow** (0.41.0): *Scale* (come sopra), oppure un **canale**: la voce viene portata alla nota che quel canale sta suonando, nell'ottava più vicina alla voce (come Pitcher con il MIDI); finché il canale non suona nulla, la voce resta com'è. Metti le note da cantare in un pattern di un canale strumento (abbassane il volume se non vuoi sentirlo) | Natural (chromatic), Hard tune (chromatic), C major fast, A minor natural, A minor pentatonic hard, Bass chromatic, Octave up, Fifth harmony |

I progetti fatti prima della 0.21.0 suonano uguali: i loro riverberi sono Hall senza pre-delay e i delay sono Stereo. I file con i
nuovi effetti non si possono leggere con le versioni precedenti (lì lo slot torna vuoto).

Sono effetti propri di Sun Loops Studio. Il menu dello slot ha anche **Plugin effects** (i plugin effetto trovati dall'ultima
scansione, vedi *Plugin*) e **Open plugin window** per uno slot con un plugin; i parametri di un plugin sono nella sua finestra, non nel pannello effetti.

### Effetti sul master

La **strip master** (a destra delle tracce) ha gli stessi dieci slot effetti di una traccia. Clicca uno slot per
scegliere un effetto (tutti quelli sopra, un Maximizer in fondo per il mastering); cliccando la strip o uno slot lo vedi nel pannello
effetti, intitolato *Effects - Master*. La catena master elabora la **somma di tutte le tracce**, in ordine, **prima
del fader master** e del limitatore morbido finale, quindi un compressore o un equalizzatore lì modella tutto il mix,
e il WAV esportato lo include. Serve per compressione di insieme, un EQ finale o un filo di riverbero su tutto. Gli
slot si possono escludere (bypass) o rimuovere come quelli di una traccia, e la catena viene salvata nel progetto.

Il volume master è il cursore della barra degli strumenti e il fader master è lo stesso controllo. La strip master ha un
indicatore.

### Send, bus aux e gruppi VCA

Sotto i suoi slot effetti ogni traccia ha:

- **A** e **B**: quanta parte della traccia va a **Aux A** e **Aux B** (tutto a sinistra: niente; 1,5 tutto a destra). Doppio clic su uno slider
  per spegnere il send;
- **Pre**: se acceso, i send prendono il suono **prima** del fader, del bilanciamento e del VCA (il livello del send non segue il fader). Spento
  (predefinito), sono **post-fader**. Una traccia in mute, o zittita dal solo di un'altra, non manda niente in nessun caso;
- **V1** e **V2**: mettono la traccia nel gruppo VCA 1 e/o 2.

I **bus aux** (le due strip dopo le tracce, con la barra verde) sommano ciò che le tracce mandano, lo fanno passare dai loro dieci slot effetti
(clic su uno slot come su una traccia), poi da fader e bilanciamento, e aggiungono il risultato al mix **prima degli effetti master**. Uso tipico: un riverbero
su Aux A, un delay su Aux B (imposta il Mix dell'effetto al 100 %), e ogni traccia manda quanto serve. Un bus aux ha mute e bilanciamento ma niente
solo, e il nome è fisso (Aux A, Aux B). I parametri dei suoi effetti non si possono ancora automatizzare.

I **gruppi VCA** (le due strip con la barra blu) hanno fader e mute. Il fader moltiplica il fader di ogni traccia membro (da 0 a +3,5 dB); il mute le zittisce.
Il VCA non muove i fader dei membri, li scala: un send post-fader lo segue, uno pre-fader no, e una traccia in due gruppi è scalata da entrambi. La strip mostra
quante tracce ne fanno parte.

Non c'è una finestra a parte per la matrice di routing: ogni strip di traccia è una riga della matrice (i suoi due send e i due interruttori VCA), le strip dei
bus e dei gruppi sono le colonne. Un bus non si può mandare a un altro bus (niente anelli di feedback) e non c'è sidechain. Send, bus e gruppi VCA si salvano nel progetto.

Con i bus il mixer è più largo: se il pannello è troppo stretto le strip scorrono di lato (c'è una barra sotto); doppio clic sulla barra del titolo del pannello
per farlo grande quanto la finestra.

## Misuratori

Il pulsante **Meters** (seconda riga della barra degli strumenti) apre una piccola finestra che resta sopra le altre (si chiude con la x o con il pulsante).
Misura ciò che il programma **manda alle casse**: dopo il volume master, con suono di avvio e metronomo inclusi, e non coincide con il file esportato (l'esportazione
viene generata a parte e non è misurata).

**Loudness (LUFS).** Secondo ITU-R BS.1770-4 ed EBU R128:

- *Momentary* (gli ultimi 400 ms), *Short-term* (gli ultimi 3 s);
- *Integrated*: tutto il brano dall'ultimo **Reset** (o dall'avvio), ignorando il silenzio (sotto -70 LUFS) e tutto ciò che sta più di 10 LU sotto la media.
  Diventa **verde** entro 1 LU dal riferimento EBU di -23 LUFS;
- *Loudness range* (LRA) in LU, come EBU Tech 3342: quanto varia la loudness a breve termine (dal 10° al 95° percentile, escludendo silenzio e passaggi 20 LU sotto
  la media). Un tono costante dà 0.
- *True peak* in dBTP: il livello massimo del segnale come lo ricostruirebbe un convertitore (sovracampionamento 4x), che può superare il campione più alto. Diventa rosso sopra -1 dBTP.

Le letture di loudness sono verificate nei test automatici con i segnali di riferimento EBU (non con uno strumento hardware); il filtro del true peak è preciso a circa 0,05 dB su un tono
con dissolvenza d'ingresso e un attacco senza dissolvenza può leggere fino a 0,4 dB in più, perché un click sfora davvero.

**Spettro.** FFT a 4096 punti (finestra di Hann) del mix sinistra+destra, da 20 Hz a 20 kHz su asse logaritmico e da 0 a -90 dB, aggiornato 30 volte al secondo; il riempimento
scende di 36 dB al secondo, la linea sottile è il massimo tenuto (il pulsante *Peak hold* lo spegne, *Reset* lo azzera).

**Goniometro e correlazione.** Gli ultimi 1024 campioni come punti: un suono mono è una linea verticale, un suono a sinistra pende a sinistra, un suono ampio riempie il quadrato. La barra mostra
la correlazione dei due canali (+1 mono, 0 non correlati, -1 fasi opposte: una barra rossa avverte che il suono può sparire su una cassa mono).

## Metronomo

Il pulsante **Metronome** accende o spegne un click. Tasto destro per un piccolo pannello con il **tempo** (lo stesso BPM
della barra), il **volume** e il pulsante **Tap tempo**: battilo a tempo con un brano (quattro o più colpi) e il BPM lo
segue. Il click è più alto e più forte sul primo battito di ogni battuta e più basso sugli altri (la battuta del metro, 0.46.0). Mentre il brano
suona segue il brano; da fermo tiene il tempo da solo al BPM, così puoi esercitarti o registrare. Il metronomo non fa mai
parte di un'esportazione. Viene ricordato tra una sessione e l'altra.

## Esportazione

**File > Export audio** scrive la modalità corrente: il pattern (una battuta, se non l'hai cambiata) o il brano (fino alla fine dell'ultima clip,
clip audio comprese), più un secondo di coda per non tagliare i rilasci e le code della batteria. Viene generato in
stereo, al BPM impostato nella barra, dallo stesso motore che suona, quindi suona come ciò che ascolti. Scegli il formato
nel sottomenu; poi le **Export options** (0.45.0, ricordate) chiedono:

- **Sample rate**: 44100, 48000 (predefinito), 88200 o 96000 Hz.
- **Bit depth** (WAV, AIFF, FLAC): 16 bit (con dither TPDF), 24 bit o 32 bit in virgola mobile (WAV). **Quality** (MP3, AAC, Ogg
  Vorbis): da 96 a 320 kbit/s.
- **Title** e **Artist**, scritti in WAV (lista INFO), Ogg Vorbis (commenti), MP3 e AAC (tag, tramite ffmpeg); FLAC e AIFF non
  ne hanno.
- **One file per mixer track (stems)**: invece del mix, un file per ogni traccia del mixer che suona qualcosa, chiamato
  *<nome> - <traccia>*, ognuno generato con solo quella traccia in solo (mandate ed effetti master compresi, come nel mix); le
  tracce mute vengono saltate. Sommati danno il mix (finché gli effetti master non sono compressori o limiter).

La tabella indica i valori predefiniti:

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

- Fino a 32 canali, fino a 500 tracce del mixer (8 all'inizio) con 10 slot effetti ciascuna, 2 bus aux (10 slot ciascuno), 2 gruppi VCA, 64 pattern, da 1 a 128 tracce di playlist, una linea del
  tempo di almeno 32 pattern e 64 battute, 4 corsie di automazione, 64 clip di automazione di al massimo 256 punti, 32 voci simultanee.
- Gli effetti usano circa 15 MB di memoria a 48 kHz all'inizio, più circa 0,8 MB per ogni riverbero, delay, chorus e Pitch Corrector oltre
  il sedicesimo (di più a frequenze di campionamento maggiori). La memoria preparata resta finché il dispositivo audio non riparte.
- I pattern durano da 4 a 64 quarti (uguale per tutti; 2 o 3 con un metro corto); un solo metro per tutto il brano; fino a 256 cambi di tempo, a gradino, suonati solo in modalità Song; le clip audio non li seguono.
- I sample sono mono o stereo, al massimo 30 secondi. Il sampler ha un inviluppo e un filtro per canale (niente inviluppo del filtro, LFO o
  modulazione); al massimo quattro zone suonano per nota. Clip audio: fino a 5 minuti per file, 64 clip.
- Il synth: un LFO e un filtro per canale, due oscillatori, niente unisono, ancora niente motore additivo, la velocity non modula gli
  inviluppi; l'LFO riparte a ogni nota. Il motore FM ha quattro operatori, solo sinusoidi, nessun filtro, e la sua matrice è calcolata con un
  ritardo di un campione (come nella maggior parte dei synth FM a matrice).
- Plugin: VST3, LV2 e LADSPA (niente VST2, niente Audio Unit); stereo in ingresso e in uscita (uscite extra e ingressi sidechain non sono usati);
  i loro parametri si possono automatizzare (0.35.0); le modifiche fatte nella finestra di un plugin si salvano col progetto ma non entrano nell'annulla.
- Non ancora disponibili: time-stretch dei campioni nel Sampler (si possono modificare solo le clip audio della playlist), automazione di pitch, swing,
  volume master, mute e solo, effetti per clip. Vedi [roadmap.md](roadmap.md) (in inglese).
