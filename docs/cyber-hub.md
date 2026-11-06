# The Cyber-Hub sounds and the start-up sound

Sun Loops Studio carries the **Cyber-Hub** sound theme by Hubert (30 system sounds, stereo Ogg Vorbis, from
<https://github.com/Hubert9204/Cyber-Hub>) and the start-up sound `intro.wav`. They are **built into the program** (nothing is
read from disk at run time) and are **used unchanged**: the program decodes them and plays them, it never edits them.

## Where to find them

- **Browser > Samples > Cyber-Hub**: the 30 sounds filed in *Hits and sub*, *Tones* and *Long and ambient*, plus
  *Sun Loops intro*. Drag one onto a channel of the Channel Rack (it becomes that channel's sample, played one-shot) or
  onto the Playlist (it becomes an audio clip on the track and beat under the mouse). Double-click loads it into the
  selected channel.
- **Browser > Drum Kits > Cyber-Hub Kit**: five sounds picked as a kit - Kick (*button-pressed*), Sub (*power-plug*), Hat
  (*audio-volume-change*), Clap (*trash-empty*) and Tom (*button-pressed-modifier*).
- The **demo project** that the program opens with is made of them (see below).
- In a saved project these samples are stored as `builtin:cyber:<name>`, so the project does not need the files.

## The listening analysis

I cannot hear sound, so "listening" here means **measuring** each file (length, strongest frequency, spectral
centroid = how bright it is, stereo or mono, how short and sharp the start is) and reading the results as a sound
designer would. It is an analysis, not an opinion about taste: judge with your own ears.

What it showed, and what it was used for:

- **Everything is mastered to full scale** (peaks at 0 dB). Used as they are, the sounds are loud: the demo keeps their
  channel volumes down.
- **Short and low (kick-like):** *button-pressed* (0.45 s, 65 Hz, centroid 117 Hz) is a clean low thump; *button-pressed-modifier*
  (0.30 s, 130 Hz) a higher thud. *power-plug* is a 1.1 s pure low hit and exactly mono. These are the kick, tom and sub of the kit.
- **Short and bright (hat-like):** *audio-volume-change* lasts 0.16 s and is almost all noise (centroid 7.7 kHz): the hat.
- **Noisy bursts (clap/snare-like):** *trash-empty* (1.5 s, crunchy, centroid 1.2 kHz) is the clap in the kit; it rings
  longer than a normal clap, so it is mixed quietly.
- **Pitched tones, 0.8 - 1.5 s:** the *completion-*, *outcome-*, *dialog-error* sounds have a clear fundamental (about 200 - 400 Hz);
  they work as stingers and, pitched with the channel's Pitch knob, as short melodic hits. *dialog-question* (1 kHz)
  and *service-login* (1 kHz) are the highest.
- **Long beds, 3 s and more:** *desktop-login* (5.5 s) and *desktop-logout* (5.4 s) start from a sub bass (about 30 - 50 Hz) and
  grow bright: risers and swells. *alarm-clock-elapsed*, *message-new-instant*, *phone-incoming-call* (8.4 s) and
  *service-login / -logout* are ringing or ambient material that suits a Playlist audio clip better than a channel.

| Sound | Length | Strongest frequency | Centroid | Filed under |
| --- | --- | --- | --- | --- |
| `alarm-clock-elapsed` | 3.30 s | 245 Hz | 2760 Hz | Long and ambient |
| `audio-volume-change` | 0.16 s | 219 Hz | 7688 Hz | Hits and sub |
| `battery-caution` | 1.21 s | 833 Hz | 1510 Hz | Hits and sub |
| `battery-low` | 1.51 s | 52 Hz | 1144 Hz | Tones |
| `bell-window-system` | 0.60 s | 402 Hz | 1367 Hz | Hits and sub |
| `button-pressed-modifier` | 0.30 s | 130 Hz | 200 Hz | Hits and sub |
| `button-pressed` | 0.45 s | 65 Hz | 117 Hz | Hits and sub |
| `completion-fail` | 1.00 s | 204 Hz | 208 Hz | Tones |
| `completion-partial` | 0.98 s | 202 Hz | 228 Hz | Tones |
| `completion-rotation` | 1.10 s | 254 Hz | 500 Hz | Tones |
| `completion-success` | 0.91 s | 255 Hz | 900 Hz | Tones |
| `desktop-login` | 5.51 s | 53 Hz | 875 Hz | Long and ambient |
| `desktop-logout` | 5.41 s | 31 Hz | 1035 Hz | Long and ambient |
| `device-added` | 1.06 s | 262 Hz | 2039 Hz | Hits and sub |
| `device-removed` | 0.99 s | 66 Hz | 1684 Hz | Hits and sub |
| `dialog-error-serious` | 0.82 s | 126 Hz | 1844 Hz | Tones |
| `dialog-error` | 0.90 s | 253 Hz | 2963 Hz | Tones |
| `dialog-information` | 0.56 s | 601 Hz | 2266 Hz | Hits and sub |
| `dialog-question` | 0.42 s | 1054 Hz | 856 Hz | Hits and sub |
| `dialog-warning-auth` | 1.20 s | 401 Hz | 4217 Hz | Hits and sub |
| `dialog-warning` | 0.54 s | 201 Hz | 1769 Hz | Hits and sub |
| `message-new-instant` | 4.22 s | 523 Hz | 1052 Hz | Long and ambient |
| `outcome-failure` | 1.00 s | 239 Hz | 2793 Hz | Tones |
| `outcome-success` | 1.36 s | 402 Hz | 2003 Hz | Tones |
| `phone-incoming-call` | 8.40 s | 522 Hz | 1343 Hz | Long and ambient |
| `power-plug` | 1.14 s | 66 Hz | 398 Hz | Hits and sub |
| `power-unplug` | 1.12 s | 202 Hz | 215 Hz | Hits and sub |
| `service-login` | 4.67 s | 1047 Hz | 1125 Hz | Long and ambient |
| `service-logout` | 3.91 s | 263 Hz | 449 Hz | Long and ambient |
| `trash-empty` | 1.51 s | 53 Hz | 1182 Hz | Hits and sub |

(The table was measured on a decoded copy of each file; "strongest frequency" is the highest peak of a whole-file spectrum, so for
sounds that glide it is only a hint.)

## The new demo

The program opens with a demo made only of these sounds, at 112 BPM: *Lead* and *Bass* are the built-in synth; the
drums are Cyber-Hub sounds (Kick = *button-pressed*, Clap = *trash-empty*, Hat = *audio-volume-change*, Tom = *button-pressed-modifier*), all at
their own pitch. The playlist is *Drums*, *Pattern 1* (two times) and *Drums*, with **Desktop login** on playlist track 8 at the
start and **Desktop logout** on the same track where the last *Drums* clip starts, both as audio clips you can move, trim, split or delete.
Patterns are called "Pattern 1" and "Drums" as before. In a played song the master peaks a couple of dB under full scale.

## The start-up sound

When the program opens (and the audio device starts) it plays `intro.wav` once, straight to the output, at the loudness
of the file: the sound is added **after** the master volume, so the Master slider does not change it. It is independent of the transport, the mixer and the project. Switch it off in **File > Play the start-up sound**
(on by default; the choice is remembered).

## Licence note

The theme's own `index.theme` says *Creative Commons Attribution-NoDerivatives 4.0 International*, while the
Cyber-Hub repository's `LICENSE` file is CC0. The files are used **unchanged** and credited ("Cyber-Hub sound theme by Hubert,
CC BY-ND 4.0 (used unchanged)", shown in the browser), which satisfies the stricter of the two. The rest of Sun Loops
Studio is GPLv3 (MIT up to 0.16.0). Because the author is the same person, this can be settled by one line in the Cyber-Hub repository.
